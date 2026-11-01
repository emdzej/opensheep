#include "levels_int.h"

/* World 3 (3.1-3.4 share the routines and the actor hook). */

static int lv3_count;                   /* 0x662ac4: Lv31_Move_02's, reset by 3.4's init 0x42d590 */

/* who: a sheep's state through 0x419f50, the shepherd's through 0x414b10 */
static void who_set_state(int32_t *w, int s)
{
    if (w[9] < 0x1e) actor_set_state(w, s);
    else player_set_state((Player *)w, s);
}

static void lv31_actor(int32_t *a)
{
    /* 0x42d5a0: the world's own move types, the parameters in the actor's object d15.. */
    int slot = a[9];
    AnimObj *o = &g_anim[slot];
    int32_t *d = o->d;
    float *f = (float *)a;
    switch (a[3]) {
    case 100:
        /* float up off the map, faster and faster (the shepherd goes to 0x65's) */
        if (slot < 0x1f) {
            if (o->sub == 0) {
                a[0x1d] &= ~1;
                o->fy = 0.0f;
                o->sub = 1;
            }
            f[11] = (float)((double)f[11] - (double)o->fy);
            o->fy = (float)((double)o->fy + (double)0.2f);
            if (f[11] >= 0.0f) return;
            sheep_out(slot);
            return;
        }
        player_set_state((Player *)a, 0);
        /* fall through */
    case 0x65:
        /* fall below the line d15 (clipped there), spinning; the shepherd comes back at (d16, d17) */
        if (a[9] < 0x1e) a[0x1d] &= ~1;
        o->rec.clip_x0 = 0;
        o->rec.clip_y1 = d[0x15];
        o->rec.clip_x1 = 8000;
        o->rec.clip_y0 = 0;
        if (o->sub == 0) {
            o->speed = 0.0f;
            o->rec.mode = 3;
            o->sub = 1;
        } else if (o->sub == 1) {
            if (o->speed <= 5.0f) o->speed = (float)((double)o->speed + (double)0.2f);
            a[0] = (a[0] + 8) & 0xff;
            f[11] = (float)((double)f[11] + (double)o->speed);
            if ((float)(d[0x15] + 0x28) < f[11]) {
                if (a[9] > 0x1d) {
                    f[10] = (float)d[0x16];
                    f[11] = (float)d[0x17];
                    player_set_state((Player *)a, 0);
                } else {
                    sheep_out(a[9]);
                }
            }
        }
        a[2] = a[0];
        return;
    case 0x66:
        /* shrink away into the pen: a sheep counts as penned; the shepherd ends the level when
           enough are, or starts the end sequence */
        if (o->sub == 0) {
            if (d[0x15] != 0) g_anim[g_obj_by_id[d[0x15]]].d[0x22] = 0x8000;
            o->sub = 1;
        } else if (o->sub == 1) {
            o->rec.scale_x -= 10;
            if (o->rec.scale_x < 0) {
                o->rec.scale_x = 0;
                o->sub = 2;
            }
            o->rec.scale_y = o->rec.scale_x;
        } else if (o->sub == 2) {
            if (slot < 0x1e) {
                a[1] = 0;
                a[0x1d] &= ~1;
                g_saved++;
                a[3] = 4;
                a[4] = 1;
            } else if (g_needed <= g_saved) {
                g_end = 1;
            } else if (!g_level_over) {
                int s = anim_new(0, 0, 0x10, -1, 4);
                g_anim[s].move = 0x11;
                g_anim[s].d[0x15] = 1;
            }
        }
        return;
    case 0x67:
        /* as 2.1's plughole: spiral into (d15, d16), shrinking; the shepherd reappears at
           (d15 + 0x40, d16 + 0x30) and grows back */
        if (slot < 0x1e) a[0x1d] &= ~1;
        if (o->sub == 0) {
            int dx = iabs(a[0xc] - d[0x15]), dy = iabs(a[0xd] - d[0x16]);
            d[0x17] = ftol(sqrt((double)(dy * dy + dx * dx)));
            d[0x18] = dir_of(a[0xc] - d[0x15], a[0xd] - d[0x16]);
            sound_play_at(o, 3, 0, 0);
            o->sub = 1;
        } else if (o->sub == 1) {
            d[0x18] = (d[0x18] + 8) & 0xff;
            f[10] = (float)((double)d[0x17] * g_sin[d[0x18]] + d[0x15]);
            f[11] = (float)((double)d[0x16] - (double)d[0x17] * g_cos[d[0x18]]);
            if ((d[0x1d]++ & 3) == 0) d[0x17]--;
            if (d[0x17] < 0x40) o->rec.scale_x = o->rec.scale_y = d[0x17] * 4;
            if (d[0x17] > 0) return;
            if (slot > 0x1d) {
                f[10] = (float)(d[0x15] + 0x40);
                f[11] = (float)(d[0x16] + 0x30);
                o->sub = 2;
                return;
            }
            sheep_out(slot);
        } else if (o->sub == 2) {
            o->rec.scale_x += 2;
            o->rec.scale_y += 2;
            a[1] = 0;
            if (o->rec.scale_x > 0xff) {
                o->rec.scale_x = 0x100;
                player_set_state((Player *)a, 0);
            }
        }
        return;
    case 0x68:
        /* hidden until the collision puts it back in state 0, then it shows as kind 0x13 */
        o->rec.scale_x = 0;
        sheep_collide(a[0xc], a[0xd], a, 1);
        if (a[3] == 0) {
            o->rec.scale_x = 0x100;
            a[5] = 0x13;
        }
        return;
    case 0x69:
        /* climb to path d15's start, then ride it; at its end state 0x19 */
        a[0x1d] &= ~1;
        if (o->sub == 0) {
            o->sub = 1;
        } else if (o->sub == 1) {
            a[1] = 0;
            a[0xe] = 6;
            path_start(d[0x15]);
            int dx = a[0xc] - g_path_x;
            if (dx < 0) f[10] = (float)((double)f[10] + 1.0);
            if (dx > 0) f[10] = (float)((double)f[10] - 1.0);
            if (a[0xd] <= g_path_y) {
                path_begin(o, d[0x15], 0, 0);
                o->sub = 2;
                return;
            }
            f[11] = (float)((double)f[11] - 3.0);
        } else if (o->sub == 2) {
            a[1] = 0;
            a[0xe] = 6;
            if (path_follow(o)) actor_set_state(a, 0x19);
            f[10] = (float)o->x;
            f[11] = (float)o->y;
        }
        return;
    case 0x6a: {
        /* hover d34 px up while the collision keeps putting it back in 0x6a (a random height
           30..61), else sink and walk on */
        f[11] = (float)((double)o->rec.shadow_dy + (double)f[11]);
        a[0xd] = ftol((double)f[11]);
        int h = o->rec.shadow_dy;
        a[3] = 0;
        if (slot < 0x1e) sheep_walk((Sheep *)a);
        else player_walk((Player *)a);
        o->rec.shadow_dy = h;
        if (a[3] == 0x6a) {
            int t = (int)(rng_next() & 0x1f) + 0x1e;
            d[0x1f] = t;
            if (o->rec.shadow_dy < t) o->rec.shadow_dy += 2;
            if (t < o->rec.shadow_dy) o->rec.shadow_dy -= 2;
        } else {
            a[3] = 0x6a;
            o->rec.shadow_dy -= 2;
            if (o->rec.shadow_dy < 1) {
                o->rec.shadow_dy = 0;
                actor_set_state(a, 0);
            }
        }
        o->depth_off = o->rec.shadow_dy;
        f[11] = (float)((double)f[11] - (double)o->rec.shadow_dy);
        a[0xd] = ftol((double)f[11]);
        return;
    }
    }
}

