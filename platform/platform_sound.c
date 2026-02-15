/*
 * platform_sound.c - SDL2 audio mixer replacing Mac Sound Manager
 *
 * This is an SDL2 port of the original source/sound.c which used the
 * classic Mac OS Sound Manager. It implements a custom audio callback
 * mixer that reads 8-bit unsigned PCM samples from the original Mac
 * sound pack data and mixes them into a signed 16-bit stereo output.
 */

#include <SDL.h>
#include <math.h>
#include <string.h>

#include "mac_compat.h"
#include "endian_compat.h"
#include "objects.h"
#include "roads.h"
#include "preferences.h"
#include "packs.h"
#include "random.h"

/* ---------- constants (from original sound.c) ---------- */

#define kNumChannels        3       /* number of sound channels */
#define kNumHQChannels      6       /* number of sound channels in HQ mode */
#define kMaxPanDist         400.0   /* distance for maximum stereo panning */
#define kMaxListenDist      1250.0  /* maximum audible distance */
#define kDopplerFactor      0.004   /* doppler effect strength */
#define kMaxNoiseVelo       65.0    /* velocity at which engine makes max noise */
#define kMinSqueakSlide     0.5
#define kSqueakFactor       0.5
#define kNumGears           4       /* number of gears */
#define kHighestGear        55.0    /* velocity for highest gear (m/s) */
#define kHighestGearTurbo   70.0    /* velocity for highest gear with turbo */
#define kShiftTolerance     2.5

/* ---------- sound data structures ---------- */

enum {
    kSoundPriorityHigher = 1 << 0
};

typedef struct {
    UInt32 numSamples;
    UInt32 priority;
    UInt32 flags;
    UInt32 offsets[1];
} tSound;

/*
 * Mac SoundHeader layouts:
 *
 * Standard (stdSH, encode=0x00):
 *   Ptr     samplePtr      (4 bytes) - 0 means data follows inline
 *   UInt32  length          (4 bytes) - number of samples
 *   Fixed   sampleRate      (4 bytes) - 16.16 fixed point
 *   UInt32  loopStart       (4 bytes)
 *   UInt32  loopEnd         (4 bytes)
 *   UInt8   encode          (1 byte)  = 0x00
 *   UInt8   baseFrequency   (1 byte)
 *   [sample data: 8-bit unsigned PCM at offset 22]
 *
 * Extended (extSH, encode=0xFF):
 *   Ptr     samplePtr      (4 bytes)
 *   UInt32  numChannels    (4 bytes)
 *   Fixed   sampleRate      (4 bytes) - 16.16 fixed point
 *   UInt32  loopStart       (4 bytes)
 *   UInt32  loopEnd         (4 bytes)
 *   UInt8   encode          (1 byte)  = 0xFF
 *   UInt8   baseFrequency   (1 byte)
 *   UInt32  numFrames       (4 bytes) - actual number of sample frames
 *   UInt8   AIFFSampleRate[10]
 *   Ptr     markerChunk     (4 bytes)
 *   Ptr     instrumentChunks(4 bytes)
 *   Ptr     AESRecording    (4 bytes)
 *   UInt16  sampleSize      (2 bytes) - bits per sample (8 or 16)
 *   UInt8   futureUse[14]
 *   [sample data at offset 64]
 */
#define kStdSoundHeaderSize 22
#define kExtSoundHeaderSize 64

/* ---------- channel structure ---------- */

typedef struct {
    uint8_t  *sampleData;       /* pointer to raw PCM data */
    uint32_t  sampleLength;     /* number of sample frames */
    float     position;         /* current playback position (fractional) */
    float     playbackRate;     /* actual rate stepping through samples */
    float     baseRate;         /* sampleRate / deviceRate (1.0 if they match) */
    float     volumeL, volumeR; /* left/right volume (0.0-1.0) */
    int       playing;
    int       looping;
    uint32_t  priority;
    int       callbackPending;  /* for engine/skid looping */
    uint16_t  sampleSize;       /* bits per sample: 8 or 16 */
    uint16_t  numChannels;      /* 1=mono, 2=stereo */
} SoundChannel;

