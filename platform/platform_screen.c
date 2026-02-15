/*
 * platform_screen.c - SDL2+OpenGL screen module for Reckless Drivin'
 * Replaces the original Mac DrawSprocket-based source/screen.c
 */

#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "mac_compat.h"
#include "endian_compat.h"
#include "platform_screen.h"

/* Extern from screenfx module */
extern void ShiftInPicture(void);

/* SDL/GL state */
static SDL_Window *gWindow;
static SDL_GLContext gGLContext;
static GLuint gFramebufferTexture;
static uint32_t *gConvertBuffer;   /* RGBA32 conversion buffer, 640*480 */
static float gFadeFactor = 1.0f;
static int gScreenInited = 0;

/* Guard zones around framebuffer to detect buffer overflows.
 * Rendering code often writes directly to gBaseAddr with no bounds checking.
 * On the original Mac, gBaseAddr was VRAM where overflows were harmless.
 * In our SDL port, gBaseAddr is heap memory, so overflows corrupt the heap. */
#define GUARD_SIZE 4096
#define GUARD_PATTERN 0xDE
static uint8_t *gBaseAddrAlloc; /* actual allocation (guard + fb + guard) */

static void InitGuardZones(void)
{
    memset(gBaseAddrAlloc, GUARD_PATTERN, GUARD_SIZE);
    memset(gBaseAddrAlloc + GUARD_SIZE + 640 * 480 * 2, GUARD_PATTERN, GUARD_SIZE);
}

static void CheckGuardZones(const char *context)
{
    int i;
    int preCorrupt = 0, postCorrupt = 0;

    for (i = 0; i < GUARD_SIZE; i++) {
        if (gBaseAddrAlloc[i] != GUARD_PATTERN)
            preCorrupt++;
        if (gBaseAddrAlloc[GUARD_SIZE + 640 * 480 * 2 + i] != GUARD_PATTERN)
            postCorrupt++;
    }

    if (preCorrupt) {
        fprintf(stderr, "[GUARD] %s: PRE-BUFFER underflow detected! %d/%d guard bytes corrupted\n",
                context, preCorrupt, GUARD_SIZE);
        /* Find the last corrupted byte to estimate how far the underflow reached */
        for (i = GUARD_SIZE - 1; i >= 0; i--)
            if (gBaseAddrAlloc[i] != GUARD_PATTERN) {
                fprintf(stderr, "[GUARD] Underflow reached %d bytes before gBaseAddr\n", GUARD_SIZE - i);
                break;
            }
    }
    if (postCorrupt) {
        fprintf(stderr, "[GUARD] %s: POST-BUFFER overflow detected! %d/%d guard bytes corrupted\n",
                context, postCorrupt, GUARD_SIZE);
        /* Find the first corrupted byte to estimate how far the overflow reached */
        for (i = 0; i < GUARD_SIZE; i++)
            if (gBaseAddrAlloc[GUARD_SIZE + 640 * 480 * 2 + i] != GUARD_PATTERN) {
                fprintf(stderr, "[GUARD] Overflow reached %d bytes past gBaseAddr end\n", i + 1);
                break;
            }
    }

    if (preCorrupt || postCorrupt) {
        fprintf(stderr, "[GUARD] This indicates rendering code is writing outside the 640x480 framebuffer!\n");
        fflush(stderr);
        /* Re-initialize guard zones so we can see if it happens again */
        InitGuardZones();
    }
}

/* Public globals matching original screen.h externs */
Ptr gBaseAddr;
short gRowBytes;
short gXSize, gYSize;
int gOddLines;
Handle gTranslucenceTab = NULL;
Handle g16BitClut = NULL;
UInt8 gLightningTab[kLightValues][256];
int gScreenMode;
int gScreenBlitSpecial = 0;

/* Public wrapper for guard zone check (callable from other modules) */
void CheckGuardZonesExtern(const char *ctx)
{
    if (gBaseAddrAlloc)
        CheckGuardZones(ctx);
}

/* Message buffer */
static char gMessageBuffer[1024];
static char *gMessagePos;
static int gMessageCount;

