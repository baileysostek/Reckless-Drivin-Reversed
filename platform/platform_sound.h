#ifndef __PLATFORM_SOUND_H__
#define __PLATFORM_SOUND_H__

#include "mac_compat.h"
#include "vec2d.h"

void LoadSounds(void);
void InitChannels(void);
void PlaySound(t2DPoint pos, t2DPoint velo, float freq, float vol, int id);
void SimplePlaySound(int id);
void SetCarSound(float engine, float skidL, float skidR, float velo);
void StartCarChannels(void);
void SetGameVolume(int volume);
void SetSystemVolume(void);
void BeQuiet(void);

#endif /* __PLATFORM_SOUND_H__ */
