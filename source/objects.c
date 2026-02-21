#include <math.h>
#include <string.h>
#include "input.h"
#include "objects.h"
#include "screen.h"
#include "vec2d.h"
#include "roads.h"
#include "trig.h"
#include "gamesounds.h"
#include "textfx.h"
#include "sprites.h"
#include "packs.h"
#include "objectcontrol.h"
#include "random.h"

#define kWreckDelay			0.3
#define kPlayerDeathDelay	2.5
#define kMaxRotVelo			(2*PI*5)
#define kExpMass			250
#define kGravity			50.0

/* Deferred-free list: RemoveObject unlinks objects from the active list but
 * defers DisposePtr until the end of MoveObjects. This prevents use-after-free
 * when HandleCollision (called from ObjectPhysics) kills an object that was
 * saved as the 'next' pointer in MoveObjects' linked list iteration.
 * Classic Mac OS DisposePtr didn't truly invalidate memory, so the original
 * code worked by accident. Modern free() makes the memory inaccessible. */
#define MAX_PENDING_FREE 256
static Ptr gPendingFree[MAX_PENDING_FREE];
static int gPendingFreeCount = 0;

void ObjectPhysics(tObject *);

#ifndef _MSC_VER
int abs(int x)
{
	return x>=0?x:-x;
}
#endif

void Explosion(t2DPoint pos,t2DPoint velo,int offs,float mass,int sound)
{
	if(fabs(pos.y-gCameraObj->pos.y)<kVisDist)
	{
		float l=3*kScale*sqrt(mass*0.001);
		int i;
		int num=mass/kExpMass;
		if(num < 0) num = 0;
		if(num > 50) num = 50; /* safety cap */
		if(sound){
			float boom=num*0.07+0.5;
			PlaySound(pos,P2D(0,0),1,boom>1?1:boom,offs?140:129);
		}
		for(i=0;i<num;i++)
		{
			tObject *expObj;	
			float ran=RanFl(0,1);
			if(offs)		
				expObj=NewObject(gFirstObj,1001+offs);
			else
				if(ran>0.3)
					expObj=NewObject(gFirstObj,1001);
				else if(ran>0.08)
					expObj=NewObject(gFirstObj,195);
				else{
					expObj=NewObject(gFirstObj,1020);
					expObj->jumpVelo=15;
				}				
			expObj->frameDuration=RanFl(0,0.25);
			expObj->frame=0;
			expObj->pos.x=pos.x+RanFl(0,l);
			expObj->pos.y=pos.y+RanFl(0,l);
			expObj->dir=RanFl(0,2*PI);
			expObj->velo=VEC2D_Sum(VEC2D_Scale(velo,0.5),VEC2D_Scale(P2D(sin(expObj->dir),cos(expObj->dir)),ran>0.08?7:50));
		}
	}
}

tObject *GetCloseObj(t2DPoint pos,tObject *posObj,float *dist)
{
	tObject	*theObj=(tObject*)(gFirstObj->next);
	tObject *closeObj=nil;
	*dist=INFINITY;
	while(theObj!=gFirstObj)
	{
		if(theObj!=posObj)
		{
			float xdist=theObj->pos.x-pos.x;
			float ydist=theObj->pos.y-pos.y;
			float sqdist=xdist*xdist+ydist*ydist;
			if(sqdist<*dist)
			{
				*dist=sqdist;
				closeObj=theObj;
			}
		}
		theObj=(tObject*)theObj->next;
	}
	return closeObj;
}

