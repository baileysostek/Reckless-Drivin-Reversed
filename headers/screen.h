#ifndef __SCREEN
#define __SCREEN

#include "mac_compat.h"

enum{
	kScreenSuspended,
	kScreenRunning,
	kScreenStopped,
	kScreenPaused
};

#define kXOrigin (gXSize/2)
#define kYOrigin (gYSize/2)
#define kMaxScreenY 640
#define kLightValues 16

extern Ptr gBaseAddr;
extern short gRowBytes;
extern short gXSize,gYSize;
extern int gOddLines;
extern Handle gTranslucenceTab,g16BitClut;
extern UInt8 gLightningTab[kLightValues][256];
extern int gScreenBlitSpecial;

void InitScreen(void);
void SetScreenClut(int);
void ScreenMode(int);
void Blit2Screen(void);
void AddFloatToMessageBuffer(const char *,float);
void FlushMessageBuffer(void);
void TakeScreenshot(void);
Point GetScreenPos(Point *);
GWorldPtr GetScreenGW(void);
void FadeScreen(int);

/* BlendRGB16 / ShadeRGB16 operate on 1-5-5-5 XRGB pixels.
 * Pixels are stored big-endian in the framebuffer. On little-endian x86,
 * reading a UInt16 byte-swaps the value, scrambling the green channel
 * across the byte boundary. We must swap to native before applying the
 * 4-bit-per-channel masks (0x7800, 0x03c0, 0x001e) and swap back. */
static inline UInt16 BlendRGB16(UInt16 a,UInt16 b)
{
	UInt16 na=(a>>8)|(a<<8), nb=(b>>8)|(b<<8);
	UInt16 r=((na&0x001e)>>1)+((nb&0x001e)>>1)+((na&0x03c0)>>1)+((nb&0x03c0)>>1)+((na&0x7800)>>1)+((nb&0x7800)>>1);
	return (r>>8)|(r<<8);
}

static inline UInt16 ShadeRGB16(int shade,UInt16 a)
{
	UInt16 na=(a>>8)|(a<<8);
	UInt16 r=((na&0x001e)*shade/kLightValues)+((na&0x03c0)*shade/kLightValues&0x03c0)+((na&0x7800)*shade/kLightValues&0x7800);
	return (r>>8)|(r<<8);
}

#endif
