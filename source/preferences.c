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

/* From platform_screen.c - avoid including platform_screen.h (conflicts with screen.h) */
extern void ResizeFramebuffer(int newWidth);
extern int ComputeWidescreenWidth(int winW, int winH);

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

void Preferences(void)
{
    int pressed = 0;
    SDL_Event event;

    /* Wait for press, redrawing on resize */
    while (!pressed) {
				Blit2Screen();
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_KEYDOWN:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_JOYBUTTONDOWN:
                case SDL_CONTROLLERBUTTONDOWN:
                    pressed = 1;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        ResizeFramebuffer(ComputeWidescreenWidth(event.window.data1, event.window.data2));
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

    /* Restore menu screen directly — no fade-to-black transition */
    FadeScreen(0);
    ScreenUpdate(nil);
}
