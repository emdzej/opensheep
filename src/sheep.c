/* The flock: the per-sheep state machine (0x41caf0), flocking (neighbours 0x41b680, the flock's
   average 0x41b7e0, steering 0x41b9f0), collision with the level (0x41b560: zones and masks, the
   attractors 0x41a910, the special cells 0x41ac20), state changes (0x419f50), animation (0x41e380)
   and the draw records (0x41e6d0). Sheep dwords are named in game.h; the rest are used here:
     d[0x15] pushed  d[0x16] timer (state 0x23)  d[0x17] attached object  d[0x18] counted
     d[0x19] centre dir  d[0x1c] pick-up cooldown  d[0x1d] flags (1 flocks, 2 near a type 5
     attractor, 4 counted by 0x41c970)  d[0x1e] ticks  d[0x1f] neighbours  d[0x20..] neighbour
     (sheep number + 1)  d[0x3e] neighbours' speed  d[0x3f] their heading  d[0x40] heading to their
     centre  d[0x41] closest (number + 1)  d[0x42] its distance  d[0x43..0x44] home  d[0x45] ticks
     away from any attractor  d[0x46..0x47] wandering  d[0x48] hazard ticks | 0x8000 */
#include "sheep.h"
#include "exe.h"
#include "level.h"
#include "levels.h"
#include "paths.h"
#include "play.h"
#include "player.h"
#include "render.h"
#include "rng.h"
#include "sound.h"
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int g_sheep_out;                        /* 0x4a3d80: sheep lost or penned (ends the level) */
int g_hazard;                           /* 0x4bbd58: the hazard cell kind last stood on */
int g_counted;                          /* 0x4bbd5c */
int g_carried;                          /* 0x4a40c4 */
AnimObj *g_carrier;                     /* 0x4a40c8 */
float g_flock_speed;                    /* 0x4bbd00 */
static int centre_x, centre_y;          /* 0x4b99b0, 0x4b99b4 */
static int centre_dist;                 /* 0x4bbd48 */
static float want_speed;                /* 0x4b99bc */
static int want_n;                      /* 0x4bbcfc */

/* 0x44cc6c: turn steps by how far off the target is (16 directions), rebuilt from TURN_SPEED */
static int32_t turn_step[16];
static void turn_defaults(void) { exe_read(turn_step, 0x44cc6c, sizeof turn_step); }
EXE_LOADER(turn_defaults)

static Sheep *sheep_of(int32_t *d) { return (Sheep *)d; }
static int32_t *nb(int v) { return v ? g_sheep[v - 1].d : NULL; }
static int iabs(int v) { return v < 0 ? -v : v; }

int dir16_toward(int x0, int y0, int x1, int y1)
{
    /* 0x4034a0 */
    int dx = x1 - x0, dy = y1 - y0;
    if (dx < 1) {
        if (dy < 1) {
            if (dy != dx && -dy <= -dx) return (dy * 2 == dx || -dx < dy * -2) + 12;
            return (dx * -2 + dy == 0 || dx * -2 < -dy) + 14;
        }
        if (-dy != dx && dy <= -dx) return (dy * -2 != dx && dy * 2 <= -dx) + 10;
        return (dx * -2 - dy != 0 && dy <= dx * -2) + 8;
    }
    if (dy < 1) {
        if (-dy < dx) return (dy * -2 < dx) + 2;
        return -dy < dx * 2;
    }
    if (dy < dx) return (dx <= dy * 2) + 4;
    return (dx * 2 <= dy) + 6;
}

bool actor_walk_to(int32_t *s, int x, int y)
{
    /* 0x41a780: the y step uses the screen-squashed cosine (0x49fdb0) */
    Sheep *a = sheep_of(s);
    int d = dir16_toward(a->ix, a->iy, x, y) * 16;
    a->dir = d;
    g_anim[a->slot].dir = d / 16;
    a->x = (float)((double)g_sin[d] * (double)a->speed + (double)a->x);
    double ny = (double)a->y - (double)g_cos_y[d] * (double)a->speed;
    a->y = (float)ny;
    int ey = ftol(ny) - y;               /* fst: the unrounded y */
    int ex = ftol((double)a->x) - x;
    return iabs(ey) + iabs(ex) < 4;
}

void sheep_turn(int32_t *s, int target)
{
    /* 0x41b950 */
    int ts = g_fvars.turn_speed;
    if (turn_step[7] != ts) {
        for (int k = 0; k < 8; k++) turn_step[7 - k] = turn_step[8 + k] = ts * (8 - k) / 8;
    }
    unsigned d = (unsigned)s[0];
    unsigned diff = ((unsigned)target & 0xff) - d + 0x100;
    if (diff != 0x100) {
        int k = (int)(diff >> 4 & 0xf);
        if (diff & 0x80) { s[0] = (int32_t)((d - (unsigned)turn_step[k]) & 0xff); return; }
        s[0] = (int32_t)(d + (unsigned)turn_step[k]);
    }
    s[0] &= 0xff;
}

void sheep_out(int n)
{
    /* 0x4032c0: a sheep leaves play (lost or penned); the last one ends the level */
    Sheep *s = &g_sheep[n];
    if (s->alive == 2) return;
    s->state = 4;
    s->alive = 1;
    s->d[0x1d] &= ~1;
    anim_kill(&g_anim[n]);
    if (s->d[0x18] == 0) {
        g_sheep_out++;
        s->kind = 0;
        s->d[0x18] = 1;
        if (g_nsheep <= g_sheep_out && g_level != 5 && !g_level_over) {
            int i = anim_new(0, 0, 0x10, -1, 4);
            g_anim[i].move = 0x11;
            g_anim[i].d[0x15] = 0;
        }
    }
}

void sheep_set_kind(int32_t *s, int k, int t)
{
    /* 0x41a520 */
    Sheep *a = sheep_of(s);
    AnimObj *o = &g_anim[a->slot];
    switch (k) {
    case 0:
        if (a->kind == 0x21) { a->kind = 0x13; return; }
        a->kind = 0;
        a->u50 = t != -1 ? t * 0x3c : 0;
        return;
    case 0xd: o->rec.tint |= 1; return;
    case 0xe: o->rec.tint |= 2; return;
    case 0xf: o->rec.tint |= 4; return;
    case 0x17: case 0x19:
        if (a->kind == k) return;
        if (o->d[0x21]) anim_kill(&g_anim[o->d[0x21]]);
        a->kind = k;
        {
            int i = anim_new(ftol(a->x), ftol(a->y), k == 0x17 ? 0x11 : 0x12, 8, 4);
            g_anim[i].move = 7;
            g_anim[i].sub = 7;
            g_anim[i].d[0x15] = a->slot;
            o->d[0x21] = i;
        }
        return;
    case 0x22:
        o->rec.tint = 0;
        a->kind = 0;
        a->u50 = 0;
        if (s[0x17]) anim_kill(&g_anim[s[0x17]]);
        if (s[0x16]) s[0x16] = 0;
        return;
    default:
        a->kind = k;
        a->u50 = t != -1 ? t * 0x3c : 0;
        return;
    }
}

/* An effect object in the sheep's place, drawn as the sheep was (tint, scale, depth). */
static int sheep_effect(Sheep *a, int anim, int dir, int type)
{
    AnimObj *o = &g_anim[a->slot];
    int i = anim_new(ftol(a->x), ftol(a->y), anim, dir, type);
    g_anim[i].rec.tint = o->rec.tint;
    g_anim[i].rec.scale_x = o->rec.scale_x;
    g_anim[i].rec.scale_y = o->rec.scale_y;
    g_anim[i].depth_off = o->depth_off;
    return i;
}

