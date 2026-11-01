/* Per-level code: the descriptor's code pointers (world.h DESC_*) name functions in the exe; each is
   ported here and dispatched by its address. */
#pragma once
#include "game.h"

/* descriptor +0x18: the shepherd's and the sheep's move types >= 100 (an actor: Player or Sheep). */
void level_actor_hook(int32_t *actor);
