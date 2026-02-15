/*
 * platform_interface.c - SDL2-based menu system replacing Mac GWorld/DrawSprocket interface.
 * Provides placeholder solid-color button rendering for the main menu.
 */

#include <SDL.h>
#include <string.h>
#include <stdio.h>
#include "mac_compat.h"
#include "endian_compat.h"
#include "quickdraw.h"
#include "lzrw3a.h"
#include "lzrw.h"
#include "platform_screen.h"

/* Button enums */
enum {
    kNoButton = -1,
    kStartGameButton,
    kPrefsButton,
    kScoreButton,
    kHelpButton,
    kQuitButton,
    kRegisterButton,
    kAboutButton
};

/* Globals extern'd in headers/interface.h */
int gExit;
short gLevelResFile = 0, gAppResFile;
Str63 gLevelFileName;

/* Static globals */
static Ptr gMainScreenBuf = NULL;   /* 640*480*2 bytes - normal menu framebuffer */
static Ptr gHilitBuf = NULL;        /* highlighted buttons */
static Ptr gSelectedBuf = NULL;     /* selected/pressed buttons */
static Rect gButtons[7];            /* button rectangles */
static int gNumButtons = 7;
static int gButtonLocation = -1;    /* kNoButton */
static int gInterfaceInited = 0;

/* Externs from other modules */
extern Ptr gBaseAddr;
extern short gRowBytes, gXSize, gYSize;
extern int gRegistered;
extern int gGameOn;
extern void Blit2Screen(void);
extern void FadeScreen(int);
extern void ScreenMode(int);
extern GWorldPtr GetScreenGW(void);
extern Point GetScreenPos(Point *);
extern void SetScreenClut(int);
extern void StartGame(int);
extern void Preferences(void);
extern void CheckHighScore(UInt32);
extern void ShowHighScores(int);
extern void Register(int);
extern void SimplePlaySound(int);
extern void InitChannels(void);
extern void ConfigureInput(void);
extern int ContinuePress(void);
extern void SetGameVolume(int);

/* Forward declarations */
static void FillRect16(Ptr buf, int rowBytes, Rect *r, UInt16 color);
static int LoadPPic(int id, Ptr destBuf, int rowBytes);
static void DrawScreen(int button, Ptr src);
static void UpdateButtonLocation(void);
static int GetButtonClick(int mx, int my);
static void HandleCommand(int cmd);

/* ------------------------------------------------------------------ */

static void FillRect16(Ptr buf, int rowBytes, Rect *r, UInt16 color)
{
    int x, y;
    for (y = r->top; y < r->bottom; y++) {
        UInt16 *row = (UInt16 *)(buf + y * rowBytes + r->left * 2);
        for (x = 0; x < (r->right - r->left); x++) {
            row[x] = color;
        }
    }
}

/*
 * LoadPPic - Load a PPic resource from disk, LZRW3-A decompress, and render
 * into a 16-bit framebuffer using the QuickDraw PICT parser.
 * Returns 1 on success, 0 on failure.
 */