void actor_set_state(int32_t *s, int st)
{
    /* 0x419f50 */
    Sheep *a = sheep_of(s);
    AnimObj *o = &g_anim[a->slot];
    int old = a->state;
    if (old == st) return;
    int old_sub = o->sub;
    a->state = st;
    o->sub = 0;
    switch (st) {
    case 9:
        if (a->kind != 0x13 && a->kind != 0x10 && a->kind != 4) { o->d[0x15] = 0; return; }
        a->state = 0;
        return;
    case 10:
        a->u18 = 0;
        a->timer = 0;
        return;
    case 0xd:
        anim_new(ftol(a->x), ftol(a->y), 0xd, 8, 4);
        return;
    case 0x10:
        if (s[0x18] == 0) {
            s[0x18] = 1;
            s[0x1d] &= ~1;
            g_sheep_out++;
            sound_play_at(o, crt_rand() % 3 + 0x46, 0, 0);
            if (g_nsheep <= g_sheep_out && g_level != 5 && !g_level_over) {
                int i = anim_new(0, 0, 0x10, -1, 4);
                g_anim[i].move = 0x11;
                g_anim[i].d[0x15] = 0;
            }
        }
        return;
    case 0x17: {
        int i = sheep_effect(a, 0xc, o->dir, 1);
        g_anim[i].rec.mode = 3;
        sheep_out(a->slot);
        sound_play_at(o, crt_rand() % 3 + 0x4e, 0, 0);
        return;
    }
    case 0x19:
        if (a->kind != 0x21) {
            if (a->kind == 0x13) sheep_effect(a, 0x2f, (o->dir & ~3) + 2, 1);
            else sheep_effect(a, 0xd, o->dir, 1);
            sheep_out(a->slot);
            return;
        }
        break;
    case 0x1b:
        if (a->kind != 8 && a->kind != 0x14) {
            anim_new(ftol(a->x), ftol(a->y), 0xc, 8, 4);
            int i = anim_new(ftol(a->x), ftol(a->y), 0x11, o->dir, 1);
            g_anim[i].rec.mode = 3;
            sheep_out(a->slot);
            sound_play_at(o, crt_rand() % 3 + 0x3d, 0, 0);
            return;
        }
        break;
    case 0x1d:
        sheep_effect(a, 0x1a, o->dir, 1);
        sheep_out(a->slot);
        sound_play_at(o, crt_rand() % 3 + 0x4b, 0, 0);
        return;
    case 0x1e:
        sheep_kind_drop(s);
        o->d[0x15] = ftol(a->x);
        o->d[0x16] = ftol(a->y);
        return;
    case 0x22:
        if (a->kind == 0x21) break;
        return;
    case 0x23:
        if (a->kind != 8 && a->kind != 0x14 && s[0x16] == 0) {
            s[0x16] = 900;
            int i = anim_new(a->ix, a->iy, 0x15, 8, 4);
            s[0x17] = i;
            g_anim[i].move = 0x1f;
            g_anim[i].d[0x15] = a->slot;
        }
        break;
    default:
        return;
    }
    /* the change does not take */
    a->state = old;
    o->sub = old_sub;
}

void sheep_kind_drop(int32_t *s)
{
    /* 0x41c5b0: a sheep carrying something (kinds 1, 0x11) drops it */
    Sheep *a = sheep_of(s);
    if (a->kind == 0x11) {
        anim_new(a->ix, a->iy, 0x34, g_anim[a->slot].dir, 1);
        a->kind = 0;
    }
    if (a->kind == 1) {
        anim_new(a->ix, a->iy, 0x33, g_anim[a->slot].dir, 1);
        a->kind = 0;
    }
}

void sheep_launch(int32_t *s, int dir, int count, int height, int then, int speed)
{
    /* 0x41a820: d[0x15] counts down from count, d[0x16] count, d[0x18] height, d[0x1a] then,
       d[0x19] landed once, d[0x1b] speed x10 */
    Sheep *a = sheep_of(s);
    int slot = a->slot;
    AnimObj *o = &g_anim[slot];
    a->state = 1;
    a->dir = dir;
    o->d[0x15] = o->d[0x16] = count;
    o->fy = a->y;
    o->d[0x18] = height;
    o->d[0x1a] = then;
    o->d[0x19] = 0;
    o->d[0x1b] = speed;
    o->d[0x1d] = 0;
    if (slot < MAX_SHEEP) {
        if (a->kind == 0) {
            anim_set(slot, 2, -1);
            a->anim[0] = a->anim[1] = a->anim[2] = 2;
        }
    } else {
        anim_set(slot, 3, -1);
    }
}

void sheep_launch_sound(int32_t *s, int dir, int count, int height, int then, int speed)
{
    /* 0x41a8c0 */
    sheep_launch(s, dir, count, height, then, speed);
    sound_play_at(&g_anim[sheep_of(s)->slot], 0x7d, 0, 0);
}

/* ---------------------------------------------------------------- collision */

static void sheep_attractors(Sheep *a, int act)
{
    /* 0x41a910 */
    a->d[0x1d] &= ~2;
    for (int i = 0; i < g_nattract; i++) {
        Attractor *t = &g_attract[i];
        if (t->type <= 2) continue;
        int ix = a->ix, iy = a->iy;
        if (iabs(t->x - ix) + iabs(t->y - iy) >= t->range) continue;
        switch (t->type) {
        case 3:
            if (a->d[0x18] == 0 && a->state != 7 && a->state != 10 && a->kind != 0x1b && a->kind != 0x1d) {
                sheep_launch_sound(a->d, dir_of(ix - t->x, iy - t->y), (t->power * 5 + 5) * 0x32,
                                   (t->power * 5 + 5) * 2, 10, 0x1e);
                g_anim[a->slot].d[0x1d] = 8;
                a->state = 7;
            }
            break;
        case 4: case 5:
            break;
        case 6: if (act && a->kind != 0x1c && a->kind != 0x1b) actor_set_state(a->d, 0x17); break;
        case 7: if (act && a->kind != 0x14) actor_set_state(a->d, 0x1b); break;
        case 8:
            if (act) {
                sheep_out(a->slot);
                anim_new(a->ix, a->iy, 0x44, 8, 3);
            }
            break;
        case 9: if (act && a->kind != 0x14) actor_set_state(a->d, 0x1d); break;
        case 10:
            if (!act) break;
            switch (t->power) {
            case 0:
                actor_set_state(a->d, 9);
                g_anim[a->slot].d[0x15] = 0;
                break;
            case 1: if (a->alive == 0) actor_set_state(a->d, 0x19); break;
            case 2:
                if (a->state != 0x1f) a->dir = dir_of(ix - t->x, iy - t->y);
                actor_set_state(a->d, 0x1f);
                a->speed = 5.0f;
                break;
            case 3: actor_set_state(a->d, 0x22); break;
            case 4:
                if (a->state != 10) {
                    actor_set_state(a->d, 10);
                    g_anim[a->slot].rec.tint = 5;
                }
                break;
            case 5: actor_set_state(a->d, 0x10); break;
            }
            break;
        default:
            fprintf(stderr, "Unknown attractor %d (Sheep)\n", t->type);
            break;
        }
    }
}

static void trigger(int16_t id, int special, int who)
{
    if (!id) return;
    int s = g_obj_by_id[id];
    g_anim[s].d[0x22] = special;
    g_anim[s].d[0x21] = who;
}

