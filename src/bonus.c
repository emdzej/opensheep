#include "levels_int.h"
#include "input.h"
#include <stdlib.h>
#include <string.h>

/* The bonus levels (x.5): their descriptor hooks and routines. */

/* ================================================================ 1.5: the sheep train */

/* The engine (Lv15_Move_00) runs on the shepherd's steering; each sheep it collects becomes a car
   (Lv15_Move_01) inserted behind it, following the engine's trail 20 ticks per car back. */
static int16_t *trail;            /* 0x65fe84: 10000 positions {x, y} */
static int trail_pos;             /* 0x65fe80 */
static int boost;                 /* 0x65fe88: ticks of slowed cars after a pick-up (the train grows) */
static AnimObj *engine;           /* 0x65fe7c */
static int pickups;               /* 0x65fe78: pick-ups on the field */
static int spawn_wait;            /* 0x65fe74 */
static int end_wait;              /* 0x65fe70: ticks to the end after the crash */
int g_bonus_count;                /* 0x65fea8: the bonus levels' HUD counter (lives, laps...) */

static int lv15_hit(int x, int y)
{
    /* 0x423c40: the free special cell under (x, y), -1 for none */
    uint32_t c = g_lv.layer3[(y / TILE_H) * g_lv.w + x / TILE_W];
    if (!(c & 0xff)) return -1;
    int si = c >> 8 & 0xff;
    if (g_lv.tab1[(x % TILE_W) + (y % TILE_H - TILE_H + (int)(c & 0xff) * TILE_H) * TILE_W] != 1) return -1;
    if (g_special[si].active) return -1;
    return si;
}

static void lv15_spawn(void)
{
    /* 0x423ce0: a pick-up somewhere on the field: a sheep of the breed (3 in 8) or one of two
       bonuses */
    if (pickups >= 100) return;
    int r = (int)(rng_next() & 7);
    int type = r <= 1 ? 0 : r > 4 ? 3 : 2;
    int x = (int32_t)rng_next() % 500 + 0x32;
    int y = (int32_t)rng_next() % 0x136 + 0x50;
    int anim = type ? type + 0x28 : (int)exe_u32(0x450998 + 4 * (uint32_t)g_breed);
    AnimObj *o = &g_anim[anim_new(x, y, anim, -1, 3)];
    o->move = 0x8002;
    o->d[0x15] = type;
    pickups++;
}

static void lv15_init(void)
{
    /* 0x423e30 + 0x423d80 */
    for (int i = 0; i < MAX_SHEEP; i++) sheep_out(i);
    free(trail);
    trail = calloc(10000, 2 * sizeof *trail);   /* 0x421dc0 */
    g_bonus_count = 3;
    AnimObj *o = &g_anim[anim_new(0x140, 0xf0, 7, 8, 1)];
    g_player.state = 100;
    g_player.face = 0x80;
    o->move = 0x8000;
    o->d[0x15] = 0;
    o->d[0x16] = 0x96;
    o->d[0x17] = 0x80;
    o->d[0x18] = 0;
    engine = o;
    g_player.dir = 0x80;
    pickups = 0;
    for (int i = 0; i < 0x1e; i++) lv15_spawn();
    trail_pos = 0;
    boost = 0;
    end_wait = 0;
    spawn_wait = 0x3c;
}

static void lv15_tick(void)
{
    /* 0x423e70 */
    g_timer_on = 0;
    g_hud_style = 8;
    if (boost > 0) boost--;
    trail_pos = (trail_pos + 1) % 10000;
    if (spawn_wait-- <= 0) {
        lv15_spawn();
        spawn_wait = (int)(rng_next() & 0x3f) + 0x1e;
    }
    if (end_wait > 0 && --end_wait == 0) g_end = 7;
}

static void lv15_actor(int32_t *a)
{
    /* 0x423ef0: the shepherd steering (100: toward his offset d1d / d1e, at most 8 a tick) and
       thrown off (0x65) */
    if (a[3] == 0x65) {
        a[0xe] = 5;
        AnimObj *o = &g_anim[a[9]];
        if (o->frame > 10) o->frame = 10;
        return;
    }
    if (a[3] != 100) return;
    int dx = a[0x1d], dy = a[0x1e];
    int dir = dir_of(dx, dy);
    int r = ftol(sqrt((double)(dy * dy) + (double)(dx * dx)));
    if (r > 0x80) {
        a[0x1d] = g_isin[dir] / 256;
        a[0x1e] = -(g_icos[dir] / 256);
        r = 0x80;
    }
    a[0x1f] = r;
    if (r > 5) a[0] = a[2] = dir_turn(a[0], dir, 8) & 0xff;
    a[0xe] = 0x11;
    ((float *)a)[1] = 0;
    a[2] = a[0];
}

