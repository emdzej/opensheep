#include "levels_int.h"
#include "input.h"
#include "sprites.h"
#include <math.h>
#include <stdio.h>

/* The bonus levels with cars: 5.5 (the kart race) and 6.5 (the dodgems). A car (0x661058, 0xd8
   bytes, one per sheep slot) keeps its momentum as 32 velocity bins, one per heading sector: the
   bin it faces is driven, the others decay; the sum moves it. */

typedef union {
    struct {
        int32_t off;            /* 0x00 negative: not driven */
        float x, y;             /* 0x04 */
        int32_t state;          /* 0x0c 1 racing, 2 waiting (timer) */
        int32_t control;        /* 0x10 0 the keys, 3 the computer */
        int32_t keys;           /* 0x14 1 left, 2 right, 4 throttle, 8 brake */
        int32_t heading;        /* 0x18 0..0xfff */
        int32_t turn;           /* 0x1c */
        int32_t spin;           /* 0x20 */
        int32_t wp;             /* 0x24 the computer's next waypoint */
        int32_t lane;           /* 0x28 0..3 */
        float vel[32];          /* 0x2c */
        float acc;              /* 0xac */
        int32_t timer;          /* 0xb0 */
        int32_t next_cp;        /* 0xb4 */
        int32_t laps;           /* 0xb8 */
        int32_t rank;           /* 0xbc */
        int32_t max_turn;       /* 0xc0 */
        float acc_step, top, acc_max, decay, coast;     /* 0xc4 */
    };
    int32_t d[0x36];
} Car;

static Car cars[10];              /* 0x661058 */
static float speed_now;           /* 0x6629a8 */
static float vx, vy;              /* 0x660048, 0x660044 */
static int wp_x[64][4], wp_y[64][4];   /* 0x660058 (x, y of the four lanes' point k, 8 dwords a k) */
static int wp_count;              /* 0x66004c */
static int finish_order;          /* 0x660050 */
static int blink;                 /* 0x660040 */

static int angle_of(int dx, int dy)
{
    /* 0x425540: 0..0xfff, 0 up, clockwise */
    double t = atan2(-(double)dy, (double)dx);
    return ftol(((1.570796327 - t) * 4096.5 + 3.141592654) * 0.15915494307111402) & 0xfff;
}

static int sector(const Car *c) { return c->heading >> 7; }

static void char_bank_load(int bank, uint32_t name, int at, uint32_t codes)
{
    /* the shepherd's car: the name's two letters at `at` replaced by the character's code */
    char path[64];
    snprintf(path, sizeof path, "%s", exe_str(name));
    uint32_t code = exe_u32(codes + 2 * (uint32_t)(g_char & 3));
    if ((int)strlen(path) > at + 1) {
        path[at] = (char)code;
        path[at + 1] = (char)(code >> 8);
    }
    sprite_bank_load(bank, path);
}

static void car_keys(Car *c)
{
    /* 0x429ef0 (the computer: 0x42a750) */
    if (c->control == 0) {
        c->keys = 0;
        if (g_keyheld[DIK_LEFT]) c->keys = 1;
        if (g_keyheld[DIK_RIGHT]) c->keys |= 2;
        if (g_keyheld[DIK_UP]) c->keys |= 4;
        if (g_keyheld[DIK_DOWN]) c->keys |= 8;
    } else if (c->control == 3) {
        /* 0x42a750: steer for this lane's waypoint (dead ahead within 8), throttle always; within
           80 px (Manhattan) of any lane's point the next one; stuck 500 ticks, a shove back */
        c->keys = 0;
        if (c->off != 0) return;
        int k = c->wp & 63;
        int a = angle_of(wp_x[k][c->lane & 3] - ftol((double)c->x), wp_y[k][c->lane & 3] - ftol((double)c->y));
        int h = c->heading;
        int diff = a - h + 0x1000;
        if ((iabs(diff) & 0xfff) > 8) c->keys |= diff & 0x800 ? 1 : 2;
        int iy = ftol((double)c->y), ix = ftol((double)c->x);
        int near = 0;
        for (int l = 0; l < 4; l++)
            if (iabs(iy - wp_y[k][l]) + iabs(ix - wp_x[k][l]) < 0x50) near = l + 1;
        if (near && ++c->wp >= wp_count) c->wp = 0;
        c->keys |= 4;
        if (c->timer > 500) {
            c->vel[((h >> 7) - 0x10) & 0x1f] = 2.0f;
            c->timer = 0;
        }
    }
}

