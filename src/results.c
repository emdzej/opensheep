/* The screen after a level (0x40ff30 sets it up, 0x410bc0 / 0x410590 run it, 0x4107c0 / 0x410440 draw
   it): the panels slide in one by one, the bonuses count into the score, the flock dances, then
   replay / continue / save. A level lost shows its own short screen. */
#include "results.h"
#include "flow.h"
#include "game.h"
#include "hud.h"
#include "input.h"
#include "levels_int.h"
#include "objects.h"
#include "play.h"
#include "player.h"
#include "rng.h"
#include "sprites.h"
#include "surface.h"
#include <stdio.h>
#include <string.h>

/* The panel tables, {x, y, cur_x, cur_y, strid} each, in the exe: 0x44c2d0 the success screen's ten
   (title, time, time bonus, score, the flock, flock bonus, total, the three options), 0x44c3b0 the
   failure screen's four (title, score, the reason, press a key). The screens move cur_x / cur_y. */
enum { WIN = 0x44c2d0, FAIL = 0x44c3b0, REC = 0x14 };
enum { P_TITLE, P_TIME, P_TBONUS, P_SCORE, P_FLOCK, P_FBONUS, P_TOTAL, P_OPT };
enum { F_TITLE, F_SCORE, F_WHY, F_KEY };

static int32_t get(uint32_t table, int i, int f) { return exe_s32(table + REC * (uint32_t)i + 4 * (uint32_t)f); }
static void put(uint32_t table, int i, int f, int32_t v)
{
    memcpy(exe_mut(table + REC * (uint32_t)i + 4 * (uint32_t)f, 4), &v, 4);
}
#define X(t, i) get(t, i, 0)
#define Y(t, i) get(t, i, 1)
#define CX(t, i, v) put(t, i, 2, v)
#define CY(t, i, v) put(t, i, 3, v)

static int end_code;         /* 0x4b9118 */
static int mult;             /* 0x4b9620: difficulty + 1 */
static int nshown;           /* 0x4b9634: the sheep on the screen (4 breeds in world 7) */
static int tbonus;           /* 0x4b9658: the time bonus still to count */
static int stage;            /* 0x4b9648 */
static int timer;            /* 0x4b9110 */
static int slide;            /* 0x4b963c: 0 sliding in, 1 wobbling, 2 still */
static int skip;             /* 0x4b90f4: a key went down: finish the stage */
static int score_left;       /* 0x4b9630: the level's score still to count */
static int fbonus;           /* 0x4b9100: the flock bonus still to count */
static int tbonus0, score0, fbonus0;   /* 0x4b9114, 0x4b9654, 0x4b9670 */
static int w7_count[4];      /* 0x4b9660: world 7's sheep counted, per breed */
static int phase, amp;       /* 0x4b9108, 0x4b9674 */
static int tick6;            /* 0x4b9624: a counting sound every 6 */
static Surface *back;        /* 0x4b90fc */

static void sound(int n)
{
    /* 0x40fef0: a bonus sound at full volume, unless skipping. TODO: the mixer (0x41f3a0, 0x4376b0) */
    (void)n;
}

static int *target_anim(int i) { return &g_sheep[i].d[0x38 / 4]; }   /* 0x4b99f8 */