static void lv15_crash_player(void)
{
    AnimObj *p = &g_anim[g_player.slot];
    p->depth_off = 0;
    p->rec.shadow_dy = 0;
    player_set_state(&g_player, 0x65);
}

static void lv15_wreck(AnimObj *a)
{
    /* subs 6-8: thrown (anim 5) and down (anim 8, a moan) */
    switch (a->sub) {
    case 6:
        anim_set(a->slot, 5, -1);
        lv15_crash_player();
        a->sub = 7;
        break;
    case 7:
        if (a->frame > 1) a->sub = 8;
        break;
    case 8:
        if (a->state == 2) {
            sound_play_at(a, crt_rand() % 2, 0, 0);
            anim_set(a->slot, 8, -1);
            if (a->d[0x1e] == 0) end_wait = 0x78;
        }
        break;
    }
}

static void lv15_move_00(AnimObj *a)
{
    /* 0x424000: the engine, up to speed d16 / 100 in the shepherd's direction with him on board;
       a free special cell wrecks it (smoke, remains), as does running into its own train */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0: {
        if (a->speed < (float)((double)(float)d[0x16] * (double)0.01f))
            a->speed = (float)((double)a->speed + (double)0.01f);
        int dir = g_player.face;
        d[0x17] = dir;
        a->dir = (dir + 8) / 16;
        double nx = (double)g_sin[dir] * (double)a->speed + (double)a->fx;
        float ny = (float)((double)a->fy - (double)g_cos[dir] * (double)a->speed);
        if (d[0x15] == 0) {
            a->fx = (float)nx;
            a->fy = ny;
            a->x = ftol(nx);
            a->y = ftol((double)ny);
            g_player.x = (float)nx;
            g_player.y = (float)((double)ny + 6.0);
        }
        g_player.speed = 0;
        AnimObj *p = &g_anim[g_player.slot];
        p->depth_off = 0x18;
        p->rec.shadow_dy = 10;
        if (a->anim != 1) anim_set(a->slot, 1, a->dir);
        a->frame = g_anim[g_player.slot].frame;
        a->countdown = g_anim[g_player.slot].countdown;
        anim_set_frame(a);
        if (lv15_hit(a->x, a->y) != -1) {
            lv15_crash_player();
            a->sub = 2;
        }
        if (crt_rand() % 0xb4 == 0) sound_play_at(a, crt_rand() % 3 + 0x71, 0, 0);
        break;
    }
    case 2:
        anim_set(a->slot, 0x10, -1);
        d[0x19] = 0x1e;
        a->sub = 3;
        sound_play_at(a, crt_rand() % 3 + 0x43, 0, 0);
        break;
    case 3: {
        int t = d[0x19];
        d[0x19] = t - 1;
        if (t < 1) {
            anim_new(a->x, a->y, 0xc, 8, 4);
            d[0x19] = 0x1e;
            a->sub = 4;
        }
        break;
    }
    case 4: {
        int t = d[0x19];
        d[0x19] = t - 1;
        if (t < 1) {
            anim_new(a->x, a->y, 0xc, 8, 4);
            g_anim[anim_new(a->x, a->y, 0x11, a->dir, 1)].rec.mode = 3;
            if (d[0x1e] == 0) end_wait = 0x78;
            a->sub = 5;
        }
        break;
    }
    case 5:
        a->rec.scale_x = 0;
        break;
    default:
        lv15_wreck(a);
        break;
    }
    if (trail) {
        trail[2 * trail_pos] = (int16_t)a->x;
        trail[2 * trail_pos + 1] = (int16_t)a->y;
    }
}

