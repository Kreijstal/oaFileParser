/*
 * MIT License
 *
 * Copyright (c) 2017 EDDR Software, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Changes:
 * 2017-01-01: First & Last Name: What you did.
 * 2017-05-28: Kevin Nesmith: Initial contribution.
 *
 */

#include "oaFileParser.h"
#include <cstring>

namespace oafp
{
    unsigned long roundAlign8Bit(unsigned long len)
    {
        unsigned long rem = len%8;
        return len + (8 - rem);
    }

    void oaFileParser::read0x04(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        unsigned int flags = 0;
        unsigned int in = 0;
        in = fread(&flags, sizeof(flags), 1, file);
        onParsedFlags(flags);
    }

    void oaFileParser::read0x05(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        unsigned int timeStamp;
        unsigned int in = 0;
        in = fread(&timeStamp, sizeof(timeStamp), 1, file);
        onParsedTimeStamp(timeStamp);
    }

    void oaFileParser::read0x06(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        unsigned long lsTime;
        unsigned int in = 0;
        in = fread(&lsTime, sizeof(lsTime), 1, file);
        onParsedLastSavedTime(lsTime);
    }

    void oaFileParser::read0x07(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        unsigned int numRes;
        unsigned int numData;
        unsigned int in = 0;
        in = fread(&numRes, sizeof(numRes), 1, file);
        in = fread(&numData, sizeof(numData), 1, file);
        unsigned int numOther = numData - numRes;
        unsigned long ids[numRes];
        unsigned int types[numRes];
        unsigned long tblIds[numOther];
        unsigned int tblTypes[numOther];
        in = fread(&ids[0], sizeof(ids[0]) * numRes, 1, file);
        in = fread(&types[0], sizeof(types[0]) * numRes, 1, file);
        in = fread(&tblIds[0], sizeof(tblIds[0]) * numOther, 1, file);
        in = fread(&tblTypes[0], sizeof(tblTypes[0]) * numOther, 1, file);
        onParsedDatabaseMap(ids, types, numRes, tblIds, tblTypes, numOther);
    }

    void oaFileParser::read0x0a(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        char buffer[tblSize];
        tableIndex table;
        unsigned int empty;
        unsigned int in = 0;
        in = fread(&table, sizeof(table), 1, file);
        in = fread(&empty, sizeof(empty), 1, file);
        in = fread(&buffer, sizeof(buffer), 1, file);
        onParsedStringTable(table, buffer);
    }

