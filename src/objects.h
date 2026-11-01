/* Level objects: static props (one frame) and animated objects, their .ANM animations, the draw
   records they hand to the depth-sorted renderer (render.h), and the per-tick animation update.
   Layouts follow the original arrays byte for byte (the move routines index into them). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* One sprite to draw, 0x58 bytes; embedded in every object, the player and the sheep. */
typedef struct {
    int32_t x, y;               /* +0x00 screen position of the sprite's top-left corner */
    int32_t wx, wy;             /* +0x08 screen position of the object (anims) */
    int32_t w, h;               /* +0x10 frame size */
    int32_t scale_x, scale_y;   /* +0x18 8.8 */
    int32_t shadow_dy;          /* +0x20 kind 7: y offset */
    int32_t kind;               /* +0x24 1-5 sprite (2: scaled by 0x44b3a0), 6 text, 7 shadow */
    int32_t hot_x, hot_y;       /* +0x28 */
    int32_t depth;              /* +0x30 screen y used for sorting */
    int32_t bank;               /* +0x34 */
    uint32_t frame;             /* +0x38 with SPR_MIRROR */
    int32_t flip;               /* +0x3c mirrored by direction */
    int32_t clip_x0, clip_x1, clip_y0, clip_y1;   /* +0x40 world px; clip_x0 = -1: none */
    uint8_t sortable;           /* +0x50 sorted by foot y within its row */
    int8_t mode;                /* +0x51 row choice: 0/4 foot y, 1 mask, 2 top, 3 bottom */
    int8_t layer;               /* +0x52 0..3 */
    int8_t push;                /* +0x53 pushes overlapping sprites behind (0x40c070) */
    int8_t tint;                /* +0x54 Sprite_Tint flags */
    int8_t shadow;              /* +0x55 -1: no shadow */
    uint8_t pad[2];
} DrawRec;

typedef struct {                /* 0x47e070, 0x78 bytes */
    int32_t state;              /* -1 free, 2 shown */
    int32_t id;                 /* object id (B table + 40) */
    int32_t x, y;
    int32_t mirror;
    int32_t anim, dir;          /* level .ANM animation, direction 0..15 */
    int32_t depth_off;
    DrawRec rec;
} StaticObj;

/* 0x459078, 0x10c bytes. Slots 0-29 are the sheep, 31 the player, 40+ level objects. The move
   routines, the player and the sheep use dwords 0x15-0x2b as scratch whose meaning depends on the
   routine; they reach them through d[] with the original dword index. */
typedef union {
    struct {
        int32_t slot;
        int32_t type;           /* -1 free; .ANM file: 0 player, 1 sheep, 2/3 level, 4 common */
        int32_t depth_off;
        int32_t state;          /* 1 animating, 2 stopped */
        int32_t id;
        int32_t x, y;           /* +0x14 */
        float fx, fy;           /* +0x1c */
        float speed;            /* +0x24 */
        int32_t mirror;         /* +0x28 */
        int32_t move;           /* +0x2c move routine: n standard (0x44c57c), n | 0x8000 level-specific */
        int32_t sub;            /* +0x30 the routine's sub-state */
        int32_t anim;           /* +0x34 */
        int32_t frame, prev_frame;  /* +0x38 */
        int32_t nframes;        /* +0x40 */
        int32_t pause;          /* +0x44 ticks per frame */
        int32_t dir;            /* +0x48 */
        int32_t countdown;      /* +0x4c */
        int32_t loop;           /* +0x50 at the end: 0 stop, 1 remove, 2 restart */
        int32_t param[8];       /* +0x54 from the .POS file */
        int32_t u74[10];
        int32_t sound_on;       /* +0x9c */
        int32_t ua0[4];
        DrawRec rec;            /* +0xb0 */
        int32_t sound;          /* +0x108 */
    };
    int32_t d[0x43];
} AnimObj;

enum {
    MAX_STATIC = 1024, MAX_ANIM = 512, MAX_OBJ_ID = 2000,
    FIRST_OBJ = 40,             /* slots below are reserved (player, sheep helpers, HUD) */
    ANM_PLAYER = 0, ANM_SHEEP = 1, ANM_COMMON = 2, ANM_LEVEL = 3,
};

extern StaticObj g_static[MAX_STATIC];
extern AnimObj *const g_anim;            /* MAX_ANIM records; g_anim[-1] is a spare (objects.c) */
extern int g_static_hi, g_anim_hi;       /* 0x49c07c, 0x49f894: highest used slot + 1 */
extern int g_obj_by_id[MAX_OBJ_ID];      /* 0x47a878: slot, | 0x8000 for a static object */
extern int g_loading;                    /* 0x4a10c0 */

/* The four .ANM files (0x49f880..0x49f88c; part of Level_LoadAnims 0x401240). */
void anims_load(const char *level_anm_path);
void anims_free(void);

/* 0x402a90: objects from the level's B table, then the .POS file. */
void objects_load(const char *pos_path);
/* The .POS records still loaded (0x47db5c): their number and record k's object id. */
int pos_count(void);
int pos_id(int k);
int static_new(int x, int y, int anim, int dir);              /* 0x402220 */
int anim_new(int x, int y, int anim, int dir, int type);      /* 0x402330 */
void anim_set_frame(AnimObj *a);                              /* 0x401e90 */
void anim_kill(AnimObj *a);                                   /* 0x4025c0 */
void anim_init(int type, int slot, int anim, int dir);        /* 0x402490: 3 shepherd, 2 sheep */
void anim_set(int slot, int anim, int dir);                   /* 0x402560: dir -1 keeps it */
void anim_step(int slot, int dir, int step);                  /* 0x402630 */
void rec_set_sprite(DrawRec *r, int bank, uint32_t frame);    /* 0x421b10 */
/* 0x402700: one tick of animation and move routines. */
void objects_update(void);
/* 0x402830: push the visible objects' draw records. */
void objects_collect(void);
