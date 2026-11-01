#include "save.h"
#include "exe.h"
#include "platform.h"
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
