/* Shared simulation state: the shepherd, the flock, the sheep attractors, the level's special cells,
   the direction tables and the tuning read from data\misc\*.txt. Layouts follow the original arrays
   (the code indexes them by dword as often as by name), so each struct is a union with a dword view
   d[] whose indices match the original's. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "objects.h"

/* ---------------------------------------------------------------- x87 arithmetic
   The exe runs the FPU at its default 53-bit precision (no Direct3D), so float expressions are
   evaluated in double and rounded to float only when stored; __ftol (0x43ca04) truncates through a
   64-bit fistp and keeps the low 32 bits. */
static inline int32_t ftol(double v)
{
    if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) return 0;   /* 0x8000000000000000 */
    return (int32_t)(uint32_t)(uint64_t)(int64_t)v;
}

/* ---------------------------------------------------------------- tables (0x401420, 0x40b070) */
extern float g_sin[256], g_cos[256];        /* 0x47d75c, 0x4a10c4: direction 0 = up, clockwise */
/* g_sin[t / 2] for a jump's height (t = 256 * time left / duration). On the landing tick t is below
   zero and the original reads before the table: the end of the draw list (0x47c7bc, entries 996..999),
   null or a pointer, 0 as a float to within 1e-38. */
static inline float arc_sin(int t) { return t / 2 < 0 ? 0.0f : g_sin[t / 2]; }
extern int32_t g_isin[256], g_icos[256];    /* 0x47db68, 0x4a14c4: x 32768 */
extern int32_t g_icos_y[256];               /* 0x49f9ac: g_icos * 24 / 32 */
extern float g_cos_y[256];                  /* 0x49fdb0: cos * 24 * 0.03125 */
extern int32_t g_deflect[256 * 16];         /* 0x4adec8: [dir * 16 + wall] -> turn step, 0 = blocked */
void game_init_tables(void);                /* 0x401420 */
void game_init_deflect(void);               /* 0x40b070 */
/* 0x403a00: direction (0..255) of the vector (dx, dy), screen y down. */
int dir_of(int dx, int dy);
/* 0x40cae0: turn dir toward target by at most step. */
int dir_turn(int dir, int target, int step);

/* ---------------------------------------------------------------- tuning */
typedef struct {                            /* 0x44c650.., Data\misc\<player>.txt (0x4180e0) */
    int32_t creep_speed, walk_speed, run_speed;     /* x10 */
    int32_t creep_radius, walk_radius, run_radius;
} PlayerVars;
typedef struct {                            /* 0x44cbf8.., Data\misc\<breed>.txt (0x41e960) */
    int32_t neighbour_dist, too_close_dist, turn_speed, sheep_accel, man_ahead_dist;
    int32_t bias_center, bias_direction, bias_closest, bias_random, bias_player;
    int32_t amble_speed, wander_radius, num_neighbours, sheep_speed;
} FlockVars;
extern PlayerVars g_pvars;
extern FlockVars g_fvars;                   /* the file's values ... */
extern FlockVars g_flock;                   /* 0x4bbd10: ... copied for the level */
void player_vars_load(int character);       /* 0x4180e0 */
void flock_vars_load(int breed);            /* 0x41e960: the stored copy (flock_vars_save) first */
void flock_vars_save(int breed);            /* 0x41e850: g_fvars as the breed's file, to the host's storage */

/* ---------------------------------------------------------------- attractors (0x4a04c0) */
typedef struct {
    int16_t type;       /* 0 free; 1 repel, 2 the shepherd, 3 knock back; 4+ level triggers */
    int16_t u2;
    int16_t x, y;
    int16_t power;      /* +8 */
    int16_t range;      /* +0xa: Manhattan distance */
} Attractor;
enum { MAX_ATTRACT = 256 };
extern Attractor g_attract[MAX_ATTRACT];
extern int16_t g_nattract;                  /* 0x47db64: highest used + 1 */
int attract_new(int x, int y, int type, int range, int power);   /* 0x403970 */

/* ---------------------------------------------------------------- special cells (0x4a28d0, .SPC) */
typedef struct {
    int8_t type;
    uint8_t active;     /* +1: busy (a routine is running) */
    int16_t a[7];       /* +2.. meaning per type */
    uint8_t count;      /* +0x10 */
    uint8_t u11;
} Special;
enum { MAX_SPECIAL = 256 };
extern Special g_special[MAX_SPECIAL];
void specials_load(void);                   /* part of 0x401950 */
/* A bank table (8-byte entries {flags, name}) into banks first.. (0x401050 per entry). */
void bank_table_load(uint32_t table, int first);
/* One bank table entry into a bank (0x401050, then Sprite_LoadBank). */
void bank_entry_load(uint32_t entry, int bank);
void actor_banks_load(void);                /* 0x4014c0, 0x401560: the shepherd's and the flock's banks */
void player_banks_load(void);               /* 0x4014c0 */
void sheep_banks_load(void);                /* 0x401560 */

