#include "hint.h"
#include "exe.h"
#include "font.h"
#include "hud.h"
#include "moves.h"
#include "sprites.h"
#include "world.h"
#include "xp.h"
#include <stdio.h>
#include <stdlib.h>

enum { HINT_LINES = 10 };                   /* 0x4b9754 to 0x4b977c */

static uint16_t *text;                      /* 0x47db60 */
static int text_n;                          /* 0x49f9a4 */
static const uint16_t *lines[HINT_LINES + 1];
static uint16_t not_found[0x20];

void hint_text_free(void)
{
    free(text);
    text = NULL;
    text_n = 0;
}

void hint_text_load(void)
{
    /* "Data\local\" (0x44aa04) + the level's name + the language's .txe; each '\r' becomes the end
       of its line */
    char path[96];
    snprintf(path, sizeof path, "%s%s%s", exe_str(0x44aa04), g_desc ? world_level_name() : "", lang_ext());
    hint_text_free();
    size_t size = 0;
    uint8_t *f = file_load(path, &size);
    if (!f) {
        fprintf(stderr, "could not find %s\n", path);
        return;
    }
    text_n = (int)(size / 2);
    text = malloc(((size_t)text_n + 1) * sizeof *text);
    if (text) {
        for (int i = 0; i < text_n; i++) {
            text[i] = (uint16_t)(f[2 * i] | f[2 * i + 1] << 8);
            if (text[i] == 0xd) text[i] = 0;
        }
        text[text_n] = 0;
    } else {
        text_n = 0;
    }
    free(f);
}

/* the char at i, 0 past the end (the original reads on: it bounds its scans by the byte count) */
static uint16_t at(int i) { return i < text_n ? text[i] : 0; }

void hint_show(int n)
{
    /* 0x411a70: the lines after the line starting with the two digits of n */
    g_hint = n;
    g_hint_shown = 1;
    if (text) {
        int p = 0;
        for (;;) {
            uint16_t c = at(p);
            bool ended = false;
            while (c < '0' || c > '9') {
                while (c != '\n' && !ended) {
                    if (++p > text_n) ended = true;
                    c = at(p);
                }
                p++;
                if (ended) goto missing;
                c = at(p);
            }
            int v = (c - '0') * 10 + at(p + 1) - '0';
            p += 2;
            if (v == n) break;
        }
        int k = 0;
        for (;;) {
            while (p < text_n && at(p) != '\n') p++;
            p++;
            if (p >= text_n || at(p) == ':') break;
            if (k == HINT_LINES) break;     /* (the original writes on past its ten) */
            lines[k++] = text + p;
            lines[k] = NULL;
        }
        if (k == 0) lines[0] = NULL;
        return;
    }
missing:
    lines[0] = exe_wtext(0x44c54c, not_found, 0x20);   /* "Hint message not found" */
    lines[1] = NULL;
}

static void hint_box(int x, int y, int w, int h)
{
    /* 0x40b370: the box's inside darkened and tinted blue, its edges from bank 0x149 */
    Framebuffer *fb = sprite_get_target();
    int right = x + w, bottom = y + h;
    if (!fb || x < 0 || right > fb->w || y < 0 || bottom > 0x1e0) return;
    for (int j = 0; j < h; j++) {
        uint16_t *p = fb->pixels + (size_t)(y + j) * fb->pitch + x;
        for (int i = 0; i < w; i++) p[i] = (uint16_t)(((p[i] & 0xf7de) >> 1) + 0xf);
    }
    for (int i = 0, bx = x + 0x17; i < w / 32 - 1; i++, bx += 0x20) {
        sprite_draw(bx, y - 7, 0x149, 4, 0x100, 0x100);
        sprite_draw(bx, bottom - 3, 0x149, 6, 0x100, 0x100);
    }
    for (int i = 0, by = y + 0x10; i < h / 0x18 - 1; i++, by += 0x18) {
        sprite_draw(x - 7, by, 0x149, 5, 0x100, 0x100);
        sprite_draw(right - 2, by, 0x149, 7, 0x100, 0x100);
    }
    sprite_draw(x - 7, y - 7, 0x149, 0, 0x100, 0x100);
    sprite_draw(right - 0x19, y - 7, 0x149, 1, 0x100, 0x100);
    sprite_draw(x - 7, bottom - 0x12, 0x149, 2, 0x100, 0x100);
    sprite_draw(right - 0x19, bottom - 0x12, 0x149, 3, 0x100, 0x100);
}

void hint_draw(void)
{
    /* 0x40b590: the lines' box measured from (g_hint_y, g_hint_x) 20 px a line, then rounded up to
       whole 32 x 24 edge pieces and centred above the bottom */
    if (!g_hint_shown || !lines[0]) return;
    int left = g_hint_y, right = g_hint_y, top = g_hint_x, bottom = g_hint_x, y = g_hint_x;
    for (int i = 0; i < HINT_LINES && lines[i]; i++, y += 0x14) {
        int b[4];
        font_box(lines[i], FONT_BIG, b);
        if (b[0] + g_hint_y < left) left = b[0] + g_hint_y;
        if (right < b[2] + g_hint_y) right = b[2] + g_hint_y;
        if (b[1] + y < top) top = b[1] + y;
        if (bottom < b[3] + y) bottom = b[3] + y;
    }
    int rows = (bottom - top + 0x17) / 0x18;
    int w = ((right - left + 0x1f) / 32 + 1) * 32;
    hint_box(300 - w / 2, 0x1a8 - rows * 0x18, w + 6, rows * 0x18 + 10);
    y = 0x1ae - rows * 0x18;
    for (int i = 0; i < HINT_LINES && lines[i]; i++, y += 0x14)
        font_draw(0x140 - w / 2, y, lines[i], FONT_BIG, 0xffffff);
}