void sheep_specials(int x, int y, Sheep *a, int act)
{
    /* 0x41ac20 */
    uint32_t c = g_lv.layer3[(y / TILE_H) * g_lv.w + x / TILE_W];
    if (!(c & 0xff)) return;
    int si = c >> 8 & 0xff, sdir = (int32_t)c >> 16 & 0xf;
    if (g_lv.tab1[(x % TILE_W) + (y % TILE_H - TILE_H + (int)(c & 0xff) * TILE_H) * TILE_W] != 1) return;
    Special *s = &g_special[si];
    if (s->active) return;
    if (act) s->count++;
    AnimObj *o = &g_anim[a->slot];
    int n = a->slot;
    switch (s->type) {
    case 0: case 2: case 4: case 6: case 0x15:
        return;
    case 1: case 3: case 0xd:
        break;
    case 5:
        a->push_dir = sdir;
        a->hit = 2;
        return;
    case 7:
        g_hazard = 0;
        if (a->kind < 7 || (8 < a->kind && a->kind != 10)) a->d[0x48] |= 0x8000;
        return;
    case 8:
        if (a->kind != 6) { a->push_dir = sdir; a->hit = 2; }
        return;
    case 9:
        if (!act || a->state == 1) return;
        {
            unsigned d = (unsigned)(a->dir - sdir * 0x10) & 0xff;
            if (0x3f < d && d < 0xc1) return;
        }
        a->hit = -1;
        a->push_dir = sdir;
        sheep_launch_sound(a->d, sdir * 0x10, s->a[0] * 10, s->a[1], s->a[4], s->a[3]);
        a->face = a->dir;
        trigger(s->a[2], si, n);
        return;
    case 10:
        if (act && a->hit != 2) {
            a->pushed = 1;
            a->push_dir = sdir;
            a->hit = -1;
            o->speed = (float)((double)s->a[0] * (double)0.1f);
        }
        return;
    case 0xb:
        if (s->a[2] != 0) {
            g_saved++;
            actor_set_state(a->d, s->a[2]);
            o->d[0x15] = s->a[3];
            o->d[0x16] = s->a[4];
            o->d[0x17] = s->a[5];
            return;
        }
        g_saved++;
        a->state = 4;
        a->alive = 1;
        a->hit = -1;
        if (a->d[0x17]) anim_kill(&g_anim[a->d[0x17]]);
        return;
    case 0xc:
        if (act && s->a[3] != 2) {
            trigger(s->a[1], si, n);
            o->d[0x23] = s->a[0];
            o->d[0x15] = s->a[2];
            actor_set_state(a->d, 8);
            a->push_dir = sdir;
            a->hit = -1;
        }
        return;
    case 0xe:
        actor_set_state(a->d, s->a[0]);
        if (s->a[3]) {
            a->pushed = 1;
            a->push_dir = sdir;
            o->speed = (float)((double)s->a[3] * (double)0.1f);
        }
        trigger(s->a[1], si, n);
        trigger(s->a[2], si, n);
        return;
    case 0xf:
        if (!act) return;
        a->dir = (sdir + 8) * 0x10;
        a->dir = ((rng_next() & 3) - 1 + a->dir) & 0xff;
        a->hit = -1;
        a->push_dir = (sdir - 8) & 0xf;
        actor_set_state(a->d, 0x1f);
        a->speed = (float)((double)s->a[0] * (double)0.1f);
        trigger(s->a[1], si, n);
        return;
    case 0x10:
        if (a->state != 0x15 && a->kind != 0x12) {
            g_hazard = 1;
            a->d[0x48] |= 0x8000;
            a->anim[0] = a->anim[1] = a->anim[2] = 6;
            if (rng_next() % 300 == 0) sound_play_at(o, (crt_rand() & 1) + 0x80, 0, 0);
        }
        return;
    case 0x11:
        a->d[0x1d] |= 4;
        return;
    case 0x12:
        if (!act) return;
        a->pushed = 1;
        a->push_dir = sdir;
        a->hit = -1;
        o->speed = (float)((double)s->a[4] * (double)0.1f);
        sheep_set_kind(a->d, s->a[0], s->a[1]);
        trigger(s->a[2], si, n);
        trigger(s->a[3], si, n);
        return;
    case 0x13:
        if (act) {
            a->state = 0x21;
            a->push_dir = sdir;
            a->hit = -1;
            o->speed = (float)((double)s->a[0] * (double)0.1f);
        }
        return;
    case 0x14:
        actor_set_state(a->d, s->a[2]);
        g_anim[a->slot].d[0x15] = s->a[3];
        return;
    case 0x16:
        if (!act) return;
        if (a->kind == 0x1c) {
            unsigned d = (unsigned)(sdir * 0x10 - a->dir) & 0xff;
            int pd = sdir;
            if (0x40 < d && d < 0xc0) pd = (sdir - 8) & 0xf;
            a->push_dir = pd;
            a->pushed = 1;
            a->hit = -1;
            o->speed = (float)((double)s->a[0] * (double)0.1f);
            return;
        }
        if (a->hit != 2) {
            a->pushed = 1;
            a->push_dir = sdir;
            a->hit = -1;
            o->speed = (float)((double)s->a[0] * (double)0.1f);
        }
        return;
    case 0x17:
        if (a->kind != 8) {
            g_hazard = 2;
            a->d[0x48] |= 0x8000;
            a->anim[0] = a->anim[1] = a->anim[2] = 6;
            if (rng_next() % 0xb4 == 0) sound_play_at(o, (crt_rand() & 1) + 0x89, 0, 0);
        }
        return;
    case 0x18:
        a->u18 = s->a[0];
        return;
    case 0x19:
        if (o->d[0x1e] == s->a[6]) {
            actor_set_state(a->d, s->a[2]);
            o->d[0x15] = s->a[3];
            o->d[0x16] = s->a[4];
            o->d[0x17] = s->a[5];
        }
        return;
    case 0x1a:
        if (a->kind != s->a[6]) return;
        break;
    default:
        {
            /* the original's trace (0x422290) logs it every time; once per type here */
            static uint8_t said[256];
            if (!said[(uint8_t)s->type]++) fprintf(stderr, "Unknown specific type encountered %d\n", s->type);
        }
        break;
    }
    trigger(s->a[0], si, n);
    trigger(s->a[1], si, n);
    trigger(s->a[2], si, n);
    if (s->type == 0xd) {
        actor_set_state(a->d, s->a[4]);
        o->d[0x15] = s->a[5];
        o->d[0x16] = s->a[6];
        return;
    }
    trigger(s->a[3], si, n);
}

void sheep_collide(int x, int y, int32_t *s, int act)
{
    /* 0x41b560: as the shepherd's 0x417ef0 */
    Sheep *a = sheep_of(s);
    a->zone = -1;
    a->hit = -1;
    uint32_t z = desc_u32(DESC_ZONES);
    if (z) {
        int n = exe_s32(z);
        for (int i = 0; i < n; i++) {
            uint32_t r = z + 4 + 20 * (uint32_t)i;
            if (exe_s32(r + 4) <= x && exe_s32(r + 8) <= y && x <= exe_s32(r + 12) && y <= exe_s32(r + 16)) {
                actor_set_state(s, exe_s32(r));
                a->zone = i;
                break;
            }
        }
    }
    uint16_t c = g_lv.layer1[(y / TILE_H) * g_lv.w + x / TILE_W];
    int t = c & 0xff;
    if (t) {
        int8_t m = (int8_t)g_lv.tab1[(x % TILE_W) + (y % TILE_H - TILE_H + t * TILE_H) * TILE_W];
        if (m == 1) { a->hit = 0; a->push_dir = (c & 0xf00) >> 8; }
        else if (m == 2) a->hit = 2;
    }
    sheep_attractors(a, act);
    sheep_specials(x, y, a, act);
}

static void sheep_unstick(Sheep *a)
{
    /* 0x41c080: as the shepherd's, then turn toward the free direction */
    float r = 0.0f;
    int found = -1, lx = 0, ly = 0;
    do {
        r = (float)((double)r + 1.0);
        int p = a->dir, q = a->dir;
        for (int k = 0; k < 8 && found == -1; k++) {
            p = (p - 0x10) & 0xff;
            q = (q + 0x10) & 0xff;
            lx = ftol((double)r * (double)g_sin[p] + (double)a->x);
            ly = ftol((double)a->y - (double)r * (double)g_cos[p]);
            sheep_collide(lx, ly, a->d, 0);
            if (a->hit != 2) { found = p; break; }
            lx = ftol((double)r * (double)g_sin[q] + (double)a->x);
            ly = ftol((double)a->y - (double)r * (double)g_cos[q]);
            sheep_collide(lx, ly, a->d, 0);
            if (a->hit != 2) found = q;
        }
    } while (found == -1);
    a->x = (float)lx;
    a->y = (float)ly;
    sheep_turn(a->d, found);
}

/* ---------------------------------------------------------------- flocking */

static void sheep_neighbours(Sheep *a)
{
    /* 0x41b680: the NUM_NEIGHBOURS nearest flocking sheep within NEIGHBOUR_DIST, nearest first */
    a->d[0x1f] = 0;
    a->d[0x41] = 0;
    if (!(a->d[0x1d] & 1)) return;
    int who[30], dist[30];
    for (int i = 0; i < 30; i++) {
        who[i] = 0;
        dist[i] = 0x80000;
    }
    for (int j = 0; j < MAX_SHEEP; j++) {
        Sheep *o = &g_sheep[j];
        if (o == a || !(o->d[0x1d] & 1)) continue;
        int dd = iabs(a->ix - o->ix) + iabs(a->iy - o->iy);
        if (dd >= g_fvars.neighbour_dist) continue;
        for (int k = 0; k < 0x1d; k++) {
            if (dd < dist[k]) {
                for (int m = 0x1d; m > k; m--) {
                    who[m] = who[m - 1];
                    dist[m] = dist[m - 1];
                }
                dist[k] = dd;
                who[k] = j + 1;
                break;
            }
        }
    }
    for (int k = 0; k < g_fvars.num_neighbours; k++) {
        a->d[0x20 + k] = who[k];
        if (who[k]) a->d[0x1f]++;
    }
    if (dist[0] < g_fvars.too_close_dist) {
        a->d[0x42] = dist[0];
        a->d[0x41] = who[0];
    }
}

static void sheep_average(Sheep *a)
{
    /* 0x41b7e0 */
    int n = a->d[0x1f];
    if (n == 0) {
        a->d[0x3e] = 0;
        a->d[0x3f] = -1;
        a->d[0x40] = -1;
        centre_x = a->ix;
        centre_y = a->iy;
        centre_dist = 0;
        return;
    }
    double sp = 0.0;
    int sx = 0, sy = 0, cx = 0, cy = 0;
    for (int k = 0; k < n; k++) {
        Sheep *o = (Sheep *)nb(a->d[0x20 + k]);
        sp += (double)o->speed;
        sx += g_isin[o->dir];
        sy -= g_icos[o->dir];
        cx += o->ix - a->ix;
        cy += o->iy - a->iy;
    }
    float avg = (float)(sp / (double)(float)n);
    memcpy(&a->d[0x3e], &avg, 4);
    a->d[0x3f] = dir_of(sx / 256, sy / 256);
    int ax = cx / n, ay = cy / n;
    centre_x = a->ix + ax;
    centre_y = a->iy + ay;
    centre_dist = iabs(ay) + iabs(ax);
    a->d[0x40] = centre_dist <= g_fvars.too_close_dist * 2 ? -1 : dir_of(ax, ay);
}