static void car_steer(Car *c)
{
    /* 0x424e50: turning 8 a tick up to speed x 20 (at most max_turn), reversed going backwards;
       crossing a sector hands 0.4 / 0.77 of the speed over; the spin dies down by 2 */
    if (c->off < 0) return;
    float v = c->vel[sector(c)];
    speed_now = v;
    int lim = iabs(ftol((double)v * (double)20.0f));
    if (c->max_turn <= lim) lim = c->max_turn;
    if (c->vel[sector(c)] < 0.0f) c->keys ^= 3;
    int k = c->keys;
    if (k & 1) {
        if (c->turn > 0) c->turn = 0;
        else {
            c->turn -= 8;
            if (c->turn < -lim) c->turn = -lim;
        }
    }
    if (k & 2) {
        if (c->turn < 0) c->turn = 0;
        else {
            c->turn += 8;
            if (c->turn > lim) c->turn = lim;
        }
    }
    if (!(k & 3)) c->turn = 0;
    int old = c->heading;
    c->heading = (c->turn + old) & 0xfff;
    if ((c->heading ^ old) & ~0x7f) {
        c->vel[old >> 7] = (float)((double)speed_now * (double)0.4f);
        c->vel[sector(c)] = (float)((double)speed_now * (double)0.769230783f);
    }
    if (c->spin == 0) return;
    if (c->spin > 0) {
        c->spin -= 2;
        if (c->spin < 0) c->spin = 0;
    } else {
        c->spin += 2;
        if (c->spin > 0) c->spin = 0;
    }
    c->heading = (c->heading + c->spin) & 0xfff;
}

static void car_throttle(Car *c)
{
    /* 0x429f50 */
    speed_now = c->vel[sector(c)];
    if (c->off < 0) return;
    if (c->keys & 4) {
        if (c->acc < 0.0f) c->acc = 0;
        speed_now = (float)((double)speed_now + (double)c->acc);
        double t = (double)c->acc + (double)c->acc_step;
        if (!((double)speed_now < (double)c->top)) t = (double)c->acc_max * 0.5;
        if (!(t < (double)c->acc_max)) t = c->acc_max;
        c->acc = (float)t;
        return;
    }
    if (c->keys & 8) {
        if (c->acc > 0.0f) c->acc = 0;
        speed_now = (float)((double)speed_now + (double)c->acc);
        double t = (double)c->acc - (double)c->acc_step;
        if (!(-((double)c->top * 0.5) <= (double)speed_now)) t = -131072.0;
        double m = (double)-c->acc_max;
        if (!(t > m)) t = m;
        c->acc = (float)t;
        return;
    }
    speed_now = (float)((double)speed_now * (double)c->coast);
}

static void car_bins(Car *c)
{
    /* 0x42a080 */
    for (int i = 0; i < 0x20; i++) {
        if (i == sector(c)) {
            c->vel[i] = speed_now;
        } else {
            float v = (float)((double)c->decay * (double)c->vel[i]);
            c->vel[i] = v;
            if ((double)-0.1f < (double)v && (double)v < (double)0.1f) c->vel[i] = 0;
        }
    }
}

static void car_move(Car *c)
{
    /* 0x42a0e0 + 0x424f70 */
    vy = vx = 0;
    for (int i = 0; i < 0x20; i++) {
        vx = (float)((double)g_sin[i * 8] * (double)c->vel[i] + (double)vx);
        vy = (float)((double)g_cos[i * 8] * (double)c->vel[i] + (double)vy);
    }
    c->x = (float)((double)vx + (double)c->x);
    c->y = (float)((double)c->y - (double)vy);
}

