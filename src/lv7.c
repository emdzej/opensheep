#include "levels_int.h"
#include "play.h"

/* World 7.1: the four breeds at once. Each breed's flock (g_flock_size[g], sprite banks 200 + 10g)
   is kept apart (save 0x662ac8, 30 sheep a breed; their drawn objects 0x66b3a8) and swapped into
   the sheep slots to run the flock tick (0x41caf0) once a breed; a flying saucer bombs them. */

enum { BREEDS = 4, FLOCK = BREEDS * MAX_SHEEP };

static Sheep save[FLOCK];                 /* 0x662ac8 */
static AnimObj *drawn[FLOCK];             /* 0x66b3a8 */
static int penned[BREEDS];                /* 0x66b58c */
static int total[BREEDS];                 /* 0x66b59c */
static int lost[BREEDS];                  /* 0x66b5ac */
static int open_breed = -1;               /* 0x66b588: the breed whose pen gate is open */

static void lv71_fail(int why)
{
    int s = anim_new(0, 0, 0x10, -1, 4);
    g_anim[s].move = 0x11;
    g_anim[s].d[0x15] = why;
}

static void lv71_init(void)
{
    /* 0x434c60 */
    for (int g = 0; g < BREEDS; g++) {
        /* the original sets the breed for each bank's path and leaves it at 3 */
        if (exe_u32(0x45560c) == 0) continue;
        g_breed = g;
        bank_table_load(0x455608, 200 + 10 * g);
    }
    for (int g = 0; g < BREEDS; g++) {
        lost[g] = 0;
        total[g] = g_flock_size[g];
        penned[g] = 0;
        /* the start: the lowest object running routine 0x13 with d17 = the breed */
        float px = 0, py = 0;
        for (int i = g_anim_hi - 1; i >= 0; i--)
            if (g_anim[i].move == 0x13 && g_anim[i].d[0x17] == g) {
                px = (float)g_anim[i].x;
                py = (float)g_anim[i].y;
            }
        for (int i = 0; i < MAX_SHEEP; i++) {
            Sheep *s = &g_sheep[i];
            int n = g * MAX_SHEEP + i;
            if (i >= total[g]) {
                s->alive = 2;
                drawn[n] = NULL;
                g_anim[i].type = -1;
            } else {
                s->x = (float)((double)((int32_t)rng_next() % 64 - 0x20) + (double)px);
                s->y = (float)((double)((int32_t)rng_next() % 0x60 - 0x30) + (double)py);
                int d = (int)(rng_next() & 0xff);
                s->dir = s->face = d;
                s->alive = 0;
                s->kind = 0;
                s->slot = i;
                s->speed = 0;
                s->state = 0;
                s->anim[0] = 3;
                s->anim[1] = 0;
                s->anim[2] = 1;
                s->d[0x1d] = 1;
                s->d[0x3f] = s->d[0x40] = d;
                s->d[0x43] = ftol(s->x);
                s->d[0x44] = ftol(s->y);
                s->idle = (int)(rng_next() & 0x3f);
                s->d[0x45] = (int)(rng_next() & 0xff);
                anim_init(2, i, 0, d / 16);
                int k = anim_new(ftol(s->x), ftol(s->y), 0, d / 16, 1);
                g_anim[k].rec.kind = 1;
                drawn[n] = &g_anim[k];
            }
            save[n] = *s;
        }
    }
    open_breed = -1;
}

static void lv71_tick(void)
{
    /* 0x434f40 */
    for (int g = 0; g < BREEDS; g++) {
        Sheep *sv = &save[g * MAX_SHEEP];
        AnimObj **dr = &drawn[g * MAX_SHEEP];
        for (int i = 0; i < MAX_SHEEP; i++) {
            g_sheep[i] = sv[i];
            if (dr[i]) {
                dr[i]->rec.bank = dr[i]->d[0x1f];
                g_anim[i] = *dr[i];
            } else {
                g_sheep[i].alive = 2;
            }
        }
        sheep_tick();
        for (int i = 0; i < MAX_SHEEP; i++) {
            if (!dr[i]) continue;
            sv[i] = g_sheep[i];
            *dr[i] = g_anim[i];
            if (g_anim[i].type == -1) {
                /* the original goes on through the cleared pointer */
                dr[i] = NULL;
                continue;
            }
            AnimObj *o = dr[i];
            o->d[0x1e] = g;
            o->d[0x1f] = o->rec.bank;
            int bank = 200 + 10 * g + (int)exe_u32(0x455650 + 4 * (uint32_t)o->rec.bank);
            o->rec.bank = bank;
            rec_set_sprite(&o->rec, bank, o->rec.frame);
        }
    }
    for (int i = 0; i < MAX_SHEEP; i++) {
        g_anim[i].type = -1;
        g_sheep[i].alive = 2;
    }
    int all = 0, in = 0, out = 0;
    for (int g = 0; g < BREEDS; g++) {
        all += total[g];
        in += penned[g];
        out += lost[g];
    }
    g_sheep_out = out;
    g_saved = in;
    g_nsheep = all;
    if (out + in >= all) {
        if (in >= g_needed) g_end = 1;
        else if (!g_level_over) lv71_fail(1);
    }
    if (open_breed != -1 && lost[open_breed] + penned[open_breed] >= total[open_breed]) open_breed = -1;
}

