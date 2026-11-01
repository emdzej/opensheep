/* Sound effects. TODO: the mixer (DirectSound, 0x41f000..0x41f5b0) and the .SLF lists; for now the
   entry points the simulation calls, with the original's visibility test so callers see the same
   return values. */
#pragma once
#include "objects.h"

/* 0x401e00: play sound id for an object if it is near the screen; returns the voice (0: none). */
int sound_play_at(AnimObj *a, int id, int flags, int arg);