static void car_skid(Car *c)
{
    /* 0x42a140: sideways speed raises dust (0xc) */
    int s0 = sector(c) + 4;
    double s = 0.0;
    for (int k = 0; k < 8; k++) {
        s += (double)c->vel[(s0 + k) & 0x1f];
        s += (double)c->vel[(s0 - 0x10 + k) & 0x1f];
    }
    float side = (float)s;
    if ((crt_rand() & 3) != 0) return;
    int r = crt_rand() & 3;
    if (r < ftol((double)side)) anim_new(ftol((double)c->x), ftol((double)c->y), 0xc, 8, 4);
}

static void car_bump(Car *c, int n, bool dodgem)
{
    /* 0x42a1c0 (6.5: 0x424f90, over the cars in play, the shepherd's hitting 1.5 times as hard
       and taking 0.75): within 48 px (Manhattan) of another car: pushed out to 48.5 px; head-on
       both lose a quarter of their speed and spin; the two swap momentum (sound 4-7) */
    for (int k = 0; k < n; k++) {
        Car *o = &cars[k];
        if (o == c || (dodgem ? o->state != 2 : o->state == 0)) continue;
        int dy = ftol((double)c->y - (double)o->y);
        int dx = ftol((double)c->x - (double)o->x);
        if (iabs(dx) + iabs(dy) >= 0x30) continue;
        int d = dir_of(dx, dy);
        c->x = (float)((double)g_sin[d] * (double)48.5f + (double)o->x);
        c->y = (float)((double)o->y - (double)g_cos[d] * (double)48.5f);
        int diff = c->heading - o->heading;
        if (diff < 0) diff += 0x1000;
        if (diff > 0x400 && diff < 0xc00) {
            int i = sector(c), j = (i + 1) & 0x1f;
            c->vel[i] = (float)((double)c->vel[i] * (double)0.75f);
            c->vel[j] = (float)((double)c->vel[i] * (double)0.25f + (double)c->vel[j]);
            o->vel[i] = (float)((double)o->vel[i] * (double)0.75f);
            o->vel[j] = (float)((double)c->vel[i] * (double)0.25f + (double)o->vel[j]);
            c->spin = -0x32;
            o->spin = -0x32;
        }
        float take = 1.0f, give = 1.0f;
        if (dodgem && c == &cars[0]) take = 0.75f, give = 1.5f;
        if (dodgem && o == &cars[0]) take = 1.5f, give = 0.75f;
        for (int i = 0; i < 0x20; i++) {
            float t = (float)((double)give * (double)c->vel[i]);
            c->vel[i] = (float)((double)take * (double)o->vel[i]);
            o->vel[i] = t;
        }
        sound_play_at(&g_anim[k], (int)(rng_next() & 3) + 4, 0, 0);
    }
}

static void car_wall(Car *c, int pd)
{
    /* 0x42a360: knocked a px back off the wall (push direction pd), the bins mirrored about it at
       half, and a spin by the angle hit */
    int u = (pd - 8) & 0xf;
    int e = (pd * 2 + 8) & 0x1f;
    c->x = (float)((double)g_sin[u * 16] + (double)c->x);
    c->y = (float)((double)c->y - (double)g_cos[u * 16]);
    int lo = e, hi = e;
    for (int n = 0; n < 0xe; n++) {
        lo = (lo - 1) & 0x1f;
        hi = (hi + 1) & 0x1f;
        float a = (float)((double)c->vel[lo] * 0.5);
        float b = (float)((double)c->vel[hi] * 0.5);
        c->vel[lo] = b;
        c->vel[hi] = a;
    }
    int r = iabs(ftol((double)speed_now * (double)20.0f));
    switch (((((c->heading >> 7) & 0xf) - e) * 2 >> 2) & 7) {
    case 0: case 1: c->spin = -(r / 2); break;
    case 5: c->spin = -r; break;
    case 3: c->spin = r; break;
    case 7: c->spin = r / 2; break;
    }
}

