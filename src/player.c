/* The shepherd: input (0x414eb0), demo record / playback (0x415260), the state machine
   (0x415f10), collision with the level (0x417ef0: zones, attractors 0x417310, special cells
   0x417550), animation (0x416db0) and the draw records (0x417070, the mouse cursor 0x40bec0). */
#include "player.h"
#include "exe.h"
#include "level.h"
#include "levels.h"
#include "paths.h"
#include "render.h"
#include "rng.h"
#include "sheep.h"
#include "sound.h"
#include "sprites.h"
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int g_in_raw_x, g_in_raw_y;             /* 0x4b9820, 0x4b9824 */
static int in_prev_x, in_prev_y;        /* 0x4b9780, 0x4b9830 */
static int in_still;                    /* 0x4b9838 */
static int in_cap, in_cap_dir;          /* 0x4b981c, 0x4b9834 */
int g_key_x, g_key_y;                   /* 0x4b0fa0, 0x4b0fa4: -1 / 0 / 1 */
int g_key_run, g_key_creep;             /* 0x4b0b58, 0x4a3b3c */
int g_pad_x, g_pad_y;                   /* 0x4bc924, 0x4bc928: -1000..1000 */
int8_t g_pad_buttons;                   /* 0x4cfd44 */
int g_cursor_on;                        /* 0x44b444 */
int8_t g_cursor_key;                    /* 0x4a4128 */
int g_cursor_x, g_cursor_y;             /* 0x4ae058, 0x4ae068: screen */
int g_player_moved;                     /* 0x4a40c5 */
int g_effect_fired;                     /* 0x4b982c */
int g_score;                            /* 0x4a2068 */
int32_t *g_chain[32][2];                /* 0x4bbbf8: sheep following one another (effect 4) */

/* ---------------------------------------------------------------- demo streams (0x4ae434..0x4ae440)
   Four run-length streams of {u16 value, u16 count}: the buttons, input x, input y, magnitude. */
enum { DEMO_MAX = 50000 };
typedef struct { uint16_t v; int16_t n; } DemoRun;
static DemoRun demo_buf[4][DEMO_MAX + 2];
DemoRun *g_demo_stream[4] = { demo_buf[0], demo_buf[1], demo_buf[2], demo_buf[3] };
int g_demo_pos[4];                      /* 0x4ae0e8, 0x4ae0dc, 0x4ae0d4, 0x4ae0d0 */
int g_demo_left[4];                     /* 0x4a3b4c, 0x4a3b48, 0x4a3b44, 0x4a3b40 */
int g_demo_len[4];                      /* 0x4b0f10, 0x4b0f0c, 0x4b0f08, 0x4b0f04 */
int (*g_demo_abort)(void);              /* 0x422930: a key ends the playback */

/* ---------------------------------------------------------------- small helpers */

static uint32_t rd_blo32(int off)
{
    uint32_t v;
    memcpy(&v, g_lv.blo + off, 4);
    return v;
}

void player_set_state(Player *p, int s)
{
    /* 0x414b10 */
    if (p->state == s) return;
    p->state = s;
    g_anim[p->slot].sub = 0;
    switch (s) {
    case 9:
        if (p->kind == 0x13 || p->kind == 0x10) p->state = 0;
        break;
    case 0x1b: case 0x23: p->state = 10; break;
    case 0x1e: p->state = 0; break;
    }
}

void player_set_kind(Player *p, int k, int t)
{
    /* 0x414ba0 */
    switch (k) {
    case 0: case 0x10: case 0x13:
        p->kind = k;
        if (t != -1) p->kind_time = t * 0x3c;
        break;
    case 0xd: g_anim[p->slot].rec.tint |= 1; break;
    case 0xe: g_anim[p->slot].rec.tint |= 2; break;
    case 0xf: g_anim[p->slot].rec.tint |= 4; break;
    case 0x15: player_set_state(p, 0x17); break;
    }
}

void player_init(Player *p)
{
    /* 0x418010 */
    memset(p, 0, sizeof *p);
    p->kind = 0;
    p->anim[0] = 0;
    p->anim[1] = 2;
    p->anim[2] = 1;
    p->creep = (float)((double)g_pvars.creep_speed * (double)0.1f);
    p->walk = (float)((double)g_pvars.walk_speed * (double)0.1f);
    p->run = (float)((double)g_pvars.run_speed * (double)0.1f);
    p->idle = (crt_rand() & 0x7f) + 0xb4;
    p->attract = attract_new((int16_t)ftol(p->x), (int16_t)ftol(p->y), 2, 0x32, 8);
    p->attract2 = attract_new((int16_t)ftol(p->x), (int16_t)ftol(p->y), 2, 0x32, 8);
}

/* ---------------------------------------------------------------- power-ups (0x414c70) */

static void player_effects(Player *p)
{
    g_effect_fired = 0;
    if (g_action == 1) {
        int e = p->effect < 0 ? -p->effect : p->effect;
        if (e == 6) {
            g_action = 2;
            g_effect_count--;
            int i = anim_new(ftol(p->x), ftol(p->y), 5, 8, 4);
            AnimObj *a = &g_anim[i];
            a->move = 7;
            a->sub = 4;
            a->d[0x16] = 6;
            a->rec.scale_x = a->rec.scale_y = 0x80;
            sound_play_at(a, 0x62, 0, 0);
            g_effect_fired = 1;
            if (g_effect_count == 0) p->effect = 0;
        }
    }
    if (p->effect <= 0) return;
    switch (p->effect) {
    case 2: case 6: case 7:
        break;
    case 3:
        if (--p->effect_time > 0) return;
        p->effect = 0;
        /* TODO: sound: 0x4a40cc fades the music back (0x4376b0), 0x4a40d0 stops a voice (0x406650) */
        break;
    case 4: {
        if (p->effect_time - 1 < 1) p->effect = 0;
        else p->effect_time--;
        int32_t *local[32][2];
        memset(local, 0, sizeof local);
        int32_t *q = p->d;
        int more = p->effect;
        while (more) {
            int slot = q[9];
            int32_t *a = g_chain[slot][0], *b = g_chain[slot][1];
            local[slot][0] = a;
            local[slot][1] = b;
            if (q[3] == 0 && q[4] != 2) {
                local[31][0] = q;
                if (b) q = b;
            } else {
                if (a) local[a[9]][1] = NULL;
                local[slot][0] = local[slot][1] = NULL;
                g_chain[slot][1] = NULL;
            }
            more = g_chain[q[9]][1] != NULL;
        }
        memcpy(g_chain, local, sizeof g_chain);
        break;
    }
    default:
        g_effect_count = 0;
        p->effect = 0;
        break;
    }
}

