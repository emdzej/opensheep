/* The movie player: 0x403a30 opens a Bink file as g_movie, and while it is set the main loop
   (0x438350) plays it in place of App_Frame: a frame whenever BinkWait allows, the keys' hold
   counters ticked, and a key, pad button or mouse click (0x422930) or the last frame ends it. */
#pragma once
#include "platform.h"
#include <stdbool.h>

/* 0x403a30. Nothing plays when the file can't be opened. */
void movie_play(const char *path);
bool movie_active(void);
/* One app frame of the main loop's movie branch. */
void movie_frame(Framebuffer *fb);
