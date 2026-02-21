#ifndef __PLATFORM_SCREEN_H__
#define __PLATFORM_SCREEN_H__

#include "mac_compat.h"
#include <SDL.h>

/* Screen mode constants (from original headers/screen.h) */
enum {
    kScreenSuspended,
    kScreenRunning,
    kScreenStopped,
    kScreenPaused
};

#define kXOrigin (gXSize/2)
#define kYOrigin (gYSize/2)
#define kMaxScreenY 640
#define kLightValues 16

/* Globals (match original screen.h externs) */
extern Ptr gBaseAddr;
extern short gRowBytes;
extern short gXSize, gYSize;
extern int gOddLines;
extern Handle gTranslucenceTab, g16BitClut;
extern UInt8 gLightningTab[kLightValues][256];
extern int gScreenBlitSpecial;

/* Functions */
void InitScreen(void);
void SetScreenClut(int id);
void ScreenMode(int mode);
void Blit2Screen(void);
void AddFloatToMessageBuffer(const char *label, float value);
void FlushMessageBuffer(void);
void TakeScreenshot(void);
Point GetScreenPos(Point *inPos);
GWorldPtr GetScreenGW(void);
void FadeScreen(int out);

/* Inline blending functions (from original screen.h - these operate on big-endian 16-bit pixels)
 * 16-bit format: 1-5-5-5 xRRRRRGGGGGBBBBB in big-endian
 * The original masks (0x7800, 0x03c0, 0x001e) extract 4 bits per channel
 * from a native-endian value. Since our pixels are big-endian, we must
 * byte-swap before applying masks and swap back after. */
static inline UInt16 BlendRGB16(UInt16 a, UInt16 b)
{
    UInt16 na = (a >> 8) | (a << 8), nb = (b >> 8) | (b << 8);
    UInt16 r = ((na&0x001e)>>1)+((nb&0x001e)>>1)+((na&0x03c0)>>1)+((nb&0x03c0)>>1)+((na&0x7800)>>1)+((nb&0x7800)>>1);
    return (r >> 8) | (r << 8);
}

static inline UInt16 ShadeRGB16(int shade, UInt16 a)
{
    UInt16 na = (a >> 8) | (a << 8);
    UInt16 r = ((na&0x001e)*shade/kLightValues)+((na&0x03c0)*shade/kLightValues&0x03c0)+((na&0x7800)*shade/kLightValues&0x7800);
    return (r >> 8) | (r << 8);
}

void WindowToFramebuffer(int winX, int winY, int *fbX, int *fbY);
void ResizeFramebuffer(int newWidth);
int ComputeWidescreenWidth(int winW, int winH);
void SetFullscreen(int enable);
int  IsFullscreen(void);

#endif
