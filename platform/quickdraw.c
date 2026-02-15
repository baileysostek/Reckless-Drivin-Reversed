/*
 * quickdraw.c - QuickDraw PICT v2 parser for Reckless Drivin' PPic resources.
 *
 * Handles the two PICT opcodes used by the game:
 *   0x0098 PackBitsRect  - 8-bit indexed color with embedded color table
 *   0x009A DirectBitsRect - 16-bit direct color (1-5-5-5 XRGB)
 *
 * Both render into a 16-bit big-endian framebuffer matching the game's
 * software renderer format.
 */

#include "quickdraw.h"
#include "endian_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Stream reader ---- */

typedef struct {
    const unsigned char *ptr;
    const unsigned char *end;
} PictStream;

static int ps_remaining(PictStream *s)
{
    return (int)(s->end - s->ptr);
}

static uint8_t ps_read_u8(PictStream *s)
{
    if (s->ptr >= s->end) return 0;
    return *s->ptr++;
}

static uint16_t ps_read_u16(PictStream *s)
{
    if (s->ptr + 2 > s->end) { s->ptr = s->end; return 0; }
    uint16_t v = ReadBE16(s->ptr);
    s->ptr += 2;
    return v;
}

static int16_t ps_read_s16(PictStream *s)
{
    return (int16_t)ps_read_u16(s);
}

static uint32_t ps_read_u32(PictStream *s)
{
    if (s->ptr + 4 > s->end) { s->ptr = s->end; return 0; }
    uint32_t v = ReadBE32(s->ptr);
    s->ptr += 4;
    return v;
}

static void ps_skip(PictStream *s, int n)
{
    s->ptr += n;
    if (s->ptr > s->end) s->ptr = s->end;
}

static void ps_align(PictStream *s)
{
    /* PICT v2 opcodes are word-aligned */
    /* We don't need explicit alignment since opcodes are always 2 bytes
       and data fields are designed to keep alignment. But just in case: */
}

static void ps_read_rect(PictStream *s, Rect *r)
{
    r->top    = ps_read_s16(s);
    r->left   = ps_read_s16(s);
    r->bottom = ps_read_s16(s);
    r->right  = ps_read_s16(s);
}

/* ---- PackBits decompression ---- */

/*
 * Decompress one scanline of PackBits data.
 * For pixelSize <= 8: operates on bytes (unitSize=1)
 * For pixelSize = 16: operates on 16-bit words (unitSize=2)
 *
 * src:       compressed data pointer
 * srcAvail:  bytes available in src
 * dst:       output buffer for decompressed scanline
 * dstLen:    expected decompressed length in bytes
 * unitSize:  1 for byte-mode, 2 for 16-bit word-mode
 *
 * Returns number of bytes consumed from src.
 */
static int UnpackBits(const unsigned char *src, int srcAvail,
                      unsigned char *dst, int dstLen, int unitSize)
{
    const unsigned char *srcStart = src;
    const unsigned char *srcEnd = src + srcAvail;
    unsigned char *dstStart = dst;
    unsigned char *dstEnd = dst + dstLen;

    while (src < srcEnd && dst < dstEnd) {
        int8_t n = (int8_t)*src++;

        if (n >= 0) {
            /* Literal run: copy (n+1) units */
            int count = (n + 1) * unitSize;
            if (src + count > srcEnd) break;
            if (dst + count > dstEnd) count = (int)(dstEnd - dst);
            memcpy(dst, src, count);
            src += (n + 1) * unitSize;
            dst += count;
        } else if (n != -128) {
            /* Repeat run: repeat next unit (1-n) times */
            int repeat = 1 - n;
            if (src + unitSize > srcEnd) break;
            if (unitSize == 1) {
                uint8_t val = *src++;
                int i;
                for (i = 0; i < repeat && dst < dstEnd; i++)
                    *dst++ = val;
            } else {
                /* 16-bit unit */
                uint8_t v0 = src[0], v1 = src[1];
                int i;
                src += 2;
                for (i = 0; i < repeat && dst + 1 < dstEnd; i++) {
                    *dst++ = v0;
                    *dst++ = v1;
                }
            }
        }
        /* n == -128: NOP, skip */
    }

    return (int)(src - srcStart);
}

