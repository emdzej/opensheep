#include "levels_int.h"
#include "flow.h"
#include "font.h"
#include "hud.h"
#include "input.h"
#include "objects.h"
#include "play.h"
#include "player.h"
#include "save.h"
#include "xp.h"
#include <stdio.h>

/* The menu level (MenuLev, world 0 level 0 and level 0 of every world as the world map): one map,
   a 640x480 page of it at a time, buttons placed in the .POS file and the cursor (move 0x8001) that
   steps between them along the neighbour ids each button carries (d15..d18: left, right, up, down).
   Every page's labels are drawn into the tiles; the item lists (0x4517f0) add the texts that change.
   The tables the original rewrites as it goes (the items' colours, string ids and text pointers, the
   text buffers) are used in place in the exe image. */

enum {
    PAGE_MAIN = 0, PAGE_CHAR = 1, PAGE_LOAD = 2, PAGE_OPTIONS = 3, PAGE_CONTROLS = 4, PAGE_JUKEBOX = 5,
    PAGE_LOADED = 6, PAGE_BEST = 7, PAGE_DIFFICULTY = 8, PAGE_BREED = 9, PAGE_LEVELS = 10, PAGE_MAP = 11,
    PAGE_SAVE = 12, PAGE_LAST = 13,
    ITEM = 0x18,                    /* {x, y, s16 string id, colour, s8 font, text pointer} */
    NAME_BUF = 0x65ffb8,            /* the name being typed */
    WIDE_BUF = 0x4cfa38,            /* 0x422090's buffer */
    BTN_OFF = 0x19,                 /* a button's d19: what it selects, -1 disabled */
};

static int page;                    /* 0x65ffa4 */
static int sel = -1;                /* 0x660004: the button chosen this tick, -1 none */
static int naming;                  /* 0x65ffa8: typing a save's name */
static int idle;                    /* 0x65fffc: ticks without a move (to the attract mode) */
static int back_latch;              /* 0x660000: pad button 4 */
static int demo_i;                  /* 0x660014 */
static int attract;                 /* 0x660018: next in the attract cycle, 0 demo, 1 credits, 2 scores */
static int track;                   /* 0x65ff90: the jukebox */
static int hs_i, hs_n;              /* 0x65ff98, 0x65ff34: the best-times page */
static int8_t hs_list[40][2];       /* 0x65ff3c: the (world, level) pairs played */
static int load_slot;               /* 0x65fff8 */
static int8_t breeds_seen;          /* 0x65ffac: breeds whose movie has played */
static int save_slot;               /* 0x65ffa0 */
static int last_key;                /* 0x65ff94 */
static int name_len, name_max;      /* 0x65ff30, 0x65ffb0 */
static int blink;                   /* 0x65ffb4 */
static int8_t lat_right, lat_left, lat_up, lat_down, lat_button;  /* 0x65ff38, 0x65ff8c, 0x660009, 0x660008, 0x65ff9c */
static bool music_on;               /* 0x4a19c8 */
float g_sfx_volume = 1.0f;          /* 0x4b9850 */
float g_music_volume = 1.0f;        /* 0x4b9860 */
int g_sfx_level;                    /* 0x451848 */
int g_cheats;                       /* 0x4b0fd4 */
int g_menu_back;                    /* 0x4b9638 */

static void tables_init(void)
{
    /* the C runtime's initialisers (0x44a004..): 0x425f40.. the pages' items, 0x427730 the breed page's
       string ids (0x66000c); the last, 0x438140, constructs the application and is not run */
    for (uint32_t p = 0x44a004; p < 0x44a030; p += 4) exe_run(exe_u32(p));
}
EXE_LOADER(tables_init)

/* The load page's slot texts (heap buffers of 0x40 bytes in the original). */
static uint16_t load_text[SAVE_SLOTS][0x20];

static uint32_t items_of(int p) { return exe_u32(0x4517f0 + 4 * (uint32_t)p); }
static void put_s16(uint32_t va, int v) { int16_t x = (int16_t)v; memcpy(exe_mut(va, 2), &x, 2); }
static void put_u32(uint32_t va, uint32_t v) { memcpy(exe_mut(va, 4), &v, 4); }
static uint16_t *wbuf(uint32_t va, size_t n) { return (uint16_t *)exe_mut(va, 2 * n); }
static uint16_t *name_buf(void) { return wbuf(NAME_BUF, 0x20); }
static int bit(int i) { return (int)exe_u32(0x44b198 + 4 * (uint32_t)i); }

