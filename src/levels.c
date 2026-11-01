#include "levels.h"
#include "exe.h"
#include "level.h"
#include "moves.h"
#include "paths.h"
#include "player.h"
#include "rng.h"
#include "sheep.h"
#include "sound.h"
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void unported(const char *what, uint32_t va)
{
    static uint32_t said[64];
    for (int i = 0; i < 64; i++) {
        if (said[i] == va) return;
        if (!said[i]) { said[i] = va; break; }
    }
    fprintf(stderr, "level %s hook 0x%x not ported\n", what, va);
}

static int iabs(int v) { return v < 0 ? -v : v; }

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* g_obj_by_id[id] as a slot (the original uses whatever is there). */
static AnimObj *obj(int id)
{
    if (id < 0 || id >= MAX_OBJ_ID) return NULL;
    int s = g_obj_by_id[id];
    return s >= 0 && s < MAX_ANIM ? &g_anim[s] : NULL;
}

/* ------------------------------------------------------------------ world 1 level 1 (0x451ff0) */

static void lv11_actor(int32_t *a)
{
    /* 0x42aee0: state 100 is a sheep thrown off the tractor's trailer */
    Sheep *s = (Sheep *)a;
    if (s->state != 100) return;
    AnimObj *o = &g_anim[s->slot];
    sheep_knocked(s, s->slot);
    s->anim[0] = s->anim[1] = s->anim[2] = 0x1d;
    o->frame = 0;
    anim_set_frame(o);
}

static void lv11_place(AnimObj *a)
{
    int32_t *d = a->d;
    d[0x1d] = 0x3c;
    path_start(d[0x1a]);
    Sheep *w = (Sheep *)actor(d[0x21]);   /* x, y at +0x28 in the shepherd too */
    w->x = (float)g_path_x;
    w->y = (float)g_path_y;
    AnimObj *t = obj(d[0x1b]);
    if (t) { t->d[0x22] = d[0x21]; t->d[0x21] = d[0x21]; }
}

static void lv11_anim(int id, int anim)
{
    if (obj(id)) anim_set(g_obj_by_id[id], anim, -1);
}

static void lv11_move_01(AnimObj *a)
{
    /* 0x42b000: a hiding place; d15 / d16 its anims in use / idle, d17 / d18 the same for object d19,
       d1a the path whose start the actor (d21) is put on, d1b the object told who */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x15], -1);
            lv11_anim(d[0x19], d[0x17]);
            lv11_place(a);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (d[0x22] != -1) lv11_place(a);
        if (--d[0x1d] < 1) {
            anim_set(a->slot, d[0x16], -1);
            lv11_anim(d[0x19], d[0x18]);
            a->sub = 0;
        }
    }
    d[0x22] = -1;
}

static void lv11_move_03(AnimObj *a)
{
    /* 0x42b1a0: on a trigger leaves anim d15 behind (drawn under), opens gate d18 and, after a
       second, follows path d17 away; d16 its anim meanwhile */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] != -1) {
            path_start(d[0x17]);
            g_anim[anim_new(a->x, a->y, d[0x15], -1, 3)].rec.mode = 3;
            a->x = g_path_x;
            a->y = g_path_y;
            anim_set(a->slot, d[0x16], -1);
            d[0x1d] = 0x3c;
            gate_set(d[0x18], 1);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (d[0x1d]-- < 1) {
            path_begin(a, d[0x17], 0, 0);
            a->sub = 2;
        }
    } else if (a->sub == 2) {
        if (path_follow(a)) anim_kill(a);
    }
}

static void lv11_set5(AnimObj *a, int anim, bool rnd)
{
    /* objects d15-d19 */
    for (int i = 0; i < 5; i++) {
        int s = g_obj_by_id[a->d[0x15 + i]];
        anim_set(s, anim, -1);
        if (rnd) g_anim[s].countdown = (int32_t)(rng_next() & 0x1f);
    }
}

static void lv11_move_04(AnimObj *a)
{
    /* 0x42b280: a sprinkler over special d1b; touched there it plays d1a (with d15-d19) for 0x78
       ticks and sends whoever into move type 6; touched anywhere else it freezes for good */
    int32_t *d = a->d;
    int t = d[0x22], who = d[0x21];
    switch (a->sub) {
    case 0:
        d[0x20] = 0;
        a->sub = 1;
        break;
    case 1:
        if (a->frame == 0 && a->prev_frame != 0) sound_play_at(a, 0x1a, 0, 0);
        if (t == -1) break;
        if (t != d[0x1b]) {
            g_special[d[0x1b]].active = 1;
            a->sub = 3;
            break;
        }
        d[0x1e] = a->anim;
        d[0x1d] = 0x78;
        anim_set(a->slot, d[0x1a], -1);
        a->countdown = (int32_t)(rng_next() & 0x1f);
        lv11_set5(a, d[0x1a], true);
        actor_set_state(actor(who), 6);
        g_special[d[0x1b]].type = 5;
        a->sub = 2;
        if (d[0x20] == 0) d[0x20] = sound_play_at(a, 0x18, 1, 0);
        break;
    case 2:
        if (t == -1 || t == d[0x1b]) {
            if (d[0x1d]-- > 0) break;
            anim_set(a->slot, d[0x1e], -1);
            lv11_set5(a, d[0x1e], false);
            g_special[d[0x1b]].type = 1;
            a->sub = 1;
        } else {
            g_special[d[0x1b]].active = 1;
            a->sub = 3;
        }
        d[0x20] = 0;   /* TODO: stop the voice (0x406650) */
        break;
    case 3:
        a->countdown = 10;
        for (int i = 0; i < 5; i++) g_anim[g_obj_by_id[d[0x15 + i]]].countdown = 10;
        break;
    }
    d[0x22] = -1;
}

