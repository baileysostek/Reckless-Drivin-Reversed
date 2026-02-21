#include <stdio.h>
#include <SDL.h>
#include "input.h"
#include "screen.h"
#include "gameframe.h"
#include "roads.h"
#include "objects.h"
#include "error.h"
#include "trig.h"
#include "gamesounds.h"
#include "renderframe.h"
#include "interface.h"
#include "screenfx.h"
#include "textfx.h"
#include "sprites.h"
#include "packs.h"
#include "high.h"
#include "register.h"
#include "preferences.h"
#include "mac_compat.h"
#include "endian_compat.h"

tRoad gRoadData;	
UInt32 *gRoadLenght;
tRoadInfo *gRoadInfo;
tLevelData *gLevelData;
tTrackInfo *gTrackUp,*gTrackDown;
tMarkSeg *gMarks;
int gMarkSize;
int gLevelID;
tObject *gFirstObj,*gCameraObj,*gPlayerObj,*gSpikeObj,*gBrakeObj,*gFirstVisObj,*gLastVisObj;
tTrackSeg gTracks[kMaxTracks];
int gTrackCount;
int gPlayerLives,gExtraLives;
int gNumMissiles,gNumMines;
float gPlayerDeathDelay,gFinishDelay;
int gPlayerScore,gDisplayScore;
int gPlayerBonus;
UInt32 gPlayerAddOns;
float gGameTime;
float gXDriftPos,gYDriftPos,gXFrontDriftPos,gYFrontDriftPos,gZoomVelo;
int gGameOn;
int gPlayerCarID;
float gPlayerSlide[4]={0,0,0,0};
float gSpikeFrame;
int gLCheat;

int abs(int x);
void CopClear();

/* Byte-swap big-endian pack data structures to native endianness */
static void SwapLevelData(tLevelData *ld)
{
	int i;
	ld->roadInfo = SwapS16(ld->roadInfo);
	ld->time = SwapU16(ld->time);
	for(i = 0; i < 10; i++) {
		ld->objGrps[i].resID = SwapS16(ld->objGrps[i].resID);
		ld->objGrps[i].numObjs = SwapS16(ld->objGrps[i].numObjs);
	}
	ld->xStartPos = SwapS16(ld->xStartPos);
	ld->levelEnd = SwapU16(ld->levelEnd);
}

static void SwapRoadInfo(tRoadInfo *ri)
{
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
	/* dustColor and water are UInt8 - no swap needed */
	ri->filler2 = SwapU16(ri->filler2);
	ri->slideFriction = SwapFloat(ri->slideFriction);
}

static void SwapTrackInfo(tTrackInfo *ti)
{
	UInt32 i;
	ti->num = SwapU32(ti->num);
	for(i = 0; i < ti->num; i++) {
		ti->track[i].flags = SwapU16(ti->track[i].flags);
		ti->track[i].x = SwapS16(ti->track[i].x);
		ti->track[i].y = SwapS32(ti->track[i].y);
		ti->track[i].velo = SwapFloat(ti->track[i].velo);
	}
}

static void SwapMarkSegs(tMarkSeg *marks, int count)
{
	int i;
	for(i = 0; i < count; i++) {
		marks[i].p1.x = SwapFloat(marks[i].p1.x);
		marks[i].p1.y = SwapFloat(marks[i].p1.y);
		marks[i].p2.x = SwapFloat(marks[i].p2.x);
		marks[i].p2.y = SwapFloat(marks[i].p2.y);
	}
}

static void SwapObjectPos(tObjectPos *op)
{
	op->x = SwapS32(op->x);
	op->y = SwapS32(op->y);
	op->dir = SwapFloat(op->dir);
	op->typeRes = SwapS16(op->typeRes);
	op->filler = SwapS16(op->filler);
}

Ptr LoadObjs(Ptr dataPos)
{
	int i;
	UInt32 numObjs;
	tObjectPos *objs=(tObjectPos*)(dataPos+sizeof(UInt32));
	numObjs = SwapU32(*(UInt32*)dataPos);
	*(UInt32*)dataPos = numObjs; /* write back swapped value */
	for(i=0;i<(int)numObjs;i++)
	{
		SwapObjectPos(&objs[i]);
		{
			tObject *theObj=NewObject(gFirstObj,objs[i].typeRes);
			theObj->dir=objs[i].dir;
			theObj->pos.x=objs[i].x;
			theObj->pos.y=objs[i].y;
		}
	}
	return (Ptr)(objs+numObjs);
}