static int LoadPPic(int id, Ptr destBuf, int rowBytes)
{
    char path[256];
    FILE *f;
    long fileSize;
    unsigned char *fileData;
    unsigned long uncompSize;
    unsigned char *decompData;
    struct compress_identity identity;
    uint8_t *wrk_mem;
    uint64_t dst_len;
    int result;

    sprintf(path, "assets/ppic_%d.bin", id);
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[LoadPPic] Failed to open %s\n", path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize < 5) {
        fclose(f);
        return 0;
    }

    fileData = (unsigned char *)malloc(fileSize);
    if (!fileData) {
        fclose(f);
        return 0;
    }
    fread(fileData, 1, fileSize, f);
    fclose(f);

    /* First 4 bytes: big-endian uncompressed size */
    uncompSize = ReadBE32(fileData);
    if (uncompSize == 0 || uncompSize > 4 * 1024 * 1024) {
        fprintf(stderr, "[LoadPPic] Bad uncompressed size %lu in %s\n", uncompSize, path);
        free(fileData);
        return 0;
    }

    decompData = (unsigned char *)malloc(uncompSize);
    if (!decompData) {
        free(fileData);
        return 0;
    }

    /* LZRW3-A decompress */
    identity = lzrw_identity();
    wrk_mem = (uint8_t *)malloc(identity.memory);
    if (!wrk_mem) {
        free(decompData);
        free(fileData);
        return 0;
    }

    dst_len = 0;
    lzrw3a_compress(COMPRESS_ACTION_DECOMPRESS,
                    wrk_mem,
                    fileData + 4, (uint32_t)(fileSize - 4),
                    decompData, &dst_len);
    free(wrk_mem);
    free(fileData);

    fprintf(stderr, "[LoadPPic] %s: %ld -> %llu bytes (expected %lu)\n",
            path, fileSize - 4, (unsigned long long)dst_len, uncompSize);

    /* Parse PICT and render to destination buffer */
    result = DrawPPic(destBuf, rowBytes, decompData, (long)dst_len);

    free(decompData);
    return result;
}

/* ------------------------------------------------------------------ */

void ShowPicScreen(int id)
{
    int rowBytes = 640 * 2;

    FadeScreen(1);
    Blit2Screen();  /* show the faded-out (black) frame */
    ScreenMode(kScreenRunning);

    /* Clear to black first, then load the PPic image */
    memset(gBaseAddr, 0, 640 * 480 * 2);
    LoadPPic(id, gBaseAddr, rowBytes);

    FadeScreen(0);  /* restore full brightness before blitting new content */
    Blit2Screen();
}

void ShowPicScreenNoFade(int id)
{
    int rowBytes = 640 * 2;

    ScreenMode(kScreenRunning);

    memset(gBaseAddr, 0, 640 * 480 * 2);
    LoadPPic(id, gBaseAddr, rowBytes);

    Blit2Screen();
}

/* ------------------------------------------------------------------ */

void InitInterface(void)
{
    int bufSize = 640 * 480 * 2;

    if (!gInterfaceInited) {
        gMainScreenBuf = NewPtrClear(bufSize);
        gHilitBuf      = NewPtrClear(bufSize);
        gSelectedBuf   = NewPtrClear(bufSize);

        if (!gMainScreenBuf || !gHilitBuf || !gSelectedBuf) {
            fprintf(stderr, "InitInterface: failed to allocate screen buffers\n");
            return;
        }

        /* Button rectangles (from PPic 1000/1001 diff analysis) */
        MacSetRect(&gButtons[0], 382,  98, 594, 210); /* Start Game (upper-right) */
        MacSetRect(&gButtons[1], 106, 118, 258, 202); /* Preferences (upper-left) */
        MacSetRect(&gButtons[2],  62, 258, 210, 342); /* High Scores (left-middle) */
        MacSetRect(&gButtons[3], 426, 278, 570, 358); /* Help (right-middle-lower) */
        MacSetRect(&gButtons[4], 462, 386, 610, 466); /* Quit (bottom-right) */
        MacSetRect(&gButtons[5], 250, 222, 398, 306); /* Register (center) */
        MacSetRect(&gButtons[6], 426, 230, 478, 246); /* About (small, right of center) */
        gNumButtons = 7;

        LoadPPic(1000, gMainScreenBuf, 640 * 2);
        LoadPPic(1001, gHilitBuf, 640 * 2);
        LoadPPic(1002, gSelectedBuf, 640 * 2);
        gInterfaceInited = 1;
    }

    ScreenMode(kScreenRunning);
    memcpy(gBaseAddr, gMainScreenBuf, bufSize);
    Blit2Screen();
    FadeScreen(0);
    gGameOn = 0;
}

void DisposeInterface(void)
{
    if (gInterfaceInited) {
        if (gMainScreenBuf) { DisposePtr(gMainScreenBuf); gMainScreenBuf = NULL; }
        if (gHilitBuf)      { DisposePtr(gHilitBuf);      gHilitBuf = NULL; }
        if (gSelectedBuf)   { DisposePtr(gSelectedBuf);   gSelectedBuf = NULL; }
        gInterfaceInited = 0;
    }
}

