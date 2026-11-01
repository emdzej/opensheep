/* Level 1.1 in play, headless: the sheep stay put with no input (no trigger fires on its own), a
   sheep put on the horsebox ramp is penned, and the level clock ends the level. */
#include "hud.h"
#include "exe.h"
#include "level.h"
#include "moves.h"
#include "movie.h"
#include "play.h"
#include "player.h"
#include "rng.h"
#include "sheep.h"
#include "sound.h"
#include <math.h>
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
    /* the flock of 10 at the bottom, and the 7 the level's markers stand for (0x4016a0) at the top
       and the middle */
    CHECK(g_nsheep == 17, "flock %d", g_nsheep);
    for (int t = 0; t < 120; t++) play_tick();
    for (int i = 0; i < g_nsheep; i++)
        CHECK(g_sheep[i].state == 0 && (i < 10) == (g_sheep[i].iy > 1500), "sheep %d state %d at %d,%d", i,
              g_sheep[i].state, g_sheep[i].ix, g_sheep[i].iy);

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

    /* a demo: recorded from the level's start, ended by the menu (0x40f560), kept and read back, then
       played to its end */
    setenv("OPENSHEEP_STORAGE", "/tmp/opensheep_test", 1);
    g_demo = 1;
    start();
    for (int t = 0; t < 50; t++) play_tick();
    demo_finish();
    CHECK(g_demo == 3 && g_demo_len[0] == 2 && g_demo_stream[0][1].n == 50, "recording %d: %d runs, %d",
          g_demo, g_demo_len[0], g_demo_stream[0][1].n);
    demo_save_stored();
    g_demo_len[0] = 0;
    CHECK(demo_load_stored() && g_demo_len[0] == 2 && g_demo_stream[0][1].n == 50, "stored recording");
    g_demo = 2;
    start();
    int t = 0;
    for (; t < 100 && g_demo == 2; t++) play_tick();
    CHECK(g_demo == 3 && g_end == 6 && t == 51, "playback %d end %d after %d", g_demo, g_end, t);
    g_demo = 0;

    /* the breed's tuning written back (0x41e850) and read in place of the disc's */
    flock_vars_load(1);
    FlockVars disc = g_fvars;
    g_fvars.sheep_speed += 7;
    flock_vars_save(1);
    g_fvars = disc;
    flock_vars_load(1);
    CHECK(g_fvars.sheep_speed == disc.sheep_speed + 7 && g_fvars.bias_player == disc.bias_player &&
          g_flock.sheep_speed == g_fvars.sheep_speed, "stored flock vars %d", g_fvars.sheep_speed);
    remove("/tmp/opensheep_test/Factoral.txt");

    /* the sound lists: effect 50 (generic.slf) loaded, 16 effect slots, the music streamed */
    start();
    int v = sound_play(50, 0, 0);
    CHECK(v != 0, "effect 50");
    int n = 1;
    while (n < 20 && sound_play(50, 0, 0)) n++;
    CHECK(n == 16, "%d effect slots", n);
    sound_stop_sfx(-1);
    sound_tick();
    CHECK(sound_play(50, 0, 0) != 0, "a slot after the stop");
    sound_stop_sfx(-1);
    int m = sound_stream(100, 1, 1);
    CHECK(m != 0, "music");
    snd_set_vol(m, 0.95f);
    sound_tick();
    double e = 0;
    int frames;
    for (int t = 0; t < 30; t++) {
        sound_frame();
        const float *o = sound_output(&frames);
        for (int i = 0; i < frames * 2; i++) e += fabs(o[i]);
    }
    CHECK(frames == 705 || frames == 706, "%d frames", frames);
    CHECK(e > 1.0, "music silent");
    sound_stop_streams(-1);
    sound_free_all();

    /* the logos movie: pictures and sound, 25 fps over the 62.5 Hz frames, ends by itself */
    static uint16_t px[640 * 480];
    Framebuffer mfb = { 640, 480, 640, px };
    movie_play(exe_str(0x44b990));
    CHECK(movie_active(), "Logos.bik");
    e = 0;
    t = 0;
    long lit = 0;
    for (; t < 800 && movie_active(); t++) {
        movie_frame(&mfb);
        if (t == 300)
            for (int i = 0; i < 640 * 480; i++) lit += px[i] != 0;
        sound_frame();
        const float *o = sound_output(&frames);
        for (int i = 0; i < frames * 2; i++) e += fabs(o[i]);
    }
    /* the last of the 276 frames is due at 11000 ms, the 689th app frame */
    CHECK(t == 689, "logos over after %d frames", t);
    CHECK(lit > 640 * 400, "logos picture %ld lit", lit);
    CHECK(e > 100.0, "logos silent %.1f", e);

    /* the texts: "go" of the race's count-down, and a wide format with the '$' bank switch */
    CHECK(strings_load(), "GameStrings");
    CHECK(str_get(0x110)[0] != 0, "string 0x110 empty");
    uint16_t fmt[16], buf[16];
    wfmt(buf, 16, exe_wtext(0x44cb48, fmt, 16), 3, 7);
    CHECK(buf[0] == '$' && buf[1] == '0' && buf[2] == '3' && buf[3] == ':' && buf[5] == '7' && !buf[6],
          "format");

    /* the menu pages' items are filled by the exe's initialisers (exe_run at load): the main page's
       first label has a string, its terminator is at x -1 */
    uint32_t items = exe_u32(0x4517f0);
    CHECK(exe_u16(items + 8) != 0 && str_get((int16_t)exe_u16(items + 8))[0] != 0, "menu label");
    uint32_t it = items;
    while (exe_s32(it) != -1 && it < items + 0x18 * 32) it += 0x18;
    CHECK(exe_s32(it) == -1, "menu page 0 has no end");

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
