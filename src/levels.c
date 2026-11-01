#include "levels_int.h"
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

static const struct {
    bool (*actor)(uint32_t va, int32_t *actor);
    bool (*call)(uint32_t va);
    MoveFn (*move)(uint32_t va);
} worlds[] = {
    { lv1_actor, NULL, lv1_move }, { lv2_actor, NULL, lv2_move }, { lv3_actor, lv3_call, lv3_move },
    { lv4_actor, lv4_call, lv4_move }, { lv5_actor, NULL, lv5_move },
};

void level_hook(uint32_t off)
{
    uint32_t va = desc_u32(off);
    if (!va) return;
    for (size_t i = 0; i < sizeof worlds / sizeof worlds[0]; i++)
        if (worlds[i].call && worlds[i].call(va)) return;
    unported(off == DESC_INIT ? "init" : off == DESC_TICK ? "tick" : off == DESC_RELEASE ? "release" : "draw", va);
}

void level_actor_hook(int32_t *actor)
{
    uint32_t va = desc_u32(DESC_PLAYER);
    if (!va) return;
    for (size_t i = 0; i < sizeof worlds / sizeof worlds[0]; i++)
        if (worlds[i].actor(va, actor)) return;
    unported("actor", va);
}

MoveFn level_move(uint32_t va)
{
    for (size_t i = 0; i < sizeof worlds / sizeof worlds[0]; i++) {
        MoveFn f = worlds[i].move(va);
        if (f) return f;
    }
    return NULL;
}
