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
int gSelectedCarID = kNormalPlayerCarID;
static tRoad sRoadOverride = NULL;  /* malloc'd road data from editor save */  /* session car choice; default = player car */
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

static void SwapObjectPos(tObjectPos *op);  /* defined below, used here */

/* -----------------------------------------------------------------------
 * EditorLoadRoadData — load level data for the editor without starting
 * a game. Uses existing static Swap* helpers. Returns a malloc'd copy
 * of the road segments; caller must free(). Also sets gLevelData,
 * gRoadInfo, gTrackUp, gTrackDown for the caller to copy.
 * ----------------------------------------------------------------------- */
tRoad EditorLoadRoadData(int levelID, UInt32 *outLen,
                         tObjectPos **outObjs, UInt32 *outNumObjs)
{
	Ptr dataPos;
	UInt32 numObjs;
	tObjectPos *objs;
	tRoad result;
	UInt32 s;

	if (outObjs)    *outObjs    = NULL;
	if (outNumObjs) *outNumObjs = 0;

	LoadPack(kPackLevel1 + levelID);
	gLevelData = (tLevelData*)GetSortedPackEntry(kPackLevel1 + levelID, 1, nil);
	if (!gLevelData) { *outLen = 0; return NULL; }
	SwapLevelData(gLevelData);

	gRoadInfo = (tRoadInfo*)GetSortedPackEntry(kPackRoad, gLevelData->roadInfo, nil);

	gTrackUp = (tTrackInfo*)((Ptr)gLevelData + sizeof(tLevelData));
	SwapTrackInfo(gTrackUp);
	gTrackDown = (tTrackInfo*)((Ptr)gTrackUp + sizeof(UInt32) + gTrackUp->num * sizeof(tTrackInfoSeg));
	SwapTrackInfo(gTrackDown);

	/* Extract and byte-swap object positions, returning a malloc'd copy */
	dataPos = (Ptr)gTrackDown + sizeof(UInt32) + gTrackDown->num * sizeof(tTrackInfoSeg);
	numObjs = SwapU32(*(UInt32*)dataPos);
	*(UInt32*)dataPos = numObjs;
	objs = (tObjectPos*)(dataPos + sizeof(UInt32));

	if (outObjs && outNumObjs && numObjs > 0) {
		UInt32 oi;
		tObjectPos *objCopy = (tObjectPos*)malloc(numObjs * sizeof(tObjectPos));
		if (objCopy) {
			memcpy(objCopy, objs, numObjs * sizeof(tObjectPos));
			for (oi = 0; oi < numObjs; oi++)
				SwapObjectPos(&objCopy[oi]);
			*outObjs = objCopy;
		}
		*outNumObjs = numObjs;
	}

	gRoadLenght = (UInt32*)(objs + numObjs);
	*gRoadLenght = SwapU32(*gRoadLenght);
	gRoadData = (tRoad)((Ptr)gRoadLenght + sizeof(UInt32));

	/* Byte-swap road segments in-place */
	for (s = 0; s < *gRoadLenght; s++) {
		gRoadData[s][0] = SwapS16(gRoadData[s][0]);
		gRoadData[s][1] = SwapS16(gRoadData[s][1]);
		gRoadData[s][2] = SwapS16(gRoadData[s][2]);
		gRoadData[s][3] = SwapS16(gRoadData[s][3]);
	}

	/* Return a malloc'd copy for the editor to own */
	*outLen = *gRoadLenght;
	result = (tRoad)malloc(*outLen * sizeof(tRoadSeg));
	if (result)
		memcpy(result, gRoadData, *outLen * sizeof(tRoadSeg));
	return result;
}