/* ---- InitScreen ---- */
void InitScreen(void)
{
    if (gScreenInited)
        return;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init(VIDEO) failed: %s\n", SDL_GetError());
        exit(1);
    }

    gXSize = 640;
    gYSize = 480;

    gWindow = SDL_CreateWindow(
        "Reckless Drivin'",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    if (!gWindow) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(1);
    }

    gGLContext = SDL_GL_CreateContext(gWindow);
    if (!gGLContext) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_GL_SetSwapInterval(1);

    /* Create framebuffer texture */
    glGenTextures(1, &gFramebufferTexture);
    glBindTexture(GL_TEXTURE_2D, gFramebufferTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    /* Allocate 16-bit software framebuffer with guard zones */
    gBaseAddrAlloc = (uint8_t *)calloc(GUARD_SIZE + 640 * 480 * 2 + GUARD_SIZE, 1);
    gBaseAddr = (Ptr)(gBaseAddrAlloc + GUARD_SIZE);
    gRowBytes = 1280;
    InitGuardZones();

    /* Allocate RGBA32 conversion buffer */
    gConvertBuffer = (uint32_t *)calloc(640 * 480, sizeof(uint32_t));

    gScreenMode = kScreenSuspended;
    gScreenInited = 1;

    /* Init message buffer */
    gMessagePos = gMessageBuffer + 1;
    gMessageCount = 0;
}

/* ---- Blit2Screen ---- */
void Blit2Screen(void)
{
    int winW, winH;
    int vpX, vpY, vpW, vpH;
    float aspectWin, aspectGame;
    int totalPixels = 640 * 480;
    int i;
    const uint8_t *src = (const uint8_t *)gBaseAddr;

    /* Check guard zones for framebuffer overflow */
    CheckGuardZones("Blit2Screen");

    /* Convert 16-bit big-endian pixels to RGBA32 */
    for (i = 0; i < totalPixels; i++) {
        uint16_t pixel = (uint16_t)((src[0] << 8) | src[1]);
        src += 2;

        uint8_t r5 = (pixel >> 10) & 0x1F;
        uint8_t g5 = (pixel >> 5) & 0x1F;
        uint8_t b5 = pixel & 0x1F;

        uint8_t r8 = (r5 << 3) | (r5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t b8 = (b5 << 3) | (b5 >> 2);

        if (gFadeFactor != 1.0f) {
            r8 = (uint8_t)(r8 * gFadeFactor);
            g8 = (uint8_t)(g8 * gFadeFactor);
            b8 = (uint8_t)(b8 * gFadeFactor);
        }

        gConvertBuffer[i] = (uint32_t)r8 | ((uint32_t)g8 << 8) | ((uint32_t)b8 << 16) | 0xFF000000u;
    }

    /* Upload to texture */
    glBindTexture(GL_TEXTURE_2D, gFramebufferTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, gConvertBuffer);

    /* Compute letterbox viewport maintaining 4:3 aspect */
    SDL_GetWindowSize(gWindow, &winW, &winH);
    if (winW < 1 || winH < 1)
        return;  /* Window minimized or zero-sized; skip rendering */
    aspectWin = (float)winW / (float)winH;
    aspectGame = 640.0f / 480.0f;

    if (aspectWin > aspectGame) {
        /* Window is wider than 4:3 -- pillarbox */
        vpH = winH;
        vpW = (int)(winH * aspectGame);
        vpX = (winW - vpW) / 2;
        vpY = 0;
    } else {
        /* Window is taller than 4:3 -- letterbox */
        vpW = winW;
        vpH = (int)(winW / aspectGame);
        vpX = 0;
        vpY = (winH - vpH) / 2;
    }

    glViewport(vpX, vpY, vpW, vpH);

    /* Set up orthographic projection */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Clear and draw fullscreen quad */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gFramebufferTexture);

    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
    glEnd();

    glDisable(GL_TEXTURE_2D);

    SDL_GL_SwapWindow(gWindow);

    /* Handle blit-special (shift-in effect) */
    if (gScreenBlitSpecial) {
        gScreenBlitSpecial = 0;
        ShiftInPicture();
    }
}

