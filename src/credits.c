/* The credits (state 11: 0x40d7a0, 0x40d9e0, 0x40dc70): data\local\credits.tx? a block of names at a
   time, each line flying in from a random edge to its place, then the block dropping off the bottom;
   two palm trees and a hula sheep dance, and three of the developers get their portraits. */
#include "credits.h"
#include "exe.h"
#include "flow.h"
#include "hud.h"
#include "input.h"
#include "objects.h"
#include "rng.h"
#include "sprites.h"
#include "surface.h"
#include "xp.h"
#include "cheats.h"
#include "sound.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LINES = 10 };

/* 0x4adf00, 0x20 bytes a line */
static struct cline {
    uint8_t drop;        /* +0x00: falling off the screen */
    int32_t tx, ty;      /* +0x04: where it flies to */
    int32_t delay;       /* +0x0c: ticks before it starts */
    float x, y;          /* +0x10 */
    float slope;         /* +0x18: |dy / dx| of its flight */
    uint16_t *text;      /* +0x1c */
} line[LINES];
static int nlines;       /* 0x4ae0b0 */
static uint8_t music_phase;   /* 0x4a40fc */
static int hold;         /* 0x4adef8: ticks the block stays */

static uint16_t *text;   /* 0x4ae430: the file, UTF-16 */
static int text_n;       /* its characters (see next_block) */
static int cursor;       /* 0x4ae42c */

static int timer[3], period[3] = { 5, 2, 1 }, frame[3], frames[3];   /* 0x4a905c.., 0x4a906c.., 0x4b0b6c.., 0x4a3d6c.. */
static uint8_t blink_steve, blink_kevin;   /* 0x4ae0fc, 0x4a40e0 */
static Surface *back;    /* 0x4b10d8 */

static size_t wlen(const uint16_t *s) { size_t n = 0; while (s[n]) n++; return n; }

