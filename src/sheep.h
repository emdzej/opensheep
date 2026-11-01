/* The flock (0x419f50..0x41e960). */
#pragma once
#include "game.h"

extern int g_sheep_out;                 /* 0x4a3d80: sheep lost or penned */
extern int g_counted;                   /* 0x4bbd5c */
extern int g_carried;                   /* 0x4a40c4 */
extern AnimObj *g_carrier;              /* 0x4a40c8 */
extern float g_flock_speed;             /* 0x4bbd00 */

/* 0x4034a0: the 16-direction octant step from (x0, y0) toward (x1, y1) (even = axis / diagonal). */
int dir16_toward(int x0, int y0, int x1, int y1);
/* 0x41a780: step an actor (sheep or shepherd) toward (x, y) at its speed; true within 4 px. */
bool actor_walk_to(int32_t *actor, int x, int y);
/* 0x419f50: a sheep's (or the shepherd's) move type, with the effects the new state starts. */
void actor_set_state(int32_t *actor, int s);
void sheep_turn(int32_t *s, int target);                       /* 0x41b950 */
void sheep_out(int n);                                         /* 0x4032c0 */
void sheep_set_kind(int32_t *s, int k, int t);                 /* 0x41a520 */
void sheep_kind_drop(int32_t *s);                              /* 0x41c5b0 */
void sheep_launch(int32_t *s, int dir, int count, int height, int then, int speed);        /* 0x41a820 */
void sheep_launch_sound(int32_t *s, int dir, int count, int height, int then, int speed);  /* 0x41a8c0 */
void sheep_collide(int x, int y, int32_t *s, int act);         /* 0x41b560 */
void sheep_knocked(Sheep *a, int n);                           /* 0x41c630: n = a->slot */
void sheep_walk(Sheep *a);                                     /* 0x41c480: move type 0 */
void sheep_tick(void);                                         /* 0x41caf0 */
void sheep_state_unported(Sheep *a);
void sheep_animate(void);                                      /* 0x41e380 */
void sheep_collect(void);                                      /* 0x41e6d0 */