static int subs_set(AnimObj *a, int from, int n)
{
    /* how many of the objects d[from..] have left sub-state 0 */
    int c = 0;
    for (int i = 0; i < n; i++) c += g_anim[g_obj_by_id[a->d[from + i]]].sub != 0;
    return c;
}

static void lv31_move_00(AnimObj *a)
{
    /* 0x42dbb0: as 2.1's 0x42bd10, frames d19..d1a inclusive, sound 9 on d19, always move type 0x1b */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x19] <= a->frame && a->frame <= d[0x1a] && a->frame != a->prev_frame) {
            if (a->frame == d[0x19]) sound_play_at(a, 9, 0, 0);
            path_start(d[0x15]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
            o->move = 4;
            o->d[0x15] = d[0x16];
            o->d[0x16] = d[0x15];
            o->d[0x17] = 10;
            o->d[0x18] = 10;
            o->d[0x19] = 0x20;
            o->d[0x1a] = 0x1b;
            o->d[0x1c] = 1;
        }
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

static void lv31_move_01(AnimObj *a)
{
    /* 0x42dcd0: once two Move_02 objects are done, a trigger plays anim d15; past frame d19 it
       triggers d16 and d17; special d18 is off meanwhile; then it goes */
    int32_t *d = a->d;
    if (a->sub == 0) {
        g_special[d[0x18]].active = 1;
        if (d[0x22] != -1 && lv3_count > 1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (a->frame > d[0x19]) {
            g_anim[g_obj_by_id[d[0x16]]].d[0x22] = 0x8000;
            g_anim[g_obj_by_id[d[0x17]]].d[0x22] = 0x8000;
            g_special[d[0x18]].active = 0;
            a->sub = 2;
        }
    } else if (a->sub == 2 && a->state == 2) {
        g_special[d[0x18]].active = 1;
        anim_kill(a);
    }
    d[0x22] = -1;
}

static void lv31_move_02(AnimObj *a)
{
    /* 0x42ddd0: Move_05 that counts itself once when it reaches sub-state 1 */
    move_05(a);
    if (a->sub == 1 && a->d[0x1f] == 0) {
        a->d[0x1f] = 1;
        lv3_count++;
    }
}

static void lv31_move_03(AnimObj *a)
{
    /* 0x42de10: loops its animation with a d17 / 6 s pause; special d18 is off on frames d15..d16 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1 && d[0x1d]-- < 1) {
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
    g_special[d[0x18]].active = !(d[0x15] <= a->frame && a->frame <= d[0x16]);
}

static void lv31_move_04(AnimObj *a)
{
    /* 0x42de90: on frames between d15 and d16 who triggers falls (0x65) below d18, the shepherd
       back at (d19, d1a); the animation repeats after d17 / 6 s */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x15] < a->frame && a->frame < d[0x16] && d[0x22] != -1) {
            int32_t *w = actor(d[0x21]);
            who_set_state(w, 0x65);
            AnimObj *o = &g_anim[w[9]];
            o->d[0x15] = d[0x18];
            o->d[0x16] = d[0x19];
            o->d[0x17] = d[0x1a];
        }
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1 && d[0x1d]-- < 1) {
        anim_set(a->slot, a->anim, -1);
        a->sub = 0;
    }
    d[0x22] = -1;
}