    void oaFileParser::read0x0b(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        
        // Read table header
        tableIndex table;
        unsigned int in = 0;
        in = fread(&table, sizeof(table), 1, file);
        
        printf("Instance Table 0x0b at 0x%lx: pos=0x%lx size=%lu used=%u deleted=%u first=%u\n", 
               pos, pos, tblSize, table.used, table.deleted, table.first);
        
        // Calculate data section
        unsigned long dataStart = pos + sizeof(table);
        unsigned long dataSize = tblSize - sizeof(table);
        
        // Try 8-byte records (two 4-byte IDs: instance ID -> master ID)
        if (dataSize % 8 == 0) {
            unsigned long numRecords = dataSize / 8;
            printf("Trying 8-byte records: %lu records\n", numRecords);
            
            unsigned long *instanceIds = new unsigned long[numRecords];
            unsigned long *masterIds = new unsigned long[numRecords];
            
            fseek(file, dataStart, SEEK_SET);
            for(unsigned long i = 0; i < numRecords; ++i) {
                in = fread(&instanceIds[i], sizeof(unsigned long), 1, file);
                in = fread(&masterIds[i], sizeof(unsigned long), 1, file);
                printf("  Record %lu: Instance ID 0x%08lx -> Master ID 0x%08lx\n", i, instanceIds[i], masterIds[i]);
            }
            
            // Call the virtual method with parsed data
            onParsedInstanceTable(numRecords, instanceIds, masterIds);
            
            delete[] instanceIds;
            delete[] masterIds;
        } else {
            printf("Data size %lu is not divisible by 8, cannot parse as 8-byte records\n", dataSize);
        }
        
        // Also do ASCII extraction as fallback for debugging
        fseek(file, pos, SEEK_SET);
        char buffer[tblSize];
        in = fread(&buffer, sizeof(buffer), 1, file);
        
        printf("ASCII extraction:\n");
        unsigned long i = 0;
        unsigned long runStart = 0;
        bool inRun = false;
        const int MIN_RUN = 3;
        printf("\tStrings: ");
        while(i < (unsigned long)tblSize) {
            unsigned char c = (unsigned char)buffer[i];
            if(c >= 32 && c < 127) {
                if(!inRun) { runStart = i; inRun = true; }
            } else {
                if(inRun) {
                    unsigned long runLen = i - runStart;
                    if((int)runLen >= MIN_RUN) {
                        for(unsigned long k = runStart; k < i; ++k) putchar(buffer[k]);
                        putchar('|');
                    }
                    inRun = false;
                }
            }
            ++i;
        }
        if(inRun) {
            unsigned long runLen = i - runStart;
            if((int)runLen >= MIN_RUN) {
                for(unsigned long k = runStart; k < i; ++k) putchar(buffer[k]);
                putchar('|');
            }
        }
        putchar('\n');
    }
 
void oaFileParser::read0x0c(FILE *file, unsigned long pos,
                            unsigned long tblSize)
{
    fseek(file, pos, SEEK_SET);
    tableIndex table;
    unsigned int in = 0;
    in = fread(&table, sizeof(table), 1, file);

    printf("Table 0x0c (deterministic extractor): pos=0x%lx size=%lu used=%u deleted=%u first=%u\n",
           pos, tblSize, table.used, table.deleted, table.first);

    unsigned long dataStart = pos + sizeof(table);
    unsigned long dataSize = (tblSize > sizeof(table)) ? (tblSize - sizeof(table)) : 0;

    if (dataSize == 0) {
        printf("Table 0x0c has no data section\n");
        return;
    }

    unsigned char *buffer = new unsigned char[dataSize];
    fseek(file, dataStart, SEEK_SET);
    in = fread(buffer, 1, dataSize, file);

    printf("Data size: %lu\n", dataSize);

    const unsigned short target16 = 222; // exact string-index for "M0"
    const unsigned char tlo = (unsigned char)(target16 & 0xff);
    const unsigned char thi = (unsigned char)((target16 >> 8) & 0xff);

    // Open deterministic JSON output file (overwrites if exists).
    FILE *out = fopen("aaic/m0_properties_M0.json", "w");
    if (out == NULL) {
        printf("Warning: could not open aaic/m0_properties_M0.json for writing\n");
    } else {
        fprintf(out, "[\n");
    }

    bool firstRecord = true;

    // Helper to emit a JSON record for a found match.
    auto emit_record = [&](unsigned long matchTableOffset, bool isFourByteMatch){
        if (out == NULL) return;
        if (!firstRecord) fprintf(out, ",\n");
        fprintf(out, "  {\n");
        fprintf(out, "    \"file_offset\": \"%#lx\",\n", dataStart + matchTableOffset);
        fprintf(out, "    \"table_offset\": \"%#lx\",\n", matchTableOffset);
        fprintf(out, "    \"match_width_bytes\": %d,\n", isFourByteMatch ? 4 : 2);

        // Two-byte LE indices sequence (up to 32 entries) starting at match
        fprintf(out, "    \"two_byte_indices\": [");
        unsigned long seqMax2 = (matchTableOffset + 2*32 <= dataSize) ? (matchTableOffset + 2*32) : dataSize;
        bool first = true;
        for (unsigned long p = matchTableOffset; p + 1 < seqMax2; p += 2) {
            unsigned short v = (unsigned short)(buffer[p] | (buffer[p+1] << 8));
            if (!first) fprintf(out, ", ");
            fprintf(out, "%u", (unsigned int)v);
            first = false;
        }
        fprintf(out, "],\n");

        // Four-byte LE values sequence (up to 16 entries) starting at match
        fprintf(out, "    \"four_byte_values\": [");
        unsigned long seqMax4 = (matchTableOffset + 4*16 <= dataSize) ? (matchTableOffset + 4*16) : dataSize;
        first = true;
        for (unsigned long p = matchTableOffset; p + 3 < seqMax4; p += 4) {
            unsigned long v = 0;
            memcpy(&v, &buffer[p], 4);
            if (!first) fprintf(out, ", ");
            fprintf(out, "%lu", v);
            first = false;
        }
        fprintf(out, "]\n");

        fprintf(out, "  }");
        firstRecord = false;
    };

    // Scan for exact 2-byte little-endian occurrences.
    for (unsigned long i = 0; i + 1 < dataSize; ++i) {
        if (buffer[i] == tlo && buffer[i+1] == thi) {
            unsigned long abs = dataStart + i;
            printf("Found exact 2-byte match for index %u at file_offset=0x%lx (table offset 0x%lx)\n",
                   (unsigned int)target16, abs, i);

            // Emit deterministic record using only explicit bytes
            emit_record(i, false);
        }
    }

    // Scan for exact 4-byte little-endian occurrences (zero-extended)
    unsigned char t32[4] = { tlo, thi, 0x00, 0x00 };
    for (unsigned long i = 0; i + 3 < dataSize; ++i) {
        if (buffer[i] == t32[0] && buffer[i+1] == t32[1] && buffer[i+2] == t32[2] && buffer[i+3] == t32[3]) {
            unsigned long abs = dataStart + i;
            printf("Found exact 4-byte zero-extended match for index %u at file_offset=0x%lx (table offset 0x%lx)\n",
                   (unsigned int)target16, abs, i);

            // Emit deterministic record using only explicit bytes
            emit_record(i, true);
        }
    }

    if (out != NULL) {
        fprintf(out, "\n]\n");
        fclose(out);
        printf("Deterministic extraction written to aaic/m0_properties_M0.json\n");
    }

    delete[] buffer;
}
/*
 * Strict parser for table 0x0c: locate exact little-endian occurrences of
 * a 16-bit string-table index (222 decimal) and print raw records without
 * heuristics. This emits only exact numeric indices and raw hex/ASCII for
 * manual inspection (no attempt to guess key/value pairs).
 */

