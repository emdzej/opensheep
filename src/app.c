/* For now: a level loaded straight into play (the shepherd on the keyboard: arrows, shift runs, ctrl
   creeps, space acts), or with --view the level viewer:  (load a level, scroll its map with the arrow keys / mouse drag, PageUp /
   PageDown for the previous / next level; G: sprite gallery of the loaded banks, [ ] pick the bank). The front end and game states replace it as they are ported. */
#include "app.h"
#include "exe.h"
#include "flow.h"
#include "cheats.h"
#include "hiscore.h"
#include "font.h"
#include "hud.h"
#include "xp.h"
#include "input.h"
#include "level.h"
#include "levels.h"
#include "moves.h"
#include "objects.h"
#include "play.h"
#include "player.h"
#include "sheep.h"
#include "sound.h"
#include "render.h"
#include "rng.h"
#include "platform.h"
#include "sprites.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int view_x, view_y;
static bool gallery, viewer, trace, front_end;
static int ticks;
static int gallery_bank = SPR_BANK_LEVEL;

static bool load(int world, int level)
{
    /* 0x406cc0 in mode 1 runs the level's release hook before the tiles go */
    if (g_level != 0 && !viewer) level_hook(DESC_RELEASE);
    if (!world_select(world, level)) return false;
    /* the level state (0x40f2a0, case 3) picks the panel before 0x406cc0; init hooks may change it */
    g_hud_style = level == 0 ? 0 : world != 9 ? 1 : 5;
    if (!viewer && level != 0) {
        if (!level_load_resources()) return false;
        level_start();
        fprintf(stderr, "level %d.%d %s: %d sheep, %d to pen\n", world, level, world_level_name(), g_nsheep,
                g_needed);
        return true;
    }
    if (!level_load_blo(world_level_path(".blo"))) return false;
    if (g_lv.cam_max_x <= g_lv.cam_min_x) {
        /* No bounds in the file (the menu level: Menu_SetPage 0x427870 sets one 640x480 page at a time);
           the viewer shows the whole map. */
        g_lv.cam_min_x = g_lv.cam_min_y = 0;
        g_lv.cam_max_x = g_lv.px_w;
        g_lv.cam_max_y = g_lv.px_h;
    }
    sprite_banks_free_all();
    int nb = level_load_sprite_banks();
    sprite_bank_load_misc(SPR_BANK_MISC, "Misc.spr");
    sprite_bank_load_misc(SPR_BANK_TEXT_YELLOW, "TextYellow.spr");
    sprite_bank_load_misc(SPR_BANK_TEXT_BLUE, "TextBlue.spr");
    fprintf(stderr, "sprite banks: %d of %d\n", nb, g_lv.nbanks);
    anims_load(world_level_path(".anm"));
    objects_load(world_level_path(".pos"));
    view_x = g_lv.cam_min_x;
    view_y = g_lv.cam_min_y;
    level_set_camera(view_x, view_y);
    fprintf(stderr, "level %d.%d %s: %dx%d cells, %d tiles, camera x %d..%d y %d..%d\n", world, level,
            world_level_name(), g_lv.w, g_lv.h, g_lv.ntiles, g_lv.cam_min_x, g_lv.cam_max_x, g_lv.cam_min_y,
            g_lv.cam_max_y);
    return true;
}

/* Next (dir 1) or previous (-1) slot that holds a level. */
static void step_level(int dir)
{
    int slot = g_world * LEVELS_PER_WORLD + g_level;
    for (int i = 0; i < WORLDS * LEVELS_PER_WORLD; i++) {
        slot = (slot + dir + WORLDS * LEVELS_PER_WORLD) % (WORLDS * LEVELS_PER_WORLD);
        if (load(slot / LEVELS_PER_WORLD, slot % LEVELS_PER_WORLD)) return;
    }
}

bool app_init(int argc, char **argv)
{
    int world = 0, level = 0;
    front_end = true;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--level") && i + 1 < argc) sscanf(argv[++i], "%d.%d", &world, &level), front_end = false;
        else if (!strcmp(argv[i], "--view")) viewer = true;
        else if (!strcmp(argv[i], "--trace")) trace = true;
    rng_init();                         /* 0x408930, the start-up, fills the table once */
    game_new();
    g_input_mode = 1;
    if (!exe_load()) { plat_error("OpenSheep", exe_error()); return false; }
    if (!plat_init("OpenSheep", 640, 480)) return false;
    /* 0x438350: Sheep.ini, then main.xp is mounted and the texts loaded once, at start-up */
    ini_load();
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--ini")) ini_parse(argv[i + 1], strlen(argv[i + 1]));
    xp_mount("main.xp");
    hiscores_init();                    /* 0x408990 */
    if (!strings_load()) fprintf(stderr, "no GameStrings\n");
    if (!fonts_load()) fprintf(stderr, "no fonts\n");
    if (front_end && !viewer) {
        flow_start(APP_START);
        return true;
    }
    if (!load(world, level) && !load(0, 0)) { plat_error("OpenSheep", "cannot load the level"); return false; }
    return true;
}