static void lv71_out(int n)
{
    /* 0x4351a0: sheep n of the whole flock is lost; the last one fails the level */
    Sheep *s = &save[n];
    if (s->alive == 2) return;
    s->state = 4;
    s->kind = 0;
    s->alive = 2;
    s->d[0x1d] &= ~1;
    if (drawn[n]) anim_kill(drawn[n]);
    drawn[n] = NULL;
    lost[n / MAX_SHEEP]++;
    g_sheep_out++;
    if (g_nsheep - g_sheep_out <= 0 && !g_level_over) lv71_fail(0);
}

static void lv71_launch(int n, int dir, int count, int height, int then, int speed)
{
    /* 0x435280: sheep_launch for a kept sheep */
    Sheep *s = &save[n];
    AnimObj *o = drawn[n];
    s->dir = dir;
    s->state = 1;
    if (!o) return;   /* the original writes through the pointer regardless */
    o->d[0x15] = o->d[0x16] = count;
    o->d[0x18] = height;
    o->fy = s->y;
    o->d[0x19] = 0;
    o->d[0x1a] = then;
    o->d[0x1b] = speed;
    o->d[0x1d] = 0;
    anim_set(o->slot, 2, -1);
    s->anim[0] = s->anim[1] = s->anim[2] = 2;
    sound_play_at(o, 0x51, 0, 0);
}

static void lv71_blast(AnimObj *a, int n)
{
    /* a kept sheep blown up: smoke, its breed's remains (anim 0x6a + 10 x breed), lost */
    Sheep *s = &save[n];
    anim_new(s->ix, s->iy, 0xc, 8, 4);
    int k = anim_new(s->ix, s->iy, (n / MAX_SHEEP) * 10 + 0x6a, s->face / 16, 3);
    g_anim[k].rec.mode = 3;
    lv71_out(n);
    sound_play_at(a, crt_rand() % 3 + 0x3d, 0, 0);
}

static void lv71_actor(int32_t *a)
{
    /* 0x434b70: state 100, into the pen: walk to path d15's start and along it, counted for the
       breed (d1e of its object) */
    if (a[3] != 100) return;
    AnimObj *o = &g_anim[a[9]];
    float *f = (float *)a;
    o->rec.shadow_dy = 0;
    o->depth_off = 0;
    a[0x1d] &= ~1;
    switch (o->sub) {
    case 0:
        o->sub = 1;
        o->d[0x23] = o->d[0x15];
        /* fall through */
    case 1:
        path_start(o->d[0x23]);
        f[1] = 1.0f;
        if (actor_walk_to(a, g_path_x, g_path_y)) o->sub = 2;
        break;
    case 2: {
        int r = path_follow(o);
        a[0] = o->dir << 4;
        f[10] = (float)o->x;
        f[11] = (float)o->y;
        if (r) {
            int g = o->d[0x1e];
            if (g >= 0 && g < BREEDS) penned[g]++;   /* the original indexes with whatever is there */
            a[3] = 4;
            a[4] = 1;
            a[2] = a[0];
            return;
        }
        break;
    }
    }
    a[2] = a[0];
}

static void lv71_move_00(AnimObj *a)
{
    /* 0x435310: link d17 of the chain from object d16 (or a fixed point, for d16 itself) to d15 */
    int32_t *d = a->d;
    int s15 = g_obj_by_id[d[0x15]], s16 = g_obj_by_id[d[0x16]];
    AnimObj *p = &g_anim[s15], *q = &g_anim[s16];
    a->rec.mode = 1;
    a->rec.layer = 1;
    a->rec.sortable = 1;
    if (a == q) {
        a->x = 0x289;
        a->y = 0x1eb;
    } else {
        a->x = (p->x - q->x) * d[0x17] / 5 + q->x;
        a->y = q->y + (p->y - q->y) * d[0x17] / 5;
    }
    a->dir = (p->dir + d[0x17]) & 0xf;
    a->depth_off = d[0x17] * 0x48;
    p->depth_off = 0x168;
}

