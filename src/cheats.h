/* Sheep.ini's switches and the cheat keys they enable. */
#pragma once
#include <stddef.h>

extern int g_cheats;             /* 0x4b0fd4 WORLD_CHEAT (menu.c) */
extern int g_our_menu;           /* 0x4b967c OUR_MENU */
extern int g_start_level, g_start_world, g_bypass;   /* 0x4b0fc8, 0x4b0fcc, 0x4b0fd0 */
extern int g_sound_fx;           /* 0x44b1d8 SOUND_FX */
extern int g_record;             /* 0x4b0f98 RECORD */

/* 0x422650: Sheep.ini from the storage (lines "NAME value"), or the same text from elsewhere. */
void ini_load(void);
void ini_parse(const char *text, size_t n);
/* 0x40b870: OUR_MENU's keys in a level (R restart, Shift+D win), and Alt+F4. */
void cheat_keys(void);
/* Parts of 0x408ca0: WORLD_CHEAT's 1-7 and OUR_MENU's Home. */
void cheat_world_keys(void);
void cheat_debug_keys(void);
/* With OUR_MENU: P saves a screenshot (0x422580); I the debug font's chart (0x40b720). */
void cheat_screenshot(void);
void cheat_font_chart(void);