void results_start(int end)
{
    /* 0x40ff30 */
    end_code = end;
    uint32_t why = FAIL + REC * F_WHY + 0x10;
    if (end == 3) memcpy(exe_mut(why, 4), &(int32_t){ 0xca }, 4);
    else if (end == 4) memcpy(exe_mut(why, 4), &(int32_t){ 0xcb }, 4);
    else if (end == 5) memcpy(exe_mut(why, 4), &(int32_t){ 0xcc }, 4);
    mult = g_difficulty + 1;
    nshown = g_saved;
    tbonus = (g_tim_easy - g_clock) / 60 * mult * 10;
    stage = 0;
    timer = 600;
    slide = 0;
    skip = 0;
    g_4b911c = 1;
    if (g_level != 5) g_prog.u84 += g_clock;
    if (g_world == 9) g_prog.u88 = (int8_t)(g_prog.u88 + (int8_t)g_saved);
    score_left = g_score;
    if (g_world == 7) {
        fbonus = mult * 5000;
        for (int b = 0; b < 4; b++)
            if (g_w7_total[b] != g_w7_penned[b]) fbonus = 0;
    } else {
        fbonus = g_nsheep == g_saved ? mult * 5000 : 0;
    }
    /* every panel off the screen, up to the record with no string */
    for (uint32_t t = WIN; get(t, 0, 4); t += REC) { put(t, 0, 2, -100); put(t, 0, 3, -100); }
    for (uint32_t t = FAIL; get(t, 0, 4); t += REC) { put(t, 0, 2, -100); put(t, 0, 3, -100); }
    if (g_world == 7) {
        int breed = g_breed;
        for (int i = 0; i < 4; i++) {
            w7_count[i] = 0;
            g_breed = i;
            bank_entry_load(0x44a1f0, 0x32 + i);
            anim_init(2, i, 0x51, 8);
            g_anim[i].x = 0x168 + 0x40 * i;
            g_anim[i].y = 0xb9;
            g_anim[i].rec.scale_x = g_anim[i].rec.scale_y = 0;
        }
        nshown = 4;
        g_breed = breed;
    } else {
        int x = 0x168, y = 0xb9, col = 0;
        bank_entry_load(0x44a1f0, 0x36);
        for (int i = 0; i < MAX_SHEEP; i++) {
            if (i >= nshown) {
                g_anim[i].state = 0;
                continue;
            }
            anim_init(2, i, 3, 8);
            g_anim[i].x = x;
            g_anim[i].y = y;
            g_anim[i].rec.scale_x = g_anim[i].rec.scale_y = 0;
            x += 0x18;
            if (++col > 9) {
                col = 0;
                x = 0x168;
                y += 0x1e;
            }
        }
    }
    tbonus0 = tbonus;
    score0 = score_left;
    fbonus0 = fbonus;
    char path[40];
    snprintf(path, sizeof path, "%s", exe_str(0x44c414));
    path[0x16] = (char)('0' + g_world);
    surface_free(back);
    back = surface_load_bmp(path);
    /* TODO: the bonus sounds, data\misc\bonus.slf (0x41ea60) */
}

static void wobble(void)
{
    /* 0x4102a0: slide in, then a damped sine about the panel's place */
    if (slide == 0) {
        timer -= 0x14;
        if (timer < 1) {
            phase = 0;
            amp = 0x46;
            slide = 1;
        }
    } else if (slide == 1) {
        amp -= 2;
        if (amp < 1) slide = 2;
        phase = (phase + 0x10) & 0xff;
        timer = -(g_isin[phase] * amp / 0x8000);
    }
}

static bool settled(void) { return slide == 2 || skip; }

static void next_stage(void)
{
    slide = 0;
    stage++;
}

/* Count n into the score, 10 a tick (1 below 11), a sound every 6 ticks; true once it's empty. */
static bool count(int *n)
{
    if (*n < 1) return true;
    int step = *n < 11 ? 1 : 10;
    *n -= step;
    g_prog.score += step;
    if (++tick6 > 5) {
        tick6 = 0;
        sound(3);
    }
    return false;
}

