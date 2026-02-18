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

extern Ptr gBaseAddr;
extern short gRowBytes, gXSize, gYSize;
extern void Blit2Screen(void);
extern void FadeScreen(int);
extern void ScreenUpdate(WindowPtr win);
extern short gLevelResFile;
extern void ShowPicScreenNoFade(PPicID id);
extern void ResizeFramebuffer(int newWidth);
extern int ComputeWidescreenWidth(int winW, int winH);

/* ------------------------------------------------------------------ */
/* Built-in 5x7 bitmap font for score screen rendering.               */
/* Each character is 5 columns x 7 rows, stored as 7 bytes (1 bit per */
/* column, MSB = leftmost). Works without sprite packs loaded.        */
/* ------------------------------------------------------------------ */

static const unsigned char kFont5x7[96][7] = {
    /* ' ' (32) */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '!' (33) */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    /* '"' (34) */ {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},
    /* '#' (35) */ {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00},
    /* '$' (36) */ {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
    /* '%' (37) */ {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    /* '&' (38) */ {0x08,0x14,0x14,0x08,0x15,0x12,0x0D},
    /* '\''(39) */ {0x04,0x04,0x00,0x00,0x00,0x00,0x00},
    /* '(' (40) */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    /* ')' (41) */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    /* '*' (42) */ {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
    /* '+' (43) */ {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    /* ',' (44) */ {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
    /* '-' (45) */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    /* '.' (46) */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    /* '/' (47) */ {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
    /* '0' (48) */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* '1' (49) */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* '2' (50) */ {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    /* '3' (51) */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    /* '4' (52) */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* '5' (53) */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* '6' (54) */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* '7' (55) */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    /* '8' (56) */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* '9' (57) */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* ':' (58) */ {0x00,0x00,0x04,0x00,0x04,0x00,0x00},
    /* ';' (59) */ {0x00,0x00,0x04,0x00,0x04,0x04,0x08},
    /* '<' (60) */ {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
    /* '=' (61) */ {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    /* '>' (62) */ {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
    /* '?' (63) */ {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    /* '@' (64) */ {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},
    /* 'A' (65) */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 'B' (66) */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* 'C' (67) */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* 'D' (68) */ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    /* 'E' (69) */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* 'F' (70) */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* 'G' (71) */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    /* 'H' (72) */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 'I' (73) */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* 'J' (74) */ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    /* 'K' (75) */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* 'L' (76) */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* 'M' (77) */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* 'N' (78) */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* 'O' (79) */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 'P' (80) */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* 'Q' (81) */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* 'R' (82) */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* 'S' (83) */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    /* 'T' (84) */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* 'U' (85) */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 'V' (86) */ {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    /* 'W' (87) */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* 'X' (88) */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* 'Y' (89) */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* 'Z' (90) */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    /* '[' (91) */ {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
    /* '\\' (92)*/ {0x10,0x10,0x08,0x04,0x02,0x01,0x01},
    /* ']' (93) */ {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
    /* '^' (94) */ {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
    /* '_' (95) */ {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
    /* '`' (96) */ {0x08,0x04,0x00,0x00,0x00,0x00,0x00},
    /* 'a' (97) */ {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    /* 'b' (98) */ {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    /* 'c' (99) */ {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},
    /* 'd' (100)*/ {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    /* 'e' (101)*/ {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    /* 'f' (102)*/ {0x06,0x08,0x1E,0x08,0x08,0x08,0x08},
    /* 'g' (103)*/ {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
    /* 'h' (104)*/ {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
    /* 'i' (105)*/ {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
    /* 'j' (106)*/ {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
    /* 'k' (107)*/ {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    /* 'l' (108)*/ {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* 'm' (109)*/ {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
    /* 'n' (110)*/ {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    /* 'o' (111)*/ {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    /* 'p' (112)*/ {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    /* 'q' (113)*/ {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
    /* 'r' (114)*/ {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    /* 's' (115)*/ {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
    /* 't' (116)*/ {0x08,0x08,0x1E,0x08,0x08,0x09,0x06},
    /* 'u' (117)*/ {0x00,0x00,0x11,0x11,0x11,0x13,0x0D},
    /* 'v' (118)*/ {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    /* 'w' (119)*/ {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
    /* 'x' (120)*/ {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    /* 'y' (121)*/ {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    /* 'z' (122)*/ {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
    /* '{' (123)*/ {0x02,0x04,0x04,0x08,0x04,0x04,0x02},
    /* '|' (124)*/ {0x04,0x04,0x04,0x04,0x04,0x04,0x04},
    /* '}' (125)*/ {0x08,0x04,0x04,0x02,0x04,0x04,0x08},
    /* '~' (126)*/ {0x00,0x00,0x08,0x15,0x02,0x00,0x00},
    /* DEL (127)*/ {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},
};

/*
 * DrawChar5x7 - Draw a single character at (px, py) in the framebuffer.
 * Scale controls pixel size (1=normal, 2=double, etc).
 * Color is a big-endian 16-bit pixel value.
 */
static void DrawChar5x7(int px, int py, char ch, int scale, UInt16 colorBE)
{
    int idx, row, col;
    int rowBytes = gXSize * 2;

    if (ch < 32 || ch > 127) ch = '?';
    idx = ch - 32;

    for (row = 0; row < 7; row++) {
        unsigned char bits = kFont5x7[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                int sx, sy;
                for (sy = 0; sy < scale; sy++) {
                    int y = py + row * scale + sy;
                    if (y < 0 || y >= gYSize) continue;
                    for (sx = 0; sx < scale; sx++) {
                        int x = px + col * scale + sx;
                        if (x < 0 || x >= gXSize) continue;
                        *(UInt16 *)(gBaseAddr + y * rowBytes + x * 2) = colorBE;
                    }
                }
            }
        }
    }
}

/*
 * DrawString5x7 - Draw a null-terminated C string at (px, py).
 * Returns the X position after the last character.
 */
static int DrawString5x7(int px, int py, const char *str, int scale, UInt16 colorBE)
{
    while (*str) {
        DrawChar5x7(px, py, *str, scale, colorBE);
        px += 6 * scale;  /* 5 pixels + 1 pixel gap */
        str++;
    }
    return px;
}

/*
 * DrawPascalString5x7 - Draw a Pascal string (length byte + chars).
 */
static int DrawPascalString5x7(int px, int py, const unsigned char *pstr, int scale, UInt16 colorBE)
{
    int i;
    int len = pstr[0];
    for (i = 1; i <= len; i++) {
        DrawChar5x7(px, py, (char)pstr[i], scale, colorBE);
        px += 6 * scale;
    }
    return px;
}

/*
 * DrawNumber5x7 - Draw an integer right-aligned ending at px.
 */
static void DrawNumber5x7(int px, int py, unsigned long num, int scale, UInt16 colorBE)
{
    char buf[16];
    int len, i;
    sprintf(buf, "%lu", num);
    len = (int)strlen(buf);
    /* Right-align: draw from right to left */
    px -= (len - 1) * 6 * scale;
    for (i = 0; i < len; i++) {
        DrawChar5x7(px, py, buf[i], scale, colorBE);
        px += 6 * scale;
    }
}

/* ------------------------------------------------------------------ */
/* Big-endian 16-bit color constants (1-5-5-5 format)                 */
/* ------------------------------------------------------------------ */
/* White: 0x7FFF native -> BE = 0xFF7F */
#define kColorWhiteBE  0xFF7F
/* Yellow: R=31 G=31 B=0 -> native 0x7FE0 -> BE = 0xE07F */
#define kColorYellowBE 0xE07F
/* Cyan: R=0 G=31 B=31 -> native 0x03FF -> BE = 0xFF03 */
#define kColorCyanBE   0xFF03
/* Green: R=0 G=31 B=0 -> native 0x03E0 -> BE = 0xE003 */
#define kColorGreenBE  0xE003
/* Gray: R=16 G=16 B=16 -> native 0x4210 -> BE = 0x1042 */
#define kColorGrayBE   0x1042

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

        if (gPrefs.high[i].name[0] > 0) {
            DrawPascalString5x7(kScoreNameX + xOff, y, gPrefs.high[i].name, kScoreScale, color);
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
 * SDL text input events. Stores result as a Pascal string in outName.
 * Max 15 chars (Str15). Returns 1 on success, 0 if cancelled.
 */
static int GetPlayerName(unsigned char *outName, UInt32 score)
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
        /* Use default name */
        outName[0] = 6;
        memcpy(outName + 1, "Player", 6);
    } else {
        /* Store as Pascal string */
        outName[0] = (unsigned char)nameLen;
        memcpy(outName + 1, nameBuf, nameLen);
    }
    return !cancelled;
}

extern int gOSX;

void SetHighScoreEntry(int index, UInt32 score)
{
    unsigned char name[16];

    GetPlayerName(name, score);

    memcpy(gPrefs.high[index].name, name, name[0] + 1);
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
