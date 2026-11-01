#include "levels_int.h"
#include <stdlib.h>

/* The bonus levels (x.5): their descriptor hooks and routines. */

/* ================================================================ 1.5: the sheep train */

/* The engine (Lv15_Move_00) runs on the shepherd's steering; each sheep it collects becomes a car
   (Lv15_Move_01) inserted behind it, following the engine's trail 20 ticks per car back. */
static int16_t *trail;            /* 0x65fe84: 10000 positions {x, y} */
static int trail_pos;             /* 0x65fe80 */
static int boost;                 /* 0x65fe88: ticks of slowed cars after a pick-up (the train grows) */
static AnimObj *engine;           /* 0x65fe7c */
static int pickups;               /* 0x65fe78: pick-ups on the field */
static int spawn_wait;            /* 0x65fe74 */
static int end_wait;              /* 0x65fe70: ticks to the end after the crash */
int g_bonus_count;                /* 0x65fea8: the bonus levels' HUD counter (lives, laps...) */

static int lv15_hit(int x, int y)
{
    /* 0x423c40: the free special cell under (x, y), -1 for none */
    uint32_t c = g_lv.layer3[(y / TILE_H) * g_lv.w + x / TILE_W];
    if (!(c & 0xff)) return -1;
    int si = c >> 8 & 0xff;
    if (g_lv.tab1[(x % TILE_W) + (y % TILE_H - TILE_H + (int)(c & 0xff) * TILE_H) * TILE_W] != 1) return -1;
    if (g_special[si].active) return -1;
    return si;
}

static void lv15_spawn(void)
{
    /* 0x423ce0: a pick-up somewhere on the field: a sheep of the breed (3 in 8) or one of two
       bonuses */
    if (pickups >= 100) return;
    int r = (int)(rng_next() & 7);
    int type = r <= 1 ? 0 : r > 4 ? 3 : 2;
    int x = (int32_t)rng_next() % 500 + 0x32;
    int y = (int32_t)rng_next() % 0x136 + 0x50;
    int anim = type ? type + 0x28 : (int)exe_u32(0x450998 + 4 * (uint32_t)g_breed);
    AnimObj *o = &g_anim[anim_new(x, y, anim, -1, 3)];
    o->move = 0x8002;
    o->d[0x15] = type;
    pickups++;
}

static void lv15_init(void)
{
    /* 0x423e30 + 0x423d80 */
    for (int i = 0; i < MAX_SHEEP; i++) sheep_out(i);
    free(trail);
    trail = calloc(10000, 2 * sizeof *trail);   /* 0x421dc0 */
    g_bonus_count = 3;
    AnimObj *o = &g_anim[anim_new(0x140, 0xf0, 7, 8, 1)];
    g_player.state = 100;
    g_player.face = 0x80;
    o->move = 0x8000;
    o->d[0x15] = 0;
    o->d[0x16] = 0x96;
    o->d[0x17] = 0x80;
    o->d[0x18] = 0;
    engine = o;
    g_player.dir = 0x80;
    pickups = 0;
    for (int i = 0; i < 0x1e; i++) lv15_spawn();
    trail_pos = 0;
    boost = 0;
    end_wait = 0;
    spawn_wait = 0x3c;
}

static void lv15_tick(void)
{
    /* 0x423e70 */
    g_timer_on = 0;
    g_hud_style = 8;
    if (boost > 0) boost--;
    trail_pos = (trail_pos + 1) % 10000;
    if (spawn_wait-- <= 0) {
        lv15_spawn();
        spawn_wait = (int)(rng_next() & 0x3f) + 0x1e;
    }
    if (end_wait > 0 && --end_wait == 0) g_end = 7;
}

static void lv15_actor(int32_t *a)
{
    /* 0x423ef0: the shepherd steering (100: toward his offset d1d / d1e, at most 8 a tick) and
       thrown off (0x65) */
    if (a[3] == 0x65) {
        a[0xe] = 5;
        AnimObj *o = &g_anim[a[9]];
        if (o->frame > 10) o->frame = 10;
        return;
    }
    if (a[3] != 100) return;
    int dx = a[0x1d], dy = a[0x1e];
    int dir = dir_of(dx, dy);
    int r = ftol(sqrt((double)(dy * dy) + (double)(dx * dx)));
    if (r > 0x80) {
        a[0x1d] = g_isin[dir] / 256;
        a[0x1e] = -(g_icos[dir] / 256);
        r = 0x80;
    }
    a[0x1f] = r;
    if (r > 5) a[0] = a[2] = dir_turn(a[0], dir, 8) & 0xff;
    a[0xe] = 0x11;
    ((float *)a)[1] = 0;
    a[2] = a[0];
}