int NumLevels()
{
	int count = 0;
	int i;
	char path[256];
	FILE *f;
	for(i = 0; i < 10; i++) {
		snprintf(path, sizeof(path), "assets/pack_%d.bin", 140 + i);
		f = fopen(path, "rb");
		if(f) { fclose(f); count++; }
		else break;
	}
	if(count == 0) count = 10;
	return count;
}

void GameEndSequence();

int LoadLevel()
{
	int i,sound;
	if(gLevelID>=kEncryptedPack-kPackLevel1||gLevelResFile)
		if(!gRegistered)
		{
			ShowPicScreen(PPIC_SEEN_EVERYTHING_TEXT);
			WaitForPress();
			BeQuiet();
			InitInterface();
			MacShowCursor();
			if(!gLCheat)
				CheckHighScore(gPlayerScore);
			return false;
		}

	gFirstObj=(tObject*)NewPtrClear(sizeof(tObject));
	gFirstObj->next=gFirstObj;
	gFirstObj->prev=gFirstObj;

	if(gLevelID>=NumLevels())
	{
		GameEndSequence();
		gLevelID=0;
	}

	fprintf(stderr, "[LoadLevel] Loading level %d\n", gLevelID);
	LoadPack(kPackLevel1+gLevelID);
	fprintf(stderr, "[LoadLevel] GetSortedPackEntry for levelData...\n");
	gLevelData=(tLevelData*)GetSortedPackEntry(kPackLevel1+gLevelID,1,nil);
	if(!gLevelData) { fprintf(stderr, "[LoadLevel] FATAL: gLevelData is NULL!\n"); return false; }
	fprintf(stderr, "[LoadLevel] SwapLevelData...\n");
	SwapLevelData(gLevelData);
	fprintf(stderr, "[LoadLevel] GetSortedPackEntry for marks...\n");
	gMarks=(tMarkSeg*)GetSortedPackEntry(kPackLevel1+gLevelID,2,&gMarkSize);
	if(!gMarks) { fprintf(stderr, "[LoadLevel] FATAL: gMarks is NULL!\n"); return false; }
	gMarkSize/=sizeof(tMarkSeg);
	SwapMarkSegs(gMarks, gMarkSize);
	fprintf(stderr, "[LoadLevel] GetSortedPackEntry for roadInfo (id=%d)...\n", gLevelData->roadInfo);
	gRoadInfo=(tRoadInfo*)GetSortedPackEntry(kPackRoad,gLevelData->roadInfo,nil);
	if(!gRoadInfo) { fprintf(stderr, "[LoadLevel] FATAL: gRoadInfo is NULL!\n"); return false; }
	/* tRoadInfo already byte-swapped at init time by SwapPackRoad */
	fprintf(stderr, "[LoadLevel] Parsing tracks and objects...\n");
	gTrackUp=(tTrackInfo*)((Ptr)gLevelData+sizeof(tLevelData));
	SwapTrackInfo(gTrackUp);
	gTrackDown=(tTrackInfo*)((Ptr)gTrackUp+sizeof(UInt32)+gTrackUp->num*sizeof(tTrackInfoSeg));
	SwapTrackInfo(gTrackDown);
	gRoadLenght=(UInt32*)LoadObjs((Ptr)gTrackDown+sizeof(UInt32)+gTrackDown->num*sizeof(tTrackInfoSeg));
	*gRoadLenght = SwapU32(*gRoadLenght);
	gRoadData=(tRoad)((Ptr)gRoadLenght+sizeof(UInt32));
	fprintf(stderr, "[LoadLevel] Road: %u segments, water=%d\n", *gRoadLenght, gRoadInfo->water);

	/* Byte-swap road segment data (SInt16[4] per segment) */
	{
		UInt32 roadSegs = *gRoadLenght;
		UInt32 s;
		for(s = 0; s < roadSegs; s++) {
			gRoadData[s][0] = SwapS16(gRoadData[s][0]);
			gRoadData[s][1] = SwapS16(gRoadData[s][1]);
			gRoadData[s][2] = SwapS16(gRoadData[s][2]);
			gRoadData[s][3] = SwapS16(gRoadData[s][3]);
		}
	}

	fprintf(stderr, "[LoadLevel] Inserting object groups...\n");
	for(i=0;i<9;i++)
		if((*gLevelData).objGrps[i].resID)
		{
			fprintf(stderr, "[LoadLevel]   group %d: resID=%d numObjs=%d\n", i, (*gLevelData).objGrps[i].resID, (*gLevelData).objGrps[i].numObjs);
			InsertObjectGroup((*gLevelData).objGrps[i]);
		}

	/* Validate linked list integrity before creating player */
	{
		tObject *check = (tObject*)gFirstObj->next;
		int count = 0;
		while(check != gFirstObj && count < 50000) {
			if(!check) { fprintf(stderr, "[LoadLevel] ERROR: NULL in linked list at count=%d!\n", count); break; }
			if(check->prev == NULL) { fprintf(stderr, "[LoadLevel] ERROR: NULL prev at count=%d obj=%p!\n", count, (void*)check); break; }
			count++;
			check = (tObject*)check->next;
		}
		fprintf(stderr, "[LoadLevel] Object list: %d objects, gFirstObj=%p\n", count, (void*)gFirstObj);
	}

	{
		int carType = gRoadInfo->water ? kNormalPlayerBoatID : gPlayerCarID;
		fprintf(stderr, "[LoadLevel] Creating player object (type=%d)...\n", carType);
		gPlayerObj=NewObject(gFirstObj, carType);
	}
	gPlayerObj->pos.x=gLevelData->xStartPos;
	gPlayerObj->pos.y=500;
	gPlayerObj->control=kObjectDriveUp;
	gPlayerObj->target=1;
	gCameraObj=gPlayerObj;
	gPlayerBonus=1;
//	gPlayerObj=nil; //	Uncomment this line to make the player car ai controlled
	gSpikeObj=nil;
	gBrakeObj=nil;
	CopClear();
	SortObjects();
	
	gGameTime=0;
	gTrackCount=0;
	gPlayerDeathDelay=0;
	gFinishDelay=0;
	gPlayerBonus=1;
	gDisplayScore=gPlayerScore;
	gXDriftPos=0;
	gYDriftPos=0;
	gXFrontDriftPos=0;
	gYFrontDriftPos=0;
	gZoomVelo=kMaxZoomVelo;
	ClearTextFX();
	StartCarChannels();
	gScreenBlitSpecial=true;
	return true;
}