static void lv11_move_05(AnimObj *a)
{
    /* 0x42b560: after d16 / 6 s follows path d15, scaled by d17 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x15], 0, 0);
        a->sub = 1;
        d[0x16] *= 6;
        a->rec.scale_y = d[0x17];
        a->rec.scale_x = d[0x17];
    } else if (a->sub == 1) {
        if (d[0x16]-- < 1) a->sub = 2;
    } else if (a->sub == 2) {
        path_follow(a);
    }
}

static float lv11_dist(AnimObj *a)
{
    /* 0x42baf0: to the shepherd */
    double dx = (double)a->x - ftol((double)g_player.x), dy = (double)a->y - ftol((double)g_player.y);
    double r = sqrt(dy * dy + dx * dx);
    return (float)(r < 0.0 ? -r : r);
}

static void lv11_move_08(AnimObj *a)
{
    /* 0x42b8a0: Move_03 that calls out (sound 5) when the shepherd comes within 40, then waits
       150-349 ticks */
    int32_t *d = a->d;
    if (a->sub == 0) d[0x1f] = 0;
    move_03(a);
    if (d[0x1f] == 0) {
        if (lv11_dist(a) <= 40.0f) {
            sound_play_at(a, 5, 0, 0);
            d[0x1f] = crt_rand() % 200 + 0x96;
        }
    } else if (--d[0x1f] == 0) {
        d[0x1f] = 0;
    }
}

static void lv11_move_09(AnimObj *a)
{
    /* 0x42ba60: Move_10 with an engine: sound 0x11 waiting, 0x12 once it sets off */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->sound_on == 0) {
            a->sound_on = 1;
            d[0x1f] = sound_play_at(a, 0x11, 1, 0);
        }
    } else if (a->sub == 3) {
        /* TODO: stop the voice d1f (0x406650) */
        d[0x1f] = sound_play_at(a, 0x12, 1, 0);
    }
    move_10(a);
}

static void lv11_noise(AnimObj *a, int s1, int s2)
{
    /* 0x42b910 / 0x42b990: Move_19 making one of two noises every 100-399 ticks */
    int32_t *d = a->d;
    if (a->sub == 0) d[0x17] = 0;
    if (d[0x17] == 0) {
        d[0x17] = crt_rand() % 300 + 100;
        sound_play_at(a, crt_rand() % 100 > 0x31 ? s1 : s2, 0, 0);
    } else {
        d[0x17]--;
    }
    move_19(a);
}

static void lv11_move_10(AnimObj *a) { lv11_noise(a, 1, 2); }
static void lv11_move_11(AnimObj *a) { lv11_noise(a, 0x11, 0x12); }

static void lv11_move_12(AnimObj *a)
{
    /* 0x42ba10: Move_05 with one of sounds 0x13-0x17 as it is touched */
    if (a->frame == 1 && a->prev_frame != 1 && a->sub == 1) sound_play_at(a, crt_rand() % 5 + 0x13, 0, 0);
    move_05(a);
}

static void lv11_move_13(AnimObj *a)
{
    /* 0x42bb50: Move_03 with sound 8 now and then */
    move_03(a);
    if ((rng_next() & 0x7f) == 0) sound_play_at(a, 8, 0, 0);
}

static void lv11_move_14(AnimObj *a)
{
    /* 0x42bb80: Move_03 with its shadow 0x30 lower */
    move_03(a);
    a->rec.shadow_dy = 0x30;
}

static void lv11_move_06(AnimObj *a)
{
    /* 0x42b5d0: the tractor drives left and wraps round; it shoves the shepherd, who is flattened
       when already down (anim 5) */
    float px = g_player.x, py = g_player.y;
    if (--a->x < -0x100) a->x = g_lv.cam_max_x + 100;
    push_out_of_rect(a->x - 0x96, a->y + 0x32, 0x96, 0x78);
    if (g_player.x != px || g_player.y != py) {
        AnimObj *p = &g_anim[PLAYER_SLOT];
        if (p->anim == 5) {
            p->frame = 0xb;
            p->countdown = 0x1e;
            p->rec.mode = 3;
            anim_set_frame(p);
            g_player.x = px;
            g_player.y = py;
            return;
        }
        if (g_player.x <= 32.0f) g_player.x = 32.0f;
    }
}