static size_t wlen(const uint16_t *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* wcpcpy (0x422050) into [d, end): the original has no end. */
static uint16_t *wcp(uint16_t *d, const uint16_t *s, const uint16_t *end)
{
    while (*s && d < end - 1) *d++ = *s++;
    *d = 0;
    return d;
}

/* Into string n of the table, no longer than it is: the original writes whatever fits the text. */
static void str_put(int n, const uint16_t *s)
{
    uint16_t *d = str_mut(n);
    if (d) wcp(d, s, d + wlen(d) + 1);
}

/* 0x422090: a name widened into the one shared buffer; returns its address. */
static uint32_t widen(const char *s)
{
    uint16_t *d = wbuf(WIDE_BUF, 0x100);
    size_t i = 0;
    for (; s[i] && i < 0xff; i++) d[i] = (uint8_t)s[i];
    d[i] = 0;
    return WIDE_BUF;
}

/* 0x426b20: " m:ss" of a time in ticks into p[0..4] (p[2], the colon, stays). */
static void put_time(uint16_t *p, int t)
{
    int s = t / 60, ts = s / 10, m = ts / 6;
    p[4] = (uint16_t)(s % 10 + '0');
    p[3] = (uint16_t)(ts % 6 + '0');
    p[1] = (uint16_t)(m % 10 + '0');
    p[0] = m / 10 ? (uint16_t)(m / 10 + '0') : ' ';
}

/* str(0xbba) is " m:ss" after two characters; 0x426b20 writes the time into it. */
static void put_time_str(int t)
{
    uint16_t *s = str_mut(exe_s16(0x450bba));
    if (s && wlen(s) >= 7) put_time(s + 2, t);
}

/* The limit for the difficulty, the difficulty's name (0x450bb4..). */
static int tim_of_difficulty(void)
{
    return g_difficulty == 0 ? g_tim_easy : g_difficulty == 1 ? g_tim_normal : g_tim_hard;
}
static int diff_strid(void) { return exe_s16(0x450bb4 + 2 * (uint32_t)(g_difficulty < 0 || g_difficulty > 2 ? 0 : g_difficulty)); }

/* A marker over a button (0x402330 with move 0x8004, the no-op); the button may be disabled. */
static void marker(int x, int y, int anim, int button)
{
    int s = anim_new(x, y, anim, -1, 3);
    if (s >= 0) g_anim[s].move = 0x8004;
    AnimObj *b = button ? obj(button) : NULL;
    if (b) b->d[BTN_OFF] = -1;
}

static int bits4(int v)
{
    /* 0x4094b0 */
    int n = 0;
    for (int i = 0; i < 4; i++, v >>= 1) n += v & 1;
    return n;
}

static void levels_train(void)
{
    /* 0x426bc0: the training levels' page; one unlocks per level done */
    map_copy((g_theme + 4) * 0x80, 0x3c0, 0x820, 0x228, 4, 0x10);
    put_s16(0x4514c8, exe_s16(0x450b9e + 2 * (uint32_t)g_theme));
    for (int i = 0; i < 4; i++) {
        uint32_t it = 0x4514e0 + 0x48 * (uint32_t)i;
        put_s16(it, exe_s16(0x450bd2 + 2 * (uint32_t)(i + g_theme * 6)));
        put_s16(it + 0x18, 0);
        put_u32(it + 0x24, 0);
        put_s16(it + 0x30, 0);
        put_u32(it + 0x3c, 0);
        int x = exe_s32(0x451a38 + 8 * (uint32_t)i), y = exe_s32(0x451a3c + 8 * (uint32_t)i);
        int btn = exe_s32(0x451a58 + 4 * (uint32_t)i);
        if (g_prog.levels & bit(i)) marker(x, y, 0x5b, btn);
        if (bits4(g_prog.levels) < i) marker(x + 10, y - 8, 0x5c, btn);
    }
}

static void levels_page(void)
{
    /* 0x426d20: the world's four levels with their limits; a tick on the done ones, a star on the
       bonus done, a lock on those not in this build */
    if (g_theme == 9) { levels_train(); return; }
    map_copy((g_theme + 4) * 0x80, 0x3c0, 0x820, 0x228, 4, 0x10);
    put_s16(0x4514c8, exe_s16(0x450b9e + 2 * (uint32_t)g_theme));
    for (int i = 0; i < 4; i++) {
        put_s16(0x4514e0 + 0x48 * (uint32_t)i, exe_s16(0x450bd2 + 2 * (uint32_t)(i + g_theme * 6)));
        uint32_t buf = 0x4512c0 + 0x80 * (uint32_t)i;
        uint16_t *d = wbuf(buf, 0x40), *end = d + 0x40;
        level_load_tim(g_theme, i + 1);
        int t = tim_of_difficulty();
        put_u32(0x451504 + 0x48 * (uint32_t)i, buf);
        if (g_lang < 7) {
            put_time_str(t);
            uint16_t sp[2];
            uint16_t *p = wcp(d, str_get(diff_strid()), end);
            p = wcp(p, exe_wtext(0x451c9c, sp, 2), end);
            wcp(p, str_get(exe_s16(0x450bba)), end);
        } else {
            uint16_t fmt[32];
            int n = g_difficulty == 0 ? 5 : g_difficulty == 1 ? 10 : 0x14;
            wfmt(d, 0x40, exe_wtext(0x451c74, fmt, 32), str_get(0x71), n, str_get(0x72), str_get(0x73), t / 0xe10,
                 t % 0xe10 / 0x3c);
        }
        int x = exe_s32(0x451a38 + 8 * (uint32_t)i), y = exe_s32(0x451a3c + 8 * (uint32_t)i);
        int btn = exe_s32(0x451a58 + 4 * (uint32_t)i);
        if (g_prog.levels & bit(i)) marker(x, y, 0x5b, btn);
        if (g_prog.bonus & bit(i)) marker(0x79e, y + 0x18, 0x5d, 0);
        uint32_t desc = world_desc_of(g_theme, i + 1);
        if (!desc || exe_u32(desc + 4) != 2) marker(x + 10, y - 8, 0x5c, btn);
    }
}

static void last_page(void)
{
    /* 0x427260: world 7's one level and its limit */
    level_load_tim(g_theme, 1);
    put_time_str(tim_of_difficulty());
    uint16_t *d = wbuf(0x4512c0, 0x40), *end = d + 0x40, sp[2];
    uint16_t *p = wcp(d, str_get(diff_strid()), end);
    p = wcp(p, exe_wtext(0x451c9c, sp, 2), end);
    wcp(p, str_get(exe_s16(0x450bba)), end);
}

static void best_page(void)
{
    /* 0x427330: the levels done in the loaded game, one at a time with its best result */
    memset(hs_list, 0, sizeof hs_list);
    hs_n = 0;
    for (int w = 1; w < 10; w++)
        for (int l = 1; l <= 5; l++)
            if (g_prog_loaded.worlds[w] & bit(l) && hs_n < 40) {
                hs_list[hs_n][0] = (int8_t)w;
                hs_list[hs_n][1] = (int8_t)l;
                hs_n++;
            }
    g_next_world = hs_list[hs_i][0];
    g_next_level = hs_list[hs_i][1];
    map_copy((g_next_world + 4) * 0x80, (g_next_level * 3 + 0x1b) * 0x20, 0xb40, 0xc0, 4, 4);
    put_s16(0x451070, exe_s16(0x450bd0 + 2 * (uint32_t)(g_next_level + g_next_world * 6)));
    if (g_next_world == 9) {
        put_s16(0x451088, 0);
        put_s16(0x4510a0, 0);
    } else {
        put_s16(0x451088, diff_strid());
        put_s16(0x4510a0, exe_s16(0x450bba));
        level_load_tim(g_next_world, g_next_level);
        put_time_str(tim_of_difficulty());
    }
    /* the table has 50 entries; world 9's level 5 would be the 51st */
    int k = g_next_level + g_next_world * 5;
    HiScore *h = k < 50 ? &g_hiscores[k] : &g_hiscores[49];
    uint16_t num[16];
    wfmt(num, 16, (const uint16_t[]){ '%', 'd', 0 }, h->score);
    str_put(exe_s16(0x4510d0), num);
    uint16_t *t = str_mut(exe_s16(0x451100));
    if (t && wlen(t) >= 5) put_time(t, h->time);
    uint16_t name[13];
    memcpy(name, h->name, sizeof h->name);
    name[12] = 0;
    str_put(exe_s16(0x451130), name);
}

static void breed_page(void)
{
    /* 0x427780: a tick on the breeds played; each breed's flock size as "N)" after the first character
       of its string (ids at 0x66000c) */
    for (int i = 0; i < 4; i++) {
        if (g_prog.breeds & bit(i))
            marker(exe_s32(0x451a68 + 8 * (uint32_t)i), exe_s32(0x451a6c + 8 * (uint32_t)i), 0x5b,
                   exe_s32(0x451a88 + 4 * (uint32_t)i));
        uint16_t *s = str_mut(exe_s16(0x66000c + 2 * (uint32_t)i));
        int n = g_flock_size[i];
        if (!s || wlen(s) < 4) continue;
        if (n < 10) {
            s[1] = (uint16_t)(n + '0');
            s[2] = ')';
            s[3] = 0;
        } else {
            s[1] = (uint16_t)(n / 10 + '0');
            s[2] = (uint16_t)(n % 10 + '0');
            s[3] = ')';
            s[4] = 0;
        }
    }
}

static void load_page(void)
{
    /* 0x427550: the four saves, "Empty" (string 0x38) where there is none */
    for (int i = 0; i < SAVE_SLOTS; i++) {
        uint16_t *t = load_text[i];
        wcp(t, str_get(0x38), t + 0x20);
        if (save_read(i)) wcp(t, wbuf(widen((const char *)g_slots[i] + 1), 0x100), t + 0x20);
        put_u32(0x450e3c + ITEM * (uint32_t)i, 0);
    }
}

static void save_page(void)
{
    /* 0x427640: the same for saving; each item points at the shared buffer, filled from the slot's
       name before the slot is read (so it holds "") */
    for (int i = 0; i < SAVE_SLOTS; i++) {
        uint8_t *s = g_slots[i];
        memset(s, 0, SAVE_SIZE);
        put_u32(0x451714 + ITEM * (uint32_t)i, widen((const char *)s + 1));
        const uint16_t *e = str_get(0x38);
        for (size_t k = 0; e[k] && k < SAVE_SIZE - 2; k++) s[1 + k] = (uint8_t)e[k];
        uint8_t raw[SAVE_SIZE];
        if (save_file(i, raw) && raw[0] == SAVE_VERSION) memcpy(s, raw, SAVE_SIZE);
    }
}

static int volume_index(float v)
{
    /* 0x427b00's search: the first step at or above v (the original runs on past the table) */
    int i = 0;
    while (i < 11 && exe_f32(0x45184c + 4 * (uint32_t)i) < v) i++;
    return i;
}

static void sliders_init(void)
{
    /* 0x427b00: the two volume sliders (objects 107, 108) at the volumes */
    const float vols[2] = { g_sfx_volume, g_music_volume };
    for (int k = 0; k < 2; k++) {
        AnimObj *a = obj(107 + k);
        if (!a) continue;
        int i = volume_index(vols[k]);
        a->d[0x1d] = i;
        a->y = a->d[0x18] - (a->d[0x18] - a->d[0x17]) * i / 0xb;
    }
}

static void cursor_to_first(int cursor, uint32_t buttons)
{
    /* the first button of the four still enabled */
    for (int i = 0; i < 4; i++) {
        int id = exe_s32(buttons + 4 * (uint32_t)i);
        AnimObj *b = obj(id);
        if (b && b->d[BTN_OFF] != -1) {
            if (cursor >= 0) g_anim[cursor].d[0x15] = id;   /* the original writes slot -1 without one */
            return;
        }
    }
}

static void colours(uint32_t va, int n, int on)
{
    for (int i = 0; i < n; i++) put_u32(va + ITEM * (uint32_t)i, 0x80808080);
    put_u32(va + ITEM * (uint32_t)on, 0xffffffff);
}

static void set_page(int p)
{
    /* Menu_SetPage 0x427870 */
    if (p == PAGE_MAIN) {
        objects_load(world_level_path(".pos"));
        tile_anims_load();
        sliders_init();
    } else if (p == PAGE_LEVELS && g_theme == 7) {
        p = PAGE_LAST;
    }
    sel = -1;
    naming = 0;
    idle = 0;
    page = p;
    int cursor = -1;
    for (int i = g_anim_hi - 1; i >= 0; i--)
        if (g_anim[i].move == 0x8001) {
            g_anim[i].d[0x15] = exe_s32(0x450cb8 + 4 * (uint32_t)p);
            g_anim[i].sub = 0;
            cursor = i;
        }
    g_lv.cam_min_x = exe_s32(0x450c48 + 8 * (uint32_t)p);
    g_lv.cam_max_x = g_lv.cam_min_x + 0x280;
    g_lv.cam_min_y = exe_s32(0x450c4c + 8 * (uint32_t)p);
    g_player.x = (float)(g_lv.cam_min_x + 0x140);
    g_lv.cam_max_y = g_lv.cam_min_y + 0x1e0;
    g_player.y = (float)(g_lv.cam_min_y + 0xf0);
    g_anim[g_player.slot].rec.scale_x = 0;
    g_anim[g_player.slot].rec.scale_y = 0;
    switch (p) {
    case PAGE_CONTROLS: colours(0x450f54, 4, g_input_mode); break;
    case PAGE_DIFFICULTY: colours(0x45117c, 3, g_difficulty); break;
    case PAGE_MAP: g_tile_anim[g_theme < 8 ? g_theme : 0].mode = 1; break;
    case PAGE_LOAD: load_page(); break;
    case PAGE_LEVELS: levels_page(); cursor_to_first(cursor, 0x451a58); break;
    case PAGE_LAST: last_page(); break;
    case PAGE_BREED: breed_page(); cursor_to_first(cursor, 0x451a88); break;
    case PAGE_BEST: best_page(); break;
    case PAGE_SAVE: save_page(); break;
    case PAGE_JUKEBOX:
        track = 0;
        put_s16(0x450fc8, 0x1d);
        break;
    }
}

static void actors_hold(void)
{
    /* the flock and the shepherd stand still (state 4) */
    for (int i = 0; i < MAX_SHEEP; i++) g_sheep[i].state = 4;
    g_player.state = 4;
    g_timer_on = 0;
}

static void menu_init00(void)
{
    /* Menu_Init00 0x427be0: the main page, or the best-times page after a level played for it */
    set_page(g_game_mode == 1 ? PAGE_BEST : PAGE_MAIN);
    actors_hold();
}

static void menu_init01(void)
{
    /* Menu_Init01 0x427c30: the save page between levels */
    set_page(PAGE_SAVE);
    actors_hold();
    naming = 0;
    g_hud_style = 0;
}

static void menu_init_map(void)
{
    /* Menu_InitMap 0x427c70: a world's map; its levels page once one is done */
    set_page(g_prog.levels == 0 ? PAGE_MAP : PAGE_LEVELS);
    actors_hold();
}

static void attract_step(void)
{
    /* 0x427cc0: after 0x707 idle ticks a demo, then the credits, then the best scores, in turn */
    if (++idle <= 0x707) return;
    if (attract == 0) {
        g_next_world = exe_s32(0x451a98 + 8 * (uint32_t)demo_i);
        g_next_level = exe_s32(0x451a9c + 8 * (uint32_t)demo_i);
        g_theme = (int8_t)g_next_world;
        world_select(g_next_world, g_next_level);
        /* TODO: the demo's input stream (0x40fb40 on the level's .rec) */
        g_app_sub = 0;
        g_prog.breeds = g_prog.levels = 0;
        g_prog.bonus = 0;
        g_breed = 0;
        g_char = 0;
        for (int i = 0; i < 4; i++) g_flock_size[i] = 10;
        g_demo = 2;
        g_app_state = APP_LEVEL;
        g_end = 100;
        g_difficulty = 1;
        demo_i = (demo_i + 1) & 3;
        attract = 1;
    } else if (attract == 1) {
        xp_unmount_all();
        xp_mount("main.xp");
        g_app_state = APP_CREDITS;
        g_app_sub = 0;
        attract = 2;
    } else {
        xp_unmount_all();
        xp_mount("main.xp");
        g_hiscore_mark = -1;
        g_app_state = APP_HISCORES;
        g_app_sub = 0;
        attract = 0;
    }
}

static bool held_new(int dik) { return g_keyheld[dik] > 0 && g_keyheld[dik] < 6; }

static void play_movie(const char *name)
{
    /* "data\biks\%s.bik" (0x451cb4) in the movie player (0x403a30). TODO: Bink */
    char path[64];
    snprintf(path, sizeof path, exe_str(0x451cb4), name);
    (void)path;
}

static void new_game(int world)
{
    /* the progress of a new game or of the training */
    int n = g_difficulty < 2 ? 10 : 0x14;
    g_theme = (int8_t)world;
    for (int i = 0; i < 4; i++) g_flock_size[i] = (int8_t)n;
    breeds_seen = 0;
    g_4b911c = -1;
    memset(g_prog.worlds, 0, 12);
    g_prog.bonus = 0;
    g_prog.breeds = 0;
    g_prog.levels = 0;
    g_prog.u84 = 0;
    g_prog.score = 0;
    g_next_world = world;
    g_next_level = 0;
    set_page(PAGE_CHAR);
}

static void load_slot_progress(int slot)
{
    memcpy(&g_prog, g_slots[slot] + 0x24, sizeof g_prog);
    g_breed = g_prog.breed;
    memcpy(&g_prog_loaded, g_slots[slot] + 0x24, sizeof g_prog_loaded);
}

static void jukebox(int t)
{
    /* the track's title; TODO: the music (0x4193c0 stops it, 0x41f160(track + 100) plays) */
    track = t;
    put_s16(0x450fc8, t == 0 ? 0x1d : exe_s16(0x450b9e + 2 * (uint32_t)t));
}

static bool name_key(void)
{
    /* 0x427ea0: a key that went down types its character (0x451bf8[language][dik]), backspace
       deletes, Enter ends */
    if (g_last_key == last_key) return false;
    last_key = g_last_key;
    uint16_t *name = name_buf();
    if (g_last_key < 0x40 && name_len < name_max) {
        int8_t c = exe_s8(exe_u32(0x451bf8 + 4 * (uint32_t)g_lang) + (uint32_t)g_last_key);
        if (c) {
            name[name_len++] = (uint16_t)c;
            name[name_len] = 0;
        }
    }
    if (g_last_key == 0xe) {
        if (name_len > 0) name[--name_len] = 0;
    } else if (g_last_key == DIK_RETURN) {
        return true;
    }
    return false;
}

static void menu_tick(void)
{
    /* Menu_Tick 0x427f40 */
    int was = sel, method = g_input_mode;
    bool back = false;
    if (held_new(DIK_ESCAPE)) {
        g_keyheld[DIK_ESCAPE] = 6;
        back = true;
    }
    if (!back_latch) {
        if (g_pad_buttons & 8) back = true, back_latch = 1;
    } else if (!(g_pad_buttons & 8)) {
        back_latch = 0;
    }
    g_anim[PLAYER_SLOT].rec.shadow = -1;
    back = back || sel == 10;
    switch (page) {
    case PAGE_MAIN:
        if (sel == -1) attract_step();
        switch (sel) {
        case 0: g_game_mode = 0; new_game(1); break;
        case 1: set_page(PAGE_LOAD); break;
        case 2: g_prog.u88 = 0; g_game_mode = 2; new_game(9); break;
        case 3: set_page(PAGE_OPTIONS); break;
        case 4: g_quit = true; break;
        }
        break;
    case PAGE_CHAR:
        if (sel >= 0 && sel < 4) {
            g_char = (int8_t)sel;
            set_page(PAGE_MAP);
            /* TODO: stop the music and free its streams (0x419490, 0x4066a0) */
            music_on = false;
            play_movie(exe_str(exe_u32(0x451828 + 4 * (uint32_t)g_char)));
        }
        if (back) set_page(PAGE_MAIN);
        break;
    case PAGE_LOAD:
        if (sel >= 0 && sel < 4 && g_slots[sel][0] == SAVE_VERSION) {
            load_slot = sel;
            hs_i = 0;
            load_slot_progress(sel);
            set_page(PAGE_LOADED);
        }
        if (back) set_page(PAGE_MAIN);
        break;
    case PAGE_OPTIONS:
        switch (sel) {
        case 0: set_page(PAGE_CONTROLS); break;
        case 1: set_page(PAGE_JUKEBOX); break;
        case 2: set_page(PAGE_DIFFICULTY); break;
        case 3: g_end = 10; break;
        }
        if (back) set_page(PAGE_MAIN);
        break;
    case PAGE_CONTROLS: {
        /* the methods not in use stop animating */
        static const int icon[4] = { 0x5c, 0x5b, 0x5a, 0x59 };
        for (int i = 0; i < 4; i++) {
            AnimObj *a = obj(icon[i]);
            if (method != i && a) a->countdown = 4;
        }
        if (sel >= 0 && sel < 4) {
            g_input_mode = sel;
            colours(0x450f54, 4, sel);
        }
        if (back) set_page(PAGE_OPTIONS);
        break;
    }
    case PAGE_JUKEBOX:
        if (sel == 0 && track > 0) jukebox(track - 1);
        else if (sel == 1 && track < 7) jukebox(track + 1);
        if (back) set_page(PAGE_OPTIONS);
        break;
    case PAGE_LOADED:
        if (sel == 0) {
            hs_i = 0;
            g_4b911c = -1;
            g_end = 9;
            load_slot_progress(load_slot);
            g_world = g_theme;
            g_game_mode = g_theme == 9 ? 2 : 0;
        } else if (sel == 1) {
            set_page(PAGE_BEST);
        }
        if (back) set_page(PAGE_LOAD);
        break;
    case PAGE_BEST:
        if (sel == 0) {
            g_prog.score = 0;
            g_prog.u84 = 0;
            g_game_mode = 1;
            int n = g_difficulty > 1 ? 0x14 : 10;
            for (int i = 0; i < 4; i++) g_flock_size[i] = (int8_t)n;
            g_end = 2;
        } else if (sel == 1) {
            if (hs_i < hs_n - 1) hs_i++;
            best_page();
        } else if (sel == 2) {
            if (hs_i > 0) hs_i--;
            best_page();
        }
        if (back) set_page(PAGE_LOADED);
        break;
    case PAGE_DIFFICULTY:
        if (sel >= 0 && sel < 3) {
            int old = g_difficulty;
            g_difficulty = (int8_t)sel;
            put_u32(0x45117c + ITEM * (uint32_t)old, 0x80808080);
            put_u32(0x45117c + ITEM * (uint32_t)sel, 0xffffffff);
        }
        if (back || was == 10) set_page(PAGE_OPTIONS);
        break;
    case PAGE_BREED:
        if (sel >= 0 && sel < 4) {
            g_breed = sel;
            if (!(breeds_seen & bit(sel))) {
                /* TODO: the music stops (0x419490, 0x4193c0) */
                play_movie(exe_str(exe_u32(0x451838 + 4 * (uint32_t)sel)));
                breeds_seen = (int8_t)(breeds_seen | bit(sel));
            }
            g_end = 2;
        }
        if (back) {
            g_menu_back = 1;
            set_page(PAGE_LEVELS);
        }
        break;
    case PAGE_LEVELS:
        if (g_cheats && g_keyheld[0x30] == 1) {
            /* B: every level, breed and bonus done; on to the world's last */
            g_breed = 0;
            g_prog.levels = g_prog.breeds = g_prog.bonus = 0xf;
            g_next_level = 5;
            g_end = 2;
        }
        if (sel != -1) {
            g_next_level = sel + 1;
            set_page(PAGE_BREED);
        }
        break;
    case PAGE_MAP:
        if (!music_on) {
            /* TODO: the level's sounds (0x401730) and the music (0x41f160(100)) */
        }
        if (method != 2 && method != 3) pad_read();
        if (g_pad_buttons == 0) lat_button = 0;
        else lat_button++;
        if (held_new(DIK_RETURN) || lat_button == 1) {
            g_keyheld[DIK_RETURN] = 6;
            set_page(PAGE_LEVELS);
        }
        if (held_new(DIK_SPACE) || lat_button == 1) {
            g_keyheld[DIK_SPACE] = 6;
            set_page(PAGE_LEVELS);
        }
        break;
    case PAGE_SAVE: {
        int s = sel;
        if (sel >= 0 && sel < 4) {
            save_slot = sel;
            last_key = g_last_key;
            s = -1;
            sel = -1;
            name_len = 0;
            uint16_t *name = name_buf();
            if (g_slots[save_slot][0]) {
                /* the saved name to edit (the original copies without a limit) */
                const uint8_t *src = g_slots[save_slot] + 1;
                int n = 0;
                do name[n] = src[n]; while (src[n++] && n < 0x1f);
                name[n] = 0;
                name_len = n - 1;
            }
            naming = 1;
        }
        if (back || s == 10) {
            g_4b911c = -1;
            g_end = 8;
        }
        break;
    }
    case PAGE_LAST:
        if (sel != -1) {
            g_next_level = sel + 1;
            g_end = 2;
        }
        break;
    }
    if (naming) {
        name_max = 0xd;
        put_u32(0x451714 + ITEM * (uint32_t)save_slot, NAME_BUF);
        if (name_key()) {
            naming = 0;
            char narrow[0x20];
            const uint16_t *name = name_buf();
            for (int i = 0; i < 0x20; i++) narrow[i] = (char)name[i];
            narrow[0x1f] = 0;
            save_write(save_slot, narrow);
            g_4b911c = -1;
            g_end = 8;
        }
    }
    g_player.in_dx = -1000;
}

static void menu_draw(void)
{
    /* Menu_Draw 0x428b40: the page's items; the name being typed blinks a '_' */
    blink++;
    for (uint32_t it = items_of(page); exe_s32(it) != -1; it += ITEM) {
        int strid = exe_s16(it + 8);
        const uint16_t *s = strid ? str_mut(strid) : NULL;
        uint16_t tmp[0x100];
        uint32_t ptr = exe_u32(it + 0x14);
        if (!s && page == PAGE_LOAD && it >= 0x450e28 && it < 0x450e28 + 4 * ITEM)
            s = load_text[(it - 0x450e28) / ITEM];
        if (!s && ptr) s = ptr == NAME_BUF ? name_buf() : exe_wtext(ptr, tmp, 0x100);
        if (!s) continue;
        int x = exe_s32(it), y = exe_s32(it + 4) + 4, font = exe_s8(it + 0x10);
        uint32_t rgb = exe_u32(it + 0xc);
        if (ptr == NAME_BUF && s == name_buf() && blink & 0x10) {
            uint16_t *name = name_buf();
            name[name_len] = '_';
            name[name_len + 1] = 0;
            font_draw(x, y, name, font, rgb);
            name[name_len] = 0;
        } else {
            font_draw(x, y, s, font, rgb);
        }
    }
}

/* ---------------------------------------------------------------- moves 0x451c30 */

static void menu_move_00(AnimObj *a)
{
    /* 0x428c10: a button that is lit (d1c) */
    if (a->d[0x1c]) a->rec.tint = 7;
}

/* A button's corner where the cursor sits. */
static int btn_x(const AnimObj *b) { return b->mirror ? b->x - b->rec.w : b->x; }

static void menu_move_01(AnimObj *a)
{
    /* 0x428c30: the cursor on button d15; arrows (or the stick) step to the neighbours, up / down
       move a slider (move 0x8003), Enter (or a pad button) selects */
    int32_t *d = a->d;
    if (d[0x15] == -1 || naming) return;
    g_cam_target = a;
    sel = -1;
    AnimObj *b = obj(d[0x15]);
    if (!b) return;
    int dir = -1;
    switch (a->sub) {
    case 0:
        a->rec.mode = 2;
        a->x = btn_x(b) + 0x20;
        a->y = b->y + 0x14;
        anim_set(a->slot, 0x4d, 8);
        anim_set(b->slot, b->d[0x1b], -1);
        a->sub = 1;
        return;
    case 1:
        if (g_input_mode != 2 && g_input_mode != 3) pad_read();
        lat_right = g_pad_x < 0x1f5 ? 0 : (int8_t)(lat_right + 1);
        lat_left = g_pad_x < -500 ? (int8_t)(lat_left + 1) : 0;
        lat_up = g_pad_y < -500 ? (int8_t)(lat_up + 1) : 0;
        lat_down = g_pad_y < 0x1f5 ? 0 : (int8_t)(lat_down + 1);
        lat_button = g_pad_buttons & 3 ? (int8_t)(lat_button + 1) : 0;
        if (held_new(DIK_LEFT) || lat_left == 1) dir = 0;
        if (held_new(DIK_RIGHT) || lat_right == 1) dir = 1;
        if (held_new(DIK_UP) || lat_up == 1) dir = 2;
        if (held_new(DIK_DOWN) || lat_down == 1) dir = 3;
        if (dir != -1) {
            if (b->move == 0x8003 && dir > 1) {
                if (dir == 2 && b->d[0x1d] < 0xb) {
                    b->d[0x1d]++;
                    a->sub = 4;
                } else if (dir == 3 && b->d[0x1d] > 0) {
                    b->d[0x1d]--;
                    a->sub = 4;
                }
            } else if (b->d[0x15 + dir]) {
                anim_set(b->slot, b->d[0x1a], -1);
                d[0x16] = b->d[0x15 + dir];
                d[0x1c] = 0;
                d[0x1d] = d[0x1e] = 0xc;
                anim_set(a->slot, 0x4b, 8);
                a->sub = 2;
            }
        }
        if (held_new(DIK_RETURN) || lat_button == 1) {
            anim_set(b->slot, b->d[0x1a], -1);
            g_keyheld[DIK_RETURN] = 6;
            sel = b->d[BTN_OFF];
            /* TODO: the select sound (0x41f3a0(2, 0, 0)) at full volume (0x4376b0, 0x4060d0) */
        }
        return;
    case 2:
        sound_play_at(a, 0, 0, 0);
        a->sub = 3;
        /* fall through */
    case 3: {
        idle = 0;
        int t = ++d[0x1c];
        AnimObj *c = obj(d[0x16]);
        if (t < 0x15 && c) {
            int bx = btn_x(b), cx = btn_x(c);
            a->x = (cx - bx) * t / 0x14 + 0x20 + bx;
            a->y = (c->y - b->y) * t / 0x14 + 0x14 + b->y;
            a->frame = t / 2;
            anim_set_frame(a);
            return;
        }
        d[0x15] = d[0x16];
        sound_play_at(a, 1, 0, 0);
        a->sub = 0;
        return;
    }
    case 4:
        sound_play_at(a, 0, 0, 0);
        a->sub = 5;
        /* fall through */
    case 5: {
        idle = 0;
        int step = b->d[0x18] - (b->d[0x18] - b->d[0x17]) * b->d[0x1d] / 0xb - b->y;
        if (step < -3) step = -3;
        else if (step > 3) step = 3;
        b->y += step;
        a->y = b->y + 0x14;
        if (step == 0) {
            float v = exe_f32(0x45184c + 4 * (uint32_t)b->d[0x1d]);
            if (b->d[BTN_OFF] == 2) {
                g_sfx_volume = v;
                g_sfx_level = b->d[0x1d];
            }
            if (b->d[BTN_OFF] == 3) g_music_volume = v;
            a->sub = 0;
        }
        return;
    }
    }
}

static void menu_move_02(AnimObj *a)
{
    /* 0x429070: the shepherd on the world map walks path (world - 1); off the map page he waits
       off the screen */
    if (page == PAGE_MAP) {
        if (a->sub == 0) {
            path_begin(a, g_theme - 1, 1, 1);
            a->sub = 1;
        } else if (a->sub == 1) {
            path_follow(a);
        }
    } else {
        a->x = -1000;
        a->sub = 0;
    }
}

static void move_nop(AnimObj *a) { (void)a; }

bool menu_call(uint32_t va)
{
    switch (va) {
    case 0x427be0: menu_init00(); return true;
    case 0x427c30: menu_init01(); return true;
    case 0x427c70: menu_init_map(); return true;
    case 0x427f40: menu_tick(); return true;
    case 0x428b40: menu_draw(); return true;
    default: return false;
    }
}

bool menu_actor(uint32_t va, int32_t *actor)
{
    (void)va; (void)actor;
    return false;
}

MoveFn menu_move(uint32_t va)
{
    switch (va) {
    case 0x428c10: return menu_move_00;
    case 0x428c30: return menu_move_01;
    case 0x429070: return menu_move_02;
    case 0x4039f0: return move_nop;
    default: return NULL;
    }
}