static void car_map(Car *c, int i)
{
    /* 0x42a470: the map under the car through its sheep: specials 0x14 / 0x15 (at speed > 1) set
       a spin (a slick, a bump); a wall bounces it (sound 0-3) */
    Sheep *s = &g_sheep[i];
    s->u18 = -1;
    sheep_collide(ftol((double)c->x), ftol((double)c->y), s->d, 0);
    Car *sc = &cars[s->slot % 10];
    if (c->vel[sector(c)] > 1.0f && sc->spin == 0) {
        if (s->u18 == 0x14) {
            sc->spin = (crt_rand() & 0xff) - 0x80;
            s->u18 = -1;
        }
        if (s->u18 == 0x15) {
            sc->spin = (crt_rand() & 0x3f) - 0x20;
            s->u18 = -1;
        }
    }
    if (s->hit == 2) {
        c->x = s->x;
        c->y = s->y;
        car_wall(c, s->push_dir);
        sound_play_at(&g_anim[s->slot], (int)(rng_next() & 3), 0, 0);
    }
}

static void car_laps(Car *c, int i)
{
    /* 0x42a580: checkpoints (special 0x18's value) in order; the first again is a lap, 100
       points for the shepherd; at five laps the place */
    Sheep *s = &g_sheep[i];
    c->timer++;
    if (s->u18 == -1 || s->u18 != c->next_cp) return;
    c->timer = 0;
    if (c->next_cp == 0) {
        c->laps++;
        if (i == 0) g_score += 100;
        if (c->laps == 5) g_anim[s->slot].d[0x19] = finish_order++;
    }
    if (++c->next_cp > (int)exe_u32(0x451dd8)) c->next_cp = 0;
}

static int progress_cp(const Car *c, int ncp)
{
    return c->next_cp == 0 ? ncp + 1 : c->next_cp;
}

static void car_ranks(int n, int ncp)
{
    /* 0x42a630 (6.5: 0x425420): an exchange sort by laps, checkpoint, then the ticks since it */
    Car *p[10];
    for (int i = 0; i < n; i++) p[i] = &cars[i];
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            Car *a = p[i], *b = p[j];
            bool swap;
            if (a->laps != b->laps) swap = a->laps < b->laps;
            else if (progress_cp(a, ncp) != progress_cp(b, ncp)) swap = progress_cp(a, ncp) < progress_cp(b, ncp);
            else swap = a->timer <= b->timer;
            if (swap) {
                p[j] = a;
                p[i] = b;
            }
        }
    }
    for (int i = 0; i < n; i++) p[i]->rank = i;
}

static void cars_tick(void)
{
    /* 0x42a890 */
    for (int i = 0; i < 10; i++) {
        Car *c = &cars[i];
        if (c->state == 1) {
            car_keys(c);
            car_steer(c);
            car_throttle(c);
            car_bins(c);
            car_move(c);
            car_skid(c);
            car_bump(c, 10, false);
            car_map(c, i);
            car_laps(c, i);
        }
        if (c->state == 2 && --c->timer < 1) c->state = 1;
    }
    car_ranks(10, (int)exe_u32(0x451dd8));
}

/* ================================================================ 5.5: the kart race */

static void lv55_init(void)
{
    /* 0x429c00: the shepherd's kart (sprites by character), ten karts on the grid two seconds
       apart (the shepherd's swapped into the fifth place), the four lanes' waypoints from paths
       1-4 */
    char_bank_load(0xca, 0x451e58, 0x16, 0x451e80);
    int y = 0x500;
    for (int i = 0; i < 10; i++) {
        Sheep *s = &g_sheep[i];
        Car *c = &cars[i];
        s->state = 100;
        memset(c, 0, sizeof *c);
        c->control = 3;
        int lane = i & 3;
        s->x = (float)(lane * 0x38 + 0x186);
        s->y = (float)y;
        s->dir = 0;
        s->face = 0;
        c->x = s->x;
        c->y = s->y;
        c->lane = lane;
        c->max_turn = 0x34;
        c->acc_step = 0.05f;
        c->top = 5.0f;
        c->acc_max = 0.1f;
        c->decay = 0.96f;
        c->coast = 0.995f;
        c->state = 2;
        c->timer = 0x78 + 0x1e * i;
        y += 0x12;
    }
    for (int i = 10; i < MAX_SHEEP; i++) sheep_out(i);
    float t;
    t = cars[0].x; cars[0].x = cars[4].x; cars[4].x = t;
    t = cars[0].y; cars[0].y = cars[4].y; cars[4].y = t;
    int w = cars[0].timer; cars[0].timer = cars[4].timer; cars[4].timer = w;
    cars[0].control = 0;
    int counts[4] = { 0 };
    for (int l = 0; l < 4; l++) {
        for (int k = 0;; k++) {
            if (path_flag(l + 1, k) == 2) break;
            if (counts[l] < 64) {
                path_point(l + 1, k, &wp_x[counts[l]][l], &wp_y[counts[l]][l]);
                counts[l]++;
            }
            if (k > 1000) break;   /* (a path without an end) */
        }
    }
    g_hud_style = 6;
    wp_count = counts[0];
    finish_order = 0;
}