/* The frames of gallery_bank in rows, over the map (so the blending shows). */
static void draw_gallery(Framebuffer *fb)
{
    sprite_target(fb);
    sprite_clip(0, 0, fb->w, fb->h);
    int x = 4, y = 4, row_h = 0;
    for (int i = 0; i < sprite_frames(gallery_bank); i++) {
        SprFrame f;
        if (!sprite_frame(gallery_bank, i, &f)) continue;
        if (x + f.w > fb->w && x > 4) { x = 4; y += row_h + 4; row_h = 0; }
        if (y >= fb->h) break;
        sprite_draw(x, y, gallery_bank, (uint32_t)i, SPR_SCALE_1, SPR_SCALE_1);
        x += f.w + 4;
        if (f.h > row_h) row_h = f.h;
    }
}

static int key_down(int key) { return plat_key_down(key); }

static bool frame(void)
{
    if (!plat_poll()) return false;
    if (front_end && !viewer) {
        input_read(key_down);
        int mx, my, dx, dy;
        uint32_t buttons = 0;
        plat_mouse(&mx, &my, &dx, &dy, &buttons);
        g_mouse_button = buttons & 1;
        Framebuffer *fb = plat_fb();
        bool on = flow_frame(fb);
        plat_present();
        return on;
    }
    int keys[16], n = plat_key_presses(keys, 16);
    for (int i = 0; i < n; i++) {
        if (keys[i] == KEY_PAGEDOWN) step_level(1);
        if (keys[i] == KEY_PAGEUP) step_level(-1);
        if (keys[i] == KEY_G) gallery = !gallery;
        if (keys[i] == KEY_RIGHTBRACKET || keys[i] == KEY_LEFTBRACKET) {
            int d = keys[i] == KEY_RIGHTBRACKET ? 1 : -1;
            for (int k = 0; k < SPR_BANKS; k++) {
                gallery_bank = (gallery_bank + d + SPR_BANKS) % SPR_BANKS;
                if (sprite_frames(gallery_bank)) break;
            }
            fprintf(stderr, "bank %d: %d frames\n", gallery_bank, sprite_frames(gallery_bank));
        }
    }
    if (!viewer && g_level != 0) {
        input_read(key_down);
        play_tick();
        if (trace && ++ticks % 30 == 0) {
            fprintf(stderr, "t%d shepherd %d,%d st %d | saved %d out %d |", ticks, g_player.ix, g_player.iy,
                    g_player.state, g_saved, g_sheep_out);
            for (int i = 0; i < g_nsheep && i < MAX_SHEEP; i++)
                fprintf(stderr, " %d,%d/%d", g_sheep[i].ix, g_sheep[i].iy, g_sheep[i].state);
            fprintf(stderr, "\n");
        }
        Framebuffer *fb = plat_fb();
        memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
        play_draw(fb);
        if (gallery) draw_gallery(fb);
        plat_present();
        return true;
    }
    int speed = plat_key_down(KEY_LSHIFT) || plat_key_down(KEY_RSHIFT) ? 24 : 8;
    if (plat_key_down(KEY_LEFT)) view_x -= speed;
    if (plat_key_down(KEY_RIGHT)) view_x += speed;
    if (plat_key_down(KEY_UP)) view_y -= speed;
    if (plat_key_down(KEY_DOWN)) view_y += speed;
    int mx, my, dx, dy;
    uint32_t buttons;
    if (plat_mouse(&mx, &my, &dx, &dy, &buttons) && buttons & 1) { view_x -= dx; view_y -= dy; }
    level_set_camera(view_x, view_y);
    view_x = g_lv.cam_x;
    view_y = g_lv.cam_y;
    Framebuffer *fb = plat_fb();
    memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
    objects_update();
    level_draw_map(fb);
    draw_list_reset();
    objects_collect();
    render_sorted(fb);
    if (gallery) draw_gallery(fb);
    plat_present();
    return true;
}

bool app_frame(void)
{
    bool on = frame();
    sound_frame();          /* this frame's 16 ms of sound, for the host to take */
    return on;
}

void app_exit(void)
{
    level_release();
    plat_shutdown();
}
