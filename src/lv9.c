#include "levels_int.h"

/* World 9 (the training levels 9.1-9.x share the routines and the actor hook). */

static void lv91_actor(int32_t *a)
{
    /* 0x435e10: state 100, carried along path d15 (anim 0x50, sound 3), then held */
    if (a[3] != 100) return;
    AnimObj *o = &g_anim[a[9]];
    int32_t *d = o->d;
    float *f = (float *)a;
    if (o->sub == 0) {
        path_begin(o, d[0x15], 0, 0);
        sound_play_at(o, 3, 0, 0);
        d[0x16] = 0x50;
        o->sub = 1;
    } else if (o->sub == 1) {
        a[0xe] = a[0xf] = a[0x10] = d[0x16];
        if (path_follow(o)) {
            a[3] = 4;
            a[4] = 1;
        }
        f[10] = (float)o->x;
        a[2] = a[0] = 0x80;
        f[11] = (float)o->y;
    }
}

static void lv91_move_01(AnimObj *a)
{
    /* 0x435ec0: plays through, rests d17 x 6 ticks and restarts; special d18 is free while the frame
       is within d15..d16 or d19..d1a */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1) {
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            anim_set(a->slot, a->anim, -1);
            a->sub = 0;
        }
    }
    g_special[d[0x18]].active = 1;
    if (d[0x15] <= a->frame && a->frame <= d[0x16]) g_special[d[0x18]].active = 0;
    if (d[0x19] <= a->frame && a->frame <= d[0x1a]) g_special[d[0x18]].active = 0;
}

static void lv91_move_02(AnimObj *a)
{
    /* 0x435f60: Move_10 that, once it has its sheep (d20), shows anim 0x48 and stops */
    move_10(a);
    if (a->d[0x20] != 0) {
        anim_set(a->slot, 0x48, 10);
        a->move = 0;
    }
}

static int ticks_of(AnimObj *a)
{
    /* the current point's ticks (the original divides by them even if 0) */
    int t = path_ticks(a->d[0x23], a->d[0x24]);
    return t ? t : 1;
}

static void lv91_move_03(AnimObj *a)
{
    /* 0x435f90: a balloon along path d15 dragging a knock-back attractor (one a tick for d16
       ticks first): low at points 0 and 4, rising over point 1, down at 2 (then drawn as ground),
       sinking over 3; the attractor is parked off the map while it is up */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x15], 1, 1);
        d[0x1d] = attract_new(a->x, a->y, 3, 0x28, 0);
        int t = d[0x16];
        d[0x16] = t - 1;
        if (t < 1) a->sub = 1;
        return;
    }
    if (a->sub != 1) return;
    path_follow(a);
    Attractor *t = &g_attract[d[0x1d]];
    t->x = (int16_t)a->x;
    t->y = (int16_t)a->y;
    int v;
    switch (d[0x24]) {
    case 0:
    case 4:
        v = 0x40;
        t->x = -100;
        a->rec.shadow_dy = 0x40;
        a->y -= 0x40;
        break;
    case 1:
        t->x = -100;
        v = 0x40 - (d[0x25] << 6) / ticks_of(a);
        a->rec.shadow_dy = v;
        a->y -= v;
        break;
    case 2:
        a->rec.shadow_dy = 0;
        a->depth_off = 0;
        a->rec.mode = 1;
        a->rec.layer = 1;
        a->rec.sortable = 1;
        return;
    case 3:
        t->x = -100;
        v = (d[0x25] << 6) / ticks_of(a);
        a->rec.shadow_dy = v;
        a->y -= v;
        break;
    default:
        return;
    }
    a->depth_off = v;
    a->rec.mode = 2;
    a->rec.layer = 0;
    a->rec.sortable = 0;
}