static void lv11_move_07(AnimObj *a)
{
    /* 0x42b6c0: the trailer, d15 the tractor it follows; sheep inside the box d16..d19 are tossed
       (state 100) while it is on the field (x < 1201), and the shepherd is knocked over */
    int32_t *d = a->d;
    AnimObj *o = obj(d[0x15]);
    if (!o) return;
    if (a->sub == 0) {
        d[0x1d] = a->x - o->x;
        a->sub = 1;
        d[0x1e] = a->y - o->y;
    }
    int x = d[0x1d] + o->x, y = d[0x1e] + o->y;
    a->x = x;
    int x0 = d[0x16] + x, x1 = d[0x17] + x, y0 = d[0x18] + y, y1 = d[0x19] + y;
    a->y = y;
    if (x < 0x4b1) {
        for (int i = 0; i < MAX_SHEEP; i++) {
            Sheep *s = &g_sheep[i];
            if (s->alive != 2 && (float)x0 < s->x && s->x < (float)x1 && (float)y0 < s->y && s->y < (float)y1) {
                s->x = (float)(a->x + 0x74);
                s->y = (float)(a->y + 0x2d);
                sheep_launch_sound(s->d, 0x40, 1000, 0x1e, 0, 0x23);
                s->kind = 4;
                s->state = 100;
                sound_play_at(a, 0xb, 0, 0);
            }
        }
    } else {
        push_out_of_rect(x0, y0, x1 - x0, y1 - y0);
    }
    if ((float)x0 < g_player.x && g_player.x < (float)x1 && (float)y0 < g_player.y && g_player.y < (float)y1 &&
        g_player.state == 0) {
        g_player.speed = 0;
        g_player.state = 10;
        anim_set(g_player.slot, 5, -1);
    }
}

/* ------------------------------------------------------------------ world 2 (shared by 2.1-2.4) */

static void lv21_actor(int32_t *a)
{
    /* 0x42bba0: state 100 is the plughole: the actor spirals into (o d15, d16), shrinking, and is lost
       (a sheep) or put back beside it (the shepherd) */
    if (a[3] != 100) return;
    int slot = a[9];
    AnimObj *o = &g_anim[slot];
    int32_t *d = o->d;
    float *f = (float *)a;
    if (o->sub == 0) {
        int dx = iabs(a[0xc] - d[0x15]), dy = iabs(a[0xd] - d[0x16]);
        d[0x17] = ftol(sqrt((double)(dy * dy + dx * dx)));
        d[0x18] = dir_of(a[0xc] - d[0x15], a[0xd] - d[0x16]);
        o->sub = 1;
    } else if (o->sub == 1) {
        if (slot < MAX_SHEEP) a[0x1d] &= ~1;
        d[0x18] = (d[0x18] + 8) & 0xff;
        f[10] = (float)((double)d[0x17] * g_sin[d[0x18]] + d[0x15]);
        f[11] = (float)((double)d[0x16] - (double)d[0x17] * g_cos[d[0x18]]);
        if ((d[0x1d]++ & 3) == 0) d[0x17]--;
        if (d[0x17] < 0x40) o->rec.scale_x = o->rec.scale_y = d[0x17] * 4;
        if (d[0x17] > 0) return;
        if (slot < MAX_SHEEP) {
            sheep_out(slot);
            return;
        }
        f[10] = (float)(d[0x15] + 0x20);
        f[11] = (float)(d[0x16] + 0x64);
        o->rec.scale_y = o->rec.scale_x = 0x100;
        player_set_state((Player *)a, 0);
    }
}

/* The original sends the shepherd to 0x4032c0 too, which writes past the flock; only sheep go. */
static void actor_out(int n)
{
    if (n < MAX_SHEEP) sheep_out(n);
}

static void freeze_list(uint32_t va, uint32_t stride)
{
    /* object ids, 0-terminated: hold their animations */
    for (uint32_t id; (id = exe_u32(va)) != 0; va += stride) g_anim[g_obj_by_id[id]].countdown = 10;
}

static void lv21_spawn(AnimObj *a, int move)
{
    /* an object with anim d16 at path d15's start, with d15 / d16 swapped as its parameters */
    int32_t *d = a->d;
    path_start(d[0x15]);
    AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
    o->move = move;
    o->d[0x15] = d[0x16];
    o->d[0x16] = d[0x15];
    if (move != 4) return;
    o->d[0x17] = 10;
    o->d[0x18] = 10;
    o->d[0x19] = 0x20;
    o->d[0x1a] = g_theme == 6 ? 0x10 : 0x1b;
    o->d[0x1c] = 1;
}

static void lv21_move_00(AnimObj *a)
{
    /* 0x42bd10: on frames between d19 and d1a starts a Move_04 object along path d15 (anim d16)
       that sends sheep into move type 0x1b (0x10 in theme 6); when its animation ends it waits
       d17 / 6 s, restarts object d18's animation and its own */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x19] < a->frame && a->frame < d[0x1a] && a->frame != a->prev_frame) lv21_spawn(a, 4);
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1 && d[0x1d]-- < 1) {
        if (d[0x18] != 0) {
            int s = g_obj_by_id[d[0x18]];
            anim_set(s, g_anim[s].anim, -1);
        }
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
}