static void sheep_steer(Sheep *a)
{
    /* 0x41b9f0: sum the pulls (closest neighbour, slope, attractors, the flock's heading and
       centre, or home when alone), turn toward them and ease the speed toward the average wanted */
    want_speed = 0.0f;
    want_n = 0;
    int lx = 0, ly = 0;
    if (a->d[0x41]) {
        Sheep *c = (Sheep *)nb(a->d[0x41]);
        int d = dir_of(a->ix - c->ix, a->iy - c->iy);
        lx = g_isin[d] * g_fvars.bias_closest / 256;
        ly = -(g_icos[d] * g_fvars.bias_closest / 256);
        if (a->d[0x42] < g_fvars.bias_closest / 2 && a->speed < 0.5f) a->speed = 0.5f;
    }
    if (a->hit == 0) {
        int w = a->push_dir * 0x10;
        unsigned diff = (unsigned)(w - a->dir) & 0xff;
        if (diff < 0x48 || 0xb8 < diff) {
            lx += g_isin[w] * -0x28 / 256;
            ly += g_icos[w] * 0x28 / 256;
        }
    }
    a->d[0x1d] &= ~2;
    bool seen = false;
    for (int i = 0; i < g_nattract; i++) {
        Attractor *t = &g_attract[i];
        if (t->type == 0) continue;
        int dd = iabs(t->x - a->ix) + iabs(t->y - a->iy);
        if (dd < t->range * 2) {
            seen = true;
            if (i == g_player.attract) {
                g_flock_sx += a->ix;
                g_flock_sy += a->iy;
                g_flock_n++;
                g_flock_speed = (float)((double)g_flock_speed + (double)a->speed);
            }
        }
        if (dd >= t->range) continue;
        if (t->type == 1) {
            int d = dir_of(t->x - a->ix, t->y - a->iy);
            lx += g_isin[d] * t->power / 64;
            ly -= g_icos[d] * t->power / 64;
            unsigned diff = (unsigned)(a->dir - d) & 0xff;
            if (diff < 0x40 || 0xc0 < diff) {
                if (t->range / 4 < dd) {
                    want_speed = (float)((double)want_speed + 6.0);
                    want_n += 2;
                    continue;
                }
                want_speed = (float)((double)want_speed + 1.0);
            }
            want_n++;
        } else if (t->type == 2 || t->type == 5) {
            if (t->type == 5) a->d[0x1d] |= 2;
            int d = dd < t->range / 3 ? dir_of(a->ix - t->x, a->iy - t->y)
                                      : dir_of(centre_x - t->x, centre_y - t->y);
            lx += g_isin[d] * t->power * 0x40 / 4096;
            ly -= g_icos[d] * t->power * 0x40 / 4096;
            unsigned diff = (unsigned)(a->dir - d) & 0xff;
            if (diff < 0x40 || 0xc0 < diff) {
                if (t->range / 4 <= dd) {
                    want_speed = (float)((double)want_speed + 3.0);
                    want_n++;
                } else {
                    want_speed = (float)((double)want_speed + 9.0);
                    want_n += 3;
                }
            } else {
                want_n += 2;
            }
        }
    }
    if (g_nattract < 1 || !seen) a->d[0x45]++;
    else a->d[0x45] = 0;
    bool count = true;
    if (a->d[0x45] < 300) {
        a->d[0x43] = centre_x;
        a->d[0x44] = centre_y;
        a->d[0x46] = 1;
        a->d[0x47] = (int)(rng_next() & 0xff);
        if (a->d[0x1f] == 0) {
            count = false;
        } else {
            int d = a->d[0x3f];
            if (d != -1) {
                lx += g_isin[d] * g_fvars.bias_direction / 256;
                ly -= g_icos[d] * g_fvars.bias_direction / 256;
            }
            d = a->d[0x40];
            if (d != -1) {
                lx += g_isin[d] * g_fvars.bias_center / 256;
                ly -= g_icos[d] * g_fvars.bias_center / 256;
            }
            float avg;
            memcpy(&avg, &a->d[0x3e], 4);
            want_speed = (float)((double)want_speed + (double)avg);
        }
    } else {
        if (--a->d[0x47] < 0) {
            a->d[0x47] = (int)(rng_next() & 0xff) + 0x40;
            a->d[0x46] ^= 1;
        }
        if (a->d[0x46] != 0) {
            if (a->d[0x46] == 1) want_n++;
            count = false;
        } else {
            int hx = a->d[0x43] - a->ix, hy = a->d[0x44] - a->iy;
            if (g_fvars.wander_radius < iabs(hx) + iabs(hy)) {
                int d = dir_of(hx, hy);
                lx += g_isin[d] / 4;
                ly -= g_icos[d] / 4;
            }
            double amble = (double)(float)g_fvars.amble_speed * (double)0.01f;
            if (amble <= (double)a->speed) count = false;
            else want_speed = (float)(amble + (double)want_speed);
        }
    }
    if (count) want_n++;
    a->d[0x1e]++;
    if (lx != 0 || ly != 0) {
        sheep_turn(a->d, dir_of(lx, ly));
        a->face = a->dir;
    }
    a->d[0x19] = a->d[0x40];
    double target = (double)want_speed / (double)(float)want_n - (double)0.01f;
    if (target < 0.0) target = 0.0;
    double accel = (double)(float)g_fvars.sheep_accel * (double)0.001f;
    if ((double)a->speed <= target) {
        double v = accel + (double)a->speed;
        a->speed = (float)v;
        if (target < v) a->speed = (float)target;
    } else {
        double v = (double)a->speed - accel;
        a->speed = (float)v;
        if (v < target) a->speed = (float)target;
    }
    double cap = (double)(float)g_fvars.sheep_speed * (double)0.1f;
    if ((double)a->speed <= cap) return;
    a->speed = (float)cap;
}

static bool sheep_follow(Sheep *a)
{
    /* 0x41c290: the follow-the-leader power-up (effect 4): a sheep in the chain walks after the one
       ahead of it; one near the chain's tail joins it */
    int32_t *tail = g_chain[31][0];
    int32_t *ahead = g_chain[a->slot][0];
    double accel = (double)(float)g_fvars.sheep_accel * (double)0.001f;
    if (ahead == NULL) {
        if (tail == NULL) return false;
        int dd = iabs(a->ix - tail[0xc]) + iabs(a->iy - tail[0xd]);
        if (dd >= 200) return false;
        sheep_turn(a->d, dir_of(tail[0xc] - a->ix, tail[0xd] - a->iy));
        a->face = a->dir;
        if (dd < 0x21) {
            if (a->speed <= 0.0f) {
                double v = accel + (double)a->speed;
                a->speed = (float)v;
                if (0.0 < v) a->speed = 0.0f;
            } else {
                double v = (double)a->speed - accel;
                a->speed = (float)v;
                if (v < 0.0) a->speed = 0.0f;
            }
        } else if ((double)a->speed <= (double)2.99f) {
            double v = accel + (double)a->speed;
            a->speed = (float)v;
            if (v > (double)2.99f) a->speed = 2.99f;
        } else {
            double v = (double)a->speed - accel;
            a->speed = (float)v;
            if (v < (double)2.99f) a->speed = 2.99f;
        }
        if (0x16 < dd && dd < 0x2a) {
            g_chain[a->slot][0] = tail;
            g_chain[a->slot][1] = NULL;
            g_chain[tail[9]][1] = a->d;
            g_chain[31][0] = a->d;
        }
        return true;
    }
    int dd = iabs(a->ix - ahead[0xc]) + iabs(a->iy - ahead[0xd]);
    if (dd < 200) {
        sheep_turn(a->d, dir_of(ahead[0xc] - a->ix, ahead[0xd] - a->iy));
        a->face = a->dir;
        a->speed = dd < 0x21 ? 0.0f : 3.0f;
        return true;
    }
    g_chain[ahead[9]][1] = NULL;
    g_chain[a->slot][0] = NULL;
    g_chain[a->slot][1] = NULL;
    return false;
}

static void sheep_pickup(Sheep *a)
{
    /* 0x41c1b0: the shepherd creeping onto a sheep picks it up */
    if (0 < a->d[0x1c]) { a->d[0x1c]--; return; }
    Player *p = &g_player;
    if (a->kind != 0 || p->state == 0x14 || p->state == 0x15) return;
    int dx = ftol((double)p->x - (double)a->x), dy = ftol((double)p->y - (double)a->y);
    if (iabs(dx) + iabs(dy) < 0x10 && g_creep) {
        p->state = 0x14;
        p->speed = 0.0f;
        p->anim[0] = 7;
        AnimObj *o = &g_anim[p->slot];
        o->rec.scale_x = o->rec.scale_y = 0x100;
        anim_set(p->slot, 7, -1);
        g_anim[a->slot].d[0x15] = p->dir;
        a->state = 0x14;
    }
}

void sheep_walk(Sheep *a)
{
    /* 0x41c480 */
    a->d[0x1d] |= 1;
    AnimObj *o = &g_anim[a->slot];
    o->rec.shadow_dy = 0;
    o->depth_off = 0;
    if (!sheep_follow(a)) {
        sheep_neighbours(a);
        sheep_average(a);
        sheep_steer(a);
    }
    float nx = (float)((double)g_sin[a->dir] * (double)a->speed + (double)a->x);
    float ny = (float)((double)a->y - (double)g_cos[a->dir] * (double)a->speed);
    sheep_collide(ftol((double)nx), ftol((double)ny), a->d, 1);
    if (a->hit == -1 || a->hit == 0) {
        a->x = nx;
        a->y = ny;
    } else if (a->hit == 2) {
        if (a->alive == 0) {
            sheep_turn(a->d, ((a->push_dir - 8) & 0xf) << 4);
            sheep_unstick(a);
        }
        a->hit = 0;
    }
    a->ix = ftol((double)a->x);
    a->iy = ftol((double)a->y);
    sheep_pickup(a);
}

