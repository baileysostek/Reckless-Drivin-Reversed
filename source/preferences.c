#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "mac_compat.h"
#include "error.h"
#include "input.h"
#include "preferences.h"
#include "interface.h"
#include "gamesounds.h"
#include "packs.h"
#include "screen.h"
#include "sprites.h"

/* From platform_screen.c */
extern void ResizeFramebuffer(int newWidth);
extern int ComputeWidescreenWidth(int winW, int winH);
extern void WindowToFramebuffer(int winX, int winY, int *fbX, int *fbY);
extern void SetFullscreen(int enable);
extern void Blit2Screen(void);
extern void FadeScreen(int);
extern void ScreenUpdate(WindowPtr win);

#include "font5x7.h"

tPrefs gPrefs;
extern int gOSX;

#ifdef _WIN32
#include <shlobj.h>
static const char* GetPrefsPath(void)
{
	static char path[512];
	char *appdata = getenv("APPDATA");
	if(appdata)
		snprintf(path, sizeof(path), "%s/RecklessDrivin.prefs", appdata);
	else
		snprintf(path, sizeof(path), "RecklessDrivin.prefs");
	return path;
}
#else
static const char* GetPrefsPath(void)
{
	static char path[512];
	char *home = getenv("HOME");
	if(home)
		snprintf(path, sizeof(path), "%s/.recklessdrivin/prefs", home);
	else
		snprintf(path, sizeof(path), "RecklessDrivin.prefs");
	return path;
}
#endif

static void FirstRun(void)
{
	memset(&gPrefs, 0, sizeof(tPrefs));
	gPrefs.version = kPrefsVersion;
	gPrefs.volume = 200;
	gPrefs.sound = 1;
	gPrefs.engineSound = 1;
	gPrefs.hqSound = 1;
	gPrefs.hiColor = 1;
	gPrefs.lineSkip = 0;
	gPrefs.motionBlur = 0;
	gPrefs.fullscreen = 1;
	gPrefs.widescreen = 1;
	/* Default key codes - SDL scancodes for arrow keys etc */
	gPrefs.keyCodes[kForward] = 82;   /* SDL_SCANCODE_UP */
	gPrefs.keyCodes[kBackward] = 81;  /* SDL_SCANCODE_DOWN */
	gPrefs.keyCodes[kLeft] = 80;      /* SDL_SCANCODE_LEFT */
	gPrefs.keyCodes[kRight] = 79;     /* SDL_SCANCODE_RIGHT */
	gPrefs.keyCodes[kKickdown] = 225; /* SDL_SCANCODE_LSHIFT */
	gPrefs.keyCodes[kBrake] = 44;     /* SDL_SCANCODE_SPACE */
	gPrefs.keyCodes[kFire] = 29;      /* SDL_SCANCODE_Z */
	gPrefs.keyCodes[kMissile] = 27;   /* SDL_SCANCODE_X */
}

void LoadPrefs(void)
{
	const char *path = GetPrefsPath();
	FILE *f = fopen(path, "rb");
	if(f)
	{
		long count;
		fseek(f, 0, SEEK_END);
		count = ftell(f);
		fseek(f, 0, SEEK_SET);
		if(count == sizeof(tPrefs))
		{
			fread(&gPrefs, 1, sizeof(tPrefs), f);
			fclose(f);
			if(gPrefs.version != kPrefsVersion)
			{
				FirstRun();
				WritePrefs(0);
			}
		}
		else
		{
			fclose(f);
			FirstRun();
			WritePrefs(0);
		}
	}
	else
	{
		FirstRun();
		WritePrefs(1);
	}
	/* Force 16-bit color in SDL port */
	gPrefs.hiColor = 1;
	gPrefs.lineSkip = 0;
}

void WritePrefs(int reset)
{
	const char *path;
	FILE *f;
	if(reset)
		FirstRun();
	path = GetPrefsPath();
	f = fopen(path, "wb");
	if(f)
	{
		fwrite(&gPrefs, 1, sizeof(tPrefs), f);
		fclose(f);
	}
}

