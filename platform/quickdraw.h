#ifndef __QUICKDRAW_H__
#define __QUICKDRAW_H__

#include "mac_compat.h"

/*
 * QuickDraw PICT v2 parser for Reckless Drivin' PPic resources.
 * Handles opcodes 0x0098 (PackBitsRect - indexed color) and
 * 0x009A (DirectBitsRect - 16-bit direct color).
 * Outputs 16-bit big-endian 1-5-5-5 XRGB pixels.
 */

/* Parse decompressed PICT data and render into a 16-bit framebuffer.
 * buf:      destination pixel buffer (must be at least rowBytes * 480)
 * rowBytes: stride in bytes (typically 640*2 = 1280)
 * data:     raw decompressed PICT bytes
 * dataSize: length of data in bytes
 * Returns 1 on success, 0 on failure. */
int DrawPPic(Ptr buf, int rowBytes, const unsigned char *data, long dataSize);

#endif /* __QUICKDRAW_H__ */