static void lv21_warp(AnimObj *a, bool own_bounds)
{
    /* 0x42be30 / 0x42bee0: puts whoever touches it at (d15, d16); the shepherd also gets the
       camera bounds (d17, d18, 640 x 480) or the level's */
    int32_t *d = a->d;
    if (d[0x22] != -1) {
        if (d[0x21] > 0x1e) {
            if (own_bounds) {
                g_lv.cam_min_x = d[0x17];
                g_lv.cam_max_x = d[0x17] + 0x280;
                g_lv.cam_min_y = d[0x18];
                g_lv.cam_max_y = d[0x18] + 0x1e0;
            } else {
                g_lv.cam_min_x = (int32_t)rd32(g_lv.blo + 0x28);
                g_lv.cam_max_x = (int32_t)rd32(g_lv.blo + 0x2c);
                g_lv.cam_min_y = (int32_t)rd32(g_lv.blo + 0x30);
                g_lv.cam_max_y = (int32_t)rd32(g_lv.blo + 0x34);
            }
            g_player.x = (float)d[0x15];
            g_player.y = (float)d[0x16];
            g_scroll_reset = 1;
            d[0x22] = -1;
            return;
        }
        int32_t *w = actor(d[0x21]);
        ((float *)w)[10] = (float)d[0x15];
        ((float *)w)[11] = (float)d[0x16];
        if (own_bounds && (w[3] == 1 || w[3] == 7)) actor_set_state(w, 0);
    }
    d[0x22] = -1;
}

static void lv21_move_01(AnimObj *a) { lv21_warp(a, true); }
static void lv21_move_02(AnimObj *a) { lv21_warp(a, false); }

static void lv21_catch(AnimObj *a, int sp, int gate)
{
    /* who triggered: held at (d15, d16) facing d17 (move type 4) */
    int32_t *d = a->d;
    g_special[sp].active = 1;
    d[0x1f] = d[0x21];
    int32_t *w = actor(d[0x21]);
    ((float *)w)[10] = (float)d[0x15];
    ((float *)w)[11] = (float)d[0x16];
    w[3] = 4;
    w[2] = d[0x17];
    gate_set(gate, 1);
}

static void lv21_release(AnimObj *a, bool sound)
{
    a->sub = 2;
    sheep_launch_sound(actor(a->d[0x1f]), a->d[0x18], 100, 0x14, 0, 0x14);
    if (sound) anim_stop_sound(a);
}

static void lv21_move_03(AnimObj *a)
{
    /* 0x42bf70: the first contraption: holds what triggers it while the scenery (0x452a68..) is
       rearranged, then throws it in direction d18 after 300 ticks */
    int32_t *d = a->d;
    if (a->sub == 0) {
        freeze_list(0x452a68, 8);
        if (d[0x22] == -1) return;
        lv21_catch(a, 29, 2);
        for (uint32_t p = 0x452aa0; p < 0x452ac0; p += 4) anim_kill(&g_anim[g_obj_by_id[exe_u32(p)]]);
        for (uint32_t p = 0x452ac0; p < 0x452b00; p += 16) {
            path_start((int)exe_u32(p + 4));
            AnimObj *o = &g_anim[g_obj_by_id[exe_u32(p)]];
            o->x = g_path_x;
            o->y = g_path_y;
            Special *from = &g_special[exe_u32(p + 8)];
            g_special[exe_u32(p + 12)] = *from;
            from->active = 1;
        }
        d[0x1d] = 300;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (d[0x1d]-- < 1) lv21_release(a, true);
    } else if (a->sub == 2) {
        freeze_list(0x452a68, 8);
    }
}

static void lv21_move_04(AnimObj *a)
{
    /* 0x42c1b0: a cycle: d19 for 240 ticks with specials d15-d17 busy, then d18 for 120 */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
    case 2: {
        bool busy = a->sub == 0;
        anim_set(a->slot, busy ? d[0x19] : d[0x18], -1);
        for (int i = 0; i < 3; i++) g_special[d[0x15 + i]].active = busy;
        d[0x1d] = busy ? 0xf0 : 0x78;
        a->sub = busy ? 1 : 3;
        break;
    }
    case 1:
        if (d[0x1d]-- < 1) a->sub = 2;
        break;
    case 3:
        if (d[0x1d]-- < 1) a->sub = 0;
        break;
    }
}

