#include "moves.h"
#include "exe.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "levels.h"
#include "paths.h"
#include "player.h"
#include "sheep.h"
#include "sound.h"
#include "world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int g_pickups, g_score_flash, g_timer_on;
uint8_t g_bonus;
int g_hint_owner, g_hint_shown, g_hint_x, g_hint_y, g_hint;
int g_switch_wait[32];
int g_carry_time;
AnimObj *g_carry_obj;

enum { STD_MOVES = 0x44c57c, BONUS_FLAGS = 0x44b194 };

/* g_obj_by_id[id]: an animated object's slot (the original indexes with whatever is there). */
static AnimObj *obj_by_id(int id)
{
    if (id < 0 || id >= MAX_OBJ_ID) return NULL;
    int s = g_obj_by_id[id];
    return s >= 0 && s < MAX_ANIM ? &g_anim[s] : NULL;
}

static void trigger(int id, int32_t v)
{
    AnimObj *o = obj_by_id(id);
    if (o) o->d[0x22] = v;
}

static Special *special(int i) { return i >= 0 && i < MAX_SPECIAL ? &g_special[i] : &g_special[0]; }

/* ------------------------------------------------------------------ gates (0x403030..0x403130) */

static int ngates;                      /* 0x49c078 */

static void copy_cells16(int sx, int sy, int dx, int dy, int w, int h)
{
    /* 0x403050: layer 1 */
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            g_lv.layer1[(dy + j) * g_lv.w + dx + i] = g_lv.layer1[(sy + j) * g_lv.w + sx + i];
}

static void copy_cells32(int sx, int sy, int dx, int dy, int w, int h)
{
    /* 0x4030c0: layer 3 */
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            g_lv.layer3[(dy + j) * g_lv.w + dx + i] = g_lv.layer3[(sy + j) * g_lv.w + sx + i];
}

void gate_set(int g, int open)
{
    /* Entries of 8 i16: {w, h, x, y, closed x, y, open x, y} in px; cells are 32 x 24. */
    if (g >= ngates) return;
    uint32_t e = desc_u32(DESC_GATES) + 16u * (uint32_t)g;
    int w = (int16_t)exe_u16(e), h = (int16_t)exe_u16(e + 2);
    int x = (int16_t)exe_u16(e + 4) / 32, y = (int16_t)exe_u16(e + 6) / 24;
    if (!open) {
        int sx = (int16_t)exe_u16(e + 8) / 32, sy = (int16_t)exe_u16(e + 10) / 24;
        copy_cells16(sx, sy, x, y, w, h);
        copy_cells32(sx, sy, x, y, w, h);
    } else {
        int sx = (int16_t)exe_u16(e + 12) / 32, sy = (int16_t)exe_u16(e + 14) / 24;
        copy_cells32(sx, sy, x, y, w, h);
        copy_cells16(sx, sy, x, y, w, h);
    }
}

void gates_init(void)
{
    ngates = 0;
    uint32_t t = desc_u32(DESC_GATES);
    if (!t) return;
    for (int i = 0; exe_u16(t + 16u * (uint32_t)i); i++) {
        ngates++;
        gate_set(i, 0);
    }
}

/* ------------------------------------------------------------------ shared helpers */

/* Which side of (x, y, w, h) the point (dx, dy inside it) leaves by: 0 left, 1 right, 2 top, 3 bottom. */
static int push_side(int dx, int dy, int w, int h)
{
    if (dx < w / 2) {
        if (dy < h / 2) return dy < dx ? 2 : 0;
        return dx <= h - dy ? 0 : 3;
    }
    if (dy < h / 2) return w - dx <= dy ? 1 : 2;
    return w - dx <= h - dy ? 1 : 3;
}

static void push_point(float *px, float *py, int x, int y, int w, int h)
{
    int dx = ftol((double)*px) - x, dy = ftol((double)*py) - y;
    switch (push_side(dx, dy, w, h)) {
    case 0: *px = (float)(x - 1); break;
    case 1: *px = (float)(x + 1 + w); break;
    case 2: *py = (float)(y - 1); break;
    case 3: *py = (float)(y + 1 + h); break;
    }
}

static bool inside(float px, float py, int x, int y, int w, int h)
{
    int dx = ftol((double)px) - x, dy = ftol((double)py) - y;
    return dx > -1 && dx < w && dy > -1 && dy < h;
}

void push_out_of_rect(int x, int y, int w, int h)
{
    /* 0x411560: every sheep slot (gone ones too), then the shepherd */
    for (int i = 0; i < MAX_SHEEP; i++) {
        Sheep *s = &g_sheep[i];
        if (!inside(s->x, s->y, x, y, w, h)) continue;
        push_point(&s->x, &s->y, x, y, w, h);
        if (s->state == 0x15) actor_set_state(s->d, 0);
    }
    if (inside(g_player.x, g_player.y, x, y, w, h)) push_point(&g_player.x, &g_player.y, x, y, w, h);
}

void hint_show(int n)
{
    /* 0x411a70 looks the number up in the level's hint text; the text and its drawing are not ported
       yet, so only the state the sign routines test is kept. */
    g_hint = n;
    g_hint_shown = 1;
}

/* ------------------------------------------------------------------ standard routines */

static void move_01(AnimObj *a)
{
    /* 0x411dd0: a launcher; d15 anim while busy, d16 dir, d17 count / 10, d18 then, d19 busy time / 6,
       d1a height, d1b speed */
    int32_t *d = a->d;
    int sp = d[0x22];
    if (a->sub == 0) {
        if (sp == -1) return;
        special(sp)->active = 1;
        int32_t *who = actor(d[0x21]);
        if (who[9] < 0x1e) sheep_launch_sound(who, d[0x16], d[0x17] * 10, d[0x1a], d[0x18], d[0x1b]);
        else player_launch_sound((Player *)who, d[0x16], d[0x17] * 10, d[0x1a], d[0x18], d[0x1b]);
        d[0x1f] = a->anim;
        anim_set(a->slot, d[0x15], -1);
        a->sub = 1;
        d[0x1c] = d[0x19] * 6;
    } else if (a->sub == 1) {
        if (d[0x1c]-- < 1) {
            a->sub = 0;
            special(sp)->active = 0;
            d[0x22] = -1;
            if (a->anim != d[0x1f]) anim_set(a->slot, d[0x1f], -1);
        }
        if (a->anim == d[0x15] && a->state == 2) anim_set(a->slot, d[0x1f], -1);
    }
}