/* ---------- globals ---------- */

static SoundChannel gChannels[kNumHQChannels];
static SoundChannel gEngineChannel;
static SoundChannel gSkidChannel;
static SDL_AudioDeviceID gAudioDevice;
static float gVolume;
static float gDeviceSampleRate = 22050.0f;
static int gGear;

int gChannelsInited = 0;
int gSystemVolumeActive = 1;
long gSystemRate = 0;
int gBuggySoundManager = 0; /* always 0 in SDL port */

/* ---------- helper: parse Mac SoundHeader ---------- */

/*
 * Parses a Mac SoundHeader at the given pointer.  All multi-byte fields
 * are big-endian and must be byte-swapped.  Returns a pointer to the raw
 * 8-bit unsigned PCM data that follows the header, and fills in outLength
 * with the number of samples.
 */
static uint8_t *ParseSoundHeader(const uint8_t *ptr, uint32_t *outLength,
                                 float *outSampleRate, uint16_t *outSampleSize,
                                 uint16_t *outNumChannels)
{
    uint32_t fixedRate = ReadBE32(ptr + 8);
    float sampleRate = (float)(fixedRate >> 16) + (float)(fixedRate & 0xFFFF) / 65536.0f;
    uint8_t encode = ptr[20];

    if (encode == 0xFF) {
        /* Extended sound header (extSH) */
        uint32_t numChannels = ReadBE32(ptr + 4);
        uint32_t numFrames   = ReadBE32(ptr + 22);
        uint16_t sampleSize  = (uint16_t)((ptr[48] << 8) | ptr[49]);

        if (outLength)      *outLength = numFrames;
        if (outSampleRate)  *outSampleRate = sampleRate;
        if (outSampleSize)  *outSampleSize = sampleSize;
        if (outNumChannels) *outNumChannels = (uint16_t)numChannels;

        return (uint8_t *)(ptr + kExtSoundHeaderSize);
    } else {
        /* Standard sound header (stdSH) - always 8-bit mono */
        uint32_t length = ReadBE32(ptr + 4);

        if (outLength)      *outLength = length;
        if (outSampleRate)  *outSampleRate = sampleRate;
        if (outSampleSize)  *outSampleSize = 8;
        if (outNumChannels) *outNumChannels = 1;

        return (uint8_t *)(ptr + kStdSoundHeaderSize);
    }
}

/* ---------- audio callback ---------- */

static int16_t ReadSample(SoundChannel *ch, uint32_t frame)
{
    if (ch->sampleSize == 16) {
        /* 16-bit big-endian signed samples */
        uint32_t byteOff = frame * 2 * ch->numChannels;
        int16_t s = (int16_t)((ch->sampleData[byteOff] << 8) | ch->sampleData[byteOff + 1]);
        return s;
    } else {
        /* 8-bit unsigned samples */
        uint32_t byteOff = frame * ch->numChannels;
        return (int16_t)((ch->sampleData[byteOff] - 128) * 256);
    }
}

static void MixChannel(SoundChannel *ch, int16_t *out, int numFrames)
{
    int i;
    if (!ch->playing || !ch->sampleData)
        return;

    for (i = 0; i < numFrames; i++) {
        uint32_t pos = (uint32_t)ch->position;

        if (pos >= ch->sampleLength) {
            if (ch->looping) {
                ch->position = 0.0f;
                pos = 0;
            } else {
                ch->playing = 0;
                ch->priority = 0;
                ch->callbackPending = 1;
                return;
            }
        }

        {
            int16_t sample = ReadSample(ch, pos);

            /* Mix into stereo output with per-channel volume */
            int32_t left  = out[i * 2 + 0] + (int32_t)(sample * ch->volumeL);
            int32_t right = out[i * 2 + 1] + (int32_t)(sample * ch->volumeR);

            /* Clamp to 16-bit range */
            if (left > 32767) left = 32767;
            else if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            else if (right < -32768) right = -32768;

            out[i * 2 + 0] = (int16_t)left;
            out[i * 2 + 1] = (int16_t)right;
        }

        ch->position += ch->playbackRate;
    }

    /* Check if we ended exactly at the boundary */
    if ((uint32_t)ch->position >= ch->sampleLength) {
        if (ch->looping) {
            ch->position = fmodf(ch->position, (float)ch->sampleLength);
        } else {
            ch->playing = 0;
            ch->priority = 0;
            ch->callbackPending = 1;
        }
    }
}

