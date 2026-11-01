/* Level file loading and the tile map renderer. */
#include "level.h"
#include "exe.h"
#include "sprites.h"
#include "world.h"
#include "xp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Level g_lv;

static int32_t hdr32(const uint8_t *p, int off) { int32_t v; memcpy(&v, p + off, 4); return v; }

void level_release(void)
{
    /* Level_ReleaseTiles 0x4077b0 */
    for (int i = 0; i < MAX_TILES; i++) {
        surface_free(g_lv.tile_surf[i]);
        g_lv.tile_surf[i] = NULL;
    }
    free(g_lv.blo);
    free(g_lv.map_backup);
    g_lv.blo = NULL;
    g_lv.map_backup = NULL;
}

/* 0x407b60: has this map value been given a transformed copy already? */
static bool seen_before(const uint16_t *seen, int n, uint16_t v)
{
    for (int i = 0; i < n; i++)
        if (seen[i] == v) return true;
    return false;
}

/* Tile number n (1-based) -> slot. The renderer reads the remap table one entry before 0x4ae444 for
   n = 0; the loader never asks for it (empty cells are skipped first). */
static uint16_t slot_of(uint16_t cell) { uint16_t n = cell & 0x3fff; return n ? g_lv.remap[n - 1] : 0; }

/* Level_InitTileAnims 0x407830: one surface per tile, then one per distinct transformed map value
   (bits 14-15: 1 mirror, 2 flip, 3 both), appended after the file's tiles; the map cell is rewritten to
   point at the copy, keeping its transform bits. */
static void init_tile_anims(void)
{
    if (!g_lv.map || !g_lv.ntiles) return;
    int cells = g_lv.w * g_lv.h, nx = 0;
    for (int i = 0; i < cells; i++)
        if (g_lv.map[i] >> 14) nx++;
    for (int i = 0; i < g_lv.ntiles; i++) {
        uint16_t s = g_lv.remap[i];
        if (g_lv.tile_px[s] && !g_lv.tile_surf[s])
            g_lv.tile_surf[s] = surface_from_pixels(g_lv.tile_px[s], TILE_W, TILE_H, XFORM_NONE);
    }
    uint16_t *seen = calloc((size_t)nx + 1, sizeof *seen);
    if (!seen) return;
    int added = 0;
    for (int i = 0; i < cells; i++) {
        uint16_t v = g_lv.map[i], xf = v >> 14;
        if (!xf) continue;
        uint16_t s = slot_of(v);
        if (!seen_before(seen, added, v)) {
            int slot = g_lv.ntiles + added;
            if (!g_lv.tile_px[s] || slot >= MAX_TILES) continue;   /* the original would overrun at 5000 */
            g_lv.tile_px[slot] = g_lv.tile_px[s];
            g_lv.tile_surf[slot] = surface_from_pixels(g_lv.tile_px[s], TILE_W, TILE_H, xf);
            seen[added] = v;
            g_lv.map[i] = (uint16_t)(xf << 14 | (slot + 1));
            g_lv.remap[slot] = (uint16_t)slot;
            added++;
        } else {
            for (int k = 0; k < nx; k++)                   /* searches the whole list, as the original */
                if (seen[k] == v) { g_lv.map[i] = (uint16_t)(xf << 14 | (g_lv.ntiles + 1 + k)); break; }
        }
    }
    free(seen);
    g_lv.ntiles += added;
}

