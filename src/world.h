/* Current world / level and the files that belong to it. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    LEVEL_TABLE = 0x44adec,     /* descriptor pointers, indexed world * 6 + level */
    WORLDS = 10, LEVELS_PER_WORLD = 6,
};

extern int g_world, g_level;   /* 0x4b0b60, 0x4b0f18 */
extern uint32_t g_desc;        /* 0x4a20b4: the level's descriptor (exe address) */

/* Descriptor fields (exe addresses or values): +0x00 name (UTF-16), +0x08 zone rectangles,
   +0x0c init hook, +0x10 tick hook, +0x18 the shepherd's states >= 100, +0x1c / +0x20 draw hooks,
   +0x2c tile animations, +0x30 release hook, +0x44 sheep banks, +0x48 shepherd banks, +0x4c the
   level's move routines, +0x54 gates. */
enum {
    DESC_NAME = 0x00, DESC_ZONES = 0x08, DESC_INIT = 0x0c, DESC_TICK = 0x10, DESC_PLAYER = 0x18,
    DESC_DRAW1 = 0x1c, DESC_DRAW2 = 0x20, DESC_TILE_ANIMS = 0x2c, DESC_RELEASE = 0x30,
    DESC_SHEEP_BANKS = 0x44, DESC_PLAYER_BANKS = 0x48, DESC_MOVES = 0x4c, DESC_50 = 0x50, DESC_GATES = 0x54,
};
uint32_t desc_u32(uint32_t off);

/* Select world/level and mount its archives (0x4033a0): main.xp, sound.xp, then World00.xp for a world
   map (level 0) or World%02d.xp. false if the slot has no level in this build. */
bool world_select(int world, int level);
/* The level's folder name from its descriptor (UTF-16 in the exe), e.g. "W01Level01". */
const char *world_level_name(void);
/* "Data\World01\W01Level01\W01Level01" + ext (0x401000). */
const char *world_level_path(const char *ext);