static void AudioCallback(void *userdata, Uint8 *stream, int len)
{
    int numFrames = len / (2 * sizeof(int16_t)); /* stereo 16-bit */
    int16_t *out = (int16_t *)stream;
    int i;

    (void)userdata;

    /* Zero out the output buffer */
    memset(stream, 0, len);

    /* Mix each general channel */
    for (i = 0; i < kNumHQChannels; i++)
        MixChannel(&gChannels[i], out, numFrames);

    /* Mix engine and skid channels */
    MixChannel(&gEngineChannel, out, numFrames);
    MixChannel(&gSkidChannel, out, numFrames);
}

/* ---------- public API ---------- */

void LoadSounds(void)
{
    /* Sound data is loaded from packs on demand; nothing to do here. */
}

void InitChannels(void)
{
    SDL_AudioSpec desired, obtained;
    int i;

    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            SDL_Log("SDL audio init failed: %s", SDL_GetError());
            return;
        }
    }

    /* Dispose previous device if reinitialising */
    if (gChannelsInited && gAudioDevice) {
        SDL_CloseAudioDevice(gAudioDevice);
        gAudioDevice = 0;
        gChannelsInited = 0;
    }

    memset(&desired, 0, sizeof(desired));
    desired.freq     = gPrefs.hqSound ? 44100 : 22050;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 1024;
    desired.callback = AudioCallback;
    desired.userdata = NULL;

    gAudioDevice = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (gAudioDevice == 0) {
        SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return;
    }

    gDeviceSampleRate = (float)obtained.freq;

    /* Clear all channels */
    for (i = 0; i < kNumHQChannels; i++)
        memset(&gChannels[i], 0, sizeof(SoundChannel));
    memset(&gEngineChannel, 0, sizeof(SoundChannel));
    memset(&gSkidChannel, 0, sizeof(SoundChannel));

    gGear = 0;
    gChannelsInited = gPrefs.hqSound ? kNumHQChannels : kNumChannels;

    /* Start playback */
    SDL_PauseAudioDevice(gAudioDevice, 0);
}

/*
 * Helper: load a sound buffer from pack data into a SoundChannel.
 * The sound pointer is a tSound from the pack; offset is the big-endian
 * offset to a Mac SoundHeader within that tSound.
 */
static void LoadSoundBuffer(SoundChannel *ch, tSound *sound, int sampleIndex)
{
    uint32_t numSamples = SwapU32(sound->numSamples);
    uint32_t offset;
    const uint8_t *headerPtr;
    uint32_t length = 0;
    float sampleRate = 0.0f;
    uint16_t sampleSize = 8;
    uint16_t numChannels = 1;

    if ((uint32_t)sampleIndex >= numSamples)
        sampleIndex = 0;

    offset = SwapU32(sound->offsets[sampleIndex]);
    headerPtr = (const uint8_t *)sound + offset;

    ch->sampleData   = ParseSoundHeader(headerPtr, &length, &sampleRate, &sampleSize, &numChannels);
    ch->sampleLength = length;
    ch->sampleSize   = sampleSize;
    ch->numChannels  = numChannels;
    ch->position     = 0.0f;

    /* Sanity check: if sample rate is 0 or unreasonable, default to 22050 Hz */
    if (sampleRate <= 0.0f || sampleRate > 96000.0f)
        sampleRate = 22050.0f;

    ch->baseRate     = sampleRate / gDeviceSampleRate;
}