static void sheep_hazard(Sheep *a)
{
    /* 0x41c970: ticks on a hazard cell (bit 15 set by the cell this tick) */
    if (a->alive == 2) return;
    unsigned t = 0;
    if (a->d[0x48] & 0x8000) {
        t = (a->d[0x48] & 0x7fff) + 1;
        AnimObj *o = &g_anim[a->slot];
        if (g_hazard == 0) {
            if (0x1e < t) {
                anim_new(ftol(a->x), ftol(a->y), 0xc, 8, 4);
                int i = anim_new(ftol(a->x), ftol(a->y), 0x11, o->dir, 1);
                g_anim[i].rec.mode = 3;
                sheep_out(a->slot);
            }
        } else if (g_hazard == 1) {
            if (0x78 < t) {
                anim_new(ftol(a->x), ftol(a->y), 0x25, o->dir, 1);
                sheep_out(a->slot);
                sound_play_at(o, crt_rand() % 3 + 0x46, 0, 0);
            }
        } else if (g_hazard == 2 && 0x78 < t) {
            actor_set_state(a->d, 0x19);
        }
    }
    a->d[0x48] = (int32_t)t;
    if (a->alive != 2 && (a->d[0x1d] & 4)) {
        g_counted++;
        a->d[0x1d] &= ~4;
    }
}

/* ---------------------------------------------------------------- the tick (0x41caf0) */

static void sheep_jump(Sheep *a, int n)
{
    /* state 1 (inline in 0x41caf0); state 7 (0x41c630) is the same arc with a knock-back spin */
    AnimObj *o = &g_anim[n];
    a->d[0x1d] &= ~1;
    if (a->kind == 0) a->anim[0] = a->anim[1] = a->anim[2] = 2;
    double f = o->d[0x19] == 0 ? (double)0.1f : (double)0.0588235f;
    a->speed = (float)((double)(float)o->d[0x1b] * f);
    a->x = (float)((double)g_sin[a->dir] * (double)a->speed + (double)a->x);
    o->fy = (float)((double)o->fy - (double)g_cos[a->dir] * (double)a->speed);
    o->d[0x15] += ftol((double)a->speed * -10.0);
    int t = (o->d[0x15] << 8) / o->d[0x16];
    double h = (double)o->fy - (double)(float)o->d[0x18] * (double)g_sin[t / 2];
    a->y = (float)h;
    o->rec.shadow_dy = ftol((double)o->fy - h);
    if (o->d[0x19] == 0) {
        int f16 = (t - 1) / o->nframes;
        if (f16 < o->nframes) o->frame = f16;
    } else {
        o->frame = 0;
    }
    o->countdown = 4;
    anim_set_frame(o);
    o->rec.mode = o->rec.shadow_dy < 0x19 ? 1 : 2;
    if (0x96 < o->d[0x18] && 0x28 < t && t < 0xe6 && (++o->d[0x17] & 7) == 0)
        anim_new(ftol(a->x), ftol(a->y), 0xc, 8, 4);
    if (g_anim[a->slot].d[0x15] < 1) {
        sheep_kind_drop(a->d);
        if (o->d[0x19] == 0 && o->d[0x1a] == 0) {
            /* bounce once, a quarter as long and as high */
            o->d[0x16] /= 4;
            o->d[0x15] = o->d[0x16];
            o->fy = a->y;
            o->d[0x19] = 1;
            o->d[0x18] /= 4;
            int i = anim_new(ftol(a->x), ftol(a->y), 0xc, 8, 4);
            g_anim[i].rec.scale_x = g_anim[i].rec.scale_y = 0x80;
            sheep_collide(a->ix, a->iy, a->d, 1);
            if (a->state != 1) {
                int then = o->d[0x1a];
                o->rec.shadow_dy = 0;
                o->depth_off = 0;
                actor_set_state(a->d, then);
            }
        } else {
            actor_set_state(a->d, o->d[0x1a]);
            a->d[0x1d] |= 1;
        }
    }
}

void sheep_knocked(Sheep *a, int n)
{
    /* 0x41c630 */
    AnimObj *o = &g_anim[n];
    float ox = a->x, oy = a->y;
    a->d[0x1d] &= ~1;
    if (a->kind == 0) a->anim[0] = a->anim[1] = a->anim[2] = 2;
    double f = o->d[0x19] == 0 ? (double)0.1f : (double)0.0588235f;
    a->speed = (float)((double)(float)o->d[0x1b] * f);
    a->x = (float)((double)g_sin[a->dir] * (double)a->speed + (double)a->x);
    o->fy = (float)((double)o->fy - (double)g_cos[a->dir] * (double)a->speed);
    a->anim[1] = a->anim[2] = 2;
    o->d[0x15] += ftol((double)a->speed * -10.0);
    int t = (o->d[0x15] << 8) / o->d[0x16];
    double h = (double)o->fy - (double)(float)o->d[0x18] * (double)g_sin[t / 2];
    a->y = (float)h;
    o->rec.shadow_dy = ftol((double)o->fy - h);
    if (o->d[0x19] == 0) {
        int f16 = (t - 1) / 16;
        if (f16 < o->nframes) o->frame = f16;
    } else {
        o->frame = 0;
    }
    o->countdown = 4;
    anim_set_frame(o);
    o->rec.mode = o->rec.shadow_dy < 0x19 ? 1 : 2;
    if (0x96 < o->d[0x18] && 0x28 < t && t < 0xe6 && (++o->d[0x17] & 7) == 0)
        anim_new(ftol(a->x), ftol(a->y), 0xc, 8, 4);
    a->face = (o->d[0x1d] + a->face) & 0xff;
    if (g_anim[a->slot].d[0x15] < 1) {
        sheep_kind_drop(a->d);
        if (o->d[0x19] == 0) {
            o->d[0x16] /= 4;
            o->d[0x15] = o->d[0x16];
            o->fy = a->y;
            o->d[0x19] = 1;
            o->d[0x18] /= 4;
        } else {
            a->dir = a->face;
            actor_set_state(a->d, o->d[0x1a]);
            o->rec.shadow_dy = 0;
            a->d[0x1d] |= 1;
        }
    }
    sheep_collide(ftol((double)a->x), ftol((double)a->y), a->d, 0);
    if (a->hit == 2) {
        a->x = ox;
        a->y = oy;
        a->dir = a->face;
        actor_set_state(a->d, o->d[0x1a]);
        o->rec.shadow_dy = 0;
        o->rec.mode = 1;
        a->d[0x1d] |= 1;
    }
}

void sheep_pathed(Sheep *a, int n)
{
    /* 0x41c880: state 8, walked to a path's start, then along it */
    AnimObj *o = &g_anim[n];
    a->d[0x1d] &= ~1;
    if (o->sub == 0) {
        path_start(o->d[0x23]);
        a->speed = 1.0f;
        if (actor_walk_to(a->d, g_path_x, g_path_y)) o->sub = 1;
    } else if (o->sub == 1) {
        int end = path_follow(o);
        a->dir = o->dir << 4;
        a->x = (float)o->x;
        a->y = (float)o->y;
        if (end) {
            a->state = 0;
            a->d[0x1d] |= 1;
            if (o->d[0x15]) {
                int s = g_obj_by_id[o->d[0x15]];
                g_anim[s].d[0x22] = a->slot;
                g_anim[s].d[0x21] = a->slot;
            }
        }
    }
    a->face = a->dir;
}

static void sheep_slide(Sheep *a, int n, bool spinning)
{
    /* states 0xf and 0x16: sliding along the neighbours' heading, slowing by 1 a tick */
    a->anim[0] = a->anim[1] = a->anim[2] = 2;
    g_anim[n].frame = 3;
    a->d[0x1d] &= ~1;
    sheep_turn(a->d, a->d[0x3f]);
    float sx = g_sin[a->d[0x3f]], cy = g_cos[a->d[0x3f]];
    float sp = a->speed, x = a->x, y = a->y;
    float v;
    memcpy(&v, &a->d[0x3e], 4);
    v = (float)((double)v - 1.0);
    memcpy(&a->d[0x3e], &v, 4);
    sheep_collide(ftol((double)sx * (double)sp + (double)x), ftol((double)y - (double)cy * (double)sp), a->d, 0);
    bool stop = a->hit == 2;
    if (!stop) {
        a->x = (float)((double)sx * (double)sp + (double)x);
        a->y = (float)((double)y - (double)cy * (double)sp);
        memcpy(&v, &a->d[0x3e], 4);
        stop = a->hit == 2 || v <= 0.0f;
    }
    if (stop) {
        a->state = 0;
        a->speed = (float)((double)a->speed * 0.5);
        g_anim[a->slot].depth_off = 0;
        a->d[0x1d] |= 1;
    }
    if (spinning) a->y = (float)((double)a->y + 0.5);
}

