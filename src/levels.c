#include "levels.h"
#include "world.h"
#include <stdio.h>

static void unported(const char *what, uint32_t va)
{
    static uint32_t said[64];
    for (int i = 0; i < 64; i++) {
        if (said[i] == va) return;
        if (!said[i]) { said[i] = va; break; }
    }
    fprintf(stderr, "level %s hook 0x%x not ported\n", what, va);
}

void level_actor_hook(int32_t *actor)
{
    (void)actor;
    uint32_t va = desc_u32(DESC_PLAYER);
    if (!va) return;
    switch (va) {
    default: unported("actor", va); break;
    }
}
