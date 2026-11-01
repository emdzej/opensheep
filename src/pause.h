/* The in-game menu that Esc opens over a level (0x40f560, pages 0x44c0c0), the cheat words typed while
   it is open (0x40b1d0), and the overview map held on Tab (0x409140). */
#pragma once
#include <stdbool.h>
#include "platform.h"

extern int g_pause;         /* 0x4b9678: the menu is open */

/* 0x40f510: open page p (-1: close). */
void pause_open(int p);
/* 0x40f560: the menu's frame instead of the level's; false when it is closed. */
bool pause_frame(Framebuffer *fb);
/* 0x409070, 0x409140: the TEAMPHOTO picture or the level's map instead of the level's frame; false when
   neither is up. */
bool map_frame(Framebuffer *fb);
