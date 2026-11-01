/* A level in play: its start (0x406cc0), the fixed 16 ms simulation tick and the frame (0x40b9b0),
   and the camera that follows the shepherd (Camera_Update 0x40cb40). */
#pragma once
#include "game.h"
#include "platform.h"

extern AnimObj *g_cam_target;       /* 0x4b9740: the camera follows this object instead */
extern int g_flock_n, g_flock_sx, g_flock_sy;   /* 0x4bbd4c..0x4bbd54: sheep in view and their sum */
extern int g_clock, g_time_limit;   /* 0x4b0f20, 0x4a2090: level ticks and the limit for the difficulty */
extern int g_tim_hard, g_tim_normal, g_tim_easy;   /* 0x4a1eac, 0x4a28c8, 0x4a20b0 */
extern int g_paused;                /* 0x4b0f3c */
extern int8_t g_frozen;             /* 0x4b0f24 */

/* 0x40f2a0 (part): a new game's flock: 10 sheep, 20 on hard. */
void game_new(void);
/* Level_LoadResources 0x401950 for the selected level (world_select). */
bool level_load_resources(void);
/* 0x406cc0 with mode 0: place the flock and the shepherd. */
void level_start(void);
/* Level_LoadTim 0x406c00: the time limits of a level (g_tim_*). */
void level_load_tim(int world, int level);
/* 0x40cb40 */
void camera_update(Player *p);
/* One simulation tick of 0x40b9b0's loop. */
void play_tick(void);
/* The draw part of 0x40b9b0. */
void play_draw(Framebuffer *fb);