static void lv55_tick(void)
{
    /* 0x429e70: the pad as held arrows, the karts, the shepherd riding kart 0 unseen */
    if (g_pad_x < -500) g_keyheld[DIK_LEFT]++;
    if (g_pad_x > 500) g_keyheld[DIK_RIGHT]++;
    if (g_pad_y < -500) g_keyheld[DIK_UP]++;
    if (g_pad_y > 500) g_keyheld[DIK_DOWN]++;
    cars_tick();
    g_player.x = g_sheep[0].x;
    g_player.y = g_sheep[0].y;
    AnimObj *p = &g_anim[g_player.slot];
    p->rec.scale_x = 0;
    p->rec.scale_y = 0;
}

static void lv55_actor(int32_t *a)
{
    /* 0x429a00: state 100, a kart drawn from its car: the facing in 16, the frame from the wheels
       (d1d advanced by speed x 8) and the sector's parity; the place / laps to go in d15 / d16;
       the shepherd's done at five laps (1000 x (10 - place) points) and blinks while waiting */
    if (a[3] != 100) return;
    int i = a[9];
    Sheep *s = (Sheep *)a;
    Car *c = &cars[i % 10];
    AnimObj *o = &g_anim[i];
    if (c->state != 0) {
        o->rec.scale_x = 0x100;
        o->mirror = 0;
        int u = c->heading >> 7;
        int wheel = iabs(o->d[0x1d] / 64) % 5;
        if (u < 0x11) {
            s->dir = s->face = (u / 2) << 4;
            o->frame = (u & 1) ? wheel + 5 : wheel;
        } else if (u != 0x1f) {
            s->dir = s->face = ((u + 1) / 2) << 4;
            o->frame = (u & 1) ? wheel + 5 : wheel;
        } else {
            s->dir = s->face = 0;
            o->mirror = 1;
            o->frame = wheel + 5;
        }
        s->x = c->x;
        s->y = c->y;
        o->type = 3;
        int anim = 0x4d;
        if (i == 0) {
            anim = 0x66;
            if (c->laps == 5) {
                g_end = 7;
                g_score += (10 - o->d[0x19]) * 1000;
            }
        }
        s->anim[0] = s->anim[1] = s->anim[2] = anim;
        if (o->anim == anim) anim_set_frame(o);
        o->d[0x15] = c->rank + 1;
        o->d[0x16] = 4 - c->laps;
        if (o->d[0x16] < 0) o->d[0x16] = 0;
        int v = ftol((double)c->vel[sector(c)] * (double)8.0f);
        o->countdown = 10;
        o->d[0x1d] += v;
    }
    if (c->state == 2 && i == 0) {
        blink++;
        o->rec.scale_x = (blink & 8) ? 0 : 0x100;
    }
}

/* ================================================================ 6.5: the dodgems */

/* Eight dodgems on a raft; bumped into the water (specials 0-3) a car sinks: the shepherd's comes
   back for a life, another is out for 1000 points; the last one out ends it. */
static int wp_count65;            /* 0x65fe8c */
static int left65;                /* 0x65fe90 */

