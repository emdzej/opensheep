/* Sprite banks and Sprite_Draw 0x41f5e0.

   A bank is the .SPR file as loaded (header, then the frame table at +0x2c, 8 bytes per frame:
   u32 offset, u16 group, u16 index). A frame is {u32 size, i32 w, h, hot_x, hot_y} and its rows; each
   row is a run list ended by 0x00:
     0x80..0xff  skip 256 - b pixels
     0x01..0x3f  b pixels of {u8 alpha (1..4 = 25/50/75/100 %), u16 rgb565}
     0x40..0x7f  b - 0x3f opaque u16 rgb565 pixels
   The original has four copies of the row loop (plain, clipped, mirrored, mirrored + clipped) with the
   same blend formulas; clipping is per pixel in x and per row in y. One loop with a direction and a
   clip test draws the same pixels. */
#include "sprites.h"
#include "xp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *banks[SPR_BANKS];
static size_t bank_size[SPR_BANKS];
static Framebuffer *target;
static int clip_x0 = 0, clip_y0 = 0, clip_x1 = 640, clip_y1 = 480;
static int tint;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

void sprite_bank_free(int bank)
{
    if (bank < 0 || bank >= SPR_BANKS) return;
    free(banks[bank]);
    banks[bank] = NULL;
    bank_size[bank] = 0;
}

void sprite_banks_free_all(void)
{
    for (int i = 0; i < SPR_BANKS; i++) sprite_bank_free(i);
}

bool sprite_bank_load(int bank, const char *path)
{
    /* 0x421c60 */
    if (bank < 0 || bank >= SPR_BANKS) return false;
    sprite_bank_free(bank);
    size_t n = 0;
    uint8_t *d = file_load(path, &n);
    if (!d || n < 0x2c || rd32(d) < 0 || (size_t)rd32(d) > (n - 0x2c) / 8) {
        fprintf(stderr, "Failed to load sprite bank %s (%d)\n", path, bank);
        free(d);
        return false;
    }
    banks[bank] = d;
    bank_size[bank] = n;
    return true;
}

bool sprite_bank_load_misc(int bank, const char *name)
{
    /* 0x421ba0 */
    char path[128];
    snprintf(path, sizeof path, "data\\misc\\%s", name);
    return sprite_bank_load(bank, path);
}

int sprite_frames(int bank) { return bank >= 0 && bank < SPR_BANKS && banks[bank] ? rd32(banks[bank]) : 0; }

bool sprite_frame(int bank, int frame, SprFrame *out)
{
    if (frame < 0 || frame >= sprite_frames(bank)) return false;
    const uint8_t *b = banks[bank];
    uint32_t off = (uint32_t)rd32(b + 0x2c + 8 * frame);
    if ((size_t)off + 0x14 > bank_size[bank]) return false;
    const uint8_t *f = b + off;
    out->size = rd32(f);
    out->w = rd32(f + 4);
    out->h = rd32(f + 8);
    out->hot_x = rd32(f + 12);
    out->hot_y = rd32(f + 16);
    out->rows = f + 0x14;
    return true;
}

bool sprite_frame_ids(int bank, int frame, int *group, int *index)
{
    if (frame < 0 || frame >= sprite_frames(bank)) return false;
    const uint8_t *e = banks[bank] + 0x2c + 8 * frame;
    *group = rd16(e + 4);
    *index = rd16(e + 6);
    return true;
}

void sprite_target(Framebuffer *fb) { target = fb; }

void sprite_clip(int x0, int y0, int x1, int y1)
{
    clip_x0 = x0; clip_y0 = y0; clip_x1 = x1; clip_y1 = y1;
}

void sprite_get_clip(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = clip_x0; *y0 = clip_y0; *x1 = clip_x1; *y1 = clip_y1;
}

void sprite_tint(int flags) { tint = flags; }

/* 0x4218c0, rgb565 branch: halve the flagged channels. */
static uint16_t tinted(uint16_t c)
{
    if (tint & 1) c = (uint16_t)((c & 0xf000) >> 1 | (c & 0x7ff));
    if (tint & 2) c = (uint16_t)((c & 0x7c0) >> 1 | (c & 0xf81f));
    if (tint & 4) c = (uint16_t)((c & 0x1f) >> 1 | (c & 0xffe0));
    return c;
}

static uint16_t blend(uint16_t d, uint16_t s, int a)
{
    switch (a) {
    case 4: return s;
    case 3: return (uint16_t)(s - ((s & 0xe79c) >> 2) + ((d & 0xe79c) >> 2));
    case 2: return (uint16_t)(((s & 0xf7de) >> 1) + ((d & 0xf7de) >> 1));
    case 1: return (uint16_t)(d - ((d & 0xe79c) >> 2) + ((s & 0xe79c) >> 2));
    default: return d;                                     /* other alpha bytes leave the pixel */
    }
}

/* Skip one encoded row. */
static const uint8_t *skip_row(const uint8_t *p)
{
    for (;;) {
        uint8_t b = *p++;
        if (!b) return p;
        if (b & 0x80) continue;
        p += b < 0x40 ? (size_t)b * 3 : (size_t)(b - 0x3f) * 2;
    }
}

