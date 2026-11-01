#include "game.h"
#include "exe.h"
#include "sprites.h"
#include "world.h"
#include "xp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(Player) == 0x94, "Player layout");
_Static_assert(sizeof(Sheep) == 0x124, "Sheep layout");
_Static_assert(sizeof(Attractor) == 12, "Attractor layout");
_Static_assert(sizeof(Special) == 0x12, "Special layout");

float g_sin[256], g_cos[256], g_cos_y[256];
int32_t g_isin[256], g_icos[256], g_icos_y[256];
int32_t g_deflect[256 * 16];
PlayerVars g_pvars;
FlockVars g_fvars, g_flock;
Attractor g_attract[MAX_ATTRACT];
int16_t g_nattract;
Special g_special[MAX_SPECIAL];
Player g_player;
Sheep g_sheep[MAX_SHEEP];
int g_nsheep;

int g_input_mode, g_demo, g_run, g_creep, g_action, g_mouse_dx, g_mouse_dy;
int g_saved, g_needed, g_end, g_on_ice, g_breed, g_effect_count, g_scroll_reset, g_level_over;
Progress g_prog;                 /* 0x4a1e80 */

/* The tuning variables are initialised data in the exe; the files overwrite them, and a value a file
   leaves out keeps whatever the previous level set. */
static void vars_defaults(void)
{
    exe_read(&g_pvars, 0x44c650, sizeof g_pvars);
    exe_read(&g_fvars, 0x44cbf8, sizeof g_fvars);
}
EXE_LOADER(vars_defaults)

void game_init_tables(void)
{
    /* 0x401420 */
    for (int i = 0; i < 256; i++) {
        double a = (double)i * 6.283185308 * 0.00390625;
        double s = sin(a), c = cos(a);
        g_isin[i] = ftol(s * 32768.0);
        g_sin[i] = (float)s;
        g_icos[i] = ftol(c * 32768.0);
        g_cos[i] = (float)c;
        g_icos_y[i] = g_icos[i] * 24 / 32;
        g_cos_y[i] = (float)(c * 24.0 * 0.03125);     /* from the unrounded cosine */
    }
}

void game_init_deflect(void)
{
    /* 0x40b070: for each heading and wall direction (0x44b358: w * 16), how far to turn to slide
       along the wall (4..8, signed), 0 when the heading is more than a quarter off the wall. */
    int32_t *p = g_deflect;
    for (int dir = 0; dir < 256; dir++) {
        for (int w = 0; w < 16; w++, p++) {
            int c = w * 16, lo = c - 0x7f, hi = c + 0x7f, d = dir;
            if (lo < 0) {
                d = (dir - lo) & 0xff;
                c -= lo;
                hi -= lo;
                lo = 0;
            } else if (hi > 0x100) {
                int s = c - 0x80;
                c -= s;
                lo -= s;
                d = (dir - s) & 0xff;
                hi = 0xff;
            }
            if (d < lo || hi < d) *p = 0;
            else if (c < d) { int t = 8 - (d - c) / 8; *p = t < 4 ? 4 : t; }
            else { int t = 8 - (c - d) / 8; *p = -(t < 4 ? 4 : t); }
        }
    }
}

int dir_of(int dx, int dy)
{
    /* 0x403a00 */
    double t = atan2(-(double)dy, (double)dx);
    return ftol(((1.570796327 - t) * 256.5 + 3.141592654) * 0.15915494307111402) & 0xff;
}

int dir_turn(int dir, int target, int step)
{
    /* 0x40cae0 */
    if (dir == target) return dir;
    if (target < dir) {
        if (dir - target > 0x7f) return (dir + step) & 0xff;
        dir -= step;
        if (dir < target) return target & 0xff;
    } else if (target - dir < 0x80) {
        dir += step;
        if (target < dir) return target & 0xff;
    } else {
        dir -= step;
    }
    return dir & 0xff;
}