bool level_load_blo(const char *path)
{
    /* Level_LoadBLO 0x4072d0 */
    level_release();
    size_t n = 0;
    uint8_t *f = file_load(path, &n);
    if (!f) { fprintf(stderr, "Cannot find BLO file %s\n", path); return false; }
    if (n < 0x38 || f[0] != 'P' || f[1] != 'C' || (int8_t)f[2] < 13) {
        fprintf(stderr, "This level is too old: %s\n", path);
        free(f);
        return false;
    }
    int w = hdr32(f, 4), h = hdr32(f, 8), ntiles = hdr32(f, 0x10), na = hdr32(f, 0x14), nb = hdr32(f, 0x18),
        nc = hdr32(f, 0x1c), nstr = hdr32(f, 0x24);
    size_t off_anim = 0x38 + (size_t)nstr, off_map = off_anim + (size_t)nc * 32 + (size_t)nb * 20 + (size_t)na * 260;
    size_t cells = (size_t)w * h;
    size_t need = off_map + cells * 2 + (size_t)ntiles * TILE_W * TILE_H * 2 + cells * (2 + 4 + 4) + 0x39000;
    if (w <= 0 || h <= 0 || ntiles < 0 || ntiles > MAX_TILES || need > n) {
        fprintf(stderr, "%s: bad level file\n", path);
        free(f);
        return false;
    }
    /* Everything from the map on is u16/u32 data at 4-byte multiples from the map: place the file so the
       map is 4-aligned (the original reads it unaligned in place). */
    size_t shift = (4 - off_map % 4) % 4;
    uint8_t *buf = malloc(n + shift);
    if (!buf) { free(f); return false; }
    memcpy(buf + shift, f, n);
    free(f);
    g_lv.blo = buf;
    uint8_t *b = buf + shift;
    g_lv.blo_size = n;
    g_lv.w = w;
    g_lv.h = h;
    g_lv.ntiles = ntiles;
    g_lv.cam_min_x = hdr32(b, 0x28);
    g_lv.cam_max_x = hdr32(b, 0x2c);
    g_lv.cam_min_y = hdr32(b, 0x30);
    g_lv.cam_max_y = hdr32(b, 0x34);
    g_lv.px_w = w << 5;
    g_lv.px_h = h * TILE_H;
    g_lv.objects = b + off_anim + (size_t)nc * 32;
    g_lv.nobjects = nb;
    g_lv.banks = b + off_anim + (size_t)nc * 32 + (size_t)nb * 20;
    g_lv.nbanks = na;
    g_lv.anim_frames = b + off_anim;
    g_lv.nanim_frames = nc;
    g_lv.map = (uint16_t *)(b + off_map);
    g_lv.tiles = g_lv.map + cells;
    g_lv.layer1 = g_lv.tiles + (size_t)ntiles * TILE_W * TILE_H;
    g_lv.cells = (uint32_t *)(g_lv.layer1 + cells);
    g_lv.layer3 = g_lv.cells + cells;
    g_lv.tab1 = (uint8_t *)(g_lv.layer3 + cells);
    g_lv.tab2 = g_lv.tab1 + 0x30000;
    for (int i = 0; i < MAX_TILES; i++) {
        g_lv.remap[i] = (uint16_t)i;
        g_lv.tile_px[i] = NULL;
        g_lv.tile_surf[i] = NULL;
    }
    for (int i = 0; i < ntiles; i++) g_lv.tile_px[i] = g_lv.tiles + (size_t)i * TILE_W * TILE_H;
    /* The original re-packs every tile pixel from rgb565 into the primary surface's format; in the
       640x480x16 rgb565 mode it runs in, that is the identity. */
    g_lv.map_backup = malloc(cells * 2);
    if (g_lv.map_backup) memcpy(g_lv.map_backup, g_lv.map, cells * 2);
    init_tile_anims();
    /* the rest of the load, 0x401950, 0x4016a0 and 0x4056f0: level_load_resources (play.c) */
    return true;
}

void level_set_camera(int x, int y)
{
    /* tail of Camera_Update 0x40cb40 */
    if (x < g_lv.cam_min_x) x = g_lv.cam_min_x;
    if (y < g_lv.cam_min_y) y = g_lv.cam_min_y;
    if (x > g_lv.cam_max_x - 640) x = g_lv.cam_max_x - 640;
    if (y > g_lv.cam_max_y - 480) y = g_lv.cam_max_y - 480;
    g_lv.cam_x = x;
    g_lv.cam_y = y;
    g_lv.fine_x = x & 0x1f;
    g_lv.fine_y = y % TILE_H;
}

void level_draw_map(Framebuffer *fb)
{
    /* 0x407ba0. With a fine scroll a 21st column / row is drawn and the outer ones are clipped by a
       source rectangle; the rest are blitted whole. Cells of type 1 or 2 and empty cells are skipped. */
    if (!g_lv.map) return;
    int fx = g_lv.fine_x, fy = g_lv.fine_y;
    int start = g_lv.cam_y / TILE_H * MAP_STRIDE + (g_lv.cam_x >> 5);
    int rows = VIEW_ROWS + (fy != 0), cols = VIEW_COLS + (fx != 0);
    size_t cells = (size_t)g_lv.w * g_lv.h;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = start + r * MAP_STRIDE + c;
            if (idx < 0 || (size_t)idx >= cells) continue;  /* the original reads past the map here */
            uint32_t type = g_lv.cells[idx] & 0xff;
            uint16_t v = g_lv.map[idx];
            if ((type == 1 || type == 2) || !v) continue;
            Surface *s = g_lv.tile_surf[slot_of(v)];
            if (!s) continue;
            int x = c * TILE_W - fx, y = r * TILE_H - fy;
            bool clip_x = cols == VIEW_COLS + 1 && (c == 0 || c == VIEW_COLS);
            bool clip_y = rows == VIEW_ROWS + 1 && (r == 0 || r == VIEW_ROWS);
            if (!clip_x && !clip_y) { surface_blt_fast(fb, x, y, s, NULL); continue; }
            Rect src = { 0, 0, TILE_W, TILE_H };
            if (clip_x) {
                if (c == 0) { src.left = fx; x = 0; }
                else src.right = fx;
            }
            if (clip_y) {
                if (r == 0) { src.top = fy; y = 0; }
                else src.bottom = fy;
            }
            surface_blt_fast(fb, x, y, s, &src);
        }
    }
}