static void ai65(Car *c)
{
    /* 0x425570: 0x42a750 with 150 px and the lane that point is in taken */
    c->keys = 0;
    if (c->off != 0) return;
    int k = c->wp & 63;
    int a = angle_of(wp_x[k][c->lane & 3] - ftol((double)c->x), wp_y[k][c->lane & 3] - ftol((double)c->y));
    int h = c->heading;
    int diff = a - h + 0x1000;
    if ((iabs(diff) & 0xfff) > 8) c->keys |= diff & 0x800 ? 1 : 2;
    int iy = ftol((double)c->y), ix = ftol((double)c->x);
    int lane = -1;
    for (int l = 0; l < 4; l++)
        if (iabs(iy - wp_y[k][l]) + iabs(ix - wp_x[k][l]) < 0x96) lane = l;
    if (lane != -1) {
        c->lane = lane;
        if (++c->wp >= wp_count65) c->wp = 0;
    }
    c->keys |= 4;
    if (c->timer > 500) {
        c->vel[((h >> 7) - 0x10) & 0x1f] = 2.0f;
        c->timer = 0;
    }
}

static void map65(Car *c, int i)
{
    /* 0x425290: water (specials 0 / 1 near, 2 / 3 far: sinking to 240 / 960 px below, sound 8),
       a spin (0x14), the raft's edge bounces */
    Sheep *s = &g_sheep[i];
    AnimObj *o = &g_anim[i];
    s->u18 = -1;
    sheep_collide(ftol((double)c->x), ftol((double)c->y), s->d, 0);
    switch (s->u18) {
    case 0: case 1:
        o->d[0x17] = 0xf0;
        c->state = 3;
        sound_play_at(&g_anim[s->slot], 8, 0, 0);
        break;
    case 2: case 3:
        o->d[0x17] = 0x3c0;
        c->state = 3;
        sound_play_at(&g_anim[s->slot], 8, 0, 0);
        break;
    case 0x14:
        cars[s->slot % 10].spin = (crt_rand() & 0xff) - 0x80;
        s->u18 = -1;
        break;
    }
    if (s->hit == 2) {
        c->x = s->x;
        c->y = s->y;
        car_wall(c, s->push_dir);
        sound_play_at(&g_anim[s->slot], (int)(rng_next() & 3), 0, 0);
    }
}

static void cars65_tick(void)
{
    /* 0x4256b0 */
    for (int i = 0; i < 8; i++) {
        Car *c = &cars[i];
        if (c->state == 2) {
            if (c->control == 0) car_keys(c);
            else if (c->control == 3) ai65(c);
            car_steer(c);
            car_throttle(c);
            car_bins(c);
            car_move(c);
            car_bump(c, 8, true);
            map65(c, i);
        }
        if (c->state == 1 && --c->timer < 1) c->state = 2;
        if (c->state == 3) {
            /* sinking: 3 px a tick, spinning, clipped at the water line */
            AnimObj *o = &g_anim[i];
            double y = (double)c->y + (double)3.0f;
            c->heading = (c->heading + 0x20) & 0x3ff;
            o->rec.clip_x0 = 0;
            o->rec.clip_x1 = 8000;
            c->y = (float)y;
            o->rec.clip_y0 = 0;
            o->rec.clip_y1 = o->d[0x17];
            o->rec.mode = 3;
            if (ftol(y) > o->d[0x17] + 100) {
                if (i == 0) {
                    c->x = 650.0f;
                    c->y = 550.0f;
                    c->state = 2;
                    if (--g_bonus_count <= 0) g_end = 7;
                } else {
                    g_score += 1000;
                    c->state = 4;
                    sheep_out(i);
                    if (--left65 <= 0) g_end = 7;
                }
            }
        }
    }
    car_ranks(8, (int)exe_u32(0x4509d8));
}

