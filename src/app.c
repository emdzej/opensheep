/* For now: a level viewer (load a level, scroll its map with the arrow keys / mouse drag, PageUp /
   PageDown for the previous / next level). The front end and game states replace it as they are ported. */
#include "app.h"
#include "exe.h"
#include "level.h"
#include "platform.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int view_x, view_y;

static bool load(int world, int level)
{
    if (!world_select(world, level)) return false;
    if (!level_load_blo(world_level_path(".blo"))) return false;
    if (g_lv.cam_max_x <= g_lv.cam_min_x) {
        /* No bounds in the file (the menu level: Menu_SetPage 0x427870 sets one 640x480 page at a time);
           the viewer shows the whole map. */
        g_lv.cam_min_x = g_lv.cam_min_y = 0;
        g_lv.cam_max_x = g_lv.px_w;
        g_lv.cam_max_y = g_lv.px_h;
    }
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
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--level") && i + 1 < argc) sscanf(argv[++i], "%d.%d", &world, &level);
    if (!exe_load()) { plat_error("OpenSheep", exe_error()); return false; }
    if (!plat_init("OpenSheep", 640, 480)) return false;
    if (!load(world, level) && !load(0, 0)) { plat_error("OpenSheep", "cannot load the level"); return false; }
    return true;
}

bool app_frame(void)
{
    if (!plat_poll()) return false;
    int keys[16], n = plat_key_presses(keys, 16);
    for (int i = 0; i < n; i++) {
        if (keys[i] == KEY_PAGEDOWN) step_level(1);
        if (keys[i] == KEY_PAGEUP) step_level(-1);
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
    level_draw_map(fb);
    plat_present();
    return true;
}

void app_exit(void)
{
    level_release();
    plat_shutdown();
}
