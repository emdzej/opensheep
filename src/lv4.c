#include "levels_int.h"

/* World 4 (4.1-4.4 share the routines and the actor hook). */

static void lv41_actor(int32_t *a)
{
    /* 0x42f5a0: the world's move types 100-104, parameters in the actor's object d15.. */
    int slot = a[9];
    AnimObj *o = &g_anim[slot];
    int32_t *d = o->d;
    float *f = (float *)a;
    switch (a[3]) {
    case 100:
        /* climb to path d15's start, ride it hidden, then fly off in direction d16 at 3 px a tick
           for 48 ticks into state d17 (0x19 straight away) */
        a[0x1d] &= ~1;
        switch (o->sub) {
        case 0:
            o->sub = 1;
            sound_play_at(o, 0, 0, 0);
            return;
        case 1: {
            a[1] = 0;
            if (a[5] == 0) a[0xe] = 6;
            path_start(d[0x15]);
            int dx = a[0xc] - g_path_x;
            if (dx < 0) f[10] = (float)((double)f[10] + 1.0);
            if (dx > 0) f[10] = (float)((double)f[10] - 1.0);
            if (a[0xd] > g_path_y) {
                f[11] = (float)((double)f[11] - 3.0);
                return;
            }
            path_begin(o, d[0x15], 0, 0);
            o->sub = 2;
            return;
        }
        case 2:
            a[1] = 0;
            if (a[5] == 0) a[0xe] = 6;
            o->rec.scale_x = 0;
            if (path_follow(o)) {
                o->rec.scale_x = 0x100;
                if (d[0x17] == 0x19) {
                    actor_set_state(a, 0x19);
                } else {
                    f[1] = 3.0f;
                    a[2] = a[0] = d[0x16];
                    o->sub = 3;
                    d[0x15] = 0x30;
                }
                sound_play_at(o, 2, 0, 0);
            }
            f[10] = (float)o->x;
            f[11] = (float)o->y;
            return;
        case 3: {
            if (a[5] == 0) a[0xe] = 6;
            float ny = (float)((double)f[11] - (double)g_cos[a[0]] * (double)f[1] + 2.0);
            f[10] = (float)((double)g_sin[a[0]] * (double)f[1] + (double)f[10]);
            f[11] = ny;
            int t = d[0x15];
            o->rec.shadow_dy = t;
            d[0x15] = t - 1;
            if (t > 0) return;
            actor_set_state(a, d[0x17]);
            return;
        }
        }
        return;
    case 0x65:
        /* state 8 (walk to path d23 and along it), then held (4) */
        a[0x1d] &= ~1;
        o->rec.shadow_dy = 0;
        o->depth_off = 0;
        sheep_pathed((Sheep *)a, slot);
        if (a[3] == 0) a[3] = 4;
        return;
    case 0x66:
        /* walk on at 3 px a tick until 32 px left of the clip line, then ride path d15; the
           shepherd also gets the level's camera bounds back */
        if (o->sub == 0) {
            int x = a[0xc];
            f[10] = (float)((double)g_sin[a[0]] * 3.0 + (double)f[10]);
            f[11] = (float)((double)f[11] - (double)g_cos[a[0]] * 3.0);
            if (x >= o->rec.clip_x0 - 0x20) return;
            o->sub = 1;
            path_begin(o, d[0x15], 0, 1);
            f[10] = (float)o->x;
            f[11] = (float)o->y;
            o->rec.clip_x0 = -1;
            if (a[9] > 0x1e) {
                g_lv.cam_min_x = (int32_t)rd32(g_lv.blo + 0x28);
                g_lv.cam_max_x = (int32_t)rd32(g_lv.blo + 0x2c);
                g_lv.cam_min_y = (int32_t)rd32(g_lv.blo + 0x30);
                g_lv.cam_max_y = (int32_t)rd32(g_lv.blo + 0x34);
                g_scroll_reset = 1;
            }
        } else if (o->sub == 1) {
            if (path_follow(o)) actor_set_state(a, 0);
            f[10] = (float)o->x;
            f[11] = (float)o->y;
        }
        return;
    case 0x67: {
        /* slides down the chute toward x 0x518..0x528 to y 0x45e, then drops out below it */
        int x = a[0xc];
        a[0xe] = a[0xf] = a[0x10] = 6;
        if (a[0xd] < 0x45e) {
            f[11] = (float)((double)f[11] + 2.5);
            if (x < 0x518) f[10] = (float)((double)f[10] + 3.5);
            if (x > 0x528) f[10] = (float)((double)f[10] - 3.5);
            return;
        }
        o->rec.clip_x0 = 0;
        o->rec.clip_x1 = 0x1f40;
        o->rec.clip_y0 = 0;
        o->rec.clip_y1 = 0x45e;
        f[11] = (float)((double)f[11] + 3.0);
        if (a[0xd] > 0x49e) actor_out(slot);
        return;
    }
    case 0x68: {
        /* drops into the machine in slot d15 (clipped 0x87 below it); it counts it in d1e */
        a[0xe] = a[0xf] = a[0x10] = 6;
        AnimObj *m = &g_anim[d[0x15]];
        o->rec.clip_x0 = 0;
        o->rec.clip_x1 = 0x1f40;
        o->rec.clip_y0 = 0;
        o->rec.clip_y1 = m->y + 0x87;
        int y = a[0xd];
        f[11] = (float)((double)f[11] + 3.0);
        if (y > o->rec.clip_y1 + 0x40) {
            actor_out(slot);
            m->d[0x1e]++;
        }
        return;
    }
    }
}

