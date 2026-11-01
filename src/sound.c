#include "sound.h"
#include "cheats.h"
#include "exe.h"
#include "game.h"
#include "level.h"
#include "player.h"
#include "vfs.h"
#include "world.h"
#include "xp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The buffer flags (+0x60) */
enum {
    F_MUSIC = 1,            /* a stream slot, the music volume */
    F_SFX = 2,              /* an effect slot, the effect volume */
    F_LOAD = 8,             /* given to the loader 0x405c40 */
    F_FADE = 0x800,
    F_LOOP = 0x1000,
    F_ONCE = 0x2000,
    F_STREAM = 0x10000,     /* read from the file as it plays (0x4062e0) */
    F_KEEP = 0x100000,      /* a slot drops it without releasing it (0x4195e0) */
    F_TRACK = 0x1000000,    /* the volume follows the object (0x406700) */
};
enum { VOICES = 256, IDS = 0xaa, ENTRIES = 0x2a8 / 4, SFX_SLOTS = 16, STREAM_SLOTS = 4 };
enum { CHUNK_FRAMES = 8192, MAX_OUT = SOUND_RATE / 50 + 2 };

typedef struct {
    int refs;
    int16_t *s;                 /* interleaved, channels per frame */
    uint32_t frames;
} Pcm;

typedef struct {
    bool used;
    int gen;                    /* of the handle */
    int head;                   /* a duplicate: the buffer it was made from */
    int16_t refs;               /* +4 */
    float vol;                  /* +8 */
    const int32_t *px, *py;     /* +0xc, +0x10: the object's position */
    uint32_t flags;             /* +0x60 */
    bool playing;               /* +0xa8 */
    int slot;                   /* +0xac */
    int ds_set;                 /* +0xb0 */
    float base, fade;           /* +0xb4, +0xb8 */
    bool fade_kill;             /* +0xbc */
    int chain;                  /* +0xc0: its next duplicate */
    /* the DirectSound buffer */
    bool ds_playing, ds_loop, event;    /* event: the end notification (+0x48), auto-reset */
    int ds;                     /* volume, 1/100 dB */
    int channels, rate, bits;
    uint64_t pos;               /* 32.32 source frames */
    Pcm *pcm;                   /* static: the samples (shared by the duplicates) */
    VfsFile *file;              /* streamed */
    uint32_t data_off, frames;
    int16_t *chunk;             /* frames [chunk_at, chunk_at + chunk_n) of the file */
    uint32_t chunk_at, chunk_n;
} Voice;

int g_sound_loaded;                     /* 0x4a19c8 */
int g_music, g_music2;                  /* 0x4a40cc, 0x4a40d0 */
static Voice voices[VOICES + 1];        /* [0] unused */
static int sfx_slot[SFX_SLOTS];         /* 0x4b9984 */
static int stream_slot[STREAM_SLOTS];   /* 0x4b9854 */

typedef struct { char *path; int buf; } IdEntry;   /* 0x4bbd6c: 8 bytes each */
static IdEntry *ids[IDS];
static bool have_ids;                   /* 0x4bbd6c != 0 */
static struct { int id; char *path; } entries[ENTRIES];   /* 0x4bbd64 */
static int nentries;                    /* 0x4bbd68 */

static float out[MAX_OUT * SOUND_CHANNELS];
static int out_n;

/* A handle is the pool index and the generation the entry had then, so one kept past its buffer's
   end (the original keeps dangling pointers) matches nothing. */
static Voice *V(int v)
{
    int i = v & 0x1ff;
    return v > 0 && i >= 1 && i <= VOICES && voices[i].used && voices[i].gen == v >> 9 ? &voices[i] : NULL;
}

static int voice_new(void)
{
    /* 0x405a80 then 0x405bd0 */
    for (int i = 1; i <= VOICES; i++)
        if (!voices[i].used) {
            Voice *o = &voices[i];
            int gen = (o->gen + 1) & 0x3fffff;
            memset(o, 0, sizeof *o);
            o->gen = gen;
            o->used = true;
            o->vol = o->base = 1.0f;
            o->slot = -1;
            o->ds = -10000;
            return i | gen << 9;
        }
    fprintf(stderr, "sound: out of buffers\n");   /* the original allocates on and on */
    return 0;
}