/* ---------------------------------------------------------------- input (0x414eb0) */

static void player_input(Player *p)
{
    if (g_demo == 2) return;
    switch (g_input_mode) {
    case 0: {
        p->in_dx += g_mouse_dx;
        p->in_dy += g_mouse_dy;
        g_in_raw_x = g_mouse_dx;
        g_in_raw_y = g_mouse_dy;
        int dir = dir_of(p->in_dx, p->in_dy);
        int ax = p->in_dx < 0 ? -p->in_dx : p->in_dx, ay = p->in_dy < 0 ? -p->in_dy : p->in_dy;
        if (ax > 0x100) ax = 0x100;
        if (ay > 0x100) ay = 0x100;
        int mag = ftol(sqrt((double)(ay * ay + ax * ax)));
        if (g_in_raw_x == in_prev_x && g_in_raw_y == in_prev_y) {
            if (++in_still >= 0x3c && in_cap > 0) in_cap--;
        } else {
            in_cap = mag < 0x80 ? mag : 0x80;
            in_still = 0;
            in_cap_dir = dir;
        }
        in_prev_x = g_in_raw_x;
        in_prev_y = g_in_raw_y;
        if (mag > in_cap) {
            mag = in_cap;
            p->in_dx = g_isin[in_cap_dir] * in_cap / 32768;
            p->in_dy = -(g_icos[in_cap_dir] * in_cap / 32768);
        }
        p->in_mag = mag;
        break;
    }
    case 1:
        g_run = g_creep = 0;
        p->in_dx = p->in_dy = 0;
        if (g_key_run == 1) g_run = 1;
        if (g_key_creep == 1) g_creep = 1;
        g_in_raw_x = g_key_x * 8;
        g_in_raw_y = g_key_y * 8;
        p->in_dx = g_key_x * 0x28;
        p->in_dy = g_key_y * 0x28;
        p->in_mag = g_key_x == 0 && g_key_y == 0 ? 0 : 0x28;
        break;
    case 2:
        p->in_dx = g_pad_x / 10;
        p->in_dy = g_pad_y / 10;
        g_in_raw_x = g_pad_x / 100;
        g_in_raw_y = g_pad_y / 100;
        g_run = g_pad_buttons & 1;
        g_creep = (g_pad_buttons & 2) >> 1;
        p->in_mag = ftol(sqrt((double)(p->in_dy * p->in_dy) + (double)(p->in_dx * p->in_dx)));
        break;
    case 3:
        p->in_dx = g_pad_x < -200 ? -0x28 : g_pad_x > 200 ? 0x28 : 0;
        p->in_dy = g_pad_y < -200 ? -0x28 : g_pad_y > 200 ? 0x28 : 0;
        g_in_raw_x = g_pad_x / 100;
        g_in_raw_y = g_pad_y / 100;
        g_run = g_pad_buttons & 1;
        g_creep = (g_pad_buttons & 2) >> 1;
        p->in_mag = p->in_dx == 0 && p->in_dy == 0 ? 0 : 0x28;
        break;
    }
}

/* ---------------------------------------------------------------- demos (0x415260) */

static bool demo_record(int k, uint16_t v)
{
    /* one stream: extend the current run or start a new one (runs cap at 0x7fff ticks) */
    DemoRun *s = g_demo_stream[k];
    if (s[g_demo_pos[k]].v == v) {
        if (++g_demo_left[k] != 0x7fff) return false;
        s[g_demo_pos[k]].n = 0x7fff;
    } else {
        s[g_demo_pos[k]].n = (int16_t)g_demo_left[k];
    }
    g_demo_len[k]++;
    g_demo_pos[k]++;
    g_demo_left[k] = 1;
    s[g_demo_pos[k]].v = v;
    return true;
}

static bool demo_play(int k, int32_t *out)
{
    if (g_demo_pos[k] == g_demo_len[k]) return true;
    DemoRun *s = g_demo_stream[k];
    *out = (int16_t)s[g_demo_pos[k]].v;
    if (--g_demo_left[k] < 1) {
        g_demo_pos[k]++;
        g_demo_left[k] = s[g_demo_pos[k]].n;
    }
    return false;
}

static void player_demo(Player *p)
{
    if (g_demo == 1) {
        uint16_t b = (uint16_t)(g_run != 0);
        if (g_creep) b |= 2;
        if (g_action) b |= 4;
        if (g_demo_len[0] == DEMO_MAX || g_demo_len[1] == DEMO_MAX || g_demo_len[2] == DEMO_MAX ||
            g_demo_len[3] == DEMO_MAX) {
            g_demo = 4;
            return;
        }
        demo_record(0, b);
        demo_record(1, (uint16_t)p->in_dx);
        demo_record(2, (uint16_t)p->in_dy);
        demo_record(3, (uint16_t)p->in_mag);
    }
    if (g_demo == 2) {
        int done = 0;
        g_action = g_run = g_creep = 0;
        if (g_demo_pos[0] == g_demo_len[0]) {
            done = 1;
        } else {
            uint16_t b = g_demo_stream[0][g_demo_pos[0]].v;
            g_run = (b & 1) != 0;
            g_creep = (b & 2) != 0;
            g_action = (b & 4) != 0;
            if (--g_demo_left[0] < 1) {
                g_demo_pos[0]++;
                g_demo_left[0] = g_demo_stream[0][g_demo_pos[0]].n;
            }
        }
        done += demo_play(1, &p->in_dx);
        done += demo_play(2, &p->in_dy);
        done += demo_play(3, &p->in_mag);
        if (done == 4 || (g_demo_abort && g_demo_abort())) {
            g_demo = 3;
            g_end = 6;
        }
    }
}