static void lv31_move_05(AnimObj *a)
{
    /* 0x42df90: runs tile animation d18 for d17 / 6 s then stops it for d16 / 6 s; while it runs
       who triggers goes to move type 10 */
    int32_t *d = a->d;
    TileAnim *t = &g_tile_anim[d[0x18]];
    if (a->sub == 0) {
        anim_set(a->slot, d[0x15], -1);
        a->rec.scale_x = 0;
        t->mode = 1;
        a->sub = 1;
        a->sound = 0;
    } else if (a->sub == 1) {
        t->mode = 1;
        if (t->frame == 1 && d[0x1f] != 1) sound_play_at(a, 0xc, 0, 0);
        d[0x1f] = t->frame;
        if (d[0x22] != -1) who_set_state(actor(d[0x21]), 10);
        if (d[0x1d]-- < 1) {
            t->mode = 0;
            t->frame = 0;
            a->sub = 2;
            d[0x22] = -1;
            d[0x1d] = d[0x17] * 6;
            return;
        }
    } else if (a->sub == 2 && d[0x1d]-- < 1) {
        a->sub = 1;
        d[0x22] = -1;
        d[0x1d] = d[0x16] * 6;
        return;
    }
    d[0x22] = -1;
}

static void lv31_move_06(AnimObj *a)
{
    /* 0x42e0e0: once objects d16, d17 and d18 have all started, plays anim d15 and frees special d19 */
    int32_t *d = a->d;
    if (a->sub != 0) return;
    g_special[d[0x19]].active = 1;
    if (subs_set(a, 0x16, 3) > 2) {
        anim_set(a->slot, d[0x15], -1);
        g_special[d[0x19]].active = 0;
        a->sub = 1;
    }
}