void move_02(AnimObj *a)
{
    /* 0x411ef0: a gate opening once; d15 anim, d16 gate, d17 path (-1: animation only), d18 1: drawn
       behind once open, d19 restore the anim at the path's end */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        if (d[0x22] != -1) {
            d[0x1d] = a->anim;
            anim_set(a->slot, d[0x15], -1);
            if (d[0x17] == -1) {
                a->sub = 1;
                d[0x22] = -1;
                return;
            }
            path_begin(a, d[0x17], 1, 0);
            a->sub = 2;
        }
        d[0x22] = -1;
        break;
    case 1:
        if (a->frame == a->nframes - 1) {
            if (d[0x18] == 1) a->rec.mode = 3;
            gate_set(d[0x16], 1);
            a->move = 0;
        }
        break;
    case 2:
        if (path_follow(a)) {
            if (d[0x19]) anim_set(a->slot, d[0x1d], -1);
            if (d[0x18] == 1) a->rec.mode = 3;
            gate_set(d[0x16], 1);
            a->move = 0;
        }
        break;
    }
}

void move_03(AnimObj *a)
{
    /* 0x411fe0: follows path d16 (mode d1c) carrying up to two attractors: d17 / d19 = type * 10 +
       power, d18 / d1a range */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x16], d[0x1c], 0);
        anim_set(a->slot, d[0x15], -1);
        if (d[0x17]) d[0x1d] = attract_new(a->x, a->y, d[0x17] / 10, d[0x18], d[0x17] % 10);
        if (d[0x19]) d[0x1e] = attract_new(a->x, a->y, d[0x19] / 10, d[0x1a], d[0x19] % 10);
        a->sub = 1;
    } else if (a->sub == 1) {
        path_follow(a);
        if (a->type == -1) {
            if (d[0x17]) g_attract[d[0x1d]].type = 0;
            if (d[0x19]) g_attract[d[0x1e]].type = 0;
        } else {
            if (d[0x17]) { g_attract[d[0x1d]].x = (int16_t)a->x; g_attract[d[0x1d]].y = (int16_t)a->y; }
            if (d[0x19]) { g_attract[d[0x1e]].x = (int16_t)a->x; g_attract[d[0x1e]].y = (int16_t)a->y; }
        }
    }
}

void move_05(AnimObj *a)
{
    /* 0x412270: a touch trigger; d15 anim, d16-d18 objects it triggers, d19 re-arm time / 6 (-1:
       once), d1a 1: drawn behind, d1b tint, d1c score */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        a->rec.tint = (int8_t)d[0x1b];
        if (d[0x22] == -1) return;
        if (d[0x1c]) g_pickups++;
        d[0x1d] = a->anim;
        if (!(d[0x22] & 0x8000)) special(d[0x22])->active = 1;
        anim_set(a->slot, d[0x15], -1);
        a->sub = 1;
        break;
    case 1:
        if (a->frame > 1) {
            if (d[0x16]) trigger(d[0x16], 0x8000);
            if (d[0x17]) trigger(d[0x17], 0x8000);
            if (d[0x18]) trigger(d[0x18], 0x8000);
            g_score += d[0x1c];
            if (d[0x1a] == 1) a->rec.mode = 3;
            if (d[0x19] == -1) {
                a->sub = 3;
                return;
            }
            a->sub = 2;
            d[0x1e] = d[0x19] * 6;
        }
        break;
    case 2:
        if (d[0x1e]-- < 1) {
            a->sub = 0;
            if (!(d[0x22] & 0x8000)) special(d[0x22])->active = 0;
            d[0x22] = -1;
        }
        break;
    }
}

static void kill_effect(void)
{
    /* The running effect-3 object; TODO: the music volume (0x4376b0) and its looping sound */
    int s = g_anim[g_player.slot].d[0x21];
    anim_kill(&g_anim[s]);
}

static void pickup_taken(AnimObj *a)
{
    g_score_flash = 0x48;
    a->d[0x1c] = 0;
    a->sub = 1;
}

