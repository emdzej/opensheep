#include "movie.h"
#include "bink.h"
#include "flow.h"
#include "input.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>

static Bink *g_movie;               /* 0x4a3eb0's player, its Bink handle */
static int ticks;                   /* app frames since the movie opened */
static int shown;                   /* frames done (Bink's FrameNum - 1) */
static float vol;                   /* BinkSetVolume(g_sfx_level * 0xba2), of 32768 */
static float last[2], cur[2];
static int phase;

/* The movie's sound, mixed as Bink's own DirectSound buffer: its 22050 Hz doubled to the mixer's
   rate by linear interpolation. */
static void feed(float *dst, int n)
{
    if (!g_movie || !bink_audio_rate(g_movie)) return;
    for (int i = 0; i < n; i++) {
        float l, r;
        if (phase == 0) {
            if (bink_audio(g_movie, cur, 1) != 1) cur[0] = cur[1] = 0.0f;
            l = 0.5f * (last[0] + cur[0]);
            r = 0.5f * (last[1] + cur[1]);
        } else {
            l = cur[0];
            r = cur[1];
            last[0] = cur[0];
            last[1] = cur[1];
        }
        phase ^= 1;
        dst[2 * i] += l * vol;
        dst[2 * i + 1] += r * vol;
    }
}

static void stop(void)
{
    sound_feed(NULL);
    bink_close(g_movie);
    g_movie = NULL;
}

void movie_play(const char *path)
{
    if (g_movie) stop();
    g_movie = bink_open(path);
    if (!g_movie) {
        fprintf(stderr, "cannot play %s\n", path);
        return;
    }
    ticks = shown = phase = 0;
    memset(last, 0, sizeof last);
    memset(cur, 0, sizeof cur);
    vol = (float)(g_sfx_level * 0xba2) / 32768.0f;
    /* the rate is 22050 on every disc movie; any other plays off speed */
    sound_feed(feed);
}

bool movie_active(void) { return g_movie != NULL; }

void movie_frame(Framebuffer *fb)
{
    if (!g_movie) return;
    /* BinkWait: the next frame is due at its time from the start */
    long long due = (long long)shown * 1000 * bink_fps_den(g_movie) / bink_fps_num(g_movie);
    if (due <= (long long)ticks * 16) {
        input_keys_tick();          /* 0x4056f0; then the mouse (0x405640) */
        bool more = bink_decode(g_movie);
        if (more) bink_copy(g_movie, fb->pixels, fb->pitch, fb->w, fb->h, 0, 0);
        shown++;
        /* BinkDoFrame then BinkNextFrame until FrameNum reaches Frames */
        if (!more || shown >= bink_frames(g_movie) || flow_any_key()) stop();
        pad_read();                 /* 0x422760 */
    }
    ticks++;
}
