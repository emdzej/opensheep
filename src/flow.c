#include "flow.h"
#include "cheats.h"
#include "exe.h"
#include "font.h"
#include "hud.h"
#include "input.h"
#include "level.h"
#include "levels.h"
#include "moves.h"
#include "objects.h"
#include "play.h"
#include "results.h"
#include "hiscore.h"
#include "hint.h"
#include "pause.h"
#include "credits.h"
#include "player.h"
#include "sprites.h"
#include "movie.h"
#include "sound.h"
#include "surface.h"
#include "world.h"
#include <stdio.h>
#include <string.h>

int g_app_state, g_app_sub;
int g_game_mode;
int g_next_world, g_next_level;
int g_4b911c;
Progress g_prog_loaded, g_prog_level;
int g_hiscore_mark = -1;
bool g_quit;

static int key_latch = 1;           /* 0x450608: set when a screen starts, so a key still held is no press */
static int load_blink;              /* 0x4a3b54 */
static int tick_rest;               /* 0x4a19d8: the ms short of a tick */
static unsigned sound_phase;        /* 0x4b10d0 */
static Surface *load_pic;           /* 0x4b0f48 */
static uint16_t load_bar[400];      /* 0x4ae104 */
enum { LOAD_STAGE = 5 };            /* 0x4ae0ac when 0x406cc0 is done */
static int splash_ticks;            /* 0x4adeb8 */
static Surface *splash;             /* 0x4b90fc */
static int saved_world, saved_level;   /* 0x4adefc, 0x4b0b54: the level to go back to after saving */

static int bits4(int v)
{
    /* 0x4094b0 */
    int n = 0;
    for (int i = 0; i < 4; i++, v >>= 1) n += v & 1;
    return n;
}

static int bit(int i) { return (int)exe_u32(0x44b198 + 4 * (uint32_t)i); }

bool flow_any_key(void)
{
    /* 0x422930: a key, a pad button or the mouse button that went down */
    if (g_input_mode != 2 && g_input_mode != 3) g_pad_buttons = 0;
    bool down = g_mouse_button || g_pad_buttons || g_last_key;
    if (!key_latch) {
        if (down) {
            key_latch = 1;
            return true;
        }
    } else if (!down) {
        key_latch = 0;
    }
    return false;
}

void flow_latch_key(void) { key_latch = 1; }

/* 0x4094d0: the levels 1-4 of a world in this build. */
static int levels_in(int world)
{
    int n = 0;
    for (int l = 1; l <= 4; l++) {
        uint32_t d = world_desc_of(world, l);
        if (d && exe_u32(d + 4)) n++;
    }
    return n;
}

/* 0x409500: the next world with levels, -1 after world 7. */
static int next_world(int w)
{
    for (w++; w <= 7; w++)
        if (levels_in(w) > 0) return w;
    return -1;
}

static void movie(const char *path)
{
    key_latch = 1;
    movie_play(path);
}

void flow_world_movie(int world)
{
    /* "data\biks\World01.bik" (0x44b218) with the world's digit at [0x10] */
    char path[32];
    snprintf(path, sizeof path, "%s", exe_str(0x44b218));
    if (strlen(path) > 0x10) path[0x10] = (char)('0' + world);
    movie(path);
}

static void go(int state, int world, int level)
{
    g_app_state = state;
    g_app_sub = 0;
    g_world = world;
    g_level = level;
}

