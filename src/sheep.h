/* The flock (0x419f50..0x41e960). */
#pragma once
#include "game.h"

/* 0x4034a0: the 16-direction octant step from (x0, y0) toward (x1, y1) (even = axis / diagonal). */
int dir16_toward(int x0, int y0, int x1, int y1);
/* 0x41a780: step an actor (sheep or shepherd) toward (x, y) at its speed; true within 4 px. */
bool actor_walk_to(int32_t *actor, int x, int y);
/* 0x419f50: a sheep's (or the shepherd's) move type, with the effects the new state starts. */
void actor_set_state(int32_t *actor, int s);