static void sheep_spin_on_player(Sheep *a)
{
    /* state 0xb: spins on the shepherd's spot, u18 turning by the timer (wrapping 0..359) until
       within 4 px, when the timer becomes 0x42. The original first clears bit 0 of d1d of the
       sheep numbered by the scratch 0x4adedc (left by the shepherd's walk or the draw loop,
       usually 30: past the flock); not done here. */
    a->face = (a->face + 0x10) & 0xff;
    a->dir = a->face;
    if (a->timer == 0x42) return;
    int dx = a->ix - ftol((double)g_player.x), dy = a->iy - ftol((double)g_player.y);
    if (sqrt((double)(dy * dy) + (double)(dx * dx)) - 4.0 <= 0.0) a->timer = 0x42;
    a->x = (float)((double)g_player.x + 0.0);
    a->y = (float)((double)g_player.y + 0.0);
    int t = a->timer;
    if (t > 8) t -= 0x11;
    a->u18 += t;
    if (a->u18 > 0x167) a->u18 -= 0x168;
    if (a->u18 < 0) a->u18 += 0x168;
}

static int sheep_beam_step(Sheep *a)
{
    /* 0x411800: drawn to (o d18, d19), scaled by its distance (d17 = 0x40000 / the first one);
       there 1, with d1c set by the kind u18 (0 -32..31, 1 any, 2 the timer, 4 150..213; 5 hides it) */
    AnimObj *o = &g_anim[a->slot];
    int32_t *d = o->d;
    a->speed = 0.0f;
    int dx = ftol((double)a->x) - d[0x18], dy = ftol((double)a->y) - d[0x19];
    double dist = sqrt((double)(dy * dy) + (double)(dx * dx));
    if (d[0x17] == 0) {
        int r = ftol(dist);
        d[0x17] = r ? 0x40000 / r : 0;  /* the original divides by zero when already there */
    }
    if (dist - 0.5 <= 0.0) {
        a->x = (float)d[0x1a];
        a->y = (float)d[0x1b];
        switch (a->u18) {
        case 0: d[0x1c] = ((int)(rng_next() & 0x3f) - 0x20) & 0xff; break;
        case 1: d[0x1c] = (int)(rng_next() & 0xff); break;
        case 2: d[0x1c] = a->timer; break;
        case 4: d[0x1c] = (int)(rng_next() & 0x3f) + 0x96; break;
        case 5: o->rec.scale_x = o->rec.scale_y = 0; break;
        }
        return 1;
    }
    int v = ftol(dist) * d[0x17];
    int s = (v + (v < 0 ? 0x3ff : 0)) >> 10;
    if (d[0x17] == -1) s = 0x100;
    o->rec.scale_x = o->rec.scale_y = s;
    a->x = (float)((double)d[0x18] + 0);
    a->y = (float)((double)d[0x19] + 0);
    d[0x1c] = (d[0x1c] + d[0x16]) & 0xff;
    a->dir = a->face = (a->dir + 10) & 0xff;
    return 0;
}

static void sheep_beamed(Sheep *a)
{
    /* state 0x12: rises 4 px a tick (after 6) up a beam; above o d1a it counts as penned */
    AnimObj *o = &g_anim[a->slot];
    int v = o->d[0x1b];
    o->rec.mode = 0;
    o->d[0x1b] = v + 1;
    if (v > 5) {
        a->y = (float)((double)a->y - 4.0);
        o->d[0x19] -= 4;
    }
    if (a->timer == 0) a->timer = sheep_beam_step(a);
    if ((float)o->d[0x1a] > a->y) {
        a->alive = 1;
        g_saved++;
    }
}

static void sheep_pushed(Sheep *a)
{
    /* 0x41d950, state 0x15: pushed ahead of the shepherd (his move type 0x15), speeding up to 2;
       blocked for 30 ticks both stop (he to 10) */
    if (a->speed < 2.0f) a->speed = (float)((double)a->speed + (double)0.1f);
    a->face = a->dir = g_player.face;
    float nx = (float)((double)g_sin[a->dir] * (double)a->speed + (double)a->x);
    float ny = (float)((double)a->y - (double)g_cos[a->dir] * (double)a->speed);
    sheep_collide(ftol((double)nx), ftol((double)ny), a->d, 1);
    AnimObj *o = &g_anim[a->slot], *po = &g_anim[g_player.slot];
    if (a->state == 0x15 && g_player.state == 0x15) {
        if (a->hit == 2) o->d[0x15]++;
        else o->d[0x15] = 0;
        if (o->d[0x15] == 0) {
            g_player.y = (float)((double)ny + 6.0);
            a->x = nx;
            a->y = ny;
            g_player.x = nx;
        }
        if (a->kind == 0) {
            o->frame = po->frame;
            o->countdown = po->countdown;
            anim_set_frame(o);
        }
        po->depth_off = 0x30;
        po->rec.shadow_dy = 10;
        if (o->d[0x15] > 0x1e) {
            po->depth_off = 0;
            po->rec.shadow_dy = 0;
            player_set_state(&g_player, 10);
            actor_set_state(a->d, 0);
        }
        g_player.speed = 0.0f;
        return;
    }
    po->depth_off = 0;
    po->rec.shadow_dy = 0;
    player_set_state(&g_player, 10);
}

static void sheep_path_ride(Sheep *a)
{
    /* 0x41db60, state 0x18: walks to the start of its object's path (d23), rides it (kind o d15 /
       d16 at point d18; animations 0xe on points 1-2 if d17 is 1), then walks on */
    AnimObj *o = &g_anim[a->slot];
    a->d[0x1d] &= ~1;
    if (o->sub == 0) {
        path_start(o->d[0x23]);
        a->speed = 2.0f;
        if (actor_walk_to(a->d, g_path_x, g_path_y)) o->sub = 1;
    } else if (o->sub == 1) {
        int end = path_follow(o);
        a->dir = o->dir << 4;
        a->x = (float)o->x;
        a->y = (float)o->y;
        if (o->d[0x17] == 1 && 0 < o->d[0x24] && o->d[0x24] < 3) a->anim[0] = a->anim[1] = a->anim[2] = 0xe;
        if (o->d[0x24] == o->d[0x18] && o->d[0x25] == 0) sheep_set_kind(a->d, o->d[0x15], o->d[0x16]);
        if (end) {
            a->state = 0;
            if (a->slot < 0x1e) {
                a->face = a->dir;
                a->d[0x1d] |= 1;
                return;
            }
        }
    }
    a->face = a->dir;
}

static void sheep_bounce(Sheep *a)
{
    /* 0x41dc70, state 0x1a: crouches (0x15), springs (0x16) up 64 px and again 16 px, lands (0x17) */
    AnimObj *o = &g_anim[a->slot];
    switch (o->sub) {
    case 0:
        a->speed = 0.0f;
        a->anim[0] = 0x15;
        o->sub = 1;
        break;
    case 1:
        a->anim[0] = 0x15;
        if (o->state == 2) {
            a->anim[0] = 0x16;
            o->d[0x15] = 0x40;
            o->sub = 2;
        }
        break;
    case 2:
        o->d[0x15] = 0;
        o->d[0x16] = a->iy;
        o->sub = 3;
        break;
    case 3:
    case 4: {
        a->anim[0] = 0x16;
        int step = o->sub == 3 ? 4 : 6;
        double h = o->sub == 3 ? 64.0 : 16.0;
        o->d[0x15] += step;
        double up = (double)g_sin[o->d[0x15]] * h;
        a->y = (float)((double)o->d[0x16] - up);
        o->rec.shadow_dy = ftol(up);
        if (o->d[0x15] > 0x80) {
            if (o->sub == 3) o->d[0x15] = 0;
            else o->rec.shadow_dy = 0;
            o->sub++;
        }
        break;
    }
    case 5:
        a->anim[0] = 0x17;
        if (o->state == 2) actor_set_state(a->d, 0);
        break;
    }
    a->face = a->dir;
}

static void sheep_fall_in(Sheep *a)
{
    /* 0x41ddf0, state 0x1c: falls 220 px, spinning and shrinking, then state 0x17 */
    AnimObj *o = &g_anim[a->slot];
    a->d[0x1d] &= ~1;
    if (o->sub == 0) {
        o->d[0x1b] = a->iy + 0xdc;
        o->speed = 1.0f;
        o->rec.mode = 3;
        o->depth_off = -1000;
        o->d[0x1c] = 0x100;
        sound_play_at(o, crt_rand() % 2 + 0x7a, 0, 0);
        o->sub = 1;
    } else if (o->sub == 1) {
        if (o->speed <= 5.0f) o->speed = (float)((double)o->speed + (double)0.2f);
        a->y = (float)((double)a->y + (double)o->speed);
        o->d[0x1c]--;
        o->rec.scale_x = o->rec.scale_y = o->d[0x1c];
        a->dir = (a->dir + 8) & 0xff;
        if ((float)o->d[0x1b] < a->y) actor_set_state(a->d, 0x17);
    }
    a->face = a->dir;
}

