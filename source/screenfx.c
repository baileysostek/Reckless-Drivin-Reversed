#include <string.h>
#include <stdlib.h>
#include "mac_compat.h"
#include "screen.h"
#include "trig.h"
#include "input.h"
#include "sprites.h"
#include "gameframe.h"
#include "preferences.h"
#include "random.h"

#define kGameOverSprite 265
#define kFadeStart		1.4
#define kAnimDuration	2.1
#define kShiftInDuration  0.8

enum{
	kSpinIn,
	kFlyIn,
	kNumGameOverTypes
};

void ScreenClear()
{
	/* Clear the framebuffer to black */
	memset(gBaseAddr, 0, gRowBytes * gYSize);
}

void ShiftInPicture()
{
	/* Simplified shift-in: for now just copy the back buffer to front.
	 * The original used scanline-offset effects between front/back GWorld buffers.
	 * In our SDL port, we render to gBaseAddr and Blit2Screen uploads it,
	 * so we just do a quick fade-in effect. */
	UInt64 animStart;
	float t;
	PauseFrameCount();
	animStart = GetMSTime();
	do {
		UInt64 msTime = GetMSTime();
		msTime -= animStart;
		t = msTime / 1000000.0f;
		/* Progressive reveal: just blit what we have */
		Blit2Screen();
	} while(t < kShiftInDuration);
	ResumeFrameCount();
}

void GameOverAnim()
{
	int type = RanInt(0, kNumGameOverTypes);
	int side = RanProb(0.5);
	UInt64 animStart;
	float t;

	animStart = GetMSTime();
	do {
		UInt64 msTime = GetMSTime();
		float size, xPos, yPos, dir;
		msTime -= animStart;
		t = msTime / 1000000.0f;
		switch(type)
		{
			case kSpinIn:
				size = t * t;
				xPos = gXSize / 2;
				yPos = gYSize / 2;
				dir = (side ? 1 : -1) * 2 * PI * t;
				break;
			case kFlyIn:
				size = t * t;
				xPos = gXSize / 2;
				if(side)
					yPos = (gYSize / 2) * (4.0f / 3.0f) * (1.0f / (-t - 1.0f) + 1.0f);
				else
					yPos = gYSize - (gYSize / 2) * (4.0f / 3.0f) * (1.0f / (-t - 1.0f) + 1.0f);
				dir = 0;
				break;
			default:
				size = 1; xPos = gXSize/2; yPos = gYSize/2; dir = 0;
				break;
		}
		DrawSprite(kGameOverSprite, xPos, yPos, dir, size);
		Blit2Screen();
		if(t > kFadeStart)
			FadeScreen((int)(256 - (t - kFadeStart) / (kAnimDuration - kFadeStart) * 256 + 256));
	} while(t < kAnimDuration);
	ScreenClear();
	FadeScreen(512);
}