static void win_tick(void)
{
    /* one pass of 0x410bc0's tick loop */
    switch (stage) {
    case 0:
        if (timer == 0x244) { sound(0); sound(1); }
        CX(WIN, P_TITLE, X(WIN, P_TITLE) - timer);
        CY(WIN, P_TITLE, Y(WIN, P_TITLE));
        CX(WIN, P_TOTAL, timer + X(WIN, P_TOTAL));
        CY(WIN, P_TOTAL, Y(WIN, P_TOTAL));
        if (settled()) {
            CX(WIN, P_TITLE, X(WIN, P_TITLE));
            CX(WIN, P_TOTAL, X(WIN, P_TOTAL));
            timer = 400;
            sound(2);
            next_stage();
        }
        break;
    case 1:
        /* a bonus level skips to the score panel, still sliding the time in this tick */
        if (end_code == 7) stage = 4;
        CX(WIN, P_TIME, X(WIN, P_TIME));
        CY(WIN, P_TIME, Y(WIN, P_TIME) - timer);
        if (settled()) {
            CY(WIN, P_TIME, Y(WIN, P_TIME));
            timer = 400;
            sound(2);
            next_stage();
        }
        break;
    case 2:
        CX(WIN, P_TBONUS, X(WIN, P_TBONUS));
        CY(WIN, P_TBONUS, Y(WIN, P_TBONUS) - timer);
        if (settled()) {
            timer = 400;
            CY(WIN, P_TBONUS, Y(WIN, P_TBONUS));
            next_stage();
        }
        break;
    case 3:
        if (count(&tbonus)) {
            timer = 400;
            sound(2);
            stage++;
            slide = 0;
        }
        if (skip) {
            /* after an empty count this is a second step on: the score panel is passed over */
            g_prog.score += tbonus;
            tbonus = 0;
            timer = 400;
            next_stage();
        }
        break;
    case 4:
        CX(WIN, P_SCORE, X(WIN, P_SCORE));
        CY(WIN, P_SCORE, Y(WIN, P_SCORE) - timer);
        if (settled()) {
            timer = 400;
            CY(WIN, P_SCORE, Y(WIN, P_SCORE));
            next_stage();
        }
        break;
    case 5:
        if (count(&score_left)) {
            timer = 400;
            sound(0);
            slide = 0;
            stage = 6;
        }
        if (skip) {
            g_prog.score += score_left;
            score_left = 0;
            timer = 400;
            slide = 0;
            stage = 6;
        }
        break;
    case 6:
        if (end_code == 7) {
            stage = 13;
            slide = 2;
        } else {
            CX(WIN, P_FLOCK, X(WIN, P_FLOCK) - timer);
            CY(WIN, P_FLOCK, Y(WIN, P_FLOCK));
            if (settled()) {
                timer = 400;
                CX(WIN, P_FLOCK, X(WIN, P_FLOCK));
                next_stage();
            }
        }
        break;
    case 7:
        timer = 0;
        stage++;
        slide = 2;
        break;
    case 8:
        /* the flock grows in */
        timer += 0x10;
        if (skip) timer = 0x100;
        for (int i = 0; i < nshown; i++) {
            g_anim[i].rec.scale_x = g_anim[i].rec.scale_y = timer;
            *target_anim(i) = g_world == 7 ? i + 0x51 : 3;
        }
        if (timer > 0xff) {
            timer = 0;
            stage++;
        }
        break;
    case 9:
        if (g_theme == 7) {
            /* a sheep of each breed off its pen's count every 16 ticks */
            int empty = 0;
            if ((timer & 0xf) == 0) {
                for (int b = 0; b < 4; b++) {
                    if (g_w7_penned[b] < 1) {
                        empty++;
                    } else {
                        g_w7_penned[b]--;
                        w7_count[b]++;
                        g_prog.score += mult * 100;
                    }
                }
                if (empty < 4) sound((crt_rand() & 3) + 4);
            }
            timer++;
            if (skip) {
                for (int b = 0; b < 4; b++) {
                    w7_count[b] += g_w7_penned[b];
                    g_prog.score += g_w7_penned[b] * mult * 100;
                }
                stage++;
                break;
            }
            if (empty >= 4) stage++;
        } else {
            /* a sheep starts dancing every 16 ticks (the first even with none: 100 points anyway) */
            if ((timer & 0xf) == 0) {
                *target_anim(timer / 16) = 0x15;
                g_prog.score += mult * 100;
                sound((crt_rand() & 3) + 4);
            }
            timer++;
            if (skip) {
                for (int i = 0; i < nshown; i++)
                    if (*target_anim(i) != 0x15) {
                        *target_anim(i) = 0x15;
                        g_prog.score += mult * 100;
                    }
                timer = nshown << 4;
            }
            if (timer / 16 >= nshown) stage++;
        }
        break;
    case 10:
        timer = 400;
        sound(0);
        next_stage();
        break;
    case 11:
        CX(WIN, P_FBONUS, X(WIN, P_FBONUS) - timer);
        CY(WIN, P_FBONUS, Y(WIN, P_FBONUS));
        if (settled()) {
            CX(WIN, P_FBONUS, X(WIN, P_FBONUS));
            stage++;
            timer = 400;
        }
        break;
    case 12:
        if (fbonus < 1) {
            stage = 13;
        } else {
            fbonus -= 10;
            g_prog.score += 10;
            if (++tick6 > 5) {
                tick6 = 0;
                sound(3);
            }
        }
        if (skip) {
            g_prog.score += fbonus;
            fbonus = 0;
            slide = 2;
            skip = 0;
            stage = 13;
        }
        break;
    case 13:
        timer++;
        if ((timer & 0x10) && (timer & 0xf) == 0) sound(8);
        if (g_game_mode == 0 || g_game_mode == 2) {
            for (int k = 0; k < 3; k++) {
                CX(WIN, P_OPT + k, X(WIN, P_OPT + k));
                CY(WIN, P_OPT + k, Y(WIN, P_OPT + k));
            }
            if (g_lang == 4) {
                CX(WIN, P_OPT, X(WIN, P_OPT) + 2);
                CX(WIN, P_OPT + 1, X(WIN, P_OPT + 1) + 0x10);
            }
        } else {
            /* a level for its best time: continue only */
            CY(WIN, P_OPT, -100);
            CY(WIN, P_OPT + 2, -100);
            CX(WIN, P_OPT + 1, X(WIN, P_OPT + 1));
            CY(WIN, P_OPT + 1, Y(WIN, P_OPT + 1));
        }
        break;
    }
    for (int i = 0; i < nshown; i++) {
        AnimObj *a = &g_anim[i];
        if (a->anim != *target_anim(i)) anim_set(i, *target_anim(i), 8);
        if (a->anim == 0x15 && a->state == 2) {
            *target_anim(i) = 0x16;
            anim_set(i, 0x16, 8);
        }
        anim_step(i, 8, 1);
    }
    if (stage == 13) {
        if (g_game_mode == 0 || g_game_mode == 2) {
            if (g_keyheld[DIK_LEFT] == 1 && g_4b911c > 0) g_4b911c--;
            if (g_keyheld[DIK_RIGHT] == 1 && g_4b911c < 2) g_4b911c++;
        }
        if (flow_any_key() && !g_keyheld[DIK_LEFT] && !g_keyheld[DIK_RIGHT] && !g_keyheld[DIK_UP] &&
            !g_keyheld[DIK_DOWN])
            g_app_sub = 1;
    } else if (flow_any_key()) {
        skip = 1;
    }
}