static void lv31_move_08(AnimObj *a)
{
    /* 0x42e560: on frame d18 starts an object (anim d16, routine 9) along path d15 carrying d19 / d1a
       as its attractor, mirrored if d1b; repeats after d17 / 6 s */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->frame == d[0x18] && a->frame != a->prev_frame) {
            path_start(d[0x15]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
            o->move = 0x8009;
            o->d[0x15] = d[0x16];
            o->d[0x16] = d[0x15];
            o->d[0x17] = d[0x19];
            o->d[0x18] = d[0x1a];
            if (d[0x1b] != 0) o->mirror = 1;
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

static void lv31_move_09(AnimObj *a)
{
    /* 0x42e630: follows path d16 carrying an attractor (type d17 / 10, power d17 % 10, range d18);
       removed at the end (clearing attractor d1d, 0 when it had none) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x16], 1, 0);
        if (d[0x17] != 0) d[0x1d] = attract_new(a->x, a->y, d[0x17] / 10, d[0x18], d[0x17] % 10);
        a->sub = 1;
    } else if (a->sub == 1) {
        if (path_follow(a)) {
            g_attract[d[0x1d]].type = 0;
            anim_kill(a);
            return;
        }
        if (d[0x17] != 0) {
            g_attract[d[0x1d]].x = (int16_t)a->x;
            g_attract[d[0x1d]].y = (int16_t)a->y;
        }
    }
}

static void flow_show(AnimObj *a, bool on, uint32_t e)
{
    /* the flow sprite (anim 0x4b) in d1d, placed by table entry e {.., dx, dy, anim, dir} */
    int32_t *d = a->d;
    if (d[0x1d] == 0 && on) d[0x1d] = anim_new(0, 0, 0x4b, -1, 3);
    int s = d[0x1d];
    if (!on) {
        if (s != 0) {
            anim_kill(&g_anim[s]);
            d[0x1d] = 0;
        }
        return;
    }
    AnimObj *o = &g_anim[s];
    o->x = (int32_t)exe_u32(e + 8) + a->x;
    o->y = (int32_t)exe_u32(e + 0xc) + a->y;
    o->anim = (int32_t)exe_u32(e + 0x10);
    o->dir = (int32_t)exe_u32(e + 0x14);
    anim_set_frame(o);
}

static void lv31_move_10(AnimObj *a)
{
    /* 0x42e700: a pipe piece turned a quarter (4 of 16 frames) when triggered; it carries the flow
       d1b on when a neighbour (table 0x453128, four entries of 0x18 bytes: which of d15.. and
       its flow value) matches one of the piece's two openings (0x453188, 0x10 bytes per value) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        a->frame = d[0x15];
        a->countdown = 10;
        anim_set_frame(a);
        if (d[0x22] != -1) {
            d[0x15] = (d[0x15] + 4) & 0xf;
            a->sub = 1;
            sound_play_at(a, 9, 0, 0);
        }
    } else if (a->sub == 1) {
        if (d[0x1f]++ & 1) a->frame++;
        a->frame &= 0xf;
        if (a->frame == d[0x15]) a->sub = 0;
        anim_set_frame(a);
    }
    d[0x22] = -1;
    int flow = 0, hit = 0;
    bool on = false;
    for (int i = 0; i < 4; i++) {
        uint32_t e = 0x453128 + (uint32_t)i * 0x18;
        int32_t v = (int32_t)exe_u32(e + 4);
        if (g_anim[g_obj_by_id[d[0x15 + exe_u32(e)]]].d[0x1b] != v) continue;
        uint32_t k = 0x453188 + (uint32_t)v * 0x10;
        if (a->frame == (int32_t)exe_u32(k)) flow = (int32_t)exe_u32(k + 4);
        if (a->frame == (int32_t)exe_u32(k + 8)) flow = (int32_t)exe_u32(k + 0xc);
        if (flow != 0) {
            on = true;
            hit = i;
            break;
        }
    }
    flow_show(a, on, 0x453128 + (uint32_t)hit * 0x18);
    d[0x1b] = flow;
}

static void lv31_move_11(AnimObj *a)
{
    /* 0x42e8b0: the pipe's outlet: while object d15 carries flow d16 shows the flow (table entry 0)
       and triggers object d17 */
    int32_t *d = a->d;
    bool on = g_anim[g_obj_by_id[d[0x15]]].d[0x1b] == d[0x16];
    flow_show(a, on, 0x453128);
    if (on) g_anim[g_obj_by_id[d[0x17]]].d[0x22] = 0x8000;
}

static void lv31_move_12(AnimObj *a)
{
    /* 0x42e9a0: special d18 is on until something other than d18 triggers it (anim d19); then
       whoever triggers with d18 rides path d15 (move type 0x69) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        g_special[d[0x18]].active = 1;
        if (d[0x22] != -1 && d[0x22] != d[0x18]) {
            anim_set(a->slot, d[0x19], -1);
            g_special[d[0x18]].active = 0;
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (d[0x22] != -1 && d[0x22] == d[0x18]) {
            int32_t *w = actor(d[0x21]);
            actor_set_state(w, 0x69);
            AnimObj *o = &g_anim[w[9]];
            o->d[0x15] = d[0x15];
            o->d[0x16] = d[0x16];
            o->d[0x17] = d[0x17];
            sound_play_at(a, 0xc, 0, 0);
        }
        if (a->state == 2) a->sub = 0;
    }
    d[0x22] = -1;
}

static void lv31_move_13(AnimObj *a)
{
    /* 0x42eab0: Move_10 along its path to point 15, its attractor pushed 0x50 px down; at points
       2, 5 and 8 it starts objects 502, 504, 505 and triggers 225, 226, 244; at 14 it drops the
       attractor */
    int32_t *d = a->d;
    if (d[0x24] < 0xf) {
        move_10(a);
        if (a->sub > 1 && d[0x17] != 0) g_attract[d[0x1d]].y += 0x50;
    } else {
        anim_set(a->slot, 0x4b, 6);
        d[0x24] = 0xf;
    }
    if (d[0x25] != 0) return;
    int start = 0, trig = 0;
    switch (d[0x24]) {
    case 2: start = 502; trig = 225; break;
    case 5: start = 504; trig = 226; break;
    case 8: start = 505; trig = 244; break;
    case 0xe:
        if (d[0x17] != 0) {
            g_attract[d[0x1d]].type = 0;
            d[0x17] = 0;
        }
        return;
    default: return;
    }
    AnimObj *o = &g_anim[g_obj_by_id[start]];
    if (o->sub < 3) {
        o->sub = 3;
        g_anim[g_obj_by_id[trig]].d[0x22] = 1;
    }
}

static void lv31_move_14(AnimObj *a)
{
    /* 0x42ec30: Move_05 whose trigger also puts who touched it in move type 0x19; when Move_05
       reaches sub-state 3 it plays anim 0xc, otherwise anim d1d at each end */
    if (a->d[0x22] != -1 && a->sub == 0) who_set_state(actor(a->d[0x21]), 0x19);
    move_05(a);
    if (a->sub != 3) {
        if (a->state == 2) anim_set(a->slot, a->d[0x1d], -1);
        return;
    }
    anim_set(a->slot, 0xc, -1);
    a->sub = 4;
}

static void lv31_move_15(AnimObj *a)
{
    /* 0x42ecb0: once objects d16..d19 have all started, plays anim d15 and opens gate d1a */
    if (a->sub != 0) return;
    if (subs_set(a, 0x16, 4) > 3) {
        anim_set(a->slot, a->d[0x15], -1);
        gate_set(a->d[0x1a], 1);
        a->sub = 1;
    }
}

static void lv31_move_16(AnimObj *a)
{
    /* 0x42ed70: a lever: each trigger (after a second) flips it between anim d1d (setting d1a of
       objects d17..d1b, object d1c to sub-state 1) and anim d16 (clearing them, d1c to 3) */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        d[0x1d] = a->anim;
        d[0x22] = -1;
        a->sub = d[0x15] != 0 ? 3 : 1;
        return;
    case 1:
    case 3:
        for (int i = 0x17; i <= 0x1b; i++) g_anim[g_obj_by_id[d[i]]].d[0x1a] = a->sub == 1;
        anim_set(a->slot, a->sub == 1 ? d[0x1d] : d[0x16], -1);
        d[0x1e] = 0x3c;
        a->sub++;
        d[0x22] = -1;
        return;
    case 2:
    case 4:
        if (d[0x1e] > 0) {
            d[0x22] = -1;
            d[0x1e]--;
            return;
        }
        if (d[0x22] != -1) {
            int was = a->sub;
            a->sub = was == 2 ? 3 : 1;
            g_anim[g_obj_by_id[d[0x1c]]].sub = was == 2 ? 1 : 3;
            d[0x22] = -1;
            return;
        }
        break;
    }
    d[0x22] = -1;
}

static void lv31_move_17(AnimObj *a)
{
    /* 0x42efd0: walks path d15 (anim d16); at each point plays anim d17 with an attractor (type
       d18 / 10, power d18 % 10, range d19) until it ends */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x15], 1, 1);
        a->sub = 1;
    } else if (a->sub == 1) {
        if (a->anim != d[0x16]) anim_set(a->slot, d[0x16], -1);
        int p = d[0x24];
        path_follow(a);
        if (d[0x24] != p) a->sub = 2;
    } else if (a->sub == 2) {
        if (a->anim != d[0x17]) anim_set(a->slot, d[0x17], -1);
        if (d[0x1d] == 0) d[0x1d] = attract_new(a->x, a->y, d[0x18] / 10, d[0x19], d[0x18] % 10);
        if (a->state == 2) {
            g_attract[d[0x1d]].type = 0;
            d[0x1d] = 0;
            a->sub = 1;
        }
    }
}