static void move_07(AnimObj *a)
{
    /* 0x412480: pickups and power-ups; d16 the kind */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0: {
        switch (d[0x16]) {
        case 3: case 4: case 6: case 10: case 0x14: case 0x15: case 0x16: break;
        case 0xb: a->sub = 9; break;
        default:
            fprintf(stderr, "Unrecognized pickup type %d\nX %d\nY %d\n", d[0x16], a->x, a->y);
            break;
        }
        if (d[0x22] == -1) return;
        if ((d[0x16] == 3 || d[0x16] == 6) && g_player.effect == 3) kill_effect();
        int k = d[0x16];
        switch (k) {
        case 1: case 2: case 7: break;
        case 3: {
            g_player.effect_time = 900;
            g_player.effect = k;
            int s = anim_new(ftol((double)g_player.x), ftol((double)g_player.y), 10, 8, 4);
            g_anim[s].move = 7;
            g_anim[s].sub = 6;
            g_anim[s].d[0x15] = g_player.slot;
            g_anim[g_player.slot].d[0x21] = s;
            /* TODO: music down (0x4376b0) and the looping sound 0x61 (0x41f160) */
            break;
        }
        case 4: {
            g_player.effect_time = 0x708;
            g_player.effect = k;
            memset(g_chain, 0, sizeof g_chain);
            g_chain[31][0] = g_player.d;
            int s = anim_new(g_player.ix, g_player.iy, 0xb, 8, 4);
            g_anim[PLAYER_SLOT].d[0x21] = s;
            g_anim[s].move = 7;
            g_anim[s].sub = 8;
            g_anim[s].d[0x15] = 0x1f;
            sound_play_at(a, 0x5b, 0, 0);
            break;
        }
        case 6:
            g_effect_count = 9;
            g_player.effect = k;
            sound_play_at(a, 0x5b, 0, 0);
            break;
        case 10:
            g_bonus |= exe_u8(BONUS_FLAGS + 4u * (uint32_t)g_level);
            g_score += 1000;
            sound_play_at(a, 0x5a, 0, 0);
            break;
        case 0x14: g_score += 0x19; sound_play_at(a, 0x5b, 0, 0); break;
        case 0x15: g_score += 0x32; sound_play_at(a, 0x5b, 0, 0); break;
        case 0x16: g_score += 100; sound_play_at(a, 0x5b, 0, 0); break;
        default: g_player.effect = 0; break;
        }
        pickup_taken(a);
        break;
    }
    case 1: {
        /* swell, then shrink away into the sparkle */
        int t = d[0x1c] += 2;
        a->rec.scale_x = a->rec.scale_y = g_isin[t & 0xff] / 64 + 0x100;
        if (a->rec.scale_x < 4) {
            a->type = 4;
            anim_set(a->slot, 9, 8);
            a->rec.scale_x = a->rec.scale_y = 0x100;
            a->rec.mode = 2;
            a->rec.layer = 0;
            a->rec.sortable = 0;
            a->sub = 2;
        }
        break;
    }
    case 4:
        d[0x17] = 0xb4;
        d[0x15] = attract_new((int16_t)a->x, (int16_t)a->y, 1, 0xfa, 5);
        a->sub = 5;
        break;
    case 5:
        if (--d[0x17] < 0) {
            g_attract[d[0x15]].type = 0;
            anim_kill(a);
        }
        break;
    case 6:
        a->x = ftol((double)g_player.x);
        a->y = ftol((double)g_player.y);
        if (g_player.effect != 3) anim_kill(a);
        break;
    case 7: {
        Sheep *s = &g_sheep[d[0x15]];
        a->x = s->ix;
        a->y = s->iy;
        int32_t *w = actor(d[0x15]);
        if (w[4] == 2 && g_anim[w[9]].d[0x21] != 0) anim_kill(&g_anim[g_anim[w[9]].d[0x21]]);
        break;
    }
    case 8: {
        int32_t *w = actor(d[0x15]);
        a->x = w[0xc];
        a->y = w[0xd];
        if (w[4] == 2 || g_player.effect != 4) {
            anim_kill(a);
            g_fvars = g_flock;
        }
        break;
    }
    case 9: {
        int dx = g_player.ix - a->x, dy = g_player.iy - a->y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx + dy < 0x18) {
            g_score += 2000;
            sound_play_at(a, 0x5b, 0, 0);
            d[0x1c] = 0;
            a->sub = 1;
        }
        break;
    }
    }
}

static void move_13(AnimObj *a)
{
    /* 0x413750: a hint sign; d15 the hint number */
    if (a->sub == 0) {
        if (a->d[0x22] != -1) {
            hint_show(a->d[0x15]);
            g_hint_x = g_hint_y = 100;
            g_hint_owner = a->id;
            a->sub = 1;
            sound_play_at(a, 0x60, 0, 0);
        }
    } else if (a->sub == 1) {
        if (a->d[0x22] == -1 && g_hint_owner == a->id) {
            g_hint_shown = 0;
            a->sub = 0;
        }
    }
    a->d[0x22] = -1;
}

static void move_14(AnimObj *a)
{
    /* 0x413800: a switch over special d16, on (0) or off (1, 2) to start; d17 / d18 the on / off
       anims. Off, it sends whoever steps on it into state 9 (2: once per 180 ticks). */
    int32_t *d = a->d;
    int sp = d[0x22], who = d[0x21];
    switch (a->sub) {
    case 0:
        memset(g_switch_wait, 0, sizeof g_switch_wait);
        if (d[0x15] == 0) {
            anim_set(a->slot, d[0x18], -1);
            a->sub = 1;
            d[0x22] = -1;
            return;
        }
        if (d[0x15] == 1) goto off;
        if (d[0x15] == 2) {
            anim_set(a->slot, d[0x17], -1);
            a->sub = 3;
            d[0x22] = -1;
            return;
        }
        break;
    case 1:
        special(d[0x16])->active = 1;
        if (sp != -1 && sp != d[0x16]) {
        off:
            anim_set(a->slot, d[0x17], -1);
            a->sub = 2;
            d[0x22] = -1;
            return;
        }
        break;
    case 2:
        special(d[0x16])->active = 0;
        if (sp == -1) break;
        if (sp != d[0x16]) {
            anim_set(a->slot, d[0x18], -1);
            a->sub = 1;
            d[0x22] = -1;
            return;
        }
        if (who < 0x1e) {
            actor_set_state(g_sheep[who].d, 9);
            g_anim[who].d[0x15] = 0;
            d[0x22] = -1;
            return;
        }
        if (g_player.cooldown == 0) {
            player_set_state(&g_player, 9);
            d[0x22] = -1;
            return;
        }
        break;
    case 3:
        special(d[0x16])->active = 0;
        if (sp != -1) {
            if (sp == d[0x16]) {
                if (g_switch_wait[who & 31] == 0) {
                    if (who < 0x1e) {
                        actor_set_state(g_sheep[who].d, 9);
                        g_anim[who].d[0x15] = 1;
                    } else if (g_player.cooldown == 0) {
                        player_set_state(&g_player, 9);
                    }
                    g_switch_wait[who & 31] = 0xb4;
                }
            } else {
                anim_set(a->slot, d[0x18], -1);
                a->sub = 1;
            }
        }
        for (int i = 0; i < 32; i++)
            if (g_switch_wait[i] > 0) g_switch_wait[i]--;
        break;
    }
    d[0x22] = -1;
}

void move_15(AnimObj *a)
{
    /* 0x413a50: keeps its offset from object d15 */
    AnimObj *o = obj_by_id(a->d[0x15]);
    if (!o) return;
    if (a->sub == 0) {
        a->d[0x1d] = a->x - o->x;
        a->sub = 1;
        a->d[0x1e] = a->y - o->y;
    }
    a->x = a->d[0x1d] + o->x;
    a->y = a->d[0x1e] + o->y;
}

