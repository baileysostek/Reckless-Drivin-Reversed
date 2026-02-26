#ifndef __PREFERENCES
#define __PREFERENCES

#include "input.h"

#define kNumHighScoreEntrys	10
#define kPrefsVersion 8

typedef struct{
	char name[16];
	UInt32 time;
	UInt32 score;
}tScoreRecord;

typedef struct{
	UInt16 version;
	UInt16 volume;
	UInt8  sound,engineSound,hqSound,unused1;
	UInt8  lineSkip,motionBlur,hiColor;
	UInt8 hidElements[kNumElements];
	UInt8  fullscreen;
	UInt8  widescreen;
	UInt8  unused[9];
	tScoreRecord	high[kNumHighScoreEntrys];
	float lapRecords[10];
	char name[256],code[256];
	UInt8 keyCodes[kNumElements];
	char lastName[256];
}tPrefs;

extern tPrefs gPrefs;
void Preferences();
void LoadPrefs();
void WritePrefs(int reset);
void BuildCarChoiceList(void);

#endif