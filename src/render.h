/* The level frame's draw list and the depth-sorted pass (0x40c1f0): draw records are bucketed by
   the map row they stand on and 4 layers, sorted by foot y, and each row's sprites are drawn before
   that row's foreground cells (cell types 1-2 whole, 3+ through a per-type mask), so props and
   animals walk behind trees, fences and walls. */
#pragma once
#include "objects.h"
#include "platform.h"

enum { MAX_DRAW = 1256, MAX_SHADOW = 160 };

extern DrawRec *g_draw[MAX_DRAW];   /* 0x47c7bc */
extern int g_ndraw;                 /* 0x4ae428 */
extern DrawRec g_shadow[MAX_SHADOW];/* 0x49c180 */
extern int g_nshadow;               /* 0x4a4218 */
extern int g_hide_foreground;       /* 0x4b0f34 */

void draw_list_reset(void);         /* start of the draw part of 0x40b9b0 */
void draw_list_add(DrawRec *r);
/* 0x40c1f0 into fb (sprite_target is set to fb). */
void render_sorted(Framebuffer *fb);