static void move_17(AnimObj *a)
{
    /* 0x413c00: the end of the level; d15 why (0 time, 1 flock lost, 2 done) */
    int32_t *d = a->d;
    g_level_over = 1;
    g_timer_on = 0;
    if (g_world == 9) {
        a->rec.scale_x = 0;
    } else {
        if (d[0x1e] == 0) d[0x1e] = anim_new(0, 0, 0x13, -1, 4);
        AnimObj *t = &g_anim[d[0x1e]];
        t->x = g_lv.cam_x + 0x13c;
        t->y = g_lv.cam_y + 0x6c;
        t->rec.mode = 2;
        t->rec.layer = 3;
        t->rec.sortable = 0;
    }
    a->x = g_lv.cam_x + 100;
    a->rec.mode = 2;
    a->y = g_lv.cam_y + 100;
    a->rec.layer = 3;
    a->rec.sortable = 0;
    int t = ++d[0x1d];
    if (t > 300 || g_keyheld[DIK_SPACE] == 1 || g_keyheld[DIK_RETURN] == 1) {
        g_level_over = 0;
        if (d[0x15] == 0) g_end = 3;
        else if (d[0x15] == 1) g_end = 5;
        else if (d[0x15] == 2) g_end = 4;
        if (g_level == 5) g_end = 7;
    }
}

void move_19(AnimObj *a)
{
    /* 0x413e60: breaks after d16 hits; d15 anim */
    int32_t *d = a->d;
    if (a->sub == 0) {
        anim_set(a->slot, d[0x15], -1);
        a->rec.scale_x = 0;
        a->sub = 1;
    } else if (a->sub == 1 && d[0x16] != 0) {
        if (d[0x22] != -1 && --d[0x16] < 1) {
            /* TODO: stop its sound (d42) */
            a->sound = 0;
            anim_kill(a);
        }
    }
    d[0x22] = -1;
}

void anim_stop_sound(AnimObj *a)
{
    /* 0x406650 and the voice list 0x4b9984 (TODO: the mixer) */
    if (!a->sound) return;
    a->sound_on = 0;
    a->sound = 0;
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static int iabs(int v) { return v < 0 ? -v : v; }

static void move_04(AnimObj *a)
{
    /* 0x412140: follows path d16 (mode d1c); whoever comes within d19 (Manhattan) of the point
       (x + d17, y + d18) goes into move type d1a */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x16], d[0x1c], 0);
        a->sub = 1;
    } else if (a->sub == 1) {
        path_follow(a);
        for (int i = 0; i < MAX_SHEEP; i++) {
            Sheep *s = &g_sheep[i];
            if (s->alive == 2) continue;
            int dx = iabs(a->x - ftol((double)s->x) + d[0x17]);
            int dy = iabs(a->y - ftol((double)s->y) + d[0x18]);
            if (dy + dx < d[0x19]) actor_set_state(s->d, d[0x1a]);
        }
        if (g_player.cooldown == 0) {
            int dx = iabs(a->x - g_player.ix + d[0x17]);
            int dy = iabs(a->y - g_player.iy + d[0x18]);
            if (dy + dx < d[0x19]) player_set_state(&g_player, d[0x1a]);
        }
    }
}


static void move_06(AnimObj *a)
{
    /* 0x4123d0: sends whoever touches it along path d15 (move type 0x18), with d16-d19 as its
       parameters */
    int32_t *d = a->d;
    if (d[0x22] == -1) return;
    int who = d[0x21];
    actor_set_state(actor(who), 0x18);
    int32_t *w = g_anim[who].d;
    w[0x23] = d[0x15];
    w[0x24] = 0;
    w[0x25] = 0;
    w[0x15] = d[0x16];
    w[0x16] = d[0x17];
    w[0x17] = d[0x18];
    w[0x18] = d[0x19];
    if (d[0x18] == 1) {
        g_carry_time = 0x78;
        g_carry_obj = a;
    }
    d[0x22] = -1;
}

static void set_frame(AnimObj *o, int frame)
{
    o->countdown = 10;
    o->frame = frame;
    anim_set_frame(o);
}

static void move_08(AnimObj *a)
{
    /* 0x412ad0: a treadmill; a sheep standing within 20 of (d18, d19) is held there and walks away
       from the shepherd, turning the wheel (frame d1d / 8 of 6) and driving objects d1a / d1b's d1c
       up to 0x100; d15-d17 are animated along */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->anim != d[0x1c]) anim_set(a->slot, d[0x1c], -1);
        for (int i = 0; i < MAX_SHEEP; i++) {
            Sheep *s = &g_sheep[i];
            int dx = iabs(ftol((double)s->x) - d[0x18]);
            int dy = iabs(ftol((double)s->y) - d[0x19]);
            if (dy + dx < 0x14 && s->state == 0) {
                s->state = 4;
                d[0x1f] = s->slot;
                d[0x1d] = 0;
                a->sub = 1;
            }
        }
    } else if (a->sub == 1) {
        Sheep *s = &g_sheep[d[0x1f]];
        s->x = (float)d[0x18];
        s->y = (float)d[0x19];
        int sx = ftol((double)s->x), px = ftol((double)g_player.x);
        int sy = ftol((double)s->y), py = ftol((double)g_player.y);
        int dist = iabs(sx - px) + iabs(sy - py);
        int sp = dist < 100 ? 2 : dist < 0x96 ? 1 : 0;
        int pd = dir_of(px - sx, py - sy);
        bool right = pd > 0x1f && pd < 0x61, left = pd > 0x9f && pd < 0xe1;
        if (right) s->face = s->dir = 0xc0;
        if (left) s->face = s->dir = 0x40;
        int moved = 0;
        s->speed = (float)sp;
        AnimObj *o1 = &g_anim[g_obj_by_id[d[0x1a]]];
        if (o1->d[0x1c] != 0x100) {
            if (s->dir == 0x40 && left) {
                moved = sp;
                if ((d[0x1d] += sp) > 0x2f) d[0x1d] -= 0x30;
                if (d[0x1a] != 0 && (o1->d[0x1c] += sp) > 0x100) o1->d[0x1c] = 0x100;
                if (d[0x1b] != 0) {
                    AnimObj *o2 = &g_anim[g_obj_by_id[d[0x1b]]];
                    if ((o2->d[0x1c] += sp) > 0x100) o2->d[0x1c] = 0x100;
                }
            }
            if (s->dir == 0xc0 && right) {
                moved = sp;
                if ((d[0x1d] -= sp) < 0) d[0x1d] += 0x30;
                if (d[0x1a] != 0) {
                    AnimObj *o = &g_anim[g_obj_by_id[d[0x1a]]];
                    if ((o->d[0x1c] -= sp) < 0) o->d[0x1c] = 0;
                }
                if (d[0x1b] != 0) {
                    AnimObj *o = &g_anim[g_obj_by_id[d[0x1b]]];
                    if ((o->d[0x1c] -= sp) < 0) o->d[0x1c] = 0;
                }
            }
        }
        if (d[0x22] != -1) {
            sheep_launch_sound(s->d, 0, 1000, 0x14, 0, 0x19);
            a->sub = 0;
        }
        if (moved == 0 || a->sub == 0) anim_stop_sound(a);
    }
    int f = d[0x1d] / 8;
    set_frame(a, f);
    set_frame(&g_anim[g_obj_by_id[d[0x15]]], 5 - f);
    set_frame(&g_anim[g_obj_by_id[d[0x16]]], f);
    set_frame(&g_anim[g_obj_by_id[d[0x17]]], 5 - f);
    d[0x22] = -1;
}