/* 0x422600: the first number on the line ("\r" before any digit: 0); a '-' anywhere before it
   negates. */
static int32_t parse_num(const char *p, const char *end)
{
    int sign = 1;
    for (;; p++) {
        if (p >= end || *p == '\r') return 0;
        if (*p == '-') sign = -1;
        if (*p >= '0' && *p <= '9') break;
    }
    int32_t v = 0;
    for (; p < end && *p >= '0' && *p <= '9'; p++) v = *p - '0' + v * 10;
    return sign * v;
}

typedef struct { const char *name; int32_t *var; } VarDef;

/* The vars parsers 0x4180e0 / 0x41e960: every line that starts with a variable's name sets it. */
static bool vars_load(const char *path, const VarDef *defs)
{
    size_t n = 0;
    char *buf = (char *)file_load(path, &n);
    if (!buf) return false;
    const char *end = buf + n;
    for (const char *p = buf; p < end;) {
        for (const VarDef *d = defs; d->name; d++) {
            size_t k = strlen(d->name);
            if ((size_t)(end - p) >= k && !memcmp(p, d->name, k)) {
                *d->var = parse_num(p + k, end);
                break;
            }
        }
        while (p < end && *p != '\n') p++;
        p++;
    }
    free(buf);
    return true;
}

void player_vars_load(int character)
{
    static const char *const names[] = { "BoPeep", "Adam", "Motley", "Shep" };   /* 0x44c978 */
    const VarDef defs[] = {                                                     /* 0x44c918 */
        { "CREEP_SPEED", &g_pvars.creep_speed }, { "WALK_SPEED", &g_pvars.walk_speed },
        { "RUN_SPEED", &g_pvars.run_speed }, { "CREEP_RADIUS", &g_pvars.creep_radius },
        { "WALK_RADIUS", &g_pvars.walk_radius }, { "RUN_RADIUS", &g_pvars.run_radius },
        { NULL, NULL },
    };
    char path[64];
    snprintf(path, sizeof path, "data\\misc\\%s.txt", names[character & 3]);
    if (!vars_load(path, defs)) fprintf(stderr, "Cannot load Player Vars file %s\n", path);
}

void flock_vars_load(int breed)
{
    static const char *const names[] = { "Pastoral", "Factoral", "Longwool", "Neogenetic" };  /* 0x44d2d0 */
    FlockVars *v = &g_fvars;
    const VarDef defs[] = {                                                     /* 0x44d1c0 */
        { "NEIGHBOUR_DIST", &v->neighbour_dist }, { "NUM_NEIGHBOURS", &v->num_neighbours },
        { "TOO_CLOSE_DIST", &v->too_close_dist }, { "TURN_SPEED", &v->turn_speed },
        { "SHEEP_ACCEL", &v->sheep_accel }, { "MAN_AHEAD_DIST", &v->man_ahead_dist },
        { "BIAS_CENTER", &v->bias_center }, { "BIAS_DIRECTION", &v->bias_direction },
        { "BIAS_CLOSEST", &v->bias_closest }, { "BIAS_RANDOM", &v->bias_random },
        { "BIAS_PLAYER", &v->bias_player }, { "AMBLE_SPEED", &v->amble_speed },
        { "WANDER_RADIUS", &v->wander_radius }, { "SHEEP_SPEED", &v->sheep_speed },
        { NULL, NULL },
    };
    char path[64];
    snprintf(path, sizeof path, "data\\misc\\%s.txt", names[breed & 3]);
    if (!vars_load(path, defs)) fprintf(stderr, "Cannot load Flock Vars file %s\n", path);
    g_flock = g_fvars;
}

int attract_new(int x, int y, int type, int range, int power)
{
    /* 0x403970: the first free slot (the original can step one past the table's end) */
    int i = 0;
    if (g_attract[0].type != 0)
        do i++; while (i < MAX_ATTRACT - 1 && g_attract[i].type != 0);
    Attractor *a = &g_attract[i];
    a->x = (int16_t)x;
    a->y = (int16_t)y;
    a->type = (int16_t)type;
    a->range = (int16_t)range;
    a->power = (int16_t)power;
    if (g_nattract <= i) g_nattract = (int16_t)(i + 1);
    return i;
}