void PlaySound(t2DPoint pos, t2DPoint velo, float freq, float vol, int id)
{
    tSound *sound;
    int i, chanIdx = 0;
    uint32_t priority = 0xFFFFFFFF;
    float pan, dist;
    int numChan;
    uint32_t sndPriority, sndFlags, sndNumSamples;

    if (!gPrefs.sound)
        return;

    sound = (tSound *)GetSortedPackEntry(kPackSnds, id, nil);
    if (!sound)
        return;

    sndNumSamples = SwapU32(sound->numSamples);
    sndPriority   = SwapU32(sound->priority);
    sndFlags      = SwapU32(sound->flags);

    numChan = gPrefs.hqSound ? kNumHQChannels : kNumChannels;

    /* Find lowest priority channel */
    SDL_LockAudioDevice(gAudioDevice);

    for (i = 0; i < numChan; i++) {
        if (priority > gChannels[i].priority) {
            priority = gChannels[i].priority;
            chanIdx = i;
        }
    }

    /* Priority check (same logic as original) */
    if (sndFlags & kSoundPriorityHigher) {
        if (priority >= sndPriority) {
            SDL_UnlockAudioDevice(gAudioDevice);
            return;
        }
    } else if (priority > sndPriority) {
        SDL_UnlockAudioDevice(gAudioDevice);
        return;
    }

    /* Distance attenuation */
    if (!gCameraObj) {
        SDL_UnlockAudioDevice(gAudioDevice);
        return;
    }
    dist = 1.0f - VEC2D_Value(VEC2D_Difference(pos, gCameraObj->pos)) * (1.0f / kMaxListenDist);
    if (dist <= 0.0f) {
        SDL_UnlockAudioDevice(gAudioDevice);
        return;
    }

    /* Stereo panning */
    pan = (pos.x - gCameraObj->pos.x) * (1.0f / kMaxPanDist);
    if (pan > 1.0f) pan = 1.0f;
    else if (pan < -1.0f) pan = -1.0f;

    /* Doppler effect (HQ only) */
    if (gPrefs.hqSound) {
        t2DPoint veloDiff  = VEC2D_Difference(velo, gCameraObj->velo);
        t2DPoint spaceDiff = VEC2D_Norm(VEC2D_Difference(gCameraObj->pos, pos));
        freq *= (float)pow(2.0, VEC2D_DotProduct(veloDiff, spaceDiff) * kDopplerFactor);
    }

    vol *= gVolume;

    /* Set up the channel */
    {
        SoundChannel *ch = &gChannels[chanIdx];
        int sampleIndex = RanInt(0, sndNumSamples);

        LoadSoundBuffer(ch, sound, sampleIndex);
        /* Note: the original sound.c had a bug where the rateMultiplierCmd
         * was sent to gEngineChannel instead of chan, so the Doppler freq
         * shift was never actually applied to PlaySound channels.  Match
         * that behaviour by ignoring freq here. */
        ch->playbackRate = ch->baseRate;
        ch->volumeL      = vol * dist * (1.0f - pan);
        ch->volumeR      = vol * dist * (1.0f + pan);
        ch->priority      = sndPriority;
        ch->looping       = 0;
        ch->callbackPending = 0;
        ch->playing       = 1;
    }

    SDL_UnlockAudioDevice(gAudioDevice);
}

void SimplePlaySound(int id)
{
    tSound *sound;
    int i, chanIdx = 0;
    uint32_t priority = 0xFFFFFFFF;
    int numChan;
    uint32_t sndPriority, sndFlags, sndNumSamples;


    if (!gPrefs.sound)
        return;

    sound = (tSound *)GetSortedPackEntry(kPackSnds, id, nil);
    if (!sound)
        return;

    sndNumSamples = SwapU32(sound->numSamples);
    sndPriority   = SwapU32(sound->priority);
    sndFlags      = SwapU32(sound->flags);

    numChan = gPrefs.hqSound ? kNumHQChannels : kNumChannels;

    SDL_LockAudioDevice(gAudioDevice);

    /* Find lowest priority channel */
    for (i = 0; i < numChan; i++) {
        if (priority > gChannels[i].priority) {
            priority = gChannels[i].priority;
            chanIdx = i;
        }
    }

    /* Priority check */
    if (sndFlags & kSoundPriorityHigher) {
        if (priority >= sndPriority) {
            SDL_UnlockAudioDevice(gAudioDevice);
            return;
        }
    } else if (priority > sndPriority) {
        SDL_UnlockAudioDevice(gAudioDevice);
        return;
    }

    /* Set up the channel: centered, full volume, normal rate */
    {
        SoundChannel *ch = &gChannels[chanIdx];
        int sampleIndex = RanInt(0, sndNumSamples);

        LoadSoundBuffer(ch, sound, sampleIndex);
        ch->playbackRate    = ch->baseRate;
        ch->volumeL         = gVolume;
        ch->volumeR         = gVolume;
        ch->priority         = sndPriority;
        ch->looping          = 0;
        ch->callbackPending  = 0;
        ch->playing          = 1;
    }

    SDL_UnlockAudioDevice(gAudioDevice);
}

