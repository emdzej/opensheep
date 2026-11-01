#include "render.h"
#include "exe.h"
#include "level.h"
#include "sprites.h"
#include <string.h>

DrawRec *g_draw[MAX_DRAW];
int g_ndraw;
DrawRec g_shadow[MAX_SHADOW];
int g_nshadow;
int g_hide_foreground;

enum { ROWS = 23, LAYERS = 4, SLOTS = 0x80, SLOT_MAX = 0x7e };
static int buckets[ROWS * LAYERS][SLOTS];   /* 0x4ae08c: [0] count, then draw list indices */
static int push_curve[200];                 /* 0x4b0b7c, filled by 0x40b070 */

static Framebuffer *fb;

void draw_list_reset(void) { g_ndraw = 0; g_nshadow = 0; }

void draw_list_add(DrawRec *r)
{
    if (g_ndraw < MAX_DRAW) g_draw[g_ndraw++] = r;
}

/* ------------------------------------------------------------------ foreground tiles

   The tile pointers point into the loaded .BLO, like the original's, and some clipped paths read
   outside the tile (they lose track of the row start); reads go through the file buffer so they see
   the same bytes. Writes are linear in the framebuffer, as in the locked surface. */

static uint16_t src_px(const uint16_t *tile, int i)
{
    const uint8_t *p = (const uint8_t *)tile + 2 * (ptrdiff_t)i;
    if (p < g_lv.blo || p + 2 > g_lv.blo + g_lv.blo_size + 4) return 0;
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static int8_t mask_at(const int8_t *m, int i)
{
    const uint8_t *p = (const uint8_t *)m + i;
    if (p < g_lv.blo || p >= g_lv.blo + g_lv.blo_size + 4) return 0;
    return (int8_t)*p;
}

static void put(ptrdiff_t i, uint16_t v)
{
    if (i >= 0 && i < (ptrdiff_t)fb->pitch * fb->h) fb->pixels[i] = v;
}

/* 0x407e90: whole tile, transform xf */
static void tile_whole(const uint16_t *t, int x, int y, int xf)
{
    for (int r = 0; r < 24; r++) {
        int dy = xf & 2 ? y + 23 - r : y + r;
        for (int c = 0; c < 32; c++) put((ptrdiff_t)dy * fb->pitch + x + c, t[r * 32 + (xf & 1 ? 31 - c : c)]);
    }
}

/* 0x4083e0: whole tile through a 32x24 byte mask (nonzero = draw); xf 3 ignores the mask */
static void tile_whole_masked(const uint16_t *t, const int8_t *m, int x, int y, int xf)
{
    if (xf == 3) { tile_whole(t, x, y, 3); return; }
    for (int r = 0; r < 24; r++) {
        int dy = xf == 2 ? y + 23 - r : y + r;
        for (int c = 0; c < 32; c++)
            if (m[r * 32 + c]) put((ptrdiff_t)dy * fb->pitch + x + c, t[r * 32 + (xf == 1 ? 31 - c : c)]);
    }
}

/* 0x408180 */
static void tile_overlay(const uint16_t *t, int x, int y, int xf)
{
    int x0, y0, x1, y1;
    sprite_get_clip(&x0, &y0, &x1, &y1);
    if (!t) return;
    if (x0 < x && x + 32 < x1 && y0 < y && y + 24 < y1) { tile_whole(t, x, y, xf); return; }
    if (!(x0 < x + 32 && (x < x1 || y + 24 < y0) && y < y1)) return;
    int w = 32, h = 24, si = 0;
    if (x < x0) { w = 32 - (x0 - x); si += x0 - x; x = x0; }
    if (x1 <= x + 32) w = w - 32 + (x1 - x);
    if (y < y0) { int k = y0 - y; y = y0; h = 24 - k; si += k * 32; }
    if (y1 <= y + 24) h = h - 24 + (y1 - y);
    if (h <= 0) return;
    ptrdiff_t d = (ptrdiff_t)fb->pitch * y + x;
    if (xf == 1 || xf == 3) si += w * 2 - 32;
    for (; h; h--) {
        for (int i = 0; i < w; i++) put(d++, src_px(t, xf == 1 || xf == 3 ? si-- : si++));
        d += fb->pitch - w;
        switch (xf) {
        case 0: si += 32 - w; break;
        case 1: si += w + 32; break;
        case 2: si += -32 - w; break;
        default: si += w - 32; break;
        }
    }
}

/* 0x408630 */
static void tile_overlay_masked(const uint16_t *t, const int8_t *m, int x, int y, int xf)
{
    int x0, y0, x1, y1;
    sprite_get_clip(&x0, &y0, &x1, &y1);
    if (!t) return;
    if (x0 < x && x + 32 < x1 && y0 < y && y + 24 < y1) { tile_whole_masked(t, m, x, y, xf); return; }
    if (!(x0 < x + 32 && (x < x1 || y + 24 < y0) && y < y1)) return;
    int w = 32, h = 24, si = 0, mi = 0;
    if (x < x0) { int k = x0 - x; w = 32 - k; mi += k; si += k; x = x0; }
    if (x1 <= x + 32) w = w - 32 + (x1 - x);
    if (y < y0) { int k = y0 - y; h = 24 - k; si += k * 32; mi += k * 32; y = y0; }
    if (y1 <= y + 24) h = (y1 - y) - 24 + h;
    if (h <= 0) return;
    if (xf == 0) {
        ptrdiff_t d = (ptrdiff_t)fb->pitch * y + x;
        for (; h; h--) {
            for (int i = 0; i < w; i++, d++, si++, mi++)
                if (mask_at(m, mi)) put(d, src_px(t, si));
            d += fb->pitch - w;
            si += 32 - w;
            mi += 32 - w;
        }
    } else if (xf == 1) {
        /* starts w pixels to the right, walks the source backwards, one mask byte per row */
        ptrdiff_t d = (ptrdiff_t)fb->pitch * y + w + x;
        for (; h; h--) {
            for (int i = 0; i < w; i++, d++, si--)
                if (mask_at(m, mi)) put(d, src_px(t, si));
            d += fb->pitch - w;
            mi += w + 32;
            si += w + 32;
        }
    }
    /* xf 2 and 3: the clipped path draws nothing */
}

/* ------------------------------------------------------------------ sorting */

static int8_t mask_of(uint32_t type, int fx, int fy)
{
    return mask_at((const int8_t *)g_lv.tab2, (int)((fy - 24 + (int)type * 24) * 32 + fx));
}

static uint32_t cell_type(int cx, int cy)
{
    long i = (long)cy * g_lv.w + cx;                       /* linear, as the original indexes */
    if (i < 0 || i >= (long)g_lv.w * g_lv.h) return 0;
    return g_lv.cells[i] & 0xff;
}

/* 0x40c700: the screen row a sprite spanning map rows top..bottom belongs to, from the masks of the
   cells under its left / right foot. */
static int mask_row(int fx, int fy, int cx, int top, int bottom, int row0)
{
    if (top < 0) top = 0;
    if (bottom > 0x77) bottom = 0x77;
    uint32_t t = cell_type(cx, bottom);
    int r = -0x7b;
    if (t && mask_of(t, fx, fy)) {
        for (int j = bottom; top < j;) {
            if (j < 1) break;
            j--;
            if (cell_type(cx, j)) r = j;
        }
    } else {
        for (int j = bottom; top < j;) {
            if (j < 1) break;
            j--;
            uint32_t u = cell_type(cx, j);
            if (u && mask_of(u, fx, fy) == 2) r = j;
        }
    }
    if (r == -0x7b) r = bottom;
    r -= row0;
    return r < 0 ? 0 : r < 0x16 ? r : 0x15;
}

/* 0x40c070: a sprite with push set moves overlapping sprites (kind < 4) behind it */
static void apply_push(void)
{
    for (int a = 0; a < g_ndraw; a++) {
        DrawRec *p = g_draw[a];
        if (!p->push) continue;
        for (int b = 0; b < g_ndraw; b++) {
            DrawRec *q = g_draw[b];
            if (q->kind >= 4 || q == p) continue;
            if (!(q->x < p->x + p->w && p->x < q->w + q->x)) continue;
            if (!(q->y < p->y + p->h && p->y < q->y + q->h)) continue;
            int d = p->y - q->h - q->y + p->h;
            d = d < 0 ? 0 : d > 199 ? 199 : d;
            if (p->push == 1 ? push_curve[d] + p->x < q->w / 2 + q->x
                             : q->w / 2 + q->x < p->x - push_curve[d] + p->w)
                q->depth += d + 25;
        }
    }
}

static void bucket_add(int row, int layer, int idx)
{
    int b = layer + row * LAYERS;
    if (b < 0 || b >= ROWS * LAYERS) return;               /* the original indexes out of the table */
    if (buckets[b][0] < SLOT_MAX) buckets[b][++buckets[b][0]] = idx;
}

/* 0x40cf20: n in at most width digits (the highest dropped), no leading zeros, left-aligned in
   spaces; a minus takes one of them. */
static void number_text(int n, int width, char out[16])
{
    memset(out, ' ', 12);
    int neg = n < 0, at = neg;
    /* (0 - n in unsigned: the original negates INT_MIN to itself) */
    uint32_t v = neg ? 0u - (uint32_t)n : (uint32_t)n;
    if (neg) {
        width--;
        out[0] = '-';
    }
    bool started = false;
    uint32_t p = 10000000;
    for (int left = 8; p != 1; left--, p /= 10) {
        uint32_t d = v / p;
        v %= p;
        if (d != 0) started = true;
        /* a digit past ten (v over 99999999) shows as the character after '9', as in the original */
        if (left <= width && started && at < 11) out[at++] = (char)('0' + d);
    }
    out[at < 11 ? at : 11] = (char)('0' + v);
    int end = width + neg;
    out[end < 0 ? 0 : end < 12 ? end : 12] = 0;
}

/* 0x405730: text in the exe's 8x16 font (0x44d558, 0x80 bytes a character; small: every other row),
   colour c where the font is set, the rest left (transparent), clipped to the screen. */
static void font_text(Framebuffer *fb, const char *s, int x, int y, uint16_t c, bool small)
{
    if (!fb) return;
    for (; *s; s++, x += 8) {
        int ch = (uint8_t)*s - 0x20;
        if (ch < 0 || ch >= 0x60) continue;                /* (the original reads outside the font) */
        uint32_t g = 0x44d558 + 0x80 * (uint32_t)ch;
        for (int r = 0; r < (small ? 8 : 16); r++) {
            int py = y + r;
            uint32_t row = g + 8 * (uint32_t)(small ? 2 * r : r);
            for (int i = 0; i < 8; i++) {
                int px = x + i;
                if (px < 0 || px >= 640 || py < 0 || py >= 480 || px >= fb->w || py >= fb->h) continue;
                if (exe_u8(row + (uint32_t)i)) fb->pixels[(size_t)py * (size_t)fb->pitch + (size_t)px] = c;
            }
        }
    }
}

/* 0x40c870 */
static void draw_rec(DrawRec *p)
{
    if (p->kind == 6) {
        /* a number (frame) w / 8 digits wide, its shadow in 0x4a1e78 (0) and itself in 0x44ad5c
           (0x7fff), both as 0x408930 sets them */
        char t[16];
        number_text((int32_t)p->frame, (p->w + (p->w >> 31 & 7)) >> 3, t);
        Framebuffer *fb = sprite_get_target();
        font_text(fb, t, p->x + 1, p->y + 1, 0, p->h == 8);
        font_text(fb, t, p->x, p->y, 0x7fff, p->h == 8);
        return;
    }
    if (p->kind == 7)
        sprite_draw(p->x, p->shadow_dy + p->y, p->bank, p->frame | SPR_SHEAR, p->scale_x, p->scale_y);
    if (p->kind < 1 || p->kind >= 6) return;
    if (p->clip_x0 != -1) {
        int a = p->clip_x0 - g_lv.cam_x, b = p->clip_x1 - g_lv.cam_x;
        int c = p->clip_y0 - g_lv.cam_y, d = p->clip_y1 - g_lv.cam_y;
        sprite_clip(a < 0 ? 0 : a < 640 ? a : 640, c < 0 ? 0 : c < 481 ? c : 480,
                    b < 640 ? (b < 0 ? 0 : b) : 640, d < 481 ? (d < 0 ? 0 : d) : 480);
    }
    sprite_tint(p->tint);
    if (p->kind == 2) {
        int k = exe_s32(0x44b3a0);
        sprite_draw(p->x, p->y, p->bank, p->frame, p->scale_x * k / 256, p->scale_y * k / 256);
    } else {
        sprite_draw(p->x, p->y, p->bank, p->frame, p->scale_x, p->scale_y);
    }
    sprite_tint(0);
    sprite_clip(0, 0, 640, 480);
}

/* 0x40c830 */
static void draw_row(int row)
{
    for (int l = 0; l < LAYERS; l++) {
        int *b = buckets[row * LAYERS + l];
        for (int i = 1; i <= b[0]; i++) draw_rec(g_draw[b[i]]);
    }
}

void render_sorted(Framebuffer *target)
{
    fb = target;
    sprite_target(fb);
    if (!push_curve[2])
        for (int i = 0; i < 200; i++) push_curve[i] = i + i / 2;
    apply_push();
    for (int b = 0; b < ROWS * LAYERS; b++) buckets[b][0] = 0;
    int cam_x = g_lv.cam_x, cam_y = g_lv.cam_y;
    int row0 = cam_y / 24;
    for (int k = 0; k < g_ndraw; k++) {
        DrawRec *p = g_draw[k];
        int m = p->mode, row;
        if (m == 0 || m == 4) {
            row = (p->depth + p->h + 24 + cam_y) / 24 - row0;
            row = row < 0 ? 0 : row > 0x15 ? 0x15 : row;
        } else if (m > 1) {
            row = m == 2 ? 0x16 : 0;
        } else {
            int ux = p->x + cam_x, top = (p->y + cam_y) / 24, fy = (p->y + cam_y) % 24;
            int bottom = (p->depth + p->h + cam_y) / 24;
            int a = mask_row(ux & 31, fy, ux / 32, top, bottom, row0);
            int ux2 = p->w + p->x + cam_x;
            int c = mask_row(ux2 & 31, fy, ux2 / 32, top, bottom, row0);
            row = c < a ? c : a;
        }
        bucket_add(row, p->layer, k);
    }
    /* bubble sort by foot y inside each bucket (swaps the draw list entries) */
    for (int b = 0; b < ROWS * LAYERS; b++) {
        int n = buckets[b][0];
        for (int pass = 0; pass < n; pass++)
            for (int j = 1; j < n; j++) {
                DrawRec *a = g_draw[buckets[b][j]], *c = g_draw[buckets[b][j + 1]];
                if (a->sortable && c->sortable && c->depth + c->h < a->depth + a->h) {
                    g_draw[buckets[b][j]] = c;
                    g_draw[buckets[b][j + 1]] = a;
                }
            }
    }
    /* rows: sprites, then the row's foreground cells */
    int start = (cam_x + 256) / 32 - 8 + cam_y / 24 * MAP_STRIDE;
    int rows_px = cam_y >= g_lv.px_h - 480 ? 480 : 504, cols_px = cam_x >= g_lv.px_w - 640 ? 640 : 672;
    size_t cells = (size_t)g_lv.w * g_lv.h;
    int row = 0;
    for (int y = 0; y < rows_px; y += 24, row++) {
        draw_row(row);
        for (int x = 0, c = 0; x < cols_px; x += 32, c++) {
            int idx = start + row * MAP_STRIDE + c;
            if (idx < 0 || (size_t)idx >= cells) continue;
            uint16_t v = g_lv.map[idx];
            if (!v || g_hide_foreground) continue;
            uint16_t n = v & 0x3fff, slot = n ? g_lv.remap[n - 1] : 0;
            uint32_t t = g_lv.cells[idx] & 0xff;
            if (!t) continue;
            int sx = x - g_lv.fine_x, sy = y - g_lv.fine_y;
            if (t < 3) tile_overlay(g_lv.tile_px[slot], sx, sy, v >> 14 & 3);
            else tile_overlay_masked(g_lv.tile_px[slot], (const int8_t *)g_lv.tab2 + t * 0x300 - 0x300, sx, sy, v >> 14 & 3);
        }
    }
    draw_row(row);
    draw_row(row + 1);
    if (cam_y >= g_lv.px_h - 480) draw_row(row + 2);
}
