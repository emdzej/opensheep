#include "levels_int.h"

/* World 5 (5.1-5.4 share the routines; 5.3 and 5.4 have the actor hook). */

static void lv51_fly(int32_t *a, int k, int tail)
{
    /* the ride: path d15 from the actor's offset to it (kept in d18 / d19), then off in direction
       d16 at 3 px a tick for tail ticks into state d17 (0x19 straight away); kind k meanwhile */
    AnimObj *o = &g_anim[a[9]];
    int32_t *d = o->d;
    float *f = (float *)a;
    if (o->sub == 1) {
        f[1] = 0;
        sheep_set_kind(a, k, -1);
        path_start(d[0x15]);
        d[0x18] = g_path_x - a[0xc];
        d[0x19] = g_path_y - a[0xd];
        path_begin(o, d[0x15], 0, 0);
        if (k == 0x18) sound_play_at(o, 0xc, 0, 0);
        o->sub = 2;
        return;
    }
    f[1] = 0;
    if (k == 0x18) sheep_set_kind(a, 0x18, -1);
    if (path_follow(o)) {
        if (k == 0x18) o->rec.scale_x = 0x100;
        if (d[0x17] == 0x19) {
            actor_set_state(a, 0x19);
        } else {
            f[1] = 3.0f;
            a[2] = a[0] = d[0x16];
            o->sub = 3;
            d[0x15] = tail;
        }
    }
    f[10] = (float)(o->x - d[0x18]);
    f[11] = (float)(o->y - d[0x19]);
}

static void lv51_actor(int32_t *a)
{
    /* 0x430a40: move types 100-103, parameters in the actor's object d15.. */
    int slot = a[9];
    AnimObj *o = &g_anim[slot];
    int32_t *d = o->d;
    float *f = (float *)a;
    switch (a[3]) {
    case 100:
        /* a sheep of kind 0x19 (the shepherd too) is carried off along a path (kind 0x1a) and
           dropped flying */
        switch (o->sub) {
        case 0:
            if (a[5] != 0x19 && slot <= 0x1e) {
                actor_set_state(a, 0);
                return;
            }
            o->sub = 1;
            if (slot > 0x1e) return;
            anim_kill(&g_anim[d[0x21]]);
            d[0x21] = 0;
            a[0x1d] &= ~1;
            return;
        case 1:
        case 2:
            lv51_fly(a, 0x1a, 0x10);
            return;
        case 3: {
            float ny = (float)((double)f[11] - (double)g_cos[a[0]] * (double)f[1] + 2.0);
            f[10] = (float)((double)g_sin[a[0]] * (double)f[1] + (double)f[10]);
            f[11] = ny;
            o->rec.shadow_dy = d[0x15];
            sheep_set_kind(a, 0, -1);
            int t = d[0x15];
            d[0x15] = t - 1;
            if (t > 0) return;
            actor_set_state(a, d[0x17]);
            if (slot <= 0x1e) a[0x1d] |= 1;
            return;
        }
        }
        return;
    case 0x65:
        /* as 100 for kind 0x17, carried as kind 0x18 and set down at once */
        switch (o->sub) {
        case 0:
            if (a[5] != 0x17 && slot <= 0x1e) {
                actor_set_state(a, 0);
                return;
            }
            anim_kill(&g_anim[d[0x21]]);
            d[0x21] = 0;
            o->sub = 1;
            if (slot <= 0x1e) a[0x1d] &= ~1;
            return;
        case 1:
        case 2:
            lv51_fly(a, 0x18, 0x30);
            return;
        case 3:
            o->rec.shadow_dy = 0;
            actor_set_state(a, d[0x17]);
            sheep_set_kind(a, 0, -1);
            if (slot <= 0x1e) a[0x1d] |= 1;
            return;
        }
        return;
    case 0x66:
        /* one tick back in state 0 for the special cell under it; unless that sets 0x66 again a
           sheep goes to 0x19 (the shepherd stays in 0) */
        a[3] = 0;
        if (slot > 0x1e) {
            player_specials(a[0xc], a[0xd], (Player *)a, 1);
            if (a[3] != 0x66) a[3] = 0;
        } else {
            sheep_specials(a[0xc], a[0xd], (Sheep *)a, 1);
            if (a[3] != 0x66) actor_set_state(a, 0x19);
        }
        return;
    case 0x67:
        /* state 8 (walk to path d23 and along it), then held (4) */
        a[0x1d] &= ~1;
        o->rec.shadow_dy = 0;
        o->depth_off = 0;
        sheep_pathed((Sheep *)a, slot);
        if (a[3] == 0) a[3] = 4;
        return;
    }
}

