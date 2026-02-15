#ifndef __ENDIAN_COMPAT_H__
#define __ENDIAN_COMPAT_H__

#include <stdint.h>
#include <string.h>

/* Byte-swap utilities for little-endian platforms.
 * All Mac resource data is big-endian. These convert to native (little-endian). */

static inline uint16_t SwapU16(uint16_t v)
{
    return (v >> 8) | (v << 8);
}

static inline int16_t SwapS16(int16_t v)
{
    return (int16_t)SwapU16((uint16_t)v);
}

static inline uint32_t SwapU32(uint32_t v)
{
    return ((v >> 24) & 0x000000FF) |
           ((v >>  8) & 0x0000FF00) |
           ((v <<  8) & 0x00FF0000) |
           ((v << 24) & 0xFF000000);
}

static inline int32_t SwapS32(int32_t v)
{
    return (int32_t)SwapU32((uint32_t)v);
}

static inline float SwapFloat(float v)
{
    float result;
    uint32_t tmp;
    memcpy(&tmp, &v, 4);
    tmp = SwapU32(tmp);
    memcpy(&result, &tmp, 4);
    return result;
}

/* Read big-endian values from a byte buffer */
static inline uint16_t ReadBE16(const void *p)
{
    const uint8_t *b = (const uint8_t*)p;
    return (uint16_t)((b[0] << 8) | b[1]);
}

static inline uint32_t ReadBE32(const void *p)
{
    const uint8_t *b = (const uint8_t*)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

static inline int16_t ReadBES16(const void *p)
{
    return (int16_t)ReadBE16(p);
}

static inline int32_t ReadBES32(const void *p)
{
    return (int32_t)ReadBE32(p);
}

/* Write big-endian value to a byte buffer */
static inline void WriteBE16(void *p, uint16_t v)
{
    uint8_t *b = (uint8_t*)p;
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v);
}

static inline void WriteBE32(void *p, uint32_t v)
{
    uint8_t *b = (uint8_t*)p;
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)(v);
}

#endif /* __ENDIAN_COMPAT_H__ */
