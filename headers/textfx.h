#ifndef __TEXTFX
#define __TEXTFX

#include "mac_compat.h"

enum{
	kEffectExplode=1<<0,
	kEffectSinLines=1<<2,
	kEffectMoveUp=1<<3,
	kEffectMoveDown=1<<4,
	kEffectMoveLeft=1<<5,
	kEffectMoveRight=1<<6,
	kEffectTiny=1<<7,
	kEffectAbsPos=1<<8
};

typedef struct{
	SInt32 x,y;			
	UInt32 effectFlags;
	UInt32 fxStartFrame;
	char text[32];
} tTextEffect;

void NewTextEffect(tTextEffect *);
void DrawTextFX(int,int);
void DrawTextFXZoomed(float,float,float);
void SimpleDrawText(char[256],int,int);
void MakeFXStringFromNumStr(char[32],char[32]);
void ClearTextFX();

#endif