/* Lv71_Move_01's stations (0x4557b8, 16 bytes: special cell, x, y, direction); 8 is home. */
static int st(int i, int k) { return (int)exe_u32(0x4557b8 + 16 * (uint32_t)i + 4 * (uint32_t)k); }

static void lv71_ufo_fly(AnimObj *a)
{
    /* from station d1d to d1f over 64 ticks (d1e += 2), arcing up 64 px, bobbing */
    int32_t *d = a->d;
    int t = d[0x1e] += 2;
    int ph = d[0x20];
    int y0 = st(d[0x1d], 2), x0 = st(d[0x1d], 1);
    int dy = (st(d[0x1f], 2) - y0) * t / 128;
    int arc = ftol((double)g_sin[t] * 64.0);
    a->y = dy - arc - ftol((double)g_sin[ph] * -24.0) + y0;
    int dx = (st(d[0x1f], 1) - x0) * d[0x1e] / 128;
    a->x = dx - ftol((double)g_cos[ph] * -10.0) + x0;
    if ((d[0x1e] & 3) == 0) anim_turn16(a, st(d[0x1f], 3));
}

static void lv71_ufo_hover(AnimObj *a)
{
    int32_t *d = a->d;
    int ph = d[0x20];
    a->y = st(d[0x1d], 2) - ftol((double)g_sin[ph] * -24.0);
    a->x = st(d[0x1d], 1) - ftol((double)g_cos[ph] * -10.0);
    a->dir = st(d[0x1d], 3);
}

static void lv71_move_01(AnimObj *a)
{
    /* 0x4353f0: the saucer (its shadow object d15 follows): hovers at home until a station's cell
       holds sheep (the fullest), flies there, drops bombs (routine 2) through frame 7 of anim
       0x2e for 120 ticks, flies home and waits a second */
    int32_t *d = a->d;
    d[0x20] = (d[0x20] + 2) & 0xff;
    switch (a->sub) {
    case 0:
        d[0x1d] = 8;
        a->sub = 1;
        break;
    case 1:
        if (a->anim != 3) {
            anim_set(a->slot, 3, -1);
            anim_set(g_obj_by_id[d[0x15]], 4, -1);
        }
        lv71_ufo_hover(a);
        if (d[0x1e] > 0) {
            d[0x1e]--;
            break;
        }
        {
            int best = 8;
            int8_t most = 0;
            for (int i = 0; i < 8; i++) {
                int8_t c = (int8_t)g_special[st(i, 0)].count;
                if (most < c) {
                    best = i;
                    most = c;
                }
            }
            if (best != 8) {
                d[0x1f] = best;
                d[0x1e] = 0;
                sound_play_at(a, (crt_rand() & 1) + 4, 0, 0);
                a->sub = 2;
            }
        }
        break;
    case 2:
        lv71_ufo_fly(a);
        if (d[0x1e] > 0x7f) {
            d[0x1d] = d[0x1f];
            d[0x1e] = 0x78;
            anim_set(a->slot, 0x2e, -1);
            anim_set(g_obj_by_id[d[0x15]], 0x2f, -1);
            a->sub = 3;
        }
        break;
    case 3: {
        lv71_ufo_hover(a);
        if (a->frame == 7) {
            AnimObj *b = &g_anim[anim_new(a->x, a->y, 0x2b, -1, 3)];
            b->move = 0x8002;
            b->d[0x1d] = d[0x1d];
        }
        int t = d[0x1e];
        d[0x1e] = t - 1;
        if (t < 1) {
            d[0x1f] = 8;
            d[0x1e] = 0;
            a->sub = 4;
        }
        break;
    }
    case 4:
        lv71_ufo_fly(a);
        if (d[0x1e] >= 0x80) {
            d[0x1e] = 0x3c;
            d[0x1d] = d[0x1f];
            a->sub = 1;
        }
        break;
    }
    AnimObj *s = &g_anim[g_obj_by_id[d[0x15]]];
    s->x = a->x;
    s->y = a->y;
    s->dir = a->dir;
    s->depth_off = a->depth_off - 0x38;
}