static void after_level(void)
{
    /* 0x409530: where the game goes after a level, a save or the best results */
    if (g_theme == 9) g_prog.bonus = 0;
    if (g_game_mode != 1) {
        if (g_end == 8) {
            /* back from the save page to the level it was saved after */
            g_world = saved_world;
            g_level = saved_level;
            g_theme = (int8_t)saved_world;
            g_end = 1;
            if (g_world == 7) {
                g_app_state = APP_RESULTS;
                g_app_sub = 1;
            }
        } else if (g_end == 9) {
            /* a loaded game: its world's map, or its last level once the bonus is done */
            g_next_world = g_theme;
            g_level = 0;
            g_end = 1;
            g_world = g_next_world;
            if (g_prog.levels & 0x10) {
                g_level = 5;
                g_end = 7;
            }
        }
        if (g_4b911c == 0) {
            /* replay: the level again from its start */
            g_app_state = APP_LEVEL;
            g_prog = g_prog_level;
            g_app_sub = 0;
            return;
        }
        if (g_4b911c == 2) {
            /* save: the save page (world 0 level 1), then back here */
            g_app_sub = 0;
            g_app_state = 8;
            /* (0x4a3d78 keeps g_end; nothing reads it) */
            saved_world = g_theme;
            saved_level = g_level;
            g_world = 0;
            g_level = 1;
            return;
        }
        if (g_end != 1 && g_end != 7) { go(APP_MENU, 0, 0); return; }
        if (g_world == 7) {
            flow_world_movie(7);
            g_next_world = 0;
            go(APP_MENU, 0, 0);
            return;
        }
        if (g_level == 5) {
            /* the world's last level done: on to the next world's map */
            flow_world_movie(g_theme);
            g_theme++;
            g_prog.levels = g_prog.breeds = g_prog.bonus = 0;
            g_next_world = g_theme;
            go(9, g_theme, 0);
            return;
        }
        int n = levels_in(g_theme);
        uint32_t last = world_desc_of(g_theme, 5);
        bool has_last = last && exe_u32(last + 4);
        if (bits4(g_prog.levels) != n) {
            g_level = 0;
            g_app_state = 9;
            g_app_sub = 0;
            return;
        }
        if (has_last && bits4(g_prog.bonus) == n) {
            g_app_sub = 0;
            g_app_state = APP_LEVEL;
            g_level = 5;
            return;
        }
        if (g_game_mode != 2) {
            flow_world_movie(g_theme);
            int w = next_world(g_theme);
            g_theme = (int8_t)w;
            if (w != -1) {
                g_prog.levels = g_prog.breeds = g_prog.bonus = 0;
                g_next_world = w;
                go(9, w, 0);
                return;
            }
            g_theme = 0;
        }
    }
    go(APP_MENU, 0, 0);
}

static void results_end(void)
{
    /* App_Frame state 10 once the screen is done: the flock left, the level and breed done */
    g_hiscore_mark = -1;
    if (g_level != 5 && g_world != 7) {
        g_flock_size[g_breed & 3] = (int8_t)g_saved;
        g_prog.breeds = (uint8_t)(g_prog.breeds | bit(g_breed & 3));
    }
    g_prog.levels = (uint8_t)(g_prog.levels | exe_u32(0x44b194 + 4 * (uint32_t)g_level));
    /* a game's score goes in the table when it ends: lost, or world 7 won and continued; a level
       played for its best, whenever it's won */
    bool best = g_game_mode == 1;
    if (g_world == 7 && g_4b911c == 1 && g_end == 1) {
        flow_world_movie(g_theme);
        best = true;
    }
    if (g_end != 1 && g_end != 7) best = true;
    if (best) g_hiscore_mark = hiscore_insert(g_world, g_level, g_game_mode, g_prog.score, g_prog.u84);
    if (g_world == 7) {
        if (g_4b911c == 1 && g_end == 1) {
            g_app_state = APP_HISCORES;
            g_app_sub = 0;
        } else {
            after_level();
        }
    } else if (g_hiscore_mark == -1) {
        after_level();
    } else {
        g_app_state = APP_HISCORES;
        g_app_sub = 0;
    }
}

/* 0x409d70(0): the two 200-step gradients of the bar (0x4ae104), between the rgb triples at 0x44b334 */
static void loading_gradient(void)
{
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 200; i++) {
            uint32_t a = 0x44b334 + 12 * (uint32_t)k, b = a + 12;
            int r = (exe_s32(b) - exe_s32(a)) * i / 200 + exe_s32(a);
            int g = (exe_s32(b + 4) - exe_s32(a + 4)) * i / 200 + exe_s32(a + 4);
            int bl = (exe_s32(b + 8) - exe_s32(a + 8)) * i / 200 + exe_s32(a + 8);
            load_bar[200 * k + i] = (uint16_t)((r & 0xf8) << 8 | (g & 0xfc) << 3 | bl >> 3);
        }
}

