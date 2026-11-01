/* Random numbers: the C runtime's rand() (MSVC, 0x43ca87; srand 0x43ca7d) and the game's table of 1024
   rand() values (0x4bca30) read with a wrapping index, which the simulation uses so demos replay. */
#pragma once
#include <stdint.h>

void crt_srand(uint32_t seed);
int crt_rand(void);                 /* 0..0x7fff */

void rng_init(void);                /* 0x422830: srand(0), fill from index 1023 down, index = 0 */
uint32_t rng_next(void);            /* 0x422870 */
extern int g_rng_index;             /* 0x4cfd3c (reset when a demo starts) */