/* ---------------------------------------------------------------- the shepherd (0x4b9788) */
typedef union {
    struct {
        int32_t dir;            /* 0x00 0..255 */
        float speed;            /* 0x04 */
        int32_t face;           /* 0x08 direction shown */
        int32_t state;          /* 0x0c move type (0 walk); >= 100 the level's own (descriptor +0x18) */
        int32_t u10;
        int32_t kind;           /* 0x14 animation set (0x44c688): 0 normal, 0x10, 0x13 */
        int32_t u18;
        int32_t timer;          /* 0x1c */
        int32_t zone;           /* 0x20 zone rectangle (descriptor +8) stood in, -1 none */
        int32_t slot;           /* 0x24 animated object */
        float x, y;             /* 0x28 */
        int32_t ix, iy;         /* 0x30 */
        int32_t anim[3];        /* 0x38 stand, slow, fast */
        int32_t hit;            /* 0x44 collision: 2 blocked, 0 slope, -1 none */
        int32_t push_dir;       /* 0x48 0..15 */
        int32_t idle;           /* 0x4c ticks to the idle animation */
        int32_t kind_time;      /* 0x50 */
        int32_t pushed;         /* 0x54 */
        int32_t u58, u5c, u60;
        int32_t flags;          /* 0x64 bit 1: not counted in the draw list */
        int32_t cooldown;       /* 0x68 */
        int32_t effect;         /* 0x6c power-up (negative: pending) */
        int32_t effect_time;    /* 0x70 */
        int32_t in_dx, in_dy, in_mag;   /* 0x74 input vector and its length */
        float walk, run, creep; /* 0x80 */
        int32_t attract;        /* 0x8c the shepherd's repeller */
        int32_t attract2;       /* 0x90 */
    };
    int32_t d[0x25];
} Player;
extern Player g_player;

/* ---------------------------------------------------------------- the flock (0x4b99c0) */
enum { MAX_SHEEP = 30, PLAYER_SLOT = 31 };
typedef union {
    struct {
        int32_t dir;            /* 0x00 */
        float speed;            /* 0x04 */
        int32_t face;           /* 0x08 */
        int32_t state;          /* 0x0c */
        int32_t alive;          /* 0x10 0 in play, 1 leaving, 2 gone */
        int32_t kind;           /* 0x14 animation set (0x44ccc0) */
        int32_t u18;
        int32_t timer;          /* 0x1c */
        int32_t zone;           /* 0x20 */
        int32_t slot;           /* 0x24 */
        float x, y;             /* 0x28 */
        int32_t ix, iy;         /* 0x30 */
        int32_t anim[3];        /* 0x38 */
        int32_t hit;            /* 0x44 */
        int32_t push_dir;       /* 0x48 */
        int32_t idle;           /* 0x4c */
        int32_t u50;
        int32_t pushed;         /* 0x54 */
    };
    int32_t d[0x49];
} Sheep;
extern Sheep g_sheep[MAX_SHEEP];
extern int g_nsheep;                        /* 0x4bbcf8 */

/* 0x411db0: a sheep by number, or the shepherd for 30 and up. */
static inline int32_t *actor(int n);

/* ---------------------------------------------------------------- game globals */
extern int g_input_mode;        /* 0x44b178: 0 mouse, 1 keyboard, 2 analogue pad, 3 digital pad */
extern int g_demo;              /* 0x4b0f9c: 0 off, 1 recording, 2 playing, 3 ended, 4 full */
extern int g_run, g_creep;      /* 0x4ae0d8, 0x4ae0e4: buttons held */
extern int g_action;            /* 0x4ae0f0 */
extern int g_mouse_dx, g_mouse_dy;   /* 0x4ae0a0, 0x4ae0a4 */
extern int g_saved;             /* 0x4ae084: sheep penned */
extern int g_needed;            /* 0x4a1ea0: sheep to pen (5 / 10 / 20 by difficulty) */
extern int g_end;               /* 0x4a4100: level end reason (1 won, 6 demo over, 7 bonus) */
extern int g_on_ice;            /* 0x4b9828 */
extern int g_breed;             /* 0x4a19e0: 0 Pastoral, 1 Factoral, 2 Longwool, 3 Neogenetic */

/* 0x4a1e80: the game in progress, 0x20 bytes, copied whole to and from the save slots (+0x24 of a
   Game%02d.sav) and kept at 0x4a2070 while a level runs. */
typedef struct {
    int32_t score;              /* +0x00 0x4a1e80 */
    int32_t u84;                /* +0x04 0x4a1e84 */
    int8_t u88;                 /* +0x08 0x4a1e88 */
    int8_t world;               /* +0x09 0x4a1e89: the world being played */
    uint8_t levels;             /* +0x0a 0x4a1e8a: its levels done (0x44b198 bits) */
    uint8_t breeds;             /* +0x0b 0x4a1e8b: breeds played (0x44b198 bits) */
    int8_t flock[4];            /* +0x0c 0x4a1e8c: sheep per breed, carried between levels */
    uint8_t bonus;              /* +0x10 0x4a1e90: bonus levels' flags (0x44b194 per level) */
    int8_t chr;                 /* +0x11 0x4a1e91: 0 Bo Peep, 1 Adam, 2 Motley, 3 Shep */
    int8_t breed;               /* +0x12 0x4a1e92 */
    int8_t difficulty;          /* +0x13 0x4a1e93: 0 easy .. 2 hard */
    uint8_t worlds[12];         /* +0x14 0x4a1e94: per world, the levels won */
} Progress;
extern Progress g_prog;
#define g_char (g_prog.chr)
#define g_theme (g_prog.world)
#define g_difficulty (g_prog.difficulty)
#define g_flock_size (g_prog.flock)
#define g_bonus (g_prog.bonus)
extern int g_effect_count;      /* 0x4adebc */
extern int g_scroll_reset;      /* 0x4b0f30 */
extern int g_level_over;        /* 0x4b0f2c: the end-of-level sequence has started */

static inline int32_t *actor(int n)
{
    return n < MAX_SHEEP ? g_sheep[n].d : g_player.d;
}