static void loading_start(void)
{
    /* 0x409d70(0): the picture is the level's .tgb */
    loading_gradient();
    surface_free(load_pic);
    load_pic = surface_load_tga(world_level_path(exe_str(0x44a0b0)));
    if (!load_pic) fprintf(stderr, "Cannot load %s\n", world_level_path(exe_str(0x44a0b0)));
}

static void loading_end(void)
{
    /* 0x409d70(4) */
    surface_free(load_pic);
    load_pic = NULL;
}

static bool level_begin(void)
{
    /* sub-state 0 of the level states: 0x409d70(0) selects the level, 0x406cc0(1) loads it */
    if (!world_select(g_world, g_level)) {
        fprintf(stderr, "no level %d.%d\n", g_world, g_level);
        return false;
    }
    g_hud_style = g_level == 0 ? 0 : g_world != 9 ? 1 : 5;
    loading_start();
    if (g_sound_fx) {
        /* 0x406cc0 */
        sound_stop_streams(-1);
        sound_stop_sfx(-1);
        sound_tick();
    }
    level_hook(DESC_RELEASE);
    if (!level_load_resources()) return false;
    level_start();
    if (g_level != 0) g_prog_level = g_prog;
    return true;
}

void flow_level_reload(void)
{
    level_hook(DESC_RELEASE);
    if (level_load_resources()) level_start();
}

/* A new game's progress: the flock of 10 a breed (20 on the hardest), nothing done. */
static void new_progress(void)
{
    memset(g_prog.worlds, 0, sizeof g_prog.worlds);
    g_prog.score = 0;
    g_prog.breeds = g_prog.levels = g_prog.bonus = 0;
    int8_t n = g_difficulty > 1 ? 0x14 : 10;
    for (int b = 0; b < 4; b++) g_flock_size[b] = n;
}

void flow_new_game(void)
{
    /* 0x40f2a0 */
    g_next_world = g_next_level / 6;
    g_next_level %= 6;
    g_pause = 0;
    g_app_state = APP_LEVEL;
    g_app_sub = 0;
    g_theme = (int8_t)g_next_world;
    new_progress();
    g_world = g_next_world;
    g_level = g_next_level;
    input_keys_tick();
}

static void text_centred(int y, const uint16_t *s, uint32_t rgb)
{
    font_draw(0x140 - font_width(s, FONT_BIG) / 2, y, s, FONT_BIG, rgb);
}

static void loading_texts(void)
{
    /* 0x409990: the world's and the level's names, then the bonus level's three lines or the target */
    if (g_world == 0 || g_level == 0) return;
    uint16_t buf[0x100], fmt[0x20];
    int id = g_level + g_world * 6;
    text_centred(0x14, str_get(g_world + 0x59), 0xffffffff);
    text_centred(0x3c, str_get(exe_s16(0x450bd0 + 2 * (uint32_t)id)), 0xffffffff);
    if (g_level == 5) {
        for (int i = 0; i < 3; i++) {
            wfmt(buf, 0x100, str_get(g_world * 5 + 0x11d + i));
            text_centred(0x172 + 0x14 * i, buf, 0xffffffff);
        }
        return;
    }
    level_load_tim(g_world, g_level);
    if (g_world == 9) return;
    if (g_difficulty == 0) g_needed = 5, g_time_limit = g_tim_easy;
    else if (g_difficulty == 1) g_needed = 10, g_time_limit = g_tim_normal;
    else if (g_difficulty == 2) g_needed = 0x14, g_time_limit = g_tim_hard;
    wfmt(buf, 0x100, exe_wtext(0x44b808, fmt, 0x20), str_get(0x71), g_needed, str_get(0x72), str_get(0x73),
         g_time_limit / 0xe10, g_time_limit % 0xe10 / 0x3c);
    text_centred(0x186, buf, 0xffffffff);
}