static void fail_tick(void)
{
    /* one pass of 0x410590's tick loop */
    if (stage == 0) {
        if (timer == 0x244) { sound(0); sound(1); }
        CX(FAIL, F_TITLE, X(FAIL, F_TITLE) - timer);
        CY(FAIL, F_TITLE, Y(FAIL, F_TITLE));
        CX(FAIL, F_SCORE, timer + X(FAIL, F_SCORE));
        CY(FAIL, F_SCORE, Y(FAIL, F_SCORE));
        if (settled()) {
            CX(FAIL, F_TITLE, X(FAIL, F_TITLE));
            CX(FAIL, F_SCORE, X(FAIL, F_SCORE));
            timer = 400;
            sound(2);
            next_stage();
        }
    } else if (stage == 1) {
        CX(FAIL, F_WHY, X(FAIL, F_WHY));
        CY(FAIL, F_WHY, Y(FAIL, F_WHY) - timer);
        if (settled()) {
            CY(FAIL, F_WHY, Y(FAIL, F_WHY));
            timer = 0;
            stage = 2;
        }
    } else if (stage == 2) {
        timer++;
        if (!(timer & 0x10)) {
            CY(FAIL, F_KEY, -100);
        } else {
            CX(FAIL, F_KEY, X(FAIL, F_KEY));
            CY(FAIL, F_KEY, Y(FAIL, F_KEY));
            if ((timer & 0xf) == 0) sound(8);
        }
    }
    if (flow_any_key()) {
        if (stage == 2) g_app_sub = 1;
        else skip = 1;
    }
}

/* The label of panel i (its string as the format, or "$%s" / "%s" around it). */
static int label(uint16_t *buf, size_t cap, uint32_t table, int i, uint32_t fmt_va)
{
    const uint16_t *s = str_get(get(table, i, 4));
    if (!fmt_va) return wfmt(buf, cap, s);
    uint16_t fmt[8];
    return wfmt(buf, cap, exe_wtext(fmt_va, fmt, 8), s);
}

static int with_number(uint16_t *buf, size_t cap, int n, int v)
{
    uint16_t fmt[8];
    return wfmt(buf + n, cap - (size_t)n, exe_wtext(0x44c510, fmt, 8), v);
}

