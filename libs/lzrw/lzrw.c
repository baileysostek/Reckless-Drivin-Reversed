/*
 * LZRW3-A Decompression wrapper for Reckless Drivin' pack data.
 * Uses Ross Williams' public domain LZRW3-A algorithm.
 *
 * The compressed format used by Reckless Drivin':
 * - 4 bytes: uncompressed size (big-endian)
 * - Then LZRW3-A compressed data
 */

#include "lzrw.h"
#include "lzrw3a.h"
#include "endian_compat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void LZRWDecodeHandle(Handle *h)
{
    unsigned char *compData;
    unsigned long compSize;
    unsigned long uncompSize;
    Handle newH;
    uint8_t *wrk_mem;
    uint64_t dst_len;
    struct compress_identity identity;

    if (!h || !*h) return;

    compData = (unsigned char*)**h;
    compSize = (unsigned long)GetHandleSize(*h);

    if (compSize < 4) return;

    /* First 4 bytes = uncompressed size (big-endian) */
    uncompSize = ReadBE32(compData);

    fprintf(stderr, "[LZRWDecodeHandle] compSize=%lu, uncompSize=%lu\n",
            compSize, uncompSize);

    /* If uncompressed size is 0 or unreasonable, data might not be compressed */
    if (uncompSize == 0 || uncompSize > 64 * 1024 * 1024) return;

    newH = NewHandle(uncompSize);
    if (!newH) return;

    /* Get working memory size from the algorithm identity */
    identity = lzrw_identity();

    wrk_mem = (uint8_t *)malloc(identity.memory);
    if (!wrk_mem) {
        DisposeHandle(newH);
        return;
    }

    /* Decompress: skip the 4-byte size header */
    dst_len = 0;
    lzrw3a_compress(COMPRESS_ACTION_DECOMPRESS,
                    wrk_mem,
                    compData + 4, (uint32_t)(compSize - 4),
                    (uint8_t *)*newH, &dst_len);

    fprintf(stderr, "[LZRWDecodeHandle] decompressed %lu -> %llu bytes (expected %lu)\n",
            compSize - 4, (unsigned long long)dst_len, uncompSize);

    free(wrk_mem);

    /* Replace old handle with new decompressed data */
    DisposeHandle(*h);
    *h = newH;
}