/* ---- Opcode 0x009A: DirectBitsRect (16-bit direct color) ---- */

static int ParseDirectBitsRect(PictStream *s, Ptr buf, int bufRowBytes)
{
    uint16_t pmRowBytes;
    Rect bounds, srcRect, dstRect;
    uint16_t pmVersion, packType, pixelType, pixelSize, cmpCount, cmpSize;
    uint32_t packSize, planeBytes;
    uint16_t mode;
    int height, width, y;
    int rowBytesRaw;
    unsigned char *scanline;

    /* Skip baseAddr (4 bytes) - present for DirectBitsRect */
    ps_skip(s, 4);

    /* PixMap record */
    pmRowBytes = ps_read_u16(s);
    rowBytesRaw = pmRowBytes & 0x3FFF;  /* strip flags */

    ps_read_rect(s, &bounds);
    pmVersion = ps_read_u16(s);
    packType  = ps_read_u16(s);
    packSize  = ps_read_u32(s);
    ps_skip(s, 4); /* hRes */
    ps_skip(s, 4); /* vRes */
    pixelType = ps_read_u16(s);
    pixelSize = ps_read_u16(s);
    cmpCount  = ps_read_u16(s);
    cmpSize   = ps_read_u16(s);
    planeBytes = ps_read_u32(s);
    ps_skip(s, 4); /* pmTable */
    ps_skip(s, 4); /* pmReserved */

    (void)pmVersion; (void)packSize; (void)pixelType;
    (void)cmpCount; (void)cmpSize; (void)planeBytes;

    /* srcRect, dstRect, mode */
    ps_read_rect(s, &srcRect);
    ps_read_rect(s, &dstRect);
    mode = ps_read_u16(s);
    (void)mode;

    height = bounds.bottom - bounds.top;
    width  = bounds.right  - bounds.left;

    if (height <= 0 || width <= 0 || pixelSize != 16) {
        fprintf(stderr, "[DrawPPic] DirectBitsRect: unexpected pixelSize=%d or size %dx%d\n",
                pixelSize, width, height);
        return 0;
    }

    scanline = (unsigned char *)malloc(rowBytesRaw);
    if (!scanline) return 0;

    for (y = 0; y < height; y++) {
        int byteCount;
        int consumed;
        int destY = dstRect.top + y;
        int destX = dstRect.left;

        if (ps_remaining(s) < 1) break;

        /* Scanline packed data byte count */
        if (rowBytesRaw > 250) {
            byteCount = ps_read_u16(s);
        } else {
            byteCount = ps_read_u8(s);
        }

        if (byteCount <= 0 || byteCount > ps_remaining(s)) break;

        /* Decode PackBits with 16-bit units */
        consumed = UnpackBits(s->ptr, byteCount, scanline, rowBytesRaw, 2);
        s->ptr += byteCount;

        /* Copy scanline to destination buffer */
        if (destY >= 0 && destY < 480) {
            int copyBytes = width * 2;
            int destOff = destY * bufRowBytes + destX * 2;
            if (destX + width > 640) copyBytes = (640 - destX) * 2;
            if (copyBytes > 0 && destOff >= 0) {
                /* Pixels are already big-endian 1-5-5-5 XRGB, matching our framebuffer */
                memcpy(buf + destOff, scanline, copyBytes);
            }
        }
    }

    free(scanline);
    return 1;
}

/* ---- Opcode 0x0098: PackBitsRect (indexed color) ---- */

