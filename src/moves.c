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
#include <stdio.h>
#include <string.h>

int g_pickups, g_score_flash, g_timer_on;
uint8_t g_bonus;
int g_hint_owner, g_hint_shown, g_hint_x, g_hint_y, g_hint;
int g_switch_wait[32];

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

static void move_02(AnimObj *a)
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

static void move_03(AnimObj *a)
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

static void move_05(AnimObj *a)
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

static void move_15(AnimObj *a)
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

static void move_19(AnimObj *a)
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

static void move_nop(AnimObj *a) { (void)a; }   /* 0x4039f0 */

static const struct { uint32_t va; MoveFn fn; } standard[] = {
    { 0x4039f0, move_nop }, { 0x411dd0, move_01 }, { 0x411ef0, move_02 }, { 0x411fe0, move_03 },
    { 0x412270, move_05 }, { 0x412480, move_07 }, { 0x413750, move_13 }, { 0x413800, move_14 },
    { 0x413a50, move_15 }, { 0x413c00, move_17 }, { 0x413e60, move_19 },
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
        MoveFn f = va == 0x4039f0 ? move_nop : level_move(va);
        if (f) f(a);
        else unported((int)(m | 0x8000), va);
    }
}
