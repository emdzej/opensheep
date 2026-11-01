/* Internal to the per-world files (lv1.c ..): helpers they share and their dispatch entries. */
#pragma once
#include "exe.h"
#include "level.h"
#include "levels.h"
#include "moves.h"
#include "paths.h"
#include "player.h"
#include "rng.h"
#include "sheep.h"
#include "sound.h"
#include "world.h"
#include <math.h>
#include <string.h>

static inline int iabs(int v) { return v < 0 ? -v : v; }

static inline uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* g_obj_by_id[id] as a slot (the original uses whatever is there). */
static inline AnimObj *obj(int id)
{
    if (id < 0 || id >= MAX_OBJ_ID) return NULL;
    int s = g_obj_by_id[id];
    return s >= 0 && s < MAX_ANIM ? &g_anim[s] : NULL;
}

/* The original sends the shepherd to 0x4032c0 too, which writes past the flock; only sheep go. */
static inline void actor_out(int n)
{
    if (n < MAX_SHEEP) sheep_out(n);
}

static inline void freeze_list(uint32_t va, uint32_t stride)
{
    /* object ids, 0-terminated: hold their animations */
    for (uint32_t id; (id = exe_u32(va)) != 0; va += stride) g_anim[g_obj_by_id[id]].countdown = 10;
}

static inline void map_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    /* 0x402fa0: copy a block of map cells (rows of the header's width) */
    uint16_t *s = g_lv.map + (sy / 24) * g_lv.w + sx / 32, *t = g_lv.map + (dy / 24) * g_lv.w + dx / 32;
    for (; h > 0; h--, s += g_lv.w, t += g_lv.w)
        for (int i = 0; i < w; i++) t[i] = s[i];
}

/* Each world's file: its descriptor hooks and routines by address (false / NULL: not its own). */
bool lv1_actor(uint32_t va, int32_t *actor);
MoveFn lv1_move(uint32_t va);
bool lv2_actor(uint32_t va, int32_t *actor);
MoveFn lv2_move(uint32_t va);