void EditorUnloadLevelPack(int levelID)
{
    UnloadPack(kPackLevel1 + levelID);
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

	/* ---- Read editor save file (before LoadObjs so we can override objects) ---- */
	{
		char opath[256];
		FILE *of;
		tObjectPos *editorObjs = NULL;
		UInt32 editorNumObjs = 0;
		int editorHasObjects = 0;

		if (sRoadOverride) { free(sRoadOverride); sRoadOverride = NULL; }
		snprintf(opath, sizeof(opath), "saves/level_%d_editor.bin", gLevelID);
		of = fopen(opath, "rb");
		if (of) {
			UInt32 magic = 0, version = 0;
			fread(&magic, 4, 1, of);
			fread(&version, 4, 1, of);
			if (magic == 0x52444C56u && version >= 1) {
				tLevelData savedMeta;
				UInt32 nSegs = 0;
				int gi;
				/* 1. Metadata */
				if (fread(&savedMeta, sizeof(tLevelData), 1, of) == 1) {
					gLevelData->time      = savedMeta.time;
					gLevelData->xStartPos = savedMeta.xStartPos;
					for (gi = 0; gi < 10; gi++)
						gLevelData->objGrps[gi] = savedMeta.objGrps[gi];
				}
				/* 2. Road segments (stored for later apply) */
				if (fread(&nSegs, 4, 1, of) == 1 && nSegs > 0 && nSegs <= 200000) {
					sRoadOverride = (tRoad)malloc(nSegs * sizeof(tRoadSeg));
					if (sRoadOverride) {
						if (fread(sRoadOverride, sizeof(tRoadSeg), nSegs, of) != nSegs) {
							free(sRoadOverride); sRoadOverride = NULL;
						}
					}
				}
				/* 3. Skip track up */
				{ UInt32 n = 0; if (fread(&n, 4, 1, of) == 1) fseek(of, (long)(n * sizeof(tTrackInfoSeg)), SEEK_CUR); }
				/* 4. Skip track down */
				{ UInt32 n = 0; if (fread(&n, 4, 1, of) == 1) fseek(of, (long)(n * sizeof(tTrackInfoSeg)), SEEK_CUR); }
				/* 5. Object overrides (version 2+) */
				if (version >= 2) {
					UInt32 nObjs = 0;
					if (fread(&nObjs, 4, 1, of) == 1 && nObjs > 0 && nObjs < 10000) {
						editorObjs = (tObjectPos*)malloc(nObjs * sizeof(tObjectPos));
						if (editorObjs) {
							if (fread(editorObjs, sizeof(tObjectPos), nObjs, of) == nObjs) {
								editorNumObjs = nObjs;
								editorHasObjects = 1;
							} else {
								free(editorObjs); editorObjs = NULL;
							}
						}
					}
				}
				fprintf(stderr, "[LoadLevel] Editor save v%u: road=%s objects=%d\n",
				        version, sRoadOverride ? "yes" : "no",
				        editorHasObjects ? (int)editorNumObjs : 0);
			}
			fclose(of);
		}

		/* Load objects: from editor save (v2) or from pack */
		{
			Ptr objsDataPos = (Ptr)gTrackDown + sizeof(UInt32) + gTrackDown->num * sizeof(tTrackInfoSeg);
			if (editorHasObjects) {
				/* Advance pointer past pack objects without creating them */
				UInt32 nPackObjs = SwapU32(*(UInt32*)objsDataPos);
				*(UInt32*)objsDataPos = nPackObjs;
				gRoadLenght = (UInt32*)((tObjectPos*)(objsDataPos + sizeof(UInt32)) + nPackObjs);
				/* Create editor objects */
				{
					UInt32 oi;
					for (oi = 0; oi < editorNumObjs; oi++) {
						tObject *theObj = NewObject(gFirstObj, editorObjs[oi].typeRes);
						if (theObj) {
							theObj->dir   = editorObjs[oi].dir;
							theObj->pos.x = editorObjs[oi].x;
							theObj->pos.y = editorObjs[oi].y;
						}
					}
				}
				free(editorObjs); editorObjs = NULL;
			} else {
				gRoadLenght = (UInt32*)LoadObjs(objsDataPos);
			}
		}
	}

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

	/* Apply road override from editor save (replaces pack road data) */
	if (sRoadOverride) {
		gRoadData = sRoadOverride;
		fprintf(stderr, "[LoadLevel] Applied editor road override\n");
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
	gLevelID=1; // Starting Level
	gPlayerCarID=gSelectedCarID;
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
