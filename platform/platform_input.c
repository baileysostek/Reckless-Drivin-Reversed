#include <SDL.h>
#include <math.h>
#include "mac_compat.h"
#include "trig.h"
#include "platform_input.h"
#include "platform_screen.h"
#include "preferences.h"
#include "vec2d.h"
#include "objects.h"
#include "gameframe.h"

#define kMinSwitchDelay 15

static tInputData gInputData;
int gFire, gMissile;
static int gLastScan[kNumElements];
static unsigned long gSwitchDelayStart;
static SDL_GameController *gGameController = NULL;
static int gForceFeedback = 0;
static int gFFBBlock = 0;
static int gInputMode = kInputSuspended;

/* Default key mappings (SDL scancodes) */
static SDL_Scancode gKeyMap[kNumElements] = {
    SDL_SCANCODE_UP,      /* kForward */
    SDL_SCANCODE_DOWN,    /* kBackward */
    SDL_SCANCODE_LEFT,    /* kLeft */
    SDL_SCANCODE_RIGHT,   /* kRight */
    SDL_SCANCODE_LSHIFT,  /* kKickdown */
    SDL_SCANCODE_SPACE,   /* kBrake */
    SDL_SCANCODE_Z,       /* kFire */
    SDL_SCANCODE_X,       /* kMissile */
    SDL_SCANCODE_ESCAPE,  /* kAbort */
    SDL_SCANCODE_P        /* kPause */
};

extern int gEndGame;
extern void PauseGame(void);

void InitInput(void)
{
    int i;
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);

    /* Open first available game controller */
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            gGameController = SDL_GameControllerOpen(i);
            if (gGameController) {
                /* Check if controller supports rumble */
                if (SDL_GameControllerRumble(gGameController, 0, 0, 0) == 0) {
                    gForceFeedback = 1;
                }
                break;
            }
        }
    }

    /* Clear state */
    memset(gLastScan, 0, sizeof(gLastScan));
    memset(&gInputData, 0, sizeof(gInputData));
}

void InputMode(int mode)
{
    int i;
    gInputMode = mode;

    if (mode == kInputStopped) {
        if (gGameController) {
            SDL_GameControllerClose(gGameController);
            gGameController = NULL;
            gForceFeedback = 0;
        }
    }

    if (mode == kInputSuspended) {
        for (i = 0; i < kNumElements; i++)
            gLastScan[i] = 0;
    }
}

static int GetElement(int element)
{
    const Uint8 *keystate = SDL_GetKeyboardState(NULL);

    /* Check keyboard */
    if (keystate[gKeyMap[element]])
        return 1;

    /* Check game controller */
    if (gGameController) {
        switch (element) {
            case kForward:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_DPAD_UP))
                    return 1;
                if (SDL_GameControllerGetAxis(gGameController, SDL_CONTROLLER_AXIS_LEFTY) < -16384)
                    return 1;
                break;
            case kBackward:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
                    return 1;
                if (SDL_GameControllerGetAxis(gGameController, SDL_CONTROLLER_AXIS_LEFTY) > 16384)
                    return 1;
                break;
            case kLeft:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
                    return 1;
                if (SDL_GameControllerGetAxis(gGameController, SDL_CONTROLLER_AXIS_LEFTX) < -16384)
                    return 1;
                break;
            case kRight:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
                    return 1;
                if (SDL_GameControllerGetAxis(gGameController, SDL_CONTROLLER_AXIS_LEFTX) > 16384)
                    return 1;
                break;
            case kKickdown:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_A))
                    return 1;
                break;
            case kBrake:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_B))
                    return 1;
                break;
            case kFire:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_X))
                    return 1;
                break;
            case kMissile:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_Y))
                    return 1;
                break;
            case kAbort:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_BACK))
                    return 1;
                break;
            case kPause:
                if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_START))
                    return 1;
                break;
        }
    }

    return 0;
}

static int GetEvent(int *element, int *data)
{
    int i;
    for (i = 0; i < kNumElements; i++) {
        int scan = GetElement(i);
        if (scan != gLastScan[i]) {
            *element = i;
            *data = scan;
            gLastScan[i] = scan;
            return 1;
        }
    }
    return 0;
}

static short IsPressed(unsigned short scanCode)
{
    const Uint8 *keystate = SDL_GetKeyboardState(NULL);
    return keystate[scanCode];
}

static float ThrottleReset(float throttle)
{
    if (throttle > 0) {
        throttle -= 2 * kFrameDuration;
        if (throttle < 0)
            throttle = 0;
    } else if (throttle < 0) {
        throttle += 2 * kFrameDuration;
        if (throttle > 0)
            throttle = 0;
    }
    return throttle;
}