void SetCarSound(float engine, float skidL, float skidR, float velo)
{
    if (gPrefs.engineSound && gPrefs.sound && gRoadInfo) {
        float engineVol, gearVelo;
        float highestGear = (gPlayerAddOns & kAddOnTurbo) ? kHighestGearTurbo : kHighestGear;

        if (!(*gRoadInfo).water) {
            /* Switch to correct gear */
            while (velo > (gGear + 1) * highestGear / kNumGears + kShiftTolerance
                   && (gGear + 1) < kNumGears)
                gGear++;
            while (velo < gGear * highestGear / kNumGears - kShiftTolerance
                   && gGear > 0)
                gGear--;

            gearVelo = (float)fabs(velo) / highestGear * kNumGears - gGear;
            velo /= kMaxNoiseVelo;
            if (gearVelo > 2.0f)
                gearVelo = 2.0f;
        } else {
            velo /= kMaxNoiseVelo;
            gearVelo = (engine + velo) / 2.0f;
        }

        velo = (float)fabs(velo);

        if (engine != -1.0f)
            engineVol = -gVolume / ((0.6f * engine + 0.15f * velo + 0.25f * gearVelo) - 2.0f);
        else
            engineVol = 0.0f;

        SDL_LockAudioDevice(gAudioDevice);

        /* Engine channel: reload buffer when callback signals completion */
        if (gEngineChannel.callbackPending) {
            tSound *sound = (tSound *)GetSortedPackEntry(kPackSnds, 132, nil);
            if (sound) {
                int idx = RanInt(0, SwapU32(sound->numSamples));
                LoadSoundBuffer(&gEngineChannel, sound, idx);
                gEngineChannel.looping          = 1;
                gEngineChannel.callbackPending  = 0;
                gEngineChannel.playing          = 1;
            }
        }

        /* Engine volume and pitch */
        {
            float evol = engineVol * gVolume;
            /* Original used 0x0048/0x0100 scale; normalise to 0-1 range:
             * 0x0048 = 72, so factor is 72/256 = 0.28125 */
            float scaledVol = 0.28125f * evol;
            if (scaledVol < 0.0f) scaledVol = 0.0f;
            if (scaledVol > 1.0f) scaledVol = 1.0f;

            gEngineChannel.volumeL = scaledVol;
            gEngineChannel.volumeR = scaledVol;

            /* Playback rate: original used rateMultiplierCmd with
             * 0x0000b000 * factor where 0xb000 = 0.6875 in 16.16 fixed.
             * The rate multiplier formula from the original: */
            gEngineChannel.playbackRate = gEngineChannel.baseRate * 0.6875f * (0.2f + 0.3f * engine + 0.2f * velo + 0.3f * gearVelo);
        }

        /* Skid processing */
        skidL -= kMinSqueakSlide;
        skidL /= kSqueakFactor;
        if (skidL < 0.0f) skidL = 0.0f;
        else if (skidL > 1.0f) skidL = 1.0f;

        skidR -= kMinSqueakSlide;
        skidR /= kSqueakFactor;
        if (skidR < 0.0f) skidR = 0.0f;
        else if (skidR > 1.0f) skidR = 1.0f;

        /* Skid channel: reload buffer when callback signals completion */
        if (gSkidChannel.callbackPending) {
            tSound *sound = (tSound *)GetSortedPackEntry(kPackSnds, (*gRoadInfo).skidSound, nil);
            if (sound) {
                int idx = RanInt(0, SwapU32(sound->numSamples));
                LoadSoundBuffer(&gSkidChannel, sound, idx);
                gSkidChannel.looping          = 1;
                gSkidChannel.callbackPending  = 0;
                gSkidChannel.playing          = 1;
            }
        }

        /* Skid volume and pitch */
        {
            /* Original: 0x0100 * skid * gVolume * (velo*0.5+0.5) per channel */
            float skidVolL = skidL * gVolume * (velo * 0.5f + 0.5f);
            float skidVolR = skidR * gVolume * (velo * 0.5f + 0.5f);
            if (skidVolL > 1.0f) skidVolL = 1.0f;
            if (skidVolR > 1.0f) skidVolR = 1.0f;

            gSkidChannel.volumeL = skidVolL;
            gSkidChannel.volumeR = skidVolR;

            /* Playback rate: original rateMultiplierCmd with
             * 0x00020000 * factor where 0x20000 = 2.0 in 16.16 fixed.
             * factor = -1/((skidL+skidR)*0.25 + velo*0.5 - 2) */
            if (skidL + skidR > 0.0f)
                gSkidChannel.playbackRate = gSkidChannel.baseRate * 2.0f * (-1.0f / ((skidL + skidR) * 0.25f + velo * 0.5f - 2.0f));
            else
                gSkidChannel.playbackRate = 0.0f;
        }

        SDL_UnlockAudioDevice(gAudioDevice);
    }
}

