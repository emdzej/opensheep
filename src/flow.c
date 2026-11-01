#include "flow.h"
#include "cheats.h"
#include "exe.h"
#include "font.h"
#include "hud.h"
#include "input.h"
#include "level.h"
#include "levels.h"
#include "moves.h"
#include "play.h"
#include "results.h"
#include "player.h"
#include "sprites.h"
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
static int splash_ticks;            /* 0x4adeb8 */
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
    /* the movie player (0x403a30). TODO: Bink */
    (void)path;
    key_latch = 1;
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
    /* App_Frame state 10 once the screen is done: the flock left, the level and breed done, the
       best result kept */
    g_hiscore_mark = -1;
    if (g_level != 5 && g_world != 7) {
        g_flock_size[g_breed & 3] = (int8_t)g_saved;
        g_prog.breeds = (uint8_t)(g_prog.breeds | bit(g_breed & 3));
    }
    g_prog.levels = (uint8_t)(g_prog.levels | exe_u32(0x44b194 + 4 * (uint32_t)g_level));
    /* TODO: the best results (0x40fe20) and the end of world 7 (the movie, the scores, the credits) */
    if (g_world == 7 && g_4b911c == 1 && g_end == 1) {
        g_app_state = APP_HISCORES;
        g_app_sub = 0;
        return;
    }
    if (g_world != 7 && g_hiscore_mark != -1) {
        g_app_state = APP_HISCORES;
        g_app_sub = 0;
        return;
    }
    after_level();
}

static bool level_begin(void)
{
    /* sub-state 0 of the level states: 0x409d70(0) selects the level, 0x406cc0(1) loads it */
    if (!world_select(g_world, g_level)) {
        fprintf(stderr, "no level %d.%d\n", g_world, g_level);
        return false;
    }
    g_hud_style = g_level == 0 ? 0 : g_world != 9 ? 1 : 5;
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
    /* 0x40f2a0 (TODO: 0x4b9678, the hint shown, cleared) */
    g_next_world = g_next_level / 6;
    g_next_level %= 6;
    g_app_state = APP_LEVEL;
    g_app_sub = 0;
    g_theme = (int8_t)g_next_world;
    new_progress();
    g_world = g_next_world;
    g_level = g_next_level;
    input_keys_tick();
}

static void loading_draw(Framebuffer *fb)
{
    /* 0x409d70(2): the loading picture (TODO: the level's .bmp, 0x408fb0) and "press a key" blinking */
    memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
    sprite_target(fb);
    if (g_demo != 2 && g_world != 0 && g_level != 0 && !(++load_blink & 0x10)) {
        const uint16_t *s = str_get(0x74);
        int w = font_width(s, FONT_BIG);
        if (w > 0x280) w = 0x280;
        font_draw(0x140 - w / 2, 0x1bc, s, FONT_BIG, 0);
    }
}

static void level_end(void)
{
    /* the level is over: free it and act on its end code */
    level_release();
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
            if (g_demo == 2 || g_world == 0 || g_level == 0 || flow_any_key()) g_app_sub = 2;
        } else {
            play_tick();
            memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
            play_draw(fb);
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
        /* TODO: the credits (0x40d7a0, 0x40d9e0, 0x40dc70) */
        go(APP_MENU, 0, 0);
        break;
    case APP_HISCORES:
        /* TODO: the best scores (0x40e420, 0x40e6b0, 0x40e8f0) */
        if (g_world == 7) {
            g_app_state = APP_CREDITS;
            g_app_sub = 0;
        } else {
            after_level();
        }
        break;
    case APP_END1: case APP_END2: case APP_END3:
        /* TODO: data\screenbits\EndSplash*.bmp, 600 ticks each */
        (void)splash_ticks;
        return false;
    default:
        go(APP_MENU, 0, 0);
        break;
    }
    return !g_quit;
}
