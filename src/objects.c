#include "objects.h"
#include "game.h"
#include "moves.h"
#include "level.h"
#include "render.h"
#include "rng.h"
#include "sprites.h"
#include "world.h"
#include "xp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(DrawRec) == 0x58, "DrawRec layout");
_Static_assert(sizeof(StaticObj) == 0x78, "StaticObj layout");
_Static_assert(sizeof(AnimObj) == 0x10c, "AnimObj layout");

StaticObj g_static[MAX_STATIC];
AnimObj g_anim[MAX_ANIM];
int g_static_hi, g_anim_hi;
int g_obj_by_id[MAX_OBJ_ID];
int g_loading;
int g_static_count, g_anim_count;       /* 0x49f890, 0x49f99c */

static uint8_t *anm[4];                 /* 0x49f880 player, 0x49f884 sheep, 0x49f888 common, 0x49f88c level */
static size_t anm_size[4];
static uint8_t *pos_buf;                /* 0x47db5c */
static size_t pos_len;

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

/* ------------------------------------------------------------------ .ANM files */

void anims_free(void)
{
    for (int i = 0; i < 4; i++) { free(anm[i]); anm[i] = NULL; anm_size[i] = 0; }
}

static void anim_file(int i, const char *path)
{
    free(anm[i]);
    anm[i] = file_load(path, &anm_size[i]);
    if (!anm[i]) {
        /* A missing file becomes an empty 64 KB buffer (no animations). */
        fprintf(stderr, "Warning: Cannot load %s\n", path);
        anm[i] = calloc(1, 0x10000);
        anm_size[i] = anm[i] ? 0x10000 : 0;
    }
}

void anims_load(const char *level_anm_path)
{
    /* Level_LoadAnims 0x401240 */
    anim_file(ANM_PLAYER, "Data\\Players\\Player.anm");
    anim_file(ANM_SHEEP, "Data\\Sheep\\Sheep.anm");
    anim_file(ANM_COMMON, "Data\\GUI\\Pickup.anm");
    anim_file(ANM_LEVEL, level_anm_path);
}

/* Bounds-checked view of an .ANM file (the original trusts the data). */
static const uint8_t *anm_at(int f, int32_t off, size_t n)
{
    if (!anm[f] || off < 0 || (size_t)off + n > anm_size[f]) return NULL;
    return anm[f] + off;
}

static int anm_count(int f) { const uint8_t *p = anm_at(f, 0, 4); return p ? rd32(p) : 0; }

/* Animation header (nframes, ?, frame offsets) of animation a. */
static const uint8_t *anm_anim(int f, int a)
{
    const uint8_t *o = anm_at(f, 4 * (a + 2), 4);
    return o ? anm_at(f, rd32(o), 8) : NULL;
}

static const uint8_t *anm_frame(int f, const uint8_t *anim, int frame)
{
    const uint8_t *o = anim ? anm_at(f, (int32_t)(anim - anm[f]) + 4 * (frame + 2), 4) : NULL;
    return o ? anm_at(f, rd32(o), 12) : NULL;
}

/* ------------------------------------------------------------------ draw records */

void rec_set_sprite(DrawRec *r, int bank, uint32_t frame)
{
    /* 0x421b10 */
    int f = (int)(frame & 0x7fffffff);
    if (!sprite_frames(bank)) { bank = 0; f = 0; }
    if (f >= sprite_frames(bank)) {
        fprintf(stderr, "GetSpriteInfo: Sprite out of range %d (bank %d)\n", f, bank);
        f = 0;
    }
    if (f >= sprite_frames(bank)) { frame = 0; f = 0; }
    r->frame = frame;                    /* the frame as asked for, even when it was out of range */
    r->bank = bank;
    SprFrame sf;
    if (sprite_frame(bank, f, &sf)) {
        r->w = sf.w; r->h = sf.h; r->hot_x = sf.hot_x; r->hot_y = sf.hot_y;
    } else {
        r->w = r->h = r->hot_x = r->hot_y = 0;   /* bank 0 not loaded: the original reads a NULL bank */
    }
}

/* 0x402f10: what a bank's frame is (all zero if missing). */
typedef struct { int bank, frame, nframes, group, index, w, h, hot_x, hot_y; } SprInfo;

