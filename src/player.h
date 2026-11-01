/* The shepherd (0x414b10..0x418010): input, demo streams, the move-type state machine, collision with
   the level's zones / masks / attractors / special cells, animation and drawing. */
#pragma once
#include "game.h"

extern int g_in_raw_x, g_in_raw_y;      /* 0x4b9820, 0x4b9824: this tick's raw input */
extern int g_key_x, g_key_y;            /* 0x4b0fa0, 0x4b0fa4: keyboard -1 / 0 / 1 */
extern int g_key_run, g_key_creep;      /* 0x4b0b58, 0x4a3b3c */
extern int g_pad_x, g_pad_y;            /* 0x4bc924, 0x4bc928 */
extern int8_t g_pad_buttons;            /* 0x4cfd44 */
extern int8_t g_cursor_key;             /* 0x4a4128 */
extern int g_player_moved;              /* 0x4a40c5 */
extern int g_score;                     /* 0x4a2068 */
extern int32_t *g_chain[32][2];         /* 0x4bbbf8 */
extern int (*g_demo_abort)(void);
/* 0x40fb40: a recording (.rec: four run counts, then the four streams of {u16 value, s16 ticks})
   into the demo streams; false (and the streams as they were) if it can't be read. */
bool demo_load(const char *path);
/* 0x406cc0's end: the streams set up for recording (g_demo 1) or playback (2). */
void demo_start(void);

void player_init(Player *p);                                   /* 0x418010 */
void player_tick(Player *p);                                   /* 0x415f10 */
void player_walk(Player *p);                                   /* 0x415780: move type 0 */
void player_jump(Player *p);                                   /* 0x415aa0: move type 1 */
void player_specials(int x, int y, Player *p, int act);       /* 0x417550 */
void player_set_state(Player *p, int s);                       /* 0x414b10 */
void player_set_kind(Player *p, int k, int t);                 /* 0x414ba0 */
void player_collide(int x, int y, Player *p, int act);         /* 0x417ef0 */
void player_launch(Player *p, int dir, int count, int height, int then, int speed);        /* 0x417250 */
void player_launch_sound(Player *p, int dir, int count, int height, int then, int speed);  /* 0x4172c0 */
void player_collect(Player *p);                                /* 0x417070 */
void cursor_collect(Player *p);                                /* 0x40bec0 */