void ReInitGraphics(void)
{
	/* In SDL port, we only support 16-bit color, so this is mostly a no-op */
}

/* ------------------------------------------------------------------ */
/* Preferences UI layout constants                                     */
/* ------------------------------------------------------------------ */
#define kPrefsTitle       "PREFERENCES"
#define kPrefsTitleY      100
#define kPrefsTitleScale  3

#define kPrefsCheckScale  2
#define kPrefsCheckCharW  (6 * kPrefsCheckScale)  /* pixel width of one char */
#define kPrefsCheckH      (7 * kPrefsCheckScale)  /* pixel height of one line */

/* Vertical position of each checkbox row */
#define kPrefsRow1Y       180
#define kPrefsRow2Y       220
#define kPrefsHintY       300

/* Horizontal offset from center for checkbox text */
#define kPrefsCheckX      180   /* in 640-space, offset from left */

static void DrawPrefsScreen(void)
{
    int xOff = (gXSize - 640) / 2;
    const char *fs_label = gPrefs.fullscreen ? "[X] Fullscreen" : "[ ] Fullscreen";
    const char *ws_label = gPrefs.widescreen ? "[X] Widescreen" : "[ ] Widescreen";

    /* Clear framebuffer to black */
    memset(gBaseAddr, 0, gXSize * gYSize * 2);

    /* Title */
    DrawStringCentered5x7(kPrefsTitleY, kPrefsTitle, kPrefsTitleScale, kColorYellowBE);

    /* Checkbox rows */
    DrawString5x7(kPrefsCheckX + xOff, kPrefsRow1Y, fs_label, kPrefsCheckScale, kColorWhiteBE);
    DrawString5x7(kPrefsCheckX + xOff, kPrefsRow2Y, ws_label, kPrefsCheckScale, kColorCyanBE);

    /* Hint */
    DrawStringCentered5x7(kPrefsHintY, "Press ESC to return", kPrefsCheckScale, kColorGrayBE);

    Blit2Screen();
}

void Preferences(void)
{
    int done = 0;
    SDL_Event event;

    DrawPrefsScreen();

    while (!done) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE ||
                        event.key.keysym.sym == SDLK_RETURN) {
                        done = 1;
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        int fbX, fbY;
                        int xOff = (gXSize - 640) / 2;
                        int textLeft = kPrefsCheckX + xOff;
                        int textRight = textLeft + 14 * kPrefsCheckCharW;

                        WindowToFramebuffer(event.button.x, event.button.y, &fbX, &fbY);

                        /* Check fullscreen row */
                        if (fbX >= textLeft && fbX < textRight &&
                            fbY >= kPrefsRow1Y && fbY < kPrefsRow1Y + kPrefsCheckH + 4) {
                            gPrefs.fullscreen = !gPrefs.fullscreen;
                            SetFullscreen(gPrefs.fullscreen);
                            DrawPrefsScreen();
                        }
                        /* Check widescreen row */
                        else if (fbX >= textLeft && fbX < textRight &&
                                 fbY >= kPrefsRow2Y && fbY < kPrefsRow2Y + kPrefsCheckH + 4) {
                            gPrefs.widescreen = !gPrefs.widescreen;
                            if (gPrefs.widescreen) {
                                int winW, winH;
                                SDL_GetWindowSize(SDL_GL_GetCurrentWindow(), &winW, &winH);
                                ResizeFramebuffer(ComputeWidescreenWidth(winW, winH));
                            } else {
                                ResizeFramebuffer(640);
                            }
                            DrawPrefsScreen();
                        }
                    }
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        if (gPrefs.widescreen)
                            ResizeFramebuffer(ComputeWidescreenWidth(
                                event.window.data1, event.window.data2));
                        DrawPrefsScreen();
                    } else if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                        Blit2Screen();
                    }
                    break;

                case SDL_QUIT:
                    SDL_Quit();
                    exit(0);
            }
        }
        SDL_Delay(10);
    }

    /* Save preferences and return to menu */
    WritePrefs(0);
    FadeScreen(0);
    ScreenUpdate(nil);
}