static void lv21_move_05(AnimObj *a)
{
    /* 0x42c2a0: the second contraption (Lv21_Move_03 with gate 3 and object 91 set to anim 8) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        freeze_list(0x452b00, 4);
        if (d[0x22] == -1) return;
        lv21_catch(a, 34, 3);
        anim_set(g_obj_by_id[91], 8, -1);
        d[0x1d] = 300;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (d[0x1d]-- < 1) lv21_release(a, true);
    } else if (a->sub == 2) {
        freeze_list(0x452b00, 4);
    }
}

static void lv21_move_06(AnimObj *a)
{
    /* 0x42c420: on a trigger, after d15 / 6 s, plays d17 (object d1b anim 0x70, then 0xd); special
       d18 is free only on frames d19..d1a */
    int32_t *d = a->d;
    if (a->sub == 0) {
        g_special[d[0x18]].active = 1;
        if (d[0x22] != -1) {
            d[0x15] *= 6;
            anim_set(a->slot, d[0x16], -1);
            if (d[0x1b] != 0) anim_set(g_obj_by_id[d[0x1b]], 0x70, -1);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (d[0x15]-- < 1) {
            anim_set(a->slot, d[0x17], -1);
            if (d[0x1b] != 0) anim_set(g_obj_by_id[d[0x1b]], 0xd, -1);
            a->sub = 2;
        }
    } else if (a->sub == 2) {
        g_special[d[0x18]].active = !(d[0x19] <= a->frame && a->frame <= d[0x1a]);
    }
}

static void lv21_move_07(AnimObj *a)
{
    /* 0x42c510: whoever touches it goes into move type 0x1c; the shepherd with d15 / d16 */
    int32_t *d = a->d;
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        actor_set_state(w, 0x1c);
        if (w[9] > 0x1e) {
            g_anim[w[9]].d[0x15] = d[0x15];
            g_anim[w[9]].d[0x16] = d[0x16];
        }
    }
    d[0x22] = -1;
}

static void lv21_move_08(AnimObj *a)
{
    /* 0x42c580: special d17 free for d15 / 6 s (then the animation), busy for d16 / 6 s; sub-state 3
       (Lv21_Move_10) keeps it busy */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        a->sub = 1;
        d[0x15] *= 6;
        break;
    case 1:
        if (d[0x15]-- < 1) {
            anim_set(a->slot, a->anim, -1);
            g_special[d[0x17]].active = 0;
            a->sub = 2;
        }
        break;
    case 2:
        if (a->state == 2) {
            d[0x15] = d[0x16] * 6;
            g_special[d[0x17]].active = 1;
            a->sub = 1;
        }
        break;
    case 3:
        g_special[d[0x17]].active = 1;
        break;
    }
}

static void lv21_move_09(AnimObj *a)
{
    /* 0x42c630: a repeller at (x + 0x12, y + 0x5b) from frame 7 to frame 1 */
    int32_t *d = a->d;
    if (a->frame == 7 && d[0x1d] == 0) d[0x1d] = attract_new(a->x + 0x12, a->y + 0x5b, 6, 0x18, 0);
    if (a->frame == 1 && d[0x1d] != 0) {
        g_attract[d[0x1d]].type = 0;
        d[0x1d] = 0;
    }
}

static void lv21_move_10(AnimObj *a)
{
    /* 0x42c690: on a trigger puts the objects of 0x452b1c into sub-state 3 */
    if (a->sub == 0 && a->d[0x22] != -1) {
        for (uint32_t p = 0x452b1c, id; (id = exe_u32(p)) != 0; p += 4) g_anim[g_obj_by_id[id]].sub = 3;
        a->sub = 1;
    }
}

static void lv21_move_11(AnimObj *a)
{
    /* 0x42c6f0: rises with d1c (driven by a treadmill, 0..0x100); gate 9 opens at the top */
    int32_t *d = a->d;
    if (a->sub == 0) {
        d[0x1c] = 0;
        d[0x1b] = a->y;
        d[0x1d] = 100;
        a->sub = 1;
    } else if (a->sub == 1) {
        int v = d[0x1c];
        a->y = d[0x1b] - v / 4;
        if (v == 0x100 && d[0x1d] != 0x100) gate_set(9, 1);
        if (d[0x1c] != 0x100 && d[0x1d] == 0x100) gate_set(9, 0);
        d[0x1d] = d[0x1c];
    }
}

static void lv21_move_12(AnimObj *a)
{
    /* 0x42c780: a catapult: plays d15 and throws whoever (direction d17, d18 x 10 ticks, height
       d19), then triggers d16 past frame 1 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        uint32_t t = (uint32_t)d[0x22];
        if (t == 0xffffffffu) return;
        if (!(t & 0x8000)) g_special[t & 0xff].active = 1;
        anim_set(a->slot, d[0x15], -1);
        int32_t *w = actor(d[0x21]);
        sheep_launch_sound(w, d[0x17], d[0x18] * 10, d[0x19], 0, 0x50);
        g_anim[w[9]].d[0x19] = 1;
        a->sub = 1;
    } else if (a->sub == 1 && a->frame > 1) {
        if (d[0x16] != 0) g_anim[g_obj_by_id[d[0x16]]].d[0x22] = 0x8000;
        a->sub = 3;
    }
}

static void lv21_move_13(AnimObj *a)
{
    /* 0x42c850: special d17 free on frames d15..d16; touched, it throws (direction d18, d19 x 10) */
    int32_t *d = a->d;
    g_special[d[0x17]].active = a->frame < d[0x15] || d[0x16] < a->frame;
    if (d[0x22] != -1) sheep_launch_sound(actor(d[0x21]), d[0x18], d[0x19] * 10, 0x1e, 10, 0x1e);
    d[0x22] = -1;
}