static int ParsePackBitsRect(PictStream *s, Ptr buf, int bufRowBytes)
{
    uint16_t pmRowBytes;
    Rect bounds, srcRect, dstRect;
    uint16_t pmVersion, packType, pixelType, pixelSize, cmpCount, cmpSize;
    uint32_t packSize, planeBytes;
    uint16_t mode;
    int height, width, y;
    int rowBytesRaw;
    int isPixMap;
    unsigned char *scanline;

    /* Color table (up to 256 entries mapped to 16-bit big-endian pixels) */
    uint16_t clut16[256];
    int clutSize = 0;

    /* PixMap or BitMap - read rowBytes to determine */
    pmRowBytes = ps_read_u16(s);
    isPixMap = (pmRowBytes & 0x8000) != 0;
    rowBytesRaw = pmRowBytes & 0x3FFF;

    ps_read_rect(s, &bounds);

    if (isPixMap) {
        pmVersion = ps_read_u16(s);
        packType  = ps_read_u16(s);
        packSize  = ps_read_u32(s);
        ps_skip(s, 4); /* hRes */
        ps_skip(s, 4); /* vRes */
        pixelType = ps_read_u16(s);
        pixelSize = ps_read_u16(s);
        cmpCount  = ps_read_u16(s);
        cmpSize   = ps_read_u16(s);
        planeBytes = ps_read_u32(s);
        ps_skip(s, 4); /* pmTable */
        ps_skip(s, 4); /* pmReserved */

        (void)pmVersion; (void)packType; (void)packSize;
        (void)pixelType; (void)cmpCount; (void)cmpSize; (void)planeBytes;
    } else {
        pixelSize = 1;
    }

    /* Color table */
    if (isPixMap) {
        uint32_t ctSeed;
        uint16_t ctFlags, ctSize;
        int i;

        ctSeed  = ps_read_u32(s);
        ctFlags = ps_read_u16(s);
        ctSize  = ps_read_u16(s); /* ctSize = count - 1 */
        (void)ctSeed; (void)ctFlags;

        clutSize = ctSize + 1;
        if (clutSize > 256) clutSize = 256;

        memset(clut16, 0, sizeof(clut16));
        for (i = 0; i <= ctSize && i < 256; i++) {
            uint16_t idx = ps_read_u16(s);
            uint16_t r   = ps_read_u16(s);
            uint16_t g   = ps_read_u16(s);
            uint16_t b   = ps_read_u16(s);

            /* If ctFlags bit 15 is 0, use index from table entry;
               otherwise use sequential index */
            if (idx > 255) idx = (uint16_t)i;

            /* Convert 16-bit Mac RGB components to 5-bit each.
             * Mac color table values are 0-65535; take top 5 bits.
             * Output: big-endian 0RRRRRGGGGGBBBBB */
            {
                uint8_t r5 = (uint8_t)(r >> 11);
                uint8_t g5 = (uint8_t)(g >> 11);
                uint8_t b5 = (uint8_t)(b >> 11);
                uint16_t pixel = (uint16_t)((r5 << 10) | (g5 << 5) | b5);
                /* Store as big-endian */
                clut16[idx] = SwapU16(pixel);
            }
        }
    }

    /* srcRect, dstRect, mode */
    ps_read_rect(s, &srcRect);
    ps_read_rect(s, &dstRect);
    mode = ps_read_u16(s);
    (void)mode;

    height = bounds.bottom - bounds.top;
    width  = bounds.right  - bounds.left;

    if (height <= 0 || width <= 0) {
        fprintf(stderr, "[DrawPPic] PackBitsRect: bad size %dx%d\n", width, height);
        return 0;
    }

    scanline = (unsigned char *)malloc(rowBytesRaw + 256);
    if (!scanline) return 0;

    for (y = 0; y < height; y++) {
        int destY = dstRect.top + y;
        int destX = dstRect.left;

        if (ps_remaining(s) < 1) break;

        if (rowBytesRaw < 8) {
            /* Uncompressed scanline - just read rowBytesRaw bytes */
            if (ps_remaining(s) < rowBytesRaw) break;
            memcpy(scanline, s->ptr, rowBytesRaw);
            s->ptr += rowBytesRaw;
        } else {
            int byteCount;

            /* Scanline packed data byte count */
            if (rowBytesRaw > 250) {
                byteCount = ps_read_u16(s);
            } else {
                byteCount = ps_read_u8(s);
            }

            if (byteCount <= 0 || byteCount > ps_remaining(s)) break;

            /* PackBits with byte-sized units for indexed color */
            UnpackBits(s->ptr, byteCount, scanline, rowBytesRaw, 1);
            s->ptr += byteCount;
        }

        /* Convert indexed scanline to 16-bit and write to destination */
        if (destY >= 0 && destY < 480 && isPixMap && pixelSize == 8) {
            int x;
            int maxX = width;
            if (destX + maxX > 640) maxX = 640 - destX;

            for (x = 0; x < maxX && x < rowBytesRaw; x++) {
                uint8_t idx = scanline[x];
                UInt16 *destPixel = (UInt16 *)(buf + destY * bufRowBytes + (destX + x) * 2);
                *destPixel = clut16[idx];
            }
        }
    }

    free(scanline);
    return 1;
}