/* ---------------------------------------------------------------- collision */

static void player_attractors(Player *p, int act)
{
    /* 0x417310 */
    for (int i = 0; i < g_nattract; i++) {
        Attractor *a = &g_attract[i];
        if (a->type <= 2) continue;
        int dx = a->x - ftol(p->x), dy = a->y - ftol(p->y);
        if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) >= a->range) continue;
        switch (a->type) {
        case 3:
            player_launch(p, dir_of(p->ix - a->x, p->iy - a->y), (a->power * 5 + 5) * 0x32,
                          (a->power * 5 + 5) * 2, 10, 0x1e);
            g_anim[p->slot].d[0x1d] = 8;
            p->state = 7;
            break;
        case 4: case 5:
            break;
        case 6: if (act) player_set_state(p, 0x17); break;
        case 7: if (act) player_set_state(p, 0x1b); break;
        case 9: if (act) player_set_state(p, 0x1d); break;
        case 10:
            if (!act) break;
            switch (a->power) {
            case 0: player_set_state(p, 9); break;
            case 1: player_set_state(p, 0x19); break;
            case 2:
                if (p->state != 0x1f) p->dir = dir_of(p->ix - a->x, p->iy - a->y);
                player_set_state(p, 0x1f);
                p->speed = 5.0f;
                break;
            case 3: player_set_state(p, 0x22); break;
            case 4: if (p->state != 10) player_set_state(p, 10); break;
            }
            break;
        default:
            fprintf(stderr, "Unknown attractor %d (Player)\n", a->type);
            break;
        }
    }
}

/* Point a level object (by object id) at the special that fired and at whoever fired it. */
static void trigger(int16_t id, int special, int who)
{
    if (!id) return;
    int s = g_obj_by_id[id];
    g_anim[s].d[0x22] = special;
    g_anim[s].d[0x21] = who;
}

void player_specials(int x, int y, Player *p, int act)
{
    /* 0x417550: the special cell (layer3) under (x, y), if the cell's collision mask is set there */
    g_on_ice = 0;
    uint32_t c = g_lv.layer3[(y / TILE_H) * g_lv.w + x / TILE_W];
    if (!(c & 0xff)) return;
    int si = c >> 8 & 0xff, sdir = (int32_t)c >> 16 & 0xf;
    if (g_lv.tab1[(x % TILE_W) + (y % TILE_H - TILE_H + (int)(c & 0xff) * TILE_H) * TILE_W] != 1) return;
    Special *s = &g_special[si];
    if (s->active) return;
    if (act && g_world == 7) s->count++;
    AnimObj *a = &g_anim[p->slot];
    switch (s->type) {
    case 0: case 1: case 8: case 0x10: case 0x11: case 0x17: case 0x1a:
        break;
    case 4:
        if (!g_creep) return;
        /* fall through */
    case 2: case 3:
        trigger(s->a[0], si, p->slot);
        trigger(s->a[1], si, p->slot);
        trigger(s->a[2], si, p->slot);
        trigger(s->a[3], si, p->slot);
        break;
    case 5:
        g_on_ice = 1;
        break;
    case 6: case 7:
        p->push_dir = sdir;
        p->hit = 2;
        break;
    case 9:
        if (act && p->state != 1 && s->a[5] != 1) {
            unsigned d = (unsigned)(p->dir - sdir * 0x10) & 0xff;
            if (d < 0x40 || 0xc0 < d) {
                p->state = 1;
                p->dir = p->face = sdir * 0x10;
                a->d[0x15] = a->d[0x16] = s->a[0] * 10;
                a->fy = p->y;
                a->d[0x18] = s->a[1];
                a->d[0x1b] = s->a[3];
                anim_set(p->slot, 3, -1);
                sound_play_at(a, 0x51, 0, 0);
                a->d[0x1a] = s->a[4];
            }
        }
        break;
    case 10:
        if (act) {
            p->pushed = 1;
            p->push_dir = sdir;
            a->speed = (float)((double)s->a[0] * (double)0.1f);
        }
        break;
    case 0xb:
        if (p->state == 0x14) { p->anim[0] = 10; p->state = 0; }
        if (s->a[0] == 0 || g_creep) {
            if (g_level == 5) g_end = 7;
            else if (g_saved < g_needed) {
                if (!g_level_over) {
                    int i = anim_new(0, 0, 0x10, -1, 4);
                    s->active = 1;
                    g_anim[i].move = 0x11;
                    g_anim[i].d[0x15] = 1;
                }
            } else {
                g_end = 1;
            }
        }
        trigger(s->a[1], si, p->slot);
        break;
    case 0xc:
        if (act && s->a[3]) {
            trigger(s->a[1], si, p->slot);
            a->d[0x23] = s->a[0];
            a->d[0x15] = s->a[2];
            player_set_state(p, 8);
            p->push_dir = sdir;
            p->hit = -1;
        }
        break;
    case 0xd:
        if (s->a[3] == 1) {
            trigger(s->a[0], si, p->slot);
            trigger(s->a[1], si, p->slot);
            trigger(s->a[2], si, p->slot);
            player_set_state(p, s->a[4]);
            a->d[0x15] = s->a[5];
            a->d[0x16] = s->a[6];
        }
        break;
    case 0xe:
        if (p->cooldown == 0) {
            player_set_state(p, s->a[0]);
            trigger(s->a[1], si, p->slot);
            trigger(s->a[2], si, p->slot);
        }
        if (s->a[3]) {
            p->pushed = 1;
            p->push_dir = sdir;
            a->speed = (float)((double)s->a[3] * (double)0.1f);
        }
        break;
    case 0xf:
        if (!act) return;
        p->dir = (sdir + 8) * 0x10;
        p->dir = ((rng_next() & 3) - 1 + p->dir) & 0xff;
        p->hit = -1;
        p->push_dir = (sdir - 8) & 0xf;
        player_set_state(p, 0x1f);
        p->speed = (float)((double)s->a[0] * (double)0.1f);
        trigger(s->a[1], si, p->slot);
        break;
    case 0x12:
        if (act) {
            player_set_kind(p, s->a[0], s->a[1]);
            p->pushed = 1;
            p->push_dir = sdir;
            p->hit = -1;
            a->speed = (float)((double)s->a[4] * (double)0.1f);
        }
        break;
    case 0x13:
        if (act) {
            p->state = 0x21;
            p->push_dir = sdir;
            p->hit = -1;
            a->speed = (float)((double)s->a[0] * (double)0.1f);
        }
        break;
    case 0x14:
        if (s->a[0] == 0 || g_creep) {
            player_set_state(p, s->a[2]);
            s->active = 1;
            a->d[0x15] = s->a[3];
        }
        trigger(s->a[1], si, p->slot);
        break;
    case 0x15:
        if (g_effect_fired) {
            trigger(s->a[0], si, p->slot);
            trigger(s->a[1], si, p->slot);
            trigger(s->a[2], si, p->slot);
            trigger(s->a[3], si, p->slot);
            p->push_dir = sdir;
        }
        break;
    case 0x16:
        if (act && p->hit != 2) {
            p->pushed = 1;
            p->push_dir = sdir;
            p->hit = -1;
            a->speed = (float)((double)s->a[0] * (double)0.1f);
        }
        break;
    default:
        fprintf(stderr, "Unknown specific type encountered %d\n", s->type);
        break;
    }
}

