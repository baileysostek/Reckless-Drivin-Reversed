#include <string.h>
#include <stdio.h>
#include <time.h>
#include <SDL.h>

#include "resources.h"
#include "mac_compat.h"
#include "preferences.h"
#include "interface.h"
#include "screen.h"
#include "error.h"
#include "gamesounds.h"
#include "endian_compat.h"

extern short gRowBytes, gXSize, gYSize;
extern void Blit2Screen(void);
extern void FadeScreen(int);
extern void ScreenUpdate(WindowPtr win);
extern short gLevelResFile;
extern void ShowPicScreenNoFade(PPicID id);
extern void ResizeFramebuffer(int newWidth);
extern int ComputeWidescreenWidth(int winW, int winH);

#include "font5x7.h"

/* ------------------------------------------------------------------ */
/* High score display layout                                          */
/* ------------------------------------------------------------------ */
#define kScoreStartY    150
#define kScoreLineH     28
#define kScoreScale     2
#define kScoreNameX     140
#define kScoreNumX      500

static void DrawHighScoreEntries(int hilite)
{
    int i;
    int xOff = (gXSize - 640) / 2;

    ShowPicScreenNoFade(PPIC_HIGH_SCORES);

    for (i = 0; i < kNumHighScoreEntrys; i++) {
        int y = kScoreStartY + i * kScoreLineH;
        UInt16 color = (i == hilite) ? kColorYellowBE : kColorWhiteBE;
        char rankBuf[4];

        sprintf(rankBuf, "%d.", i + 1);
        DrawString5x7(100 + xOff, y, rankBuf, kScoreScale, color);

        if (gPrefs.high[i].name[0]) {
            DrawString5x7(kScoreNameX + xOff, y, gPrefs.high[i].name, kScoreScale, color);
        } else {
            DrawString5x7(kScoreNameX + xOff, y, "---", kScoreScale, color);
        }

        if (gPrefs.high[i].score > 0) {
            DrawNumber5x7(kScoreNumX + xOff, y, gPrefs.high[i].score, kScoreScale, color);
        }
    }

    FadeScreen(0);
    Blit2Screen();
}

void ShowHighScores(int hilite)
{
    int pressed = 0;
    SDL_Event event;

    DrawHighScoreEntries(hilite);

    /* Wait for press, redrawing on resize */
    while (!pressed) {
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
                        if (gPrefs.widescreen)
                            ResizeFramebuffer(ComputeWidescreenWidth(
                                event.window.data1, event.window.data2));
                        DrawHighScoreEntries(hilite);
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

/* ------------------------------------------------------------------ */
/* SDL text input for player name entry                               */
/* ------------------------------------------------------------------ */

/*
 * GetPlayerName - Show a prompt and let the player type their name using
 * SDL text input events. Stores result as a C string in outName.
 * Max 15 chars. Returns 1 on success, 0 if cancelled.
 */
static int GetPlayerName(char *outName, UInt32 score)
{
    char nameBuf[16] = {0};
    int nameLen = 0;
    int done = 0;
    int cancelled = 0;
    int cursorVisible = 1;
    Uint32 lastBlink = SDL_GetTicks();
    SDL_Event event;

    SDL_StartTextInput();
    SaveFlushEvents();

    while (!done) {
        Uint32 now = SDL_GetTicks();

        /* Blink cursor every 500ms */
        if (now - lastBlink >= 500) {
            cursorVisible = !cursorVisible;
            lastBlink = now;
        }

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    done = 1;
                    cancelled = 1;
                    break;

                case SDL_TEXTINPUT:
                {
                    /* Append typed characters */
                    const char *text = event.text.text;
                    int i;
                    for (i = 0; text[i] && nameLen < 15; i++) {
                        char ch = text[i];
                        /* Accept printable ASCII only */
                        if (ch >= 32 && ch <= 126) {
                            nameBuf[nameLen++] = ch;
                        }
                    }
                    cursorVisible = 1;
                    lastBlink = now;
                    break;
                }

                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            done = 1;
                            break;
                        case SDLK_ESCAPE:
                            done = 1;
                            cancelled = 1;
                            break;
                        case SDLK_BACKSPACE:
                            if (nameLen > 0)
                                nameLen--;
                            cursorVisible = 1;
                            lastBlink = now;
                            break;
                    }
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        if (gPrefs.widescreen)
                            ResizeFramebuffer(ComputeWidescreenWidth(
                                event.window.data1, event.window.data2));
                    }
                    break;
            }
        }

        /* Redraw the input screen */
        {
            int xOff = (gXSize - 640) / 2;
            memset(gBaseAddr, 0, gXSize * gYSize * 2);

            DrawString5x7(160 + xOff, 140, "NEW HIGH SCORE!", 3, kColorYellowBE);

            {
                char scoreLine[32];
                sprintf(scoreLine, "Score: %lu", (unsigned long)score);
                DrawString5x7(200 + xOff, 190, scoreLine, 2, kColorWhiteBE);
            }

            DrawString5x7(130 + xOff, 240, "Enter your name:", 2, kColorCyanBE);

            /* Draw name with cursor */
            {
                char displayBuf[20];
                memcpy(displayBuf, nameBuf, nameLen);
                if (cursorVisible)
                    displayBuf[nameLen] = '_';
                else
                    displayBuf[nameLen] = ' ';
                displayBuf[nameLen + 1] = '\0';
                DrawString5x7(180 + xOff, 280, displayBuf, 3, kColorWhiteBE);
            }

            DrawString5x7(140 + xOff, 360, "Press ENTER to confirm", 2, kColorGrayBE);
            DrawString5x7(160 + xOff, 390, "Press ESC to cancel", 2, kColorGrayBE);
        }

        Blit2Screen();
        SDL_Delay(16);
    }

    SDL_StopTextInput();

    if (cancelled || nameLen == 0) {
        strcpy(outName, "Player");
    } else {
        memcpy(outName, nameBuf, nameLen);
        outName[nameLen] = '\0';
    }
    return !cancelled;
}

extern int gOSX;

void SetHighScoreEntry(int index, UInt32 score)
{
    char name[16];

    GetPlayerName(name, score);

    strcpy(gPrefs.high[index].name, name);
    gPrefs.high[index].score = score;
    gPrefs.high[index].time = (UInt32)time(NULL);
}

void CheckHighScore(UInt32 score)
{
    int i;
    if (gLevelResFile) return;
    for (i = kNumHighScoreEntrys; score > gPrefs.high[i - 1].score && i > 0; i--)
        ;
    if (i < kNumHighScoreEntrys) {
        BlockMoveData(gPrefs.high + i, gPrefs.high + i + 1,
                      sizeof(tScoreRecord) * (kNumHighScoreEntrys - i - 1));
        SimplePlaySound(153);
        SetHighScoreEntry(i, score);
        WritePrefs(false);
        ShowHighScores(i);
    }
}

void ClearHighScores()
{
    int i;
    for (i = 0; i < kNumHighScoreEntrys; i++) {
        memset(&gPrefs.high[i], 0, sizeof(tScoreRecord));
    }
    WritePrefs(false);
}
