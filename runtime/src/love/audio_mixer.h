#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <stdbool.h>

#include <ppu-types.h>

typedef struct AudioClip AudioClip;

/* A voice handle stays safe to use after its voice ends: the calls below just do nothing. */
typedef s32 AudioVoice;
#define AUDIO_NO_VOICE (-1)

int audio_open(void);
void audio_close(void);

/* Reads the file, once per path. Short sounds are decoded on first play and kept. */
AudioClip *audio_clip_load(const char *path, bool stream);

AudioVoice audio_voice_start(AudioClip *clip, f32 volume, f32 pitch);
void audio_voice_set_volume(AudioVoice voice, f32 volume);
void audio_voice_set_pitch(AudioVoice voice, f32 pitch);
void audio_voice_stop(AudioVoice voice);
bool audio_voice_is_playing(AudioVoice voice);
void audio_stop_all(void);

/* Blocks the port played as silence because the mixer fell behind. Each one is an audible click. */
u32 audio_underruns(void);

#endif
