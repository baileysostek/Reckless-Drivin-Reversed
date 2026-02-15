/*
 * Quick diagnostic: decompress a PPic file and dump the PICT header + first opcodes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../libs/lzrw/lzrw3a.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

int main(int argc, char **argv)
{
    FILE *f;
    long fileSize;
    uint8_t *fileData, *decompData, *wrk_mem;
    uint32_t uncompSize;
    uint64_t dst_len;
    struct compress_identity id;
    const uint8_t *p, *end;
    int i;

    if (argc < 2) { fprintf(stderr, "Usage: ppic_dump <file.bin>\n"); return 1; }

    f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "Can't open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); fileSize = ftell(f); fseek(f, 0, SEEK_SET);
    fileData = malloc(fileSize);
    fread(fileData, 1, fileSize, f);
    fclose(f);

    uncompSize = rd32(fileData);
    printf("File size: %ld, uncompressed size: %u\n", fileSize, uncompSize);

    decompData = malloc(uncompSize + 1024);
    id = lzrw_identity();
    wrk_mem = malloc(id.memory);
    dst_len = 0;
    lzrw3a_compress(COMPRESS_ACTION_DECOMPRESS, wrk_mem,
                    fileData + 4, (uint32_t)(fileSize - 4),
                    decompData, &dst_len);
    free(wrk_mem);
    free(fileData);
    printf("Decompressed: %llu bytes\n\n", (unsigned long long)dst_len);

    /* Dump first 128 bytes as hex */
    printf("First 128 bytes of PICT data:\n");
    for (i = 0; i < 128 && i < (int)dst_len; i++) {
        printf("%02X ", decompData[i]);
        if ((i & 15) == 15) printf("\n");
    }
    printf("\n\n");

    /* Parse PICT header */
    p = decompData;
    end = decompData + dst_len;
    printf("Size word: 0x%04X\n", rd16(p)); p += 2;
    printf("picFrame: top=%d left=%d bottom=%d right=%d\n",
           (int16_t)rd16(p), (int16_t)rd16(p+2), (int16_t)rd16(p+4), (int16_t)rd16(p+6));
    p += 8;

    /* Read opcodes */
    printf("\nOpcodes:\n");
    for (i = 0; i < 30 && p + 2 <= end; i++) {
        uint16_t op = rd16(p); p += 2;
        printf("  [offset %ld] opcode 0x%04X", (long)(p - 2 - decompData), op);

        if (op == 0x0000) { printf(" (NOP)\n"); continue; }
        if (op == 0x00FF) { printf(" (EndOfPicture)\n"); break; }
        if (op == 0x0011) {
            uint16_t ver = rd16(p); p += 2;
            printf(" (Version = 0x%04X)\n", ver);
            continue;
        }
        if (op == 0x0C00) {
            printf(" (HeaderOp, skipping 24 bytes)\n");
            /* dump them */
            {
                int j;
                printf("    HeaderOp data: ");
                for (j = 0; j < 24 && p + j < end; j++) printf("%02X ", p[j]);
                printf("\n");
            }
            p += 24;
            continue;
        }
        if (op == 0x0001) {
            uint16_t rgnSize = rd16(p); p += 2;
            printf(" (Clip, rgnSize=%d)\n", rgnSize);
            if (rgnSize > 2) p += rgnSize - 2;
            continue;
        }
        if (op == 0x001E) { printf(" (DefHilite)\n"); continue; }
        if (op == 0x000A) { printf(" (FillRgn, skipping 8)\n"); p += 8; continue; }
        if (op == 0x0098) {
            uint16_t rb = rd16(p);
            printf(" (PackBitsRect, rowBytes=0x%04X [%d], isPixMap=%d)\n",
                   rb, rb & 0x3FFF, (rb & 0x8000) != 0);
            /* Don't skip, just show first few bytes */
            {
                int j;
                printf("    Next 50 bytes: ");
                for (j = 0; j < 50 && p + j < end; j++) printf("%02X ", p[j]);
                printf("\n");
            }
            break; /* stop after first draw opcode */
        }
        if (op == 0x009A) {
            printf(" (DirectBitsRect)\n");
            {
                int j;
                printf("    Next 50 bytes: ");
                for (j = 0; j < 50 && p + j < end; j++) printf("%02X ", p[j]);
                printf("\n");
            }
            break;
        }
        if (op == 0x00A1) {
            uint16_t kind = rd16(p); p += 2;
            uint16_t len = rd16(p); p += 2;
            printf(" (LongComment kind=%d len=%d)\n", kind, len);
            p += len;
            continue;
        }
        printf(" (unknown)\n");
        break;
    }

    free(decompData);
    return 0;
}
