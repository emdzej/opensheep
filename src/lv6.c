#include "levels_int.h"
#include "play.h"

/* World 6 (6.1-6.4 share the routines and the actor hook). */

static void who_set_state(int32_t *w, int s)
{
    if (w[9] < 0x1e) actor_set_state(w, s);
    else player_set_state((Player *)w, s);
}

static void trigger(int id, int v)
{
    if (id != 0) g_anim[g_obj_by_id[id]].d[0x22] = v;
}

static void lv61_rise(int32_t *a, int anim)
{
    /* states 100 and 0x68, sub 1: drift level with path d15's start while climbing 3 px a tick to
       it, then ride it */
    AnimObj *o = &g_anim[a[9]];
    float *f = (float *)a;
    a[0xe] = anim;
    f[1] = 0;
    path_start(o->d[0x15]);
    int dx = a[0xc] - g_path_x;
    if (dx < 0) f[10] = (float)((double)f[10] + 1.0);
    if (dx > 0) f[10] = (float)((double)f[10] - 1.0);
    if (a[0xd] > g_path_y) {
        f[11] = (float)((double)f[11] - 3.0);
        return;
    }
    path_begin(o, o->d[0x15], 0, 0);
    o->sub = 2;
    if (anim == 0x1c) sound_play_at(o, 2, 0, 0);
}

static void lv61_actor(int32_t *a)
{
    /* 0x431bf0: the world's move types 100-107, parameters in the actor's object d15.. */
    int slot = a[9];
    AnimObj *o = &g_anim[slot];
    int32_t *d = o->d;
    float *f = (float *)a;
    switch (a[3]) {
    case 100:
        /* lifted to path d15 and carried along it (anim 0x1c), then back to state 0 */
        a[0x1d] &= ~1;
        switch (o->sub) {
        case 0:
            o->sub = 1;
            return;
        case 1:
            lv61_rise(a, 0x1c);
            return;
        case 2:
            f[1] = 0;
            a[0xe] = 0x1c;
            if (path_follow(o)) actor_set_state(a, 0);
            f[10] = (float)o->x;
            f[11] = (float)o->y;
            a[2] = o->dir << 4;
            return;
        }
        return;
    case 0x65:
        /* falls, spinning and speeding up to 5 px a tick, below the clip line d15 + 40: a sheep is
           lost, the shepherd comes back at (d16, d17) */
        if (slot < 0x1e) a[0x1d] &= ~1;
        o->rec.clip_x1 = 8000;
        o->rec.clip_x0 = 0;
        o->rec.clip_y0 = 0;
        o->rec.clip_y1 = d[0x15];
        if (o->sub == 0) {
            o->speed = 0;
            o->rec.mode = 3;
            o->sub = 1;
        } else if (o->sub == 1) {
            if (o->speed <= 5.0f) o->speed = (float)((double)o->speed + 0.2f);
            a[0] = (a[0] + 8) & 0xff;
            f[11] = o->speed + f[11];
            if ((float)(d[0x15] + 0x28) < f[11]) {
                if (slot > 0x1d) {
                    f[10] = (float)d[0x16];
                    f[11] = (float)d[0x17];
                    player_set_state((Player *)a, 0);
                } else {
                    actor_out(slot);
                }
            }
        }
        a[2] = a[0];
        return;
    case 0x66:
        /* walk to path d23 and ride it (the lift: then held facing up, 0x1c); sub 3, set by
           Lv61_Move_12, shrinks it away and back along path d23 + 1 with the camera on it */
        switch (o->sub) {
        case 0:
            a[0x1d] &= ~1;
            path_start(d[0x23]);
            f[1] = 1.0f;
            a[2] = a[0];
            if (actor_walk_to(a, g_path_x, g_path_y)) {
                path_begin(o, d[0x23], 0, 0);
                if (a[5] == 0) a[0xe] = a[0xf] = a[0x10] = 2;
                o->sub = 1;
            }
            return;
        case 1:
            if (a[5] == 0) a[0xe] = a[0xf] = a[0x10] = 2;
            if (path_follow(o)) {
                f[1] = 0;
                a[0xe] = 0x1c;
                o->sub = 2;
            }
            sheep_turn(a, 0);
            a[2] = a[0];
            f[10] = (float)o->x;
            f[11] = (float)o->y;
            return;
        case 2:
            a[0xe] = 0x1c;
            sheep_turn(a, 0x80);
            a[2] = a[0];
            return;
        case 3:
            o->rec.shadow = -1;
            a[2] = (a[2] + 8) & 0xff;
            o->rec.scale_x -= 2;
            if (o->rec.scale_x < 1) {
                o->rec.scale_x = 0;
                path_begin(o, d[0x23], 0, 0);
                o->sub = 4;
            }
            o->rec.scale_y = o->rec.scale_x;
            return;
        case 4:
            o->rec.shadow = -1;
            g_cam_target = o;
            if (path_follow(o)) {
                o->rec.scale_x = o->rec.scale_y = 0x100;
                path_begin(o, d[0x23] + 1, 0, 0);
                o->sub = 5;
                a[0] = a[2] = 0x80;
                sound_play_at(o, 0x11, 0, 0);
            }
            f[10] = (float)o->x;
            f[11] = (float)o->y;
            return;
        case 5:
            g_cam_target = o;
            if (path_follow(o)) {
                g_cam_target = NULL;
                actor_set_state(a, 0);
                player_set_state(&g_player, 0);
            }
            f[10] = (float)o->x;
            f[11] = (float)o->y;
            return;
        }
        return;
    case 0x67:
        /* state 8 (walk to path d23 and along it), then held (4) */
        a[0x1d] &= ~1;
        o->rec.shadow_dy = 0;
        g_anim[a[9]].depth_off = 0;
        sheep_pathed((Sheep *)a, slot);
        if (a[3] == 0) a[3] = 4;
        return;
    case 0x68:
        /* as 100 (anim 6, hidden on the path), then flung in direction d16 at 3 px a tick for 48
           ticks into state d17 (0x19 straight away) */
        a[0x1d] &= ~1;
        switch (o->sub) {
        case 0:
            o->sub = 1;
            return;
        case 1:
            lv61_rise(a, 6);
            return;
        case 2:
            a[0xe] = 6;
            f[1] = 0;
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
                sound_play_at(o, 0x1d, 0, 0);
            }
            f[10] = (float)o->x;
            f[11] = (float)o->y;
            return;
        case 3: {
            a[0xe] = 6;
            float ny = (float)((double)f[11] - (double)g_cos[a[0]] * (double)f[1] + 2.0);
            f[10] = (float)((double)g_sin[a[0]] * (double)f[1] + (double)f[10]);
            f[11] = ny;
            int t = d[0x15];
            o->rec.shadow_dy = t;
            d[0x15] = t - 1;
            if (t < 1) actor_set_state(a, d[0x17]);
            return;
        }
        }
        return;
    case 0x69:
        /* spiral into (d15, d16): the radius d17 shrinks every 4th tick, the sprite with it under
           64, gone at 0 */
        if (o->sub == 0) {
            int dx = iabs(a[0xc] - d[0x15]), dy = iabs(a[0xd] - d[0x16]);
            d[0x17] = ftol(sqrt((double)(dy * dy + dx * dx)));
            d[0x18] = dir_of(a[0xc] - d[0x15], a[0xd] - d[0x16]);
            o->rec.shadow = -1;
            o->sub = 1;
        } else if (o->sub == 1) {
            int t = (d[0x18] + 8) & 0xff;
            d[0x18] = t;
            f[10] = (float)((double)(float)d[0x17] * (double)g_sin[t] + (double)(float)d[0x15]);
            f[11] = (float)((double)(float)d[0x16] - (double)(float)d[0x17] * (double)g_cos[t]);
            if ((d[0x1d]++ & 3) == 0) d[0x17]--;
            int r = d[0x17];
            if (r < 0x40) o->rec.scale_x = o->rec.scale_y = r * 4;
            if (r < 1) actor_out(a[9]);
        }
        return;
    case 0x6a:
        /* ride path d15 keeping the offset from its start, which closes a pixel a tick; then held */
        a[0x1d] &= ~1;
        switch (o->sub) {
        case 0:
            path_start(d[0x15]);
            d[0x1d] = a[0xc] - g_path_x;
            d[0x1e] = a[0xd] - g_path_y;
            o->sub = 1;
            return;
        case 1:
            f[1] = 0;
            a[0xe] = 2;
            path_begin(o, d[0x15], 0, 0);
            o->sub = 2;
            return;
        case 2:
            f[1] = 0;
            a[0xe] = 2;
            if (path_follow(o)) {
                a[3] = 4;
                a[4] = 1;
            }
            f[10] = (float)(o->x + d[0x1d]);
            f[11] = (float)(o->y + d[0x1e]);
            a[2] = o->dir << 4;
            if (d[0x1d] < 0) d[0x1d]++;
            if (d[0x1d] > 0) d[0x1d]--;
            if (d[0x1e] < 0) d[0x1e]++;
            if (d[0x1e] > 0) d[0x1e]--;
            return;
        }
        return;
    case 0x6b:
        /* standing facing down (Lv61_Move_36) */
        a[0xe] = 0x1f;
        f[1] = 0;
        a[2] = a[0] = 0x80;
        o->rec.mode = 2;
        return;
    }
}