static void loading_draw(Framebuffer *fb)
{
    /* 0x409d70(2) */
    memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
    sprite_target(fb);
    /* 0x409020: the picture over the whole screen, the camera at 0 */
    if (load_pic)
        for (int y = 0; y < 0x1e0 && y < load_pic->h; y++)
            memcpy(fb->pixels + (size_t)y * fb->pitch, load_pic->px + (size_t)y * load_pic->w,
                   2 * (size_t)(load_pic->w < 0x280 ? load_pic->w : 0x280));
    g_lv.cam_x = g_lv.cam_y = g_lv.fine_x = g_lv.fine_y = 0;
    loading_texts();
    /* the bar's white frame, 404 x 27 at (118, 448) */
    uint16_t *top = fb->pixels + (size_t)0x1c0 * fb->pitch + 0x76;
    for (int x = 0; x < 0x194; x++) top[x] = top[x + 0x1b * fb->pitch] = 0xffff;
    for (int y = 0; y < 0x1b; y++) top[y * fb->pitch] = top[y * fb->pitch + 0x193] = 0xffff;
    /* 0x409d10: its fill, 4 pixels a percent, the stage's share of the load done (0x44b304). Here the
       load is over in one frame, at the last stage (5: 100%) */
    int done = exe_s32(0x44b308 + 8 * LOAD_STAGE) * 0 / 100 + exe_s32(0x44b304 + 8 * LOAD_STAGE);
    for (int y = 0; y < 0x18; y++)
        memcpy(fb->pixels + (size_t)(0x1c2 + y) * fb->pitch + 0x78, load_bar, 2 * (size_t)(done * 4));
    if (g_demo != 2 && g_world != 0 && g_level != 0 && !(++load_blink & 0x10)) {
        const uint16_t *s = str_get(0x74);
        int w = font_width(s, FONT_BIG);
        if (w > 0x280) w = 0x280;
        font_draw(0x140 - w / 2, 0x1bc, s, FONT_BIG, 0);
    }
}

static void level_frame(Framebuffer *fb)
{
    /* Level_Frame 0x40b9b0: as many ticks as fit 0x44b398 ms each in the time since the last frame, at
       most 6, then the draw. A frame here is 16 ms, so one tick a frame unless TURBOGAME (8 ms) or
       SNAILGAME (32 ms) changed it */
    int ms = exe_s32(0x44b398);
    tick_rest += 16;
    int n = tick_rest / ms;
    tick_rest %= ms;
    if (n > 6) n = 6;
    bool ticked = n > 0;
    while (n-- > 0) play_tick();
    if (ticked && sound_phase++ & 1) sound_tick();     /* every other frame that ticked */
    memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
    play_draw(fb);
}

/* After a level and before a screen, with SOUND_FX: everything stops and is freed. */
static void sound_release(void)
{
    if (!g_sound_fx) return;
    sound_stop_sfx(-1);
    sound_stop_streams(-1);
    sound_tick();
    sound_free_all();
}

static void level_end(void)
{
    /* the level is over: free it and act on its end code */
    level_release();
    sound_release();
    switch (g_end) {
    case 1: case 3: case 4: case 5: case 7:
        if (g_theme == 9) g_end = 1;
        if (g_theme == 9 || g_end == 1 || g_end == 7) g_prog.worlds[g_theme] |= (uint8_t)bit(g_level);
        results_start(g_end);
        g_app_state = APP_RESULTS;
        g_app_sub = 0;
        break;
    case 2:
        g_app_state = APP_NEXT;
        g_app_sub = 0;
        break;
    case 6:
        go(APP_MENU, 0, 0);
        break;
    case 8: case 9:
        after_level();
        break;
    case 10:
        g_hiscore_mark = -1;
        g_app_state = APP_HISCORES;
        g_app_sub = 0;
        break;
    }
}

/* Level_ReleaseTiles 0x4077b0 and 0x40d030 before a screen: the level's tiles, animations and every
   sprite bank, and the sound. */
static void leave_level(void)
{
    level_release();
    sound_release();
    anims_free();
    g_hint_shown = 0;
    hint_text_free();
    sprite_banks_free_all();
}