static void lv15_move_01(AnimObj *a)
{
    /* 0x424390: a car: d1d the one it follows, d1e the one behind; steers for the engine's trail
       20 ticks per car back, matching the engine's speed plus 2 per px it lags past 40 */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        d[0x1d] = engine->slot;
        d[0x1e] = engine->d[0x1e];
        if (engine->d[0x1e] != 0) g_anim[engine->d[0x1e]].d[0x1d] = a->slot;
        engine->d[0x1e] = a->slot;
        a->sub = 1;
        return;
    case 1: {
        int y = a->y;
        int n = g_anim[d[0x1d]].d[0x18] + 1;
        d[0x18] = n;
        int k = (boost + n * -0x14 + 0x271a + trail_pos) % 10000;
        int tx = trail ? trail[2 * k] : 0, ty = trail ? trail[2 * k + 1] : 0;
        int ex = a->x - tx, ey = y - ty;
        int r = ftol(sqrt((double)(ex * ex + ey * ey)));
        int dir = dir_of(tx - a->x, ty - y);
        d[0x17] = dir;
        int d16 = (dir + 8) / 16;
        a->dir = d16;
        d[0x16] = engine->d[0x16] + (r - 0x28) * 2;
        if ((float)((double)(float)d[0x16] * (double)0.01f) <= a->speed) {
            a->speed = (float)((double)a->speed - (double)0.1f);
            if (a->speed < 0.0f) a->speed = 0;
        } else {
            a->speed = (float)((double)a->speed + (double)0.1f);
        }
        if (boost != 0) a->speed = 0.5f;
        a->fx = (float)((double)g_sin[dir] * (double)a->speed + (double)a->fx);
        a->fy = (float)((double)a->fy - (double)g_cos[dir] * (double)a->speed);
        a->x = ftol((double)a->fx);
        a->y = ftol((double)a->fy);
        if (a->speed <= 1.5f && a->anim != 0) anim_set(a->slot, 0, d16);
        if (1.5f < a->speed && a->anim != 1) anim_set(a->slot, 1, a->dir);
        if (lv15_hit(a->x, a->y) != -1) {
            lv15_crash_player();
            a->sub = 2;
        }
        if (g_anim[d[0x1d]].sub == 8) a->sub = 6;
        if (boost == 0 && d[0x18] > 1 && engine->sub == 0 &&
            iabs(engine->y - a->y) + iabs(engine->x - a->x) < 0x14) {
            engine->sub = 6;
            return;
        }
        return;
    }
    case 2:
        anim_set(a->slot, 0x10, -1);
        d[0x19] = 0x1e;
        a->sub = 3;
        sound_play_at(a, crt_rand() % 3 + 0x43, 0, 0);
        d[0x1f] = 5;
        return;
    case 3: {
        /* the wreck spreads back down the train */
        if (d[0x1f]-- == 0) {
            AnimObj *b = &g_anim[d[0x1e]];
            if (b->sub != 2 && b->sub != 3) b->sub = 2;
        }
        int t = d[0x19];
        d[0x19] = t - 1;
        if (t < 1) {
            anim_new(a->x, a->y, 0xc, 8, 4);
            d[0x19] = 0x1e;
            a->sub = 4;
        }
        return;
    }
    case 4: {
        int t = d[0x19];
        d[0x19] = t - 1;
        if (t < 1) {
            if (d[0x1e] == 0) end_wait = 0x78;
            anim_new(a->x, a->y, 0xc, 8, 4);
            g_anim[anim_new(a->x, a->y, 0x11, a->dir, 1)].rec.mode = 3;
            anim_kill(a);
        }
        return;
    }
    default:
        lv15_wreck(a);
        return;
    }
}

static void lv15_move_02(AnimObj *a)
{
    /* 0x424840: a pick-up (d15: 0 a sheep, which joins the train, 100 points; 2 / 3 a bonus, 10
       points and 2 more top speed) */
    int32_t *d = a->d;
    if (a->sub == 0) {
        a->sub = 1;
        d[0x16] = a->rec.w / 2 + a->x;
        d[0x17] = a->rec.h * 3 / 4 + a->y;
        return;
    }
    if (a->sub != 1) return;
    int r = iabs(engine->y - d[0x17]) + iabs(engine->x - d[0x16]);
    if (d[0x15] == 0) {
        if (r < 0x14) {
            sound_play_at(a, 5, 0, 0);
            anim_set(a->slot, 0x29, -1);
            a->move = 0;
            g_score += 100;
            g_score_flash = 0x18;
            AnimObj *c = &g_anim[anim_new(d[0x16], d[0x17], 0, -1, 1)];
            boost += 0x14;
            c->move = 0x8001;
            c->rec.shadow = 1;
            pickups--;
        }
    } else if (d[0x15] > 1 && d[0x15] < 4 && r < 10) {
        sound_play_at(a, crt_rand() % 3 + 2, 0, 0);
        anim_kill(a);
        engine->d[0x16] += 2;
        g_score += 10;
        g_score_flash = 0x18;
        pickups--;
    }
}

