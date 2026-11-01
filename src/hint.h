/* The hint signs' texts: data\local\<level>.txe, entries "NN" followed by their lines up to a ':' line,
   shown in a box at the bottom of the screen while a sign is touched. */
#pragma once

/* 0x401620: the level's hint text (none if the file is missing). */
void hint_text_load(void);
void hint_text_free(void);
/* 0x40b590: the hint box over the frame, while one is shown. */
void hint_draw(void);