void player_collide(int x, int y, Player *p, int act)
{
    /* 0x417ef0: the zone rectangle the point is in (descriptor +8: {count, {state, x0, y0, x1, y1}}),
       the collision mask of the cell's layer1 type (1: slope with its direction in bits 8-11,
       2: wall), then the attractors and the special cells */
    p->zone = -1;
    p->hit = -1;
    uint32_t z = desc_u32(DESC_ZONES);
    if (z) {
        int n = exe_s32(z);
        for (int i = 0; i < n; i++) {
            uint32_t r = z + 4 + 20 * (uint32_t)i;
            if (exe_s32(r + 4) <= x && exe_s32(r + 8) <= y && x <= exe_s32(r + 12) && y <= exe_s32(r + 16)) {
                player_set_state(p, exe_s32(r));
                p->zone = i;
                break;
            }
        }
    }
    uint16_t c = g_lv.layer1[(y / TILE_H) * g_lv.w + x / TILE_W];
    int t = c & 0xff;
    if (t) {
        int8_t m = (int8_t)g_lv.tab1[(x % TILE_W) + (y % TILE_H - TILE_H + t * TILE_H) * TILE_W];
        if (m == 1) { p->hit = 0; p->push_dir = (c & 0xf00) >> 8; }
        else if (m == 2) p->hit = 2;
    }
    player_attractors(p, act);
    player_specials(x, y, p, act);
}

static void collide_ahead(Player *p, int dir, int act)
{
    /* the next step along dir, as most of the states test it */
    int x = ftol((double)g_sin[dir] * (double)p->speed + (double)p->x);
    int y = ftol((double)p->y - (double)g_cos[dir] * (double)p->speed);
    player_collide(x, y, p, act);
}

static void step(Player *p, int dir, float speed)
{
    p->x = (float)((double)g_sin[dir] * (double)speed + (double)p->x);
    p->y = (float)((double)p->y - (double)g_cos[dir] * (double)speed);
}

/* ---------------------------------------------------------------- movement */

static void player_unstick(Player *p)
{
    /* 0x415660: search outward (1 px per round, 8 directions either side, 16 apart) for a free spot */
    float r = 0.0f;
    int found = -1, lx = 0, ly = 0;
    for (;;) {
        r = (float)((double)r + 1.0);
        int a = p->dir, b = p->dir;
        for (int k = 0; k < 8 && found == -1; k++) {
            a = (a - 0x10) & 0xff;
            b = (b + 0x10) & 0xff;
            lx = ftol((double)r * (double)g_sin[a] + (double)p->x);
            ly = ftol((double)p->y - (double)r * (double)g_cos[a]);
            player_collide(lx, ly, p, 0);
            if (p->hit != 2) { found = a; break; }
            lx = ftol((double)r * (double)g_sin[b] + (double)p->x);
            ly = ftol((double)p->y - (double)r * (double)g_cos[b]);
            player_collide(lx, ly, p, 0);
            if (p->hit != 2) found = b;
        }
        if (found != -1) break;
    }
    p->x = (float)lx;
    p->y = (float)ly;
}

void player_walk(Player *p)
{
    /* 0x415780 */
    AnimObj *a = &g_anim[p->slot];
    a->rec.shadow = 2;
    a->rec.shadow_dy = 0;
    a->depth_off = 0;
    int target = dir_of(p->in_dx, p->in_dy);
    int mag = p->in_mag;
    int dir0 = p->dir;
    if (p->speed == 0.0f && dir0 != target) {
        if (mag > 0x10) p->dir = p->face = dir_turn(dir0, target, 0x10);
    } else {
        if (p->zone == 1) {
            p->dir = dir_turn(dir0, target, 2);
            p->face = dir_turn(p->face, target, 8);
        } else {
            p->dir = p->face = dir_turn(dir0, target, 8);
        }
        if (mag < 0x21) p->speed = 0.0f;
        else {
            p->speed = p->walk;
            if (g_run) p->speed = p->run;
        }
    }
    if (g_creep && mag > 8) p->speed = p->creep;
    if (g_action == 1 && p->effect != 6 && (p->state != 0x14 || !g_on_ice) && p->kind == 0)
        player_set_state(p, 0x24);
    p->face &= 0xff;
    p->dir &= 0xff;
    if (p->speed == 0.0f) {
        player_collide(ftol(p->x), ftol(p->y), p, 1);
        return;
    }
    collide_ahead(p, p->dir, 1);
    if (p->hit == 2) {
        player_collide(ftol(p->x), ftol(p->y), p, 0);
        int turn = g_deflect[p->push_dir + dir0 * 16];
        if (turn < -6 || 6 < turn) p->hit = 2;
        if (p->hit == 2 || p->hit < 0 || turn == 0) {
            player_unstick(p);
        } else {
            p->hit = 2;
            int d = dir0;
            for (int i = 0; i < 100 && p->hit == 2; i++) {
                d = (d + turn) & 0xff;
                collide_ahead(p, d, 1);
            }
            step(p, d, p->speed);
        }
    } else {
        step(p, p->dir, p->speed);
    }
    if (p->x < 0.0f) p->x = 0.0f;
    if (p->y < 0.0f) p->y = 0.0f;
    if ((float)g_lv.px_w <= p->x) p->x = (float)(g_lv.px_w - 1);
    if ((float)g_lv.px_h <= p->y) p->y = (float)(g_lv.px_h - 1);
}