static void lv21_move_14(AnimObj *a)
{
    /* 0x42c8c0: on a trigger plays d15 and at its end triggers d16-d18 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
    } else if (a->sub == 1 && a->state == 2) {
        for (int i = 0x16; i <= 0x18; i++)
            if (d[i] != 0) g_anim[g_obj_by_id[d[i]]].d[0x22] = 0x8000;
        a->sub = 2;
    }
}

static void lv21_move_15(AnimObj *a)
{
    /* 0x42c980: Move_15 pushing everyone out of (x + d16, y + d17, d18 x d19) */
    move_15(a);
    push_out_of_rect(a->d[0x16] + a->x, a->d[0x17] + a->y, a->d[0x18], a->d[0x19]);
}

static void lv21_move_16(AnimObj *a)
{
    /* 0x42c9b0: Move_19 with sound 0x10 or 0x11 on frames 0 and 6 */
    if (a->d[0x16] > 0 && (a->frame == 0 || a->frame == 6) && a->frame != a->prev_frame)
        sound_play_at(a, crt_rand() % 0xff > 0x7f ? 0x11 : 0x10, 0, 0);
    move_19(a);
}

static void lv21_move_17(AnimObj *a)
{
    /* 0x42cae0: a grabber running path d16 once; the first sheep (or the shepherd) near its hook
       rides along (d21) and is lost (the shepherd: move type 0x17) 180 ticks after the end */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x16], 1, 0);
        d[0x21] = -1;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (path_follow(a)) {
            a->sub = 2;
            d[0x15] = 0xb4;
            sound_play_at(a, 8, 0, 0);
        }
        if (d[0x21] == -1) {
            for (int i = 0; i < MAX_SHEEP; i++) {
                Sheep *s = &g_sheep[i];
                if (s->alive == 2) continue;
                if (iabs(a->y - s->iy + 10) + iabs(a->x - s->ix + 0x19) < 0x18) {
                    d[0x21] = s->slot;
                    s->state = 4;
                    s->d[0x1d] &= ~1;
                    sound_play_at(a, (int)(rng_next() % 3) + 0x4e, 0, 0);
                    break;
                }
            }
            if (d[0x21] == -1 &&
                iabs(a->y - g_player.iy + 10) + iabs(a->x - g_player.ix + 0x19) < 0x18) {
                d[0x21] = g_player.slot;
                g_player.state = 4;
                sound_play_at(a, (int)(rng_next() % 3) + 0x4e, 0, 0);
            }
        } else {
            int32_t *w = actor(d[0x21]);
            ((float *)w)[10] = (float)(a->x + 0x19);
            ((float *)w)[11] = (float)(a->y + (w[9] < 0x1e ? 10 : 0x1c));
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

static void lv21_move_18(AnimObj *a)
{
    /* 0x42cd30: Move_02 that, once past frame d19, sends whoever touches it (but kind 0x14) into
       move type 0x17 and is drawn behind */
    int32_t *d = a->d;
    move_02(a);
    if (a->sub == 1 && d[0x19] <= a->frame) {
        if (d[0x22] != -1) {
            int32_t *w = actor(d[0x21]);
            if (w[5] != 0x14) actor_set_state(w, 0x17);
        }
        a->rec.mode = 3;
        a->rec.layer = 2;
    }
    d[0x22] = -1;
}

static void lv21_move_19(AnimObj *a)
{
    /* 0x42ca20: Lv21_Move_00 starting a Lv21_Move_17 grabber on frame d18 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->frame == d[0x18] && a->frame != a->prev_frame) lv21_spawn(a, 0x8011);
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1 && d[0x1d]-- < 1) {
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
}

static void lv21_carry(AnimObj *a)
{
    int32_t *w = actor(a->d[0x1d]);
    ((float *)w)[10] = (float)(a->x + 0x2d);
    ((float *)w)[11] = (float)(a->y + 0x37);
}

static void lv21_move_20(AnimObj *a)
{
    /* 0x42cda0: a bin lorry: touched (but not by kind 0x16) it drives path d15 with anim d18, picks
       up whoever touches it at the second point (d19), tips it out at the third (d16, lost) and
       drives back; sounds 0x13-0x17 every 100-199 ticks while parked */
    int32_t *d = a->d;
    int t = d[0x22];
    switch (a->sub) {
    case 0:
        d[0x1e] = crt_rand() % 100 + 100;
        a->sub = 1;
        break;
    case 1:
        if (t != -1 && actor(d[0x21])[5] != 0x16) {
            path_begin(a, d[0x15], 1, 0);
            anim_set(a->slot, d[0x18], -1);
            d[0x1d] = -1;
            a->sub = 2;
        }
        if (--d[0x1e] < 1) {
            d[0x1e] = crt_rand() % 100 + 100;
            sound_play_at(a, crt_rand() % 5 + 0x13, 0, 0);
        }
        break;
    case 2:
        path_follow(a);
        path_follow(a);
        if (a->d[0x24] == 1) a->sub = 3;
        break;
    case 3:
        path_follow(a);
        if (a->d[0x24] == 2) {
            anim_set(a->slot, d[0x19], -1);
            a->sub = 4;
        }
        if (d[0x1d] != -1) {
            lv21_carry(a);
        } else if (t != -1) {
            int32_t *w = actor(d[0x21]);
            if (w[5] != 0x16) {
                d[0x1d] = d[0x21];
                actor_set_state(w, 4);
                lv21_carry(a);
            }
        }
        break;
    case 4:
        path_follow(a);
        if (a->d[0x24] == 3) {
            anim_set(a->slot, d[0x16], -1);
            if (d[0x1d] != -1) actor_out(d[0x1d]);
            a->sub = 5;
        }
        if (d[0x1d] != -1) lv21_carry(a);
        break;
    case 5:
        if (path_follow(a)) a->sub = 1;
        break;
    }
    d[0x22] = -1;
}

static void lv21_move_22(AnimObj *a)
{
    /* 0x42d040: Move_19 with sound 0x11 every 100-499 ticks */
    int32_t *d = a->d;
    if (d[0x16] > 0) {
        if (a->sub == 0) {
            d[0x17] = 0;
            a->sub = 1;
        }
        if (d[0x17] == 0) {
            d[0x17] = crt_rand() % 400 + 100;
            sound_play_at(a, 0x11, 0, 0);
        } else {
            d[0x17]--;
        }
    }
    move_19(a);
}

static void lv21_move_24(AnimObj *a)
{
    /* 0x42d0b0: the third contraption (as Lv21_Move_03 with the lists 0x452b5c.., 180 ticks) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        freeze_list(0x452b5c, 4);
        if (d[0x22] == -1) return;
        d[0x1f] = d[0x21];
        int32_t *w = actor(d[0x21]);
        ((float *)w)[10] = (float)d[0x15];
        ((float *)w)[11] = (float)d[0x16];
        w[3] = 4;
        w[2] = d[0x17];
        gate_set(2, 1);
        for (uint32_t p = 0x452b78; p < 0x452b88; p += 4) anim_kill(&g_anim[g_obj_by_id[exe_u32(p)]]);
        for (uint32_t p = 0x452b88; p < 0x452ba8; p += 8) {
            path_start((int)exe_u32(p + 4));
            AnimObj *o = &g_anim[g_obj_by_id[exe_u32(p)]];
            o->x = g_path_x;
            o->y = g_path_y;
        }
        d[0x1d] = 0xb4;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (d[0x1d]-- < 1) lv21_release(a, false);
    } else if (a->sub == 2) {
        freeze_list(0x452b5c, 4);
    }
}

static void lv21_move_25(AnimObj *a)
{
    /* 0x42d240: leaves anim d15 at (d16, d17) (mirrored d18) on frame d19 and frees special d1b on
       frame d1a; when its animation ends the special is busy for d1c / 6 s and it starts over */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->frame == d[0x19] && a->frame != a->prev_frame)
            g_anim[anim_new(d[0x16], d[0x17], d[0x15], -1, 3)].mirror = d[0x18];
        if (a->frame == d[0x1a] && a->frame != a->prev_frame) g_special[d[0x1b]].active = 0;
        if (a->state == 2) {
            d[0x1d] = d[0x1c] * 6;
            g_special[d[0x1b]].active = 1;
            a->sub = 1;
        }
    } else if (a->sub == 1 && d[0x1d]-- < 1) {
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
}