static void lv61_move_00(AnimObj *a)
{
    /* 0x432410: armed by a touch; the next one sends its toucher up path d15 (state 100, d16 /
       d17 its parameters) until the animation stops */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] != -1) a->sub = 1;
    } else if (a->sub == 1) {
        if (d[0x22] != -1) {
            int32_t *w = actor(d[0x21]);
            actor_set_state(w, 100);
            int32_t *wd = g_anim[w[9]].d;
            wd[0x15] = d[0x15];
            wd[0x16] = d[0x16];
            wd[0x17] = d[0x17];
        }
        if (a->state == 2) a->sub = 0;
    }
    d[0x22] = -1;
}

static void lv61_spawn(int path, int anim, int mirror)
{
    /* Lv61_Move_02: an object at path's start running level routine 3 on it */
    path_start(path);
    int n = anim_new(g_path_x, g_path_y, anim, -1, 3);
    AnimObj *o = &g_anim[n];
    o->rec.mode = 2;
    o->rec.layer = 0;
    o->rec.sortable = 0;
    o->move = 0x8003;
    o->d[0x15] = anim;
    o->d[0x16] = path;
    if (mirror) o->mirror = 1;
}

static void lv61_move_02(AnimObj *a)
{
    /* 0x4324e0: two touches stop it (held, special d1b busy); until then frames 2, 7, 12 and 17
       each let go an object along a path (d15 / d16, d17 / d18, d19 / d1a, d17 / d18) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        d[0x1d] = 2;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (d[0x22] != -1) d[0x1d]--;
        if (d[0x1d] < 1) {
            a->countdown = 10;
            g_special[d[0x1b]].active = 1;
        } else if (a->frame != a->prev_frame) {
            switch (a->frame) {
            case 2: lv61_spawn(d[0x15], d[0x16], 1); break;
            case 7: lv61_spawn(d[0x17], d[0x18], 0); break;
            case 12: lv61_spawn(d[0x19], d[0x1a], 0); break;
            case 17: lv61_spawn(d[0x17], d[0x18], 0); break;
            }
        }
    }
    d[0x22] = -1;
}

static void lv61_move_04(AnimObj *a)
{
    /* 0x432720: the toucher falls (state 0x65, d15 / d16 / d17 its parameters) */
    int32_t *d = a->d;
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        who_set_state(w, 0x65);
        int32_t *wd = g_anim[w[9]].d;
        wd[0x15] = d[0x15];
        wd[0x16] = d[0x16];
        wd[0x17] = d[0x17];
    }
    d[0x22] = -1;
}