static void sheep_held(Sheep *a)
{
    /* 0x41df10, state 0x1e: stands at (o d15, d16) (facing 0xa0 in 3.2) until the shepherd picks it up */
    AnimObj *o = &g_anim[a->slot];
    a->d[0x1d] &= ~1;
    o->rec.shadow_dy = 0;
    o->depth_off = 0;
    if (o->sub == 0) {
        a->x = (float)o->d[0x15];
        a->speed = 0.0f;
        a->anim[0] = 0x1c;
        a->y = (float)o->d[0x16];
        if (g_world == 3 && g_level == 2) a->face = a->dir = 0xa0;
        o->sub = 1;
    } else if (o->sub == 1) {
        a->speed = 0.0f;
        a->anim[0] = 0x1c;
        sheep_collide(a->ix, a->iy, a->d, 1);
        sheep_pickup(a);
    }
    a->face = a->dir;
}

static void sheep_skid(Sheep *a)
{
    /* 0x41dfe0, state 0x1f: slides on, spinning, slowing by 0.1 to 1; a wall stops it where it was */
    float ox = a->x, oy = a->y;
    a->d[0x1d] &= ~1;
    if (a->kind == 0) a->anim[0] = a->anim[1] = a->anim[2] = 0x16;
    int nf = (a->face + 8) & 0xff;
    a->x = (float)((double)g_sin[a->dir] * (double)a->speed + (double)a->x);
    a->y = (float)((double)a->y - (double)g_cos[a->dir] * (double)a->speed);
    a->face = nf;
    a->speed = (float)((double)a->speed - (double)0.1f);
    if (a->speed <= 1.0f) {
        a->dir = nf;
        actor_set_state(a->d, 0);
        a->d[0x1d] |= 1;
    }
    sheep_collide(ftol((double)a->x), ftol((double)a->y), a->d, 1);
    if (a->hit == 2) {
        a->x = ox;
        a->y = oy;
        a->dir = a->face;
        a->speed = 0.0f;
        actor_set_state(a->d, 0);
        a->d[0x1d] |= 1;
    }
}

static void sheep_teleport(Sheep *a)
{
    /* 0x41e0d0, state 0x20: shrinks away, reappears at (o d15, d16) with sound d17 and grows back */
    AnimObj *o = &g_anim[a->slot];
    a->speed = 0.0f;
    a->d[0x1d] &= ~1;
    if (o->sub == 0) {
        o->rec.scale_x -= 10;
        if (o->rec.scale_x < 0) {
            o->rec.scale_x = 0;
            a->x = (float)o->d[0x15];
            a->y = (float)o->d[0x16];
            o->sub = 1;
        }
        o->rec.scale_y = o->rec.scale_x;
    } else if (o->sub == 1) {
        if (o->d[0x17] != 0) sound_play_at(o, o->d[0x17], 0, 0);
        o->sub = 2;
    } else if (o->sub == 2) {
        o->rec.scale_x += 10;
        if (o->rec.scale_x > 0x100) {
            o->rec.scale_x = 0x100;
            a->d[0x1d] |= 1;
            actor_set_state(a->d, 0);
        }
        o->rec.scale_y = o->rec.scale_x;
    }
}

static void sheep_conveyed(Sheep *a)
{
    /* 0x41e1b0, state 0x21: carried along push_dir at the object's speed, walking (state 0 again) */
    AnimObj *o = &g_anim[a->slot];
    a->x = (float)((double)g_sin[a->push_dir * 16] * (double)o->speed + (double)a->x);
    a->y = (float)((double)a->y - (double)g_cos[a->push_dir * 16] * (double)o->speed);
    a->state = 0;
    sheep_walk(a);
    if (a->kind == 0) a->anim[0] = a->anim[1] = a->anim[2] = 2;
    if (o->state == 2) anim_set(o->slot, 2, -1);
}

static void sheep_whirled(Sheep *a)
{
    /* 0x41e230, state 0x22: whirled round a centre that drifts 4 px a tick along its heading, the
       radius growing from 10; past 100 it is lost */
    AnimObj *o = &g_anim[a->slot];
    int32_t *d = o->d;
    if (o->sub == 0) {
        d[0x17] = 10;
        d[0x18] = (a->face + 0x40) & 0xff;
        d[0x19] = a->face;
        d[0x15] = a->ix - ftol((double)g_sin[d[0x18]] * 10.0);
        d[0x16] = a->iy - ftol((double)g_cos[d[0x18]] * -10.0);
        o->sub = 1;
    } else if (o->sub == 1) {
        a->d[0x1d] &= ~1;
        d[0x18] = (d[0x18] + 8) & 0xff;
        d[0x15] += ftol((double)g_sin[d[0x19]] * 4.0);
        d[0x16] += ftol((double)g_cos[d[0x19]] * -4.0);
        a->x = (float)((double)d[0x17] * g_sin[d[0x18]] + d[0x15]);
        a->y = (float)((double)d[0x16] - (double)d[0x17] * g_cos[d[0x18]]);
        if ((d[0x1d]++ & 3) == 0) d[0x17] += 2;
        int r = d[0x17];
        if (r > 0x24) o->rec.scale_x = o->rec.scale_y = (100 - r) * 4;
        if (r > 99) sheep_out(a->slot);
    }
}