static void lv41_set_special(int s, int on)
{
    /* the routines index the specials with their parameters, -1 meaning none */
    if (s >= 0 && s < MAX_SPECIAL) g_special[s].active = (uint8_t)on;
}

static void lv41_move_00(AnimObj *a)
{
    /* 0x42f9f0: who triggers takes move type 100 along path d15 (then direction d16, state d17) */
    int32_t *d = a->d;
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        actor_set_state(w, 100);
        AnimObj *o = &g_anim[w[9]];
        o->d[0x15] = d[0x15];
        o->d[0x16] = d[0x16];
        o->d[0x17] = d[0x17];
        anim_set(a->slot, a->anim, -1);
    }
    d[0x22] = -1;
}

static void lv41_move_01(AnimObj *a)
{
    /* 0x42fa90: on frames between d19 and d1a starts a Move_03 object (anim d16) along path d15,
       special d18 off meanwhile; repeats after d17 / 6 s */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x19] < a->frame && a->frame < d[0x1a] && a->frame != a->prev_frame) {
            g_special[d[0x18]].active = 0;
            path_start(d[0x15]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
            o->move = 3;
            o->d[0x15] = d[0x16];
            o->d[0x16] = d[0x15];
            o->d[0x17] = 0;
            o->d[0x19] = 0;
            o->d[0x1c] = 1;
        }
        if (a->state == 2) {
            g_special[d[0x18]].active = 1;
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1 && d[0x1d]-- < 1) {
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
}

static void lv41_move_02(AnimObj *a)
{
    /* 0x42fb70: a grabber along path d16: catches who triggers (not kind 3; anim d15), drops it
       out of play and goes on (anim d1d); specials d17 / d19 / d1b are off from its points d18 /
       d1a / d1c */
    int32_t *d = a->d;
    lv41_set_special(d[0x17], 1);
    lv41_set_special(d[0x19], 1);
    lv41_set_special(d[0x1b], 1);
    int t = d[0x22];
    switch (a->sub) {
    case 0:
        path_begin(a, d[0x16], 0, 0);
        d[0x1d] = a->anim;
        a->sub = 1;
        d[0x22] = -1;
        return;
    case 1: {
        path_follow(a);
        int32_t *w;
        if (t != -1 && (w = actor(d[0x21]))[5] != 3) {
            d[0x1e] = d[0x21];
            w[3] = 4;
            w[0x10] = w[0xf] = w[0xe] = 6;
            anim_set(a->slot, d[0x15], (a->dir - 4) & 0xf);
            a->sub = 2;
        }
        if (d[0x24] == d[0x18]) lv41_set_special(d[0x17], 0);
        if (d[0x24] == d[0x1a]) lv41_set_special(d[0x19], 0);
        if (d[0x24] == d[0x1c]) {
            lv41_set_special(d[0x1b], 0);
            d[0x22] = -1;
            return;
        }
        break;
    }
    case 2: {
        int32_t *w = actor(d[0x1e]);
        w[0x10] = w[0xf] = w[0xe] = 6;
        if (a->frame > 2) {
            actor_out(w[9]);
            a->sub = 3;
            d[0x22] = -1;
            return;
        }
        break;
    }
    case 3:
        if (a->state == 2) {
            anim_set(a->slot, d[0x1d], (a->dir + 4) & 0xf);
            a->sub = 1;
        }
        break;
    }
    d[0x22] = -1;
}

static void lv41_move_03(AnimObj *a)
{
    /* 0x42fd40: the counter: frame 10 - sheep counted (to 0), a sound per change; at d15 counted
       it sounds 0x1b and triggers d16 */
    int32_t *d = a->d;
    if (d[0x15] <= g_counted) {
        sound_play_at(a, 0x1b, 0, 0);
        if (d[0x16] != 0) g_anim[g_obj_by_id[d[0x16]]].d[0x22] = 0x8000;
    }
    if (g_counted != d[0x1f]) sound_play_at(a, (int)rng_next() % 5 + 0x16, 0, 0);
    d[0x1f] = g_counted;
    if (g_counted > 10) d[0x1f] = 10;
    a->frame = 10 - d[0x1f];
    anim_set_frame(a);
    a->countdown = 10;
}

static void lv41_push(AnimObj *a)
{
    /* pushes the actors out of the sprite's strip (clipped at d16 on its side) */
    int x = a->x;
    if (a->mirror != 0) x -= a->rec.w;
    int w = a->rec.w;
    if (x < a->rec.clip_x0) {
        w += x - a->rec.clip_x0;
        x = a->rec.clip_x0;
    }
    if (a->rec.clip_x1 - x < w) w = a->rec.clip_x1 - x;
    if (w > 0) push_out_of_rect(x, a->y + 4, w, a->rec.h + 10);
}

static void lv41_clip(AnimObj *a)
{
    a->rec.clip_y0 = 0;
    a->rec.clip_y1 = 8000;
    if (a->mirror == 0) {
        a->rec.clip_x0 = 0;
        a->rec.clip_x1 = a->d[0x16];
    } else {
        a->rec.clip_x1 = 8000;
        a->rec.clip_x0 = a->d[0x16];
    }
}

static void lv41_move_04(AnimObj *a)
{
    /* 0x42fdf0: a piston along path d15, clipped at x d16, pausing d18 / 6 s at the end (sound d19) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x15], 1, 0);
        lv41_clip(a);
        a->sub++;
    } else if (a->sub == 1) {
        if (path_follow(a)) {
            a->sub = 2;
            d[0x1d] = d[0x18] * 6;
        }
    } else if (a->sub == 2 && d[0x1d]-- == 0) {
        a->sub = 1;
        sound_play_at(a, (int16_t)d[0x19], 0, 0);
    }
    lv41_push(a);
}

static void lv41_move_06(AnimObj *a)
{
    /* 0x430010: the see-saw: a sheep on special d15 takes path d16 (0x65); the shepherd stepping on
       d17 then is thrown at it (move type 1) and both end up in 0x15; 5 s before it works again */
    int32_t *d = a->d;
    int t = d[0x22];
    switch (a->sub) {
    case 0:
        g_special[d[0x15]].type = 1;
        g_special[d[0x17]].type = 6;
        if (t != -1) {
            int32_t *w = actor(d[0x21]);
            d[0x1b] = d[0x21];
            g_anim[w[9]].d[0x23] = d[0x16];
            g_anim[w[9]].d[0x15] = 0;
            actor_set_state(w, 0x65);
            g_special[d[0x15]].type = 5;
            g_special[d[0x17]].type = 2;
            a->sub = 1;
            d[0x22] = -1;
            return;
        }
        break;
    case 1:
        if (t != -1) {
            int32_t *p = actor(d[0x21]);
            d[0x1c] = d[0x21];
            g_special[d[0x15]].type = 5;
            g_special[d[0x17]].type = 6;
            int32_t *w = actor(d[0x1b]);
            p[0] = p[2] = dir_of(w[0xc] - p[0xc], w[0xd] - p[0xd]);
            player_set_state((Player *)p, 1);
            AnimObj *o = &g_anim[p[9]];
            o->d[0x15] = 0x280;
            o->d[0x16] = 0x280;
            o->d[8] = p[0xb];
            o->d[0x18] = 10;
            o->d[0x1b] = 0x14;
            anim_set(p[9], 3, -1);
            a->sub = 2;
            d[0x22] = -1;
            return;
        }
        break;
    case 2: {
        int32_t *w = actor(d[0x1b]), *p = actor(d[0x1c]);
        if (p[3] == 0) {
            g_special[d[0x15]].type = 0;
            g_special[d[0x17]].type = 6;
            p[3] = 0x15;
            w[3] = 0x15;
            g_anim[p[9]].d[0x21] = w[9];
            p[1] = 0;
            p[0xe] = 7;
            anim_set(p[9], 7, -1);
            d[0x1d] = 300;
            a->sub = 3;
            d[0x22] = -1;
            return;
        }
        break;
    }
    case 3:
        if (d[0x1d]-- < 1) a->sub = 0;
        break;
    }
    d[0x22] = -1;
}

static void lv41_move_07(AnimObj *a)
{
    /* 0x4302a0: a launcher: throws who triggers (direction d16, d17 x 10 ticks, twice for kind
       0x11, three times for kind 1; height d1a, then d18, speed d1b), anim d15, d19 / 6 s rest;
       the trigger's value names the special it holds on meanwhile */
    int32_t *d = a->d;
    int t = d[0x22];
    if (a->sub == 0) {
        if (t != -1) {
            lv41_set_special(t, 1);
            int32_t *w = actor(d[0x21]);
            int c = d[0x17];
            if (w[5] == 0x11) c *= 2;
            if (w[5] == 1) c *= 3;
            sheep_launch_sound(w, d[0x16], c * 10, d[0x1a], d[0x18], d[0x1b]);
            d[0x1f] = a->anim;
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
            d[0x1c] = d[0x19] * 6;
        }
    } else if (a->sub == 1) {
        if (d[0x1c]-- < 1) {
            a->sub = 0;
            lv41_set_special(t, 0);
            d[0x22] = -1;
            if (a->anim != d[0x1f]) anim_set(a->slot, d[0x1f], -1);
        }
        if (a->anim == d[0x15] && a->state == 2) anim_set(a->slot, d[0x1f], -1);
    }
}

static void lv41_move_08(AnimObj *a)
{
    /* 0x42ff00: as 0x42fdf0, a piston that waits at the end for a trigger */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x15], 1, 0);
        lv41_clip(a);
        a->sub = 2;
    } else if (a->sub == 1) {
        if (path_follow(a)) {
            d[0x1d] = d[0x18] * 6;
            a->sub = 2;
        }
    } else if (a->sub == 2 && d[0x22] != -1) {
        sound_play_at(a, (int16_t)d[0x19], 0, 0);
        a->sub = 1;
    }
    lv41_push(a);
    d[0x22] = -1;
}

