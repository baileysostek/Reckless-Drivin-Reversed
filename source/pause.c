#include <SDL.h>
#include "mac_compat.h"
#include "gameframe.h"
#include "interface.h"
#include "input.h"
#include "error.h"
#include "gamesounds.h"
#include "screenfx.h"
#include "screen.h"

void PauseGame()
{
	int paused = 1;
	SDL_Event event;

	PauseFrameCount();
	SaveFlushEvents();
	InputMode(kInputSuspended);
	BeQuiet();
	MacShowCursor();
	ShowPicScreen(1006);

	while(paused)
	{
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_QUIT:
					paused = 0;
					break;
				case SDL_MOUSEBUTTONDOWN:
					paused = 0;
					break;
				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
						event.window.event == SDL_WINDOWEVENT_EXPOSED)
						Blit2Screen();
					break;
			}
		}
		SDL_Delay(16);
	}

	MacHideCursor();
	InputMode(kInputRunning);
	ScreenClear();
	StartCarChannels();
	ResumeFrameCount();
}
