#include "oaFileParser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <map>

using namespace std;

/*
 Simple emitter that scans selected table IDs for 8-byte pair records
 (instanceId -> masterId). Outputs deterministic JSON mapping observed
 records including file offsets so they can be correlated across versions.

 Usage: ./emit_instances /path/to/sch.oa > instances.json
*/

static inline string to_hex(unsigned long v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setw(sizeof(unsigned long)*2) << std::setfill('0') << v;
    return ss.str();
}

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s /path/to/sch.oa\n", argv[0]);
        return 2;
    }

    const char* path = argv[1];
    FILE *f = fopen(path, "rb");
    if(!f) {
        perror("fopen");
        return 3;
    }

    // Read file header (reuse struct from oaFileParser.h)
    oafp::fileHeader fh;
    if(fread(&fh, sizeof(fh), 1, f) != 1) {
        fprintf(stderr, "Failed to read file header\n");
        fclose(f);
        return 4;
    }

    unsigned long used = fh.used;
    if(used == 0) {
        fprintf(stderr, "No tables reported in header\n");
        fclose(f);
        return 0;
    }

    vector<unsigned long> ids(used);
    vector<unsigned long> offsets(used);
    vector<unsigned long> sizes(used);

    if(fread(ids.data(), sizeof(ids[0]) * used, 1, f) != 1) {
        fprintf(stderr, "Failed to read ids\n");
        fclose(f);
        return 5;
    }
    if(fread(offsets.data(), sizeof(offsets[0]) * used, 1, f) != 1) {
        fprintf(stderr, "Failed to read offsets\n");
        fclose(f);
        return 6;
    }
    if(fread(sizes.data(), sizeof(sizes[0]) * used, 1, f) != 1) {
        fprintf(stderr, "Failed to read sizes\n");
        fclose(f);
        return 7;
    }

    // Determine startOffset used in oaFileParser: find id==1
    unsigned long startOffset = 0;
    for(unsigned long i=0;i<used;++i) {
        if(ids[i] == 1) { startOffset = offsets[i]; break; }
    }

    // table IDs of interest (instance-like)
    vector<unsigned long> targetIds = { 0x0b, 0x0101, 0x101, 0x105, 0x107 };

    // Data structure to emit: map tableId -> list of records
    struct Record {
        unsigned long table_index;
        unsigned long record_index;
        unsigned long instance_value;
        unsigned long master_value;
        unsigned long file_offset;
    };

    map<unsigned long, vector<Record>> emitted;

    for(unsigned long t=0; t<used; ++t) {
        unsigned long tid = ids[t];
        bool interest = false;
        for(auto x : targetIds) if(x == tid) { interest = true; break; }
        if(!interest) continue;

        // Determine whether this is an index or non-index table.
        // Based on oaFileParser.cpp: index items used startOffset + offsets[i] for small ids.
        // For the known instance tables (0x0b, 0x0101) parse used raw offsets (non-index).
        unsigned long tablePos = offsets[t];
        unsigned long tableSize = sizes[t];

        // Safety: if tableSize is less than header of tableIndex, skip
        if(tableSize < sizeof(oafp::tableIndex)) {
            fprintf(stderr, "Table 0x%lx at index %lu has size %lu too small for header - skipping\n",
                    tid, t, tableSize);
            continue;
        }

        unsigned long filePos = tablePos;
        // read table header
        if(fseek(f, filePos, SEEK_SET) != 0) {
            perror("fseek");
            continue;
        }

        oafp::tableIndex table;
        if(fread(&table, sizeof(table), 1, f) != 1) {
            fprintf(stderr, "Failed to read tableIndex for table 0x%lx\n", tid);
            continue;
        }

        unsigned long dataStart = filePos + sizeof(table);
        unsigned long dataSize = (tableSize > sizeof(table)) ? (tableSize - sizeof(table)) : 0;

        if(dataSize == 0) {
            // nothing to parse
            continue;
        }

        // Try to parse as 8-byte records (instanceId, masterId)
        if(dataSize % 8 == 0) {
            unsigned long numRecords = dataSize / 8;
            if(fseek(f, dataStart, SEEK_SET) != 0) { perror("fseek"); continue; }

            for(unsigned long r=0; r<numRecords; ++r) {
                unsigned long instanceVal = 0, masterVal = 0;
                if(fread(&instanceVal, sizeof(instanceVal), 1, f) != 1) { break; }
                if(fread(&masterVal, sizeof(masterVal), 1, f) != 1) { break; }

                Record rec;
                rec.table_index = t;
                rec.record_index = r;
                rec.instance_value = instanceVal;
                rec.master_value = masterVal;
                rec.file_offset = dataStart + r*8;

                emitted[tid].push_back(rec);
            }
        } else {
            // Not clean 8-byte records. Try heuristic scan: look for repeating pairs or plausible master ids.
            // Read the whole data area and try to find 8-byte aligned pairs of small values.
            vector<unsigned char> buf(dataSize);
            if(fseek(f, dataStart, SEEK_SET) != 0) { perror("fseek"); continue; }
            if(fread(buf.data(), 1, dataSize, f) != dataSize) {
                // fallback: partial read
            }
            // heuristic: scan at each offset aligned to 4 for potential instance/master pairs
            for(unsigned long p = 0; p + 7 < dataSize; p += 4) {
                unsigned long a = 0, b = 0;
                memcpy(&a, &buf[p], 4);
                memcpy(&b, &buf[p+4], 4);
                // filter out nonsense: both zero => skip
                if(a==0 && b==0) continue;
                // restrict to relatively small values or plausible offsets
                // allow many values, we'll emit for manual inspection
                Record rec;
                rec.table_index = t;
                rec.record_index = p / 8; // best-effort
                rec.instance_value = a;
                rec.master_value = b;
                rec.file_offset = dataStart + p;
                emitted[tid].push_back(rec);
            }
        }
    }

    // Emit JSON to stdout
    cout << "{\n";
    bool firstTable = true;
    for(auto &kv : emitted) {
        if(!firstTable) cout << ",\n";
        firstTable = false;
        unsigned long tableId = kv.first;
        cout << "  \"" << to_hex(tableId) << "\": [\n";
        bool firstRec = true;
        for(auto &r : kv.second) {
            if(!firstRec) cout << ",\n";
            firstRec = false;
            cout << "    {\n";
            cout << "      \"table_index\": " << r.table_index << ",\n";
            cout << "      \"record_index\": " << r.record_index << ",\n";
            cout << "      \"instance_value\": " << r.instance_value << ",\n";
            cout << "      \"master_value\": " << r.master_value << ",\n";
            cout << "      \"file_offset\": \"" << to_hex(r.file_offset) << "\"\n";
            cout << "    }";
        }
        cout << "\n  ]";
    }
    cout << "\n}\n";

    fclose(f);
    return 0;
}