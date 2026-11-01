#include "pause.h"
#include "cheats.h"
#include "exe.h"
#include "flow.h"
#include "font.h"
#include "game.h"
#include "hud.h"
#include "input.h"
#include "level.h"
#include "play.h"
#include "player.h"
#include "sheep.h"
#include "sprites.h"
#include "surface.h"
#include "world.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>

int g_pause;
static int page;            /* 0x4b910c */
static int cur;             /* 0x4b90f8: the item under the cursor */
static int pick;            /* 0x4b9104: the item last picked on a list page */
static int blink;           /* 0x4b9650 */
static int side;            /* 0x4b90f0: a left / right on the levels page is held */
static int typed[10];       /* 0x4b1080: letters of each cheat word typed so far */
static int typed_latch;     /* 0x4ae0ec */
static int photo;           /* 0x4b107c: TEAMPHOTO typed */

enum { PAGES = 0x44c0c0, PAGE = 0x14, ITEM = 0x14, NO_ITEMS = 0x44bc70, LEVEL_ITEMS = 0x4b9120 };
static uint32_t pg(int p, int off) { return exe_u32(PAGES + PAGE * (uint32_t)p + (uint32_t)off); }
static int count(void) { return (int)pg(page, 0xc); }

static void levels_page(void)
{
    /* 0x40f220: the debug list of every level, three columns of fifteen; a level without code is marked
       (0x8000 in x) */
    int n = 0, x = 0x1e, y = 10;
    for (int i = 0; i < 0x40; i++) {
        uint32_t d = exe_u32(LEVEL_TABLE + 4 * (uint32_t)i);
        if (!d) break;
        int32_t *it = exe_mut(LEVEL_ITEMS + ITEM * (uint32_t)n, ITEM);
        it[0] = exe_u32(d + 4) ? x : x | 0x8000;
        it[1] = y;
        it[2] = 0;
        it[3] = 8;
        it[4] = (int32_t)exe_u32(d);
        n++;
        if ((y += 0x1e) >= 0x1cc) {
            y = 10;
            x += 0x96;
        }
    }
    *(int32_t *)exe_mut(PAGES + PAGE * 4 + 0xc, 4) = n;
}

static void levels_tick(void)
{
    /* 0x40f170: left and right move a column */
    if (!side) {
        if (g_keyheld[DIK_LEFT]) {
            if (cur >= 0xf) cur -= 0xf;
            side = 1;
        } else if (g_keyheld[DIK_RIGHT]) {
            if (cur < count() - 0xf) cur += 0xf;
            side = 1;
        }
    } else if (!g_keyheld[DIK_LEFT] && !g_keyheld[DIK_RIGHT]) {
        side = 0;
    }
    g_next_level = cur;
}

static void call(uint32_t va)
{
    /* the pages' routines */
    switch (va) {
    case 0: case 0x4039f0: break;
    case 0x40f0c0: g_quit = true; break;               /* Quit: WM_CLOSE */
    case 0x40f0e0:                                     /* Quit Level: the flock lost */
        g_saved = 0;
        g_sheep_out = g_nsheep;
        g_end = 3;
        g_pause = 0;
        break;
    case 0x40f110:                                     /* the world map's Exit to Main Menu */
        menu_set_page(0);
        g_keyheld[DIK_RETURN] = g_keyheld[DIK_SPACE] = 10;
        g_pause = 0;
        break;
    case 0x40f140:                                     /* Replay Level */
        g_prog = g_prog_level;
        g_app_state = APP_LEVEL;
        g_pause = 0;
        g_app_sub = 0;
        break;
    case 0x40f170: levels_tick(); break;
    case 0x40f220: levels_page(); break;
    case 0x40f2a0: flow_new_game(); break;
    case 0x40f350:
        /* TODO: the demos page (record, play, load and save a .REC: 0x40fd70, 0x40f990) */
        pause_open(0x11);
        break;
    case 0x40f400: case 0x40f480:
        /* TODO: the debug change of shepherd (0x4180e0, 0x4014c0) and of breed (0x41e850, 0x41e960,
           0x401560) mid-level */
        g_pause = 0;
        break;
    case 0x40f470: g_difficulty = (int8_t)cur; break;
    case 0x40f4d0: pause_open(0); break;
    case 0x40f4e0: pick = cur; break;
    case 0x40f4f0:
        g_pause = 0;
        g_input_mode = pick;
        break;
    default:
        fprintf(stderr, "pause: no routine %#x\n", va);
        break;
    }
}