void sheep_tick(void)
{
    g_flock_sx = g_flock_sy = -1;
    g_carried = 0;
    g_carrier = NULL;
    g_flock_speed = 0.0f;
    g_flock_n = 0;
    for (int n = 0; n < MAX_SHEEP; n++) {
        Sheep *a = &g_sheep[n];
        AnimObj *o = &g_anim[n];
        if (a->alive == 1) {
            a->alive = 2;
            a->d[0x1d] &= ~1;
        }
        if (a->alive == 2) continue;
        if (a->anim[0] != -1) {
            uint32_t e = 0x44ccc0 + 12 * (uint32_t)a->kind;
            a->anim[0] = exe_s32(e);
            a->anim[1] = exe_s32(e + 4);
            a->anim[2] = exe_s32(e + 8);
        }
        if (a->kind < 0x1e || 0x20 < a->kind) o->type = ANM_SHEEP;
        o->rec.clip_x0 = -1;
        o->rec.mode = 1;
        o->rec.layer = 1;
        o->rec.shadow = 3;
        if (a->pushed) {
            a->pushed = 0;
            a->x = (float)((double)g_sin[a->push_dir * 16] * (double)o->speed + (double)a->x);
            a->y = (float)((double)a->y - (double)g_cos[a->push_dir * 16] * (double)o->speed);
        }
        if (a->state >= 100) {
            level_actor_hook(a->d);
        } else switch (a->state) {
        case 0:
            sheep_walk(a);
            sheep_hazard(a);
            break;
        case 1:
            sheep_jump(a, n);
            break;
        case 4: case 0xe:
            a->d[0x1d] &= ~1;
            break;
        case 6:
            if (o->sub == 0) {
                int i = anim_new(a->ix, a->iy, g_theme == 2 ? 0x5f : 0x4f, -1, 3);
                o->d[0x15] = i;
                o->d[0x16] = 0x78;
                o->rec.scale_x = 0;
                o->sub = 1;
            } else if (o->sub == 1) {
                if (o->d[0x16]-- < 1) {
                    anim_set(o->d[0x15], g_theme == 2 ? 0x60 : 0x50, -1);
                    o->sub = 2;
                }
            } else if (o->sub == 2 && g_anim[o->d[0x15]].state == 2) {
                anim_kill(&g_anim[o->d[0x15]]);
                sheep_out(a->slot);
            }
            break;
        case 7:
            sheep_knocked(a, n);
            break;
        case 8:
            sheep_pathed(a, n);
            break;
        case 9:
            a->d[0x1d] &= ~1;
            if (o->sub == 0) {
                a->anim[0] = 0x10;
                a->speed = 0.0f;
                o->d[0x16] = 0x1e;
                o->sub = 1;
                sound_play_at(o, crt_rand() % 3 + 0x43, 0, 0);
            } else if (o->sub == 1) {
                a->anim[0] = 0x10;
                if (o->d[0x16]-- < 1) {
                    anim_new(ftol(a->x), ftol(a->y), 0xc, 8, 4);
                    o->d[0x16] = 0x1e;
                    o->sub = 2;
                }
            } else if (o->sub == 2) {
                a->anim[0] = 0x10;
                if (o->d[0x16]-- < 1) {
                    if (o->d[0x15] == 0) {
                        anim_new(ftol(a->x), ftol(a->y), 0xc, 8, 4);
                        int i = anim_new(ftol(a->x), ftol(a->y), 0x11, g_anim[a->slot].dir, 1);
                        g_anim[i].rec.mode = 3;
                        sheep_out(a->slot);
                    } else {
                        actor_set_state(a->d, 0);
                    }
                }
            }
            a->face = a->dir;
            break;
        case 10:
            a->d[0x1d] &= ~1;
            if (++a->timer < 0xe11) {
                if (rng_next() % 0xb4 == 0) {
                    int r = crt_rand() % 2;
                    sound_play_at(&g_anim[a->slot], r + 0x74, 0, 0);
                }
                int dx = ftol((double)g_player.x - (double)a->x), dy = ftol((double)g_player.y - (double)a->y);
                if (iabs(dx) + iabs(dy) < 0x20 && g_creep) {
                    g_player.state = 3;
                    g_player.speed = 0.0f;
                    g_player.anim[0] = 7;
                    a->u18 = 2;
                }
                a->speed = 0.0f;
                sheep_collide(a->ix, a->iy, a->d, 1);
            } else {
                anim_new(a->ix, a->iy, 0x27, -1, 1);
                sheep_out(a->slot);
            }
            break;
        case 0xc:
            sheep_walk(a);
            if (a->zone == -1) {
                a->state = 0;
            } else {
                uint32_t r = desc_u32(DESC_ZONES) + 4 + 20 * (uint32_t)a->zone;
                o->rec.clip_x0 = exe_s32(r + 4);
                o->rec.clip_y0 = exe_s32(r + 8) - 0x22;
                o->rec.clip_x1 = exe_s32(r + 12);
                o->rec.mode = 2;
                o->rec.clip_y1 = exe_s32(r + 16) - 0x22;
                o->rec.layer = 0;
                o->rec.sortable = 0;
                o->rec.shadow = -1;
                a->anim[0] = 0x12;
                a->anim[1] = 0x13;
                a->anim[2] = 0x14;
                g_carrier = o;
                if (0.0f < a->speed) g_carried = 1;
            }
            break;
        case 0xd:
            o->rec.shadow = -1;
            a->d[0x1d] &= ~1;
            a->anim[0] = 0x1e;
            a->speed = 0.0f;
            if (o->anim == 0x1e && o->state == 2) sheep_out(a->slot);
            break;
        case 0xf:
            sheep_slide(a, n, false);
            break;
        case 0x10:
            a->d[0x1d] &= ~1;
            a->anim[0] = 0x25;
            a->speed = 0.0f;
            sheep_collide(a->ix, a->iy, a->d, 1);
            break;
        case 0x14: {
            if (rng_next() % 0x78 == 0) sound_play_at(o, crt_rand() % 2 + 0x86, 0, 0);
            int lift = exe_s32(0x44ccac + 4 * (uint32_t)(g_char & 3));
            a->speed = 0.0f;
            a->anim[0] = 5;
            o->depth_off = lift + 0x10;
            o->rec.shadow_dy = lift - 0x10;
            a->x = g_player.x;
            a->y = g_player.y;
            AnimObj *po = &g_anim[g_player.slot];
            if (po->anim == 7) lift = po->frame << 2;
            a->y = (float)((double)a->y - (double)(float)lift);
            a->d[0x1c] = 0x3c;
            int pdir = g_player.dir;
            unsigned turned = (unsigned)(pdir - o->d[0x15]);
            o->d[0x15] = pdir;
            a->face = (a->face + (int)(turned & 0xff)) & 0xff;
            if (g_player.state != 0x14) {
                a->y = g_player.y;
                sheep_launch(a->d, pdir, 2000, 0x1e, 0, 0x28);
                o->d[0x15] = 0x5dc;
                o->rec.shadow_dy = 0;
                o->depth_off = 0;
                sound_play_at(o, crt_rand() % 2 + 0x77, 0, 0);
                a->state = 7;
            }
            break;
        }
        case 0x16:
            sheep_slide(a, n, true);
            break;
        case 0xb: sheep_spin_on_player(a); break;
        case 0x12: sheep_beamed(a); break;
        case 0x15: sheep_pushed(a); break;
        case 0x18: sheep_path_ride(a); break;
        case 0x1a: sheep_bounce(a); break;
        case 0x1c: sheep_fall_in(a); break;
        case 0x1e: sheep_held(a); break;
        case 0x1f: sheep_skid(a); break;
        case 0x20: sheep_teleport(a); break;
        case 0x21: sheep_conveyed(a); break;
        case 0x22: sheep_whirled(a); break;
        default:                        /* 2, 3, 5, 0x11, 0x13, 0x17, 0x19, 0x1b, 0x1d: nothing */
            break;
        }
        a->ix = ftol((double)a->x);
        a->iy = ftol((double)a->y);
        int kt = a->u50;
        if (kt != -1 && 0 < kt && --a->u50 == 0) sheep_set_kind(a->d, 0, -1);
    }
    sheep_animate();
}

/* ---------------------------------------------------------------- animation (0x41e380) */

void sheep_animate(void)
{
    for (int i = 0; i < MAX_SHEEP; i++) {
        Sheep *a = &g_sheep[i];
        AnimObj *o = &g_anim[i];
        int d16 = a->face / 16;
        int stand, slow, fast;
        int t = a->u50;
        if (t == 0 || 300 < t || (t & 4)) {
            stand = a->anim[0];
            slow = a->anim[1];
            fast = a->anim[2];
        } else {
            stand = 7;
            slow = 0;
            fast = 1;
        }
        if (a->state != 0xe && a->alive == 2) continue;
        if (stand == -1) {
            anim_step(i, d16, 1);
        } else if (a->state == 10) {
            if (a->u18 == 0) {
                if (o->anim != 5) anim_set(i, 5, d16);
                if (o->state == 2) {
                    a->u18 = 1;
                    anim_set(i, 8, d16);
                }
            } else if (a->u18 == 2) {
                if (o->anim != 9) anim_set(i, 9, d16);
                if (o->state == 2) {
                    a->state = 0;
                    a->d[0x1d] |= 1;
                }
            }
            anim_step(i, d16, 1);
        } else if (a->state != 0xe) {
            if (a->speed != 0.0f) {
                a->idle = (int)(rng_next() & 0x3f) + 0x3c;
                if (a->speed <= 1.5f && o->anim != slow) anim_set(i, slow, d16);
                if (1.5f < a->speed && o->anim != fast) anim_set(i, fast, d16);
                anim_step(i, d16, 1);
            } else if (stand == 7) {
                if (a->idle < 1) {
                    if (g_theme != 6) {
                        int e = exe_s32(0x44cbf0);
                        if (o->anim != e) anim_set(i, e, d16);
                    } else if (o->anim == 0x15) {
                        if (o->state == 2) anim_set(i, 0x16, d16);
                    } else if (o->anim != 0x16) {
                        anim_set(i, 0x15, d16);
                    }
                } else {
                    anim_set(i, 7, d16);
                }
                if (o->anim == 0x45 && o->frame == 8 && o->prev_frame != 8)
                    anim_new(a->ix, a->iy, 0x14, o->dir, 4);
                a->idle--;
                anim_step(i, d16, 1);
            } else {
                if (o->anim != stand) anim_set(i, stand, d16);
                a->idle--;
                anim_step(i, d16, 1);
            }
        } else {
            /* state 0xe: playing a one-off animation (d[6], facing d[7]) */
            if (a->u18 == o->anim) anim_step(i, a->timer, 1);
            else if (o->anim == 5) anim_step(i, d16, 1);
            else if (o->anim == 0xc) { o->rec.mode = 3; anim_step(i, d16, 3); }
            else if (o->anim == 0xf) {
                if (o->state != 2) anim_step(i, d16, 1);
                else a->state = 0;
            }
        }
        o->x = ftol((double)a->x);
        o->y = ftol((double)a->y);
    }
    /* TODO: the looping sound of a sheep playing animation 0xe (0x4bbd60) */
}

/* ---------------------------------------------------------------- drawing (0x41e6d0) */

void sheep_collect(void)
{
    for (int i = 0; i < MAX_SHEEP; i++) {
        Sheep *a = &g_sheep[i];
        if (a->state != 0xe && a->alive == 2) continue;
        AnimObj *o = &g_anim[i];
        DrawRec *r = &o->rec;
        int wx = a->ix - g_lv.cam_x, wy = a->iy - g_lv.cam_y;
        r->wx = wx;
        r->wy = wy;
        int sx = wx + (r->flip ? -(r->hot_x + r->w) : r->hot_x);
        int sy = wy + r->hot_y;
        if (!(sx < 640 && 0 < r->w + sx && sy < 480 && 0 < r->h + sy)) continue;
        r->x = sx;
        r->y = sy;
        r->depth = o->depth_off + sy;
        if (g_ndraw < MAX_DRAW) g_draw[g_ndraw++] = r;
        if (r->shadow != -1 && g_nshadow < MAX_SHADOW && g_ndraw < MAX_DRAW) {
            DrawRec *s = &g_shadow[g_nshadow++];
            *s = *r;
            s->kind = 7;
            s->mode = 3;
            g_draw[g_ndraw++] = s;
        }
    }
}
