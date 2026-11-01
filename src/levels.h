/* Per-level code: the descriptor's code pointers (world.h DESC_*) name functions in the exe; each is
   ported here and dispatched by its address. */
#pragma once
#include "game.h"

/* descriptor +0x18: the shepherd's and the sheep's move types >= 100 (an actor: Player or Sheep). */
void level_actor_hook(int32_t *actor);
/* descriptor +0x0c init, +0x10 tick, +0x1c / +0x20 draw, +0x30 release (world.h DESC_*). */
void level_hook(uint32_t off);

#include "moves.h"
/* descriptor +0x4c: the C port of the level-specific move routine at va (NULL: not ported). */
MoveFn level_move(uint32_t va);
