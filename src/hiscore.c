/* The best scores: HiScore.tab (0x408850 / 0x4088b0, defaults 0x40ef50), a game's place in it
   (0x40fe20) and the screen that shows them a page at a time (0x40e420 / 0x40e6b0 / 0x40e8f0). */
#include "hiscore.h"
#include "exe.h"
#include "flow.h"
#include "game.h"
#include "hud.h"
#include "input.h"
#include "platform.h"
#include "rng.h"
#include "sprites.h"
#include "surface.h"
#include "cheats.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>

/* 0x4a20c8: 60 entries of 0x20 bytes, {u8 character, s32 score at +4, s32 time at +8, UTF-16 name[10]
   at +0xc}: 0-9 the games, 10 + 5 * (world - 1) + level - 1 the best of each level. Then the file's
   checksum byte; the original writes 0x800 bytes from here. */
enum { ENTRY = 0x20, ENTRIES = 60, TABLE = ENTRY * ENTRIES, FILE_SIZE = 0x800, NAME = 0xc };
static uint8_t tab[FILE_SIZE];
static uint8_t music_phase;   /* 0x4a907c */

static int32_t rd(int i, int off) { int32_t v; memcpy(&v, tab + ENTRY * i + off, 4); return v; }
static void wr(int i, int off, int32_t v) { memcpy(tab + ENTRY * i + off, &v, 4); }
static void name_get(int i, uint16_t out[11])
{
    memcpy(out, tab + ENTRY * i + NAME, 20);
    out[10] = 0;
}
/* A UTF-16 string into entry i's name. Like 0x422050 it doesn't stop at the name's 10 characters: a
   longer one runs on into the next entry (only with the typing cursor, 8 + 2 + the terminator, which
   clears the next entry's character byte). Returns where it stopped (the terminator). */
static size_t name_put(int i, size_t at, const uint16_t *s)
{
    size_t off = (size_t)ENTRY * (size_t)i + NAME + 2 * at;
    for (; *s && off + 2 < sizeof tab; s++, off += 2, at++) memcpy(tab + off, s, 2);
    memset(tab + off, 0, 2);
    return at;
}

static const char *key(void) { return exe_str(0x44b734); }   /* "HiScore.tab" */

static uint8_t crypt_key(uint32_t k) { return exe_u8(0x44b280 + (k & 0xf)); }   /* "KevDos Rules OK!" */

static bool load(void)
{
    /* 0x408850: each byte plus the key, their sum the byte after */
    uint8_t buf[FILE_SIZE];
    int n = plat_storage_get(key(), buf, sizeof buf);
    if (n <= TABLE || n > FILE_SIZE) return false;
    memcpy(tab, buf, (size_t)n);
    uint8_t sum = 0;
    for (uint32_t k = TABLE, i = 0; k > 0; k--, i++) {
        tab[i] = (uint8_t)(tab[i] + crypt_key(k));
        sum = (uint8_t)(sum + tab[i]);
    }
    return tab[TABLE] == sum;
}

static bool save(void)
{
    /* 0x4088b0: encrypted into the table itself, written, then read back */
    uint8_t sum = 0;
    for (uint32_t k = TABLE, i = 0; k > 0; k--, i++) {
        sum = (uint8_t)(sum + tab[i]);
        tab[i] = (uint8_t)(tab[i] - crypt_key(k));
    }
    tab[TABLE] = sum;
    bool ok = plat_storage_set(key(), tab, FILE_SIZE);
    if (ok) load();
    else for (uint32_t k = TABLE, i = 0; k > 0; k--, i++) tab[i] = (uint8_t)(tab[i] + crypt_key(k));
    return ok;
}

static void defaults(void)
{
    /* 0x40ef50: the developers' names (another list for language 8), 10000 down to 1000 */
    uint32_t names = g_lang != 8 ? 0x44b230 : 0x44b258;
    for (int i = 0; i < ENTRIES; i++) {
        uint16_t name[11];
        tab[ENTRY * i] = (uint8_t)(crt_rand() & 3);
        name_put(i, 0, exe_wtext(exe_u32(names + 4 * (uint32_t)(i % 10)), name, 11));
        wr(i, 4, i < 10 ? 10000 - 1000 * i : 1000);
        wr(i, 8, i < 10 ? 0x3840 : 36000);
    }
}

void hiscores_init(void)
{
    /* part of 0x408990 at start-up */
    memset(tab, 0, TABLE);
    if (!load()) {
        defaults();
        if (!save()) fprintf(stderr, "Could Not Save %s\n", key());
    }
}