void pause_open(int p)
{
    g_keyheld[DIK_RETURN] = g_keyheld[DIK_SPACE] = 10;
    if (p == -1) {
        g_pause = 0;
        return;
    }
    page = p;
    g_pause = 1;
    cur = 0;
    call(pg(p, 0));
}

static void cheat_words(void)
{
    /* 0x40b1d0: each new key steps every word it continues (0x44b41c, scan codes) */
    if (typed_latch) {
        if (!g_last_key) typed_latch = 0;
        return;
    }
    if (!g_last_key) return;
    for (int i = 0; i < 10; i++) {
        uint32_t w = exe_u32(0x44b41c + 4 * (uint32_t)i);
        int k = typed[i];
        if (g_last_key != exe_s8(w + (uint32_t)k)) {
            typed[i] = 0;
            continue;
        }
        typed[i] = k + 1;
        if (exe_u8(w + (uint32_t)k + 1)) continue;
        int32_t *idle = exe_mut(0x44cbf0, 4), *ms = exe_mut(0x44b398, 4), *steps = exe_mut(0x44b39c, 4),
                *scale = exe_mut(0x44b3a0, 4);
        switch (i) {
        case 0: *idle = 0x45; break;                  /* POOSHEEP */
        case 1: *ms = 8; break;                       /* TURBOGAME */
        case 2: *ms = 0x20; break;                    /* SNAILGAME */
        case 3: *steps = 2; break;                    /* TURBOSHEEP */
        case 4: *steps = 0; break;                    /* SNAILSHEEP */
        case 5: *scale = 0x180; break;                /* FATSHEEP */
        case 6: *scale = 0x80; break;                 /* MINISHEEP */
        case 7:                                       /* CANCELCHEATS */
            *idle = 3;
            *ms = 0x10;
            *steps = 1;
            *scale = 0x100;
            break;
        case 8: photo = 1; break;                     /* TEAMPHOTO */
        case 9:                                       /* CREDITS */
            g_app_state = APP_CREDITS;
            g_app_sub = 0;
            break;
        }
        g_pause = 0;
    }
    typed_latch = 1;
}

bool pause_frame(Framebuffer *fb)
{
    if (g_demo == 2) return false;
    if (g_menu_back) {
        input_keys_tick();
        if (!g_keyheld[DIK_ESCAPE] && !g_keyheld[DIK_UP] && !g_keyheld[DIK_DOWN] && !g_keyheld[DIK_RETURN])
            g_menu_back = 0;
    }
    int first = g_our_menu ? 0 : 0x15;
    if (g_world == 0 || g_level == 0) {
        /* on the menu level only over the world map */
        if (menu_page() != 10 && menu_page() != 0xb) return false;
        first = 0x17;
    }
    if (!g_menu_back && g_keyheld[DIK_ESCAPE]) {
        g_pause ^= 1;
        if (g_pause) {
            pause_open(first);
            /* TODO: while recording a demo, the menu ends it (0x4b0f9c 3, the streams closed) */
        }
        g_menu_back = 1;
    }
    if (!g_pause) return false;
    input_keys_tick();
    cheat_words();
    if (!g_menu_back) {
        if (g_keyheld[DIK_UP]) {
            g_menu_back = 1;
            if (--cur < 0) cur = count() - 1;
        } else if (g_keyheld[DIK_DOWN]) {
            g_menu_back = 1;
            if (++cur > count() - 1) cur = 0;
        } else if (g_keyheld[DIK_RETURN]) {
            g_menu_back = 1;
            pause_open((int)exe_s32(pg(page, 0x10) + ITEM * (uint32_t)cur + 0xc));
        }
    }
    call(pg(page, 4));
    /* the level drawn as Level_Frame does, the items over it */
    memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
    play_draw_world(fb);
    blink++;
    uint32_t it = pg(page, 0x10);
    if (it != NO_ITEMS)
        for (int i = 0; i < count(); i++, it += ITEM) {
            int x = exe_s32(it) & 0x7fff, y = exe_s32(it + 4), id = exe_s32(it + 8);
            uint16_t buf[0x40];
            const uint16_t *s = id ? str_mut(id) : NULL;
            if (!s && exe_u32(it + 0x10)) s = exe_wtext(exe_u32(it + 0x10), buf, 0x40);
            if (s) {
                if (!g_our_menu) text_draw(s, x, y);
                else font_draw(x, y, s, FONT_BIG, 0xffffff);
            }
            if (i == cur && blink & 4) sprite_draw(x - 0x14, y + 2, SPR_BANK_MISC, 0x3e, 0x200, 0x80);
        }
    call(pg(page, 8));
    sound_tick();
    return true;
}