static void lv31_move_18(AnimObj *a)
{
    /* 0x42f0c0: swallows who triggers (hidden, move type 4) for three plays of anim d15, triggers
       object d16 and lets go */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] != -1) {
            d[0x1d] = d[0x21];
            d[0x1e] = 3;
            d[0x1f] = a->anim;
            g_anim[g_obj_by_id[d[0x16]]].d[0x22] = 0x8000;
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        int32_t *w = actor(d[0x1d]);
        AnimObj *o = &g_anim[w[9]];
        w[3] = 4;
        o->rec.scale_x = o->rec.scale_y = 0;
        if (a->state != 2) return;
        if (d[0x1e] > 0) {
            d[0x1e]--;
            anim_set(a->slot, d[0x15], 8);
            return;
        }
        w[3] = 0;
        o->rec.scale_x = o->rec.scale_y = 0x100;
        anim_set(a->slot, d[0x1f], 0);
        a->sub = 2;
    }
}

static void lv31_move_19(AnimObj *a)
{
    /* 0x42f1e0: when object d15 (d16, d17, d18) has started, copies its map cell into two places */
    static const int16_t cell[4][2] = { { 0x220, 0x690 }, { 0x240, 0x690 }, { 0x220, 0x6a8 }, { 0x240, 0x6a8 } };
    for (int i = 0; i < 4; i++) {
        int32_t *id = &a->d[0x15 + i];
        if (*id == 0 || g_anim[g_obj_by_id[*id]].sub <= 0) continue;
        int x = cell[i][0], y = cell[i][1];
        map_copy(x, y, x + 0x180, y - 0x258, 1, 1);
        map_copy(x, y, x + 0x180, y - 0x588, 1, 1);
        *id = 0;
    }
}