/* ================================================================ 2.5: the sack race */

/* Sheep 0-3 hop along paths 1-4 (sheep 0 the shepherd's, as fast as keys are hit: one press a tick
   over the last 256 ticks; the others in random spurts); specials (0x18) mark hurdles and bridges. */
static int race_state;            /* 0x6629ac: 0 the count-down, 1 the race */
static int count_ticks;           /* 0x6629b0 */
static int count;                 /* 0x6629b4: the count-down shown (0x10e + n), -1 gone */
static int finished;              /* 0x662abc */
static uint8_t presses[256];      /* 0x6629b8 */
static int press_i;               /* 0x662ac0 */

static void lv25_init(void)
{
    /* 0x42a920 */
    for (int i = 0; i < 4; i++) {
        Sheep *s = &g_sheep[i];
        AnimObj *o = &g_anim[i];
        actor_set_state(s->d, 100);
        s->dir = 0x40;
        s->face = 0x40;
        anim_set(i, 0x1b, 4);
        path_begin(o, i + 1, 0, 1);
        s->kind = 6;
        o->countdown = crt_rand() & 0x1f;
        o->d[0x1d] = o->d[0x1e] = o->d[0x1f] = 1;
    }
    g_anim[0].type = 4;
    anim_set(0, 1, 4);
    for (int i = 4; i < MAX_SHEEP; i++) sheep_out(i);
    memset(presses, 0, sizeof presses);
    g_anim[PLAYER_SLOT].rec.scale_x = 0;
    g_anim[PLAYER_SLOT].rec.scale_y = 0;
    race_state = 0;
    count = 0;
    count_ticks = 0;
    g_timer_on = 0;
    g_player.state = 4;
    g_player.x = g_sheep[0].x;
    g_player.y = g_sheep[0].y;
    g_anim[PLAYER_SLOT].rec.shadow = -1;
    g_hud_style = 2;
}

static void lv25_tick(void)
{
    /* 0x42aa30: three seconds of count-down ("go" at the third), then the race until all four are
       in; the shepherd's sheep's place scores */
    if (race_state == 1) {
        if (finished != 4) return;
        switch (g_anim[0].d[0x19]) {
        case 1: g_score = 10000; break;
        case 2: g_score = 5000; break;
        case 3: g_score = 1000; break;
        case 4: g_score = 0; break;
        }
        g_end = 7;
        return;
    }
    if (race_state != 0 || ++count_ticks <= 0x3c) return;
    count_ticks = 0;
    if (++count == 2) {
        g_timer_on = 1;
        finished = 0;
        for (int i = 0; i < 4; i++) g_anim[i].sub = 2;
    } else if (count >= 3) {
        count = -1;
        race_state = 1;
    }
}

static void lv25_hop_player(int32_t *a)
{
    /* 0x42ab60 */
    input_pad_keys();
    int sum = 0;
    for (int i = 0; i < 256; i++) sum += g_keyheld[i];
    presses[press_i] = sum == 1;
    press_i = (press_i + 1) & 0xff;
    int n = 0;
    for (int i = 0; i < 256; i++) n += presses[i] != 0;
    float *f = (float *)a;
    f[1] = (float)((double)n * (double)4.0f * (double)0.0285714287f);
    if (f[1] > 4.0f) f[1] = 4.0f;
}

static void lv25_hop_ai(int32_t *a, AnimObj *o)
{
    /* 0x42abf0: slowing by 0.05 a tick, every 5-12 ticks a push of 0.5 */
    float *f = (float *)a;
    int t = o->d[0x15];
    o->d[0x15] = t - 1;
    if (t > 0) {
        f[1] = (float)((double)f[1] - (double)0.05f);
        if (f[1] < 0.0f) f[1] = 0;
        return;
    }
    f[1] = (float)((double)f[1] + 0.5);
    if (f[1] > 4.0f) f[1] = 4.0f;
    o->d[0x15] = (crt_rand() & 7) + 5;
}