static SprInfo sprite_info(int bank, int frame)
{
    SprInfo s = { 0 };
    SprFrame f;
    if (frame >= sprite_frames(bank) || !sprite_frame(bank, frame, &f)) return s;
    s.bank = bank;
    s.frame = frame;
    s.nframes = sprite_frames(bank);
    sprite_frame_ids(bank, frame, &s.group, &s.index);
    s.w = f.w; s.h = f.h; s.hot_x = f.hot_x; s.hot_y = f.hot_y;
    return s;
}

/* ------------------------------------------------------------------ static objects */

static void static_set_frame(int i)
{
    /* 0x401d40: frame 0 of the object's level animation, by direction */
    StaticObj *s = &g_static[i];
    int bank = 0;
    uint32_t frame = 0;
    if (s->anim < anm_count(ANM_LEVEL)) {
        const uint8_t *c = anm_frame(ANM_LEVEL, anm_anim(ANM_LEVEL, s->anim), 0);
        int d = s->dir;
        if (d < 9) s->rec.flip = 0;
        else { s->rec.flip = 1; d = 16 - d; }
        if (c && d >= 0 && d < 9) {
            if (c[0] == 0 || c[0] == 1) { bank = c[1]; frame = c[d + 2]; }
            else if (c[0] == 4) { bank = c[1] + 0x100; frame = c[d + 2]; }
        }
    }
    rec_set_sprite(&s->rec, bank, frame);
}

int static_new(int x, int y, int anim, int dir)
{
    /* 0x402220 */
    if (dir == -1) dir = sprite_info(anim, 0).group & 0xf;   /* the anim number taken as a bank */
    int i = FIRST_OBJ;
    while (i < MAX_STATIC && g_static[i].state != -1) i++;
    if (i >= MAX_STATIC) {
        fprintf(stderr, "Init New Anim: Anim number too high %d\n", i);
        return MAX_STATIC - 1;
    }
    StaticObj *s = &g_static[i];
    s->state = 2;
    s->anim = anim;
    s->dir = dir;
    s->rec.kind = 5;
    s->x = x;
    s->y = y;
    s->rec.depth = 0;
    s->rec.scale_x = s->rec.scale_y = 0x100;
    s->rec.shadow = -1;
    s->rec.shadow_dy = 0;
    s->rec.tint = 0;
    static_set_frame(i);
    s->rec.clip_x0 = -1;
    if (g_static_hi <= i) g_static_hi = i + 1;
    return i;
}

/* ------------------------------------------------------------------ animated objects */

void anim_set_frame(AnimObj *a)
{
    /* 0x401e90: run the current frame's commands */
    int f;
    switch (a->type) {
    case 0: f = ANM_PLAYER; break;
    case 1: f = ANM_SHEEP; break;
    case 2: case 3: f = ANM_LEVEL; break;
    case 4: f = ANM_COMMON; break;
    default:
        if (a->type != -1) fprintf(stderr, "GetAnimFrame: AnimType is wrong %d\n", a->type);
        a->pause = 1;
        rec_set_sprite(&a->rec, 0, 0);
        return;
    }
    const uint8_t *hdr = anm_anim(f, a->anim);
    int n = hdr ? rd32(hdr) : 0;
    a->nframes = n;
    if (n <= a->frame) a->frame = n < 1 ? 0 : n - 1;
    const uint8_t *c = anm_frame(f, hdr, a->frame);
    int d = a->dir;
    if (d < 9) a->rec.flip = 0;
    else { a->rec.flip = 1; d = 16 - d; }
    if (g_theme == 5 && g_level == 5 && (a->anim == 0x4d || a->anim == 0x66)) a->rec.flip |= a->mirror;
    int bank = 0;
    uint32_t frame = 0;
    const uint8_t *end = anm[f] + anm_size[f];
    for (bool done = false; c && !done;) {
        if (c >= end) break;
        switch (c[0]) {
        case 0:
            done = true;
            c += 1;
            break;
        case 1:
        case 4:
            if (end - c < 12) { done = true; break; }
            bank = c[1] + (c[0] == 4 ? 0x100 : 0);
            frame = (d >= 0 && d < 9 ? c[2 + d] : 0) | (uint32_t)a->rec.flip << 31;
            a->pause = c[11] + 1;
            c += 12;
            break;
        case 2:
            if (end - c < 2) { done = true; break; }
            a->loop = c[1];
            c += 2;
            break;
        case 3:
            /* Sound: 0 start a looping sound while on screen, 1 play sound c[5..6], 2 play one of c[2]
               sounds from c[5..6] (picked with rand()). Not while loading. TODO: sound. */
            if (end - c < 9) { done = true; break; }
            if (!g_loading && c[1] == 2) (void)crt_rand();
            c += 9;
            break;
        default:
            c += 1;
            break;
        }
        if (a->pause < 1) {
            fprintf(stderr, "Anim pause value is wrong %d\n", a->pause);
            a->pause = 10;
        }
    }
    rec_set_sprite(&a->rec, bank, frame);
}