/* ---- ScreenMode ---- */
void ScreenMode(int mode)
{
    switch (mode) {
    case kScreenRunning:
        SDL_ShowWindow(gWindow);
        gScreenMode = kScreenRunning;
        SetScreenClut(8);
        break;
    case kScreenSuspended:
        gScreenMode = kScreenSuspended;
        break;
    case kScreenStopped:
        if (gFramebufferTexture) {
            glDeleteTextures(1, &gFramebufferTexture);
            gFramebufferTexture = 0;
        }
        if (gGLContext) {
            SDL_GL_DeleteContext(gGLContext);
            gGLContext = NULL;
        }
        if (gWindow) {
            SDL_DestroyWindow(gWindow);
            gWindow = NULL;
        }
        if (gBaseAddrAlloc) {
            free(gBaseAddrAlloc);
            gBaseAddrAlloc = NULL;
            gBaseAddr = NULL;
        }
        if (gConvertBuffer) {
            free(gConvertBuffer);
            gConvertBuffer = NULL;
        }
        gScreenMode = kScreenStopped;
        gScreenInited = 0;
        break;
    case kScreenPaused:
        gScreenMode = kScreenPaused;
        break;
    }
}

/* ---- FadeScreen ---- */
void FadeScreen(int out)
{
    if (out == 1) {
        gFadeFactor = 0.0f;        /* fade to black */
    } else if (out == 0) {
        gFadeFactor = 1.0f;        /* full brightness */
    } else if (out >= 256) {
        gFadeFactor = (float)(out - 256) / 256.0f;
    }
}

/* ---- GetScreenGW ---- */
GWorldPtr GetScreenGW(void)
{
    return NULL;    /* GWorld not used in SDL port */
}

/* ---- GetScreenPos ---- */
Point GetScreenPos(Point *inPos)
{
    Point result;

    if (inPos) {
        result = *inPos;
    } else {
        int mx, my;
        int winW, winH;
        int vpX, vpY, vpW, vpH;
        float aspectWin, aspectGame;

        SDL_GetMouseState(&mx, &my);
        SDL_GetWindowSize(gWindow, &winW, &winH);

        if (winW < 1 || winH < 1) {
            result.h = 0;
            result.v = 0;
            return result;
        }

        aspectWin = (float)winW / (float)winH;
        aspectGame = 640.0f / 480.0f;

        if (aspectWin > aspectGame) {
            vpH = winH;
            vpW = (int)(winH * aspectGame);
            vpX = (winW - vpW) / 2;
            vpY = 0;
        } else {
            vpW = winW;
            vpH = (int)(winW / aspectGame);
            vpX = 0;
            vpY = (winH - vpH) / 2;
        }

        /* Scale mouse coords to 640x480 game coords, accounting for letterbox */
        result.h = (short)((mx - vpX) * 640 / vpW);
        result.v = (short)((my - vpY) * 480 / vpH);

        /* Clamp */
        if (result.h < 0) result.h = 0;
        if (result.h > 639) result.h = 639;
        if (result.v < 0) result.v = 0;
        if (result.v > 479) result.v = 479;
    }

    return result;
}

/* ---- WindowToFramebuffer ---- */
/* Maps window pixel coordinates to 640x480 framebuffer coordinates,
 * accounting for letterbox/pillarbox viewport. */
void WindowToFramebuffer(int winX, int winY, int *fbX, int *fbY)
{
    int winW, winH;
    int vpX, vpY, vpW, vpH;
    float aspectWin, aspectGame;

    SDL_GetWindowSize(gWindow, &winW, &winH);
    if (winW < 1 || winH < 1) {
        *fbX = 0; *fbY = 0;
        return;
    }

    aspectWin = (float)winW / (float)winH;
    aspectGame = 640.0f / 480.0f;

    if (aspectWin > aspectGame) {
        vpH = winH;
        vpW = (int)(winH * aspectGame);
        vpX = (winW - vpW) / 2;
        vpY = 0;
    } else {
        vpW = winW;
        vpH = (int)(winW / aspectGame);
        vpX = 0;
        vpY = (winH - vpH) / 2;
    }

    *fbX = (winX - vpX) * 640 / vpW;
    *fbY = (winY - vpY) * 480 / vpH;

    if (*fbX < 0) *fbX = 0;
    if (*fbX > 639) *fbX = 639;
    if (*fbY < 0) *fbY = 0;
    if (*fbY > 479) *fbY = 479;
}

/* Convert an 8-bit RGB triplet to big-endian 1-5-5-5 XRGB pixel */
static UInt16 RGB8toBE16(int r8, int g8, int b8)
{
    int r5 = r8 * 31 / 255;
    int g5 = g8 * 31 / 255;
    int b5 = b8 * 31 / 255;
    UInt16 pixel = (UInt16)((r5 << 10) | (g5 << 5) | b5);
    /* Swap to big-endian */
    return (UInt16)(((pixel >> 8) & 0xFF) | ((pixel & 0xFF) << 8));
}