t2DPoint GetUniquePos(SInt16 minOffs,SInt16 maxOffs,float *objDir,int *dir)
{
	t2DPoint pos;
	int ok;
	int target;
	do{
		tRoad roadData;
		float sqdist;
		pos.y=RanFl(500,gLevelData->levelEnd);
		{int ri=(int)(pos.y/2); if(ri<0)ri=0; if((UInt32)ri>=*gRoadLenght)ri=*gRoadLenght-1; roadData=gRoadData+ri;}
		if(*objDir==-1)
		{
			if(RanProb(0.5)||!gTrackDown->num)
			{
				for(target=1;
					(target<gTrackUp->num)&&(gTrackUp->track[target].y<pos.y);
					target++);
				pos.x=gTrackUp->track[target-1].x+(gTrackUp->track[target].x-gTrackUp->track[target-1].x)/
					(gTrackUp->track[target].y-gTrackUp->track[target-1].y)*(pos.y-gTrackUp->track[target-1].y);
				*dir=kObjectDriveUp;
			}
			else
			{
				for(target=1;
					(target<gTrackDown->num)&&(gTrackDown->track[target].y>pos.y);
					target++);
				pos.x=gTrackDown->track[target-1].x+(gTrackDown->track[target].x-gTrackDown->track[target-1].x)/
					(gTrackDown->track[target].y-gTrackDown->track[target-1].y)*(pos.y-gTrackDown->track[target-1].y);			
				*dir=kObjectDriveDown;
			}
			GetCloseObj(pos,nil,&sqdist);	
			ok=(sqdist>kScale*kScale*kMinCarDist*kMinCarDist);
		}
		else
		{
			int border=RanInt(0,4);
			if((*roadData)[1]==(*roadData)[2])
				switch(border)
				{
					case 0:case 2:
						pos.x=(*roadData)[0]+RanFl(minOffs,maxOffs);
						ok=(*roadData)[3]>=pos.x+minOffs;
						break;
					case 1:case 3:
						pos.x=(*roadData)[3]-RanFl(minOffs,maxOffs);
						ok=(*roadData)[0]<=pos.x-minOffs;
						break;
				}
			else
				switch(border)
				{
					case 0:case 2:
						pos.x=(*roadData)[border]+RanFl(minOffs,maxOffs);
						if(minOffs>=0)
							ok=(*roadData)[border+1]>=pos.x+minOffs;
						else
						 	ok=(border==2)?(*roadData)[1]<=pos.x+minOffs:true;
						break;
					case 1:case 3:
						pos.x=(*roadData)[border]-RanFl(minOffs,maxOffs);
						if(minOffs>=0)
							ok=(*roadData)[border-1]<=pos.x-minOffs;
						else
						 	ok=(border==1)?(*roadData)[2]>=pos.x-minOffs:true;
						break;
				}
			*dir=kObjectNoInput;	
		}
	}while(!ok);
	if(*objDir==-1)
	{
		t2DPoint targetPos=(*dir==kObjectDriveUp?P2D(gTrackUp->track[target].x,gTrackUp->track[target].y)
			:P2D(gTrackDown->track[target].x,gTrackDown->track[target].y));
		if(pos.y-targetPos.y)
			if(*dir==kObjectDriveUp)
				*objDir=atan((pos.x-targetPos.x)/(pos.y-targetPos.y));
			else
				*objDir=PI+atan((pos.x-targetPos.x)/(pos.y-targetPos.y));
		else
			*objDir=(*dir==kObjectDriveUp)?0:PI;
	}
	else
		*objDir=RanFl(-*objDir,*objDir);
	return pos;
}

void InsertObjectGroup(tObjectGroupReference groupRef)
{
	int probilities[100];
	int entryCnt,indexCount=0,probCount;
	tObjectGroup *group=(tObjectGroup*)GetSortedPackEntry(kPackOgrp,groupRef.resID,nil);
	if(!group) return;
	for(entryCnt=0;entryCnt<(*group).numEntries;entryCnt++)
		for(probCount=0;probCount<(*group).data[entryCnt].probility&&indexCount<100;probCount++)
			probilities[indexCount++]=entryCnt;
	if(indexCount==0) return;
	for(entryCnt=0;entryCnt<groupRef.numObjs;entryCnt++)
	{
		int probIndex=probilities[RanInt(0,indexCount)];
		int control;
		tObject *theObj=NewObject(gFirstObj,(*group).data[probIndex].typeRes);
		theObj->pos=P2D(INFINITY,INFINITY);
		theObj->dir=(*group).data[probIndex].dir;
		theObj->pos=GetUniquePos((*group).data[probIndex].minOffs,(*group).data[probIndex].maxOffs,&theObj->dir,&control);
		theObj->control+=control;
		if((theObj->control&0x0000000f)==kObjectDriveUp)
			for(theObj->target=0;
				(theObj->target<gTrackUp->num)&&(gTrackUp->track[theObj->target].y<theObj->pos.y);
				theObj->target++);
		else
			for(theObj->target=0;
				(theObj->target<gTrackDown->num)&&(gTrackDown->track[theObj->target].y>theObj->pos.y);
				theObj->target++);	
	}
}