int anim_new(int x, int y, int anim, int dir, int type)
{
    /* 0x402330 */
    int i = FIRST_OBJ;
    while (i < MAX_ANIM && g_anim[i].type != -1) i++;
    if (i >= MAX_ANIM) {
        fprintf(stderr, "anim_new: no free slot\n");   /* the original runs past the array */
        return MAX_ANIM - 1;
    }
    AnimObj *a = &g_anim[i];
    memset(a, 0, sizeof *a);
    a->anim = anim;
    a->type = type;
    a->slot = i;
    a->rec.kind = 4;
    a->x = x;
    a->y = y;
    int saved = g_loading;
    if (dir == -1) {
        g_loading = 1;
        anim_set_frame(a);
        dir = sprite_info(a->rec.bank, 0).group & 0xf;
    }
    g_loading = saved;
    a->dir = dir;
    a->state = 1;
    a->fx = (float)x;
    a->d[0x22] = -1;                     /* +0x88: not triggered */
    a->rec.scale_x = a->rec.scale_y = 0x100;
    a->rec.shadow = -1;
    a->fy = (float)y;
    a->rec.mode = 1;
    a->rec.layer = 1;
    a->rec.sortable = 1;
    anim_set_frame(a);
    a->countdown = a->pause;
    a->rec.clip_x0 = -1;
    a->rec.clip_x1 = 8000;
    a->rec.clip_y0 = 0;
    a->rec.clip_y1 = 8000;
    a->ua0[0] = 0x8000;                  /* +0xa0 */
    if (g_anim_hi <= i) g_anim_hi = i + 1;
    return i;
}

void anim_kill(AnimObj *a)
{
    /* 0x4025c0 (TODO: stop its sound) */
    if (a->id) g_obj_by_id[a->id] = -1;
    a->sound_on = 0;
    a->sound = 0;
    a->type = -1;
}

void anim_init(int type, int slot, int anim, int dir)
{
    /* 0x402490: the shepherd's (type 3) or a sheep's (type 2) animated object */
    AnimObj *a = &g_anim[slot];
    if (type == 3) { a->type = ANM_PLAYER; a->rec.shadow = 2; }
    else { a->type = ANM_SHEEP; a->rec.shadow = 3; }
    a->rec.kind = type;
    a->rec.shadow_dy = 0;
    a->anim = anim;
    a->dir = dir & 0xf;
    a->frame = 0;
    a->pause = 0;
    a->state = 1;
    a->slot = slot;
    a->depth_off = 0;
    a->rec.depth = 0;
    a->rec.tint = 0;
    anim_set_frame(a);
    a->countdown = a->pause;
    a->sub = 0;
    a->rec.clip_x0 = -1;
    a->rec.scale_x = a->rec.scale_y = 0x100;
    a->rec.sortable = 1;
    a->rec.push = 0;
    a->rec.layer = 1;
    a->rec.mode = 1;
}

void anim_set(int slot, int anim, int dir)
{
    /* 0x402560 */
    AnimObj *a = &g_anim[slot];
    a->anim = anim;
    if (dir != -1) a->dir = dir;
    a->frame = 0;
    a->pause = 0;
    a->state = 1;
    anim_set_frame(a);
    a->countdown = a->pause;
}

void anim_step(int slot, int dir, int step)
{
    /* 0x402630: advance by step ticks, facing dir (-1: keep) */
    AnimObj *a = &g_anim[slot];
    if (dir != -1 && a->dir != dir) {
        a->dir = dir;
        a->rec.frame = 0xffffffffu;
    }
    if (a->state == 0) return;
    if (a->state != 2) {
        int c = a->countdown - step;
        a->prev_frame = a->frame;
        a->countdown = c;
        while (c < 1) {
            int f = a->frame;
            a->frame = f + 1;
            if (a->nframes <= f + 1) {
                if (a->loop == 0) { a->state = 2; a->frame = f; }
                else if (a->loop == 1) anim_kill(a);
                else if (a->loop == 2) a->frame = 0;
            }
            anim_set_frame(a);
            c = a->countdown + a->pause;
            a->countdown = c;
        }
    }
    if (a->rec.frame == 0xffffffffu) anim_set_frame(a);
}