static void lv61_move_05(AnimObj *a)
{
    /* 0x4327c0: a toucher of kind d16 opens gate d18 (anim d17, then done); anyone else gets state
       d15 (the shepherd only when not recovering) */
    int32_t *d = a->d;
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        if (w[5] == d[0x16]) {
            anim_set(a->slot, d[0x17], -1);
            a->rec.mode = 3;
            gate_set(d[0x18], 1);
            d[0x22] = -1;
            a->move = 0;
            return;
        }
        if (w[9] < 0x1e) actor_set_state(w, d[0x15]);
        else if (g_player.cooldown == 0) player_set_state((Player *)w, d[0x15]);
    }
    d[0x22] = -1;
}

static void lv61_move_06(AnimObj *a)
{
    /* 0x432860: a winch; a sheep that touches it walks onto it (state 0x66, path d15) and, once
       there, is held while the shepherd works it: the sheep faces left or right as he stands
       beside it, the closer the faster (2 under 100 px, 1 under 150), winding the rope d1c (frame
       d1c / 4, 0..39) down (to 0: trigger d17, special d18 busy, once) or up; creeping into it
       throws the sheep off */
    int32_t *d = a->d;
    if (a->sub == 0) {
        a->countdown = 4;
        a->frame = d[0x1c] / 4;
        if (d[0x22] != -1 && d[0x21] < 0x1e) {
            int32_t *w = actor(d[0x21]);
            actor_set_state(w, 0x66);
            g_anim[w[9]].d[0x23] = d[0x15];
            d[0x1d] = w[9];
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        a->countdown = 4;
        a->frame = d[0x1c] / 4;
        int32_t *w = actor(d[0x1d]);
        if (w[0xe] == 0x1c) {
            w[3] = 4;
            a->sub = 2;
        }
    } else if (a->sub == 2) {
        int32_t *w = actor(d[0x1d]);
        float *wf = (float *)w;
        int wx = ftol(wf[10]), px = ftol(g_player.x);
        int wy = ftol(wf[11]), py = ftol(g_player.y);
        int dist = iabs(wx - px) + iabs(wy - py);
        int spd = dist < 100 ? 2 : dist < 0x96 ? 1 : 0;
        int dir = dir_of(px - wx, py - wy);
        if (dir >= 0x20 && dir <= 0x60) w[2] = w[0] = 0xc0;
        if (dir >= 0xa0 && dir <= 0xe0) w[2] = w[0] = 0x40;
        wf[1] = (float)spd;
        if (d[0x17] != 0) {
            if (w[0] == 0x40 && dir >= 0xa0 && dir <= 0xe0 && (d[0x1c] -= spd) < 0) {
                d[0x1c] = 0;
                trigger(d[0x17], 0x8000);
                g_special[d[0x18]].active = 1;
                sound_play_at(a, 0x25, 0, 0);
                d[0x17] = 0;
            }
            if (w[0] == 0xc0 && dir >= 0x20 && dir <= 0x60 && (d[0x1c] += spd) > 0x27) d[0x1c] = 0x27;
        }
        a->countdown = 4;
        a->frame = d[0x1c] / 4;
        if (d[0x22] != -1 && d[0x21] > 0x1e && g_creep != 0) {
            sheep_launch_sound(actor(d[0x1d]), 0x80, 1000, 0x14, 0, 0x19);
            a->sub = 0;
        }
    }
    d[0x22] = -1;
    a->rec.frame = a->frame;
}

static void lv61_move_07(AnimObj *a)
{
    /* 0x432b30: a toucher of kind d18 ends it (anim d1c, trigger d19); anyone else is thrown
       (direction d16, d17 x 10 ticks, height d1a, speed d1b) while it plays anim d15, a second
       before it takes another */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] == -1) return;
        int32_t *w = actor(d[0x21]);
        if (w[5] == d[0x18]) {
            anim_set(a->slot, d[0x1c], -1);
            trigger(d[0x19], 0x8000);
            a->rec.mode = 3;
            a->sub = 2;
            return;
        }
        if (w[9] < 0x1e) sheep_launch_sound(w, d[0x16], d[0x17] * 10, d[0x1a], 0, d[0x1b]);
        else player_launch_sound((Player *)w, d[0x16], d[0x17] * 10, d[0x1a], 0, d[0x1b]);
        d[0x1f] = a->anim;
        anim_set(a->slot, d[0x15], -1);
        d[0x1d] = 0x3c;
        a->sub = 1;
    } else if (a->sub == 1) {
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            a->sub = 0;
            d[0x22] = -1;
            if (a->anim != d[0x1f]) anim_set(a->slot, d[0x1f], -1);
        }
        if (a->anim == d[0x15] && a->state == 2) anim_set(a->slot, d[0x1f], -1);
    }
}

static void lv61_move_08(AnimObj *a)
{
    /* 0x432c70: Move_05 that, touched, makes specials 38-46, 48, 58 and 59 busy */
    static const uint8_t cells[] = { 38, 39, 40, 41, 42, 43, 44, 45, 46, 48, 58, 59 };
    if (a->d[0x22] != -1)
        for (size_t i = 0; i < sizeof cells; i++) g_special[cells[i]].active = 1;
    move_05(a);
}

