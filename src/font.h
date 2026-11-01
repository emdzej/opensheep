/* The .fnt fonts (data\misc\*.fnt): anti-aliased glyphs blended over the frame. 0 is Ariel18 (the
   menus' and the panels' texts), 1 Ariel12 (0x4b0fa8, 0x4b0fac). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { FONT_BIG, FONT_SMALL, FONTS };

/* 0x408a40 (part): load the two fonts of the language (names 0x44b294, 0x44b2cc). */
bool fonts_load(void);
void fonts_free(void);
/* 0x421d60: s at (x, y) in font f and colour rgb (0x00rrggbb) on the sprite target; "\r\n" starts a
   new line at x 0. */
void font_draw(int x, int y, const uint16_t *s, int f, uint32_t rgb);
/* The width of s in font f (the box 0x404a50 measures). */
int font_width(const uint16_t *s, int f);
