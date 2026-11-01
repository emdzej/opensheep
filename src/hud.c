#include "hud.h"
#include "font.h"
#include "exe.h"
#include "game.h"
#include "moves.h"
#include "play.h"
#include "player.h"
#include "sound.h"
#include "sprites.h"
#include "sheep.h"
#include "xp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_lang;                          /* 0x4a40ba */

static uint16_t **strings;           /* 0x65fe64 */
static int nstrings;                 /* 0x65fe68 */
static const uint16_t empty[1];

static size_t wlen(const uint16_t *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

void strings_free(void)
{
    /* 0x423700 */
    for (int i = 0; i < nstrings; i++) free(strings[i]);
    free(strings);
    strings = NULL;
    nstrings = 0;
}

/* 0x4236c0: one line (up to CR or LF) into line; returns the characters used, the break included.
   The original reads on until it finds one; here the end of the file ends the line too. */
static int read_line(uint16_t *line, size_t cap, const uint16_t *src, int left)
{
    int n = 0;
    while (n < left && src[n] != 10 && src[n] != 13) {
        if ((size_t)n < cap - 1) line[n] = src[n];
        n++;
    }
    line[(size_t)n < cap - 1 ? (size_t)n : cap - 1] = 0;
    return n + 1;
}

/* A line that defines a string: anything but a blank line or a // comment. */
static bool is_entry(const uint16_t *line)
{
    if (line[0] == '/') return line[1] != '/';
    return line[0] != 13 && line[0] != 10 && line[0] != 0;
}

/* The leading number of an entry (its first word, atoi'd as the original does) and where the text
   starts (after the blanks that follow it). */
static int entry_id(const uint16_t *line, size_t *text)
{
    char word[256];
    size_t n = 0, len = wlen(line);
    while (n < len && n < sizeof word - 1 && line[n] != ' ') { word[n] = (char)line[n]; n++; }
    word[n] = 0;
    size_t i = strlen(word);
    while (i < len && (line[i] == ' ' || line[i] == 10 || line[i] == 13 || line[i] == 9)) i++;
    *text = i;
    return atoi(word);
}

const char *lang_ext(void)
{
    static char ext[8];
    snprintf(ext, sizeof ext, "%s", exe_str(0x44a098));
    ext[3] = (char)exe_u8(0x456264 + (uint32_t)(g_lang & 0xf));
    return ext;
}

bool strings_load(void)
{
    /* 0x4233b0 on "data\local\GameStrings" + ".txe" with the language letter (0x44a098, 0x456264) */
    char path[64];
    snprintf(path, sizeof path, exe_str(0x45628c), lang_ext());
    strings_free();
    size_t size = 0;
    uint8_t *f = file_load(path, &size);
    if (!f) return false;
    int n = (int)(size / 2);
    uint16_t *w = malloc((size_t)n * 2 + 2);
    if (!w) { free(f); return false; }
    for (int i = 0; i < n; i++) w[i] = (uint16_t)(f[2 * i] | f[2 * i + 1] << 8);
    free(f);
    const uint16_t *src = w;
    if (n > 0 && src[0] == 0xfeff) { src++; n--; }
    uint16_t line[256];
    size_t text;
    int max = 0;
    for (int at = 0; at < n;) {
        at += read_line(line, 256, src + at, n - at);
        if (!is_entry(line)) continue;
        int id = entry_id(line, &text);
        if (id > max) max = id;
    }
    nstrings = max + 1;
    strings = calloc((size_t)nstrings, sizeof *strings);
    for (int at = 0; strings && at < n;) {
        at += read_line(line, 256, src + at, n - at);
        if (!is_entry(line)) continue;
        int id = entry_id(line, &text);
        if (id < 0) continue;           /* the original would write before the table */
        size_t len = wlen(line + text);
        free(strings[id]);
        strings[id] = malloc(len * 2 + 2);
        if (strings[id]) memcpy(strings[id], line + text, len * 2 + 2);
    }
    free(w);
    return strings != NULL;
}

const uint16_t *str_get(int n)
{
    if (n < 0 || n >= nstrings || !strings[n]) return empty;
    return strings[n];
}

uint16_t *str_mut(int n)
{
    return n < 0 || n >= nstrings ? NULL : strings[n];
}

const uint16_t *exe_wtext(uint32_t va, uint16_t *dst, size_t cap)
{
    size_t i = 0;
    for (; i < cap - 1 && exe_u16(va + 2 * (uint32_t)i); i++) dst[i] = exe_u16(va + 2 * (uint32_t)i);
    dst[i] = 0;
    return dst;
}

int wfmt(uint16_t *out, size_t cap, const uint16_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t n = 0;
#define PUT(c) do { if (n < cap - 1) out[n] = (uint16_t)(c); n++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        bool zero = *fmt == '0';
        if (zero) fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == 'd') {
            char num[16];
            int len = snprintf(num, sizeof num, "%d", va_arg(ap, int));
            int neg = num[0] == '-';
            if (neg) PUT('-');
            for (int i = len; i < width; i++) PUT(zero ? '0' : ' ');
            for (int i = neg; i < len; i++) PUT(num[i]);
        } else if (*fmt == 's') {
            const uint16_t *s = va_arg(ap, const uint16_t *);
            size_t len = s ? wlen(s) : 0;
            for (size_t i = len; i < (size_t)width; i++) PUT(' ');
            for (size_t i = 0; i < len; i++) PUT(s[i]);
        } else if (*fmt == '%') {
            PUT('%');
        } else if (!*fmt) {
            break;
        }
    }
