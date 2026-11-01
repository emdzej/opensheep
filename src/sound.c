#include "sound.h"
#include "level.h"

int sound_play_at(AnimObj *a, int id, int flags, int arg)
{
    /* 0x401e00: within 80 px left / 80 px right and 160 / 160 px of the screen; then 0x41f3a0 plays
       it (not yet: returns 0, as when no voice is free) */
    (void)id; (void)flags; (void)arg;
    if (a->x < g_lv.cam_x - 0x50 || a->x > g_lv.cam_x + 0x2d0) return 0;
    if (a->y < g_lv.cam_y - 0xa0 || a->y > g_lv.cam_y + 0x280) return 0;
    return 0;
}