static void lv41_move_09(AnimObj *a)
{
    /* 0x4303a0: Move_05 that ignores triggers until d1c pickups are collected */
    if (a->d[0x22] != -1 && g_pickups < a->d[0x1c]) a->d[0x22] = -1;
    move_05(a);
}

static void lv41_move_10(AnimObj *a)
{
    /* 0x4303d0: a switch: special d15 on and tile animation d16 off until triggered, then the
       other way round for good */
    int32_t *d = a->d;
    g_tile_anim[d[0x16]].mode = 0;
    g_special[d[0x15]].active = 1;
    if (d[0x22] != -1) {
        g_tile_anim[d[0x16]].mode = 1;
        g_special[d[0x15]].active = 0;
        a->move = 0;
    }
}

static void lv41_move_11(AnimObj *a)
{
    /* 0x430420: a lift on path d15 whose stop (point 0-3) the triggers d16..d19 choose; sheep
       standing in it are thrown toward (0x214, 0x336) (anim 8), those beside it fall (0xd) */
    int32_t *d = a->d;
    int t = d[0x22];
    if (t != -1) {
        if (t == d[0x16]) d[0x1d] = 0;
        if (t == d[0x17]) d[0x1d] = 1;
        if (t == d[0x18]) d[0x1d] = 2;
        if (t == d[0x19]) d[0x1d] = 3;
    }
    int px, py;
    path_point(d[0x15], d[0x1d], &px, &py);
    int dy = py - a->y;
    if (dy > 2) dy = 2;
    else if (dy < -2) dy = -2;
    a->y += dy;
    a->x = px;
    for (int i = 0; i < MAX_SHEEP; i++) {
        Sheep *s = &g_sheep[i];
        if (s->alive == 2 || s->state != 0) continue;
        int x = s->ix, y = s->iy;
        if (0x17e < x && x < 0x1a0 && a->y - 4 < y && y < a->y + 0x47) {
            int n = (iabs(y - 0x336) + iabs(x - 0x214)) * 9;
            sheep_launch_sound(s->d, dir_of(0x214 - x, 0x336 - y), n, 0x32, 0, 0x1e);
            anim_set(a->slot, 8, -1);
            continue;
        }
        if (0x100 < x && x < 0x19e && 0x210 < y && y < 0x378) actor_set_state(s->d, 0xd);
    }
    if (a->anim == 8 && a->state == 2) anim_set(a->slot, 0x5a, -1);
    d[0x22] = -1;
}

