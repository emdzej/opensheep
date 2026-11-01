/* The four save slots (Saves\Game%02d.sav, 0x44 bytes each: +0 version 5, +1 the name, +0x24 the
   game in progress) and the best times per level. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "game.h"

enum { SAVE_SLOTS = 4, SAVE_SIZE = 0x44, SAVE_VERSION = 5 };

extern uint8_t g_slots[SAVE_SLOTS][SAVE_SIZE];   /* 0x4a1ef0 */

/* 0x427550 / 0x427640: read slot i into g_slots (zeroed when missing or of another version);
   false if there is none. */
int save_read(int i);
/* The file of slot i, whatever its version, into out (zeroed first); false if there is none. */
bool save_file(int i, uint8_t out[SAVE_SIZE]);
/* Save_Write 0x407700: name and g_prog (with the breed being played) into slot i, then to storage. */
void save_write(int i, const char *name);
/* The progress stored in slot i. */
Progress save_progress(int i);

/* 0x4a214c: the best result per level, indexed level + world * 5. */
typedef struct {
    int32_t score;
    int32_t time;
    uint16_t name[12];
} HiScore;
extern HiScore g_hiscores[50];

/* The registry values under SOFTWARE\Empire Interactive\Sheep (0x422a00 / 0x422a80), one storage
   key each (the value's name, ' ' as '_'): 0x40d380 reads Difficulty, Controller, Sfx Volume and
   Music Volume at start-up (0x408930), the main loop's exit (0x438350) writes them back. */
void settings_load(void);
void settings_save(void);