static void move_09(AnimObj *a)
{
    /* 0x412f20: a ride; whoever touches it goes into move type 0x20 with object d17's d18 / d19,
       which plays its d16 while this plays d15 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        a->sub = 1;
        d[0x1d] = a->anim;
    }
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        actor_set_state(w, 0x20);
        AnimObj *o = &g_anim[g_obj_by_id[d[0x17]]];
        g_anim[w[9]].d[0x15] = o->d[0x18];
        g_anim[w[9]].d[0x16] = o->d[0x19];
        anim_set(a->slot, d[0x15], -1);
        anim_set(o->slot, o->d[0x16], -1);
    }
    if (a->anim != d[0x1d] && a->state == 2) anim_set(a->slot, d[0x1d], -1);
    d[0x22] = -1;
}

static void attract_follow(int on, int n, AnimObj *a)
{
    if (!on) return;
    g_attract[n].x = (int16_t)a->x;
    g_attract[n].y = (int16_t)a->y;
}

void move_10(AnimObj *a)
{
    /* 0x413130: on a trigger, after d1b / 6 s, plays d15 with up to two attractors (as Move_03),
       walks to path d16's start (modes 0 and 2) and follows it in mode d1c; d20 = 1 at its end */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        if (d[0x22] != -1) {
            a->sub = 1;
            d[0x1b] *= 6;
        }
        break;
    case 1:
        if (d[0x1b]-- < 1) {
            if (d[0x17]) d[0x1d] = attract_new(a->x, a->y, d[0x17] / 10, d[0x18], d[0x17] % 10);
            if (d[0x19]) d[0x1e] = attract_new(a->x, a->y, d[0x19] / 10, d[0x1a], d[0x19] % 10);
            anim_set(a->slot, d[0x15], -1);
            a->sub = 2;
            a->fx = (float)a->x;
            a->fy = (float)a->y;
        }
        break;
    case 2:
        if (d[0x1c] == 0 || d[0x1c] == 2) {
            path_start(d[0x16]);
            int dir = dir16_toward(a->x, a->y, g_path_x, g_path_y);
            a->dir = dir;
            a->fx = (float)((double)g_sin[dir * 16] + g_sin[dir * 16] + a->fx);
            a->fy = (float)(a->fy - ((double)g_cos_y[dir * 16] + g_cos_y[dir * 16]));
            a->x = ftol((double)a->fx);
            a->y = ftol((double)a->fy);
            if (iabs(a->y - g_path_y) + iabs(a->x - g_path_x) < 4) a->sub = 3;
        } else {
            a->sub = 3;
        }
        break;
    case 3:
        a->d[0x23] = d[0x16];
        a->d[0x24] = 0;
        a->d[0x25] = 0;
        a->d[0x26] = d[0x1c];
        a->sub = 4;
        break;
    case 4:
        d[0x20] = 0;
        if (path_follow(a)) d[0x20] = 1;
        break;
    }
    if (a->sub > 1) {
        attract_follow(d[0x17], d[0x1d], a);
        attract_follow(d[0x19], d[0x1e], a);
    }
}

static void relay(AnimObj *a, int32_t v)
{
    if (a->d[0x17]) trigger(a->d[0x17], v);
    if (a->d[0x18]) trigger(a->d[0x18], v);
    if (a->d[0x19]) trigger(a->d[0x19], v);
}

static void move_11(AnimObj *a)
{
    /* 0x413390: a two-way lever; each trigger plays d15 (then d16), passes the trigger on to
       d17-d19 once past frame 1 and waits d1a / 6 s */
    int32_t *d = a->d;
    int32_t t = d[0x22];
    switch (a->sub) {
    case 0:
        if (t != -1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 1;
        }
        break;
    case 1:
    case 4:
        if (a->frame > 1) {
            relay(a, t);
            a->sub = a->sub == 1 ? 2 : 5;
            d[0x1c] = d[0x1a] * 6;
        }
        break;
    case 2:
    case 5:
        if (d[0x1c]-- < 1) {
            d[0x22] = -1;
            a->sub = a->sub == 2 ? 3 : 0;
        }
        break;
    case 3:
        if (t != -1) {
            anim_set(a->slot, d[0x16], -1);
            a->sub = 4;
        }
        break;
    }
}