/* Static inert object type used when a requested type ID doesn't exist.
 * Has no physics flags, no collision, no animation - just sits harmlessly
 * until removed. deathObj=-1 means RemoveObject on kill. */
static tObjectType gDummyObjectType = {0};
static int gDummyTypeInited = 0;

static tObjectTypePtr GetDummyType(void)
{
	if(!gDummyTypeInited) {
		memset(&gDummyObjectType, 0, sizeof(gDummyObjectType));
		gDummyObjectType.deathObj = -1; /* just remove on death */
		gDummyTypeInited = 1;
	}
	return &gDummyObjectType;
}

tObject *NewObject(tObject *prev,SInt16 typeRes)
{
	tObject *theObj=(tObject*)NewPtrClear(sizeof(tObject));
	theObj->next=prev->next;
	theObj->prev=prev;
	((tObject*)(prev->next))->prev=theObj;
	prev->next=theObj;
	theObj->type=(tObjectTypePtr)GetUnsortedPackEntry(kPackObTy,typeRes,0);
	if(!theObj->type) {
		fprintf(stderr, "[NewObject] WARNING: type %d not found, using inert dummy\n", typeRes);
		theObj->type = GetDummyType();
	}
	if((*theObj->type).flags&kObjectRandomFrameFlag)
		theObj->frame=(*theObj->type).frame+RanInt(0,(*theObj->type).numFrames);
	else
		theObj->frame=(*theObj->type).frame;
	if((*theObj->type).flags2&kObjectRoadKill)
		theObj->control=kObjectCrossRoad;
	else
		theObj->control=0;
	if((*theObj->type).flags&kObjectHeliFlag)
		theObj->jumpHeight=12;
	theObj->layer=(*theObj->type).flags2>>5&3;
	if((*theObj->type).creationSound&&gPlayerObj)
		PlaySound(gPlayerObj->pos,gPlayerObj->velo,1,1,(*theObj->type).creationSound);
	return theObj;
}

void RemoveObject(tObject *theObj)
{
	if(theObj==gPlayerObj)
	{
		theObj->frame=0;
		theObj->type=(tObjectTypePtr)GetUnsortedPackEntry(kPackObTy,2000,0);
		if(!theObj->type) theObj->type = GetDummyType(); /* Fallback if type 2000 missing */
	}
	else
	{
		if(theObj==gFirstVisObj)
			gFirstVisObj=(tObject*)gFirstVisObj->next;
		if(theObj==gLastVisObj)
			gLastVisObj=(tObject*)gLastVisObj->next;
		((tObject*)theObj->prev)->next=theObj->next;
		((tObject*)theObj->next)->prev=theObj->prev;
		/* Defer free: the object's own next/prev pointers still point to their
		 * old neighbors, so any saved 'next' pointer in MoveObjects can still
		 * follow the chain to the correct successor. Mark type=NULL so the
		 * iteration loop skips this dead object. */
		theObj->type = NULL;
		if(gPendingFreeCount < MAX_PENDING_FREE)
			gPendingFree[gPendingFreeCount++] = (Ptr)theObj;
		else
			DisposePtr((Ptr)theObj);
	}
}

