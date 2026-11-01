/* The level's .NPF paths (0x4a04b0): an offset table at +8 (one u32 per path, from the file start)
   to runs of 8-byte points {i16 flag, i16 ticks, i16 x, i16 y}; flag 0 marks the start, 2 the end. */
#pragma once
#include "objects.h"

extern int g_path_x, g_path_y;                 /* 0x4b9750, 0x4b974c: set by path_start */
void paths_load(void);                         /* 0x401600 */
void paths_free(void);
/* 0x411b40: point i of path p (x at +4, y at +6). */
void path_point(int p, int i, int *x, int *y);
/* 0x411b60: the first point of path p into g_path_x / g_path_y. */
void path_start(int p);
/* 0x411bc0: an object follows path p (mode 0-4, see path_follow); flags & 1 puts it on the start. */
void path_begin(AnimObj *a, int p, int mode, int flags);
/* 0x411c10: one tick along the path (d[0x23] path, 0x24 point, 0x25 tick, 0x26 mode); 1 at the end
   (modes 3 and 4 then remove the object). */
int path_follow(AnimObj *a);
/* 0x403570: turn a 16-direction object one step toward d16. */
void anim_turn16(AnimObj *a, int d16);