static void lv41_move_12(AnimObj *a)
{
    /* 0x4305c0: loops with a d17 / 6 s pause (held while triggered); special d18's value is 0x19
       on frames d15..d16 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1 && d[0x22] == -1 && d[0x1d]-- < 1) {
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
    g_special[d[0x18]].a[0] = d[0x15] <= a->frame && a->frame <= d[0x16] ? 0x19 : 0;
}

static void lv41_move_13(AnimObj *a)
{
    /* 0x430650: who triggers walks on in direction d15 (0x66), clipped at x d16 and y d17, then
       rides path d18 */
    int32_t *d = a->d;
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        AnimObj *o = &g_anim[w[9]];
        w[0] = d[0x15];
        o->rec.clip_x0 = d[0x16];
        o->rec.clip_x1 = 8000;
        o->rec.clip_y0 = 0;
        o->rec.clip_y1 = d[0x17];
        o->d[0x15] = d[0x18];
        actor_set_state(w, 0x66);
    }
    d[0x22] = -1;
}

static void lv41_move_14(AnimObj *a)
{
    /* 0x4306d0: once objects d16, d17 and d18 have all started, plays anim d15 and opens gate d19 */
    int32_t *d = a->d;
    if (a->sub != 0) return;
    int c = 0;
    for (int i = 0x16; i <= 0x18; i++) c += g_anim[g_obj_by_id[d[i]]].sub != 0;
    if (c > 2) {
        anim_set(a->slot, d[0x15], -1);
        gate_set(d[0x19], 1);
        a->sub = 1;
    }
}