int CalcBackCollision(t2DPoint pos)
{
	int roadIdx=(int)(pos.y/2);
	tRoad roadData;
	if(roadIdx<0 || (UInt32)roadIdx>=*gRoadLenght) return 0;
	roadData=gRoadData+roadIdx;
	if((*roadData)[0]>pos.x)
		return ((*roadData)[0]-pos.x)>(*gRoadInfo).tolerance?2:1;
	if((*roadData)[3]<pos.x)
		return (pos.x-(*roadData)[3])>(*gRoadInfo).tolerance?2:1;
	if(((*roadData)[1]<pos.x)&&((*roadData)[2]>pos.x))
		return (pos.x-(*roadData)[1]<(*roadData)[2]-pos.x?pos.x-(*roadData)[1]:(*roadData)[2]-pos.x)>16?2:1;
	return 0;
}

void KillObject(tObject *theObj)
{
	tObjectTypePtr objType=theObj->type;
	int sinkEnable;
	if(!objType) return; /* Already dead/removed — don't dereference NULL type */
	sinkEnable=CalcBackCollision(theObj->pos)==2&&(*objType).flags2&kObjectSink;
	if(theObj==gPlayerObj)
	{
		if(!gFinishDelay&&!(gPlayerDeathDelay!=0)&&gPlayerLives)
		{
			tTextEffect fx;
			fx.x=320; fx.y=240; fx.effectFlags=kEffectExplode; fx.fxStartFrame=0;
			strcpy(fx.text, "OUCHeee");
			NewTextEffect(&fx);
			gPlayerLives--;
			FFBJolt(1.0,1.0,1.0);
			if(!(gPlayerAddOns&kAddOnLock))
			{
				gPlayerAddOns=0;
				gNumMines=0;
				gNumMissiles=0;
			}
			else
				gPlayerAddOns^=kAddOnLock;
			gPlayerDeathDelay=kPlayerDeathDelay;
		}
		theObj->slide=0;
		gPlayerSlide[0]=0;
		gPlayerSlide[1]=0;
		gPlayerSlide[2]=0;
		gPlayerSlide[3]=0;
		theObj->throttle=0;
	}
	else if((*objType).score)
	{
		tTextEffect fx;
		char str[32];
		gPlayerScore+=(*objType).score;
		NumToString((*objType).score,str);
		fx.x=theObj->pos.x;
		fx.y=theObj->pos.y;
		fx.effectFlags=kEffectExplode+kEffectTiny+kEffectAbsPos;
		MakeFXStringFromNumStr(str,fx.text);
		NewTextEffect(&fx);
	}
	SpriteUnused(theObj->frame);
	if((*objType).flags&kObjectDefaultDeath)
		Explosion(theObj->pos,theObj->velo,sinkEnable?gRoadInfo->deathOffs:0,objType->mass,true);
	if((*objType).deathObj==-1)
	{
		RemoveObject(theObj);
		return;
	}
	{
		int deathId = (*objType).deathObj+(sinkEnable?gRoadInfo->deathOffs:0);
		tObjectTypePtr newType = (tObjectTypePtr)GetUnsortedPackEntry(kPackObTy,deathId,0);
		if(!newType) { RemoveObject(theObj); return; }
		theObj->type = newType;
	}
	theObj->layer=(*theObj->type).flags2>>5&3;
	objType=theObj->type;
	if((*objType).flags&kObjectRandomFrameFlag)
		theObj->frame=(*objType).frame+RanInt(0,(*theObj->type).numFrames);
	else
		theObj->frame=(*objType).frame;
	if((*objType).creationSound)
		PlaySound(theObj->pos,theObj->velo,1,1,(*objType).creationSound);
	theObj->frameDuration=(*objType).frameDuration;
	theObj->control=kObjectNoInput;
}

static inline void ChangeObjs(tObject* obj1,tObject* obj2)
{
	((tObject*)obj1->prev)->next=obj2;
	((tObject*)obj2->next)->prev=obj1;
	obj1->next=obj2->next;
	obj2->prev=obj1->prev;
	obj1->prev=obj2;
	obj2->next=obj1;
}