static void lv31_move_20(AnimObj *a)
{
    /* 0x42f370: anim d15 until triggered, then anim d16 triggering d17 / d18 while it is, back
       d19 / 6 s after */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->anim != d[0x15]) anim_set(a->slot, d[0x15], -1);
        if (d[0x22] != -1) a->sub = 1;
    } else if (a->sub == 1) {
        if (a->anim != d[0x16]) anim_set(a->slot, d[0x16], -1);
        d[0x1d] = d[0x19] * 6;
        if (d[0x22] == -1) {
            a->sub = 2;
        } else {
            if (d[0x17] != 0) g_anim[g_obj_by_id[d[0x17]]].d[0x22] = 0x8000;
            if (d[0x18] != 0) g_anim[g_obj_by_id[d[0x18]]].d[0x22] = 0x8000;
        }
    } else if (a->sub == 2 && d[0x1d]-- < 1) {
        a->sub = 0;
    }
    d[0x22] = -1;
}

static void lv31_move_21(AnimObj *a)
{
    /* 0x42f470: a sign showing hint d1c while touched, until touched with creep held: then it
       becomes Move_02 */
    int32_t *d = a->d;
    if (d[0x1b] != 0) {
        if (g_hint_owner == a->id) g_hint_shown = g_hint_owner = 0;
        move_02(a);
    } else if (d[0x22] == -1) {
        if (g_hint_owner == a->id) g_hint_shown = g_hint_owner = 0;
    } else if (g_creep == 0) {
        if (g_hint_owner != a->id) {
            hint_show(d[0x1c]);
            g_hint_y = 100;
            g_hint_x = 100;
            g_hint_owner = a->id;
        }
    } else {
        d[0x1b] = 1;
    }
    d[0x22] = -1;
}