/* ------------------------------------------------------------------ loading */

void objects_load(const char *pos_path)
{
    /* 0x402a90 */
    free(pos_buf);
    pos_buf = NULL;
    pos_len = 0;
    memset(g_static, 0, sizeof g_static);
    memset(g_anim, 0, sizeof g_anim);
    for (int i = 0; i < MAX_STATIC; i++) g_static[i].state = -1;
    for (int i = 0; i < MAX_ANIM; i++) g_anim[i].type = -1;
    g_static_count = g_anim_count = 0;
    for (int i = 0; i < MAX_OBJ_ID; i++) g_obj_by_id[i] = -1;
    /* Table B from entry 31 on: {i32 x, y; i16 sprite, depth; u8 mirror, sortable, layer, mode;
       i16 id; u8 push; u8} (the first 31 entries are not objects). */
    int nb = g_lv.nobjects;
    for (int k = 31; k < nb; k++) {
        const uint8_t *e = g_lv.objects + 20 * k;
        int32_t ex = rd32(e), ey = rd32(e + 4);
        int16_t spr, depth, id;
        memcpy(&spr, e + 8, 2);
        memcpy(&depth, e + 10, 2);
        memcpy(&id, e + 16, 2);
        int8_t mirror = (int8_t)e[12];
        SprInfo info = sprite_info(spr + SPR_BANK_LEVEL, 0);
        int oid = id + 40;
        if (info.nframes < 2) {
            int i = static_new(mirror ? ex + info.w : ex, ey, spr + 40, -1);
            StaticObj *s = &g_static[i];
            s->mirror = mirror;
            s->rec.sortable = e[13];
            s->rec.push = (int8_t)e[18];
            s->rec.mode = (int8_t)e[15];
            s->rec.layer = (int8_t)e[14];
            s->depth_off = depth;
            s->id = oid;
            if (oid > MAX_OBJ_ID || oid < 0) { fprintf(stderr, "Object ID number has exceeded MAX %d\n", oid); continue; }
            g_static_count++;
            if (oid < MAX_OBJ_ID) g_obj_by_id[oid] = i | 0x8000;
        } else {
            int i = anim_new(mirror ? ex + info.w : ex, ey, spr + 40, -1, 3);
            AnimObj *a = &g_anim[i];
            a->x += mirror ? info.hot_x : -info.hot_x;
            a->y -= info.hot_y;
            a->mirror = mirror;
            a->rec.sortable = e[13];
            a->rec.push = (int8_t)e[18];
            a->rec.mode = (int8_t)e[15];
            a->rec.layer = (int8_t)e[14];
            a->depth_off = depth;
            a->state = 1;
            a->id = oid;
            if (oid > MAX_OBJ_ID) { fprintf(stderr, "Object ID number has exceeded MAX %d\n", oid); continue; }
            g_anim_count++;
            if (oid >= 0 && oid < MAX_OBJ_ID) g_obj_by_id[oid] = i;
        }
    }
    fprintf(stderr, "Total Static Objects %d, Total Anim Objects %d\n", g_static_count, g_anim_count);

    /* The .POS file: a 0x38-byte header (count first), then 56-byte records {id, flags, ?, dir,
       countdown, move routine, param[8]}. A static object named there becomes an animated one. */
    size_t n = 0;
    pos_buf = file_load(pos_path, &n);
    pos_len = pos_buf ? n : 0;
    if (!pos_buf || n < 0x38) return;
    int count = rd32(pos_buf);
    for (int k = 0; k < count && 0x38 + 56 * (size_t)(k + 1) <= n; k++) {
        uint8_t *r = pos_buf + 0x38 + 56 * k;
        int id = rd32(r);
        if (id < 0 || id >= MAX_OBJ_ID || g_obj_by_id[id] == -1) continue;
        int slot = g_obj_by_id[id];
        if (slot & 0x8000) {
            StaticObj *s = &g_static[slot & 0x7fff];
            int i = anim_new(s->x, s->y, s->anim, s->dir, 3);
            AnimObj *a = &g_anim[i];
            a->id = s->id;
            a->mirror = s->mirror;
            a->rec.mode = s->rec.mode;
            a->rec.layer = s->rec.layer;
            a->rec.sortable = s->rec.sortable;
            a->rec.push = s->rec.push;
            g_obj_by_id[id] = i;
            s->state = -1;                       /* 0x4025b0 */
            slot = i;
        }
        AnimObj *a = &g_anim[slot];
        uint32_t flags = (uint32_t)rd32(r + 4);
        a->move = rd32(r + 20);
        if (rd32(r + 12)) a->dir = rd32(r + 12);
        if (flags & 0x80000000u) a->rec.shadow = 0;
        flags &= 0x3fffffff;
        memcpy(r + 4, &flags, 4);
        a->countdown = rd32(r + 16);
        a->state = (int32_t)flags;
        for (int p = 0; p < 8; p++) a->param[p] = rd32(r + 24 + 4 * p);
    }
}