    void oaFileParser::read0x19(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        unsigned long createTime;
        unsigned int in = 0;
        in = fread(&createTime, sizeof(createTime), 1, file);
        onParsedCreateTime(createTime);
    }

    void oaFileParser::read0x1c(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        unsigned short dataModelRev;
        char buildName[tblSize - sizeof(dataModelRev)];
        unsigned int in = 0;
        in = fread(&dataModelRev, sizeof(dataModelRev), 1, file);
        in = fread(&buildName, sizeof(buildName), 1, file);
        onParsedDMandBuildName(dataModelRev, buildName);
    }

    void oaFileParser::read0x1d(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        appInfo ai;
        char buffer[tblSize - (sizeof(unsigned short)*4)];
        char *appBuildName;
        char *kitBuildName;
        char *platforName;
        unsigned int in = 0;
        in = fread(&ai, sizeof(ai), 1, file);
        in = fread(&buffer, sizeof(buffer), 1, file);
        unsigned int b = 0;
        appBuildName = &buffer[b];
        b += roundAlign8Bit(strlen(appBuildName));
        kitBuildName = &buffer[b];
        b += roundAlign8Bit(strlen(kitBuildName));
        platforName = &buffer[b];
        onParsedBuildInformation(ai.appDataModelRev, ai.kitDataModelRev,
                                 ai.appAPIMinorRev, ai.kitReleaseNum,
                                 appBuildName, kitBuildName, platforName);
    }

    void oaFileParser::read0x1f(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        unsigned long num;
        unsigned int in = 0;
        in = fread(&num, sizeof(num), 1, file);
        unsigned long ids[num];
        unsigned int types[num];
        in = fread(&ids[0], sizeof(ids[0]) * num, 1, file);
        in = fread(&types[0], sizeof(types[0]) * num, 1, file);
        onParsedDatabaseMapD(ids, types, num);
    }

    void oaFileParser::read0x28(FILE *file, unsigned long pos,
                                unsigned long tblSize)
    {
        fseek(file, pos, SEEK_SET);
        unsigned int bitCheck;
        unsigned int in = 0;
        in = fread(&bitCheck, sizeof(bitCheck), 1, file);
        onParsedDatabaseMarker(bitCheck);
    }

    int oaFileParser::parse(const char *filePath)
    {
        try {
            fileHeader fh;
            FILE *file = fopen(filePath, "r");

            if(file==NULL) {
                throw("File path does not exist.");
                return 1;
            }

            size_t in = fread(&fh, sizeof(fh), 1, file);
            onParsedPreface(fh.testBit, fh.type, fh.schema, fh.offset, fh.size, fh.used);

            unsigned long ids[fh.used];
            unsigned long offsets[fh.used];
            unsigned long sizes[fh.used];
            unsigned long startOffset = 0;
            in = fread(&ids[0], sizeof(ids[0]) * fh.used, 1, file);
            in = fread(&offsets[0], sizeof(offsets[0]) * fh.used, 1, file);
            in = fread(&sizes[0], sizeof(sizes[0]) * fh.used, 1, file);

            onParsedTableInformation(ids, offsets, sizes, fh.used);

            for(int i=0; i<fh.used; ++i) {
                if(ids[i]==1) {
                    startOffset = offsets[i];
                    break;
                }
            }

            for(int i=0; i<fh.used; ++i) {
                switch((int)ids[i]) {
                    //Index Items; Offset start from startOffset
                    case 0x04: read0x04(file, startOffset + offsets[i], sizes[i]);
                        break; //Flags??

                    case 0x05: read0x05(file, startOffset + offsets[i], sizes[i]);
                        break; //Time stamp??

                    case 0x06: read0x06(file, startOffset + offsets[i], sizes[i]);
                        break; //Last saved time

                    case 0x07: read0x07(file, startOffset + offsets[i], sizes[i]);
                        break; //File mapping

                    case 0x19: read0x19(file, startOffset + offsets[i], sizes[i]);
                        break; //Creation time

                    case 0x1c: read0x1c(file, startOffset + offsets[i], sizes[i]);
                        break; //DM information and version numbers

                    case 0x1d: read0x1d(file, startOffset + offsets[i], sizes[i]);
                        break; //Software build information

                    case 0x28: read0x28(file, startOffset + offsets[i], sizes[i]);
                        break; //End of database marker??

                    //Non-Index Items; Offset start from 0
                    case 0x0a: read0x0a(file, offsets[i], sizes[i]);
                        break; //String table
                    case 0x0b: read0x0b(file, offsets[i], sizes[i]);
                        break; //Instance table
                    case 0x0101: read0x0b(file, offsets[i], sizes[i]);
                        break; //Unknown table, try same parsing as instance table
                }
            }

            fclose(file);
        } catch(...) {
            onParsedError("Error: paring file.");
            return 1;
        }

        return 0;
    }
} //End namespace oafp