/* Generate a 256-entry 16-bit CLUT from the standard Mac OS system palette
 * (clut ID 8).  The Mac system 256-color palette is:
 *   Indices 0-215:   6x6x6 RGB color cube (reverse order: 0=white, 215=black)
 *   Indices 216-225: Red intensity ramp (10 shades)
 *   Indices 226-235: Green intensity ramp (10 shades)
 *   Indices 236-245: Blue intensity ramp (10 shades)
 *   Indices 246-254: Grey intensity ramp (9 shades, bright to dark)
 *   Index 255:       Black
 * The 6 levels per channel in the cube are: 255, 204, 153, 102, 51, 0
 * The 10 ramp intensities (bright to dark): 238,221,187,170,136,119,85,68,34,17
 */
static void GenerateDefaultClut(void)
{
    static const int kRampValues[10] = {238,221,187,170,136,119,85,68,34,17};
    int i;
    UInt16 *clut;

    if (g16BitClut) {
        DisposeHandle(g16BitClut);
        g16BitClut = NULL;
    }

    g16BitClut = NewHandle(256 * sizeof(UInt16));
    if (!g16BitClut) return;

    clut = (UInt16 *)*g16BitClut;

    /* Indices 0-215: 6x6x6 color cube, reverse order */
    for (i = 0; i < 216; i++) {
        int r8 = (5 - (i / 36)) * 51;
        int g8 = (5 - ((i / 6) % 6)) * 51;
        int b8 = (5 - (i % 6)) * 51;
        clut[i] = RGB8toBE16(r8, g8, b8);
    }

    /* Indices 216-225: Red ramp */
    for (i = 0; i < 10; i++)
        clut[216 + i] = RGB8toBE16(kRampValues[i], 0, 0);

    /* Indices 226-235: Green ramp */
    for (i = 0; i < 10; i++)
        clut[226 + i] = RGB8toBE16(0, kRampValues[i], 0);

    /* Indices 236-245: Blue ramp */
    for (i = 0; i < 10; i++)
        clut[236 + i] = RGB8toBE16(0, 0, kRampValues[i]);

    /* Indices 246-254: Grey ramp */
    for (i = 0; i < 9; i++)
        clut[246 + i] = RGB8toBE16(kRampValues[i], kRampValues[i], kRampValues[i]);

    /* Index 255: Black */
    clut[255] = RGB8toBE16(0, 0, 0);
}

/* ---- SetScreenClut ---- */
void SetScreenClut(int id)
{
    char path[512];
    FILE *f;
    long size;
    char *data;

    /* Try to load Cl16 resource from assets */
    snprintf(path, sizeof(path), "assets/cl16_%d.bin", id);
    f = fopen(path, "rb");
    if (!f) {
        /* No Cl16 resource file; generate a default CLUT so particles work */
        if (!g16BitClut)
            GenerateDefaultClut();
        return;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (g16BitClut) {
        DisposeHandle(g16BitClut);
        g16BitClut = NULL;
    }

    g16BitClut = NewHandle(size);
    if (g16BitClut) {
        data = *g16BitClut;
        fread(data, 1, (size_t)size, f);
    }

    fclose(f);
}

/* ---- TakeScreenshot ---- */
void TakeScreenshot(void)
{
    /* No-op stub */
}

/* ---- FlushMessageBuffer ---- */
void FlushMessageBuffer(void)
{
    gMessagePos = gMessageBuffer + 1;
    gMessageCount = 0;
}

/* ---- AddFloatToMessageBuffer ---- */
void AddFloatToMessageBuffer(StringPtr label, float value)
{
    int len;
    char numStr[64];

    if (!label)
        return;

    /* Pascal string: first byte is length */
    len = label[0];
    if (gMessagePos + len + 20 > gMessageBuffer + sizeof(gMessageBuffer))
        return;

    memcpy(gMessagePos, label + 1, len);
    gMessagePos += len;

    snprintf(numStr, sizeof(numStr), "%.2f ", value);
    len = (int)strlen(numStr);
    memcpy(gMessagePos, numStr, len);
    gMessagePos += len;

    gMessageCount++;
}
