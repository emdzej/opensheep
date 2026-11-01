/* Sound: the .SLF sound lists (0x41ea60), the original's DirectSound buffer objects (0x405a80, 0xc4
   bytes) and its slot tables (16 effects 0x4b9984, 4 streams 0x4b9854), and a mixer standing in for
   DirectSound.

   A voice is the handle of one buffer object (0: none), what the original hands back as a pointer;
   callers keep them in their scratch dwords. The mixer runs inside the core, a fixed number of output
   frames per app frame (sound_frame), so when a buffer ends, and with it which slots are free and what
   the play calls return, is the same on every host. */
#pragma once
#include "objects.h"
#include <stdbool.h>

enum { SOUND_RATE = 44100, SOUND_CHANNELS = 2 };

extern float g_sfx_volume, g_music_volume;   /* 0x4b9850, 0x4b9860 (menu.c) */
extern int g_sfx_level;                     /* 0x451848 (menu.c): the effects' slider, 0..11 */
extern int g_sound_loaded;                   /* 0x4a19c8: the lists are loaded */
extern int g_music, g_music2;                /* 0x4a40cc the level's music, 0x4a40d0 a looping stream over it */

/* 0x401730: with SOUND_FX, the generic, breed, player and level lists; every id but the music
   (100-109) loaded; the list freed. */
void sound_lists_load(void);
/* 0x40ff30: the results' list, data\\misc\\bonus.slf. */
void sound_bonus_load(void);
/* 0x41ea60: one list; reset starts a new id table. */
bool sound_list_load(const char *path, bool reset);
/* 0x41f2e0 */
int sound_preload(int id);
/* 0x41f3a0: an effect; returns the voice (0: none, or no free slot). */
int sound_play(int id, int loop, int stream);
/* 0x401e00: an effect for an object near the screen, at its distance from the shepherd. */
int sound_play_at(AnimObj *a, int id, int loop, int stream);
/* 0x41f160: a stream (the music). */
int sound_stream(int id, int loop, int stream);
/* 0x418fd0 then 0x418cd0(v, 0x10009, 0): a file streamed straight into a stream slot (the credits'
   and high scores' music). */
int sound_stream_file(const char *path, int loop);
/* 0x419120: update every voice in a slot, drop those that ended. */
void sound_tick(void);
/* 0x419490 / 0x4193c0: stop effect / stream slot i, -1 all. */
void sound_stop_sfx(int i);
void sound_stop_streams(int i);
/* Inline in many places (0x4025c0, 0x413e60, ...): stop a voice and empty its effect slot. */
void sound_drop(int v);
/* The same for a stream: stopped, its slot freed (0x4193c0). */
void sound_stream_drop(int v);
/* The end of a power-up (0x412480, 0x414c70): the music back to 0.95, the stream over it stopped. */
void sound_music_restore(void);
/* 0x41ef60: the lists and every buffer. */
void sound_free_all(void);

/* The buffer methods: 0x4376b0 (volume), 0x4060d0 (update; false once it ended), 0x406650 (stop). */
void snd_set_vol(int v, float vol);
float snd_vol(int v);
bool snd_update(int v);
void snd_stop(int v);
/* 0x406700: the voice at the object (its volume following it, if looping). */
void sound_follow(int v, AnimObj *a);

/* An outside source added into the mix (the movie's sound, Bink's own buffer); NULL: none. fn adds
   n stereo frames at SOUND_RATE into dst. */
void sound_feed(void (*fn)(float *dst, int n));

/* Mix one app frame (16 ms) of output; the host takes it with sound_output. */
void sound_frame(void);
const float *sound_output(int *frames);