static void lv51_move_00(AnimObj *a)
{
    /* 0x430ea0: along path d16; the first sheep (any of the 30) or the shepherd within d17
       (Manhattan) is thrown (direction d18, d19 x 10 ticks, height d1b, then d1a, speed d1c),
       anim d15, a second's rest */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x16], 1, 0);
        d[0x1f] = a->anim;
        a->sub = 1;
    } else if (a->sub == 1) {
        path_follow(a);
        int32_t *t = NULL;
        for (int i = 0; i < MAX_SHEEP; i++) {
            Sheep *s = &g_sheep[i];
            if (iabs(s->iy - a->y) + iabs(s->ix - a->x) < d[0x17]) {
                t = s->d;
                break;
            }
        }
        if (iabs(g_player.ix - a->x) + iabs(g_player.iy - a->y) < d[0x17]) t = g_player.d;
        if (t) {
            if (t[9] < 0x1e) sheep_launch_sound(t, d[0x18], d[0x19] * 10, d[0x1b], d[0x1a], d[0x1c]);
            else player_launch_sound((Player *)t, d[0x18], d[0x19] * 10, d[0x1b], d[0x1a], d[0x1c]);
            anim_set(a->slot, d[0x15], -1);
            d[0x1d] = 0x3c;
            a->sub = 2;
        }
    } else if (a->sub == 2) {
        path_follow(a);
        if (d[0x1d]-- < 1) {
            a->sub = 1;
            if (a->anim != d[0x1f]) anim_set(a->slot, d[0x1f], -1);
        }
        if (a->anim == d[0x15] && a->state == 2) anim_set(a->slot, d[0x1f], -1);
    }
}

static void lv51_move_01(AnimObj *a)
{
    /* 0x431030: triggered, anim d15 then d16 along path d19, opening gate d1a halfway through its
       first leg; at point 1 anim d17 for 3 s, then d18 */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        a->sub = 1;
        break;
    case 1:
        if (d[0x22] != -1) {
            path_begin(a, d[0x19], 1, 1);
            anim_set(a->slot, d[0x15], -1);
            a->sub = 2;
        }
        break;
    case 2:
        if (a->state == 2) {
            anim_set(a->slot, d[0x16], -1);
            a->sub = 3;
        }
        break;
    case 3:
        path_follow(a);
        if (d[0x25] == path_ticks(d[0x23], d[0x24]) / 2) gate_set(d[0x1a], 1);
        if (d[0x24] == 1) {
            a->rec.mode = 3;
            anim_set(a->slot, d[0x17], -1);
            d[0x1e] = 0xb4;
            a->sub = 4;
        }
        break;
    case 4:
        if (--d[0x1e] < 1) {
            anim_set(a->slot, d[0x18], -1);
            a->sub = 5;
        }
        break;
    }
}

static void lv51_move_02(AnimObj *a) { call_out(a, g_level == 4 ? 8 : 0xb); }   /* 0x431160 */
static void lv51_move_03(AnimObj *a) { call_out(a, 10); }                       /* 0x4311e0 */

static void lv51_move_04(AnimObj *a)
{
    /* 0x431250: on frame d18 starts an object (anim d16, routine 5, mirrored as this one) along
       path d15; repeats after d17 / 6 s */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->frame == d[0x18] && a->frame != a->prev_frame) {
            path_start(d[0x15]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
            o->move = 0x8005;
            o->d[0x15] = d[0x16];
            o->d[0x16] = d[0x15];
            o->mirror = a->mirror;
        }
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1 && d[0x1d]-- < 1) {
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
}

static void lv51_move_05(AnimObj *a)
{
    /* 0x431310: a grab along path d16: the first sheep within 24 (or the shepherd) is held to it
       (state 4); at the end of the path a 3 s wait (sound 0xf), then it goes and takes the sheep
       out of play (the shepherd to 0x17); the animation runs only while empty */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x16], 1, 0);
        d[0x21] = -1;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (path_follow(a)) {
            a->sub = 2;
            d[0x15] = 0xb4;
            sound_play_at(a, 0xf, 0, 0);
        }
        if (d[0x21] == -1) {
            for (int i = 0; i < MAX_SHEEP; i++) {
                Sheep *s = &g_sheep[i];
                if (s->alive == 2 || iabs(a->x - s->ix) + iabs(a->y - s->iy) >= 0x18) continue;
                d[0x21] = s->slot;
                s->state = 4;
                s->d[0x1d] &= ~1;
                break;
            }
            if (d[0x21] != -1) return;
            if (iabs(a->y - g_player.iy + 0x12) + iabs(a->x - g_player.ix) < 0x18) {
                d[0x21] = g_player.slot;
                g_player.state = 4;
            }
        } else {
            float *w = (float *)actor(d[0x21]);
            w[10] = (float)a->x;
            w[11] = (float)(((int32_t *)w)[9] < 0x1e ? a->y : a->y + 0x12);
        }
    } else if (a->sub == 2 && d[0x15]-- < 1) {
        anim_kill(a);
        if (d[0x21] != -1) {
            if (d[0x21] < 0x1e) sheep_out(d[0x21]);
            else player_set_state(&g_player, 0x17);
        }
    }
    if (d[0x21] == -1) a->countdown = 2;
}

static void lv51_ride(AnimObj *a, int32_t *w)
{
    AnimObj *o = &g_anim[w[9]];
    o->d[0x15] = a->d[0x15];
    o->d[0x16] = a->d[0x16];
    o->d[0x17] = a->d[0x17];
    w[2] = a->d[0x18] << 4;
}

