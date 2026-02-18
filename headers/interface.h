#ifndef __INTERFACE
#define __INTERFACE

#include "mac_compat.h"
#include "resources.h"

extern int gExit;
extern short gLevelResFile,gAppResFile;
extern Str63 gLevelFileName;

void SaveFlushEvents();
void Eventloop();
void InitInterface();
void DisposeInterface();
void ScreenUpdate(WindowPtr win);
void ShowPicScreen(PPicID id);
void ShowPicScreenNoFade(PPicID id);
int LoadPPic(PPicID id, Ptr destBuf, int rowBytes);
int LoadBMP(const char *path, Ptr destBuf, int destRowBytes,
            int destX, int destY, int destW, int destH);
void WaitForPress();

#endif