static void lv61_move_23(AnimObj *a)
{
    /* 0x432cd0: as Move_08 for specials 28-30, 32-35, 55 and 102 */
    static const uint8_t cells[] = { 28, 29, 30, 32, 33, 34, 35, 55, 102 };
    if (a->d[0x22] != -1)
        for (size_t i = 0; i < sizeof cells; i++) g_special[cells[i]].active = 1;
    move_05(a);
}

static void lv61_move_10(AnimObj *a)
{
    /* 0x432d20: Move_02 once touched d19 times, with sound 0x18 as it starts */
    int32_t *d = a->d;
    if (d[0x22] != -1) d[0x19]--;
    if (d[0x19] < 1) {
        int s = a->sub;
        move_02(a);
        if (s == 0 && a->sub != 0) sound_play_at(a, 0x18, 0, 0);
    }
    d[0x22] = -1;
}

static void lv61_move_11(AnimObj *a)
{
    /* 0x432d70: a hint sign (hint d15, anims d16 / d17) that the shepherd can push away by
       creeping into it: anim d18 along path d19 dragging a repelling attractor, then triggers
       d1a / d1b and anim d16 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->anim != d[0x16]) anim_set(a->slot, d[0x16], -1);
        anim_stop_sound(a);
        if (d[0x22] != -1) {
            hint_show(d[0x15]);
            g_hint_y = 100;
            g_hint_x = 100;
            g_hint_owner = a->id;
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (a->anim != d[0x17]) anim_set(a->slot, d[0x17], -1);
        if (d[0x22] == -1) {
            if (g_hint_owner == a->id) {
                g_hint_shown = 0;
                a->sub = 0;
            }
        } else if (g_creep != 0) {
            anim_set(a->slot, d[0x18], -1);
            path_begin(a, d[0x19], 0, 0);
            d[0x1d] = attract_new(a->x, a->y, 1, 0x96, 9);
            g_hint_shown = 0;
            a->sub = 2;
        }
    } else if (a->sub == 2) {
        if (!path_follow(a)) {
            g_attract[d[0x1d]].x = (int16_t)a->x;
            g_attract[d[0x1d]].y = (int16_t)a->y;
        } else {
            anim_stop_sound(a);
            trigger(d[0x1a], 0x8000);
            trigger(d[0x1b], 0x8000);
            anim_set(a->slot, d[0x16], -1);
            g_attract[d[0x1d]].type = 0;
            a->sub = 3;
        }
    }
    d[0x22] = -1;
}

static void lv61_move_12(AnimObj *a)
{
    /* 0x432fc0: the lift; a toucher walks on (state 0x66, path d15) and, once up, gate 5 opens; the
       next touch (anim d17) sends it off along path d16 with the shepherd held still, gate 5
       closing when it has gone */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        if (d[0x22] != -1) {
            int32_t *w = actor(d[0x21]);
            actor_set_state(w, 0x66);
            g_anim[w[9]].d[0x23] = d[0x15];
            d[0x1d] = w[9];
            a->sub = 1;
        }
        break;
    case 1:
        if (actor(d[0x1d])[0xe] == 0x1c) {
            gate_set(5, 1);
            a->sub = 2;
        }
        break;
    case 2:
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x17], -1);
            player_set_state(&g_player, 4);
            g_player.speed = 0;
            g_anim[d[0x1d]].sub = 3;
            g_anim[d[0x1d]].d[0x23] = d[0x16];
            a->sub = 3;
        }
        break;
    case 3:
        if (actor(d[0x1d])[3] != 0x66) {
            gate_set(5, 0);
            a->sub = 0;
        }
        break;
    }
    d[0x22] = -1;
}

static void lv61_move_13(AnimObj *a)
{
    /* 0x433120: hidden until touched; then opens gate d15 and shows */
    a->rec.scale_x = 0;
    if (a->d[0x22] != -1) {
        gate_set(a->d[0x15], 1);
        a->rec.scale_x = 0x100;
        a->move = 0;
    }
}

static void lv61_move_14(AnimObj *a)
{
    /* 0x433160: Move_03 after d1b ticks; at each path start anim d15 */
    int32_t *d = a->d;
    if (a->sub != 0 || d[0x1b]-- < 0) {
        move_03(a);
        if (d[0x24] == 0 && d[0x25] == 0) anim_set(a->slot, d[0x15], -1);
    }
}

static void lv61_move_15(AnimObj *a)
{
    /* 0x4331b0: plays through, rests d17 x 6 ticks and restarts (a touch meanwhile: trigger d19);
       special d18 is free while the frame is within d15..d16 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1) {
        if (d[0x22] == -1) {
            int t = d[0x1d];
            d[0x1d] = t - 1;
            if (t < 1) {
                anim_set(a->slot, a->anim, -1);
                a->sub = 0;
            }
        } else {
            trigger(d[0x19], 0x8000);
        }
    }
    g_special[d[0x18]].active = !(d[0x15] <= a->frame && a->frame <= d[0x16]);
}

static void lv61_move_16(AnimObj *a)
{
    /* 0x433260: Move_05, drawing a random number at frame 1 of the running phase (unused) */
    if (a->frame == 1 && a->prev_frame != 1 && a->sub == 1) crt_rand();
    move_05(a);
}

static void lv61_move_17(AnimObj *a)
{
    /* 0x433290: touched, plays anim d15, from frame d16 letting go anim d17 along path d18 each
       frame; at its end triggers d19 / d1a */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (a->frame != a->prev_frame && d[0x16] <= a->frame) {
            path_start(d[0x18]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x17], -1, 3)];
            o->move = 3;
            o->d[0x15] = d[0x17];
            o->d[0x16] = d[0x18];
            o->d[0x1c] = 4;
        }
        if (a->state == 2) {
            trigger(d[0x19], 0x8000);
            trigger(d[0x1a], 0x8000);
            a->sub = 2;
        }
    }
}