void ScreenUpdate(WindowPtr win)
{
    int bufSize = 640 * 480 * 2;
    (void)win;
    gButtonLocation = kNoButton;
    memcpy(gBaseAddr, gMainScreenBuf, bufSize);
    Blit2Screen();
}

/* ------------------------------------------------------------------ */

static void DrawScreen(int button, Ptr src)
{
    int rowBytes = 640 * 2;

    if (button != kNoButton) {
        /* Copy just the button rectangle from src to gBaseAddr */
        Rect *r = &gButtons[button];
        int y;
        for (y = r->top; y < r->bottom; y++) {
            memcpy(gBaseAddr + y * rowBytes + r->left * 2,
                   src       + y * rowBytes + r->left * 2,
                   (r->right - r->left) * 2);
        }
        Blit2Screen();
    } else {
        /* Copy entire screen */
        memcpy(gBaseAddr, src, 640 * 480 * 2);
        Blit2Screen();
    }
}

/* ------------------------------------------------------------------ */

void SaveFlushEvents(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
        ; /* discard everything */
}

static void UpdateButtonLocation(void)
{
    int mx, my;
    int i;
    int button = kNoButton;

    SDL_GetMouseState(&mx, &my);
    WindowToFramebuffer(mx, my, &mx, &my);

    for (i = 0; i < gNumButtons; i++) {
        if (mx >= gButtons[i].left && mx < gButtons[i].right &&
            my >= gButtons[i].top  && my < gButtons[i].bottom) {
            button = i;
            break;
        }
    }

    if (button != gButtonLocation) {
        if (gButtonLocation != kNoButton)
            DrawScreen(gButtonLocation, gMainScreenBuf);
        if (button != kNoButton)
            DrawScreen(button, gHilitBuf);
        gButtonLocation = button;
    }
}

static int GetButtonClick(int mx, int my)
{
    int i;
    int button = kNoButton;
    int scaledX, scaledY;
    int clicked = 0, oldClicked = 0;

    /* Scale to virtual coordinates accounting for letterbox viewport */
    WindowToFramebuffer(mx, my, &scaledX, &scaledY);

    /* Find which button was clicked */
    for (i = 0; i < gNumButtons; i++) {
        if (scaledX >= gButtons[i].left && scaledX < gButtons[i].right &&
            scaledY >= gButtons[i].top  && scaledY < gButtons[i].bottom) {
            button = i;
            break;
        }
    }

    if (button == kNoButton)
        return kNoButton;

    fprintf(stderr, "[GetButtonClick] button=%d, calling SimplePlaySound(147)...\n", button);
    SimplePlaySound(147);
    fprintf(stderr, "[GetButtonClick] SimplePlaySound done\n");

    /* Track mouse while button held */
    while (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            ; /* pump events to keep responsive */

        /* Scale current mouse position accounting for letterbox viewport */
        WindowToFramebuffer(mx, my, &scaledX, &scaledY);

        clicked = (scaledX >= gButtons[button].left && scaledX < gButtons[button].right &&
                   scaledY >= gButtons[button].top  && scaledY < gButtons[button].bottom);

        if (clicked) {
            if (clicked != oldClicked)
                DrawScreen(button, gSelectedBuf);
        } else {
            if (clicked != oldClicked)
                DrawScreen(button, gMainScreenBuf);
        }
        oldClicked = clicked;
    }

    if (clicked) {
        DrawScreen(button, gHilitBuf);
        return button;
    }
    return kNoButton;
}

/* ------------------------------------------------------------------ */

