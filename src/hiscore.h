/* The best scores: HiScore.tab in the storage and the screen that shows them. */
#pragma once
#include "platform.h"

/* Start-up: the table from HiScore.tab, or the defaults (written out). */
void hiscores_init(void);
/* 0x40fe20: a finished game's (mode 0) or level's (mode 1) score; its entry, -1 if none. Saves. */
int hiscore_insert(int world, int level, int mode, int score, int time);
/* State 12: set up (0x40e420), one frame (0x40e6b0: g_app_sub becomes 2 when done), the end (0x40e8f0). */
void hiscores_start(void);
void hiscores_frame(Framebuffer *fb);
void hiscores_end(void);