/* The arc of a jump or a knock-back: d[0x15] counts down from d[0x16] by speed * -10 a tick and
   the height is d[0x18] * sin; the shadow stays on the ground (fy). */
static int arc(Player *p, AnimObj *a)
{
    double v = (double)a->d[0x1b] * (double)0.1f;
    p->speed = (float)v;
    p->x = (float)(v * (double)g_sin[p->dir] + (double)p->x);
    a->fy = (float)((double)a->fy - (double)g_cos[p->dir] * (double)p->speed);
    return 0;
}

static int arc_height(Player *p, AnimObj *a)
{
    a->d[0x15] += ftol((double)p->speed * -10.0);
    int t = (a->d[0x15] << 8) / a->d[0x16];
    double h = (double)a->fy - (double)a->d[0x18] * (double)g_sin[t / 2];
    p->y = (float)h;
    a->rec.shadow_dy = ftol((double)a->fy - h);
    return t;
}

static void arc_dust(Player *p, AnimObj *a, int t, bool from_float)
{
    a->rec.mode = a->rec.shadow_dy > 0x18 ? 2 : 1;
    if (a->d[0x18] > 0x96 && t > 0x28 && t < 0xe6 && (++a->d[0x17] & 7) == 0) {
        if (from_float) anim_new(ftol(p->x), ftol(p->y), 0xc, 8, 4);
        else anim_new(p->ix, p->iy, 0xc, 8, 4);
    }
}

void player_jump(Player *p)
{
    /* 0x415aa0 */
    AnimObj *a = &g_anim[p->slot];
    arc(p, a);
    p->anim[1] = p->anim[2] = 3;
    int t = arc_height(p, a);
    a->countdown = 4;
    a->frame = (t - 1) / 16;
    anim_set_frame(a);
    arc_dust(p, a, t, true);
    if (g_anim[p->slot].d[0x15] < 1) player_set_state(p, a->d[0x1a]);
}

static void player_knocked(Player *p)
{
    /* 0x415bf0 */
    AnimObj *a = &g_anim[p->slot];
    float ox = p->x, oy = p->y;
    if (p->kind == 0) p->anim[0] = p->anim[1] = p->anim[2] = 3;
    arc(p, a);
    int t = arc_height(p, a);
    int f = (t - 1) / 16;
    if (f < a->nframes) a->frame = f;
    a->countdown = 4;
    anim_set_frame(a);
    arc_dust(p, a, t, false);
    p->face = (a->d[0x1d] + p->face) & 0xff;
    if (g_anim[p->slot].d[0x15] < 1) {
        p->dir = p->face;
        player_set_state(p, a->d[0x1a]);
        a->rec.shadow_dy = 0;
    }
    player_collide(ftol(p->x), ftol(p->y), p, 0);
    if (p->hit == 2) {
        p->x = ox;
        p->y = oy;
        p->dir = p->face;
        player_set_state(p, a->d[0x1a]);
        a->rec.shadow_dy = 0;
        a->rec.mode = 1;
    }
}

static void player_steer_carried(Player *p)
{
    /* 0x415db0: state 0x15, carried by a level object (d[0x21] of the shepherd's object) */
    int iy = ftol(p->y), ix = ftol(p->x);
    int d = dir_of(ix + p->in_dx - ix, iy + p->in_dy - iy);
    int mag = ftol(sqrt((double)(p->in_dx * p->in_dx) + (double)(p->in_dy * p->in_dy)));
    if (mag > 0x80) {
        mag = 0x80;
        p->in_dx = g_isin[d] / 256;
        p->in_dy = -(g_icos[d] / 256);
    }
    p->in_mag = mag;
    if (mag > 5) p->dir = p->face = dir_turn(p->dir, d, 8) & 0xff;
    AnimObj *a = &g_anim[p->slot];
    a->rec.shadow = -1;
    a->depth_off = 0x18;
    int32_t *who = actor(a->d[0x21]);
    if (who[3] != 0x15 || p->state != 0x15) {
        a->depth_off = 0;
        a->rec.shadow_dy = 0;
        player_set_state(p, 10);
    }
}

void player_launch(Player *p, int dir, int count, int height, int then, int speed)
{
    /* 0x417250 */
    AnimObj *a = &g_anim[p->slot];
    p->state = 1;
    p->dir = dir;
    a->d[0x15] = a->d[0x16] = count;
    a->d[0x1a] = then;
    a->fy = p->y;
    a->d[0x1b] = speed;
    a->d[0x18] = height;
    a->d[0x19] = 0;
    a->d[0x1d] = 0;
    anim_set(p->slot, 3, -1);
}

void player_launch_sound(Player *p, int dir, int count, int height, int then, int speed)
{
    /* 0x4172c0 */
    player_launch(p, dir, count, height, then, speed);
    sound_play_at(&g_anim[p->slot], 0x51, 0, 0);
}

/* ---------------------------------------------------------------- animation (0x416db0) */