void Input(tInputData **data)
{
    int playerVelo = VEC2D_DotProduct(gPlayerObj->velo, P2D(sin(gPlayerObj->dir), cos(gPlayerObj->dir)));
    int axState = 0;
    int switchRequest = false;
    int element, eventData;

    /* Pump SDL events so keyboard state updates and window stays responsive */
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                gEndGame = true;
            } else if (ev.type == SDL_WINDOWEVENT &&
                       ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                if (gPrefs.widescreen)
                    ResizeFramebuffer(ComputeWidescreenWidth(ev.window.data1, ev.window.data2));
            }
        }
    }

    gMissile = false;
    gFire = false;

    if (playerVelo)
        gSwitchDelayStart = gFrameCount;

    gInputData.handbrake = (GetElement(kBrake) ? gInputData.handbrake + kFrameDuration * 8 : 0);
    if (gInputData.handbrake > 1) gInputData.handbrake = 1;

    gInputData.kickdown = false;

    if (GetElement(kForward)) axState = 1;
    if (GetElement(kBackward)) axState = -1;

    switch (axState) {
        case 1:
            if (!gInputData.reverse) {
                gInputData.throttle += 3 * kFrameDuration;
                if (gInputData.throttle > 1) gInputData.throttle = 1;
                gInputData.brake = 0;
                if (playerVelo < 0) {
                    switchRequest = true;
                    gSwitchDelayStart = gFrameCount - kMinSwitchDelay;
                }
            } else {
                gInputData.brake += kFrameDuration * 6;
                if (gInputData.brake > 1) gInputData.brake = 1;
                gInputData.throttle = ThrottleReset(gInputData.throttle);
                if (!playerVelo)
                    switchRequest = true;
                if (playerVelo > 0) {
                    switchRequest = true;
                    gSwitchDelayStart = gFrameCount - kMinSwitchDelay;
                }
            }
            break;
        case -1:
            if (gInputData.reverse) {
                gInputData.throttle -= 3 * kFrameDuration;
                if (gInputData.throttle < -1) gInputData.throttle = -1;
                gInputData.brake = 0;
                if (playerVelo > 0) {
                    switchRequest = true;
                    gSwitchDelayStart = gFrameCount - kMinSwitchDelay;
                }
            } else {
                gInputData.brake += kFrameDuration * 6;
                if (gInputData.brake > 1) gInputData.brake = 1;
                gInputData.throttle = ThrottleReset(gInputData.throttle);
                if (!playerVelo)
                    switchRequest = true;
                if (playerVelo < 0) {
                    switchRequest = true;
                    gSwitchDelayStart = gFrameCount - kMinSwitchDelay;
                }
            }
            break;
        case 0:
            gInputData.brake = 0;
            gInputData.throttle = ThrottleReset(gInputData.throttle);
            break;
    }

    if (GetElement(kKickdown) && axState != -1) {
        gInputData.kickdown = true;
        gInputData.throttle = 1;
        if (gInputData.reverse && !gInputData.brake) {
            switchRequest = true;
            gSwitchDelayStart = gFrameCount - kMinSwitchDelay;
        }
    }

    axState = 0;
    if (GetElement(kRight)) axState = 1;
    if (GetElement(kLeft)) axState = -1;

    switch (axState) {
        case 1:
            gInputData.steering += (gInputData.steering < 0) ? 8 : 3 * kFrameDuration;
            if (gInputData.steering > 1) gInputData.steering = 1;
            break;
        case -1:
            gInputData.steering -= (gInputData.steering > 0) ? 8 : 3 * kFrameDuration;
            if (gInputData.steering < -1) gInputData.steering = -1;
            break;
        case 0:
            if (gInputData.steering > 0) {
                gInputData.steering -= 8 * kFrameDuration;
                if (gInputData.steering < 0)
                    gInputData.steering = 0;
            } else {
                gInputData.steering += 8 * kFrameDuration;
                if (gInputData.steering > 0)
                    gInputData.steering = 0;
            }
            break;
    }

    while (GetEvent(&element, &eventData))
        switch (element) {
            case kForward:
                if (!playerVelo && gInputData.reverse && eventData) {
                    switchRequest = true;
                    gSwitchDelayStart = gFrameCount - kMinSwitchDelay;
                }
                break;
            case kBackward:
                if (!playerVelo && !gInputData.reverse && eventData) {
                    switchRequest = true;
                    gSwitchDelayStart = gFrameCount - kMinSwitchDelay;
                }
                break;
            case kMissile:
                gMissile = eventData;
                break;
            case kFire:
                gFire = eventData;
                break;
            case kAbort:
                if (eventData)
                    gEndGame = true;
                break;
            case kPause:
                if (eventData)
                    PauseGame();
                break;
        };

    if (switchRequest && gFrameCount >= gSwitchDelayStart + kMinSwitchDelay)
        gInputData.reverse = !gInputData.reverse;

    *data = &gInputData;
}

UInt64 GetMSTime(void)
{
    Uint64 counter = SDL_GetPerformanceCounter();
    Uint64 freq    = SDL_GetPerformanceFrequency();
    /* Divide first, then handle remainder, to avoid overflow of counter*1000000 */
    return (counter / freq) * 1000000ULL + (counter % freq) * 1000000ULL / freq;
}

void FlushInput(void)
{
    SDL_Event event;
    int i;

    /* Pump and discard all pending SDL events */
    while (SDL_PollEvent(&event))
        ;

    /* Snapshot current key state so no stale key-down produces a false
     * transition on the first frame of the next game. */
    for (i = 0; i < kNumElements; i++)
        gLastScan[i] = GetElement(i);
}

void FFBJolt(float lMag, float rMag, float duration)
{
    if (gForceFeedback && gGameController) {
        SDL_GameControllerRumble(gGameController,
            (Uint16)(lMag * 65535),
            (Uint16)(rMag * 65535),
            (Uint32)(duration * 1000));
        gFFBBlock = gFrameCount + duration * kCalcFPS;
    }
}

void FFBDirect(float lMag, float rMag)
{
    if (gForceFeedback && gFrameCount > gFFBBlock) {
        SDL_GameControllerRumble(gGameController,
            (Uint16)(lMag * 65535),
            (Uint16)(rMag * 65535),
            100);
    }
}

int ContinuePress(void)
{
    if (gGameController) {
        if (SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_A)
            || SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_B)
            || SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_X)
            || SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_Y)
            || SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_START)
            || SDL_GameControllerGetButton(gGameController, SDL_CONTROLLER_BUTTON_DPAD_UP))
            return 1;
    }
    return 0;
}

void ConfigureInput(void)
{
    /* No-op stub - input configuration not yet implemented */
}