static void lv51_move_06(AnimObj *a)
{
    /* 0x4314d0: hidden; a sheep of kind 0x19, or the shepherd creeping, is carried along path d15
       (move type 100, then direction d16, state d17, facing d18) */
    a->rec.scale_x = 0;
    if (a->d[0x22] != -1) {
        int32_t *w = actor(a->d[0x21]);
        if (w[9] < 0x1e) {
            if (w[5] == 0x19) actor_set_state(w, 100);
        } else if (g_creep) {
            player_set_state((Player *)w, 100);
        }
        if (w[3] == 100) lv51_ride(a, w);
    }
    a->d[0x22] = -1;
}

static void lv51_move_07(AnimObj *a)
{
    /* 0x431590: hidden; who triggers is carried (0x65) as by 0x4314d0 */
    a->rec.scale_x = 0;
    if (a->d[0x22] != -1) {
        int32_t *w = actor(a->d[0x21]);
        actor_set_state(w, 0x65);
        lv51_ride(a, w);
    }
    a->d[0x22] = -1;
}

static void lv51_move_08(AnimObj *a) { see_saw(a, 0x67); }   /* 0x431630 */

static void lv51_move_09(AnimObj *a)
{
    /* 0x4318c0: Move_02 that ignores triggers until d1c pickups are collected */
    if (a->d[0x22] != -1 && g_pickups < a->d[0x1c]) a->d[0x22] = -1;
    move_02(a);
}

static void lv51_carry_attractors(AnimObj *a)
{
    int32_t *d = a->d;
    if (d[0x17] != 0) {
        g_attract[d[0x1d]].x = (int16_t)a->x;
        g_attract[d[0x1d]].y = (int16_t)a->y;
    }
    if (d[0x19] != 0) {
        g_attract[d[0x1e]].x = (int16_t)a->x;
        g_attract[d[0x1e]].y = (int16_t)a->y;
    }
}

static void lv51_start(AnimObj *a)
{
    /* path d16 (mode d1c), anim d15 and up to two attractors (d17 / d18, d19 / d1a: type / 10,
       power % 10, range) */
    int32_t *d = a->d;
    path_begin(a, d[0x16], d[0x1c], 0);
    anim_set(a->slot, d[0x15], -1);
    if (d[0x17] != 0) d[0x1d] = attract_new(a->x, a->y, d[0x17] / 10, d[0x18], d[0x17] % 10);
    if (d[0x19] != 0) d[0x1e] = attract_new(a->x, a->y, d[0x19] / 10, d[0x1a], d[0x19] % 10);
}

static void lv51_move_10(AnimObj *a)
{
    /* 0x4318f0: a walker along path d16 starting d1b ticks in, carrying its attractors */
    if (a->sub == 0) {
        lv51_start(a);
        for (int i = a->d[0x1b]; i > 0; i--) path_follow(a);
        a->sub = 1;
    } else if (a->sub == 1) {
        path_follow(a);
        lv51_carry_attractors(a);
    }
}

static void lv51_move_11(AnimObj *a)
{
    /* 0x431a20: Move_03 that drops an object (anim d1b) every 10-25 ticks */
    move_03(a);
    if (a->d[0x1f]-- < 1) {
        a->d[0x1f] = (crt_rand() & 0xf) + 10;
        anim_new(a->x, a->y, a->d[0x1b], -1, 3);
    }
}

static void lv51_move_12(AnimObj *a)
{
    /* 0x431a70: as 0x4318f0, playing anim d1b at the end of the path, then round again */
    int32_t *d = a->d;
    if (a->sub == 0) {
        lv51_start(a);
        a->sub = 1;
    } else if (a->sub == 1) {
        if (path_follow(a)) {
            anim_set(a->slot, d[0x1b], -1);
            a->sub = 2;
        }
        lv51_carry_attractors(a);
    } else if (a->sub == 2 && a->state == 2) {
        anim_set(a->slot, d[0x15], -1);
        path_begin(a, d[0x16], d[0x1c], 1);
        a->sub = 1;
    }
}

bool lv5_actor(uint32_t va, int32_t *actor)
{
    if (va != 0x430a40) return false;
    lv51_actor(actor);
    return true;
}

MoveFn lv5_move(uint32_t va)
{
    switch (va) {
    case 0x430ea0: return lv51_move_00;
    case 0x431030: return lv51_move_01;
    case 0x431160: return lv51_move_02;
    case 0x4311e0: return lv51_move_03;
    case 0x431250: return lv51_move_04;
    case 0x431310: return lv51_move_05;
    case 0x4314d0: return lv51_move_06;
    case 0x431590: return lv51_move_07;
    case 0x431630: return lv51_move_08;
    case 0x4318c0: return lv51_move_09;
    case 0x4318f0: return lv51_move_10;
    case 0x431a20: return lv51_move_11;
    case 0x431a70: return lv51_move_12;
    default: return NULL;
    }
}