void WaitForPress(void)
{
    int pressed = 0;
    SDL_Event event;

    /* Wait for any press */
    while (!pressed) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_KEYDOWN:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_JOYBUTTONDOWN:
                case SDL_CONTROLLERBUTTONDOWN:
                    pressed = 1;
                    break;
            }
        }
        if (ContinuePress())
            pressed = 1;
        SDL_Delay(10);
    }

    /* Wait for release */
    {
        int held = 1;
        while (held) {
            held = 0;
            while (SDL_PollEvent(&event))
                ;
            if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK)
                held = 1;
            {
                const Uint8 *keys = SDL_GetKeyboardState(NULL);
                int numkeys;
                (void)SDL_GetKeyboardState(&numkeys);
                /* Just check if any key is still pressed */
                {
                    int k;
                    for (k = 0; k < numkeys; k++) {
                        if (keys[k]) { held = 1; break; }
                    }
                }
            }
            SDL_Delay(10);
        }
    }

    SaveFlushEvents();
}

/* ------------------------------------------------------------------ */

static void HandleCommand(int cmd)
{
    fprintf(stderr, "[HandleCommand] cmd=%d\n", cmd);
    switch (cmd) {
        case kNoButton:
            return;
        case kStartGameButton:
            fprintf(stderr, "[HandleCommand] StartGame...\n");
            StartGame((SDL_GetModState() & KMOD_ALT) ? 1 : 0);
            fprintf(stderr, "[HandleCommand] StartGame returned\n");
            break;
        case kPrefsButton:
            fprintf(stderr, "[HandleCommand] Preferences...\n");
            Preferences();
            break;
        case kScoreButton:
            fprintf(stderr, "[HandleCommand] ShowHighScores...\n");
            ShowHighScores(-1);
            break;
        case kHelpButton:
            ShowPicScreen(1007);
            WaitForPress();
            ShowPicScreen(1008);
            WaitForPress();
            /* Redraw the menu */
            FadeScreen(1);
            ScreenUpdate(nil);
            FadeScreen(0);
            break;
        case kQuitButton:
            gExit = 1;
            break;
        case kRegisterButton:
            Register(1);
            break;
        case kAboutButton:
            /* No action for About yet */
            break;
    }
}

void Eventloop(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                gExit = 1;
                break;

            case SDL_MOUSEMOTION:
                UpdateButtonLocation();
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    Blit2Screen();
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int btn = GetButtonClick(event.button.x, event.button.y);
                    HandleCommand(btn);
                }
                break;

            case SDL_KEYDOWN:
            {
                int btn = kNoButton;
                SDL_Keycode key = event.key.keysym.sym;

                switch (key) {
                    case SDLK_s:
                    case SDLK_n:
                    case SDLK_RETURN:
                    case SDLK_SPACE:
                        btn = kStartGameButton;
                        break;
                    case SDLK_p:
                        btn = kPrefsButton;
                        break;
                    case SDLK_c:
                    case SDLK_o:
                        btn = kScoreButton;
                        break;
                    case SDLK_h:
                    case SDLK_SLASH: /* '?' is Shift+/ */
                        btn = kHelpButton;
                        break;
                    case SDLK_q:
                    case SDLK_ESCAPE:
                        btn = kQuitButton;
                        break;
                    case SDLK_r:
                        btn = kRegisterButton;
                        break;
                }

                if (btn != kNoButton) {
                    /* Brief visual feedback */
                    DrawScreen(btn, gSelectedBuf);
                    SimplePlaySound(147);
                    SDL_Delay(100);
                    if (gButtonLocation == btn)
                        DrawScreen(btn, gHilitBuf);
                    else
                        DrawScreen(btn, gMainScreenBuf);
                    HandleCommand(btn);
                }
                break;
            }

            case SDL_DROPFILE:
                /* Future: load custom level file from event.drop.file */
                if (event.drop.file) {
                    SDL_free(event.drop.file);
                }
                break;
        }
        /* If game started or we're exiting, stop processing events —
         * the interface buffers have been freed by DisposeInterface() */
        if (gGameOn || gExit)
            break;
    }

    /* Check gamepad continue button */
    if (!gGameOn && ContinuePress())
        HandleCommand(kStartGameButton);
}