static void player_animate(Player *p)
{
    int d16 = (p->face + 8) / 16 & 0xf;
    int slot = p->slot;
    AnimObj *a = &g_anim[slot];
    int stepn;
    switch (p->state) {
    case 0: case 0xc: case 0x18:
        if (a->anim == 2 && p->speed == 0.0f) {
            if (a->frame == 0xc) { anim_set(slot, p->anim[0], d16); goto done; }
            stepn = 1;
            break;
        }
        if (p->speed == 0.0f) {
            if (p->anim[0] == 0x14) {
                if (p->idle < 1) { if (a->anim != 6) anim_set(slot, 6, d16); }
                else { p->idle--; if (a->anim != 0x14) anim_set(slot, 0x14, d16); }
            } else if (a->anim != p->anim[0]) {
                anim_set(slot, p->anim[0], d16);
            }
        } else {
            p->idle = (crt_rand() & 0x7f) + 0xb4;
        }
        if (p->speed == p->creep && a->anim != p->anim[1]) anim_set(slot, p->anim[1], d16);
        if (p->walk <= p->speed && a->anim != p->anim[2]) anim_set(slot, p->anim[2], d16);
        if (p->walk < p->speed) stepn = 4;
        else if (p->speed <= p->creep) stepn = 1;
        else stepn = 2;
        break;
    case 1: case 7:
        stepn = 1;
        break;
    default:
        if (p->speed == 0.0f) {
            if (a->anim != p->anim[0]) anim_set(slot, p->anim[0], d16);
        } else {
            if (p->speed == p->creep && a->anim != p->anim[1]) anim_set(slot, p->anim[1], d16);
            if (p->walk <= p->speed && a->anim != p->anim[2]) anim_set(slot, p->anim[2], d16);
        }
        stepn = p->speed <= p->creep ? 1 : 2;
        break;
    }
    anim_step(slot, d16, stepn);
done:
    g_anim[p->slot].x = ftol(p->x);
    g_anim[p->slot].y = ftol(p->y);
}

/* ---------------------------------------------------------------- the tick (0x415f10) */

static void anim_set_from(Player *p, int kind)
{
    /* the stand / slow / fast animations of an animation set (0x44c688, 12 bytes each) */
    uint32_t e = 0x44c688 + 12 * (uint32_t)kind;
    p->anim[0] = exe_s32(e);
    p->anim[1] = exe_s32(e + 4);
    p->anim[2] = exe_s32(e + 8);
}

