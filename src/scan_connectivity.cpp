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
#include <cstdint>

using namespace std;

/*
 Connectivity scanner: finds candidate net records by locating two-byte string
 indices followed by sequences of 4-byte integers (candidate OIDs). Emits JSON.

 Usage: ./scan_connectivity /path/to/sch.oa > aaic/parsed_connectivity.json
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
    if(!f) { perror("fopen"); return 3; }

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

    if(fread(ids.data(), sizeof(ids[0]) * used, 1, f) != 1) { fprintf(stderr,"Failed to read ids\n"); fclose(f); return 5; }
    if(fread(offsets.data(), sizeof(offsets[0]) * used, 1, f) != 1) { fprintf(stderr,"Failed to read offsets\n"); fclose(f); return 6; }
    if(fread(sizes.data(), sizeof(sizes[0]) * used, 1, f) != 1) { fprintf(stderr,"Failed to read sizes\n"); fclose(f); return 7; }

    // find string table (0x0a)
    int stringTableIndex = -1;
    for(unsigned long i=0;i<used;++i) if(ids[i]==0x0a) { stringTableIndex = (int)i; break; }

    vector<string> string_by_offset;
    unsigned long string_table_used = 0;
    unsigned long string_table_data_start = 0;

    if(stringTableIndex >= 0) {
        unsigned long tablePos = offsets[stringTableIndex];
        unsigned long tableSize = sizes[stringTableIndex];
        if(tableSize >= sizeof(oafp::tableIndex)) {
            if(fseek(f, tablePos, SEEK_SET) != 0) { perror("fseek"); }
            oafp::tableIndex t;
            if(fread(&t, sizeof(t), 1, f) == 1) {
                string_table_used = t.used;
                string_table_data_start = tablePos + sizeof(t);
                unsigned long dataSize = (tableSize > sizeof(t)) ? (tableSize - sizeof(t)) : 0;
                if(dataSize > 0) {
                    vector<char> buf(dataSize);
                    if(fseek(f, string_table_data_start, SEEK_SET) == 0) {
                        size_t r = fread(buf.data(), 1, dataSize, f);
                        unsigned long p = 0;
                        while(p < r) {
                            // skip non-printable
                            if((unsigned char)buf[p] < 32) { ++p; continue; }
                            unsigned long start = p;
                            string s;
                            while(p < r && (unsigned char)buf[p] >= 32 && (unsigned char)buf[p] < 127) {
                                s.push_back(buf[p]);
                                ++p;
                            }
                            if(!s.empty()) {
                                if(string_by_offset.size() <= start) string_by_offset.resize(start+1);
                                string_by_offset[start] = s;
                            }
                        }
                    }
                }
            }
        }
    }

    auto lookup_string = [&](unsigned int off)->string {
        if(off < string_by_offset.size() && !string_by_offset[off].empty()) return string_by_offset[off];
        return string();
    };

    struct Candidate { unsigned long tableId; unsigned long tableIndex; unsigned long fileOffset; string name; vector<unsigned long> oids; };
    vector<Candidate> candidates;

    // scan each non-string table for occurrences of 2-byte string indices followed by 4-byte OIDs
    for(unsigned long t=0;t<used;++t) {
        if((int)t == stringTableIndex) continue;
        unsigned long tid = ids[t];
        unsigned long tablePos = offsets[t];
        unsigned long tableSize = sizes[t];
        if(tableSize < sizeof(oafp::tableIndex)) continue;

        if(fseek(f, tablePos, SEEK_SET) != 0) continue;
        oafp::tableIndex tbl;
        if(fread(&tbl, sizeof(tbl), 1, f) != 1) continue;
        unsigned long dataStart = tablePos + sizeof(tbl);
        unsigned long dataSize = (tableSize > sizeof(tbl)) ? (tableSize - sizeof(tbl)) : 0;
        if(dataSize == 0) continue;

        vector<unsigned char> buf(dataSize);
        if(fseek(f, dataStart, SEEK_SET) != 0) continue;
        size_t got = fread(buf.data(), 1, dataSize, f);
        if(got == 0) continue;

        for(unsigned long p=0; p+1 < got; ++p) {
            unsigned int idx = (unsigned int)(buf[p] | (buf[p+1] << 8));
            if(string_table_used == 0) break;
            if(idx >= string_table_used) continue;
            string name = lookup_string(idx);
            if(name.empty()) continue;

            // Look ahead for up to 16 4-byte little-endian integers as OIDs
            vector<unsigned long> oids;
            unsigned long q = p + 2;
            const unsigned MAX_OIDS = 16;
            unsigned count = 0;
            while(count < MAX_OIDS && q + 3 < got) {
                unsigned long v = 0;
                memcpy(&v, &buf[q], 4);
                // simple plausibility filter
                if(v == 0 || v > 1000000) break;
                oids.push_back(v);
                q += 4;
                ++count;
            }

            if(!oids.empty()) {
                Candidate c;
                c.tableId = tid;
                c.tableIndex = t;
                c.fileOffset = dataStart + p;
                c.name = name;
                c.oids = oids;
                candidates.push_back(c);
            }
        }
    }

    // Emit JSON
    cout << "{\n";
    cout << "  \"connectivity_candidates\": [\n";
    for(size_t i=0;i<candidates.size();++i) {
        auto &c = candidates[i];
        cout << "    {\n";
        cout << "      \"table_id\": \"" << to_hex(c.tableId) << "\",\n";
        cout << "      \"table_index\": " << c.tableIndex << ",\n";
        cout << "      \"file_offset\": \"" << to_hex(c.fileOffset) << "\",\n";
        cout << "      \"net_name\": ";
        cout << "\"";
        for(char ch : c.name) {
            if(ch=='\"') cout << "\\\"";
            else cout << ch;
        }
        cout << "\",\n";
        cout << "      \"oids\": [";
        for(size_t j=0;j<c.oids.size();++j) {
            if(j) cout << ", ";
            cout << c.oids[j];
        }
        cout << "]\n";
        cout << "    }";
        if(i+1 < candidates.size()) cout << ",\n";
        else cout << "\n";
    }
    cout << "  ]\n";
    cout << "}\n";

    fclose(f);
    return 0;
}