static Surface *map_pic;    /* 0x4b1068 */
static unsigned photo_phase, map_phase;   /* 0x4a3b50, 0x4ae078 */
static Surface *photo_pic;  /* 0x4b1018 */
static int map_x, map_y;    /* 0x4a9084, 0x4a4104: the map's first lit pixel, the level's (0, 0) */
static int pen_x, pen_y;    /* 0x4adeac, 0x4ae050 */

static void draw_pic(Framebuffer *fb, const Surface *s)
{
    /* 0x409020: a 640 x 480 picture over the screen, the camera at 0 */
    for (int y = 0; y < 0x1e0 && y < s->h; y++)
        memcpy(fb->pixels + (size_t)y * fb->pitch, s->px + (size_t)y * s->w,
               2 * (size_t)(s->w < 0x280 ? s->w : 0x280));
    g_lv.cam_x = g_lv.cam_y = g_lv.fine_x = g_lv.fine_y = 0;
}

static bool photo_frame(Framebuffer *fb)
{
    /* 0x409070: TEAMPHOTO shows data\screenbits\map.tga (not on the CD) until a key */
    if (g_world == 0 || g_level == 0 || g_level == 5 || g_world == 7) return false;
    if (!photo_pic) {
        if (!photo) return false;
        photo_pic = surface_load_tga(exe_str(0x44b7f0));
        if (!photo_pic) {
            fprintf(stderr, "Cannot load %s\n", exe_str(0x44b7f0));
            photo = 0;                  /* (the original shows the last picture loaded there, or none) */
            return false;
        }
        return true;
    }
    input_keys_tick();
    input_map();
    if (!flow_any_key()) {
        draw_pic(fb, photo_pic);
        if (photo_phase++ & 1) sound_tick();     /* every other frame */
        return true;
    }
    surface_free(photo_pic);
    photo_pic = NULL;
    photo = 0;
    g_scroll_reset = 1;
    return false;
}

bool map_frame(Framebuffer *fb)
{
    /* 0x409140: Tab (or pad button 4) holds the level's .tga map, a dot each for the pen (green), the
       flock and the shepherd (red) at a sixth of their place */
    if (photo_frame(fb)) return true;
    if (g_world == 0 || g_level == 0 || g_level == 5 || g_world == 7) return false;
    if (map_pic) {
        input_keys_tick();
        input_map();
        if (!g_keyheld[DIK_TAB] && !(g_pad_buttons & 8)) {
            surface_free(map_pic);
            map_pic = NULL;
            g_scroll_reset = 1;
            return false;
        }
        draw_pic(fb, map_pic);
        sprite_target(fb);
        sprite_tint(5);
        sprite_draw(pen_x / 6 + map_x, pen_y / 6 + map_y, SPR_BANK_MISC, 7, 0x100, 0x100);
        sprite_tint(0);
        for (int i = 0; i < MAX_SHEEP; i++)
            if (g_sheep[i].alive != 2)
                sprite_draw(ftol(g_sheep[i].x) / 6 + map_x, ftol(g_sheep[i].y) / 6 + map_y, SPR_BANK_MISC, 7,
                            0x100, 0x100);
        sprite_tint(6);
        sprite_draw(ftol(g_player.x) / 6 + map_x, ftol(g_player.y) / 6 + map_y, SPR_BANK_MISC, 7, 0x100, 0x100);
        sprite_tint(0);
        if (map_phase++ & 1) sound_tick();       /* every other frame */
        return true;
    }
    if (!g_keyheld[DIK_TAB] && !(g_pad_buttons & 8)) return false;
    map_pic = surface_load_tga(world_level_path(exe_str(0x44a0a8)));
    if (!map_pic) {
        fprintf(stderr, "Cannot load %s\n", world_level_path(exe_str(0x44a0a8)));
        return false;
    }
    map_x = map_y = 0;
    for (int i = 0; i < map_pic->w * map_pic->h && i < 0x4b000; i++)
        if (map_pic->px[i]) {
            map_x = i % map_pic->w;
            map_y = i / map_pic->w;
            break;
        }
    if (map_y > 0x1de) map_x = map_y = 0;
    /* the top-left cell of a pen (special 0xb or 0x14) */
    pen_x = pen_y = -100;
    for (int y = g_lv.cam_max_y - 1; y >= g_lv.cam_min_y; y -= 0x18)
        for (int x = g_lv.cam_max_x - 1; x >= g_lv.cam_min_x; x -= 0x20) {
            uint32_t c = g_lv.layer3[x / 32 + y / 24 * g_lv.w];
            int t = g_special[c >> 8 & 0xff].type;
            if ((uint8_t)c && (t == 0xb || t == 0x14)) pen_x = x, pen_y = y;
        }
    return true;
}
