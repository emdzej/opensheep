#include "levels_int.h"

/* World 2 (2.1-2.4 share the routines and the actor hook). */

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

bool lv2_actor(uint32_t va, int32_t *actor)
{
    if (va != 0x42bba0) return false;
    lv21_actor(actor);
    return true;
}

MoveFn lv2_move(uint32_t va)
{
    switch (va) {
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
