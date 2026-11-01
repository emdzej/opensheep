#include "levels.h"
#include "level.h"
#include "moves.h"
#include "paths.h"
#include "player.h"
#include "sheep.h"
#include "sound.h"
#include "world.h"
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
    case 0x42b5d0: return lv11_move_06;
    case 0x42b6c0: return lv11_move_07;
    default: return NULL;
    }
}
