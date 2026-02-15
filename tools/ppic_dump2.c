#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../libs/lzrw/lzrw3a.h"
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

int main(int argc, char **argv) {
    FILE *f; long fileSize; uint8_t *fd, *dd, *wm;
    uint32_t us; uint64_t dl; struct compress_identity id;
    const uint8_t *p;
    int i;

    if (argc < 2) return 1;
    f = fopen(argv[1], "rb"); if (!f) return 1;
    fseek(f,0,SEEK_END); fileSize=ftell(f); fseek(f,0,SEEK_SET);
    fd=malloc(fileSize); fread(fd,1,fileSize,f); fclose(f);
    us=rd32(fd); dd=malloc(us+1024);
    id=lzrw_identity(); wm=malloc(id.memory); dl=0;
    lzrw3a_compress(COMPRESS_ACTION_DECOMPRESS, wm, fd+4, (uint32_t)(fileSize-4), dd, &dl);
    free(wm); free(fd);

    /* Skip to opcode area */
    p = dd;
    p += 2;  /* size word */
    printf("picFrame: %d %d %d %d\n", (int16_t)rd16(p), (int16_t)rd16(p+2), (int16_t)rd16(p+4), (int16_t)rd16(p+6));
    p += 8;

    /* Skip to opcode 0x0098 - just scan for it */
    while (p < dd + dl - 1) {
        uint16_t op = rd16(p);
        if (op == 0x0098) {
            p += 2; /* consume opcode */
            printf("Found PackBitsRect at offset %ld\n", (long)(p - 2 - dd));

            /* PixMap */
            uint16_t rowBytes = rd16(p); p += 2;
            int isPixMap = (rowBytes & 0x8000) != 0;
            int rbRaw = rowBytes & 0x3FFF;
            printf("rowBytes=0x%04X raw=%d isPixMap=%d\n", rowBytes, rbRaw, isPixMap);

            int16_t bTop=rd16(p), bLeft=rd16(p+2), bBot=rd16(p+4), bRight=rd16(p+6);
            p += 8;
            printf("bounds: %d %d %d %d\n", bTop, bLeft, bBot, bRight);

            if (isPixMap) {
                printf("pmVersion=%d packType=%d packSize=%u\n", rd16(p), rd16(p+2), rd32(p+4));
                p += 8; /* version, packType, packSize */
                p += 8; /* hRes, vRes */
                printf("pixelType=%d pixelSize=%d cmpCount=%d cmpSize=%d\n", rd16(p), rd16(p+2), rd16(p+4), rd16(p+6));
                int pixSize = rd16(p+2);
                p += 8; /* pixelType..cmpSize */
                p += 12; /* planeBytes, pmTable, pmReserved */

                /* Color table */
                uint32_t ctSeed = rd32(p); p += 4;
                uint16_t ctFlags = rd16(p); p += 2;
                uint16_t ctSize = rd16(p); p += 2;
                printf("ctSeed=%u ctFlags=0x%04X ctSize=%d (%d entries)\n", ctSeed, ctFlags, ctSize, ctSize+1);

                /* Show first 10 and last 5 color table entries */
                const uint8_t *ctStart = p;
                for (i = 0; i <= (int)ctSize && i < 256; i++) {
                    uint16_t idx = rd16(p);
                    uint16_t r = rd16(p+2);
                    uint16_t g = rd16(p+4);
                    uint16_t b = rd16(p+6);
                    p += 8;
                    if (i < 10 || i > (int)ctSize - 5)
                        printf("  clut[%d]: idx=%d r=%u g=%u b=%u\n", i, idx, r, g, b);
                    else if (i == 10)
                        printf("  ... (%d entries skipped) ...\n", ctSize - 14);
                }

                /* srcRect, dstRect, mode */
                printf("srcRect: %d %d %d %d\n", (int16_t)rd16(p), (int16_t)rd16(p+2), (int16_t)rd16(p+4), (int16_t)rd16(p+6));
                p += 8;
                printf("dstRect: %d %d %d %d\n", (int16_t)rd16(p), (int16_t)rd16(p+2), (int16_t)rd16(p+4), (int16_t)rd16(p+6));
                p += 8;
                printf("mode: %d\n", rd16(p));
                p += 2;

                /* First 5 scanlines */
                printf("\nFirst 5 scanline byteCounts:\n");
                for (i = 0; i < 5; i++) {
                    int bc;
                    if (rbRaw > 250) { bc = rd16(p); p += 2; }
                    else { bc = *p++; }
                    printf("  line %d: byteCount=%d, first bytes:", i, bc);
                    { int j; for (j = 0; j < 16 && j < bc; j++) printf(" %02X", p[j]); }
                    printf("\n");
                    p += bc;
                }
            }
            break;
        }
        p += 2; /* try next word */
    }

    free(dd);
    return 0;
}