static void lv21_move_26(AnimObj *a)
{
    /* 0x42d300: Move_10 whose attractors sit at (x + 0x20, y + 0x30); at its path's start it
       triggers object 333 */
    int32_t *d = a->d;
    if (d[0x25] == 0 && d[0x24] == 1) {
        sound_play_at(a, 0xd, 0, 0);
        g_anim[g_obj_by_id[333]].d[0x22] = 0x8000;
    }
    move_10(a);
    if (a->sub > 1) {
        if (d[0x17] != 0) {
            g_attract[d[0x1d]].x = (int16_t)(a->x + 0x20);
            g_attract[d[0x1d]].y = (int16_t)(a->y + 0x30);
        }
        if (d[0x19] != 0) {
            g_attract[d[0x1e]].x = (int16_t)(a->x + 0x20);
            g_attract[d[0x1e]].y = (int16_t)(a->y + 0x30);
        }
    }
}

static void map_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    /* 0x402fa0: copy a block of map cells (rows of the header's width) */
    uint16_t *s = g_lv.map + (sy / 24) * g_lv.w + sx / 32, *t = g_lv.map + (dy / 24) * g_lv.w + dx / 32;
    for (; h > 0; h--, s += g_lv.w, t += g_lv.w)
        for (int i = 0; i < w; i++) t[i] = s[i];
}