static void lv65_init(void)
{
    /* 0x424aa0: eight cars two abreast (the shepherd's, quicker to respond, swapped to the last
       place), half a second apart; lane l's waypoints are path 1 from point l + 1 on */
    char_bank_load(0xc8, 0x450a68, 0x17, 0x450a90);
    int y = 0x1f4;
    for (int i = 0; i < 8; i++) {
        Car *c = &cars[i];
        Sheep *s = &g_sheep[i];
        memset(c, 0, sizeof *c);
        c->control = 3;
        int lane = i & 3;
        s->state = 100;
        s->x = (float)(lane * 0x38 + 0x186);
        s->y = (float)y;
        s->dir = 0;
        s->face = 0;
        c->x = s->x;
        c->y = s->y;
        c->lane = lane;
        c->max_turn = 0x34;
        c->top = 4.0f;
        if (i == 0) {
            cars[0].decay = 0.96f;
            cars[0].acc_max = 0.1f;
            cars[0].acc_step = 0.05f;
        } else {
            c->decay = 0.995f;
            c->acc_max = 0.09f;
            c->acc_step = 0.04f;
        }
        c->coast = 0.999f;
        c->state = 1;
        c->timer = i * 0x1e;
        y += 0x12;
    }
    for (int i = 8; i < MAX_SHEEP; i++) sheep_out(i);
    float t;
    t = cars[0].x; cars[0].x = cars[7].x; cars[7].x = t;
    t = cars[0].y; cars[0].y = cars[7].y; cars[7].y = t;
    int w = cars[0].timer; cars[0].timer = cars[7].timer; cars[7].timer = w;
    left65 = 7;
    cars[0].control = 0;
    int counts[4] = { 0 };
    for (int l = 0; l < 4; l++) {
        for (int k = l + 1; path_flag(1, k) != 2 && k < 1000; k++) {
            if (counts[l] < 64) {
                path_point(1, k, &wp_x[counts[l]][l], &wp_y[counts[l]][l]);
                counts[l]++;
            }
        }
    }
    g_hud_style = 7;
    wp_count65 = counts[0];
    g_bonus_count = 3;
}

static void lv65_tick(void)
{
    /* 0x424d50 */
    g_timer_on = 0;
    if (g_pad_x < -500) g_keyheld[DIK_LEFT]++;
    if (g_pad_x > 500) g_keyheld[DIK_RIGHT]++;
    if (g_pad_y < -500) g_keyheld[DIK_UP]++;
    if (g_pad_y > 500) g_keyheld[DIK_DOWN]++;
    cars65_tick();
    g_player.x = g_sheep[0].x;
    g_player.y = g_sheep[0].y;
    g_player.in_dx = -500;
    AnimObj *p = &g_anim[g_player.slot];
    p->rec.scale_x = 0;
    p->rec.scale_y = 0;
    g_anim[PLAYER_SLOT].rec.shadow = -1;
}

static void lv65_actor(int32_t *a)
{
    /* 0x4249c0: state 100, a dodgem drawn from its car (no shadow while sinking); the wheels
       turn with the speed */
    if (a[3] != 100) return;
    int i = a[9];
    Car *c = &cars[i % 10];
    AnimObj *o = &g_anim[i];
    Sheep *s = (Sheep *)a;
    if (c->state <= 0) return;
    if (c->state > 2) {
        if (c->state != 3) return;
        o->rec.shadow = -1;
    }
    s->dir = s->face = c->heading >> 4;
    s->x = c->x;
    s->y = c->y;
    o->type = 3;
    int anim = i == 0 ? 0x64 : 0x31;
    s->anim[0] = s->anim[1] = s->anim[2] = anim;
    o->d[0x15] = c->rank + 1;
    o->d[0x16] = 4 - c->laps;
    if (o->d[0x16] < 0) o->d[0x16] = 0;
    o->countdown += 1 - ftol((double)c->vel[sector(c)]);
    if (o->countdown <= 0) o->countdown = 0;
}

/* ================================================================ dispatch */

bool cars_actor(uint32_t va, int32_t *actor)
{
    switch (va) {
    case 0x429a00: lv55_actor(actor); return true;
    case 0x4249c0: lv65_actor(actor); return true;
    default: return false;
    }
}

bool cars_call(uint32_t va)
{
    switch (va) {
    case 0x429c00: lv55_init(); return true;
    case 0x429e70: lv55_tick(); return true;
    case 0x424aa0: lv65_init(); return true;
    case 0x424d50: lv65_tick(); return true;
    default: return false;
    }
}