#undef PUT
    out[n < cap ? n : cap - 1] = 0;
    va_end(ap);
    return (int)n;
}

void text_draw(const uint16_t *s, int x, int y)
{
    /* 0x40bdb0. Languages 8 and up draw with the Ariel12 font (0x4b0fa8) instead, through the queue
       0x405250 / 0x405430 drains at once: a leading space dropped, then a leading '£' yellow or '$'
       red (the rest of the string as it is) */
    if (g_lang >= 8) {
        uint32_t rgb = 0x7fffff00;
        if (*s == ' ') s++;
        if (*s == 0xa3) s++;
        else if (*s == '$') { rgb = 0x7fff0000; s++; }
        font_draw(x, y, s, 1, rgb);
        return;
    }
    int bank = 0;
    for (; *s; s++) {
        if (*s == 0xa3) bank = 0;
        else if (*s == '$') bank = 1;
        else {
            sprite_draw(x, y, SPR_BANK_TEXT_YELLOW + bank, (uint32_t)(*s - 0x20), SPR_SCALE_1, SPR_SCALE_1);
            x += 0x11;
        }
    }
}

static int last_secs;                /* 0x4b983c */
static uint8_t demo_blink;           /* 0x4b9844 */

/* The flashing bonus icon (bank 0x147, frame score_flash / 4 % 6) placed after its hot spot
   (0x421b10), and the score after it. */
static void score_at(int icon_x, int text_x)
{
    int frame = g_score_flash / 4 % 6;
    SprFrame f;
    int hx = sprite_frame(0x147, frame, &f) ? f.hot_x : 0;
    sprite_draw(hx + icon_x, 2, 0x147, (uint32_t)frame, SPR_SCALE_1, SPR_SCALE_1);
    uint16_t fmt[16], buf[64];
    wfmt(buf, 64, exe_wtext(0x44cb60, fmt, 16), g_score);
    text_draw(buf, text_x, 10);
}

static void count_at(int icon, int icon_x, int n, int text_x)
{
    uint16_t fmt[16], buf[64];
    sprite_draw(icon_x, 2, icon, 0, SPR_SCALE_1, SPR_SCALE_1);
    wfmt(buf, 64, exe_wtext(0x44bc64, fmt, 16), n);
    text_draw(buf, text_x, 10);
}

void hud_draw(void)
{
    /* 0x4181d0 */
    int t = g_time_limit - g_clock;
    if (t < 0) t = 0;
    int secs = t % 0xe10 / 0x3c;
    uint16_t fmt[32], buf[256];
    switch (g_hud_style) {
    case 1: {
        /* the level: sheep still out, sheep safe, the score, the clock (ticking under 20 s) */
        int s = last_secs;
        if (t < 0x4b0 && t != 0 && secs != last_secs) {
            s = secs;
            sound_play_at(&g_anim[PLAYER_SLOT], t / 0x3c % 2 == 0 ? 0x5c : 0x5d, 0, 0);
        }
        last_secs = s;
        count_at(0x145, 2, g_nsheep - g_saved - g_sheep_out, 0x38);
        count_at(0x146, 0x78, g_saved, 0xae);
        score_at(0xf0, 0x126);
        sprite_draw(0x1e0, 2, 0x148, (uint32_t)(0xb - secs / 5), SPR_SCALE_1, SPR_SCALE_1);
        exe_wtext(t < 0x4b0 && !(t & 0x10) ? 0x44cb48 : 0x44cb30, fmt, 32);
        wfmt(buf, 256, fmt, t / 0xe10, secs);
        text_draw(buf, 0x216, 10);
        if (g_demo == 2) {
            if (demo_blink < 0x19) text_draw(str_get(0x4a), 0x104, 0x1c7);
            demo_blink = (uint8_t)((demo_blink + 1) % 0x28);
        }
        break;
    }
    case 2: {
        /* the race (2.5): the shepherd's sheep's speed as a bar of I's, the time run */
        int min = g_clock / 0xe10, sec = g_clock % 0xe10 / 0x3c, hund = g_clock / 6 % 10 * 10;
        wfmt(buf, 256, str_get(0xb8));
        buf[3] = ' ';
        int bar = (int)(((float *)g_sheep[0].d)[1] * 5.0f);
        int i = 0;
        for (; i < bar && 4 + i < 255; i++) buf[4 + i] = 'I';
        buf[4 + i] = 0;
        text_draw(buf, 10, 2);
        wfmt(buf, 256, exe_wtext(0x44cb0c, fmt, 32), str_get(0xb7), min, sec, hund);
        text_draw(buf, 10, 0x20);
        break;
    }
    case 3:
    case 4:
        score_at(2, 0x38);
        count_at(0x145, 0xa0, g_hud_style == 3 ? g_lives45 : g_bonus_count, 0xd6);
        break;
    case 5:
        count_at(0x145, 2, g_nsheep - g_saved - g_sheep_out, 0x38);
        count_at(0x146, 0x78, g_saved, 0xae);
        score_at(0xf0, 0x126);
        break;
    case 6:
        wfmt(buf, 256, exe_wtext(0x44caec, fmt, 32), str_get(0xbc), g_anim[0].d[0x15], str_get(0xbd),
             g_anim[0].d[0x16]);
        text_draw(buf, 0, 2);
        break;
    case 7:
        count_at(0x145, 2, g_bonus_count, 0x38);
        score_at(0x78, 0xae);
        break;
    case 8:
        score_at(2, 0x38);
        break;
    }
}
