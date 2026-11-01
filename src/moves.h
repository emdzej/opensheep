/* Object move routines: the standard ones (table 0x44c57c, Move_NN) and the dispatch to the level's
   own (descriptor +0x4c, levels.c). A routine runs once per tick for its object, after the animation
   step (0x402700); the object's sub-state is a->sub and its scratch dwords d[0x15..]. */
#pragma once
#include "objects.h"

typedef void (*MoveFn)(AnimObj *);

extern int g_pickups;                   /* 0x4b972c: scoring pickups collected */
extern int g_score_flash;               /* 0x4b9848: HUD flash ticks */
extern uint8_t g_bonus;                 /* 0x4a1e90: bonus levels' flags (0x44b194 per level) */
extern int g_timer_on;                  /* 0x4a3e90: the level clock runs */
extern int8_t g_end_press_pad, g_end_press_key;   /* 0x4a4141, 0x4a4124: presses during the end */
extern int g_hint_owner;                /* 0x4b9744: id of the sign showing the hint */
extern int g_hint_shown;                /* 0x4b9754 (the hint's line list; non-zero while shown) */
extern int g_hint_x, g_hint_y;          /* 0x4b970c, 0x4b9710 */
extern int g_hint;                      /* the number passed to 0x411a70 */
extern int g_switch_wait[32];           /* 0x4b9680: Move_14 per-actor cooldown */

/* 0x402700's dispatch: a->move n runs standard routine n, n | 0x8000 the level's routine n. */
void move_run(AnimObj *a);
/* 0x403130: gate g (descriptor +0x54) closed (open = 0) or open: copy its map cells into place. */
void gate_set(int g, int open);
/* Part of 0x401950: count the gates and close them all. */
void gates_init(void);
/* 0x411560: push the sheep and the shepherd out of the rectangle (sheep in state 0x15 walk again). */
void push_out_of_rect(int x, int y, int w, int h);
/* 0x411a70: the hint text with number n (text file 0x47db60; TODO: the texts). */
void hint_show(int n);