static void lv25_actor(int32_t *a)
{
    /* 0x42ac60: state 100, a racer: speed steps along its path a tick; special 0x18's value is
       1 a hurdle (a 0x28 px jump over 0xa0), 2 / 3 under / over a bridge (draw mode, layer,
       sorted), 4-7 the tall hurdle object 0x89 + n knocked (0x50 px over 0x15e) */
    if (a[3] != 100) return;
    Sheep *s = (Sheep *)a;
    AnimObj *o = &g_anim[a[9]];
    int32_t *d = o->d;
    switch (o->sub) {
    case 0:
        o->sub = 1;
        break;
    case 2: {
        if (a[9] == 0) lv25_hop_player(a);
        else lv25_hop_ai(a, o);
        for (int n = ftol((double)s->speed); n > 0; n--) {
            if (path_follow(o)) {
                o->sub = 5;
                d[0x19] = ++finished;
                if (a[9] == 0) g_timer_on = 0;
                break;
            }
        }
        s->u18 = 0;
        sheep_specials(ftol((double)s->x), ftol((double)s->y), s, 1);
        switch (s->u18) {
        case 1:
            d[0x16] = 0xa0;
            d[0x17] = 0;
            d[0x18] = 0x28;
            o->sub = 3;
            break;
        case 2:
            d[0x1d] = 2;
            d[0x1e] = 0;
            d[0x1f] = 1;
            break;
        case 3:
            d[0x1d] = d[0x1e] = d[0x1f] = 1;
            break;
        case 4: case 5: case 6: case 7: {
            AnimObj *h = obj(0x89 + s->u18);   /* the original takes the slot unchecked */
            if (h) anim_set(h->slot, 0, -1);
            d[0x16] = 0x15e;
            d[0x17] = 0;
            d[0x18] = 0x50;
            o->sub = 3;
            break;
        }
        }
        if (o->frame == 1 && o->prev_frame != 1) sound_play_at(o, (int)(rng_next() & 6) + 2, 0, 0);
        break;
    }
    case 3: {
        for (int i = 0; i < 3; i++) path_follow(o);
        d[0x17] += 4;
        int k = (d[0x17] << 7) / d[0x16];
        int h = ftol((double)(float)d[0x18] * (double)g_sin[k]);
        o->rec.shadow_dy = h;
        o->y -= h;
        if (d[0x17] >= d[0x16]) {
            o->rec.shadow_dy = 0;
            o->sub = 2;
        }
        break;
    }
    }
    s->x = (float)o->x;
    s->y = (float)o->y;
    a[0] = a[2] = o->dir << 4;
    o->depth_off = d[0x1d] == 1 ? 0 : 0x190;
    o->rec.mode = (uint8_t)d[0x1d];
    o->rec.layer = (uint8_t)d[0x1e];
    o->rec.sortable = (uint8_t)d[0x1f];
    if (a[9] != 0) return;
    /* the shepherd rides along unseen */
    g_player.x = s->x;
    g_player.y = s->y;
    g_anim[PLAYER_SLOT].rec.scale_x = 0;
    g_anim[PLAYER_SLOT].rec.scale_y = 0;
    g_anim[PLAYER_SLOT].rec.shadow = -1;
    g_anim[0].anim = 1;
    g_anim[0].type = 3;
    a[0xe] = -1;
    g_player.in_dx = -500;
}

/* ================================================================ 3.5: the shooting gallery */

/* Three rounds, one screen each (640 px apart): six waves of five targets fly in along paths; the
   shepherd walks the bottom edge, loads a cannon (Lv35_Move_02) and its shot (Move_00) knocks down
   a target (Move_03); targets past their second point drop bombs (Move_01). */
static int round35;               /* 0x65fea4 */
static int wave;                  /* 0x65fe98 */
static int wave_wait;             /* 0x65feac */
static int spawn_t;               /* 0x65fea0 */
static int bomb_t;                /* 0x65fe94 */
static int left35;                /* 0x65feb0: targets still up this round */
static int ntargets;              /* 0x65feb4 */
static AnimObj *targets[30];      /* 0x65feb8 */