static bool weq(const uint16_t *a, uint32_t va)
{
    uint16_t b[32];
    exe_wtext(va, b, 32);
    for (size_t i = 0;; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
}

/* 0x4236c0: a line up to its CR or LF (copied without it); returns the characters used, one past. */
static int read_line(uint16_t *out, int cap)
{
    int n = 0;
    while (cursor + n < text_n && text[cursor + n] != 10 && text[cursor + n] != 13) {
        if (n < cap - 1) out[n] = text[cursor + n];
        n++;
    }
    out[n < cap - 1 ? n : cap - 1] = 0;
    return n + 1;
}

static int add_line(const uint16_t *s, int sx, int sy, int tx, int ty)
{
    /* 0x40dd90 */
    if (nlines > 9) return nlines - 1;
    int i = nlines++;
    size_t n = wlen(s);
    line[i].text = malloc((n + 1) * 2);
    if (line[i].text) memcpy(line[i].text, s, (n + 1) * 2);
    line[i].tx = tx;
    line[i].ty = ty;
    line[i].x = (float)sx;
    line[i].y = (float)sy;
    float slope = ((float)ty - (float)sy) / ((float)tx - line[i].x);
    line[i].slope = slope < 0.0f ? slope * -1.0f : slope;
    return i;
}

/* 0x40e050: the next block (its line count first, '|' a blank line, "//" a comment). */
static bool next_block(void)
{
    nlines = 0;
    hold = 200;
    int delay = 0;
    uint16_t buf[256];
    int count;
    /* The original compares the position (characters) with the file's size in bytes and so reads on
       past the end of the text after the last block; here the text ends there. */
    for (;;) {
        if (cursor >= text_n) return false;
        cursor += read_line(buf, 256);
        if (buf[0] == '/') { if (buf[1] == '/') continue; }
        else if (buf[0] == '|') { buf[0] = ' '; break; }
        else if (buf[0] == 0) continue;
        break;
    }
    count = 0;
    for (const uint16_t *p = buf; *p >= '0' && *p <= '9'; p++) count = *p - '0' + count * 10;   /* 0x40e020 */
    int ty = 0x50;
    for (int k = 0; k != count;) {
        if (cursor >= text_n) return false;
        cursor += read_line(buf, 256);
        if (buf[0] == '/') { if (buf[1] == '/') continue; }
        else if (buf[0] == '|') buf[0] = ' ';
        else if (buf[0] == 0) continue;
        int len = (int)wlen(buf) + 2, sx = 0, sy = 0;
        switch (crt_rand() & 7) {
        case 0: case 1: sx = 0x280; sy = -0x28; break;
        case 2: sx = 0x280; sy = 0xf0; break;
        case 3: sx = 0x280; sy = 0x1e0; break;
        case 4: sx = 0x140; sy = 0x1e0; break;
        case 5: sy = 0x1e0; sx = len * -0xf - 0xf; break;
        case 6: sy = 0xf0; sx = len * -0xf - 0xf; break;
        case 7: sy = -0x28; sx = len * -0xf - 0xf; break;
        }
        int i = add_line(buf, sx, sy, 0x140 - len * 0xf / 2, ty);
        line[i].delay = delay;
        k++;
        delay += 10 + crt_rand() % 10;
        ty += 0x1e;
    }
    return true;
}

static void free_lines(void)
{
    for (int i = 0; i < nlines; i++) free(line[i].text);
    memset(line, 0, sizeof line);
}

void credits_start(void)
{
    /* 0x40d7a0 */
    memset(line, 0, sizeof line);
    for (int i = 0; i < 3; i++) { timer[i] = 4; frame[i] = 0; }
    nlines = 0;
    surface_free(back);
    back = surface_load_bmp(exe_str(0x44bb30));
    sprite_bank_load_misc(SPR_BANK_MISC, "Misc.spr");
    sprite_bank_load_misc(SPR_BANK_TEXT_YELLOW, "TextYellow.spr");
    sprite_bank_load_misc(SPR_BANK_TEXT_BLUE, "TextBlue.spr");
    sprite_bank_load(0x65, exe_str(0x44bb10));    /* c_palms */
    sprite_bank_load(0x66, exe_str(0x44baec));    /* c_hulasheep */
    frames[0] = sprite_frames(0x65);
    frames[1] = sprite_frames(0x66);
    frames[2] = 0;
    if (g_sound_fx) sound_stream_file(exe_str(0x44bacc), 1);
    char ext[8], path[64];
    snprintf(ext, sizeof ext, "%s", exe_str(0x44a098));
    ext[3] = (char)exe_u8(0x456264 + (uint32_t)(g_lang & 0xf));
    snprintf(path, sizeof path, exe_str(0x44bab4), ext);
    free(text);
    text = NULL;
    text_n = 0;
    cursor = 1;              /* past the byte-order mark */
    size_t size = 0;
    uint8_t *f = file_load(path, &size);
    if (f) {
        text_n = (int)(size / 2);
        text = malloc((size_t)text_n * 2 + 2);
        if (text)
            for (int i = 0; i < text_n; i++) text[i] = (uint16_t)(f[2 * i] | f[2 * i + 1] << 8);
        else
            text_n = 0;
        free(f);
    }
    next_block();
    sprite_bank_load(0x68, exe_str(0x44ba9c));    /* Kev */
    sprite_bank_load(0x69, exe_str(0x44ba80));    /* Steve */
    sprite_bank_load(0x6a, exe_str(0x44ba68));    /* Ben */
}

static void title(void)
{
    /* 0x40dd20: one character at a time */
    const uint16_t *s = str_get(0xcd);
    int n = (int)wlen(s), x = 0x140 - (int)((unsigned)(n * 15) >> 1);
    for (int i = 0; i < n; i++, x += 15) {
        uint16_t c[2] = { s[i], 0 };
        text_draw(c, x, 10);
    }
}

static void at_hot(int x, int y, int bank, int f)
{
    SprFrame sf;
    int hx = 0, hy = 0;
    if (sprite_frame(bank, f, &sf)) { hx = sf.hot_x; hy = sf.hot_y; }
    sprite_draw(hx + x, hy + y, bank, (uint32_t)f, SPR_SCALE_1, SPR_SCALE_1);
}

static void tick(void)
{
    for (int k = 0; k < 3; k++)
        if (timer[k]-- == 0) {
            timer[k] = period[k];
            if (++frame[k] >= frames[k]) frame[k] = 0;
        }
    for (int i = 0; i < LINES; i++) {
        if (!line[i].text) continue;
        if (line[i].delay != 0) { line[i].delay--; continue; }
        int ix = (int)line[i].x;
        if (line[i].tx == ix && line[i].ty == (int)line[i].y) continue;
        double dx = (double)line[i].tx - line[i].x, dy = (double)line[i].ty - line[i].y;
        double d = sqrt(dx * dx + dy * dy) * 0.05;
        if (line[i].drop) {
            line[i].y += 10.0f;
            continue;
        }
        line[i].x = (float)(ix > line[i].tx ? line[i].x - d : d + line[i].x);
        double step = d * line[i].slope;
        line[i].y = (float)((int)line[i].y > line[i].ty ? line[i].y - step : step + line[i].y);
    }
}

static void lines_draw(void)
{
    /* 0x40de50 */
    for (int i = 0; i < LINES; i++) {
        if (!line[i].text) continue;
        uint16_t buf[256], fmt[8];
        size_t n = wlen(line[i].text);
        if (n > 255) n = 255;
        memcpy(buf, line[i].text, n * 2);
        buf[n] = 0;
        if (weq(line[i].text, 0x44bb88)) {
            /* STEVEN KEEN blinks */
            wfmt(buf, 256, exe_wtext(blink_steve & 1 ? 0x44bb78 : 0x44bb80, fmt, 8), line[i].text);
            blink_steve++;
            sprite_draw(0x1ea, 0x32, 0x69, 0, SPR_SCALE_1, SPR_SCALE_1);
        }
        if (weq(line[i].text, 0x44bb60)) {
            /* KEVIN COOK too */
            wfmt(buf, 256, exe_wtext(blink_kevin & 1 ? 0x44bb78 : 0x44bb80, fmt, 8), line[i].text);
            blink_kevin++;
            sprite_draw(0x32, 0x32, 0x68, 0, SPR_SCALE_1, SPR_SCALE_1);
        }
        text_draw(buf, (int)line[i].x, (int)line[i].y);
        if (weq(line[i].text, 0x44bb48) && sprite_frames(0x6a)) sprite_draw(0x1ea, 0x96, 0x6a, 0, SPR_SCALE_1, SPR_SCALE_1);
    }
}

static void block_step(void)
{
    /* 0x40e320: once the last line is in place (or off the bottom), hold, drop the block, then the next */
    if (nlines > 0) {
        const struct cline *l = &line[nlines - 1];
        bool there = l->drop ? l->y >= 480.0f
                             : !(l->x < (float)(l->tx - 1) || (float)(l->tx + 1) < l->x ||
                                 l->y < (float)(l->ty - 1) || (float)(l->ty + 1) < l->y);
        if (!there) return;
    }
    if (line[0].drop != 1) {
        if (--hold == 0) {
            for (int i = nlines - 1, d = (nlines - 1) * 15; i >= 0; i--, d -= 15) {
                line[i].ty = 0x1e0;
                line[i].delay = d;
                line[i].drop = 1;
            }
            hold = 2;
        }
        return;
    }
    /* 0x40e2c0 */
    free_lines();
    if (!next_block()) g_app_sub = 2;
}

void credits_frame(Framebuffer *fb)
{
    /* 0x40d9e0, one tick a frame */
    input_keys_tick();
    for (int k = 0; k < 0x40; k++)
        if (g_keydown[k]) { g_app_sub = 2; break; }   /* 0x439680 */
    memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
    if (back) surface_blt_fast(fb, 0, 0, back, NULL);
    sprite_target(fb);
    title();
    at_hot(0xd3, 0xe2, 0x65, frame[0]);
    at_hot(0x11e, 0x112, 0x66, frame[1]);
    tick();
    lines_draw();
    block_step();
    if (music_phase++ % 5 == 0) sound_tick();
}

void credits_end(void)
{
    /* 0x40dc70 (Ben's bank stays) */
    surface_free(back);
    back = NULL;
    free_lines();
    free(text);
    text = NULL;
    sprite_bank_free(SPR_BANK_MISC);
    sprite_bank_free(0x65);
    sprite_bank_free(0x66);
    sprite_bank_free(0x68);
    sprite_bank_free(0x69);
    sprite_bank_free(SPR_BANK_TEXT_YELLOW);
    sprite_bank_free(SPR_BANK_TEXT_BLUE);
}