static void lv21_move_27(AnimObj *a)
{
    /* 0x42d3c0: on a trigger plays d15; at its last frame opens gate d16 and patches the map */
    int32_t *d = a->d;
    if (a->sub == 0) {
        uint32_t t = (uint32_t)d[0x22];
        if (t != 0xffffffffu) {
            if (!(t & 0x8000)) g_special[t & 0xff].active = 1;
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
        d[0x22] = -1;
    } else if (a->sub == 1 && a->frame == a->nframes - 1) {
        gate_set(d[0x16], 1);
        map_copy(0x940, 0xc0, 0x3a0, 0x318, 7, 7);
        a->move = 0;
    }
}

static void lv21_move_28(AnimObj *a)
{
    /* 0x42d450: Move_05 with a repeller (type 10, range 0x96) until its last frame */
    int32_t *d = a->d;
    move_05(a);
    if (a->sub > 0) {
        if (d[0x1f] == 0) d[0x1f] = attract_new(a->x, a->y, 10, 0x96, 1);
        if (a->frame == a->nframes - 1) g_attract[d[0x1f]].type = 0;
    }
}

static void lv21_move_29(AnimObj *a)
{
    /* 0x42d4b0: Move_02 that only kind d19 triggers */
    if (a->d[0x22] != -1 && actor(a->d[0x21])[5] != a->d[0x19]) a->d[0x22] = -1;
    move_02(a);
}

static void lv21_move_30(AnimObj *a)
{
    /* 0x42d4f0: Move_03 whose first attractor is off the map except at path point 4 */
    move_03(a);
    if (a->d[0x24] != 4) g_attract[a->d[0x1d]].x = -100;
}

static void lv21_move_31(AnimObj *a)
{
    /* 0x42d520: clipped to object d15's top (d1c = 0, following it) or to its own y once open
       (Move_02) */
    if (a->d[0x1c] == 0) {
        move_15(a);
        a->rec.clip_x0 = 0;
        a->rec.clip_y0 = g_anim[g_obj_by_id[a->d[0x15]]].rec.clip_y0;
    } else {
        move_02(a);
        if (a->sub == 0) {
            a->rec.clip_x0 = 0;
            a->rec.clip_y0 = a->y;
        }
    }
}

/* ------------------------------------------------------------------ dispatch */

void level_actor_hook(int32_t *actor)
{
    uint32_t va = desc_u32(DESC_PLAYER);
    if (!va) return;
    switch (va) {
    case 0x42aee0: lv11_actor(actor); break;
    case 0x42bba0: lv21_actor(actor); break;
    default: unported("actor", va); break;
    }
}

MoveFn level_move(uint32_t va)
{
    switch (va) {
    case 0x42b000: return lv11_move_01;
    case 0x42b1a0: return lv11_move_03;
    case 0x42b280: return lv11_move_04;
    case 0x42b560: return lv11_move_05;
    case 0x42b5d0: return lv11_move_06;
    case 0x42b6c0: return lv11_move_07;
    case 0x42b8a0: return lv11_move_08;
    case 0x42ba60: return lv11_move_09;
    case 0x42b910: return lv11_move_10;
    case 0x42b990: return lv11_move_11;
    case 0x42ba10: return lv11_move_12;
    case 0x42bb50: return lv11_move_13;
    case 0x42bb80: return lv11_move_14;
    case 0x42bd10: return lv21_move_00;
    case 0x42be30: return lv21_move_01;
    case 0x42bee0: return lv21_move_02;
    case 0x42bf70: return lv21_move_03;
    case 0x42c1b0: return lv21_move_04;
    case 0x42c2a0: return lv21_move_05;
    case 0x42c420: return lv21_move_06;
    case 0x42c510: return lv21_move_07;
    case 0x42c580: return lv21_move_08;
    case 0x42c630: return lv21_move_09;
    case 0x42c690: return lv21_move_10;
    case 0x42c6f0: return lv21_move_11;
    case 0x42c780: return lv21_move_12;
    case 0x42c850: return lv21_move_13;
    case 0x42c8c0: return lv21_move_14;
    case 0x42c980: return lv21_move_15;
    case 0x42c9b0: return lv21_move_16;
    case 0x42cae0: return lv21_move_17;
    case 0x42cd30: return lv21_move_18;
    case 0x42ca20: return lv21_move_19;
    case 0x42cda0: return lv21_move_20;
    case 0x42e1a0: return move_20;          /* Lv21_Move_21: Move_20 without its unused key test */
    case 0x42d040: return lv21_move_22;
    case 0x42d0b0: return lv21_move_24;
    case 0x42d240: return lv21_move_25;
    case 0x42d300: return lv21_move_26;
    case 0x42d3c0: return lv21_move_27;
    case 0x42d450: return lv21_move_28;
    case 0x42d4b0: return lv21_move_29;
    case 0x42d4f0: return lv21_move_30;
    case 0x42d520: return lv21_move_31;
    default: return NULL;
    }
}
