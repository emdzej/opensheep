/* Level 1.1 in play, headless: the flock stays put with no input (no trigger fires on its own), a
   sheep put on the horsebox ramp is penned, and the level clock ends the level. */
#include "exe.h"
#include "level.h"
#include "moves.h"
#include "play.h"
#include "rng.h"
#include "sheep.h"
#include "vfs_host.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void start(void)
{
    rng_init();
    game_new();
    if (!world_select(1, 1) || !level_load_resources()) { printf("cannot load 1.1\n"); exit(1); }
    level_start();
}

int main(void)
{
    if (!vfs_mount_default()) { fprintf(stderr, "no data (./cd or OPENSHEEP_DATA)\n"); return 2; }
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }

    start();
    CHECK(g_nsheep == 10, "flock %d", g_nsheep);
    for (int t = 0; t < 120; t++) play_tick();
    for (int i = 0; i < g_nsheep; i++)
        CHECK(g_sheep[i].state == 0 && g_sheep[i].iy > 1500, "sheep %d state %d at %d,%d", i, g_sheep[i].state,
              g_sheep[i].ix, g_sheep[i].iy);

    /* the ramp's special cells: x 1088..1119, y 2040..2087 */
    g_sheep[0].x = 1100.0f;
    g_sheep[0].y = 2060.0f;
    for (int t = 0; t < 60 && !g_saved; t++) play_tick();
    CHECK(g_saved == 1, "saved %d", g_saved);
    CHECK(g_sheep[0].alive != 0, "penned sheep alive %d", g_sheep[0].alive);

    start();
    g_clock = g_time_limit - 1;
    play_tick();                /* the end object appears after the objects' update ... */
    play_tick();                /* ... and its routine starts the end */
    CHECK(g_level_over == 1, "level over %d at the time limit", g_level_over);
    for (int t = 0; t < 400 && !g_end; t++) play_tick();
    CHECK(g_end == 4, "end %d", g_end);

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