static void lv61_move_18(AnimObj *a)
{
    /* 0x433390: the d1c-th touch plays anim d15 and triggers d16-d18 (and each touch after) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        d[0x20] = 0;
        d[0x1f] = d[0x1c];
        d[0x1c] = 0;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (d[0x22] != -1 && d[0x1f] <= ++d[0x20]) {
            anim_set(a->slot, d[0x15], -1);
            trigger(d[0x16], 0x8000);
            trigger(d[0x17], 0x8000);
            trigger(d[0x18], 0x8000);
        }
    }
    d[0x22] = -1;
}

static void lv61_move_19(AnimObj *a)
{
    /* 0x433480: plays anim d15 d1a times, letting go anim d16 along path d17 at frame d19, then
       rests d18 x 6 ticks */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->state == 2) {
            d[0x1d] = d[0x1a];
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (a->frame == d[0x19] && a->frame != a->prev_frame) {
            path_start(d[0x17]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
            o->move = 3;
            o->d[0x15] = d[0x16];
            o->d[0x16] = d[0x17];
            o->d[0x17] = 0x46;
            o->d[0x18] = 0x14;
            o->d[0x1c] = 4;
        }
        if (a->state == 2) {
            if (--d[0x1d] > 0) {
                anim_set(a->slot, d[0x15], -1);
                return;
            }
            a->sub = 2;
            d[0x1e] = d[0x18] * 6;
        }
    } else if (a->sub == 2) {
        int t = d[0x1e];
        d[0x1e] = t - 1;
        if (t < 1) a->sub = 0;
    }
}

static void lv61_move_20(AnimObj *a)
{
    /* 0x433590: a pair of cells, d15 and d17 (their special types switched as it goes): a sheep
       walks along path d16 to the first (state 0x67); the shepherd, touching next, turns to it
       and walks over (state 1, anim 3) until both are pushed off together (0x15) for 300 ticks */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        g_special[d[0x15]].type = 1;
        g_special[d[0x17]].type = 6;
        if (d[0x22] != -1) {
            int32_t *w = actor(d[0x21]);
            d[0x1b] = d[0x21];
            AnimObj *wo = &g_anim[w[9]];
            wo->d[0x23] = d[0x16];
            wo->d[0x15] = 0;
            actor_set_state(w, 0x67);
            g_special[d[0x15]].type = 5;
            g_special[d[0x17]].type = 2;
            a->sub = 1;
        }
        break;
    case 1:
        if (d[0x22] != -1) {
            int32_t *p = actor(d[0x21]);
            d[0x1c] = d[0x21];
            g_special[d[0x15]].type = 5;
            g_special[d[0x17]].type = 6;
            int32_t *s = actor(d[0x1b]);
            int dir = dir_of(s[0xc] - p[0xc], s[0xd] - p[0xd]);
            p[0] = p[2] = dir;
            player_set_state((Player *)p, 1);
            AnimObj *po = &g_anim[p[9]];
            po->d[0x15] = 0x280;
            po->d[0x16] = 0x280;
            po->d[8] = p[0xb];
            po->d[0x18] = 10;
            po->d[0x1b] = 0x14;
            anim_set(p[9], 3, -1);
            a->sub = 2;
        }
        break;
    case 2: {
        int32_t *s = actor(d[0x1b]), *p = actor(d[0x1c]);
        if (p[3] == 0) {
            g_special[d[0x15]].type = 0;
            g_special[d[0x17]].type = 6;
            p[3] = 0x15;
            s[3] = 0x15;
            g_anim[p[9]].d[0x21] = s[9];
            p[1] = 0;
            p[0xe] = 7;
            anim_set(p[9], 7, -1);
            d[0x1d] = 300;
            a->sub = 3;
        }
        break;
    }
    case 3: {
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) a->sub = 0;
        break;
    }
    }
    d[0x22] = -1;
}

static void lv61_move_21(AnimObj *a)
{
    /* 0x433820: between frames d19 and d1a lets go anim d16 along path d15 each frame (routine 4,
       sound 5 / 9 on the first), rests d17 x 6 ticks and restarts with object d18 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        int fr = a->frame;
        if (d[0x19] < fr && fr < d[0x1a] && fr != a->prev_frame) {
            if (fr == d[0x19] + 1) sound_play_at(a, g_level == 1 ? 5 : 9, 0, 0);
            path_start(d[0x15]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
            o->move = 4;
            o->d[0x15] = d[0x16];
            o->d[0x16] = d[0x15];
            o->d[0x17] = 10;
            o->d[0x18] = 10;
            o->d[0x1c] = 1;
            o->d[0x19] = 0x10;
            o->d[0x1a] = 0x10;
        }
        if (a->state == 2) {
            a->sub = 1;
            d[0x1d] = d[0x17] * 6;
        }
    } else if (a->sub == 1) {
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            if (d[0x18] != 0) {
                int s = g_obj_by_id[d[0x18]];
                anim_set(s, g_anim[s].anim, -1);
            }
            anim_set(a->slot, a->anim, -1);
            a->sub = 0;
        }
    }
}

static void lv61_move_22(AnimObj *a)
{
    /* 0x433950: anim d15 while touched, back to its own (sound d16) when let go */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        d[0x22] = -1;
        d[0x1d] = a->anim;
        a->sub = 1;
        return;
    case 1:
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x15], -1);
            d[0x22] = -1;
            a->sub = 2;
            return;
        }
        break;
    case 2:
        if (d[0x22] == -1) {
            d[0x22] = -1;
            a->sub = 3;
            return;
        }
        break;
    case 3:
        anim_set(a->slot, d[0x1d], -1);
        /* the original passes d16's low half (the high half is left over in the register) */
        if (d[0x16] != -1) sound_play_at(a, (int16_t)d[0x16], 0, 0);
        a->sub = 1;
        break;
    }
    d[0x22] = -1;
}

