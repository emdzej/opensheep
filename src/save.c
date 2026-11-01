#include "save.h"
#include "exe.h"
#include "platform.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>

uint8_t g_slots[SAVE_SLOTS][SAVE_SIZE];
HiScore g_hiscores[50];

/* "Saves\Game%02d.sav" (0x451ca0) as a storage key: the folder is the store itself. */
static const char *slot_key(int i)
{
    static char key[32];
    char path[32];
    snprintf(path, sizeof path, exe_str(0x451ca0), i);
    const char *base = strrchr(path, '\\');
    snprintf(key, sizeof key, "%s", base ? base + 1 : path);
    return key;
}

bool save_file(int i, uint8_t out[SAVE_SIZE])
{
    memset(out, 0, SAVE_SIZE);
    int n = plat_storage_get(slot_key(i), out, SAVE_SIZE);
    return n >= 1 && n <= SAVE_SIZE;
}

int save_read(int i)
{
    memset(g_slots[i], 0, SAVE_SIZE);
    uint8_t buf[SAVE_SIZE];
    if (!save_file(i, buf) || buf[0] != SAVE_VERSION) return 0;
    memcpy(g_slots[i], buf, SAVE_SIZE);
    return 1;
}

void save_write(int i, const char *name)
{
    uint8_t *s = g_slots[i];
    s[0] = SAVE_VERSION;
    size_t n = 0;
    for (; name[n] && n < 0x23; n++) s[1 + n] = (uint8_t)name[n];
    if (n < 0x20) memset(s + 1 + n, 0, 0x20 - n);   /* the original pads only short names */
    memcpy(s + 0x24, &g_prog, sizeof g_prog);
    s[0x24 + 0x12] = (uint8_t)g_breed;
    /* the original patches the slot digit into "Saves\Game01.sav" (0x44add8) */
    plat_storage_set(slot_key(i), s, SAVE_SIZE);
}

Progress save_progress(int i)
{
    Progress p;
    memcpy(&p, g_slots[i] + 0x24, sizeof p);
    return p;
}

static const char *reg_key(uint32_t name)
{
    static char key[32];
    snprintf(key, sizeof key, "%s", exe_str(name));
    for (char *p = key; *p; p++)
        if (*p == ' ') *p = '_';
    return key;
}

/* 0x422a00: false when the value is missing */
static bool reg_read(uint32_t name, uint32_t *v)
{
    uint8_t b[4];
    if (plat_storage_get(reg_key(name), b, 4) != 4) {
        *v = 0xffffffffu;
        return false;
    }
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return true;
}

static void reg_write(uint32_t name, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    plat_storage_set(reg_key(name), b, 4);
}

void settings_load(void)
{
    uint32_t controller, difficulty, sfx, music;
    if (!reg_read(0x44ba44, &controller)) controller = 1;
    if (!reg_read(0x44ba38, &difficulty)) difficulty = 1;
    if (!reg_read(0x44ba2c, &sfx)) sfx = 0xff;
    if (!reg_read(0x44ba1c, &music)) music = 0xff;
    g_input_mode = (int)controller;
    g_difficulty = (int8_t)difficulty;
    /* the dwords as 64-bit integers, times 1/255 (0x448310); the effects' product stays on the FPU
       stack for the level, times 11 (0x44830c) */
    double k = exe_f32(0x448310), s = (double)sfx * k;
    g_sfx_volume = (float)s;
    g_music_volume = (float)((double)music * k);
    g_sfx_level = ftol(s * (double)exe_f32(0x44830c));
}

void settings_save(void)
{
    reg_write(0x44ba38, (uint32_t)(int32_t)g_difficulty);
    reg_write(0x44ba44, (uint32_t)g_input_mode);
    reg_write(0x44ba2c, (uint32_t)ftol((double)g_sfx_volume * exe_f64(0x448408)));
    reg_write(0x44ba1c, (uint32_t)ftol((double)g_music_volume * exe_f64(0x448408)));
}
