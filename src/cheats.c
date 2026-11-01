/* The developers' switches: Sheep.ini (0x422650), read at start-up, and the keys they enable (0x40b870,
   part of 0x408ca0). The retail CD has no Sheep.ini, so all of this is off unless one is provided. */
#include "cheats.h"
#include "exe.h"
#include "flow.h"
#include "game.h"
#include "hud.h"
#include "input.h"
#include "world.h"
#include "play.h"
#include "platform.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>

int g_mono;                 /* 0x4cfe58 MONO (read by the debug log only) */
int g_our_menu;             /* 0x4b967c OUR_MENU: the debug keys */
int g_start_level = 0;      /* 0x4b0fc8 START_LEVEL */
int g_start_world = 0;      /* 0x4b0fcc START_WORLD */
int g_bypass;               /* 0x4b0fd0 BYPASS_FRONTEND: straight into START_WORLD.START_LEVEL */
int g_sound_fx = 1;         /* 0x44b1d8 SOUND_FX */
int g_record;               /* 0x4b0f98 RECORD: demos */
static int view_a, view_b;  /* 0x4b0f40, 0x4b0f44: cycled by Shift+1 / Shift+2, read by nothing */

/* The option table 0x4505a8: {name, variable} pairs, the names read from the exe. */
static int *option_var(uint32_t va)
{
    switch (va) {
    case 0x4cfe58: return &g_mono;
    case 0x4b0f3c: return &g_paused;            /* DISSABLE_TIMER */
    case 0x4b0fc8: return &g_start_level;
    case 0x4b0fcc: return &g_start_world;
    case 0x4b967c: return &g_our_menu;
    case 0x4b0fd4: return &g_cheats;            /* WORLD_CHEAT */
    case 0x44b178: return &g_input_mode;        /* CONTROL_TYPE */
    case 0x4b0fd0: return &g_bypass;
    case 0x44b1d8: return &g_sound_fx;
    case 0x4b0f98: return &g_record;
    case 0x4a40ba: return &g_lang;              /* LANGUAGE */
    }
    return NULL;
}

/* 0x422600: the first number after p (a '-' anywhere before it makes it negative), 0 at a '\r'. */
static int number(const char *p, const char *end)
{
    int sign = 1, v = 0;
    for (;; p++) {
        if (p >= end || *p == '\r') return 0;   /* (the original reads on past the end of the file) */
        if (*p == '-') sign = -1;
        if (*p >= '0' && *p <= '9') break;
    }
    for (; p < end && *p >= '0' && *p <= '9'; p++) v = *p - '0' + v * 10;
    return sign * v;
}

void ini_parse(const char *text, size_t n)
{
    /* 0x422650: per line, the first option whose name starts it */
    const char *end = text + n;
    for (const char *p = text; p < end;) {
        for (uint32_t e = 0x4505a8; exe_u32(e); e += 8) {
            const char *name = exe_str(exe_u32(e));
            size_t k = strlen(name);
            if ((size_t)(end - p) < k || memcmp(p, name, k)) continue;
            int *var = option_var(exe_u32(e + 4));
            if (var) {
                *var = number(p + k, end);
                fprintf(stderr, "INI file is setting %s to %d\n", name, *var);
            }
            break;
        }
        while (p < end && *p != '\n') p++;
        p++;
    }
}

void ini_load(void)
{
    /* "Sheep.ini" (0x450744) beside the game: here, in the storage */
    static char buf[4096];
    int n = plat_storage_get(exe_str(0x450744), buf, sizeof buf);
    if (n < 0) return;
    if (n > (int)sizeof buf) {
        fprintf(stderr, "Sheep.ini: longer than %zu bytes, ignored\n", sizeof buf);
        return;
    }
    ini_parse(buf, (size_t)n);
}

void cheat_world_keys(void)
{
    /* part of 0x408ca0: with WORLD_CHEAT, 1-7 (held, not shift) start that world's map with a new
       game, after the movie of the world before it */
    if (!g_cheats || g_keyheld[DIK_LSHIFT]) return;
    for (int i = 0; i < 7; i++) {
        if (!g_keyheld[DIK_1 + i]) continue;
        if (g_sound_fx) {
            sound_stop_sfx(-1);
            sound_stop_streams(-1);
            sound_tick();
            sound_free_all();
        }
        if (i > 0) flow_world_movie(i);
        input_keys_tick();
        g_next_level = 6 * (i + 1);
        flow_new_game();
    }
}

void cheat_keys(void)
{
    /* 0x40b870, every tick of a level */
    if (menu_naming()) return;
    if (g_our_menu) {
        /* I: TODO: 0x40b720 draws the level's tile map over the screen */
        if (g_keyheld[0x13] == 1) {
            /* R: the level again from its start (a demo being played carries on recording) */
            if (g_demo) g_demo = 1;
            flow_level_reload();
        }
        if (g_our_menu) {
            if (!g_keyheld[DIK_LSHIFT]) goto quit;
            if (g_keyheld[DIK_D] == 1) {
                /* Shift+D: the level won with the whole flock */
                g_end = 1;
                g_prog.breeds |= exe_u8(0x44b198 + 4 * (uint32_t)g_breed);
                g_prog.levels |= exe_u8(0x44b194 + 4 * (uint32_t)g_level);
                g_prog.bonus |= exe_u8(0x44b194 + 4 * (uint32_t)g_level);
                g_saved = g_nsheep;
            }
        }
    }
    if (g_keyheld[DIK_LSHIFT]) {
        if (g_keyheld[DIK_1] == 1 && ++view_a > 2) view_a = 0;
        if (g_keyheld[DIK_1 + 1] == 1 && ++view_b > 2) view_b = 0;
    }
quit:
    /* Alt+F4 */
    if (g_keyheld[DIK_LMENU] && g_keyheld[DIK_F1 + 3]) g_quit = true;
}

void cheat_debug_keys(void)
{
    /* part of 0x408ca0, with OUR_MENU: Home stops the actors (TODO: the editor 0x40aea0 it runs then);
       P without shift saves a screenshot (TODO: 0x422580, Sheep0001.bmp on) */
    if (!g_our_menu) return;
    if (g_keyheld[0xc7] == 1) g_frozen = !g_frozen;
}