static void lv15_crash_player(void)
{
    AnimObj *p = &g_anim[g_player.slot];
    p->depth_off = 0;
    p->rec.shadow_dy = 0;
    player_set_state(&g_player, 0x65);
}

static void lv15_wreck(AnimObj *a)
{
    /* subs 6-8: thrown (anim 5) and down (anim 8, a moan) */
    switch (a->sub) {
    case 6:
        anim_set(a->slot, 5, -1);
        lv15_crash_player();
        a->sub = 7;
        break;
    case 7:
        if (a->frame > 1) a->sub = 8;
        break;
    case 8:
        if (a->state == 2) {
            sound_play_at(a, crt_rand() % 2, 0, 0);
            anim_set(a->slot, 8, -1);
            if (a->d[0x1e] == 0) end_wait = 0x78;
        }
        break;
    }
}

static void lv15_move_00(AnimObj *a)
{
    /* 0x424000: the engine, up to speed d16 / 100 in the shepherd's direction with him on board;
       a free special cell wrecks it (smoke, remains), as does running into its own train */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0: {
        if (a->speed < (float)((double)(float)d[0x16] * (double)0.01f))
            a->speed = (float)((double)a->speed + (double)0.01f);
        int dir = g_player.face;
        d[0x17] = dir;
        a->dir = (dir + 8) / 16;
        double nx = (double)g_sin[dir] * (double)a->speed + (double)a->fx;
        float ny = (float)((double)a->fy - (double)g_cos[dir] * (double)a->speed);
        if (d[0x15] == 0) {
            a->fx = (float)nx;
            a->fy = ny;
            a->x = ftol(nx);
            a->y = ftol((double)ny);
            g_player.x = (float)nx;
            g_player.y = (float)((double)ny + 6.0);
        }
        g_player.speed = 0;
        AnimObj *p = &g_anim[g_player.slot];
        p->depth_off = 0x18;
        p->rec.shadow_dy = 10;
        if (a->anim != 1) anim_set(a->slot, 1, a->dir);
        a->frame = g_anim[g_player.slot].frame;
        a->countdown = g_anim[g_player.slot].countdown;
        anim_set_frame(a);
        if (lv15_hit(a->x, a->y) != -1) {
            lv15_crash_player();
            a->sub = 2;
        }
        if (crt_rand() % 0xb4 == 0) sound_play_at(a, crt_rand() % 3 + 0x71, 0, 0);
        break;
    }
    case 2:
        anim_set(a->slot, 0x10, -1);
        d[0x19] = 0x1e;
        a->sub = 3;
        sound_play_at(a, crt_rand() % 3 + 0x43, 0, 0);
        break;
    case 3: {
        int t = d[0x19];
        d[0x19] = t - 1;
        if (t < 1) {
            anim_new(a->x, a->y, 0xc, 8, 4);
            d[0x19] = 0x1e;
            a->sub = 4;
        }
        break;
    }
    case 4: {
        int t = d[0x19];
        d[0x19] = t - 1;
        if (t < 1) {
            anim_new(a->x, a->y, 0xc, 8, 4);
            g_anim[anim_new(a->x, a->y, 0x11, a->dir, 1)].rec.mode = 3;
            if (d[0x1e] == 0) end_wait = 0x78;
            a->sub = 5;
        }
        break;
    }
    case 5:
        a->rec.scale_x = 0;
        break;
    default:
        lv15_wreck(a);
        break;
    }
    if (trail) {
        trail[2 * trail_pos] = (int16_t)a->x;
        trail[2 * trail_pos + 1] = (int16_t)a->y;
    }
}

