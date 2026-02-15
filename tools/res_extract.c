/*
 * Resource Fork Extractor for Reckless Drivin'
 *
 * Parses a Mac resource fork (the "Data" file) and extracts resources
 * to individual files in the assets/ directory.
 *
 * Mac Resource Fork Format:
 * Header (offset 0):
 *   UInt32 dataOffset    - offset to resource data section
 *   UInt32 mapOffset     - offset to resource map
 *   UInt32 dataLength    - length of resource data section
 *   UInt32 mapLength     - length of resource map
 *
 * Resource Data Section:
 *   Each resource: UInt32 length, then length bytes of data
 *
 * Resource Map:
 *   16 bytes: copy of resource header
 *   4 bytes: next map handle (skip)
 *   2 bytes: file reference (skip)
 *   2 bytes: resource file attributes
 *   2 bytes: offset to type list (from start of map)
 *   2 bytes: offset to name list (from start of map)
 *
 * Type List (at mapOffset + typeListOffset):
 *   UInt16 numTypes-1 (0-based count)
 *   For each type:
 *     UInt32 type (4-char code)
 *     UInt16 numResources-1
 *     UInt16 refListOffset (from start of type list)
 *
 * Reference List (at typeListStart + refListOffset):
 *   For each resource:
 *     SInt16 id
 *     SInt16 nameOffset (from name list start, -1 if none)
 *     UInt8  attributes
 *     UInt8[3] dataOffset (24-bit offset from data section start)
 *     UInt32 reserved
 *
 * All values are big-endian.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755)
#endif

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static int16_t read_s16(const uint8_t *p) {
    return (int16_t)read_u16(p);
}

static uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t read_u24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static void type_to_str(uint32_t type, char *out) {
    out[0] = (char)((type >> 24) & 0xFF);
    out[1] = (char)((type >> 16) & 0xFF);
    out[2] = (char)((type >> 8) & 0xFF);
    out[3] = (char)(type & 0xFF);
    out[4] = '\0';
}

/* Make a safe filename from a type string (replace non-alnum with _) */
static void type_to_filename(uint32_t type, char *out) {
    int i;
    type_to_str(type, out);
    for (i = 0; i < 4; i++) {
        char c = out[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            out[i] = '_';
    }
    /* Trim trailing underscores */
    for (i = 3; i >= 0 && out[i] == '_'; i--)
        out[i] = '\0';
}

int main(int argc, char *argv[])
{
    FILE *f;
    uint8_t *data;
    long fileSize;
    uint32_t dataOffset, mapOffset, dataLength, mapLength;
    uint8_t *map;
    uint16_t typeListOffset, nameListOffset;
    uint8_t *typeList;
    uint16_t numTypes;
    int ti;
    const char *dataFile;
    const char *assetsDir;
    int totalExtracted = 0;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <Data file> <assets directory>\n", argv[0]);
        return 1;
    }

    dataFile = argv[1];
    assetsDir = argv[2];

    /* Read entire file */
    f = fopen(dataFile, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open '%s'\n", dataFile);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t*)malloc(fileSize);
    if (!data) { fclose(f); return 1; }
    fread(data, 1, fileSize, f);
    fclose(f);

    /* Parse header */
    if (fileSize < 16) {
        fprintf(stderr, "File too small for resource fork header\n");
        free(data);
        return 1;
    }
    dataOffset = read_u32(data + 0);
    mapOffset = read_u32(data + 4);
    dataLength = read_u32(data + 8);
    mapLength = read_u32(data + 12);

    printf("Resource fork: dataOffset=%u, mapOffset=%u, dataLength=%u, mapLength=%u\n",
           dataOffset, mapOffset, dataLength, mapLength);

    if (mapOffset + mapLength > (uint32_t)fileSize) {
        fprintf(stderr, "Resource map extends beyond file\n");
        free(data);
        return 1;
    }

    /* Create assets directory */
    MKDIR(assetsDir);

    /* Parse resource map */
    map = data + mapOffset;
    /* Skip: 16 bytes header copy, 4 bytes next map, 2 bytes fileRef, 2 bytes attrs */
    typeListOffset = read_u16(map + 24);
    nameListOffset = read_u16(map + 26);

    typeList = map + typeListOffset;
    numTypes = read_u16(typeList) + 1; /* stored as count-1 */

    printf("Number of resource types: %u\n", numTypes);

    for (ti = 0; ti < numTypes; ti++) {
        uint8_t *typeEntry = typeList + 2 + ti * 8;
        uint32_t resType = read_u32(typeEntry);
        uint16_t numResources = read_u16(typeEntry + 4) + 1; /* stored as count-1 */
        uint16_t refListOffset = read_u16(typeEntry + 6);
        uint8_t *refList = typeList + refListOffset;
        char typeStr[8];
        char typeFname[8];
        int ri;

        type_to_str(resType, typeStr);
        type_to_filename(resType, typeFname);

        printf("  Type '%.4s': %u resources\n", typeStr, numResources);

        for (ri = 0; ri < numResources; ri++) {
            uint8_t *ref = refList + ri * 12;
            int16_t resID = read_s16(ref);
            /* int16_t nameOff = read_s16(ref + 2); */
            uint8_t attrs = ref[4];
            uint32_t resDataOffset = read_u24(ref + 5);
            uint32_t absDataOffset = dataOffset + resDataOffset;
            uint32_t resLength;
            char outPath[512];
            FILE *outFile;

            (void)attrs;

            if (absDataOffset + 4 > (uint32_t)fileSize) {
                fprintf(stderr, "    Resource %.4s %d: data offset out of range\n", typeStr, resID);
                continue;
            }

            resLength = read_u32(data + absDataOffset);
            absDataOffset += 4; /* skip the length field */

            if (absDataOffset + resLength > (uint32_t)fileSize) {
                fprintf(stderr, "    Resource %.4s %d: data extends beyond file (%u bytes at %u)\n",
                        typeStr, resID, resLength, absDataOffset);
                continue;
            }

            /* Build output filename: lowercase type + underscore + ID */
            {
                char lowerType[8];
                int k;
                strncpy(lowerType, typeFname, sizeof(lowerType));
                for (k = 0; lowerType[k]; k++)
                    if (lowerType[k] >= 'A' && lowerType[k] <= 'Z')
                        lowerType[k] += 32;
                snprintf(outPath, sizeof(outPath), "%s/%s_%d.bin", assetsDir, lowerType, resID);
            }

            outFile = fopen(outPath, "wb");
            if (!outFile) {
                fprintf(stderr, "    Cannot create '%s'\n", outPath);
                continue;
            }
            fwrite(data + absDataOffset, 1, resLength, outFile);
            fclose(outFile);
            totalExtracted++;
        }
    }

    printf("\nExtracted %d resources to '%s'\n", totalExtracted, assetsDir);
    free(data);
    return 0;
}