static void lv61_move_24(AnimObj *a)
{
    /* 0x433a10: at frame d18 lets go anim d16 along path d15 (level routine 0x19; one of sounds
       0x1a-0x1c if d19), rests d17 x 6 ticks and restarts */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->frame == d[0x18] && a->frame != a->prev_frame) {
            path_start(d[0x15]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
            o->move = 0x8019;
            o->d[0x15] = d[0x16];
            o->d[0x16] = d[0x15];
            if (d[0x19] != 0) sound_play_at(a, crt_rand() % 3 + 0x1a, 0, 0);
        }
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
}

static void lv61_move_25(AnimObj *a)
{
    /* 0x433af0: along path d16 and gone */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x16], 1, 0);
        d[0x21] = -1;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (path_follow(a)) {
            d[0x15] = 0xb4;
            anim_kill(a);
        }
    }
}

static void lv61_move_26(AnimObj *a)
{
    /* 0x433b50: the toucher is flung (state 0x68, d15 / d16 / d17 its parameters) */
    int32_t *d = a->d;
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        actor_set_state(w, 0x68);
        int32_t *wd = g_anim[w[9]].d;
        wd[0x15] = d[0x15];
        wd[0x16] = d[0x16];
        wd[0x17] = d[0x17];
    }
    d[0x22] = -1;
}

static void lv61_move_28(AnimObj *a)
{
    /* 0x433be0: grows to double size and is gone */
    a->rec.scale_y += 5;
    a->rec.scale_x += 5;
    if (a->rec.scale_x > 0x1ff) anim_kill(a);
}

static void lv61_throw(AnimObj *a, int sound)
{
    /* Lv61_Move_27 / 34 at frame 4: a puff (anim d16, level routine 0x1c) 10 px left of it, then
       rest d18 x 6 ticks */
    int32_t *d = a->d;
    AnimObj *o = &g_anim[anim_new(a->x - 10, a->y, d[0x16], -1, 3)];
    o->rec.mode = 2;
    o->rec.layer = 0;
    o->rec.sortable = 0;
    o->move = 0x801c;
    o->d[0x17] = 0x65;
    o->d[0x15] = d[0x16];
    o->d[0x18] = 0x14;
    a->sub = 3;
    d[0x1e] = d[0x18] * 6;
    if (sound) sound_play_at(a, 0xd, 0, 0);
}

static void lv61_move_27(AnimObj *a)
{
    /* 0x433c20: puffs (see lv61_throw), anim d15 between */
    int32_t *d = a->d;
    if (a->sub == 0) {
        d[0x1d] = 1;
        a->sub = 2;
    } else if (a->sub == 2) {
        if (a->frame == 4 && a->prev_frame != 4) {
            lv61_throw(a, 0);
            d[0x22] = -1;
            return;
        }
    } else if (a->sub == 3) {
        int t = d[0x1e];
        d[0x1e] = t - 1;
        if (t < 1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 2;
        }
    }
    d[0x22] = -1;
}

static void lv61_move_29(AnimObj *a)
{
    /* 0x433d30: a jet with tile animation d18: on (sound 10) for d16 x 6 ticks pushing touchers
       into state 10, off for d17 x 6 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        anim_set(a->slot, d[0x15], -1);
        a->rec.scale_x = 0;
        g_tile_anim[d[0x18]].mode = 1;
        a->sub = 1;
        a->sound = 0;
    } else if (a->sub == 1) {
        g_tile_anim[d[0x18]].mode = 1;
        if (d[0x22] != -1) who_set_state(actor(d[0x21]), 10);
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            g_tile_anim[d[0x18]].mode = 0;
            g_tile_anim[d[0x18]].frame = 0;
            a->sub = 2;
            d[0x1d] = d[0x17] * 6;
            sound_drop(a->sound);   /* d27 stays */
            a->sound = 0;
        }
    } else if (a->sub == 2) {
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            d[0x1d] = d[0x16] * 6;
            a->sub = 1;
            a->sound = sound_play_at(a, 10, 1, 0);
        }
    }
    d[0x22] = -1;
}

static void lv61_move_30(AnimObj *a)
{
    /* 0x433ea0: while touched, an attractor (d17: type x 10 + power, range d18) and the looping
       sound d1c over quieter music */
    int32_t *d = a->d;
    a->rec.scale_x = 0;
    if (a->sub == 0) {
        if (d[0x22] != -1) {
            if (d[0x17] != 0) d[0x1d] = attract_new(a->x, a->y, d[0x17] / 10, d[0x18], d[0x17] % 10);
            if (g_music) snd_set_vol(g_music, 0.8f);
            g_music2 = sound_stream((int16_t)d[0x1c], 1, 1);
            if (g_music2) snd_set_vol(g_music2, 1.0f);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (d[0x22] == -1) {
            if (g_music) snd_set_vol(g_music, 0.95f);
            sound_stream_drop(g_music2);
            if (d[0x17] != 0) g_attract[d[0x1d]].type = 0;
            a->sub = 0;
        }
    }
    d[0x22] = -1;
}

static void lv61_move_31(AnimObj *a)
{
    /* 0x433ff0: alternates anim d15 (sound 0x1e then 10, looping; special d19 free) for d17 x 6
       ticks and anim d16 (special d19 busy, silent) for d18 x 6 */
    int32_t *d = a->d;
    int snd;
    if (a->sub == 0) {
        anim_set(a->slot, d[0x15], -1);
        a->sub = 1;
        a->sound = 0;
        d[0x1d] = d[0x17] * 6;
        snd = 0x1e;
    } else if (a->sub == 1) {
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            anim_set(a->slot, d[0x16], -1);
            a->sub = 2;
            d[0x1d] = d[0x18] * 6;
            g_special[d[0x19]].active = 1;
            sound_drop(a->sound);   /* d27 stays */
            a->sound = 0;
        }
        d[0x22] = -1;
        return;
    } else {
        int t;
        if (a->sub != 2 || (t = d[0x1d], d[0x1d] = t - 1, t > 0)) {
            d[0x22] = -1;
            return;
        }
        a->sub = 1;
        d[0x1d] = d[0x17] * 6;
        anim_set(a->slot, d[0x15], -1);
        snd = 10;
        g_special[d[0x19]].active = 0;
    }
    a->sound = sound_play_at(a, snd, 1, 0);
    d[0x22] = -1;
}