static void voice_delete(int v)
{
    Voice *o = V(v);
    if (!o) return;
    for (int i = 1; i <= VOICES; i++) {
        Voice *p = &voices[i];
        if (p->used && p->chain == v) p->chain = o->chain;     /* out of its chain */
    }
    if (o->pcm && --o->pcm->refs == 0) { free(o->pcm->s); free(o->pcm); }
    if (o->file) vfs_close(o->file);
    free(o->chunk);
    o->used = false;
    /* a buffer takes the duplicates made from it along */
    for (int i = 1; i <= VOICES; i++)
        if (voices[i].used && voices[i].head == v) voice_delete(i | voices[i].gen << 9);
}

static void voice_release(int v)
{
    Voice *o = V(v);
    if (o && --o->refs == 0) voice_delete(v);
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* The RIFF header: format and where the samples are (0x423890, 0x423ad0). */
static bool wav_header(const uint8_t *h, size_t n, Voice *o, uint32_t *data_off, uint32_t *data_len)
{
    if (n < 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
    bool fmt = false;
    for (size_t p = 12; p + 8 <= n;) {
        uint32_t len = le32(h + p + 4);
        if (!memcmp(h + p, "fmt ", 4) && p + 8 + 16 <= n) {
            if ((h[p + 8] | h[p + 9] << 8) != 1) return false;    /* PCM only */
            o->channels = h[p + 10] | h[p + 11] << 8;
            o->rate = (int)le32(h + p + 12);
            o->bits = h[p + 22] | h[p + 23] << 8;
            fmt = true;
        } else if (!memcmp(h + p, "data", 4)) {
            *data_off = (uint32_t)p + 8;
            *data_len = len;
            return fmt && (o->channels == 1 || o->channels == 2) && (o->bits == 8 || o->bits == 16) &&
                   o->rate > 0;
        }
        p += 8 + (size_t)len + (len & 1);
    }
    return false;
}

static void decode(const Voice *o, const uint8_t *src, uint32_t frames, int16_t *dst)
{
    uint32_t n = frames * (uint32_t)o->channels;
    for (uint32_t i = 0; i < n; i++)
        dst[i] = o->bits == 8 ? (int16_t)((src[i] - 128) << 8) : (int16_t)(src[2 * i] | src[2 * i + 1] << 8);
}

static void full_path(const char *path, char *p, size_t cap)
{
    /* 0x405dd0: ".wav" (0x44ad80) unless the name has it */
    snprintf(p, cap, "%s%s", path, strstr(path, exe_str(0x44ad80)) ? "" : exe_str(0x44ad80));
}

/* 0x405c40 / 0x405ea0: load the file into the buffer, or open it for streaming. */
static bool voice_load(int v, const char *path, uint32_t flags)
{
    Voice *o = V(v);
    char p[300];
    full_path(path, p, sizeof p);
    o->flags = flags;
    uint32_t off, len;
    if (flags & F_STREAM) {
        VfsFile *f = file_open(p);
        uint8_t h[512];
        int64_t n = f ? vfs_read_at(f, 0, h, sizeof h) : -1;
        if (n <= 0 || !wav_header(h, (size_t)n, o, &off, &len)) { if (f) vfs_close(f); return false; }
        uint64_t size = vfs_file_size(f);
        if (off > size) off = (uint32_t)size;
        if (len > size - off) len = (uint32_t)(size - off);
        o->file = f;
        o->data_off = off;
        o->frames = len / (uint32_t)(o->channels * o->bits / 8);
        o->chunk = malloc((size_t)CHUNK_FRAMES * 2 * sizeof *o->chunk);
        return o->chunk != NULL;
    }
    size_t n;
    uint8_t *d = file_load(p, &n);
    if (!d || !wav_header(d, n, o, &off, &len)) { free(d); return false; }
    if (len > n - off) len = (uint32_t)(n - off);
    Pcm *pcm = calloc(1, sizeof *pcm);
    pcm->frames = len / (uint32_t)(o->channels * o->bits / 8);
    pcm->s = malloc((size_t)pcm->frames * (size_t)o->channels * sizeof *pcm->s + 1);
    decode(o, d + off, pcm->frames, pcm->s);
    free(d);
    pcm->refs = 1;
    o->pcm = pcm;
    o->frames = pcm->frames;
    return true;
}

/* 0x406770: a duplicate of a loaded buffer. Its +0xb4 starts at 0, not 1.0 as a new buffer's
   (0x405bd0), and nothing raises it: every duplicate plays at -70 dB, so only the first of a sound
   playing over itself is heard. */
static int voice_dup(int v)
{
    int d = voice_new();
    Voice *o = V(v), *n = V(d);
    if (!n) return 0;
    n->flags = o->flags;
    n->channels = o->channels;
    n->rate = o->rate;
    n->bits = o->bits;
    n->pcm = o->pcm;
    if (n->pcm) n->pcm->refs++;
    n->frames = o->frames;
    n->base = 0.0f;
    n->head = o->head ? o->head : v;
    return d;
}

/* 0x4062a0 */
static int ds_volume(const Voice *o)
{
    double x = (double)o->base * (double)o->vol;
    if (o->flags & F_MUSIC) x *= (double)g_music_volume;
    else if (o->flags & F_SFX) x *= (double)g_sfx_volume;
    return ftol(-(7000.0 - 7000.0 * x));
}

/* 0x405cf0 */
static void voice_play(int v)
{
    Voice *o = V(v);
    if (!o || (!o->pcm && !o->file)) return;
    o->ds = ds_volume(o);
    o->pos = 0;
    o->event = false;
    o->ds_playing = true;
    o->ds_loop = (o->flags & (F_LOOP | F_STREAM)) != 0;
    o->playing = true;
}

/* 0x406b10: 1 near the shepherd, 0 beyond 400 px */
static float distance_volume(const Voice *o)
{
    if (!o->px || !o->py) return 1.0f;
    double dx = (double)*o->px - ftol((double)g_player.x), dy = (double)*o->py - ftol((double)g_player.y);
    double d = sqrt(dx * dx + dy * dy);
    if (d > 400.0) return 0.0f;
    if (d < 80.0) return 1.0f;
    return (float)(1.0 - d * (double)0.0025f * 0.25);
}

/* 0x4061f0 */
static bool voice_fade(int v)
{
    Voice *o = V(v);
    if (!(o->flags & F_FADE)) return true;
    if (o->fade <= 0.0f) {
        if (o->base > 1.0f) { o->base = 1.0f; o->flags &= ~(uint32_t)F_FADE; }
    } else if (o->base < 0.0f) {
        if (o->fade_kill) {
            o->playing = false;
            o->flags &= ~(uint32_t)F_FADE;
            return false;
        }
        o->base = 0.0f;
        o->flags &= ~(uint32_t)F_FADE;
        o->fade = 0.0f;
    }
    return true;
}

void snd_set_vol(int v, float vol)
{
    Voice *o = V(v);
    if (o) o->vol = vol;
}

float snd_vol(int v)
{
    Voice *o = V(v);
    return o ? o->vol : 0.0f;
}

bool snd_update(int v)
{
    /* 0x4060d0 */
    Voice *o = V(v);
    if (!o || !o->playing) return false;
    if (o->flags & F_TRACK) o->vol = distance_volume(o);
    int ds = ds_volume(o);
    if (o->ds_set != ds) { o->ds = ds; o->ds_set = ds; }
    if (o->flags & F_STREAM) {
        /* the refill (0x4062e0) fails at the end of a file that does not loop */
        if (!o->ds_playing) o->playing = false;
        return o->playing;
    }
    bool ended = o->event;
    o->event = false;
    if (ended && !(o->flags & F_LOOP)) {
        o->playing = false;
        return false;
    }
    if (!voice_fade(v)) return false;
    o->playing = true;
    return true;
}

void snd_stop(int v)
{
    /* 0x406650 */
    Voice *o = V(v);
    if (!o || !o->playing) return;
    if (o->flags & F_STREAM) o->pos = 0;    /* the file back to its start (0x423be0) */
    o->ds_playing = false;
    o->playing = false;
    o->flags &= ~(uint32_t)F_FADE;
}

void sound_follow(int v, AnimObj *a)
{
    /* 0x406700 */
    Voice *o = V(v);
    if (!o) return;
    o->px = &a->x;
    o->py = &a->y;
    if (o->flags & F_LOOP) o->flags |= F_TRACK;
}

/* ------------------------------------------------------------------ slots */

static bool sfx_add(int v)
{
    /* 0x418e90 */
    for (int i = 0; i < SFX_SLOTS; i++)
        if (sfx_slot[i] == 0) { sfx_slot[i] = v; V(v)->slot = i; return true; }
    return false;
}

static bool stream_add(int v)
{
    /* 0x418e50 */
    for (int i = 0; i < STREAM_SLOTS; i++)
        if (stream_slot[i] == 0) { stream_slot[i] = v; V(v)->slot = i; return true; }
    return false;
}

static void stream_drop(int i)
{
    /* 0x4195e0(v, 0) for a stream */
    int v = stream_slot[i];
    stream_slot[i] = 0;
    if (V(v) && !(V(v)->flags & F_KEEP)) voice_release(v);
}

void sound_tick(void)
{
    /* 0x419120 (the group volumes changing only recompute, 0x4062a0 with the result dropped) */
    for (int i = 0; i < STREAM_SLOTS; i++)
        if (stream_slot[i] && !snd_update(stream_slot[i])) stream_drop(i);
    for (int i = 0; i < SFX_SLOTS; i++)
        if (sfx_slot[i] && !snd_update(sfx_slot[i])) sfx_slot[i] = 0;
}

/* 0x418cd0 for the cases the game uses: put the buffer in a slot, a duplicate of it if dup (one not
   playing from its chain: a sound starts over itself in another buffer). Returns the buffer in the
   slot, 0 if none was free. */
static int place(int v, uint32_t flags, bool dup)
{
    if (!V(v)) return 0;
    if (flags & F_MUSIC) {
        if (!(flags & F_STREAM) && dup) {
            /* 0x406930, a stream buffer's duplicate: not used with the lists the CD has */
            return 0;
        }
        return stream_add(v) ? v : 0;
    }
    if (flags & F_STREAM) {
        if (sfx_add(v)) return v;
        if (dup) voice_release(v);
        return 0;
    }
    int b = v;
    if (dup) {
        flags |= F_ONCE;
        sound_tick();
        for (int last = b; V(b)->ds_playing;) {
            last = b;
            b = V(b)->chain;
            if (b == 0) {
                b = voice_dup(last);
                if (!b) return 0;
                V(b)->refs++;
                V(last)->chain = b;
            }
        }
    }
    V(b)->flags = flags;
    return sfx_add(b) ? b : 0;
}

void sound_stop_sfx(int i)
{
    /* 0x419490 */
    if (i < 0 || i >= 0x20) {
        if (i != -1) return;
        for (int k = 0; k < SFX_SLOTS; k++)
            if (sfx_slot[k]) { snd_stop(sfx_slot[k]); sfx_slot[k] = 0; }
    } else if (i < SFX_SLOTS && sfx_slot[i]) {   /* (the original reads up to slot 31) */
        snd_stop(sfx_slot[i]);
        sfx_slot[i] = 0;
    }
}

void sound_drop(int v)
{
    if (!v) return;
    snd_stop(v);
    for (int i = 0; i < SFX_SLOTS; i++)
        if (sfx_slot[i] == v) { sfx_slot[i] = 0; break; }
}

void sound_stream_drop(int v)
{
    if (!v) return;
    snd_stop(v);
    for (int i = 0; i < STREAM_SLOTS; i++)
        if (stream_slot[i] == v) { sound_stop_streams(i); stream_slot[i] = 0; break; }
}

void sound_music_restore(void)
{
    if (g_music) snd_set_vol(g_music, 0.95f);
    if (g_music2) {
        /* (the original only empties the slot, leaving the buffer allocated) */
        sound_stream_drop(g_music2);
        g_music2 = 0;
    }
}

void sound_stop_streams(int i)
{
    /* 0x4193c0 */
    for (int k = 0; k < STREAM_SLOTS; k++) {
        if ((i != -1 && k != i) || !stream_slot[k]) continue;
        int v = stream_slot[k];
        snd_stop(v);
        stream_slot[k] = 0;
        voice_release(v);
    }
}

/* ------------------------------------------------------------------ the lists */

static void entries_free(void)
{
    /* 0x41ef70 */
    for (int i = 0; i < nentries; i++) { free(entries[i].path); entries[i].path = NULL; }
    nentries = 0;
}

static void ids_free(void)
{
    /* 0x41f000 */
    if (!have_ids) return;
    for (int i = 0; i < STREAM_SLOTS; i++)
        if (stream_slot[i]) { int v = stream_slot[i]; stream_slot[i] = 0; voice_release(v); }
    for (int i = 0; i < SFX_SLOTS; i++)
        if (sfx_slot[i]) { int v = sfx_slot[i]; sfx_slot[i] = 0; voice_release(v); }
    for (int i = 0; i < IDS; i++) {
        if (!ids[i]) continue;
        free(ids[i]->path);
        if (ids[i]->buf) voice_release(ids[i]->buf);
        free(ids[i]);
        ids[i] = NULL;
    }
    have_ids = false;
}

void sound_free_all(void)
{
    /* 0x41ef60 */
    entries_free();
    ids_free();
}

/* 0x41ec70: "id name path", the path running to the end of the line (it may hold spaces). */
static void list_line(const char *line)
{
    char id[256] = "", name[256] = "", tok[256] = "", rest[256] = "", path[256] = "";
    if (sscanf(line, "%255s %255s %255s", id, name, tok) < 1) return;
    if (*id) {
        size_t at = strlen(id) + 1 + strlen(name) + 1, n = strlen(line);
        snprintf(rest, sizeof rest, "%s", at < n ? line + at : "");
    }
    snprintf(path, sizeof path, "%s", tok);
    for (size_t i = 0; rest[i]; i++)
        if (rest[i] == tok[0]) {
            snprintf(path, sizeof path, "%s", rest + i);
            path[strcspn(path, "\r\n")] = 0;
            break;
        }
    if (!*name) {
        if (*path || *id) fprintf(stderr, "%s", exe_str(0x44d478));
        return;
    }
    if (!*path || !*id) { fprintf(stderr, "%s", exe_str(0x44d478)); return; }
    if (nentries >= ENTRIES) return;     /* (the original writes on past its 170 entries) */
    entries[nentries].id = atoi(id) & 0xffff;
    entries[nentries].path = strdup(path);
    nentries++;
}

bool sound_list_load(const char *path, bool reset)
{
    /* 0x41ea60 */
    if (reset) {
        ids_free();             /* (the original drops the table without freeing it) */
        entries_free();
    }
    size_t n;
    char *d = (char *)file_load(path, &n);
    if (!d) return false;
    for (size_t at = 0; at < n;) {
        /* 0x41ec40: up to a '\r' or '\n' */
        char line[256];
        size_t k = 0;
        while (at + k < n && d[at + k] != '\r' && d[at + k] != '\n') k++;
        size_t c = k < sizeof line ? k : sizeof line - 1;
        memcpy(line, d + at, c);
        line[c] = 0;
        at += k + 1;
        list_line(line);
    }
    free(d);
    if (reset) have_ids = true;
    /* every entry so far (all the lists since the reset) into the id table */
    for (int i = 0; i < nentries; i++) {
        int id = entries[i].id;
        if (id >= IDS) continue;            /* (the original writes past its table) */
        if (ids[id]) free(ids[id]->path);   /* (the original drops the entry, a buffer with it) */
        else ids[id] = malloc(sizeof *ids[id]);
        ids[id]->path = strdup(entries[i].path);
        ids[id]->buf = 0;
    }
    return true;
}

static IdEntry *id_entry(int id)
{
    id &= 0xffff;
    return have_ids && id < IDS && ids[id] && ids[id]->path ? ids[id] : NULL;
}

static bool exists(const char *path)
{
    /* 0x41f500 */
    char p[300];
    snprintf(p, sizeof p, "%s%s", path, exe_str(0x44ad80));
    return file_exists(p);
}

int sound_preload(int id)
{
    /* 0x41f2e0 with no flags: 0x418bd0 loads it (flags 2) */
    IdEntry *e = id_entry(id);
    if (!e || !exists(e->path)) return 0;
    int v = voice_new();
    if (v && !voice_load(v, e->path, F_SFX | F_LOAD)) { voice_delete(v); v = 0; }
    e->buf = v;
    if (!v) return 0;
    V(v)->refs++;
    return v;
}

/* every id but the music (100-109), then the list goes */
static void preload_all(void)
{
    for (int i = 0; i < IDS; i++)
        if (i < 100 || i > 0x6d) sound_preload(i);
    entries_free();
}

void sound_lists_load(void)
{
    /* 0x401730 */
    if (!g_sound_fx) return;
    sound_list_load(exe_str(0x44aa88), true);
    char p[256];
    snprintf(p, sizeof p, exe_str(0x44aa58), exe_str(exe_u32(0x44a4f0 + 4 * (uint32_t)g_breed)));
    sound_list_load(p, false);
    snprintf(p, sizeof p, exe_str(0x44aa58), exe_str(exe_u32(0x44a500 + 4 * (uint32_t)g_char)));
    sound_list_load(p, false);
    sound_list_load(world_level_path(exe_str(0x44a0a0)), false);
    preload_all();
    g_sound_loaded = 1;
}

void sound_bonus_load(void)
{
    /* part of 0x40ff30 */
    if (!g_sound_fx) return;
    sound_list_load(exe_str(0x44c4e0), true);
    preload_all();
}

/* 0x419010: a buffer of its own for the file, playing. */
static int voice_open(const char *path, float vol, uint32_t flags)
{
    int v = voice_new();
    if (!v) return 0;
    V(v)->vol = vol;
    V(v)->refs++;
    if (!voice_load(v, path, flags | F_LOAD)) { voice_delete(v); return 0; }
    voice_play(v);
    return v;
}

int sound_play(int id, int loop, int stream)
{
    /* 0x41f3a0 */
    IdEntry *e = id_entry(id);
    if (!e) return 0;
    if (e->buf) {
        uint32_t f = (stream ? F_STREAM : 0) | F_SFX | F_LOAD | (loop ? F_LOOP : F_ONCE);
        int v = place(e->buf, f, true);
        if (v) {
            snd_set_vol(v, g_world == 0 ? 1.0f : 0.0f);
            voice_play(v);
            snd_update(v);
        }
        return v;
    }
    if (!exists(e->path)) return 0;
    if (stream) {
        int v = voice_open(e->path, 1.0f, (loop ? F_LOOP : F_ONCE) | F_STREAM | F_SFX);
        if (!v) return 0;
        snd_set_vol(v, 0.0f);
        V(v)->refs++;
        place(v, F_STREAM | F_SFX | F_LOAD, false);
        return v;
    }
    /* 0x418f10 puts it in a slot, and 0x418cd0 then in a second one */
    int v = voice_open(e->path, 1.0f, (loop ? F_LOOP : F_ONCE) | F_SFX);
    if (v) {
        sfx_add(v);
        V(v)->refs++;
        snd_set_vol(v, 0.0f);
        snd_update(v);
        place(v, F_ONCE | F_SFX | F_LOAD, false);
    }
    return v;
}

int sound_play_at(AnimObj *a, int id, int loop, int stream)
{
    /* 0x401e00: within 80 px left / right and 160 px above / below the screen */
    if (a->x < g_lv.cam_x - 0x50 || a->x > g_lv.cam_x + 0x2d0) return 0;
    if (a->y < g_lv.cam_y - 0xa0 || a->y > g_lv.cam_y + 0x280) return 0;
    int v = sound_play(id, loop, stream);
    if (v) {
        sound_follow(v, a);
        if (!(char)loop) {
            snd_set_vol(v, distance_volume(V(v)));
            snd_update(v);
        }
    }
    return v;
}

int sound_stream(int id, int loop, int stream)
{
    /* 0x41f160 */
    IdEntry *e = id_entry(id);
    if (!e) return 0;
    if (e->buf) {
        uint32_t f = (stream ? F_STREAM : 0) | F_MUSIC | F_LOAD | (loop ? F_LOOP : F_ONCE);
        int v = place(e->buf, f, true);
        if (v) { voice_play(v); V(v)->refs++; }
        return v;
    }
    if (!exists(e->path)) return 0;
    /* 0x418fd0 / 0x418f90 */
    uint32_t f = F_MUSIC | (loop ? F_LOOP : F_ONCE) | (stream ? F_STREAM : 0);
    int v = voice_open(e->path, 1.0f, f);
    place(v, (stream ? F_STREAM : F_ONCE) | F_MUSIC | F_LOAD, false);
    return v;
}

int sound_stream_file(const char *path, int loop)
{
    int v = voice_open(path, 1.0f, F_MUSIC | (loop ? F_LOOP : F_ONCE) | F_STREAM);
    snd_set_vol(v, 1.0f);
    place(v, F_STREAM | F_MUSIC | F_LOAD, false);
    return v;
}

/* ------------------------------------------------------------------ the mixer */

static const int16_t *frame_at(Voice *o, uint32_t i)
{
    if (o->pcm) return o->pcm->s + (size_t)i * (size_t)o->channels;
    if (i < o->chunk_at || i >= o->chunk_at + o->chunk_n) {
        uint32_t bpf = (uint32_t)(o->channels * o->bits / 8);
        uint32_t n = o->frames - i < CHUNK_FRAMES ? o->frames - i : CHUNK_FRAMES;
        uint8_t *raw = malloc((size_t)n * bpf);
        int64_t got = raw ? vfs_read_at(o->file, o->data_off + (uint64_t)i * bpf, raw, (size_t)n * bpf) : -1;
        n = got > 0 ? (uint32_t)((uint64_t)got / bpf) : 0;
        if (n) decode(o, raw, n, o->chunk);
        free(raw);
        o->chunk_at = i;
        o->chunk_n = n;
        if (!n) { static const int16_t zero[2]; return zero; }
    }
    return o->chunk + (size_t)(i - o->chunk_at) * (size_t)o->channels;
}

static void mix_voice(Voice *o, float *dst, int n)
{
    if (!o->frames) { o->ds_playing = false; o->event = true; return; }
    float amp = o->ds <= -10000 ? 0.0f : o->ds >= 0 ? 1.0f : powf(10.0f, (float)o->ds / 2000.0f);
    uint64_t step = ((uint64_t)o->rate << 32) / SOUND_RATE;
    for (int k = 0; k < n; k++) {
        uint32_t i = (uint32_t)(o->pos >> 32);
        if (i >= o->frames) {
            o->event = true;
            if (!o->ds_loop || (o->file && !(o->flags & F_LOOP))) { o->ds_playing = false; return; }
            o->pos -= (uint64_t)o->frames << 32;
            i = (uint32_t)(o->pos >> 32);
        }
        const int16_t *a = frame_at(o, i);
        float l = a[0], r = a[o->channels - 1];
        if (i + 1 < o->frames) {
            float t = (float)(uint32_t)o->pos * (1.0f / 4294967296.0f);
            const int16_t *b = frame_at(o, i + 1);
            l += (b[0] - l) * t;
            r += (b[o->channels - 1] - r) * t;
        }
        dst[2 * k] += l * amp * (1.0f / 32768.0f);
        dst[2 * k + 1] += r * amp * (1.0f / 32768.0f);
        o->pos += step;
    }
}

void sound_frame(void)
{
    /* 16 ms of output, the remainder carried: 44100 / 62.5 = 705.6 frames */
    static uint32_t rem;
    uint32_t total = SOUND_RATE * 2 + rem;
    out_n = (int)(total / 125);
    rem = total % 125;
    memset(out, 0, sizeof(float) * (size_t)out_n * SOUND_CHANNELS);
    for (int i = 1; i <= VOICES; i++)
        if (voices[i].used && voices[i].ds_playing) mix_voice(&voices[i], out, out_n);
    for (int i = 0; i < out_n * SOUND_CHANNELS; i++) out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
}

const float *sound_output(int *frames)
{
    *frames = out_n;
    return out;
}
