#include "levels.h"
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

static void unported(const char *what, uint32_t va)
{
    static uint32_t said[64];
    for (int i = 0; i < 64; i++) {
        if (said[i] == va) return;
        if (!said[i]) { said[i] = va; break; }
    }
    fprintf(stderr, "level %s hook 0x%x not ported\n", what, va);
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

/* ------------------------------------------------------------------ dispatch */

void level_actor_hook(int32_t *actor)
{
    uint32_t va = desc_u32(DESC_PLAYER);
    if (!va) return;
    switch (va) {
    case 0x42aee0: lv11_actor(actor); break;
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
    default: return NULL;
    }
}
