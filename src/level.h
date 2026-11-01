/* The loaded level: the .BLO file (docs/formats.md), its tile surfaces and the scrolling camera.
   Fields name the original globals they stand for. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "platform.h"
#include "surface.h"

enum {
    TILE_W = 32, TILE_H = 24,
    MAP_STRIDE = 0x80,              /* the renderer steps rows by 128 cells, whatever the header says */
    MAX_TILES = 5000,               /* 0x4a4220 / 0x4a9088 / 0x4ae444 hold 5000 entries */
    VIEW_COLS = 20, VIEW_ROWS = 20, /* 640 / 32, 480 / 24 */
};

typedef struct {
    uint8_t *blo;                   /* 0x4a20a4: the whole file, patched in place */
    size_t blo_size;
    int w, h;                       /* 0x4a1ebc, 0x4a1ec0: cells */
    int ntiles;                     /* 0x4a1edc: tiles in the file, then + transformed copies */
    int cam_min_x, cam_max_x;       /* 0x4a1eec, 0x4a1ec8: header +0x28 / +0x2c, world px */
    int cam_min_y, cam_max_y;       /* 0x4a1eb8, 0x4a1ed4: header +0x30 / +0x34 */
    int px_w, px_h;                 /* 0x4ae040, 0x4a9080 */
    uint16_t *map;                  /* 0x4a1ee0: w * h cells, bits 14-15 transform, 0-13 tile number (1-based) */
    uint16_t *map_backup;           /* 0x4a3ad0: "Level Backup" */
    uint16_t *tiles;                /* 0x4a1ecc: ntiles x 32x24 rgb565 */
    uint16_t *layer1;               /* 0x4a1ea4: w * h u16 */
    uint32_t *cells;                /* 0x4a20a0: w * h u32, low byte = cell type */
    uint32_t *layer3;               /* 0x4a1ea8: w * h u32 */
    uint8_t *tab1, *tab2;           /* 0x4a1ed8 (0x18000 + 0x4800), 0x4a2094 */
    uint8_t *objects;               /* 0x4a2098: table B, 20-byte object placements */
    int nobjects;                   /* 0x4a20ac */
    uint8_t *banks;                 /* 0x4a1ee4: table A, 256-byte sprite strip paths */
    int nbanks;                     /* 0x4a20c0 */
    uint8_t *anim_frames;           /* 0x4a209c entries of 32 bytes (DAT_004a209c = count at +0x1c) */
    int nanim_frames;
    uint16_t remap[MAX_TILES];      /* 0x4ae444: tile number - 1 -> tile slot (identity after load) */
    const uint16_t *tile_px[MAX_TILES];   /* 0x4a9088 */
    Surface *tile_surf[MAX_TILES];  /* 0x4a4220 */
    /* camera (Camera_Update 0x40cb40) */
    int cam_x, cam_y;               /* 0x4adeb0, 0x4adeb4: world px of the top-left corner */
    int fine_x, fine_y;             /* 0x4a3d58, 0x4a3d5c */
} Level;

extern Level g_lv;

/* Level_LoadBLO 0x4072d0 + Level_InitTileAnims 0x407830. path is a game path ("Data\World01\..."). */
bool level_load_blo(const char *path);
/* The level's sprite banks 100 + i from table A (part of Level_LoadResources 0x401950). Returns how
   many loaded. */
int level_load_sprite_banks(void);
/* Level_ReleaseTiles 0x4077b0 plus freeing the file. */
void level_release(void);
/* Clamp the camera to the level's bounds and derive the fine scroll (tail of Camera_Update 0x40cb40). */
void level_set_camera(int x, int y);
/* Map renderer 0x407ba0: the visible 20x20 (21x21 when scrolled) cells into fb. */
void level_draw_map(Framebuffer *fb);

/* ---------------------------------------------------------------- tile animations (descriptor +0x2c) */
typedef struct {                    /* 0x4a01b0, 12 bytes */
    int16_t mode;                   /* 0 stopped, 1 looping, 2 once, then frame `hold` and stop */
    int16_t anim;                   /* entry in the frame buffer */
    int16_t frame;
    int16_t pause;
    int16_t hold;                   /* +8 */
    int16_t countdown;              /* +0xa */
} TileAnim;
enum { MAX_TILE_ANIMS = 64 };
extern TileAnim g_tile_anim[MAX_TILE_ANIMS];
extern int16_t g_ntile_anims;       /* 0x49fdac */
/* 0x4037c0 with 0x4035a0: the level's tile animations and their frames, cut from the map. */
void tile_anims_load(void);
/* 0x403890: one tick; a frame swaps the remap entries of the animated tiles. */
void tile_anims_tick(void);