static void lv61_move_32(AnimObj *a)
{
    /* 0x434150: Move_02, hidden */
    a->rec.scale_x = 0;
    move_02(a);
}

static void lv61_move_33(AnimObj *a)
{
    /* 0x434170: swaps anims d15 / d16 every 64-127 ticks until object d18 starts (sub 1); then a
       touch plays anim d17 and from its frame 2 it is done (trigger d19) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            anim_set(a->slot, a->anim == d[0x15] ? d[0x16] : d[0x15], -1);
            d[0x1d] = (rng_next() & 0x3f) + 0x40;
        }
        if (g_anim[g_obj_by_id[d[0x18]]].sub == 1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x17], -1);
            a->sub = 2;
        }
    } else if (a->sub == 2 && a->frame > 1) {
        a->rec.mode = 3;
        a->sub = 3;
        trigger(d[0x19], 0x8000);
    }
}

static void lv61_move_34(AnimObj *a)
{
    /* 0x434270: Move_27 with sound 0xd at each puff */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        d[0x1d] = 1;
        a->sub = 2;
        break;
    case 1:
    case 2:
        if (a->frame == 4 && a->prev_frame != 4) lv61_throw(a, 1);
        break;
    case 3: {
        int t = d[0x1e];
        d[0x1e] = t - 1;
        if (t < 1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 2;
        }
        break;
    }
    }
    d[0x22] = -1;
}

static void lv61_carry(AnimObj *a)
{
    int32_t *w = actor(a->d[0x1d]);
    float *f = (float *)w;
    f[10] = (float)(a->d[0x1a] + a->x);
    f[11] = (float)(a->d[0x1b] + a->y);
}

static void lv61_move_35(AnimObj *a)
{
    /* 0x434390: a crane, calling now and then (sounds 0x15-0x19); touched, it runs path d15 (anim
       d18): from point 1 the first to touch it again is held at (d1a, d1b) from it (state 4),
       at point 2 anim d19, at point 3 anim d16 and the sheep is lost; then waits again */
    int32_t *d = a->d;
    int touched = d[0x22];
    switch (a->sub) {
    case 0:
        a->sub = 1;
        d[0x22] = -1;
        d[0x1e] = crt_rand() % 100 + 100;
        return;
    case 1:
        if (touched != -1) {
            path_begin(a, d[0x15], 1, 0);
            anim_set(a->slot, d[0x18], -1);
            d[0x1d] = -1;
            a->sub = 2;
        }
        if (--d[0x1e] < 1) {
            d[0x1e] = crt_rand() % 100 + 100;
            sound_play_at(a, crt_rand() % 5 + 0x15, 0, 0);
            d[0x22] = -1;
            return;
        }
        break;
    case 2:
        path_follow(a);
        path_follow(a);
        if (d[0x24] == 1) {
            a->sub = 3;
            d[0x22] = -1;
            return;
        }
        break;
    case 3:
        path_follow(a);
        if (d[0x24] == 2) {
            anim_set(a->slot, d[0x19], -1);
            a->sub = 4;
        }
        if (d[0x1d] != -1) {
            lv61_carry(a);
        } else if (touched != -1) {
            d[0x1d] = d[0x21];
            actor_set_state(actor(d[0x21]), 4);
            lv61_carry(a);
        }
        break;
    case 4:
        path_follow(a);
        if (d[0x24] == 3) {
            anim_set(a->slot, d[0x16], -1);
            if (d[0x1d] != -1) actor_out(d[0x1d]);
            a->sub = 5;
        }
        if (d[0x1d] != -1) lv61_carry(a);
        break;
    case 5:
        if (path_follow(a)) a->sub = 1;
        break;
    }
    d[0x22] = -1;
}

static void lv61_move_36(AnimObj *a)
{
    /* 0x434620: a touch walks the shepherd to path d15's start (objects d17 / d18 to sub 5) where
       he stands (0x6b) for 3 seconds: trigger d19, then from the 10th tick on d16; a touch after
       (d16 still triggered) and d19 gets 0x8001 until let go */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        if (d[0x22] != -1) {
            d[0x22] = -1;
            a->sub = 1;
            return;
        }
        break;
    case 1:
        g_anim[g_obj_by_id[d[0x17]]].sub = 5;
        g_anim[g_obj_by_id[d[0x18]]].sub = 5;
        path_start(d[0x15]);
        g_player.speed = 2.5f;
        if (actor_walk_to(g_player.d, g_path_x, g_path_y)) {
            g_player.x = (float)g_path_x;
            g_player.y = (float)g_path_y;
            player_set_state(&g_player, 0x6b);
            g_anim[g_obj_by_id[d[0x19]]].d[0x22] = 0x8000;
            d[0x22] = -1;
            d[0x1d] = 0xb4;
            a->sub = 3;
            return;
        }
        break;
    case 3: {
        if (d[0x1d] < 0xab) g_anim[g_obj_by_id[d[0x16]]].d[0x22] = 0x8000;
        int t = d[0x1d];
        d[0x1d] = t - 1;
        if (t < 1) {
            a->rec.mode = 1;
            player_set_state(&g_player, 0);
            d[0x22] = -1;
            a->sub = 4;
            return;
        }
        break;
    }
    case 4:
        g_anim[g_obj_by_id[d[0x16]]].d[0x22] = 0x8000;
        if (d[0x22] != -1) {
            d[0x22] = -1;
            a->sub = 5;
            return;
        }
        break;
    case 5:
        g_anim[g_obj_by_id[d[0x19]]].d[0x22] = 0x8001;
        if (d[0x22] == -1) a->sub = 0;
        break;
    }
    d[0x22] = -1;
}