static void lv91_move_04(AnimObj *a)
{
    /* 0x436140: trigger d15 plays anim d17 once, trigger d16 anim d18 and then it is done */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] == d[0x16]) {
            anim_set(a->slot, d[0x18], -1);
            a->sub = 2;
        }
        if (d[0x22] == d[0x15]) {
            anim_set(a->slot, d[0x17], -1);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (d[0x22] == d[0x16]) {
            anim_set(a->slot, d[0x18], -1);
            a->sub = 2;
        }
        if (a->state == 2) a->sub = 0;
    } else if (a->sub == 2 && a->state == 2) {
        a->sub = 3;
    }
    d[0x22] = -1;
}

static void lv91_move_05(AnimObj *a)
{
    /* 0x436210: Move_02 touched only by kind 10 */
    if (a->d[0x22] != -1 && actor(a->d[0x21])[5] != 10) a->d[0x22] = -1;
    move_02(a);
}

static void lv91_move_06(AnimObj *a)
{
    /* 0x436250: touched, it goes off (anim d15, a puff) and every sheep within 80 px (Manhattan)
       takes fright (0x19) */
    int32_t *d = a->d;
    if (d[0x22] == -1) return;
    anim_set(a->slot, d[0x15], -1);
    anim_new(a->x, a->y, 0xe, -1, 4);
    for (int i = 0; i < MAX_SHEEP; i++) {
        Sheep *s = &g_sheep[i];
        if (s->alive != 2 && s->state != 0x19 && iabs(s->iy - a->y) + iabs(s->ix - a->x) < 0x50)
            actor_set_state(s->d, 0x19);
    }
    a->move = 0;
}

static void lv91_move_07(AnimObj *a)
{
    /* 0x4362e0: special d15 is free while the frame is within d16..d17; at the end anim d19 for
       d18 x 6 ticks, then anim d1a */
    int32_t *d = a->d;
    if (a->sub == 0) {
        g_special[d[0x15]].active = a->frame < d[0x16] || d[0x17] < a->frame;
        if (a->state == 2) {
            anim_set(a->slot, d[0x19], -1);
            a->sub = 1;
            d[0x1d] = d[0x18] * 6;
        }
    } else if (a->sub == 1) {
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            anim_set(a->slot, d[0x1a], -1);
            a->sub = 0;
        }
    }
}

static void lv91_move_08(AnimObj *a)
{
    /* 0x436370: along path d16 (mode d1c) with anim d15; a sheep or the shepherd within 16 px
       (Manhattan) makes it react (anim d17, sound 9 / 10) until that has played */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x16], d[0x1c], 0);
        anim_set(a->slot, d[0x15], -1);
        a->sub = 1;
    } else if (a->sub == 1) {
        path_follow(a);
        for (int i = 0; i < MAX_SHEEP; i++) {
            Sheep *s = &g_sheep[i];
            if (s->alive == 2) continue;
            int sx = ftol(s->x), sy = ftol(s->y);
            if (iabs(a->x - sx) + iabs(a->y - sy) < 0x10) {
                anim_set(a->slot, d[0x17], -1);
                sound_play_at(a, (int)(rng_next() & 1) + 9, 0, 0);
                a->sub = 2;
            }
        }
        if (g_player.cooldown == 0 && iabs(a->y - g_player.iy) + iabs(a->x - g_player.ix) < 0x10) {
            anim_set(a->slot, d[0x17], -1);
            sound_play_at(a, (int)(rng_next() & 1) + 9, 0, 0);
            a->sub = 2;
        }
    } else if (a->sub == 2) {
        path_follow(a);
        if (a->state == 2) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
    }
}

bool lv9_actor(uint32_t va, int32_t *actor)
{
    if (va != 0x435e10) return false;
    lv91_actor(actor);
    return true;
}

MoveFn lv9_move(uint32_t va)
{
    switch (va) {
    case 0x435ec0: return lv91_move_01;
    case 0x435f60: return lv91_move_02;
    case 0x435f90: return lv91_move_03;
    case 0x436140: return lv91_move_04;
    case 0x436210: return lv91_move_05;
    case 0x436250: return lv91_move_06;
    case 0x4362e0: return lv91_move_07;
    case 0x436370: return lv91_move_08;
    default: return NULL;
    }
}