/* ------------------------------------------------------------------ update and draw */

void objects_update(void)
{
    /* 0x402700: slots 32..g_anim_hi (inclusive, as the original) */
    for (int i = 32; i <= g_anim_hi && i < MAX_ANIM; i++) {
        AnimObj *a = &g_anim[i];
        if (a->type == -1 || (g_world == 7 && a->type == 1)) continue;
        if (a->state != 2) {
            a->prev_frame = a->frame;
            a->countdown--;
            while (a->countdown < 1) {
                a->frame++;
                if (a->frame < a->nframes) {
                    anim_set_frame(a);
                } else if (a->loop == 0) {
                    a->state = 2;
                    a->frame--;
                } else if (a->loop == 1) {
                    anim_kill(a);
                } else if (a->loop == 2) {
                    a->frame = 0;
                    anim_set_frame(a);
                }
                a->countdown += a->pause;
            }
        }
        move_run(a);
    }
}

void objects_collect(void)
{
    /* 0x402830 */
    for (int i = 0; i < g_static_hi; i++) {
        StaticObj *s = &g_static[i];
        if (s->state <= 1) continue;
        int sx = s->x - g_lv.cam_x, sy = s->y - g_lv.cam_y, off;
        if (!s->mirror && !s->rec.flip) off = s->rec.hot_x;
        else { off = -(s->rec.hot_x + s->rec.w); s->rec.frame |= 0x80000000u; }
        sx += off;
        sy += s->rec.hot_y;
        if (sx < 640 && 0 < s->rec.w + sx && sy < 480 && 0 < s->rec.h + sy) {
            s->rec.y = sy;
            draw_list_add(&s->rec);
            s->rec.x = sx;
            s->rec.depth = s->depth_off + sy;
        }
    }
    for (int i = 33; i <= g_anim_hi && i < MAX_ANIM; i++) {
        AnimObj *a = &g_anim[i];
        if (a->type == -1) continue;
        int sx = a->x, sy = a->y;
        if (a->rec.bank != 0xd6) { sx -= g_lv.cam_x; sy -= g_lv.cam_y; }   /* bank 0xd6 is screen-fixed */
        int w = a->rec.w, off;
        a->rec.wx = sx;
        a->rec.wy = sy;
        a->rec.depth = a->depth_off + sy + a->rec.hot_y;
        if (!a->mirror && !a->rec.flip) off = a->rec.hot_x;
        else { off = -(a->rec.hot_x + w); a->rec.frame |= 0x80000000u; }
        sx += off;
        sy += a->rec.hot_y;
        if (sx - w / 2 < 640 && 0 < w + sx && sy < 480 && 0 < a->rec.h + sy) {
            a->rec.y = sy;
            draw_list_add(&a->rec);
            a->rec.x = sx;
            if (a->rec.shadow != -1 && g_nshadow < MAX_SHADOW) {
                DrawRec *s = &g_shadow[g_nshadow];
                draw_list_add(s);
                *s = a->rec;
                g_nshadow++;
                s->kind = 7;
                s->mode = 3;
            }
        }
    }
}

int pos_count(void)
{
    if (!pos_buf || pos_len < 0x38) return 0;
    int n = rd32(pos_buf);
    int fit = (int)((pos_len - 0x38) / 56);
    return n < 0 ? 0 : n < fit ? n : fit;
}

int pos_id(int k) { return rd32(pos_buf + 0x38 + 56 * (size_t)k); }
