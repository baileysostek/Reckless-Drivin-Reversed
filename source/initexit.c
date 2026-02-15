#include <SDL.h>
#include <math.h>
#include "mac_compat.h"
#include "endian_compat.h"
#include "input.h"
#include "screen.h"
#include "error.h"
#include "sprites.h"
#include "trig.h"
#include "gamesounds.h"
#include "gameinitexit.h"
#include "interface.h"
#include "preferences.h"
#include "random.h"
#include "packs.h"
#include "roads.h"
#include "objects.h"
#include "register.h"

float gSinTab[kSinTabSize];
int gInitSuccessful=false;
int gOSX=0;

#undef sin
#undef cos
void InitTrig()
{
	int i;
	for(i=0;i<kSinTabSize;i++)
		gSinTab[i]=sin(2*3.1415*(float)i/(float)kSinTabSize);
}

UInt32 U32Version(NumVersion v)
{
	return *((UInt32*)(&v));
}

/* Byte-swap a single tObjectType in-place */
static void SwapOneObjectType(tObjectType *ot)
{
	ot->mass = SwapFloat(ot->mass);
	ot->maxEngineForce = SwapFloat(ot->maxEngineForce);
	ot->maxNegEngineForce = SwapFloat(ot->maxNegEngineForce);
	ot->friction = SwapFloat(ot->friction);
	ot->flags = SwapU16(ot->flags);
	ot->deathObj = SwapS16(ot->deathObj);
	ot->frame = SwapS16(ot->frame);
	ot->numFrames = SwapU16(ot->numFrames);
	ot->frameDuration = SwapFloat(ot->frameDuration);
	ot->wheelWidth = SwapFloat(ot->wheelWidth);
	ot->wheelLength = SwapFloat(ot->wheelLength);
	ot->steering = SwapFloat(ot->steering);
	ot->width = SwapFloat(ot->width);
	ot->length = SwapFloat(ot->length);
	ot->score = SwapU16(ot->score);
	ot->flags2 = SwapU16(ot->flags2);
	ot->creationSound = SwapS16(ot->creationSound);
	ot->otherSound = SwapS16(ot->otherSound);
	ot->maxDamage = SwapFloat(ot->maxDamage);
	ot->weaponObj = SwapS16(ot->weaponObj);
	ot->weaponInfo = SwapS16(ot->weaponInfo);
}

static void SwapObTyCallback(Ptr data, int size, int id, void *ctx)
{
	(void)size; (void)ctx;
	SwapOneObjectType((tObjectType*)data);
}

static void SwapPackObTy(void)
{
	ForEachPackEntry(kPackObTy, SwapObTyCallback, NULL);
}

static void SwapOgrpCallback(Ptr data, int size, int id, void *ctx)
{
	tObjectGroup *og = (tObjectGroup*)data;
	UInt32 j, numEntries;
	(void)size; (void)ctx;
	numEntries = SwapU32(og->numEntries);
	og->numEntries = numEntries;
	for(j = 0; j < numEntries; j++) {
		og->data[j].typeRes = SwapS16(og->data[j].typeRes);
		og->data[j].minOffs = SwapS16(og->data[j].minOffs);
		og->data[j].maxOffs = SwapS16(og->data[j].maxOffs);
		og->data[j].probility = SwapS16(og->data[j].probility);
		og->data[j].dir = SwapFloat(og->data[j].dir);
	}
}

static void SwapPackOgrp(void)
{
	ForEachPackEntry(kPackOgrp, SwapOgrpCallback, NULL);
}

static void SwapRoadCallback(Ptr data, int size, int id, void *ctx)
{
	tRoadInfo *ri = (tRoadInfo*)data;
	(void)size; (void)id; (void)ctx;
	ri->friction = SwapFloat(ri->friction);
	ri->airResistance = SwapFloat(ri->airResistance);
	ri->backResistance = SwapFloat(ri->backResistance);
	ri->tolerance = SwapU16(ri->tolerance);
	ri->marks = SwapS16(ri->marks);
	ri->deathOffs = SwapS16(ri->deathOffs);
	ri->backgroundTex = SwapS16(ri->backgroundTex);
	ri->foregroundTex = SwapS16(ri->foregroundTex);
	ri->roadLeftBorder = SwapS16(ri->roadLeftBorder);
	ri->roadRightBorder = SwapS16(ri->roadRightBorder);
	ri->tracks = SwapS16(ri->tracks);
	ri->skidSound = SwapS16(ri->skidSound);
	ri->filler = SwapS16(ri->filler);
	ri->xDrift = SwapFloat(ri->xDrift);
	ri->yDrift = SwapFloat(ri->yDrift);
	ri->xFrontDrift = SwapFloat(ri->xFrontDrift);
	ri->yFrontDrift = SwapFloat(ri->yFrontDrift);
	ri->trackSlide = SwapFloat(ri->trackSlide);
	ri->dustSlide = SwapFloat(ri->dustSlide);
	ri->filler2 = SwapU16(ri->filler2);
	ri->slideFriction = SwapFloat(ri->slideFriction);
}

static void SwapPackRoad(void)
{
	ForEachPackEntry(kPackRoad, SwapRoadCallback, NULL);
}

void Init()
{
	fprintf(stderr, "[Init] Randomize...\n");
	Randomize();
	fprintf(stderr, "[Init] LoadPrefs...\n");
	LoadPrefs();
	fprintf(stderr, "[Init] CheckRegi...\n");
	CheckRegi();
	fprintf(stderr, "[Init] InitScreen...\n");
	InitScreen();
	fprintf(stderr, "[Init] ShowPicScreen...\n");
	ShowPicScreen(1003);
	fprintf(stderr, "[Init] LoadPack(kPackSnds)...\n");
	LoadPack(kPackSnds);
	fprintf(stderr, "[Init] LoadPack(kPackObTy)...\n");
	LoadPack(kPackObTy);
	SwapPackObTy();
	fprintf(stderr, "[Init] LoadPack(kPackOgrp)...\n");
	LoadPack(kPackOgrp);
	SwapPackOgrp();
	fprintf(stderr, "[Init] LoadPack(kPackRoad)...\n");
	LoadPack(kPackRoad);
	SwapPackRoad();
	/* 16-bit color mode only */
	fprintf(stderr, "[Init] LoadPack(kPacksR16)...\n");
	LoadPack(kPacksR16);
	fprintf(stderr, "[Init] LoadPack(kPackcR16)...\n");
	LoadPack(kPackcR16);
	fprintf(stderr, "[Init] LoadPack(kPackTx16)...\n");
	LoadPack(kPackTx16);
	fprintf(stderr, "[Init] LoadSprites...\n");
	LoadSprites();
	fprintf(stderr, "[Init] InitTrig...\n");
	InitTrig();
	fprintf(stderr, "[Init] InitInput...\n");
	InitInput();
	fprintf(stderr, "[Init] SetGameVolume...\n");
	SetGameVolume(-1);
	fprintf(stderr, "[Init] InitChannels...\n");
	InitChannels();
	fprintf(stderr, "[Init] InitInterface...\n");
	InitInterface();
	fprintf(stderr, "[Init] Done!\n");
	gInitSuccessful=true;
}

void Exit()
{
	if(gInitSuccessful)
	{
		WritePrefs(false);
		FadeScreen(1);
		ScreenMode(kScreenSuspended);
		SetSystemVolume();
		FadeScreen(256);
		FadeScreen(0);
		ScreenMode(kScreenStopped);
		InputMode(kInputStopped);
	}
	SDL_Quit();
	exit(0);
}
