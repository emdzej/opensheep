/* The front end's state machine (App_Frame 0x40a170): movies, the menu level, the levels, the
   screens between them. */
#pragma once
#include <stdbool.h>
#include "game.h"
#include "platform.h"

enum {
    APP_START = 0, APP_LOGOS = 1, APP_INTRO = 2, APP_MENU = 3, APP_LEVEL = 4, APP_NEXT = 6,
    APP_RESULTS = 10, APP_CREDITS = 11, APP_HISCORES = 12, APP_END1 = 13, APP_END2 = 14, APP_END3 = 15,
};

extern int g_app_state, g_app_sub;  /* 0x4a3d8c, 0x4a3d88 */
extern int g_game_mode;             /* 0x4a1eb4: 0 the game, 1 a level for its best time, 2 training */
extern int g_next_world, g_next_level;   /* 0x4ae0f8, 0x4a4214: what the menu starts */
extern int g_4b911c;                /* 0x4b911c */
extern Progress g_prog_loaded;      /* 0x4a2048: the slot picked on the load page */
extern Progress g_prog_level;       /* 0x4a2070: the progress at the level's start */
extern int g_hiscore_mark;          /* 0x4a3d60: the entry the best-scores screen highlights, -1 none */
extern bool g_quit;                 /* PostQuitMessage */

/* One frame of App_Frame (input, the state's work, drawing into fb). false: quit. */
bool flow_frame(Framebuffer *fb);
/* Start at a state (APP_START for the movies, APP_MENU for the menu). */
void flow_start(int state);