/* 0x4197b0: a frame's shadow. Every other row of the top h / 2 x 2 rows darkens the target, each row
   one pixel further right, from (x - h / 2, y + h / 2): translucent pixels by a quarter, opaque ones
   by half; when clipped, all of them by half. */
static void draw_sheared(int x, int y, int bank, uint32_t frame)
{
    SprFrame f;
    if (!sprite_frame(bank, (int)(frame & 0x3fffffff), &f)) return;
    int half = (int)((uint32_t)f.h >> 1), ext = f.w + half;
    x -= half;
    y += half;
    if (!(x < clip_x1 && clip_x0 <= x + ext && y < clip_y1 && clip_y0 <= y + half)) return;
    bool clipped = x < clip_x0 || clip_x1 <= x + ext || y < clip_y0 || clip_y1 <= y + half;
    bool mirror = frame & SPR_MIRROR;
    int dir = mirror ? -1 : 1;
    const uint8_t *p = f.rows;
    int rows = half;
    if (clipped) {
        if (y < clip_y0) {
            int skip = clip_y0 - y;
            x += skip;
            if ((rows -= skip) <= 0) return;
            for (skip *= 2; skip; skip--) p = skip_row(p);
            y = clip_y0;
        }
        if (y + rows - clip_y1 >= 0) rows -= y + rows - clip_y1;
    }
    for (; rows > 0; rows--, y++, x++) {
        uint16_t *row = target->pixels + (ptrdiff_t)y * target->pitch;
        int px = mirror ? x + f.w - 1 : x;
        for (;;) {
            uint8_t b = *p++;
            if (!b) break;
            if (b & 0x80) { px += dir * (256 - b); continue; }
            bool alpha = b < 0x40;
            int n = alpha ? b : b - 0x3f;
            for (int i = 0; i < n; i++, p += alpha ? 3 : 2, px += dir) {
                uint16_t *q = &row[px];
                if (clipped) {
                    if (clip_x0 <= px && px < clip_x1) *q = (uint16_t)((*q & 0xf7de) >> 1);
                } else if (alpha) {
                    *q = (uint16_t)(*q - ((*q & 0xe79c) >> 2));
                } else {
                    *q = (uint16_t)((*q & 0xf7de) >> 1);
                }
            }
        }
        p = skip_row(p);
    }
}

void sprite_draw(int x, int y, int bank, uint32_t frame, int sx, int sy)
{
    if (!target) return;
    if (!(clip_x0 < clip_x1 && clip_x1 >= 0 && clip_y0 < clip_y1 && clip_y1 >= 0)) return;
    bool tint_on = false;
    if (tint && (int)(frame & 0x7fffffff) < sprite_frames(bank)) {
        tint_on = true;                                    /* drawn as bank 399: the shear flag is lost */
        frame &= (uint32_t)SPR_MIRROR;
    }
    if ((int)(frame & 0x3fffffff) >= sprite_frames(bank)) return;
    if (frame & SPR_SHEAR) {
        draw_sheared(x, y, bank, frame);
        return;
    }
    if (sx != SPR_SCALE_1 || sy != SPR_SCALE_1) {
        static bool warned;
        if (!warned) fprintf(stderr, "sprite_draw: scaled blitter 0x420320 not ported\n"), warned = true;
        return;
    }
    SprFrame f;
    if (!sprite_frame(bank, (int)(frame & 0x3fffffff), &f)) return;
    if (!(x < clip_x1 && clip_x0 <= x + f.w && y < clip_y1 && clip_y0 <= y + f.h)) return;
    bool mirror = frame & SPR_MIRROR;
    bool clipped = x < clip_x0 || clip_x1 <= x + f.w || y < clip_y0 || clip_y1 <= y + f.h;
    const uint8_t *p = f.rows;
    int rows = f.h;
    if (clipped && y < clip_y0) {
        int skip = clip_y0 - y;
        rows = f.h - skip;
        if (rows == 0 || f.h < skip) return;
        for (; skip; skip--) p = skip_row(p);
        y = clip_y0;
    }
    if (clipped && y + rows - clip_y1 >= 0) rows -= y + rows - clip_y1;
    int dir = mirror ? -1 : 1;
    int x_start = mirror ? x + f.w - 1 : x;
    for (; rows > 0; rows--, y++) {
        uint16_t *row = target->pixels + (ptrdiff_t)y * target->pitch;
        int px = x_start;
        for (;;) {
            uint8_t b = *p++;
            if (!b) break;
            if (b & 0x80) { px += dir * (256 - b); continue; }
            if (b < 0x40) {
                for (int i = 0; i < b; i++, p += 3, px += dir)
                    if (!clipped || (clip_x0 <= px && px < clip_x1)) {
                        uint16_t s = rd16(p + 1);
                        if (tint_on) s = tinted(s);
                        row[px] = blend(row[px], s, p[0]);
                    }
            } else {
                for (int i = 0; i < b - 0x3f; i++, p += 2, px += dir)
                    if (!clipped || (clip_x0 <= px && px < clip_x1)) {
                        uint16_t s = rd16(p);
                        row[px] = tint_on ? tinted(s) : s;
                    }
            }
        }
    }
}