static void lv41_move_15(AnimObj *a)
{
    /* 0x430770: the machine: restarts object d15, drops a product (anim d16, or a sheep of the
       breed while d1e lasts) at one of five spots (0x4538d8 x, 0x4538ec y) and slides object d15
       to its x (0x453900) over 2 s; who triggers goes in (0x68) */
    int32_t *d = a->d;
    a->rec.mode = 3;
    a->rec.layer = 0;
    AnimObj *m = &g_anim[g_obj_by_id[d[0x15]]];
    if (a->sub == 0) {
        anim_set(m->slot, m->anim, -1);
        uint32_t r = 4u * (uint32_t)((int)rng_next() % 5);
        int x = (int32_t)exe_u32(0x4538d8 + r) + a->x, y = (int32_t)exe_u32(0x4538ec + r) + a->y;
        if (d[0x1e] < 1) {
            anim_new(x, y, d[0x16], -1, 3);
        } else {
            anim_new(x, y, d[0x16] + 1 + g_breed, -1, 3);
            d[0x1e]--;
        }
        a->sub = 1;
        d[0x1f] = (int32_t)exe_u32(0x453900 + r) + a->x;
    } else if (a->sub == 1) {
        if (m->x < d[0x1f]) m->x++;
        if (d[0x1f] < m->x) m->x--;
        if (m->state == 2) {
            d[0x1d] = 0x78;
            a->sub = 2;
        }
    } else if (a->sub == 2 && d[0x1d]-- < 1) {
        a->sub = 0;
    }
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        actor_set_state(w, 0x68);
        g_anim[w[9]].d[0x15] = a->slot;
        d[0x22] = -1;
    }
}