void StartCarChannels(void)
{
    int i;

    SDL_LockAudioDevice(gAudioDevice);

    /* Stop engine and skid channels */
    gEngineChannel.playing = 0;
    gSkidChannel.playing   = 0;
    memset(&gEngineChannel, 0, sizeof(SoundChannel));
    memset(&gSkidChannel, 0, sizeof(SoundChannel));

    if (gPrefs.engineSound && gPrefs.sound) {
        /* Queue initial engine buffers (looping) */
        for (i = 0; i < 2; i++) {
            tSound *sound = (tSound *)GetSortedPackEntry(kPackSnds, 132, nil);
            if (sound) {
                int idx = RanInt(0, SwapU32(sound->numSamples));
                LoadSoundBuffer(&gEngineChannel, sound, idx);
                gEngineChannel.looping         = 1;
                gEngineChannel.callbackPending = 0;
                gEngineChannel.playing         = 1;
            }

            sound = (tSound *)GetSortedPackEntry(kPackSnds, (*gRoadInfo).skidSound, nil);
            if (sound) {
                int idx = RanInt(0, SwapU32(sound->numSamples));
                LoadSoundBuffer(&gSkidChannel, sound, idx);
                gSkidChannel.looping         = 1;
                gSkidChannel.callbackPending = 0;
                gSkidChannel.playing         = 1;
            }
        }
    }

    SDL_UnlockAudioDevice(gAudioDevice);

    gGear = 0;
    SetCarSound(0, 0, 0, 0);
}

void BeQuiet(void)
{
    int i;

    SDL_LockAudioDevice(gAudioDevice);

    for (i = 0; i < (gPrefs.hqSound ? kNumHQChannels : kNumChannels); i++) {
        gChannels[i].playing  = 0;
        gChannels[i].priority = 0;
    }
    gEngineChannel.playing = 0;
    gSkidChannel.playing   = 0;

    SDL_UnlockAudioDevice(gAudioDevice);
}

void SetGameVolume(int volume)
{
    if (volume == -1)
        gVolume = gPrefs.volume / 256.0f;
    else
        gVolume = volume / 256.0f;
}

void SetSystemVolume(void)
{
    /* No-op in SDL port. On classic Mac OS this restored the system
     * sample rate after the game changed it. SDL handles this
     * transparently. */
}
