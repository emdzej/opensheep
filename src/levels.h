/* Per-level code: the descriptor's code pointers (world.h DESC_*) name functions in the exe; each is
   ported here and dispatched by its address. */
#pragma once
#include "game.h"

/* descriptor +0x18: the shepherd's states >= 100. */
void level_player_hook(Player *p);
