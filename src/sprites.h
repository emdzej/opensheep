/* Sprite banks (.SPR files, docs/formats.md) and the run-length sprite blitter. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

enum {
    SPR_BANKS = 400,               /* 0x4bc294 */
    SPR_BANK_LEVEL = 100,          /* + i: the level's banks (BLO table A), Level_LoadResources 0x401950 */
    SPR_BANK_TEXT_YELLOW = 0x128, SPR_BANK_TEXT_BLUE = 0x129, SPR_BANK_MISC = 0x12a,
    SPR_BANK_CPUI = 300,           /* + i: the CPUI_* banks */
    SPR_BANK_TINT = 399,           /* the tinted copy of the last sprite (0x4218c0) */
    SPR_MIRROR = (int)0x80000000u, /* frame flag: draw mirrored left-right */
    SPR_SHEAR = 0x40000000,        /* frame flag: the sheared (shadow) blitter 0x4197b0 */
    SPR_SCALE_1 = 0x100,           /* 8.8 scale */
};

typedef struct {
    int size, w, h, hot_x, hot_y;
    const uint8_t *rows;           /* run-length rows */
} SprFrame;

/* Load a bank from a game path (0x421c60) or from data\misc\ (0x421ba0). The previous one is freed. */
bool sprite_bank_load(int bank, const char *path);
bool sprite_bank_load_misc(int bank, const char *name);
void sprite_bank_free(int bank);
void sprite_banks_free_all(void);
/* Frames in a bank (0 if not loaded); frame info (false if out of range). */
int sprite_frames(int bank);
bool sprite_frame(int bank, int frame, SprFrame *out);
/* The frame table's u16 group and index of a frame. */
bool sprite_frame_ids(int bank, int frame, int *group, int *index);

/* The draw target and the clip window (0x66b5d0/0x66b5d4 and 0x4bc8f0 / 0x4bc8f4 / 0x44d4b0 / 0x44d4b4). */
void sprite_target(Framebuffer *fb);
void sprite_clip(int x0, int y0, int x1, int y1);
void sprite_get_clip(int *x0, int *y0, int *x1, int *y1);
/* Tint flags 0x4bc8f8: bit 0 halves red, bit 1 green, bit 2 blue. */
void sprite_tint(int flags);

/* Sprite_Draw 0x41f5e0: frame of bank with its top-left corner at (x, y); frame may carry SPR_MIRROR /
   SPR_SHEAR; sx, sy 8.8 scale. */
void sprite_draw(int x, int y, int bank, uint32_t frame, int sx, int sy);
