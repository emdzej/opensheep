#include "font.h"
#include "exe.h"
#include "hud.h"
#include "sprites.h"
#include "xp.h"
#include <stdio.h>
#include <stdlib.h>

/* A loaded font (the 0x824-byte object of 0x404880). The file: a header (+0x00 its size, +0x18 the
   cell width, +0x1c the line height, +0x2c the number of ranges), the ranges {u16 first, u16 last,
   u32 size, u32 glyph offsets[last - first + 1]} and then the glyphs: w, h, x offset, y offset,
   flags (bit 0: nothing to draw) and the pixels run-length coded over the w x h box (0 n: n clear,
   0xff n: n solid, any other byte: one pixel of that coverage). */
typedef struct {
    uint8_t *file;
    size_t size;
    uint32_t ranges, glyphs, nranges;   /* offsets into file */
    uint32_t hi_first[256], hi_count[256];
    int line_h, spacing;
} Font;

static Font fonts[FONTS];

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void font_free(Font *f)
{
    free(f->file);
    *f = (Font){ 0 };
}

void fonts_free(void)
{
    for (int i = 0; i < FONTS; i++) font_free(&fonts[i]);
}

/* 0x404880 */
static bool font_load(Font *f, const char *path)
{
    font_free(f);
    f->file = file_load(path, &f->size);
    if (!f->file || f->size < 0x30) { fprintf(stderr, "Failed to load font %s\n", path); font_free(f); return false; }
    const uint8_t *d = f->file;
    f->ranges = rd32(d);
    f->nranges = rd32(d + 0x2c);
    uint32_t at = f->ranges;
    for (uint32_t i = 0; i < f->nranges; i++) {
        if (at + 8 > f->size) { font_free(f); return false; }
        at += rd32(d + at + 4);
    }
    f->glyphs = at;
    /* per high byte: the run of ranges whose first and last characters' high bytes take it in */
    for (uint32_t hi = 0; hi < 256; hi++) {
        uint32_t r = f->ranges, i = 0;
        for (; i < f->nranges; i++, r += rd32(d + r + 4)) {
            uint32_t a = d[r + 1], b = d[r + 3];
            if (a > hi) break;
            if (hi <= b) {
                f->hi_first[hi] = r;
                while (i < f->nranges && d[r + 1] <= hi && hi <= d[r + 3]) {
                    f->hi_count[hi]++;
                    i++;
                    r += rd32(d + r + 4);
                }
                break;
            }
        }
    }
    f->line_h = (int)rd32(d + 0x1c);
    f->spacing = (int)(rd32(d + 0x18) >> 3);
    return true;
}

bool fonts_load(void)
{
    char path[128];
    bool ok = true;
    for (int i = 0; i < FONTS; i++) {
        uint32_t names = i == FONT_BIG ? 0x44b294 : 0x44b2cc;
        snprintf(path, sizeof path, exe_str(0x44b79c), exe_str(exe_u32(names + 4 * (uint32_t)(g_lang & 0xf))));
        ok &= font_load(&fonts[i], path);
    }
    return ok;
}

/* 0x405170: the glyph of c, the first glyph of the file if the font has none */
static const uint8_t *glyph(const Font *f, uint16_t c)
{
    const uint8_t *d = f->file;
    uint32_t r = f->hi_first[c >> 8];
    for (uint32_t i = 0; f->hi_count[c >> 8] && i < f->hi_count[c >> 8]; i++, r += rd32(d + r + 4)) {
        uint16_t a = rd16(d + r), b = rd16(d + r + 2);
        if (c < a) break;
        if (c <= b) {
            uint32_t g = f->glyphs + rd32(d + r + 8 + 4 * (uint32_t)(c - a));
            return g + 5 <= f->size ? d + g : NULL;
        }
    }
    return f->glyphs + 5 <= f->size ? d + f->glyphs : NULL;
}

/* One pixel of coverage a (1..254) of colour c over *p, the 5:6:5 blend of 0x404e10: each colour
   term is scaled and floored on its own for red and green, together for blue. */
static uint16_t blend(uint16_t c, uint16_t p, unsigned a)
{
    unsigned na = a ^ 0xff;
    unsigned b = ((c & 0x1f) * a + (p & 0x1f) * na) >> 8;
    unsigned g = ((((c & 0x7e0) >> 5) * a & 0xff00) + (((p & 0x7e0) >> 5) * na & 0xff00)) >> 3;
    unsigned r = ((((unsigned)c >> 11) * a & 0xff00) + (((unsigned)p >> 11) * na & 0xff00)) << 3;
    return (uint16_t)(r | g | b);
}

/* 0x404e10: glyph g with its pen at (x, y). Glyphs past the right or bottom edge are dropped, the
   top is clipped. */
static void draw_glyph(Framebuffer *fb, const Font *f, const uint8_t *g, int x, int y, uint16_t col)
{
    if (g[4] & 1) return;
    int w = g[0], h = g[1], gx = g[2] + x, gy = g[3] + y, skip = 0;
    if (x + w >= 0x280 || y < 0 || h + y >= 0x1e0) return;
    if (h + gy >= 0x1e0) h = 0x1e0 - gy;
    if (gy < 0) {
        skip = -gy;
        h += gy;
        gy = 0;
        if (h <= 0) return;
        h += skip;                      /* rows counted from the glyph's top */
    }
    if (h <= 0 || w <= 0 || gx < 0) return;   /* the original writes before the row at gx < 0 */
    const uint8_t *p = g + 5, *end = f->file + f->size;
    int n = 0, total = w * h;
#define PIX(k, v) do { int row_ = (k) / w; if (row_ >= skip && gy + row_ - skip < fb->h && gx + (k) % w < fb->w) { \
        uint16_t *q_ = fb->pixels + (ptrdiff_t)(gy + row_ - skip) * fb->pitch + gx + (k) % w; v; } } while (0)
    while (n < total && p < end) {
        uint8_t b = *p++;
        if (b == 0) {
            if (p >= end) break;
            n += *p++;
        } else if (b == 0xff) {
            if (p >= end) break;
            for (int k = *p++; k > 0 && n < total; k--, n++) PIX(n, *q_ = col);
        } else {
            PIX(n, *q_ = blend(col, *q_, b));
            n++;
        }
    }
#undef PIX
}

static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)((c >> 8 & 0xf800) | (c >> 5 & 0x7e0) | (c >> 3 & 0x1f));
}

/* 0x404cf0 over the 640 x 480 box (0x421d60 passes it) */
static int run(const uint16_t *s, int x, int y, int fi, uint32_t rgb, bool draw)
{
    const Font *f = &fonts[fi < 0 || fi >= FONTS ? 0 : fi];
    Framebuffer *fb = sprite_get_target();
    if (!f->file || !s) return 0;
    if (*s == 0xfeff) s++;
    uint16_t col = rgb565(rgb);
    int x0 = x, right = x;
    for (; *s; s++) {
        if ((s[0] == 13 && s[1] == 10) || (s[0] == 10 && s[1] == 13)) {
            s++;
            y += f->line_h;
            x = 0;
            continue;
        }
        const uint8_t *g = glyph(f, *s);
        if (!g || x > 0x280) continue;
        if (0x1e0 < f->line_h + y) break;
        if (draw && fb && y >= 0) draw_glyph(fb, f, g, x, y, col);
        x += g[0] + g[2] + f->spacing;
        if (x > right) right = x;
    }
    return right - x0;
}

void font_draw(int x, int y, const uint16_t *s, int f, uint32_t rgb) { run(s, x, y, f, rgb, true); }

int font_width(const uint16_t *s, int f) { return run(s, 0, 0, f, 0, false); }