static void lv35_round(void)
{
    /* 0x425810 */
    int x0 = round35 * 0x280;
    g_lv.cam_min_x = x0;
    g_lv.cam_max_x = x0 + 0x280;
    left35 = 0x1e;
    bomb_t = 0x3c;
    wave_wait = 0x3c;
    g_player.y = 430.0f;
    memset(targets, 0, sizeof targets);
    g_player.x = (float)((double)x0 + (double)405.0f);
    g_player.state = 100;
    g_lv.cam_min_y = 0;
    g_lv.cam_max_y = 0x1e0;
    wave = 0;
    spawn_t = 0;
    ntargets = 0;
    g_hud_style = 4;
}

static void lv35_init(void)
{
    /* 0x4258b0 */
    for (int i = 0; i < MAX_SHEEP; i++) sheep_out(i);
    round35 = 0;
    g_bonus_count = 3;
    lv35_round();
}

static void lv35_tick(void)
{
    /* 0x4258e0: a wave a second after the last (five targets, a second apart, on path 0x1d +
       wave + 10 x round); a bomb every 32-95 ticks from a random target */
    g_timer_on = 0;
    int t = spawn_t;
    bool go = false;
    if (wave_wait > 0) {
        if (--wave_wait == 0) {
            t = 0x12c;
            if (++wave < 6) wave_wait = 0x258;
            go = true;
        }
    }
    if (go || t > 0) {
        spawn_t = t - 1;
        if (spawn_t % 0x3c == 0) {
            int p = wave + round35 * 10 + 0x1d;
            if (p > 0x37) p = 0x37;
            path_start(p);
            AnimObj *o = &g_anim[anim_new(g_path_x, g_path_y, 0x2b, -1, 3)];
            path_begin(o, p, 2, 1);
            o->move = 0x8003;
            o->d[0x15] = ntargets;
            if (ntargets < 30) targets[ntargets] = o;   /* the original has no check (5 x 6 fit) */
            ntargets++;
        }
    }
    if (--bomb_t <= 0) {
        int i = (int32_t)rng_next() % 0x1e;
        for (int n = 0x1e; n > 0; n--)
            if (!targets[i]) i = (i + 1) % 0x1e;
        AnimObj *o = targets[i];
        if (o && o->d[0x24] > 1) g_anim[anim_new(o->x, o->y, 0x2c, -1, 3)].move = 0x8001;
        bomb_t = (int)(rng_next() & 0x3f) + 0x20;
    }
    if (left35 != 0) return;
    if (++round35 < 3) lv35_round();
    else g_end = 7;
}

static void lv35_steer(int32_t *a)
{
    /* 0x425a80: left / right only (turning on the spot 16 a tick, else 8), kept 20 px inside
       the round's screen */
    Player *p = (Player *)a;
    p->in_dy = 0;
    p->anim[0] = 0;
    p->anim[1] = 1;
    p->anim[2] = 1;
    int dx = p->in_dx;
    int to = dx < -1 ? 0xc0 : 0;
    if (dx > 1) to = 0x40;
    int r = iabs(dx);
    if (r > 0x80) {
        p->in_dx = g_isin[to] / 256;
        r = 0x80;
    }
    p->in_mag = r;
    bool turn = true;
    int step = 8;
    if (p->speed == 0.0f && p->dir != to) {
        if (r <= 0x10) turn = false;
        else step = 0x10;
    }
    if (turn) {
        p->dir = p->face = dir_turn(p->dir, to, step);
    }
    if (!turn || r <= 0x10) {
        p->speed = 0;
    } else {
        p->speed = p->walk;
        if (g_run) p->speed = p->run;
    }
    p->x = (float)((double)g_sin[p->dir] * (double)p->speed + (double)p->x);
    float lo = (float)(g_lv.cam_min_x + 0x14), hi = (float)(g_lv.cam_max_x - 0x14);
    if (p->x < lo) p->x = lo;
    if (hi < p->x) p->x = hi;
    player_specials(ftol((double)p->x), ftol((double)p->y), p, 1);
    p->face = p->dir;
}

static void lv35_actor(int32_t *a)
{
    /* 0x425bd0: 100 walking; 0x65 hit by a bomb (anim 5, then out of lives the end); 0x66
       loading the cannon (anim 3) */
    int slot = a[9];
    AnimObj *o = &g_anim[slot];
    switch (a[3]) {
    case 100:
        lv35_steer(a);
        return;
    case 0x65:
        if (slot <= 0x1e) return;
        a[0xe] = 5;
        ((float *)a)[1] = 0;
        if (o->anim != 5) {
            anim_set(slot, 5, -1);
            return;
        }
        if (o->state != 2) return;
        if (g_bonus_count <= 0) g_end = 7;
        else a[3] = 100;
        return;
    case 0x66:
        a[0xe] = 3;
        ((float *)a)[1] = 0;
        if (o->anim != 3) {
            anim_set(slot, 3, -1);
            return;
        }
        o->countdown = 0;
        if (o->state == 2) player_set_state((Player *)a, 100);
        return;
    }
}