static void win_draw(Framebuffer *fb)
{
    /* 0x4107c0 */
    uint16_t buf[64], fmt[24];
    int n;
    if (back) surface_blt_fast(fb, 0, 0, back, NULL);
    sprite_target(fb);
    n = label(buf, 64, WIN, P_TITLE, 0);
    text_draw(buf, get(WIN, P_TITLE, 2) - n * 15 / 2, get(WIN, P_TITLE, 3));
    n = label(buf, 64, WIN, P_TIME, 0x44bb80);
    wfmt(buf + n, 64 - (size_t)n, exe_wtext(0x44c524, fmt, 24), g_clock / 3600, g_clock % 3600 / 60,
         g_clock / 6 % 10 * 10);
    text_draw(buf, get(WIN, P_TIME, 2) - n * 15 + 15, get(WIN, P_TIME, 3));
    n = label(buf, 64, WIN, P_TBONUS, 0x44bb80);
    with_number(buf, 64, n, tbonus0 - tbonus);
    text_draw(buf, get(WIN, P_TBONUS, 2) - n * 15 + 15, get(WIN, P_TBONUS, 3));
    n = label(buf, 64, WIN, P_SCORE, 0x44bb80);
    with_number(buf, 64, n, score0 - score_left);
    text_draw(buf, get(WIN, P_SCORE, 2) - n * 15 + 15, get(WIN, P_SCORE, 3));
    n = label(buf, 64, WIN, P_FLOCK, 0x44bb80);
    text_draw(buf, get(WIN, P_FLOCK, 2) - (n * 15 - 15), get(WIN, P_FLOCK, 3));
    n = label(buf, 64, WIN, P_FBONUS, 0x44bb80);
    with_number(buf, 64, n, fbonus0 - fbonus);
    text_draw(buf, get(WIN, P_FBONUS, 2) - n * 15 + 15, get(WIN, P_FBONUS, 3));
    n = label(buf, 64, WIN, P_TOTAL, 0);
    with_number(buf, 64, n, g_prog.score);
    text_draw(buf, get(WIN, P_TOTAL, 2) - n * 15, get(WIN, P_TOTAL, 3));
    if (stage > 8 && g_world == 7)
        for (int b = 0; b < 4; b++) {
            wfmt(buf, 64, exe_wtext(0x44c51c, fmt, 24), w7_count[b]);
            text_draw(buf, 0x15e + 0x40 * b, 0xc3);
        }
    for (int k = 0; k < 3; k++) {
        /* the chosen option blinks blue */
        if (!(timer & 0x10) || g_4b911c != k) n = label(buf, 64, WIN, P_OPT + k, 0x44bb78);
        else n = label(buf, 64, WIN, P_OPT + k, 0x44bb80) - 1;
        int x = get(WIN, P_OPT + k, 2) - n * 15 / 2;
        text_draw(buf, x < 0 ? 0 : x, get(WIN, P_OPT + k, 3));
    }
    for (int i = 0; i < nshown; i++) {
        const AnimObj *a = &g_anim[i];
        sprite_draw(a->x + a->rec.hot_x, a->y + a->rec.hot_y, a->rec.bank, a->rec.frame, a->rec.scale_x,
                    a->rec.scale_y);
    }
}

static void fail_draw(Framebuffer *fb)
{
    /* 0x410440 */
    uint16_t buf[64];
    int n;
    if (back) surface_blt_fast(fb, 0, 0, back, NULL);
    sprite_target(fb);
    n = label(buf, 64, FAIL, F_TITLE, 0);
    text_draw(buf, get(FAIL, F_TITLE, 2) - n * 15 / 2, get(FAIL, F_TITLE, 3));
    n = label(buf, 64, FAIL, F_SCORE, 0);
    with_number(buf, 64, n, g_prog.score);
    text_draw(buf, get(FAIL, F_SCORE, 2) - n * 15, get(FAIL, F_SCORE, 3));
    n = label(buf, 64, FAIL, F_WHY, 0x44bb80);
    text_draw(buf, get(FAIL, F_WHY, 2) - n * 15 / 2, get(FAIL, F_WHY, 3));
    n = label(buf, 64, FAIL, F_KEY, 0);
    text_draw(buf, get(FAIL, F_KEY, 2) - n * 15 / 2, get(FAIL, F_KEY, 3));
}

void results_frame(Framebuffer *fb)
{
    /* 0x410bc0 / 0x410590, one tick a frame */
    bool won = end_code == 1 || end_code == 7;
    input_keys_tick();
    input_map();
    if (won) input_pad_keys();
    wobble();
    if (won) win_tick();
    else fail_tick();
    /* TODO: the music (0x419120) */
    memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
    if (won) win_draw(fb);
    else fail_draw(fb);
    if (g_app_sub == 1) {
        /* 0x436cd0 */
        surface_free(back);
        back = NULL;
    }
}
