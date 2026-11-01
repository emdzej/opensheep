#include "play.h"
#include "cheats.h"
#include "exe.h"
#include "hint.h"
#include "hud.h"
#include "level.h"
#include "input.h"
#include "levels.h"
#include "moves.h"
#include "paths.h"
#include "player.h"
#include "render.h"
#include "rng.h"
#include "sheep.h"
#include "sound.h"
#include "sprites.h"
#include "world.h"
#include "xp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AnimObj *g_cam_target;
int g_clock, g_time_limit;          /* 0x4b0f20, 0x4a2090 */
int g_paused;                       /* 0x4b0f3c */
int8_t g_frozen;                    /* 0x4b0f24: the cheat that stops the actors */
int g_tim_hard, g_tim_normal, g_tim_easy;   /* 0x4a1eac, 0x4a28c8, 0x4a20b0 */
int g_flock_n, g_flock_sx, g_flock_sy;
static float cam_tx, cam_ty;        /* 0x4a3ea0, 0x4a4210 */
static float cam_vx, cam_vy;        /* 0x4ae054, 0x4a40f8 */
static int actor_phase;             /* 0x4ae088 */

void game_new(void)
{
    int n = g_difficulty > 1 ? 0x14 : 10;
    for (int i = 0; i < 4; i++) g_flock_size[i] = (int8_t)n;
}

bool level_load_resources(void)
{
    /* 0x401950 */
    if (g_world == 7) g_breed = 0;
    g_loading = 1;
    if (!level_load_blo(world_level_path(".blo"))) return false;
    sprite_banks_free_all();
    actor_banks_load();
    sprite_bank_load_misc(SPR_BANK_MISC, "Misc.spr");
    sprite_bank_load_misc(SPR_BANK_TEXT_YELLOW, "TextYellow.spr");
    sprite_bank_load_misc(SPR_BANK_TEXT_BLUE, "TextBlue.spr");
    bank_table_load(0x44a3f8, SPR_BANK_CPUI);
    level_load_sprite_banks();
    anims_load(world_level_path(".anm"));
    tile_anims_load();
    sound_lists_load();
    objects_load(world_level_path(".pos"));
    paths_load();
    game_init_tables();
    flock_vars_load(g_breed);
    player_vars_load(g_char);
    memset(g_attract, 0, sizeof g_attract);
    g_nattract = 0;
    specials_load();
    gates_init();
    hint_text_load();
    game_init_deflect();
    g_loading = 0;
    return true;
}

static float b_float(int i, int k)
{
    int32_t v;
    memcpy(&v, g_lv.objects + 20 * (size_t)i + 4 * (size_t)k, 4);
    return (float)v;
}

void level_load_tim(int world, int level)
{
    /* 0x406c00: the time limit in ticks (+8 of the .TIM file; 7200 without one) */
    g_tim_hard = 0x1c20;
    size_t n;
    uint8_t *f = file_load(world_path_of(world, level, ".tim"), &n);
    if (f) {
        if (n >= 12) memcpy(&g_tim_hard, f + 8, 4);
        free(f);
    }
    g_tim_normal = g_tim_hard * 2;
    g_tim_easy = g_tim_normal + 18000;
}

void level_start(void)
{
    static const int needed[3] = { 5, 10, 0x14 };
    g_needed = needed[g_difficulty < 0 ? 0 : g_difficulty > 2 ? 2 : g_difficulty];
    level_load_tim(g_world, g_level);
    g_time_limit = g_difficulty == 0 ? g_tim_easy : g_difficulty == 1 ? g_tim_normal : g_tim_hard;
    if (g_world == 9) g_needed = 1;
    if (g_demo) g_rng_index = 0;
    g_nsheep = g_flock_size[g_breed & 3];
    memset(g_sheep, 0, sizeof g_sheep);
    for (int i = g_nsheep; i < MAX_SHEEP; i++) g_sheep[i].alive = 2;
    anim_init(3, PLAYER_SLOT, 0, 0);
    for (int i = 0; i < g_nsheep; i++) {
        Sheep *s = &g_sheep[i];
        if (i + 1 < g_lv.nobjects) {
            s->x = b_float(i + 1, 0);
            s->y = b_float(i + 1, 1);
        }
        int d = (int)(rng_next() & 0xff);
        s->dir = s->face = d;
        s->slot = i;
        s->anim[0] = 3;
        s->anim[1] = 0;
        s->anim[2] = 1;
        s->d[0x1d] = 1;
        s->d[0x3f] = s->d[0x40] = d;
        s->d[0x43] = ftol(s->x);
        s->d[0x44] = ftol(s->y);
        s->idle = (int)(rng_next() & 0x3f);
        s->d[0x45] = (int)(rng_next() & 0xff);
        anim_init(2, i, 0, d / 16);
    }
    g_saved = 0;
    g_sheep_out = 0;
    g_end = 0;
    player_init(&g_player);
    memset(g_chain, 0, sizeof g_chain);
    g_score = 0;
    g_clock = 0;
    g_timer_on = 1;
    g_player.x = b_float(0, 0);
    g_player.y = b_float(0, 1);
    g_scroll_reset = 1;
    camera_update(&g_player);
    g_level_over = 0;
    g_player.slot = PLAYER_SLOT;
    g_effect_count = 0;
    g_key_run = g_key_creep = 0;
    g_action = 0;
    g_scroll_reset = 1;
    g_cam_target = NULL;
    level_hook(DESC_INIT);             /* 0x406cc0 calls it in mode 1, a level's first start */
    /* TODO: the demo streams */
}

