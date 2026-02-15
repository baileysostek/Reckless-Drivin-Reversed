#ifndef __LZRW_H__
#define __LZRW_H__

#include "mac_compat.h"

/* LZRW3-A decompression for Mac resource data.
 * Decompresses a Handle in-place, replacing its contents with decompressed data.
 * The compressed format has a 4-byte header: uncompressed size (big-endian).
 * Then LZRW3-A compressed data follows.
 */
void LZRWDecodeHandle(Handle *h);

#endif /* __LZRW_H__ */