static void lv61_move_37(AnimObj *a)
{
    /* 0x434810: a relay: 0x8000 shows anim d15, 0x8001 anim d16, each passed on to d17 / d18 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        anim_set(a->slot, d[0x16], -1);
        a->sub = 1;
    }
    if (d[0x22] == 0x8000) {
        anim_set(a->slot, d[0x15], -1);
        trigger(d[0x17], 0x8000);
        trigger(d[0x18], 0x8000);
    }
    if (d[0x22] == 0x8001) {
        anim_set(a->slot, d[0x16], -1);
        trigger(d[0x17], 0x8001);
        trigger(d[0x18], 0x8001);
    }
    d[0x22] = -1;
}

static void lv61_move_38(AnimObj *a)
{
    /* 0x434900: at frame d19 lets go anim d16 (direction 8) at path d15's start, running level
       routine 0x27 with d15-d18 */
    int32_t *d = a->d;
    if (a->frame == d[0x19] && a->frame != a->prev_frame) {
        path_start(d[0x15]);
        AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], 8, 3)];
        o->move = 0x8027;
        o->d[0x15] = d[0x15];
        o->d[0x16] = d[0x16];
        o->d[0x17] = d[0x17];
        o->d[0x18] = d[0x18];
        o->d[0x19] = -1;
    }
}

static void lv61_move_39(AnimObj *a)
{
    /* 0x434990: a hook along path d15 in a random tint; it picks up every sheep within 12 px
       (Manhattan) of (d17, d18) from it (state 4, anim 6, its tint; the last is carried, 10 px
       below it, anim d17 once d16 ends) and drops it at the end (anim d18) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        a->rec.tint = (int8_t)((int)rng_next() % 6 + 1);
        path_begin(a, d[0x15], 1, 0);
        a->sub = 1;
    } else if (a->sub == 1) {
        if (a->anim == d[0x16] && a->state == 2) anim_set(a->slot, d[0x17], 8);
        if (path_follow(a)) {
            anim_set(a->slot, d[0x18], 8);
            a->sub = 2;
            return;
        }
        if (d[0x19] != -1) {
            int32_t *w = actor(d[0x19]);
            float *f = (float *)w;
            f[10] = (float)a->x;
            f[1] = 0;
            w[2] = 0x80;
            w[0] = 0x80;
            w[0xe] = 6;
            f[11] = (float)((double)(float)a->y + 10.0);
            return;
        }
        for (int i = 0; i < MAX_SHEEP; i++) {
            Sheep *s = &g_sheep[i];
            if (s->alive == 2 || s->d[0x16] != 0) continue;
            int dx = iabs(d[0x17] - ftol(s->x) + a->x), dy = iabs(d[0x18] - ftol(s->y) + a->y);
            if (dx + dy < 0xc) {
                g_anim[i].rec.tint = a->rec.tint;
                actor_set_state(s->d, 4);
                anim_set(i, 6, 8);
                d[0x19] = s->slot;
            }
        }
    } else if (a->sub == 2 && d[0x19] != -1) {
        int32_t *w = actor(d[0x19]);
        float *f = (float *)w;
        actor_set_state(w, 0);
        f[10] = (float)a->x;
        f[11] = (float)a->y;
        actor_set_state(w, 0);
        d[0x19] = -1;
    }
}

bool lv6_actor(uint32_t va, int32_t *actor)
{
    if (va != 0x431bf0) return false;
    lv61_actor(actor);
    return true;
}

MoveFn lv6_move(uint32_t va)
{
    switch (va) {
    case 0x432410: return lv61_move_00;
    case 0x4324e0: return lv61_move_02;
    case 0x432720: return lv61_move_04;
    case 0x4327c0: return lv61_move_05;
    case 0x432860: return lv61_move_06;
    case 0x432b30: return lv61_move_07;
    case 0x432c70: return lv61_move_08;
    case 0x432cd0: return lv61_move_23;
    case 0x432d20: return lv61_move_10;
    case 0x432d70: return lv61_move_11;
    case 0x432fc0: return lv61_move_12;
    case 0x433120: return lv61_move_13;
    case 0x433160: return lv61_move_14;
    case 0x4331b0: return lv61_move_15;
    case 0x433260: return lv61_move_16;
    case 0x433290: return lv61_move_17;
    case 0x433390: return lv61_move_18;
    case 0x433480: return lv61_move_19;
    case 0x433590: return lv61_move_20;
    case 0x433820: return lv61_move_21;
    case 0x433950: return lv61_move_22;
    case 0x433a10: return lv61_move_24;
    case 0x433af0: return lv61_move_25;
    case 0x433b50: return lv61_move_26;
    case 0x433be0: return lv61_move_28;
    case 0x433c20: return lv61_move_27;
    case 0x433d30: return lv61_move_29;
    case 0x433ea0: return lv61_move_30;
    case 0x433ff0: return lv61_move_31;
    case 0x434150: return lv61_move_32;
    case 0x434170: return lv61_move_33;
    case 0x434270: return lv61_move_34;
    case 0x434390: return lv61_move_35;
    case 0x434620: return lv61_move_36;
    case 0x434810: return lv61_move_37;
    case 0x434900: return lv61_move_38;
    case 0x434990: return lv61_move_39;
    default: return NULL;
    }
}