void GetVisObjs()
{
	tObject	*theObj=(tObject*)gFirstObj->next;
	int minVis=gCameraObj->pos.y-kVisDist;
	int maxVis=gCameraObj->pos.y+kVisDist;	
	while(theObj->pos.y<minVis&&theObj!=gFirstObj)
		theObj=(tObject*)theObj->next;
	gFirstVisObj=theObj;
	while(theObj->pos.y<maxVis&&theObj!=gFirstObj)
		theObj=(tObject*)theObj->next;
	gLastVisObj=theObj;
}

void SortObjects()
{
	tObject	*startObj=gFirstObj;
	tObject	*endObj=gFirstObj;
	int change;
	do{
		tObject *theObj=startObj;
		change=false;
		while(theObj->next!=endObj)
		{
			tObject *nextObj=(tObject*)theObj->next;
			if(nextObj->pos.y<theObj->pos.y)
				{ChangeObjs(theObj,nextObj);change=true;}
			else
				theObj=nextObj;
		}
		if(change)
		{
			endObj=(tObject*)endObj->prev;
			theObj=endObj;
			while(theObj->prev!=startObj)
			{
				tObject *prevObj=(tObject*)theObj->prev;
				if(prevObj->pos.y>theObj->pos.y)
					ChangeObjs(prevObj,theObj);
				else
					theObj=prevObj;
			}
			startObj=(tObject*)startObj->next;
		}
	}while(change);
	GetVisObjs();
}

void RepairObj(tObject *theObj)
{
	theObj->damage=0;
	theObj->damageFlags=0;
	SpriteUnused(theObj->frame);
	theObj->frame=(*theObj->type).frame;
}

void FireWeapon(tObject *shooter,int weaponID)
{
	if(!shooter->jumpHeight){
		tObject *projectile=NewObject(shooter,weaponID);
		projectile->dir=shooter->dir;
		projectile->pos=shooter->pos;
		projectile->shooter=shooter;
		if(projectile->type->flags2&kObjectMissile)
			projectile->jumpVelo=25.0+RanFl(-2.0,2.0);
		if(projectile->type->weaponInfo)
			projectile->velo=VEC2D_Sum(shooter->velo,P2D(sin(shooter->dir)*projectile->type->weaponInfo,cos(shooter->dir)*projectile->type->weaponInfo));
	}
}

static inline void MoveObject(tObject *theObj)
{
	if(!theObj->type) return;
	if(*(double*)(&theObj->velo))
	{
		theObj->pos=VEC2D_Sum(theObj->pos,VEC2D_Scale(theObj->velo,kScale*kFrameDuration));
		if(theObj->pos.y>*gRoadLenght*2)
		{
			theObj->pos=P2D(gTrackUp->track[0].x,100);
			if(theObj->control==kObjectDriveUp)
				theObj->target=0;
			RepairObj(theObj);
		}
		else if(theObj->pos.y<0)
		{
			theObj->pos=P2D(gTrackDown->track[0].x,*gRoadLenght*2-100);
			if(theObj->control==kObjectDriveDown)
				theObj->target=0;
			RepairObj(theObj);
		}
	}
	if((*gRoadInfo).water)
		if((*theObj->type).flags2&kObjectFloating)
			theObj->pos=VEC2D_Sum(theObj->pos,P2D(-(*gRoadInfo).xFrontDrift*0.5*kFrameDuration,(*gRoadInfo).yFrontDrift*0.5*kFrameDuration));
	if(theObj->rotVelo){
		theObj->dir+=theObj->rotVelo*kFrameDuration;
		if(theObj->dir>=2*PI)
			theObj->dir-=2*PI;
		else if(theObj->dir<0)
			theObj->dir+=2*PI;
		if(fabs(theObj->rotVelo)>kMaxRotVelo)
			theObj->rotVelo=theObj->rotVelo>0?kMaxRotVelo:-kMaxRotVelo;
	}
	if(theObj->jumpVelo)
	{
		theObj->jumpHeight+=theObj->jumpVelo*kFrameDuration;
		theObj->jumpVelo-=kGravity*kFrameDuration;
		if(!theObj->jumpVelo)
			theObj->jumpVelo=-0.0001;
		if(theObj->jumpHeight<=0&&theObj->jumpVelo<=0)
		{
			float vol=-theObj->jumpVelo*0.018;
			theObj->jumpHeight=0;
			theObj->jumpVelo=0;
			PlaySound(theObj->pos,theObj->velo,1.0,vol>1.0?1.0:vol,137);
			if(theObj==gCameraObj)
				FFBJolt(1,1,0.4);
		}
	}
}