void player_tick(Player *p)
{
    g_player_moved = 0;
    int slot = p->slot;
    AnimObj *a = &g_anim[slot];
    player_effects(p);
    player_input(p);
    if (p->state != 0x14) anim_set_from(p, p->kind);
    a->rec.mode = 1;
    a->type = ANM_PLAYER;
    a->rec.clip_x0 = -1;
    a->rec.shadow = 2;
    a->rec.layer = 1;
    player_demo(p);
    if (p->pushed) {
        p->x = (float)((double)g_sin[p->push_dir * 16] * (double)a->speed + (double)p->x);
        p->pushed = 0;
        p->y = (float)((double)p->y - (double)g_cos[p->push_dir * 16] * (double)a->speed);
    }
    if (p->state >= 100) {
        level_actor_hook(p->d);
        goto tail;
    }
    switch (p->state) {
    case 0:
        player_walk(p);
        if (p->cooldown) p->cooldown--;
        break;
    case 1:
        p->face = p->dir;
        player_jump(p);
        break;
    case 3:
        p->speed = 0.0f;
        p->anim[0] = 7;
        if (a->state == 2) p->state = 0;
        break;
    case 4:
        goto tail;
    case 7:
        player_knocked(p);
        break;
    case 8:
        if (a->sub == 0) {
            path_start(a->d[0x23]);
            p->speed = 1.0f;
            if (actor_walk_to(p->d, g_path_x, g_path_y)) a->sub = 1;
        } else if (a->sub == 1) {
            int end = path_follow(a);
            p->dir = a->dir << 4;
            p->x = (float)a->x;
            p->y = (float)a->y;
            if (end) {
                p->state = 0;
                if (a->d[0x15]) {
                    int s = g_obj_by_id[a->d[0x15]];
                    g_anim[s].d[0x22] = p->slot;
                    g_anim[s].d[0x21] = p->slot;
                }
            }
        }
        p->face = p->dir;
        break;
    case 9:
        p->cooldown = 0x3c;
        if (a->sub == 0) {
            p->anim[0] = 0xe;
            p->speed = 0.0f;
            a->d[0x15] = 0x1e;
            a->sub = 1;
            sound_play_at(a, crt_rand() % 3 + 0x43, 0, 0);
        } else if (a->sub == 1) {
            p->anim[0] = 0xe;
            p->speed = 0.0f;
            if (a->d[0x15]-- < 1) {
                anim_new(p->ix, p->iy, 0xc, 8, 4);
                a->d[0x15] = 0x1e;
                a->sub = 2;
            }
        } else if (a->sub == 2) {
            p->anim[0] = 0xe;
            p->speed = 0.0f;
            if (a->d[0x15]-- < 1) player_set_state(p, 0);
        }
        break;
    case 10: case 0x10: case 0x17: case 0x19: case 0x1d: case 0x22:
        p->speed = 0.0f;
        p->cooldown = 0x3c;
        if (p->kind == 0x13) {
            if (a->sub == 0) {
                a->d[0x15] = 0x78;
                a->sub = 1;
            } else if (a->sub == 1) {
                p->dir = p->face = (p->dir + 0x10) & 0xff;
                if (a->d[0x15]-- < 1) player_set_state(p, 0);
            }
        } else {
            if (a->sub == 0) {
                if (!(rng_next() & 1) && p->state != 0x17) {
                    a->d[0x15] = 0x1d;
                    a->sub = 1;
                    p->anim[0] = a->d[0x15];
                    break;
                }
                a->d[0x15] = 5;
                a->sub = 1;
            } else if (a->sub == 1) {
                if (a->anim == 5) a->rec.mode = 3;
                if (a->anim == a->d[0x15] && a->state == 2) {
                    player_set_state(p, 0);
                    p->anim[0] = a->d[0x15];
                    break;
                }
            }
            p->anim[0] = a->d[0x15];
        }
        break;
    case 0xc: {
        player_walk(p);
        if (p->zone == -1) { player_set_state(p, 0); break; }
        uint32_t r = desc_u32(DESC_ZONES) + 4 + 20 * (uint32_t)p->zone;
        a->rec.clip_x0 = exe_s32(r + 4);
        a->rec.clip_y0 = exe_s32(r + 8) - 0x22;
        a->rec.clip_x1 = exe_s32(r + 12);
        a->rec.mode = 2;
        a->rec.clip_y1 = exe_s32(r + 16) - 0x22;
        a->rec.layer = 0;
        a->rec.sortable = 0;
        a->rec.shadow = -1;
        p->anim[0] = 0x15;
        p->anim[1] = 0x16;
        p->anim[2] = 0x17;
        if (0.0f < p->speed) g_player_moved = 1;
        break;
    }
    case 0xf: {
        int x = ftol((double)g_sin[p->dir] * (double)p->speed + (double)p->x);
        int y = ftol((double)p->y - (double)g_cos[p->dir] * (double)p->speed);
        player_collide(x, y, p, 0);
        if (p->hit == 2) {
            p->x = (float)((double)g_sin[p->dir] * (double)p->speed + (double)p->x);
            p->timer = 0;
            p->speed = 0.0f;
            p->y = (float)((double)g_cos[p->dir] * (double)p->speed + (double)p->y);
        } else {
            p->x = (float)x;
            p->y = (float)y;
        }
        if (--p->timer < 0) {
            anim_set(p->slot, 5, -1);
            p->state = 10;
        }
        break;
    }
    case 0x14:
        player_walk(p);
        if (a->anim == 7) {
            p->speed = 0.0f;
            p->anim[0] = 7;
            if (a->state == 2) p->anim[0] = 9;
        } else {
            p->anim[0] = 9;
            p->anim[1] = p->anim[2] = 0x1e;
            if (g_run) anim_step(p->slot, -1, 2);
            if (g_creep && !g_on_ice) { p->anim[0] = 10; p->state = 0; }
        }
        break;
    case 0x15:
        player_steer_carried(p);
        p->anim[0] = 0x11;
        p->speed = 0.0f;
        p->face = p->dir;
        break;
    case 0x18:
        if (a->sub == 0) {
            path_start(a->d[0x23]);
            p->speed = 2.0f;
            if (actor_walk_to(p->d, g_path_x, g_path_y)) a->sub = 1;
            p->face = p->dir;
        } else if (a->sub == 1) {
            int end = path_follow(a);
            p->speed = 0.0f;
            p->dir = p->face = a->dir << 4;
            p->x = (float)a->x;
            p->y = (float)a->y;
            if (a->d[0x24] == a->d[0x18] && a->d[0x25] == 0) player_set_kind(p, a->d[0x15], a->d[0x16]);
            if (end) player_set_state(p, 0);
        }
        break;
    case 0x1c:
        switch (a->sub) {
        case 0:
            a->speed = 1.0f;
            a->rec.mode = 3;
            a->d[0x1b] = p->iy + 0xdc;
            a->depth_off = -1000;
            a->d[0x1c] = 0x100;
            a->sub = 1;
            p->face = p->dir;
            goto tail;
        case 1:
            if (a->speed <= 5.0f) a->speed = (float)((double)a->speed + (double)0.2f);
            p->y = (float)((double)p->y + (double)a->speed);
            a->d[0x1c]--;
            a->rec.scale_x = a->rec.scale_y = a->d[0x1c];
            p->dir = (p->dir + 8) & 0xff;
            if ((float)a->d[0x1b] < p->y) {
                p->anim[0] = p->anim[1] = p->anim[2] = 5;
                anim_set(p->slot, 5, -1);
                a->sub = 2;
                p->face = p->dir;
                goto tail;
            }
            break;
        case 2:
            p->anim[0] = p->anim[1] = p->anim[2] = 5;
            if (a->state == 2) {
                p->x = (float)a->d[0x15];
                p->y = (float)a->d[0x16];
                a->rec.mode = 1;
                a->depth_off = 0;
                a->d[0x1b] = 0x3c;
                a->sub = 3;
                p->face = p->dir;
                goto tail;
            }
            break;
        case 3: {
            int t = --a->d[0x1b];
            if (t < 1) {
                a->rec.scale_x = a->rec.scale_y = 0x100;
                actor_set_state(p->d, 0);
                p->face = p->dir;
                goto tail;
            }
            if (t & 8) {
                a->rec.scale_x = a->rec.scale_y = 0x100;
                p->face = p->dir;
                goto tail;
            }
            a->rec.scale_x = a->rec.scale_y = 0;
            break;
        }
        }
        p->face = p->dir;
        break;
    case 0x1f: {
        float ox = p->x, oy = p->y;
        if (p->kind == 0) p->anim[0] = p->anim[1] = p->anim[2] = 6;
        p->x = (float)((double)g_sin[p->dir] * (double)p->speed + (double)p->x);
        p->face = (p->face + 8) & 0xff;
        p->y = (float)((double)p->y - (double)g_cos[p->dir] * (double)p->speed);
        p->speed = (float)((double)p->speed - (double)0.1f);
        if (p->speed <= 1.0f) {
            p->dir = p->face;
            player_set_state(p, 0);
        }
        player_collide(ftol(p->x), ftol(p->y), p, 1);
        if (p->hit == 2) {
            p->x = ox;
            p->y = oy;
            p->dir = p->face;
            p->speed = 0.0f;
            player_set_state(p, 0);
        }
        break;
    }
    case 0x20:
        p->speed = 0.0f;
        if (a->sub == 0) {
            if (a->rec.scale_x == 0x100 && g_theme == 5 && g_level == 1) sound_play_at(a, 0xe, 0, 0);
            a->rec.scale_x -= 10;
            if (a->rec.scale_x < 0) {
                a->rec.scale_x = 0;
                p->x = (float)a->d[0x15];
                p->y = (float)a->d[0x16];
                g_scroll_reset = 1;
                if (g_world == 3 && g_level == 3) {
                    /* the level's two halves have their own camera bounds */
                    if (p->x < 1920.0f) {
                        g_lv.cam_min_x = (int32_t)rd_blo32(0x28);
                        g_lv.cam_max_x = (int32_t)rd_blo32(0x2c);
                        g_lv.cam_min_y = (int32_t)rd_blo32(0x30);
                        g_lv.cam_max_y = (int32_t)rd_blo32(0x34);
                    } else {
                        g_lv.cam_max_x = 0xc80;
                        g_lv.cam_min_x = 0x780;
                        g_lv.cam_min_y = 0x3c0;
                        g_lv.cam_max_y = 0x780;
                    }
                }
                a->sub = 1;
                if (g_theme == 5 && g_level == 1) sound_play_at(a, 0x1b, 0, 0);
            }
            a->rec.scale_y = a->rec.scale_x;
        } else if (a->sub == 1) {
            if (a->d[0x17]) sound_play_at(a, (int16_t)a->d[0x17], 0, 0);
            a->sub = 2;
        } else if (a->sub == 2) {
            a->rec.scale_x += 10;
            if (0x100 < a->rec.scale_x) {
                a->rec.scale_x = 0x100;
                player_set_state(p, 0);
            }
            a->rec.scale_y = a->rec.scale_x;
        }
        break;
    case 0x21:
        p->x = (float)((double)g_sin[p->push_dir * 16] * (double)a->speed + (double)p->x);
        p->state = 0;
        p->y = (float)((double)p->y - (double)g_cos[p->push_dir * 16] * (double)a->speed);
        player_walk(p);
        p->anim[0] = p->anim[1] = p->anim[2] = 3;
        if (a->state == 2) anim_set(a->slot, 3, -1);
        break;
    case 0x24:
        if (a->sub == 0) {
            int ay = ftol((double)p->y - (double)g_cos[p->face] * 64.0);
            int ax = ftol((double)g_sin[p->face] * 64.0 + (double)p->x);
            a->d[0x16] = attract_new(ax, ay, 2, 200, 9);
            p->anim[0] = 8;
            a->sub = 1;
        } else if (a->sub != 1) {
            break;
        }
        p->speed = 0.0f;
        p->in_mag = p->in_dy = p->in_dx = 0;
        p->anim[0] = 8;
        if (a->anim == 8 && a->state == 2) {
            g_attract[a->d[0x16]].type = 0;
            player_set_state(p, 0);
        }
        break;
    default:
        fprintf(stderr, "Illeagal move type (Player) %d\n", p->state);
        break;
    }
    p->face = p->state == 1 ? p->face : p->face;
tail:
    p->ix = ftol(p->x);
    p->iy = ftol(p->y);
    {
        double ahead = (double)g_fvars.man_ahead_dist;
        int16_t ax = (int16_t)ftol((double)g_sin[p->dir] * (double)p->speed * ahead + (double)p->x);
        int16_t ay = (int16_t)ftol((double)p->y - (double)g_cos[p->dir] * (double)p->speed * ahead);
        Attractor *r = &g_attract[p->attract];
        r->type = 2;
        r->power = 8;
        r->range = (int16_t)g_pvars.walk_radius;
        if (g_run) { r->power = 0xc; r->range = (int16_t)g_pvars.run_radius; }
        if (g_creep) { r->power = 3; r->range = (int16_t)g_pvars.creep_radius; }
        if (p->effect == 3) {
            ax = (int16_t)ftol((double)g_sin[p->dir] * 64.0 + (double)p->x);
            ay = (int16_t)ftol((double)p->y - (double)g_cos[p->dir] * 64.0);
            r->type = 1;
            r->power = 8;
            r->range = 0xfa;
        }
        r->x = ax;
        r->y = ay;
        if (p->state == 0x15) {
            r->x = (int16_t)ftol(p->x);
            r->y = (int16_t)ftol(p->y);
            r->type = 1;
            r->power = 5;
            r->range = 200;
        }
    }
    player_animate(p);
}

