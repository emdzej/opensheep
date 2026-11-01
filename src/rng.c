#include "rng.h"

static uint32_t crt_state = 1;
static uint32_t table[1024];
int g_rng_index;

void crt_srand(uint32_t seed) { crt_state = seed; }

int crt_rand(void)
{
    crt_state = crt_state * 0x343fdu + 0x269ec3u;
    return (int)(crt_state >> 16 & 0x7fff);
}

void rng_init(void)
{
    crt_srand(0);
    for (g_rng_index = 0x3ff; g_rng_index >= 0; g_rng_index--) table[g_rng_index] = (uint32_t)crt_rand();
    g_rng_index = 0;
}

uint32_t rng_next(void)
{
    g_rng_index = (g_rng_index + 1) & 0x3ff;
    return table[g_rng_index];
}