static inline void AnimateObject(tObject *theObj)
{
	tObjectTypePtr objType=theObj->type;
	if(!objType) return;
	if(!(*objType).frameDuration)
		return;
	if(objType->flags&kObjectCop&&!(objType->flags&kObjectHeliFlag)&&!(objType->flags2&kObjectEngineSound))
		if(theObj->control!=kObjectCopControl)
		{
			theObj->frame=(*objType).frame;
			return;
		}
	theObj->frameDuration-=kFrameDuration;
	if(theObj->frameDuration<=0)
	{
		theObj->frameDuration+=(*objType).frameDuration;
		if((theObj->frame>=(*objType).frame)&&(theObj->frame<(*objType).frame+((*objType).numFrames&0x00ff)-1))
			theObj->frame++;
		else
			if((*objType).flags&kObjectDieWhenAnimEndsFlag&&(theObj->frame==(*objType).frame+((*objType).numFrames&0x00ff)-1)){
				KillObject(theObj);
				return;
			}
			else
			{
				theObj->frameRepeation++;
				theObj->frame=(*objType).frame;
				if(theObj->frameRepeation==((*objType).numFrames>>8)-1||!((*objType).numFrames>>8))
				{
					theObj->frameRepeation=0;
					if((*objType).otherSound)
						if(fabs(theObj->pos.y-gCameraObj->pos.y)<gYSize*2)
							if(!(objType->flags2&kObjectEngineSound))
								PlaySound(theObj->pos,theObj->velo,1,1,(*objType).otherSound);	
							else
								PlaySound(theObj->pos,theObj->velo,0.95+VEC2D_Value(theObj->velo)/240,0.6+VEC2D_Value(theObj->velo)/30,(*objType).otherSound);	
				}
			}
	}
	if((*objType).flags2&kObjectRoadKill)
		if(theObj->velo.x==0&&theObj->velo.y==0)
			theObj->frame=(*objType).frame;
}

void FlushRemovedObjects(void)
{
	int i;
	for(i = 0; i < gPendingFreeCount; i++)
		DisposePtr(gPendingFree[i]);
	gPendingFreeCount = 0;
}

void MoveObjects()
{
	tObject	*theObj=(tObject*)(gFirstObj->next);
	tInputData *input;
	int objCount = 0;

	FlushMessageBuffer();
	Input(&input);
	while(theObj!=gFirstObj)
	{
		tObject *next=(tObject*)theObj->next;
		/* Skip dead objects (removed but deferred-free, reachable via
		 * stale next pointers saved before ObjectPhysics/HandleCollision) */
		if(theObj->type == NULL){
			theObj=next;
			continue;
		}
		if((theObj==gPlayerObj)||!(gFrameCount%kLowCalcRatio)){
			if(gFrameCount%(2*kLowCalcRatio))
				ObjectControl(theObj,input);
			ObjectPhysics(theObj);
			if(next->prev==theObj){
				MoveObject(theObj);
				AnimateObject(theObj);
			}
		}else{
			MoveObject(theObj);
			AnimateObject(theObj);
		}
		theObj=next;
		objCount++;
	}
	SortObjects();
	FlushRemovedObjects();
}