bool lv3_actor(uint32_t va, int32_t *actor)
{
    if (va != 0x42d5a0) return false;
    lv31_actor(actor);
    return true;
}

bool lv3_call(uint32_t va)
{
    if (va != 0x42d590) return false;
    lv3_count = 0;                      /* 3.4's init */
    return true;
}

MoveFn lv3_move(uint32_t va)
{
    switch (va) {
    case 0x42dbb0: return lv31_move_00;
    case 0x42dcd0: return lv31_move_01;
    case 0x42ddd0: return lv31_move_02;
    case 0x42de10: return lv31_move_03;
    case 0x42de90: return lv31_move_04;
    case 0x42df90: return lv31_move_05;
    case 0x42e0e0: return lv31_move_06;
    case 0x42e560: return lv31_move_08;
    case 0x42e630: return lv31_move_09;
    case 0x42e700: return lv31_move_10;
    case 0x42e8b0: return lv31_move_11;
    case 0x42e9a0: return lv31_move_12;
    case 0x42eab0: return lv31_move_13;
    case 0x42ec30: return lv31_move_14;
    case 0x42ecb0: return lv31_move_15;
    case 0x42ed70: return lv31_move_16;
    case 0x42efd0: return lv31_move_17;
    case 0x42f0c0: return lv31_move_18;
    case 0x42f1e0: return lv31_move_19;
    case 0x42f370: return lv31_move_20;
    case 0x42f470: return lv31_move_21;
    default: return NULL;
    }
}