/* ---------------------------------------------------------------- drawing */

void player_collect(Player *p)
{
    /* 0x417070 */
    int sx = ftol(p->x) - g_lv.cam_x, sy = ftol(p->y) - g_lv.cam_y;
    AnimObj *a = &g_anim[p->slot];
    int w = a->rec.w, h = a->rec.h;
    a->rec.wx = sx;
    a->rec.wy = sy;
    sx += a->rec.flip ? -(a->rec.hot_x + w) : a->rec.hot_x;
    sy += a->rec.hot_y;
    if (sx < 640 && 0 < w + sx && sy < 480 && 0 < sy + h) {
        int n = g_ndraw;
        g_draw[n] = &a->rec;
        a->rec.x = sx;
        a->rec.y = sy;
        a->rec.depth = a->depth_off + sy;
        if (!(p->flags & 2)) g_ndraw = ++n;
        if (a->rec.shadow != -1 && g_nshadow < MAX_SHADOW) {
            DrawRec *s = &g_shadow[g_nshadow];
            g_draw[n] = s;
            *s = a->rec;
            g_ndraw++;
            g_nshadow++;
            s->kind = 7;
            s->mode = 3;
        }
    }
}

void cursor_collect(Player *p)
{
    /* 0x40bec0: in mouse mode, a key (0x4a4128 = 1..5) toggles a cross at the input point */
    if (g_input_mode != 0) return;
    if (0 < g_cursor_key && g_cursor_key < 6) {
        g_cursor_key = 6;
        g_cursor_on ^= 1;
    }
    if (!g_cursor_on) return;
    g_cursor_x = ftol(p->x) + p->in_dx - g_lv.cam_x;
    g_cursor_y = ftol(p->y) + p->in_dy - g_lv.cam_y;
    DrawRec info;
    memset(&info, 0, sizeof info);
    rec_set_sprite(&info, SPR_BANK_MISC, 1);
    int w = info.w;
    if (g_cursor_x < 640 && 0 < g_cursor_x + w && g_cursor_y < 480 && 0 < g_cursor_y + w &&
        g_nshadow < MAX_SHADOW) {
        DrawRec *r = &g_shadow[g_nshadow];
        g_draw[g_ndraw] = r;
        r->x = g_cursor_x - w / 2;
        r->y = g_cursor_y - w / 2;
        r->w = w;
        r->h = w;
        r->depth = g_cursor_y;
        r->kind = 1;
        r->sortable = 1;
        r->push = 0;
        r->layer = 0;
        r->shadow = -1;
        r->mode = p->in_mag < 0x20 ? 3 : 4;
        r->clip_x0 = -1;
        r->bank = SPR_BANK_MISC;
        r->frame = 1;
        r->scale_x = r->scale_y = 0x100;
        g_ndraw++;
        g_nshadow++;
    }
}