int hiscore_insert(int world, int level, int mode, int score, int time)
{
    /* 0x40fe20: training (mode 2) never; a level for its best (mode 1) its own entry if better; a game
       its place in the ten, the ones below moved down */
    int at;
    if (mode == 2) return -1;
    if (mode == 1) {
        at = world * 5 + 4 + level;
        if (score <= rd(at, 4)) { save(); return -1; }
        wr(at, 4, score);
    } else {
        at = -1;
        for (int i = 9; i >= 0; i--)
            if (rd(i, 4) < score) at = i;
        if (at == -1) { save(); return -1; }
        memmove(tab + ENTRY * (at + 1), tab + ENTRY * at, (size_t)ENTRY * (size_t)(9 - at));
        wr(at, 4, score);
    }
    wr(at, 8, time);
    memset(tab + ENTRY * at + NAME, 0, 2);
    tab[ENTRY * at] = (uint8_t)g_char;
    save();
    return at;
}

/* ---------------------------------------------------------------- the screen */

static Surface *back;        /* 0x4b10d8 */
static int page;             /* 0x4a40dc: 0 the games, 1-7 a world's levels */
static int wipe;             /* 0x4ae080: 1 / 4 opening top-bottom / left-right, 2 / 8 closing */
static int wx0, wy0, wx1, wy1;   /* 0x4b0ef0, 0x4b0ef4, 0x4b0ef8, 0x4b0efc */
static int wait;             /* 0x4a3e94 */
static int blink;            /* 0x4b10dc */

void hiscores_start(void)
{
    /* 0x40e420 */
    surface_free(back);
    back = surface_load_bmp(exe_str(0x44bc24));
    sprite_bank_load_misc(SPR_BANK_MISC, "Misc.spr");
    sprite_bank_load_misc(SPR_BANK_TEXT_YELLOW, "TextYellow.spr");
    sprite_bank_load_misc(SPR_BANK_TEXT_BLUE, "TextBlue.spr");
    for (int c = 0; c < 4; c++) sprite_bank_load(100 + c, exe_str(0x44bc04 - 0x20 * (uint32_t)c));
    if (g_sound_fx) sound_stream_file(exe_str(0x44bacc), 1);
    for (int i = 0; i < 50; i++) {
        /* _wcsupr on the names of the first 50 */
        for (int k = 0; k < 10; k++) {
            uint8_t *c = tab + ENTRY * i + NAME + 2 * k;
            if (!c[0] && !c[1]) break;
            if (c[1] == 0 && c[0] >= 'a' && c[0] <= 'z') c[0] = (uint8_t)(c[0] - 0x20);
        }
    }
    wy1 = wy0 = 200;
    wx0 = 0;
    wx1 = 0x280;
    wipe = 1;
    page = 0;
    wait = 200;
    if (g_hiscore_mark != -1) {
        page = g_hiscore_mark < 10 ? 0 : (g_hiscore_mark - 5) / 5;
        uint16_t name[11];
        name_get(g_hiscore_mark, name);
        uint16_t *dst = menu_name();
        memcpy(dst, name, sizeof name);
    }
    menu_name_start();
    /* (0x439280, 0x439170 acquire the DirectInput devices again; nothing to do on the host) */
}

static void text_s(uint32_t fmt_va, int strid, int x, int y)
{
    uint16_t fmt[8], buf[128];
    wfmt(buf, 128, exe_wtext(fmt_va, fmt, 8), str_get(strid));
    text_draw(buf, x, y);
}

static void text_n(uint32_t fmt_va, int x, int y, int a, int b)
{
    uint16_t fmt[16], buf[64];
    wfmt(buf, 64, exe_wtext(fmt_va, fmt, 16), a, b);
    text_draw(buf, x, y);
}

static void headers(void)
{
    /* 0x40e9b0: the title over the whole screen, the rest inside the wipe */
    sprite_clip(0, 0, 0x280, 0x1e0);
    const uint16_t *t = str_get(page == 0 ? 0xce : 0xd4);
    int n = 0;
    while (t[n]) n++;
    text_draw(t, 0x140 - (int)((unsigned)(n * 15) >> 1), 10);
    sprite_clip(wx0, wy0, wx1, wy1);
    int y = 0;
    if (page == 0) {
        text_s(0x44bb80, 0xcf, 0x1e, 0x32);
    } else {
        y = 0x19;
        text_s(0x44bb80, 0xd1, 0xf, 0x4b);
        text_s(0x44bb80, exe_s16(0x450b9e + 2 * (uint32_t)page), 200, 0x32);
    }
    y += 0x32;
    text_s(0x44bb80, 0xd0, 0x96, y);
    text_s(0x44bb80, 0xb9, 0x14a, y);
    text_s(0x44bb80, 0xb7, 0x1fe, y);
}