static void lv71_move_02(AnimObj *a)
{
    /* 0x435820: a bomb from station d1d falling to a random point of path d1d + 5 over 48 ticks,
       then 3 seconds of blast (anim 0x36, 0x37): sheep within 32 px are blown up, the shepherd
       knocked down (0x1b); anim 0x38 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        int x, y;
        path_point(d[0x1d] + 5, (int)(rng_next() & 7), &x, &y);
        d[0x15] = a->x;
        d[0x16] = a->y;
        d[0x17] = x;
        d[0x18] = y;
        d[0x19] = 0;
        a->sub = 1;
    } else if (a->sub == 1) {
        int t = d[0x19] + 1;
        a->x = (d[0x17] - d[0x15]) * t / 0x30 + d[0x15];
        d[0x19] = t;
        a->y = (d[0x18] - d[0x16]) * t / 0x30 + d[0x16];
        if (t > 0x2f) {
            a->rec.mode = 3;
            anim_set(a->slot, 0x36, -1);
            d[0x19] = 0xb4;
            a->sub = 2;
        }
    } else if (a->sub == 2) {
        if (a->state == 2) anim_set(a->slot, 0x37, -1);
        for (int i = 0; i < FLOCK; i++) {
            Sheep *s = &save[i];
            if (s->alive != 2 && iabs(s->ix - a->x) + iabs(s->iy - a->y) < 0x20) lv71_blast(a, i);
        }
        if (g_player.cooldown == 0 && iabs(g_player.iy - a->y) + iabs(g_player.ix - a->x) < 0x20) {
            player_set_state(&g_player, 0x1b);
            sound_play_at(a, crt_rand() % 3 + 0x3d, 0, 0);
        }
        if (--d[0x19] > 0) return;
        anim_set(a->slot, 0x38, -1);
        a->sub = 3;
    }
}

static void lv71_move_03(AnimObj *a)
{
    /* 0x435a60: a breed's machine: touched with a sheep beside it (64 px, Manhattan, from (32, 24)
       on it), one of breed d15 is thrown in direction d18 (anim d16, triggers d19 / d1a) and any
       other is blown up (anim d17, at frame 5) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] == -1) return;
        for (int i = 0; i < FLOCK; i++) {
            Sheep *s = &save[i];
            if (s->alive == 2 || iabs(s->ix - a->x - 0x20) + iabs(s->iy - a->y - 0x18) >= 0x40) continue;
            d[0x1d] = i;
            anim_set(a->slot, i / MAX_SHEEP == d[0x15] ? d[0x16] : d[0x17], -1);
            s->state = 4;
            a->sub = i / MAX_SHEEP == d[0x15] ? 1 : 2;
            break;
        }
    } else if (a->sub == 1) {
        int n = d[0x1d];
        if (a->frame == 2 && a->prev_frame != 2) {
            g_anim[g_obj_by_id[d[0x19]]].d[0x22] = 0x8001;
            g_anim[g_obj_by_id[d[0x1a]]].d[0x22] = 0x8001;
        }
        if (a->state == 2) {
            lv71_launch(n, d[0x18], 1000, 0x28, 0, 0x19);
            d[0x22] = -1;
            a->sub = 0;
        }
    } else if (a->sub == 2) {
        if (a->frame == 5 && a->prev_frame != 5) lv71_blast(a, d[0x1d]);
        if (a->state == 2) {
            d[0x22] = -1;
            a->sub = 0;
        }
    }
}

static void lv71_move_04(AnimObj *a)
{
    /* 0x435c90: a gate (d17): opened by 0x8001 (anim d15), closed by a touch (anim d16) while no
       pen is open */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        if (d[0x22] == 0x8001) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
        break;
    case 1:
        if (a->frame == a->nframes - 1) {
            gate_set(d[0x17], 1);
            a->sub = 2;
        }
        break;
    case 2:
        if (d[0x22] != -1 && d[0x22] != 0x8001 && open_breed == -1) {
            anim_set(a->slot, d[0x16], -1);
            a->sub = 3;
        }
        break;
    case 3:
        if (a->frame == a->nframes - 1) {
            gate_set(d[0x17], 0);
            a->sub = 0;
        }
        break;
    }
    d[0x22] = -1;
}

static void lv71_move_05(AnimObj *a)
{
    /* 0x435d90: breed d17's pen: touched while none is open, it opens (anim d15, gate d16) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] != -1 && open_breed == -1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
    } else if (a->sub == 1 && a->frame == a->nframes - 1) {
        gate_set(d[0x16], 1);
        open_breed = d[0x17];
        a->sub = 2;
    }
    d[0x22] = -1;
}

bool lv7_actor(uint32_t va, int32_t *actor)
{
    if (va != 0x434b70) return false;
    lv71_actor(actor);
    return true;
}

bool lv7_call(uint32_t va)
{
    switch (va) {
    case 0x434c60: lv71_init(); return true;
    case 0x434f40: lv71_tick(); return true;
    default: return false;
    }
}

MoveFn lv7_move(uint32_t va)
{
    switch (va) {
    case 0x435310: return lv71_move_00;
    case 0x4353f0: return lv71_move_01;
    case 0x435820: return lv71_move_02;
    case 0x435a60: return lv71_move_03;
    case 0x435c90: return lv71_move_04;
    case 0x435d90: return lv71_move_05;
    default: return NULL;
    }
}