static void splash_frame(Framebuffer *fb)
{
    /* App_Frame 0x40a170, states 13 to 17 */
    int st = g_app_state;
    if (st >= APP_END3) {
        input_keys_tick();
        input_map();
    }
    if (g_app_sub == 0) {
        static const uint32_t pic[] = { 0x44b948, 0x44b928, 0x44b908, 0x44b8dc, 0x44b908 };
        if (st != APP_END4) leave_level();
        surface_free(splash);                /* (the original drops the last one) */
        splash = surface_load_bmp(exe_str(pic[st - APP_END1]));
        splash_ticks = 600;
        g_app_sub = 1;
        return;
    }
    if (g_app_sub != 1) return;
    if (splash) surface_blt_fast(fb, 0, 0, splash, NULL);
    bool over = splash_ticks-- < 1;
    if (st >= APP_END3 && !over) over = flow_any_key();
    if (!over) return;
    switch (st) {
    case APP_END1: case APP_END2:
        g_app_state = st + 1;
        g_app_sub = 0;
        break;
    case APP_END3:
        g_quit = true;                       /* PostQuitMessage */
        break;
    case APP_END4:
        g_app_state = APP_END5;
        g_app_sub = 0;
        break;
    case APP_END5:
        g_bypass = 1;
        g_start_world = 1;
        g_start_level = 5;
        g_breed = 0;
        g_char = 0;
        g_app_state = APP_START;
        g_app_sub = 0;
        break;
    }
    if (g_app_state != st) {
        surface_free(splash);
        splash = NULL;
    }
}

void flow_start(int state)
{
    g_app_state = state;
    g_app_sub = 0;
    g_world = g_level = 0;
}

bool flow_frame(Framebuffer *fb)
{
    /* App_Frame 0x40a170 */
    if (g_quit) return false;
    switch (g_app_state) {
    case APP_START:
        input_keys_tick();
        input_map();
        if (!g_bypass) {
            movie(exe_str(0x44b990));
            g_app_state = APP_LOGOS;
            g_app_sub = 0;
        } else {
            /* BYPASS_FRONTEND: straight into START_WORLD.START_LEVEL as Bo Peep */
            g_level = g_next_level = g_start_level;
            g_theme = (int8_t)g_start_world;
            g_next_world = g_start_world;
            g_char = 0;
            new_progress();
            g_app_state = APP_LEVEL;
            g_app_sub = 0;
            g_world = g_next_world;
        }
        break;
    case APP_LOGOS:
        g_app_state = APP_INTRO;
        g_app_sub = 0;
        break;
    case APP_INTRO:
        movie(exe_str(0x44b97c));
        go(APP_MENU, 0, 0);
        break;
    case APP_MENU: case APP_LEVEL: case 8: case 9:
        if (g_app_sub == 0) {
            if (!level_begin()) {
                if (g_world == 0 && g_level == 0) return false;
                go(APP_MENU, 0, 0);
                break;
            }
            key_latch = 1;
            g_app_sub = 1;
        } else if (g_app_sub == 1) {
            input_keys_tick();
            loading_draw(fb);
            if (g_demo == 2 || g_world == 0 || g_level == 0 || flow_any_key()) {
                loading_end();
                if (g_sound_fx) {
                    g_music = sound_stream(100, 1, 1);
                    if (g_music) snd_set_vol(g_music, 0.95f);
                    sound_tick();
                }
                g_app_sub = 2;
            }
        } else {
            if (!pause_frame(fb) && !map_frame(fb)) level_frame(fb);
            if (g_end) level_end();
        }
        break;
    case APP_NEXT:
        go(APP_LEVEL, g_next_world, g_next_level);
        break;
    case APP_RESULTS:
        if (g_app_sub == 0) results_frame(fb);
        else results_end();
        break;
    case APP_CREDITS:
        if (g_app_sub == 0) {
            leave_level();
            credits_start();
            g_app_sub = 1;
        } else if (g_app_sub == 1) {
            credits_frame(fb);
        } else {
            credits_end();
            go(APP_MENU, 0, 0);
        }
        break;
    case APP_HISCORES:
        if (g_app_sub == 0) {
            leave_level();
            hiscores_start();
            g_app_sub = 1;
        } else if (g_app_sub == 1) {
            hiscores_frame(fb);
        } else {
            hiscores_end();
            if (g_world == 7) {
                g_app_state = APP_CREDITS;
                g_app_sub = 0;
            } else {
                after_level();
            }
        }
        break;
    case APP_END1: case APP_END2: case APP_END3: case APP_END4: case APP_END5:
        /* data\screenbits\EndSplash*.bmp and Hr3.bmp, 600 ticks each. Nothing sets 13 or 16, and the CD has
           no screenbits: a leftover */
        splash_frame(fb);
        break;
    default:
        go(APP_MENU, 0, 0);
        break;
    }
    return !g_quit;
}