static void row(int i, int rank_x, int rank, int y, int icon_y)
{
    uint16_t name[11];
    if (rank_x >= 0) text_n(0x44bc64, rank_x, y, rank, 0);
    name_get(i, name);
    text_draw(name, 0x96, y);
    text_n(0x44bc58, 0x14a, y, rd(i, 4), 0);
    int t = rd(i, 8);
    text_n(0x44bc44, 0x1fe, y, t / 3600, t / 60 % 60);
    sprite_draw(0x6e, icon_y, (int8_t)tab[ENTRY * i] + 100, 0, SPR_SCALE_1, SPR_SCALE_1);
}

static void rows(void)
{
    /* 0x40eb80 */
    if (page == 0) {
        for (int i = 0; i < 10; i++) row(i, 0x1e, i + 1, 0x50 + 0x28 * i, 0x50 + 0x28 * i - 5);
        return;
    }
    int n = page == 7 ? 1 : page == 9 ? 4 : 5;
    for (int k = 1; k <= n; k++) {
        int y = (k * 5 + 10) * 8;
        if (k == 5) text_draw(str_get(0xd3), 5, 0x118);   /* the world's last level */
        row(page * 5 + 4 + k, k == 5 ? -1 : 5, k, y, k * 0x28 + 0x4b);
    }
}

static void name_tick(void)
{
    /* 0x40e620: the name typed into the entry, a blinking "$I" after it; Enter saves it */
    blink++;
    wait = 200;
    size_t end = name_put(g_hiscore_mark, 0, menu_name());
    if (blink & 8) name_put(g_hiscore_mark, end, exe_wtext(0x44bc3c, (uint16_t[4]){ 0 }, 4));
    if (menu_name_key(8)) {
        name_put(g_hiscore_mark, 0, menu_name());
        save();
        g_hiscore_mark = -1;
    }
}

static bool wipe_step(void)
{
    /* 0x40ee10: true once the wipe is fully open or shut */
    switch (wipe) {
    case 1:
        if (wy0 > 0) wy0 -= 2;
        if (wy1 < 0x1e0 && (wy1 += 2) < 0x1e0) return false;
        return wy0 < 1;
    case 2:
        if (wy0 < 0xf0) wy0 += 2;
        return (wy1 < 0xf1 || (wy1 -= 2) < 0xf1) && wy0 > 0xef;
    case 4:
        if (wx0 > 0) wx0 -= 2;
        if (wx1 < 0x280 && (wx1 += 2) < 0x280) return false;
        return wx0 < 1;
    case 8:
        if (wx0 < 0x140) wx0 += 2;
        return (wx1 < 0x141 || (wx1 -= 2) < 0x141) && wx0 > 0x13f;
    }
    return false;
}

static void tick(void)
{
    if (g_hiscore_mark != -1) name_tick();
    if (!wipe_step() || wait-- != 0) return;
    if (wipe == 1 || wipe == 4) {
        /* open: shut it, one way or the other */
        wipe = crt_rand() & 1 ? 8 : 2;
        wx0 = 0; wx1 = 0x280; wy0 = 0; wy1 = 0x1e0;
        wait = 2;
    } else if (wipe == 2 || wipe == 8) {
        /* shut: the next page */
        wait = 200;
        if (++page > 7) g_app_sub = 2;
        if (!(crt_rand() & 1)) { wipe = 1; wx0 = 0; wx1 = 0x280; wy0 = wy1 = 0xf0; }
        else { wipe = 4; wx0 = wx1 = 0x140; wy0 = 0; wy1 = 0x1e0; }
    }
}

void hiscores_frame(Framebuffer *fb)
{
    /* 0x40e6b0, one tick a frame */
    input_keys_tick();
    input_map();
    if (g_hiscore_mark == -1 && flow_any_key()) g_app_sub = 2;
    memset(fb->pixels, 0, (size_t)fb->pitch * fb->h * 2);
    if (back) surface_blt_fast(fb, 0, 0, back, NULL);
    sprite_target(fb);
    headers();
    rows();
    tick();
    if (music_phase++ % 5 == 0) sound_tick();
}

void hiscores_end(void)
{
    /* 0x40e8f0 */
    surface_free(back);
    back = NULL;
    for (int c = 0; c < 4; c++) sprite_bank_free(100 + c);
    sprite_bank_free(SPR_BANK_TEXT_YELLOW);
    sprite_bank_free(SPR_BANK_TEXT_BLUE);
    sprite_bank_free(SPR_BANK_MISC);
    sprite_clip(0, 0, 0x280, 0x1e0);
}