static void lv15_move_01(AnimObj *a)
{
    /* 0x424390: a car: d1d the one it follows, d1e the one behind; steers for the engine's trail
       20 ticks per car back, matching the engine's speed plus 2 per px it lags past 40 */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        d[0x1d] = engine->slot;
        d[0x1e] = engine->d[0x1e];
        if (engine->d[0x1e] != 0) g_anim[engine->d[0x1e]].d[0x1d] = a->slot;
        engine->d[0x1e] = a->slot;
        a->sub = 1;
        return;
    case 1: {
        int y = a->y;
        int n = g_anim[d[0x1d]].d[0x18] + 1;
        d[0x18] = n;
        int k = (boost + n * -0x14 + 0x271a + trail_pos) % 10000;
        int tx = trail ? trail[2 * k] : 0, ty = trail ? trail[2 * k + 1] : 0;
        int ex = a->x - tx, ey = y - ty;
        int r = ftol(sqrt((double)(ex * ex + ey * ey)));
        int dir = dir_of(tx - a->x, ty - y);
        d[0x17] = dir;
        int d16 = (dir + 8) / 16;
        a->dir = d16;
        d[0x16] = engine->d[0x16] + (r - 0x28) * 2;
        if ((float)((double)(float)d[0x16] * (double)0.01f) <= a->speed) {
            a->speed = (float)((double)a->speed - (double)0.1f);
            if (a->speed < 0.0f) a->speed = 0;
        } else {
            a->speed = (float)((double)a->speed + (double)0.1f);
        }
        if (boost != 0) a->speed = 0.5f;
        a->fx = (float)((double)g_sin[dir] * (double)a->speed + (double)a->fx);
        a->fy = (float)((double)a->fy - (double)g_cos[dir] * (double)a->speed);
        a->x = ftol((double)a->fx);
        a->y = ftol((double)a->fy);
        if (a->speed <= 1.5f && a->anim != 0) anim_set(a->slot, 0, d16);
        if (1.5f < a->speed && a->anim != 1) anim_set(a->slot, 1, a->dir);
        if (lv15_hit(a->x, a->y) != -1) {
            lv15_crash_player();
            a->sub = 2;
        }
        if (g_anim[d[0x1d]].sub == 8) a->sub = 6;
        if (boost == 0 && d[0x18] > 1 && engine->sub == 0 &&
            iabs(engine->y - a->y) + iabs(engine->x - a->x) < 0x14) {
            engine->sub = 6;
            return;
        }
        return;
    }
    case 2:
        anim_set(a->slot, 0x10, -1);
        d[0x19] = 0x1e;
        a->sub = 3;
        sound_play_at(a, crt_rand() % 3 + 0x43, 0, 0);
        d[0x1f] = 5;
        return;
    case 3: {
        /* the wreck spreads back down the train */
        if (d[0x1f]-- == 0) {
            AnimObj *b = &g_anim[d[0x1e]];
            if (b->sub != 2 && b->sub != 3) b->sub = 2;
        }
        int t = d[0x19];
        d[0x19] = t - 1;
        if (t < 1) {
            anim_new(a->x, a->y, 0xc, 8, 4);
            d[0x19] = 0x1e;
            a->sub = 4;
        }
        return;
    }
    case 4: {
        int t = d[0x19];
        d[0x19] = t - 1;
        if (t < 1) {
            if (d[0x1e] == 0) end_wait = 0x78;
            anim_new(a->x, a->y, 0xc, 8, 4);
            g_anim[anim_new(a->x, a->y, 0x11, a->dir, 1)].rec.mode = 3;
            anim_kill(a);
        }
        return;
    }
    default:
        lv15_wreck(a);
        return;
    }
}

static void lv15_move_02(AnimObj *a)
{
    /* 0x424840: a pick-up (d15: 0 a sheep, which joins the train, 100 points; 2 / 3 a bonus, 10
       points and 2 more top speed) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        a->sub = 1;
        d[0x16] = a->rec.w / 2 + a->x;
        d[0x17] = a->rec.h * 3 / 4 + a->y;
        return;
    }
    if (a->sub != 1) return;
    int r = iabs(engine->y - d[0x17]) + iabs(engine->x - d[0x16]);
    if (d[0x15] == 0) {
        if (r < 0x14) {
            sound_play_at(a, 5, 0, 0);
            anim_set(a->slot, 0x29, -1);
            a->move = 0;
            g_score += 100;
            g_score_flash = 0x18;
            AnimObj *c = &g_anim[anim_new(d[0x16], d[0x17], 0, -1, 1)];
            boost += 0x14;
            c->move = 0x8001;
            c->rec.shadow = 1;
            pickups--;
        }
    } else if (d[0x15] > 1 && d[0x15] < 4 && r < 10) {
        sound_play_at(a, crt_rand() % 3 + 2, 0, 0);
        anim_kill(a);
        engine->d[0x16] += 2;
        g_score += 10;
        g_score_flash = 0x18;
        pickups--;
    }
}

/* ================================================================ dispatch */

bool bonus_actor(uint32_t va, int32_t *actor)
{
    switch (va) {
    case 0x423ef0: lv15_actor(actor); return true;
    default: return false;
    }
}

bool bonus_call(uint32_t va)
{
    switch (va) {
    case 0x423e30: lv15_init(); return true;
    case 0x423e70: lv15_tick(); return true;
    default: return false;
    }
}

MoveFn bonus_move(uint32_t va)
{
    switch (va) {
    case 0x424000: return lv15_move_00;
    case 0x424390: return lv15_move_01;
    case 0x424840: return lv15_move_02;
    default: return NULL;
    }
}