/* 0x40cb40: ease toward the target 0.2 px/tick per tick of difference / 16 */
static float ease(float v, float t)
{
    if (t <= 0.0f) {
        if (0.0f < v) v = 0.0f;
        if (t < v) v = (float)((double)v - (double)0.2f);
        if (v < t) v = t;
    } else {
        if (v < 0.0f) v = 0.0f;
        if (v < t) v = (float)((double)v + (double)0.2f);
        if (t < v) v = t;
    }
    return v;
}

void camera_update(Player *p)
{
    if (!g_cam_target) {
        if (g_flock_n == 0) {
            cam_tx = p->x;
            cam_ty = p->y;
        } else {
            cam_tx = (float)(g_flock_sx / g_flock_n);
            cam_ty = (float)(g_flock_sy / g_flock_n);
            float lo = (float)((double)p->x - 260.0), hi = (float)((double)p->x + 260.0);
            if (cam_tx < lo) cam_tx = lo;
            if (hi < cam_tx) cam_tx = hi;
            lo = (float)((double)p->y - 150.0);
            hi = (float)((double)p->y + 150.0);
            if (cam_ty < lo) cam_ty = lo;
            if (hi < cam_ty) cam_ty = hi;
        }
    } else {
        cam_tx = (float)g_cam_target->x;
        cam_ty = (float)g_cam_target->y;
    }
    double dx = (double)cam_tx - 320.0 - (double)g_lv.cam_x;   /* stays on the FPU stack */
    cam_ty = (float)((double)cam_ty - 240.0 - (double)g_lv.cam_y);
    int x = g_lv.cam_x, y = g_lv.cam_y;
    if (g_scroll_reset) {
        x += ftol(dx);
        y += ftol((double)cam_ty);
        cam_ty = 0.0f;
        cam_vx = cam_vy = 0.0f;
        g_scroll_reset = 0;
        dx = 0.0;
    }
    cam_tx = (float)(dx * 0.0625);
    cam_ty = (float)((double)cam_ty * 0.0625);
    cam_vx = ease(cam_vx, cam_tx);
    cam_vy = ease(cam_vy, cam_ty);
    level_set_camera(x + ftol((double)cam_vx), y + ftol((double)cam_vy));
}

void play_tick(void)
{
    /* one pass of 0x40b9b0's tick loop */
    input_keys_tick();
    input_map();
    cheat_keys();
    if (g_score_flash) g_score_flash--;
    if (!g_paused && g_timer_on) g_clock++;
    if (g_clock == g_time_limit && g_world != 9 && !g_level_over) {
        int s = anim_new(0, 0, 0x10, -1, 4);
        g_anim[s].move = 0x11;
        g_anim[s].d[0x15] = 2;
    }
    /* the actors step 0x44b39c times a tick: 1, or 2 with TURBOSHEEP, or every other tick with
       SNAILSHEEP (0) */
    int steps = exe_s32(0x44b39c);
    if (steps == 0) steps = actor_phase++ & 1;
    for (int k = 0; k < steps; k++) {
        g_counted = 0;
        for (int i = 0; i < MAX_SPECIAL; i++) g_special[i].count = 0;
        if (!g_frozen) {
            if (!g_level_over) player_tick(&g_player);
            if (g_world != 7) sheep_tick();
        }
    }
    camera_update(&g_player);
    level_hook(DESC_TICK);
    objects_update();
    tile_anims_tick();
}

void play_draw_world(Framebuffer *fb)
{
    draw_list_reset();
    level_draw_map(fb);
    cursor_collect(&g_player);
    player_collect(&g_player);
    sheep_collect();
    objects_collect();
    sprite_target(fb);
    level_hook(DESC_DRAW1);
    render_sorted(fb);
    level_hook(DESC_DRAW2);
    hud_draw();
}

void play_draw(Framebuffer *fb)
{
    play_draw_world(fb);
    hint_draw();
}