static void move_12(AnimObj *a)
{
    /* 0x413550: follows path d17 (mode d1c) with an attractor (d18 = type * 10 + power, range d19);
       each trigger swaps anims d16 / d15 and the attractor for d1a / d1b and back */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        a->d[0x26] = d[0x1c];
        a->d[0x23] = d[0x17];
        a->d[0x24] = 0;
        a->d[0x25] = 0;
        if (d[0x18]) d[0x1d] = attract_new(a->x, a->y, d[0x18] / 10, d[0x19], d[0x18] % 10);
        a->sub = 1;
        break;
    case 1:
        path_follow(a);
        attract_follow(d[0x18], d[0x1d], a);
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x16], -1);
            if (d[0x18]) {
                int16_t v = (int16_t)d[0x1a];
                g_attract[d[0x1d]].type = (int16_t)(v / 10);
                g_attract[d[0x1d]].power = (int16_t)(v % 10);
                g_attract[d[0x1d]].range = (int16_t)d[0x1b];
            }
            d[0x22] = -1;
            a->sub = 2;
        }
        break;
    case 2:
        path_follow(a);
        attract_follow(d[0x18], d[0x1d], a);
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x15], -1);
            /* without the d18 test of sub-state 1 */
            g_attract[d[0x1d]].type = (int16_t)(d[0x18] / 10);
            g_attract[d[0x1d]].power = (int16_t)(d[0x18] % 10);
            g_attract[d[0x1d]].range = (int16_t)d[0x19];
            d[0x22] = -1;
            a->sub = 1;
        }
        break;
    }
}

static void move_16(AnimObj *a)
{
    /* 0x413ab0: patrols path d15 (mode d1c), d16 / 6 s waiting and d17 / 6 s moving; a sheep within
       d18 of it ends that: anim d19, d1a points and a sparkle */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        path_begin(a, d[0x15], d[0x1c], 0);
        a->sub = 1;
        d[0x1d] = d[0x16] * 6;
        break;
    case 1:
        if (d[0x1d]-- < 1) {
            a->sub = 2;
            d[0x1d] = d[0x17] * 6;
        }
        break;
    case 2:
        path_follow(a);
        if (d[0x1d]-- < 1) {
            a->sub = 1;
            d[0x1d] = d[0x16] * 6;
        }
        break;
    }
    if (a->sub != 1 && a->sub != 2) return;
    for (int i = 0; i < MAX_SHEEP; i++) {
        Sheep *s = &g_sheep[i];
        if (s->alive == 2) continue;
        int dx = iabs(a->x - ftol((double)s->x)), dy = iabs(a->y - ftol((double)s->y));
        if (dy + dx < d[0x18]) {
            anim_set(a->slot, d[0x19], -1);
            g_score += d[0x1a];
            AnimObj *o = &g_anim[anim_new(a->x, a->y, 9, 8, 4)];
            o->rec.mode = 2;
            o->rec.layer = 0;
            o->rec.sortable = 0;
            a->sub = 3;
        }
    }
}

static void move_18(AnimObj *a)
{
    /* 0x413d60: a one-shot trigger; plays d15, follows path d19 (-1: none) once and, past frame 1,
       triggers d16-d18 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        uint32_t t = (uint32_t)d[0x22];
        if (t == 0xffffffffu) return;
        if (!(t & 0x8000)) special((int)t)->active = 1;
        anim_set(a->slot, d[0x15], -1);
        if (d[0x19] != -1) path_begin(a, d[0x19], 1, 0);
    } else {
        if (a->sub != 1) return;
        a->sub = 2;
        if (d[0x19] != -1) {
            if (!path_follow(a)) a->sub = 1;
            else d[0x19] = -1;
        }
        if (a->frame > 1) {
            if (d[0x16]) trigger(d[0x16], 0x8000);
            if (d[0x17]) trigger(d[0x17], 0x8000);
            if (d[0x18]) trigger(d[0x18], 0x8000);
            return;
        }
    }
    a->sub = 1;
}

/* 0x423800, 0x4237b0, 0x423790: the doubles of Move_20 */
static const double VEC_EPS = 9.99999993922529e-09;   /* 0x448390 */

static void vnorm(double r[2], double x, double y)
{
    double l = y * y + x * x;
    if ((l <= -VEC_EPS || VEC_EPS <= l) && ((l = sqrt(l)) <= -VEC_EPS || VEC_EPS <= l)) {
        r[0] = x / l;
        r[1] = y / l;
    } else {
        r[0] = r[1] = 0;
    }
}

static double vlen(double x, double y)
{
    double l = y * y + x * x;
    return -VEC_EPS < l && l < VEC_EPS ? 0.0 : sqrt(l);
}

static double vdot(const double u[2], const double v[2]) { return u[0] * v[0] + u[1] * v[1]; }

void move_20(AnimObj *a)
{
    /* 0x413fa0: keeps level with the shepherd along the line through path d16's first two points,
       within them by x; otherwise Move_19 */
    int x0, y0, x1, y1;
    path_point(a->d[0x16], 0, &x0, &y0);
    path_point(a->d[0x16], 1, &x1, &y1);
    double u[2], n[2], vh[2];
    vnorm(u, (double)x1 - x0, (double)y1 - y0);
    vnorm(n, -u[1], u[0]);
    double vx = (double)g_player.x - x0, vy = (double)g_player.y - y0;
    vnorm(vh, vx, vy);
    if (vdot(n, vh) > 0.0) {
        n[0] *= -1.0;
        n[1] *= -1.0;
    }
    double s = sin(acos(vdot(u, vh)));
    double l = vlen(vx, vy);
    double qx = (double)g_player.x + l * s * n[0], qy = (double)g_player.y + l * s * n[1];
    if (qx <= x0) { qx = x0; qy = y0; }
    if (x1 <= qx) { qx = x1; qy = y1; }
    a->x = ftol(qx);
    a->y = ftol(qy);
    move_19(a);
}

static void move_21(AnimObj *a)
{
    /* 0x414360: a spawner; on frames d1a..d1b it starts an object with anim d19 and move d18 at
       path d17's start, with object d16's parameters; when its animation ends it waits d1c / 6 s
       and replays it */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
    case 1:
        a->sub = 2;
        /* fall through */
    case 2:
        if (d[0x1a] <= a->frame && a->frame <= d[0x1b] && a->frame != a->prev_frame) {
            AnimObj *src = &g_anim[g_obj_by_id[d[0x16]]];
            path_start(d[0x17]);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, d[0x19], -1, 3)];
            o->move = d[0x18];
            memcpy(&o->d[0x15], &src->d[0x15], 8 * sizeof(int32_t));
            o->rec.mode = src->rec.mode;
            o->rec.layer = src->rec.layer;
        }
        if (a->state == 2) {
            a->sub = 3;
            d[0x1d] = d[0x1c] * 6;
        }
        break;
    case 3:
        if (d[0x1d]-- < 1) {
            anim_set(a->slot, a->anim, -1);
            a->sub = 2;
        }
        break;
    }
}

