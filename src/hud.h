/* The game's texts (data\local\GameStrings.txe), the sprite font and the in-level panel. Strings are
   UTF-16 as in the original (wchar_t on Windows). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern int g_lang;                   /* 0x4a40ba: index into ENGLISH.. (0x44afa4); 0 English */

/* 0x4233b0: load the string table of the current language; 0x423700 frees it. */
bool strings_load(void);
void strings_free(void);
/* 0x423760: string n (an empty string where the original would return NULL). */
const uint16_t *str_get(int n);

/* The same string, writable (the menus write digits into some); NULL if there is none. */
uint16_t *str_mut(int n);

/* swprintf 0x43d130, the subset the game uses: %d with a 0 flag and a width, %s of a UTF-16 string. */
int wfmt(uint16_t *out, size_t cap, const uint16_t *fmt, ...);
/* A UTF-16 string from the exe (the format strings), copied to dst. */
const uint16_t *exe_wtext(uint32_t va, uint16_t *dst, size_t cap);

/* 0x40bdb0: draw a string at (x, y) with the TextYellow / TextBlue banks ('£' yellow, '$' blue). */
void text_draw(const uint16_t *s, int x, int y);

/* 0x4181d0: the panel for g_hud_style (the counts, the score, the clock). */
void hud_draw(void);