static void lv35_move_00(AnimObj *a)
{
    /* 0x425cb0: the shot, along its path; a target within 20 px (Manhattan, 10 px low) falls */
    path_follow(a);
    for (int i = 0; i < 30; i++) {
        AnimObj *t = targets[i];
        if (t && iabs(a->y - t->y + 10) + iabs(a->x - t->x) < 0x14) {
            t->sub = 1;
            anim_kill(a);
        }
    }
}

static void lv35_move_01(AnimObj *a)
{
    /* 0x425d10: a bomb, 2 px a tick down; on the walking shepherd it costs a life */
    a->y += 2;
    if (a->y > 0x1e0) anim_kill(a);
    int r = iabs(a->y - ftol((double)g_player.y) + 0x1a) + iabs(a->x - ftol((double)g_player.x));
    if (r < 0x12 && g_player.state == 100) {
        g_player.state = 0x65;
        g_bonus_count--;
        anim_kill(a);
    }
}

static void lv35_move_02(AnimObj *a)
{
    /* 0x425da0: the cannon: touched, the shepherd loads it (0x66); then anim d15, a shot (anim
       d16) along path d17, and a second's rest */
    int32_t *d = a->d;
    switch (a->sub) {
    case 0:
        if (d[0x22] != -1) {
            player_set_state(&g_player, 0x66);
            a->sub = 1;
        }
        break;
    case 1: {
        if (g_player.state == 0x66) break;
        d[0x1d] = a->anim;
        anim_set(a->slot, d[0x15], -1);
        path_start(d[0x17]);
        AnimObj *b = &g_anim[anim_new(g_path_x, g_path_y, d[0x16], -1, 3)];
        path_begin(b, d[0x17], 4, 1);
        b->move = 0x8000;
        d[0x1e] = 0x3c;
        a->sub = 2;
        break;
    }
    case 2: {
        int t = d[0x1e];
        d[0x1e] = t - 1;
        if (t < 1) {
            anim_set(a->slot, d[0x1d], -1);
            d[0x22] = -1;
            a->sub = 0;
        }
        break;
    }
    }
}

static void lv35_move_03(AnimObj *a)
{
    /* 0x425ea0: a target along its path (gone at the end); shot, it falls (anim 0x2a) for 100 */
    switch (a->sub) {
    case 0:
        if (path_follow(a)) a->sub = 2;
        break;
    case 1:
        if (a->anim != 0x2a) anim_set(a->slot, 0x2a, a->dir < 8 ? 4 : 0xc);
        if (a->state == 2) {
            g_score += 100;
            a->sub = 2;
        }
        break;
    case 2:
        if (a->d[0x15] >= 0 && a->d[0x15] < 30) targets[a->d[0x15]] = NULL;
        anim_kill(a);
        left35--;
        break;
    }
}

/* ================================================================ dispatch */

bool bonus_actor(uint32_t va, int32_t *actor)
{
    switch (va) {
    case 0x423ef0: lv15_actor(actor); return true;
    case 0x42ac60: lv25_actor(actor); return true;
    case 0x425bd0: lv35_actor(actor); return true;
    default: return false;
    }
}

bool bonus_call(uint32_t va)
{
    switch (va) {
    case 0x423e30: lv15_init(); return true;
    case 0x423e70: lv15_tick(); return true;
    case 0x42a920: lv25_init(); return true;
    case 0x42aa30: lv25_tick(); return true;
    case 0x4258b0: lv35_init(); return true;
    case 0x4258e0: lv35_tick(); return true;
    default: return false;
    }
}

MoveFn bonus_move(uint32_t va)
{
    switch (va) {
    case 0x424000: return lv15_move_00;
    case 0x424390: return lv15_move_01;
    case 0x424840: return lv15_move_02;
    case 0x425cb0: return lv35_move_00;
    case 0x425d10: return lv35_move_01;
    case 0x425da0: return lv35_move_02;
    case 0x425ea0: return lv35_move_03;
    default: return NULL;
    }
}