static void move_22(AnimObj *a)
{
    /* 0x413f20: hidden until triggered, then plays d15 for d16 / 6 s */
    int32_t *d = a->d;
    if (a->sub == 0) {
        a->rec.scale_x = 0;
        a->sub = 1;
    } else if (a->sub == 1) {
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x15], -1);
            a->sub = 2;
            d[0x1d] = d[0x16] * 6;
        }
    } else if (a->sub == 2) {
        if (--d[0x1d] < 1) {
            d[0x22] = -1;
            a->sub = 1;
        }
    }
}

static void move_23(AnimObj *a)
{
    /* 0x414480: a teleport to path d15's start (move type 8); the shepherd also gets the camera
       bounds d16-d19 (d16 = -1: the level's) */
    int32_t *d = a->d;
    if (d[0x22] == -1) return;
    int32_t *w = actor(d[0x21]);
    if (w[9] > 0x1e) {
        if (d[0x16] == -1) {
            g_lv.cam_min_x = (int32_t)rd32(g_lv.blo + 0x28);
            g_lv.cam_max_x = (int32_t)rd32(g_lv.blo + 0x2c);
            g_lv.cam_min_y = (int32_t)rd32(g_lv.blo + 0x30);
            g_lv.cam_max_y = (int32_t)rd32(g_lv.blo + 0x34);
        } else {
            g_lv.cam_min_x = d[0x16];
            g_lv.cam_max_x = d[0x17];
            g_lv.cam_min_y = d[0x18];
            g_lv.cam_max_y = d[0x19];
        }
        g_scroll_reset = 1;
        g_player.in_dx = g_player.in_dy = g_player.in_mag = 0;
        g_player.speed = 0;
        g_mouse_dx = g_mouse_dy = 0;
    }
    path_start(d[0x15]);
    ((float *)w)[10] = (float)g_path_x;
    ((float *)w)[11] = (float)g_path_y;
    g_anim[w[9]].d[0x23] = d[0x15];
    g_anim[w[9]].d[0x15] = 0;
    if (w[9] > 0x1e) player_set_state((Player *)w, 8);
    else actor_set_state(w, 8);
    d[0x22] = -1;
}

static void move_24(AnimObj *a)
{
    /* 0x4145c0: switches to anim d15 once, keeping the frame timer */
    int32_t c = a->countdown;
    anim_set(a->slot, a->d[0x15], -1);
    a->countdown = c;
    a->move = 0;
}

static void move_25(AnimObj *a)
{
    /* 0x4145f0: a hint sign showing anim d16, d17 while hint d15 is up */
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
        if (d[0x22] == -1 && g_hint_owner == a->id) {
            g_hint_shown = 0;
            a->sub = 0;
        }
    }
    d[0x22] = -1;
}

static void move_26(AnimObj *a)
{
    /* 0x4146f0: plays d15 on a trigger, back to its own anim after d16 / 10 s or (d16 = -1) at the
       animation's end */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        a->sub = 1;
        d[0x1d] = a->anim;
        break;
    case 1:
        if (d[0x22] != -1) {
            anim_set(a->slot, d[0x15], -1);
            if (d[0x16] != -1) {
                a->sub = 3;
                d[0x1e] = d[0x16] * 10;
            } else {
                a->sub = 2;
            }
        }
        break;
    case 2:
        if (a->state == 2) {
            anim_set(a->slot, d[0x1d], -1);
            a->sub = 1;
        }
        break;
    case 3:
        if (d[0x1e]-- < 1) {
            anim_set(a->slot, d[0x1d], -1);
            a->sub = 1;
        }
        break;
    }
    d[0x22] = -1;
}

static void move_27(AnimObj *a)
{
    /* 0x4147c0: on a trigger, tile animation d15 gets mode d16 and hold frame d17 */
    int32_t *d = a->d;
    if (d[0x22] == -1) return;
    g_tile_anim[d[0x15]].mode = (int16_t)d[0x16];
    g_tile_anim[d[0x15]].hold = (int16_t)d[0x17];
    d[0x22] = -1;
}

static void move_28(AnimObj *a)
{
    /* 0x413000: Move_09 with a switch (d1a: 0 on, 1 off showing d16) and a sound (d1b); the rider
       also gets object d17's d1b */
    int32_t *d = a->d;
    if (d[0x1a] != 0) {
        if (d[0x1a] == 1 && a->anim != d[0x16]) anim_set(a->slot, d[0x16], -1);
        d[0x22] = -1;
        return;
    }
    if (a->anim != d[0x15]) anim_set(a->slot, d[0x15], -1);
    if (d[0x22] != -1) {
        int32_t *w = actor(d[0x21]);
        actor_set_state(w, 0x20);
        if (d[0x1b] != 0) sound_play_at(a, d[0x1b], 0, 0);
        AnimObj *o = &g_anim[g_obj_by_id[d[0x17]]];
        int32_t *r = g_anim[w[9]].d;
        r[0x15] = o->d[0x18];
        r[0x16] = o->d[0x19];
        r[0x17] = o->d[0x1b] != 0 ? o->d[0x1b] : 0;
    }
    d[0x22] = -1;
}