static void lv41_move_16(AnimObj *a)
{
    /* 0x4308f0: on frame d15 starts an object (anim d1c, routine 3) along path d1b; special d19's
       value is d1a on frames d16..d17; repeats after d17 / 6 s */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->frame == d[0x15] && a->frame != a->prev_frame) {
            path_start(d[0x1b]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x1c], -1, 3)];
            o->d[0x15] = d[0x1c];
            o->d[0x16] = d[0x1b];
            o->d[0x1c] = 4;
            o->move = 3;
        }
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1 && d[0x1d]-- < 1) {
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
    g_special[d[0x19]].a[0] = d[0x16] <= a->frame && a->frame <= d[0x17] ? (int16_t)d[0x1a] : 0;
}

static void lv41_move_17(AnimObj *a)
{
    /* 0x4309f0: Move_05 that, once past sub-state 1, turns special 0 and 52 on, stops tile
       animations 1-4 and itself */
    move_05(a);
    if (a->sub > 1) {
        g_special[0].active = 1;
        for (int i = 1; i <= 4; i++) g_tile_anim[i].mode = 0;
        g_special[52].active = 1;
        a->move = 0;
    }
}

bool lv4_actor(uint32_t va, int32_t *actor)
{
    if (va != 0x42f5a0) return false;
    lv41_actor(actor);
    return true;
}

bool lv4_call(uint32_t va)
{
    if (va != 0x42f530) return false;
    /* 4.2's init: the clip rectangles of objects 928 and 929 */
    AnimObj *o = &g_anim[g_obj_by_id[928]];
    o->rec.clip_x0 = 0;
    o->rec.clip_x1 = 0x360;
    o->rec.clip_y0 = 0;
    o->rec.clip_y1 = 0x1f40;
    o = &g_anim[g_obj_by_id[929]];
    o->rec.clip_x0 = 0;
    o->rec.clip_x1 = 0x1f40;
    o->rec.clip_y0 = 0;
    o->rec.clip_y1 = 0x168;
    return true;
}

MoveFn lv4_move(uint32_t va)
{
    switch (va) {
    case 0x42f9f0: return lv41_move_00;
    case 0x42fa90: return lv41_move_01;
    case 0x42fb70: return lv41_move_02;
    case 0x42fd40: return lv41_move_03;
    case 0x42fdf0: return lv41_move_04;
    case 0x430010: return lv41_move_06;
    case 0x4302a0: return lv41_move_07;
    case 0x42ff00: return lv41_move_08;
    case 0x4303a0: return lv41_move_09;
    case 0x4303d0: return lv41_move_10;
    case 0x430420: return lv41_move_11;
    case 0x4305c0: return lv41_move_12;
    case 0x430650: return lv41_move_13;
    case 0x4306d0: return lv41_move_14;
    case 0x430770: return lv41_move_15;
    case 0x4308f0: return lv41_move_16;
    case 0x4309f0: return lv41_move_17;
    default: return NULL;
    }
}