/* ---- Main entry point ---- */

int DrawPPic(Ptr buf, int rowBytes, const unsigned char *data, long dataSize)
{
    PictStream s;
    Rect picFrame;
    uint16_t opcode;

    if (!buf || !data || dataSize < 12) return 0;

    s.ptr = data;
    s.end = data + dataSize;

    /* PICT v2 header */
    ps_skip(&s, 2);        /* size word (ignored for v2) */
    ps_read_rect(&s, &picFrame); /* picFrame bounding rect */

    fprintf(stderr, "[DrawPPic] picFrame: %d,%d - %d,%d\n",
            picFrame.left, picFrame.top, picFrame.right, picFrame.bottom);

    /* Read opcodes */
    while (ps_remaining(&s) >= 2) {
        opcode = ps_read_u16(&s);

        switch (opcode) {
            case 0x0000: /* NOP */
            case 0x0001: /* Clip region */
            {
                if (opcode == 0x0001) {
                    uint16_t rgnSize = ps_read_u16(&s);
                    if (rgnSize > 2)
                        ps_skip(&s, rgnSize - 2);
                }
                break;
            }

            case 0x000A: /* DefineRegion / FillRgn - skip */
                ps_skip(&s, 8);
                break;

            case 0x0011: /* Version opcode */
            {
                uint16_t version = ps_read_u16(&s);
                (void)version;
                break;
            }

            case 0x0C00: /* HeaderOp (extended v2 header) */
                /* Skip 24 bytes: version(2), reserved(2), hRes(4), vRes(4),
                   srcRect(8), reserved(4) */
                ps_skip(&s, 24);
                break;

            case 0x001E: /* DefHilite */
                break;

            case 0x0098: /* PackBitsRect */
                if (!ParsePackBitsRect(&s, buf, rowBytes))
                    fprintf(stderr, "[DrawPPic] PackBitsRect failed\n");
                break;

            case 0x009A: /* DirectBitsRect */
                if (!ParseDirectBitsRect(&s, buf, rowBytes))
                    fprintf(stderr, "[DrawPPic] DirectBitsRect failed\n");
                break;

            case 0x00A1: /* LongComment */
            {
                uint16_t kind = ps_read_u16(&s);
                uint16_t len  = ps_read_u16(&s);
                (void)kind;
                ps_skip(&s, len);
                break;
            }

            case 0x00FF: /* EndOfPicture */
                return 1;

            default:
                /* Unknown opcode - try to skip based on opcode ranges */
                if (opcode >= 0x0300 && opcode <= 0x7FFF) {
                    /* Reserved opcodes with data word */
                    uint16_t dataLen = ps_read_u16(&s);
                    ps_skip(&s, dataLen);
                } else if (opcode >= 0x8000 && opcode <= 0x80FF) {
                    /* Reserved, no data */
                } else {
                    fprintf(stderr, "[DrawPPic] Unknown opcode 0x%04X at offset %ld, stopping\n",
                            opcode, (long)(s.ptr - data - 2));
                    return 1; /* Return success for what we've drawn so far */
                }
                break;
        }
    }

    return 1;
}
