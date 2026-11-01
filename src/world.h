/* Current world / level and the files that belong to it. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

enum {
    LEVEL_TABLE = 0x44adec,     /* descriptor pointers, indexed world * 6 + level */
    WORLDS = 10, LEVELS_PER_WORLD = 6,
};

extern int g_world, g_level;   /* 0x4b0b60, 0x4b0f18 */

/* Select world/level and mount its archives (0x4033a0): main.xp, sound.xp, then World00.xp for a world
   map (level 0) or World%02d.xp. false if the slot has no level in this build. */
bool world_select(int world, int level);
/* The level's folder name from its descriptor (UTF-16 in the exe), e.g. "W01Level01". */
const char *world_level_name(void);
/* "Data\World01\W01Level01\W01Level01" + ext (0x401000). */
const char *world_level_path(const char *ext);
