#include "world.h"
#include "exe.h"
#include "xp.h"
#include <stdio.h>
#include <string.h>

int g_world, g_level;
uint32_t g_desc;   /* 0x4a20b4 */

static char level_name[64];
static char level_dir[128];   /* 0x49f89c */

static uint32_t descriptor(int world, int level)
{
    if (world < 0 || world >= WORLDS || level < 0 || level >= LEVELS_PER_WORLD) return 0;
    uint32_t d = exe_u32(LEVEL_TABLE + 4 * (uint32_t)(world * LEVELS_PER_WORLD + level));
    /* Unused slots point at an empty descriptor in .bss (no name). */
    if (!d || !exe_contains(d, 4) || !exe_u32(d)) return 0;
    return d;
}

bool world_select(int world, int level)
{
    uint32_t d = descriptor(world, level);
    if (!d) return false;
    g_world = world;
    g_level = level;
    g_desc = d;
    exe_wstr(exe_u32(d), level_name, sizeof level_name);
    /* 0x4033a0: the folder is World0<w> for levels, World00 for the world map screens */
    snprintf(level_dir, sizeof level_dir, "Data\\World0%c\\%s\\", level == 0 ? '0' : '0' + world, level_name);
    xp_unmount_all();
    xp_mount("main.xp");
    xp_mount("sound.xp");
    char xp[32];
    if (level == 0) snprintf(xp, sizeof xp, "World00.xp");
    else snprintf(xp, sizeof xp, "World%02d.xp", world);
    xp_mount(xp);
    return true;
}

const char *world_level_name(void) { return level_name; }

const char *world_level_path(const char *ext)
{
    static char path[256];   /* 0x49c080 */
    snprintf(path, sizeof path, "%s%s%s", level_dir, level_name, ext);
    return path;
}

uint32_t desc_u32(uint32_t off) { return g_desc ? exe_u32(g_desc + off) : 0; }

const char *world_path_of(int world, int level, const char *ext)
{
    /* World_SetPaths 0x4033a0 + Level_Path 0x401000 for a level other than the selected one */
    static char path[256];
    char name[64];
    uint32_t d = descriptor(world, level);
    if (!d) return "";
    exe_wstr(exe_u32(d), name, sizeof name);
    snprintf(path, sizeof path, "Data\\World0%c\\%s\\%s%s", level == 0 ? '0' : '0' + world, name, name, ext);
    return path;
}

uint32_t world_desc_of(int world, int level) { return descriptor(world, level); }
