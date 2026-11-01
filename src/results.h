/* The screen after a level: its score, the bonuses and the flock (won), or why it was lost. */
#pragma once
#include "platform.h"

/* 0x40ff30: set the screen up for the level's end code (1 won, 7 a bonus level, 3-5 lost). */
void results_start(int end);
/* One frame of it (0x410bc0, or 0x410590 when lost); g_app_sub becomes 1 when it is done. */
void results_frame(Framebuffer *fb);