static void move_29(AnimObj *a)
{
    /* 0x414800: a creep-button switch; touched while creeping it toggles an attractor at
       (x + d1a, y + d1b) (d18 = type * 10 + power, range d19), showing d15 off and d16 on, at most
       every 0x78 ticks; d17 a hint (-1: none) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        if (a->anim != d[0x15]) anim_set(a->slot, d[0x15], -1);
        if (d[0x1e] != 0) {
            d[0x1e]--;
        } else if (d[0x22] != -1 && g_creep) {
            d[0x1d] = attract_new(d[0x1a] + a->x, d[0x1b] + a->y, d[0x18] / 10, d[0x19], d[0x18] % 10);
            d[0x1e] = 0x78;
            a->sub = 1;
        }
    } else if (a->sub == 1) {
        if (a->anim != d[0x16]) anim_set(a->slot, d[0x16], -1);
        if (d[0x1e] != 0) {
            d[0x1e]--;
        } else if (d[0x22] != -1 && g_creep) {
            g_attract[d[0x1d]].type = 0;
            d[0x1e] = 0x78;
            a->sub = 0;
        }
    }
    if (d[0x17] != -1) {
        if (d[0x22] != -1) {
            if (g_hint_owner == 0) sound_play_at(a, 0x60, 0, 0);
            hint_show(d[0x17]);
            g_hint_y = 100;
            g_hint_x = 100;
            g_hint_owner = a->id;
        } else if (g_hint_owner == a->id) {
            g_hint_shown = 0;
        }
    }
    d[0x22] = -1;
}

static void move_30(AnimObj *a)
{
    /* 0x414970: follows path d15 from its start, repelling (type 6, range 0x18) from frame d16 to
       frame d17 */
    int32_t *d = a->d;
    if (a->sub == 0) {
        path_begin(a, d[0x15], 1, 1);
        a->sub = 1;
    } else if (a->sub == 1) {
        path_follow(a);
        if (a->frame == d[0x16]) {
            d[0x1e] = attract_new(a->x, a->y, 6, 0x18, 0);
            a->sub = 2;
        }
    } else if (a->sub == 2 && a->frame == d[0x17]) {
        g_attract[d[0x1e]].type = 0;
        a->sub = 1;
    }
}

static void move_31(AnimObj *a)
{
    /* 0x414a00: rides on actor d15 until its +0x58 timer runs out, then puts it into move type 0x1b
       and goes */
    int n = a->d[0x15];
    int32_t *w = actor(n);
    AnimObj *o = &g_anim[n];
    a->rec.mode = o->rec.mode;
    a->rec.sortable = o->rec.sortable;
    a->rec.layer = o->rec.layer;
    a->x = w[0xc];
    a->depth_off = 0x14;
    a->y = w[0xd] + 10;
    if (--w[0x16] < 1) {
        if (w[9] < 0x1e) actor_set_state(w, 0x1b);
        else player_set_state((Player *)w, 0x1b);
        anim_kill(a);
    }
}

static void move_nop(AnimObj *a) { (void)a; }   /* 0x4039f0 */

static const struct { uint32_t va; MoveFn fn; } standard[] = {
    { 0x4039f0, move_nop }, { 0x411dd0, move_01 }, { 0x411ef0, move_02 }, { 0x411fe0, move_03 },
    { 0x412140, move_04 }, { 0x412270, move_05 }, { 0x4123d0, move_06 }, { 0x412480, move_07 },
    { 0x412ad0, move_08 }, { 0x412f20, move_09 }, { 0x413130, move_10 }, { 0x413390, move_11 },
    { 0x413550, move_12 }, { 0x413750, move_13 }, { 0x413800, move_14 }, { 0x413a50, move_15 },
    { 0x413ab0, move_16 }, { 0x413c00, move_17 }, { 0x413d60, move_18 }, { 0x413e60, move_19 },
    { 0x413fa0, move_20 }, { 0x414360, move_21 }, { 0x413f20, move_22 }, { 0x414480, move_23 },
    { 0x4145c0, move_24 }, { 0x4145f0, move_25 }, { 0x4146f0, move_26 }, { 0x4147c0, move_27 },
    { 0x413000, move_28 }, { 0x414800, move_29 }, { 0x414970, move_30 }, { 0x414a00, move_31 },
};

static MoveFn standard_fn(uint32_t va)
{
    for (size_t i = 0; i < sizeof standard / sizeof standard[0]; i++)
        if (standard[i].va == va) return standard[i].fn;
    return NULL;
}

static void unported(int m, uint32_t va)
{
    static uint8_t warned[0x10000 / 8];
    if (warned[m >> 3] >> (m & 7) & 1) return;
    warned[m >> 3] |= (uint8_t)(1 << (m & 7));
    fprintf(stderr, "move routine %s %d (0x%x) not ported\n", m & 0x8000 ? "specific" : "standard", m & 0x7fff, va);
}

void move_run(AnimObj *a)
{
    uint32_t m = (uint32_t)a->move;
    if (!(m & 0x8000)) {
        if (!m) return;
        /* the table's length: entries up to the first null */
        int n = 0;
        while (exe_u32(STD_MOVES + 4u * (uint32_t)(n + 1))) n++;
        if ((int)m > n) {
            fprintf(stderr, "Standard move routine out of range %u\n", m);
            return;
        }
        uint32_t va = exe_u32(STD_MOVES + 4 * m);
        MoveFn f = standard_fn(va);
        if (f) f(a);
        else unported((int)m, va);
    } else {
        m &= 0x7fff;
        uint32_t t = desc_u32(DESC_MOVES);
        int n = 0;
        if (t) while (exe_u32(t + 4u * (uint32_t)n)) n++;
        if ((int)m >= n) {
            fprintf(stderr, "Specific move routine out of range %u\n", m);
            return;
        }
        uint32_t va = exe_u32(t + 4 * m);
        MoveFn f = level_move(va);
        if (!f) f = standard_fn(va);    /* some levels list standard routines (4.x: 0x413fa0) */
        if (f) f(a);
        else unported((int)(m | 0x8000), va);
    }
}