void specials_load(void)
{
    /* part of 0x401950: 256 records of 18 bytes; older files have 8 or 16 bytes per record */
    memset(g_special, 0, sizeof g_special);
    size_t n = 0;
    uint8_t *f = file_load(world_level_path(".spc"), &n);
    if (!f) return;
    uint8_t *dst = (uint8_t *)g_special;
    size_t rec = n == 0x800 ? 8 : n == 0x1000 ? 16 : 0;
    if (rec) {
        for (int i = 0; i < MAX_SPECIAL; i++) memcpy(dst + 0x12 * i, f + rec * i, rec);
    } else {
        memcpy(dst, f, n < sizeof g_special ? n : sizeof g_special);   /* the original copies 0x1200 */
    }
    free(f);
}

/* 0x401050: a bank table entry's path. The low nibble of its flags picks Standard\ Extended\ Deaths\,
   the high one the folder: 0x10 the character's (with its file prefix), 0x20 the breed's, 0x40
   Extra\, 0x50 GUI\, 0x60 the map screen. */
static const char *bank_path(uint32_t entry)
{
    static char path[256];   /* 0x4bc92c */
    uint8_t f = exe_u8(entry);
    snprintf(path, sizeof path, "%s", exe_str(0x44a0e4));
    size_t n = strlen(path);
#define CAT(s) (n += (size_t)snprintf(path + n, sizeof path - n, "%s", (s)))
    switch (f & 0xf0) {
    case 0x10: CAT(exe_str(0x44a134)); CAT(exe_str(exe_u32(0x44a178 + 4 * (uint32_t)(g_char & 3)))); break;
    case 0x20: CAT(exe_str(0x44a148)); CAT(exe_str(exe_u32(0x44a18c + 4 * (uint32_t)(g_breed & 3)))); break;
    case 0x40: CAT(exe_str(0x44a0f4)); break;
    case 0x50: CAT(exe_str(0x44a110)); break;
    case 0x60: CAT(exe_str(0x44a0fc)); break;
    }
    switch (f & 0xf) {
    case 1: CAT(exe_str(0x44a150)); break;
    case 2: CAT(exe_str(0x44a15c)); break;
    case 3: CAT(exe_str(0x44a168)); break;
    }
    if ((f & 0xf0) == 0x10) CAT(exe_str(exe_u32(0x44a1a0 + 4 * (uint32_t)(g_char & 3))));
    else if ((f & 0xf0) == 0x20) CAT(exe_str(exe_u32(0x44a1b4 + 4 * (uint32_t)(g_breed & 3))));
    CAT(exe_str(exe_u32(entry + 4)));
    CAT(exe_str(0x44a1c8));
#undef CAT
    return path;
}

/* 0x4014c0 / 0x401560: the banks of a table (8-byte entries {flags, name}) whose numbers (first + i)
   are in the descriptor's 0xff-terminated list (no list: all of them). */
static void banks_from_table(uint32_t table, int first, uint32_t ids)
{
    for (int i = 0; exe_u32(table + 8 * (uint32_t)i + 4); i++) {
        int bank = first + i;
        if (ids) {
            uint32_t q = ids;
            while (exe_s8(q) != -1 && exe_s8(q) != bank) q++;
            if (exe_s8(q) != bank) continue;
        }
        sprite_bank_load(bank, bank_path(table + 8 * (uint32_t)i));
    }
}

void bank_table_load(uint32_t table, int first) { banks_from_table(table, first, 0); }

void actor_banks_load(void)
{
    banks_from_table(0x44a348, 0, desc_u32(DESC_PLAYER_BANKS));
    banks_from_table(0x44a1d0, 50, desc_u32(DESC_SHEEP_BANKS));
}