int level_load_sprite_banks(void)
{
    /* Level_LoadResources 0x401950: table A holds the artists' paths of the level's sprite strips
       ("U:\PROC\World01\Sprites\CFlower_A080000.tga"). The drive folder is overwritten with "DATA"
       in place, a frame number (5 digits before the extension) is cut, and ".SPR" appended: bank 100 + i
       = "DATA\World01\Sprites\CFlower_A0.SPR". */
    int loaded = 0;
    for (int i = 0; i < g_lv.nbanks && SPR_BANK_LEVEL + i < SPR_BANKS; i++) {
        char *e = (char *)g_lv.banks + 256 * i;
        memcpy(e + 3, "DATA", 4);
        char path[260];
        size_t n = strnlen(e + 3, 252);
        memcpy(path, e + 3, n);
        path[n] = 0;
        char *end = path + n, *cut = end - 9;
        bool digits = n >= 9;
        for (int k = 0; digits && k < 5; k++)
            if (cut[k] < '0' || cut[k] > '9') digits = false;
        if (!digits) cut = n >= 4 ? end - 4 : end;
        strcpy(cut, ".SPR");
        if (sprite_bank_load(SPR_BANK_LEVEL + i, path)) loaded++;
    }
    return loaded;
}

/* ------------------------------------------------------------------ tile animations */

TileAnim g_tile_anim[MAX_TILE_ANIMS];
int16_t g_ntile_anims;
static int16_t *anim_buf;           /* 0x49c074: per anim a byte offset, then {cells, frames, 2, targets, frames} */
static size_t anim_buf_n;

/* A cell of the map at (x, y) with the renderer's 128-cell rows, as a tile index (number - 1). */
static int16_t map_tile(int x, int y)
{
    size_t i = (size_t)y * MAP_STRIDE + (size_t)x;
    const uint16_t *end = (const uint16_t *)(g_lv.blo + g_lv.blo_size);
    if (x < 0 || y < 0 || g_lv.map + i >= end) return -1;
    return (int16_t)((g_lv.map[i] & 0x3fff) - 1);
}

static void build_frames(uint32_t ext)
{
    /* 0x4035a0: ext holds per anim {x, y (px), w, h (cells), dx, dy (cells per frame), frames} */
    int16_t *buf = calloc(0x8000, 2), *p = buf + g_ntile_anims, *lim = buf + 0x8000;
    for (int k = 0; k < g_ntile_anims; k++, ext += 14) {
        int16_t x = (int16_t)exe_u16(ext), y = (int16_t)exe_u16(ext + 2), w = (int16_t)exe_u16(ext + 4),
                h = (int16_t)exe_u16(ext + 6), dx = (int16_t)exe_u16(ext + 8), dy = (int16_t)exe_u16(ext + 10),
                frames = (int16_t)exe_u16(ext + 12);
        buf[k] = (int16_t)((p - buf) * 2);
        if (p + 3 > lim) break;
        p[0] = (int16_t)(h * w);
        p[1] = frames;
        p[2] = 2;
        p += 3;
        int cy = y / TILE_H, cx = x / 32;
        for (int f = -1; f < frames; f++) {         /* the targets, then each frame */
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w && p < lim; i++) *p++ = map_tile(cx + i, cy + j);
            if (f >= 0) { cx += dx; cy += dy; }
        }
    }
    anim_buf_n = (size_t)(p - buf);
    anim_buf = realloc(buf, anim_buf_n * 2 + 2);
}

void tile_anims_load(void)
{
    uint32_t t = desc_u32(DESC_TILE_ANIMS);
    free(anim_buf);
    anim_buf = NULL;
    anim_buf_n = 0;
    g_ntile_anims = 0;
    if (!t) return;
    int16_t v = (int16_t)exe_u16(t);
    while (v != -1 && v != 0x800 && g_ntile_anims < MAX_TILE_ANIMS) {
        TileAnim *a = &g_tile_anim[g_ntile_anims++];
        a->mode = (int16_t)exe_u16(t + 4);
        a->anim = v;
        a->frame = 0;
        a->pause = a->countdown = (int16_t)exe_u16(t + 2);
        t += 6;
        v = (int16_t)exe_u16(t);
    }
    if (v == 0x800) {
        build_frames(t + 6);
        return;
    }
    g_ntile_anims = 0;              /* the original keeps a 256-byte buffer it never fills */
}

void tile_anims_tick(void)
{
    for (int k = 0; k < g_ntile_anims; k++) {
        TileAnim *a = &g_tile_anim[k];
        if (!a->mode || --a->countdown >= 0) continue;
        if (!anim_buf || a->anim < 0 || a->anim >= g_ntile_anims) continue;
        const int16_t *h = anim_buf + anim_buf[a->anim] / 2;
        a->countdown = a->pause;
        if (a->frame + 1 < h[1]) {
            a->frame++;
        } else if (a->mode == 2) {
            a->frame = a->hold;
            a->mode = 0;
        } else if (h[2] == 1) {
            a->mode = 0;
        } else if (h[2] == 2) {
            a->frame = 0;
        }
        int n = h[0];
        const int16_t *src = h + 3 + (a->frame + 1) * n;
        if (src + n > anim_buf + anim_buf_n) continue;
        for (int i = 0; i < n; i++)
            if (h[3 + i] >= 0 && h[3 + i] < MAX_TILES) g_lv.remap[h[3 + i]] = (uint16_t)src[i];
    }
}