void DisposeLevel()
{
	/* Silence audio BEFORE freeing anything - the audio callback runs
	 * on a separate thread and might reference global game state */
	BeQuiet();

	UnloadPack(kPackLevel1+gLevelID);
	gPlayerObj=nil;
	gCameraObj=nil;
	gSpikeObj=nil;
	gBrakeObj=nil;
	gFirstVisObj=nil;
	gLastVisObj=nil;
	while((tObject*)gFirstObj->next!=gFirstObj)
	{
		SpriteUnused((*(tObject*)gFirstObj->next).frame);
		RemoveObject((tObject*)gFirstObj->next);
	}
	FlushRemovedObjects();
	DisposePtr((Ptr)gFirstObj);
	gFirstObj=nil;
}

extern int gOSX;

void GetLevelNumber()
{
	gLevelID = 0;
	gPlayerCarID = kNormalPlayerCarID;
}

void StartGame(int lcheat)
{
	DisposeInterface();
	gPlayerLives=3;
	gExtraLives=0;
	gPlayerAddOns=0;
	gPlayerDeathDelay=0;
	gFinishDelay=0;
	gPlayerScore=0;
	gLevelID=0; // Starting Level
	gPlayerCarID=kNormalPlayerCarID;
	gNumMissiles=0;
	gNumMines=0;
	gGameOn=true;
	gEndGame=false;
	if(lcheat)
		GetLevelNumber();
	gLCheat=lcheat;
	FadeScreen(1);
	ScreenMode(kScreenRunning);
	InputMode(kInputRunning);
	MacHideCursor();
	if(LoadLevel()){
		ScreenClear();
		FadeScreen(512);
		RenderFrame();
		InitFrameCount();
	}
}

void EndGame()
{	
	gPlayerLives=0;//so RenderFrame will not draw Panel.
	RenderFrame();
	DisposeLevel();
	BeQuiet();
	SimplePlaySound(152);
	GameOverAnim();		
	InitInterface();
	MacShowCursor();
	if(!gLCheat)
		CheckHighScore(gPlayerScore);
}
