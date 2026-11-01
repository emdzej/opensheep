/* Bink 1 'h' / 'i' (bink.h). The structure and names follow FFmpeg's bink.c / binkaudio.c /
   binkdsp.c / bink_demux.c (LGPL 2.1+), the format's reference; the binkb paths ('b') and the 'k'
   additions are left out, no disc movie uses them. */
#include "bink.h"
#include "vfs.h"
#include "xp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the codec's tables, from binkw32.dll (file offsets of the disc's copy) ---- */

static struct {
    bool tried, ok;
    uint8_t scan[64];
    uint8_t patterns[16][64];
    uint8_t tree_lens[16][16], tree_bits[16][16];
    int32_t intra_q[16][64], inter_q[16][64];   /* by scan index, as FFmpeg's bink_*_quant */
    uint32_t crit[24];                          /* the WMA critical frequencies, 100 .. 15500 Hz */
    uint8_t rle[16];
    uint8_t vlc_bits[16];
    struct { uint8_t sym, len; } vlc[16][128];
} T;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool perm64(const uint8_t *p)
{
    uint64_t seen = 0;
    for (int i = 0; i < 64; i++) {
        if (p[i] > 63) return false;
        seen |= 1ull << p[i];
    }
    return seen == ~0ull;
}

static bool tables_load(void)
{
    if (T.tried) return T.ok;
    T.tried = true;
    size_t n;
    uint8_t *d = file_load("binkw32.dll", &n);
    if (!d) return false;
    bool ok = n >= 0x39874;
    if (ok) {
        memcpy(T.scan, d + 0x3993c, 64);
        memcpy(T.patterns, d + 0x327f8, sizeof T.patterns);
        memcpy(T.tree_lens, d + 0x336c8, sizeof T.tree_lens);
        memcpy(T.tree_bits, d + 0x337d0, sizeof T.tree_bits);
        memcpy(T.rle, d + 0x39864, 16);
        for (int i = 0; i < 24; i++) T.crit[i] = rd32(d + 0x396d8 + 4 * i);
        ok = perm64(T.scan);
        for (int i = 0; i < 16 && ok; i++) ok = perm64(T.patterns[i]);
        /* the dll keeps the quantisers in natural order */
        for (int q = 0; q < 16 && ok; q++)
            for (int i = 0; i < 64; i++) {
                T.intra_q[q][i] = (int32_t)rd32(d + 0x2e7ec + 256 * q + 4 * T.scan[i]);
                T.inter_q[q][i] = (int32_t)rd32(d + 0x307ec + 256 * q + 4 * T.scan[i]);
            }
        for (int i = 1; i < 24 && ok; i++) ok = T.crit[i] > T.crit[i - 1];
        for (int i = 0; i < 16 && ok; i++) ok = T.rle[i] > 0;
        /* the 16 trees: complete prefix codes, read lsb first, at most 7 bits */
        for (int t = 0; t < 16 && ok; t++) {
            int max = T.tree_lens[t][15];
            ok = max >= 1 && max <= 7;
            uint32_t kraft = 0;
            for (int s = 0; s < 16 && ok; s++) {
                int l = T.tree_lens[t][s], c = T.tree_bits[t][s];
                ok = l >= 1 && l <= max && c < 1 << l;
                if (!ok) break;
                kraft += 128u >> l;
                for (int e = 0; e < 1 << (max - l); e++) {
                    T.vlc[t][c | e << l].sym = (uint8_t)s;
                    T.vlc[t][c | e << l].len = (uint8_t)l;
                }
            }
            ok = ok && kraft == 128;
            T.vlc_bits[t] = (uint8_t)max;
        }
    }
    free(d);
    T.ok = ok;
    return ok;
}

/* ---- bits, least significant first ---- */

typedef struct {
    const uint8_t *buf;
    size_t cap;            /* bytes readable */
    size_t pos, end;       /* bits */
} Bits;

static void bits_init(Bits *g, const uint8_t *p, size_t bytes)
{
    g->buf = p;
    g->cap = bytes;
    g->pos = 0;
    g->end = bytes * 8;
}

static uint32_t peek(const Bits *g, int n)
{
    size_t at = g->pos >> 3;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        if (at + (size_t)i < g->cap) v |= (uint64_t)g->buf[at + (size_t)i] << (8 * i);
    return (uint32_t)((v >> (g->pos & 7)) & ((1ull << n) - 1));
}

static uint32_t get(Bits *g, int n)
{
    if (n == 0) return 0;
    uint32_t v = peek(g, n);
    g->pos += (size_t)n;
    return v;
}

static int get1(Bits *g) { return (int)get(g, 1); }
static long left(const Bits *g) { return (long)g->end - (long)g->pos; }
static void align32(Bits *g) { g->pos = (g->pos + 31) & ~(size_t)31; }

/* ---- video ---- */

enum { SRC_BLOCK_TYPES, SRC_SUB_BLOCK_TYPES, SRC_COLORS, SRC_PATTERN, SRC_X_OFF, SRC_Y_OFF, SRC_INTRA_DC,
       SRC_INTER_DC, SRC_RUN, NB_SRC };
enum { SKIP_BLOCK, SCALED_BLOCK, MOTION_BLOCK, RUN_BLOCK, RESIDUE_BLOCK, INTRA_BLOCK, FILL_BLOCK, INTER_BLOCK,
       PATTERN_BLOCK, RAW_BLOCK };

typedef struct {
    int vlc;
    uint8_t syms[16];
} Tree;

typedef struct {
    int len;
    Tree tree;
    uint8_t *data, *data_end, *dec, *ptr;   /* dec NULL: the bundle is done for this plane */
} Bundle;

typedef struct {
    uint8_t *p[3];
    int stride[3], rows[3];
} Picture;

/* ---- audio ---- */

enum { RING = 22050 * 4 };   /* stereo frames queued */

struct Bink {
    VfsFile *f;
    int version;                       /* 'h' or 'i' */
    int w, h, nframes, fps_num, fps_den;
    int cur;
    uint32_t *index;                   /* nframes + 1 offsets */
    uint8_t *pkt;
    size_t pkt_cap;

    Picture pic[2];
    int last;                          /* pic[last]: the shown picture */
    Bundle bundle[NB_SRC];
    uint8_t *bundle_mem;
    Tree col_high[16];
    int col_lastval;

    int tracks;
    int rate, channels;                /* 0: no audio */
    int frame_len, overlap_len, block_size, num_bands;
    float root;
    unsigned bands[26];
    float quant_table[96];
    float previous[256];
    bool first;
    float *coeffs, *out, *re, *im, *tw_re, *tw_im;
    float *ring;
    int ring_r, ring_n;
};

static int huff(Bits *g, const Tree *t)
{
    uint32_t v = peek(g, T.vlc_bits[t->vlc]);
    g->pos += T.vlc[t->vlc][v].len;
    return t->syms[T.vlc[t->vlc][v].sym];
}

static int log2i(unsigned v)
{
    int n = 0;
    while (v >>= 1) n++;
    return n;
}

static void init_lengths(Bink *c, int width, int bw)
{
    width = (width + 7) & ~7;
    c->bundle[SRC_BLOCK_TYPES].len = log2i((unsigned)(width >> 3) + 511) + 1;
    c->bundle[SRC_SUB_BLOCK_TYPES].len = log2i((unsigned)(width >> 4) + 511) + 1;
    c->bundle[SRC_COLORS].len = log2i((unsigned)bw * 64 + 511) + 1;
    c->bundle[SRC_INTRA_DC].len = c->bundle[SRC_INTER_DC].len = c->bundle[SRC_X_OFF].len =
        c->bundle[SRC_Y_OFF].len = log2i((unsigned)(width >> 3) + 511) + 1;
    c->bundle[SRC_PATTERN].len = log2i((unsigned)(bw << 3) + 511) + 1;
    c->bundle[SRC_RUN].len = log2i((unsigned)bw * 48 + 511) + 1;
}

static void merge(Bits *g, uint8_t *dst, const uint8_t *src, int size)
{
    const uint8_t *src2 = src + size;
    int size2 = size;
    do {
        if (!get1(g)) {
            *dst++ = *src++;
            size--;
        } else {
            *dst++ = *src2++;
            size2--;
        }
    } while (size && size2);
    while (size--) *dst++ = *src++;
    while (size2--) *dst++ = *src2++;
}

static int read_tree(Bits *g, Tree *tree)
{
    uint8_t tmp1[16] = { 0 }, tmp2[16], *in = tmp1, *out = tmp2;
    if (left(g) < 4) return -1;
    tree->vlc = (int)get(g, 4);
    if (!tree->vlc) {
        for (int i = 0; i < 16; i++) tree->syms[i] = (uint8_t)i;
        return 0;
    }
    if (get1(g)) {
        int len = (int)get(g, 3);
        for (int i = 0; i <= len; i++) {
            tree->syms[i] = (uint8_t)get(g, 4);
            tmp1[tree->syms[i]] = 1;
        }
        for (int i = 0; i < 16 && len < 16 - 1; i++)
            if (!tmp1[i]) tree->syms[++len] = (uint8_t)i;
    } else {
        int len = (int)get(g, 2);
        for (int i = 0; i < 16; i++) in[i] = (uint8_t)i;
        for (int i = 0; i <= len; i++) {
            int size = 1 << i;
            for (int t = 0; t < 16; t += size << 1) merge(g, out + t, in + t, size);
            uint8_t *s = in;
            in = out;
            out = s;
        }
        memcpy(tree->syms, in, 16);
    }
    return 0;
}

static int read_bundle(Bits *g, Bink *c, int n)
{
    if (n == SRC_COLORS) {
        for (int i = 0; i < 16; i++)
            if (read_tree(g, &c->col_high[i]) < 0) return -1;
        c->col_lastval = 0;
    }
    if (n != SRC_INTRA_DC && n != SRC_INTER_DC && read_tree(g, &c->bundle[n].tree) < 0) return -1;
    c->bundle[n].dec = c->bundle[n].ptr = c->bundle[n].data;
    return 0;
}

/* The common start of the bundle readers: 0 when nothing is to be read now (the last batch isn't
   used up, or the bundle is done), else the count. */
static int batch(Bits *g, Bundle *b)
{
    if (!b->dec || b->dec > b->ptr) return 0;
    int t = (int)get(g, b->len);
    if (!t) b->dec = NULL;
    return t;
}

static int read_runs(Bits *g, Bundle *b)
{
    int t = batch(g, b);
    if (!t) return 0;
    if (b->data_end - b->dec < t || left(g) < 1) return -1;
    uint8_t *end = b->dec + t;
    if (get1(g)) {
        memset(b->dec, (int)get(g, 4), (size_t)t);
        b->dec = end;
    } else {
        while (b->dec < end) *b->dec++ = (uint8_t)huff(g, &b->tree);
    }
    return 0;
}

static int read_motion_values(Bits *g, Bundle *b)
{
    int t = batch(g, b);
    if (!t) return 0;
    if (b->data_end - b->dec < t || left(g) < 1) return -1;
    uint8_t *end = b->dec + t;
    if (get1(g)) {
        int v = (int)get(g, 4);
        if (v) {
            int sign = -get1(g);
            v = (v ^ sign) - sign;
        }
        memset(b->dec, v, (size_t)t);
        b->dec = end;
    } else {
        while (b->dec < end) {
            int v = huff(g, &b->tree);
            if (v) {
                int sign = -get1(g);
                v = (v ^ sign) - sign;
            }
            *b->dec++ = (uint8_t)v;
        }
    }
    return 0;
}

static int read_block_types(Bits *g, Bundle *b)
{
    static const uint8_t rlelens[4] = { 4, 8, 12, 32 };
    int t = batch(g, b), last = 0;
    if (!t) return 0;
    if (b->data_end - b->dec < t || left(g) < 1) return -1;
    uint8_t *end = b->dec + t;
    if (get1(g)) {
        memset(b->dec, (int)get(g, 4), (size_t)t);
        b->dec = end;
    } else {
        while (b->dec < end) {
            int v = huff(g, &b->tree);
            if (v < 12) {
                last = v;
                *b->dec++ = (uint8_t)v;
            } else {
                int run = rlelens[v - 12];
                if (end - b->dec < run) return -1;
                memset(b->dec, last, (size_t)run);
                b->dec += run;
            }
        }
    }
    return 0;
}

static int read_patterns(Bits *g, Bundle *b)
{
    int t = batch(g, b);
    if (!t) return 0;
    if (b->data_end - b->dec < t) return -1;
    uint8_t *end = b->dec + t;
    while (b->dec < end) {
        if (left(g) < 2) return -1;
        int v = huff(g, &b->tree);
        v |= huff(g, &b->tree) << 4;
        *b->dec++ = (uint8_t)v;
    }
    return 0;
}

static int color(Bits *g, Bink *c, Bundle *b)
{
    c->col_lastval = huff(g, &c->col_high[c->col_lastval]);
    int v = c->col_lastval << 4 | huff(g, &b->tree);
    if (c->version < 'i') {
        int sign = (int8_t)v >> 7;
        v = ((v & 0x7f) ^ sign) - sign;
        v += 0x80;
    }
    return v;
}

static int read_colors(Bits *g, Bundle *b, Bink *c)
{
    int t = batch(g, b);
    if (!t) return 0;
    if (b->data_end - b->dec < t || left(g) < 1) return -1;
    uint8_t *end = b->dec + t;
    if (get1(g)) {
        memset(b->dec, color(g, c, b), (size_t)t);
        b->dec = end;
    } else {
        while (b->dec < end) {
            if (left(g) < 2) return -1;
            *b->dec++ = (uint8_t)color(g, c, b);
        }
    }
    return 0;
}

static void put16(uint8_t *p, int v)
{
    int16_t s = (int16_t)v;
    memcpy(p, &s, 2);
}

static int read_dcs(Bits *g, Bundle *b, int start_bits, int has_sign)
{
    int len = batch(g, b);
    if (!len) return 0;
    uint8_t *dst = b->dec;
    if (left(g) < start_bits - has_sign) return -1;
    int v = (int)get(g, start_bits - has_sign);
    if (v && has_sign) {
        int sign = -get1(g);
        v = (v ^ sign) - sign;
    }
    if (b->data_end - dst < 2) return -1;
    put16(dst, v);
    dst += 2;
    len--;
    for (int i = 0; i < len; i += 8) {
        int len2 = len - i < 8 ? len - i : 8;
        if (b->data_end - dst < 2 * len2) return -1;
        int bsize = (int)get(g, 4);
        if (bsize) {
            for (int j = 0; j < len2; j++) {
                int v2 = (int)get(g, bsize);
                if (v2) {
                    int sign = -get1(g);
                    v2 = (v2 ^ sign) - sign;
                }
                v += v2;
                put16(dst, v);
                dst += 2;
                if (v < -32768 || v > 32767) return -1;
            }
        } else {
            for (int j = 0; j < len2; j++, dst += 2) put16(dst, v);
        }
    }
    b->dec = dst;
    return 0;
}

static int value(Bink *c, int n)
{
    Bundle *b = &c->bundle[n];
    /* a damaged stream could read past the bundle; the buffer's end holds it */
    if (b->ptr + 2 > b->data_end) return 0;
    if (n < SRC_X_OFF || n == SRC_RUN) return *b->ptr++;
    if (n == SRC_X_OFF || n == SRC_Y_OFF) return (int8_t)*b->ptr++;
    int16_t s;
    memcpy(&s, b->ptr, 2);
    b->ptr += 2;
    return s;
}

static int read_dct_coeffs(Bits *g, int32_t block[64], int *coef_count_, int coef_idx[64])
{
    int coef_list[128], mode_list[128];
    int list_start = 64, list_end = 64, coef_count = 0;
    const uint8_t *scan = T.scan;
    if (left(g) < 4) return -1;
    coef_list[list_end] = 4;  mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] = 1;  mode_list[list_end++] = 3;
    coef_list[list_end] = 2;  mode_list[list_end++] = 3;
    coef_list[list_end] = 3;  mode_list[list_end++] = 3;
    for (int bits = (int)get(g, 4) - 1; bits >= 0; bits--) {
        int list_pos = list_start;
        while (list_pos < list_end) {
            if (!(mode_list[list_pos] | coef_list[list_pos]) || !get1(g)) {
                list_pos++;
                continue;
            }
            int ccoef = coef_list[list_pos], mode = mode_list[list_pos], t;
            switch (mode) {
            case 0:
            case 2:
                if (mode == 0) {
                    coef_list[list_pos] = ccoef + 4;
                    mode_list[list_pos] = 1;
                } else {
                    coef_list[list_pos] = 0;
                    mode_list[list_pos++] = 0;
                }
                for (int i = 0; i < 4; i++, ccoef++) {
                    if (get1(g)) {
                        /* a damaged stream could push the list below its start */
                        if (list_start == 0) return -1;
                        coef_list[--list_start] = ccoef;
                        mode_list[list_start] = 3;
                    } else {
                        if (!bits) {
                            t = 1 - (get1(g) << 1);
                        } else {
                            t = (int)get(g, bits) | 1 << bits;
                            int sign = -get1(g);
                            t = (t ^ sign) - sign;
                        }
                        if (ccoef > 63 || coef_count > 63) return -1;
                        block[scan[ccoef]] = t;
                        coef_idx[coef_count++] = ccoef;
                    }
                }
                break;
            case 1:
                mode_list[list_pos] = 2;
                for (int i = 0; i < 3; i++) {
                    if (list_end >= 128) return -1;
                    ccoef += 4;
                    coef_list[list_end] = ccoef;
                    mode_list[list_end++] = 2;
                }
                break;
            case 3:
                if (!bits) {
                    t = 1 - (get1(g) << 1);
                } else {
                    t = (int)get(g, bits) | 1 << bits;
                    int sign = -get1(g);
                    t = (t ^ sign) - sign;
                }
                if (ccoef > 63 || coef_count > 63) return -1;
                block[scan[ccoef]] = t;
                coef_idx[coef_count++] = ccoef;
                coef_list[list_pos] = 0;
                mode_list[list_pos++] = 0;
                break;
            }
        }
    }
    *coef_count_ = coef_count;
    return (int)get(g, 4);
}

static void unquantize(int32_t block[64], const int32_t quant[64], int coef_count, const int coef_idx[64])
{
    /* (int)(a * q) >> 11: the product wraps in 32 bits, as the reference's unsigned multiply */
    block[0] = (int32_t)((uint32_t)block[0] * (uint32_t)quant[0]) >> 11;
    for (int i = 0; i < coef_count; i++) {
        int idx = coef_idx[i];
        block[T.scan[idx]] = (int32_t)((uint32_t)block[T.scan[idx]] * (uint32_t)quant[idx]) >> 11;
    }
}

static int read_residue(Bits *g, int16_t block[64], int masks_count)
{
    int coef_list[128], mode_list[128], nz_coeff[64];
    int list_start = 64, list_end = 64, nz_coeff_count = 0;
    coef_list[list_end] = 4;  mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] = 0;  mode_list[list_end++] = 2;
    for (int mask = 1 << get(g, 3); mask; mask >>= 1) {
        for (int i = 0; i < nz_coeff_count; i++) {
            if (!get1(g)) continue;
            if (block[nz_coeff[i]] < 0)
                block[nz_coeff[i]] = (int16_t)(block[nz_coeff[i]] - mask);
            else
                block[nz_coeff[i]] = (int16_t)(block[nz_coeff[i]] + mask);
            if (--masks_count < 0) return 0;
        }
        int list_pos = list_start;
        while (list_pos < list_end) {
            if (!(coef_list[list_pos] | mode_list[list_pos]) || !get1(g)) {
                list_pos++;
                continue;
            }
            int ccoef = coef_list[list_pos], mode = mode_list[list_pos];
            switch (mode) {
            case 0:
            case 2:
                if (mode == 0) {
                    coef_list[list_pos] = ccoef + 4;
                    mode_list[list_pos] = 1;
                } else {
                    coef_list[list_pos] = 0;
                    mode_list[list_pos++] = 0;
                }
                for (int i = 0; i < 4; i++, ccoef++) {
                    if (get1(g)) {
                        if (list_start == 0) return -1;
                        coef_list[--list_start] = ccoef;
                        mode_list[list_start] = 3;
                    } else {
                        if (ccoef > 63 || nz_coeff_count > 63) return -1;
                        nz_coeff[nz_coeff_count++] = T.scan[ccoef];
                        int sign = -get1(g);
                        block[T.scan[ccoef]] = (int16_t)((mask ^ sign) - sign);
                        if (--masks_count < 0) return 0;
                    }
                }
                break;
            case 1:
                mode_list[list_pos] = 2;
                for (int i = 0; i < 3; i++) {
                    if (list_end >= 128) return -1;
                    ccoef += 4;
                    coef_list[list_end] = ccoef;
                    mode_list[list_end++] = 2;
                }
                break;
            case 3: {
                if (ccoef > 63 || nz_coeff_count > 63) return -1;
                nz_coeff[nz_coeff_count++] = T.scan[ccoef];
                int sign = -get1(g);
                block[T.scan[ccoef]] = (int16_t)((mask ^ sign) - sign);
                coef_list[list_pos] = 0;
                mode_list[list_pos++] = 0;
                if (--masks_count < 0) return 0;
                break;
            }
            }
        }
    }
    return 0;
}

/* binkdsp.c's integer IDCT */
#define A1 2896
#define A2 2217
#define A3 3784
#define A4 (-5352)
#define MUL(x, y) ((int)((unsigned)(x) * (unsigned)(y)) >> 11)

#define IDCT(dest, s, d, munge, src) do { \
    const int a0 = (src)[s * 0] + (src)[s * 4]; \
    const int a1 = (src)[s * 0] - (src)[s * 4]; \
    const int a2 = (src)[s * 2] + (src)[s * 6]; \
    const int a3 = MUL(A1, (src)[s * 2] - (src)[s * 6]); \
    const int a4 = (src)[s * 5] + (src)[s * 3]; \
    const int a5 = (src)[s * 5] - (src)[s * 3]; \
    const int a6 = (src)[s * 1] + (src)[s * 7]; \
    const int a7 = (src)[s * 1] - (src)[s * 7]; \
    const int b0 = a4 + a6; \
    const int b1 = MUL(A3, a5 + a7); \
    const int b2 = MUL(A4, a5) - b0 + b1; \
    const int b3 = MUL(A1, a6 - a4) - b2; \
    const int b4 = MUL(A2, a7) + b3 - b1; \
    (dest)[d * 0] = munge(a0 + a2 + b0); \
    (dest)[d * 1] = munge(a1 + a3 - a2 + b2); \
    (dest)[d * 2] = munge(a1 - a3 + a2 + b3); \
    (dest)[d * 3] = munge(a0 - a2 - b4); \
    (dest)[d * 4] = munge(a0 - a2 + b4); \
    (dest)[d * 5] = munge(a1 - a3 + a2 - b3); \
    (dest)[d * 6] = munge(a1 + a3 - a2 - b2); \
    (dest)[d * 7] = munge(a0 + a2 - b0); \
} while (0)

#define MUNGE_NONE(x) (x)
#define MUNGE_ROW(x) (((x) + 0x7f) >> 8)
/* the reference stores the row results in bytes as they are (no clamp) */
#define MUNGE_PUT(x) ((uint8_t)(((x) + 0x7f) >> 8))

static void idct_cols(int *temp, const int32_t *block)
{
    for (int i = 0; i < 8; i++) {
        const int32_t *src = block + i;
        int *dest = temp + i;
        if ((src[8] | src[16] | src[24] | src[32] | src[40] | src[48] | src[56]) == 0) {
            for (int k = 0; k < 8; k++) dest[8 * k] = src[0];
        } else {
            IDCT(dest, 8, 8, MUNGE_NONE, src);
        }
    }
}

static void idct_put(uint8_t *dest, int stride, const int32_t *block)
{
    int temp[64];
    idct_cols(temp, block);
    for (int i = 0; i < 8; i++) IDCT(dest + i * stride, 1, 1, MUNGE_PUT, temp + 8 * i);
}

static void idct_add(uint8_t *dest, int stride, int32_t *block)
{
    int temp[64];
    idct_cols(temp, block);
    for (int i = 0; i < 8; i++) IDCT(block + 8 * i, 1, 1, MUNGE_ROW, temp + 8 * i);
    for (int i = 0; i < 8; i++, dest += stride)
        for (int j = 0; j < 8; j++) dest[j] = (uint8_t)(dest[j] + block[8 * i + j]);
}

static void scale_block(const uint8_t src[64], uint8_t *dst, int stride)
{
    for (int j = 0; j < 8; j++, dst += 2 * stride)
        for (int i = 0; i < 8; i++)
            dst[2 * i] = dst[2 * i + 1] = dst[stride + 2 * i] = dst[stride + 2 * i + 1] = src[8 * j + i];
}

static void put8(uint8_t *dst, const uint8_t *src, int stride)
{
    /* the motion reference may overlap the block only across frames (prev is the other picture) */
    for (int i = 0; i < 8; i++) memcpy(dst + i * stride, src + i * stride, 8);
}

static void fill(uint8_t *dst, int v, int stride, int n)
{
    for (int i = 0; i < n; i++) memset(dst + i * stride, v, (size_t)n);
}

static int put_pixels(Bink *c, uint8_t *dst, const uint8_t *prev, int stride, const uint8_t *ref_start,
                      const uint8_t *ref_end)
{
    int xoff = value(c, SRC_X_OFF), yoff = value(c, SRC_Y_OFF);
    ptrdiff_t at = (prev - ref_start) + xoff + (ptrdiff_t)yoff * stride;
    if (at < 0 || at > ref_end - ref_start) return -1;
    put8(dst, ref_start + at, stride);
    return 0;
}

static int run_fill(Bits *g, Bink *c, uint8_t *dst, int stride, bool pix)
{
    /* RUN_BLOCK: runs of colours along one of the 16 patterns; pix: into a 8x8 of the picture,
       else into the 16x16 block's source */
    if (left(g) < 4) return -1;
    const uint8_t *scan = T.patterns[get(g, 4)];
    int i = 0;
    do {
        int run = value(c, SRC_RUN) + 1;
        i += run;
        if (i > 64) return -1;
        if (get1(g)) {
            int v = value(c, SRC_COLORS);
            for (int j = 0; j < run; j++, scan++) dst[pix ? (*scan & 7) + (*scan >> 3) * stride : *scan] = (uint8_t)v;
        } else {
            for (int j = 0; j < run; j++, scan++)
                dst[pix ? (*scan & 7) + (*scan >> 3) * stride : *scan] = (uint8_t)value(c, SRC_COLORS);
        }
    } while (i < 63);
    if (i == 63) dst[pix ? (*scan & 7) + (*scan >> 3) * stride : *scan] = (uint8_t)value(c, SRC_COLORS);
    return 0;
}

static int decode_plane(Bink *c, Bits *g, int plane, bool chroma)
{
    Picture *cur = &c->pic[c->last ^ 1], *old = &c->pic[c->last];
    int stride = cur->stride[plane];
    int bw = chroma ? (c->w + 15) >> 4 : (c->w + 7) >> 3;
    int bh = chroma ? (c->h + 15) >> 4 : (c->h + 7) >> 3;
    int width = c->w >> chroma;
    int32_t dct[64];
    int16_t block[64];
    uint8_t ublock[64];
    int coef_count, coef_idx[64];

    init_lengths(c, width > 8 ? width : 8, bw);
    for (int i = 0; i < NB_SRC; i++)
        if (read_bundle(g, c, i) < 0) return -1;
    const uint8_t *ref_start = old->p[plane];
    const uint8_t *ref_end = ref_start + (bw - 1 + stride * (bh - 1)) * 8;

    for (int by = 0; by < bh; by++) {
        if (read_block_types(g, &c->bundle[SRC_BLOCK_TYPES]) < 0 ||
            read_block_types(g, &c->bundle[SRC_SUB_BLOCK_TYPES]) < 0 ||
            read_colors(g, &c->bundle[SRC_COLORS], c) < 0 || read_patterns(g, &c->bundle[SRC_PATTERN]) < 0 ||
            read_motion_values(g, &c->bundle[SRC_X_OFF]) < 0 || read_motion_values(g, &c->bundle[SRC_Y_OFF]) < 0 ||
            read_dcs(g, &c->bundle[SRC_INTRA_DC], 11, 0) < 0 || read_dcs(g, &c->bundle[SRC_INTER_DC], 11, 1) < 0 ||
            read_runs(g, &c->bundle[SRC_RUN]) < 0)
            return -1;
        uint8_t *dst = cur->p[plane] + 8 * by * stride;
        const uint8_t *prev = old->p[plane] + 8 * by * stride;
        for (int bx = 0; bx < bw; bx++, dst += 8, prev += 8) {
            int blk = value(c, SRC_BLOCK_TYPES), q;
            /* a 16x16 block's other quarters */
            if (((by & 1) || (bx & 1)) && blk == SCALED_BLOCK) {
                bx++;
                dst += 8;
                prev += 8;
                continue;
            }
            /* a 16x16 block on the last column or row would write past the picture */
            if (blk == SCALED_BLOCK && (bx + 1 >= bw || by + 1 >= bh)) return -1;
            switch (blk) {
            case SKIP_BLOCK:
                put8(dst, prev, stride);
                break;
            case SCALED_BLOCK:
                blk = value(c, SRC_SUB_BLOCK_TYPES);
                switch (blk) {
                case RUN_BLOCK:
                    if (run_fill(g, c, ublock, stride, false) < 0) return -1;
                    break;
                case INTRA_BLOCK:
                    memset(dct, 0, sizeof dct);
                    dct[0] = value(c, SRC_INTRA_DC);
                    if ((q = read_dct_coeffs(g, dct, &coef_count, coef_idx)) < 0) return -1;
                    unquantize(dct, T.intra_q[q], coef_count, coef_idx);
                    idct_put(ublock, 8, dct);
                    break;
                case FILL_BLOCK:
                    fill(dst, value(c, SRC_COLORS), stride, 16);
                    break;
                case PATTERN_BLOCK: {
                    int col[2];
                    for (int i = 0; i < 2; i++) col[i] = value(c, SRC_COLORS);
                    for (int j = 0; j < 8; j++) {
                        int v = value(c, SRC_PATTERN);
                        for (int i = 0; i < 8; i++, v >>= 1) ublock[i + j * 8] = (uint8_t)col[v & 1];
                    }
                    break;
                }
                case RAW_BLOCK:
                    for (int i = 0; i < 64; i++) ublock[i] = (uint8_t)value(c, SRC_COLORS);
                    break;
                default:
                    return -1;
                }
                if (blk != FILL_BLOCK) scale_block(ublock, dst, stride);
                bx++;
                dst += 8;
                prev += 8;
                break;
            case MOTION_BLOCK:
                if (put_pixels(c, dst, prev, stride, ref_start, ref_end) < 0) return -1;
                break;
            case RUN_BLOCK:
                if (run_fill(g, c, dst, stride, true) < 0) return -1;
                break;
            case RESIDUE_BLOCK:
                if (put_pixels(c, dst, prev, stride, ref_start, ref_end) < 0) return -1;
                memset(block, 0, sizeof block);
                if (read_residue(g, block, (int)get(g, 7)) < 0) return -1;
                for (int i = 0; i < 8; i++)
                    for (int j = 0; j < 8; j++) dst[i * stride + j] = (uint8_t)(dst[i * stride + j] + block[8 * i + j]);
                break;
            case INTRA_BLOCK:
                memset(dct, 0, sizeof dct);
                dct[0] = value(c, SRC_INTRA_DC);
                if ((q = read_dct_coeffs(g, dct, &coef_count, coef_idx)) < 0) return -1;
                unquantize(dct, T.intra_q[q], coef_count, coef_idx);
                idct_put(dst, stride, dct);
                break;
            case FILL_BLOCK:
                fill(dst, value(c, SRC_COLORS), stride, 8);
                break;
            case INTER_BLOCK:
                if (put_pixels(c, dst, prev, stride, ref_start, ref_end) < 0) return -1;
                memset(dct, 0, sizeof dct);
                dct[0] = value(c, SRC_INTER_DC);
                if ((q = read_dct_coeffs(g, dct, &coef_count, coef_idx)) < 0) return -1;
                unquantize(dct, T.inter_q[q], coef_count, coef_idx);
                idct_add(dst, stride, dct);
                break;
            case PATTERN_BLOCK: {
                int col[2];
                for (int i = 0; i < 2; i++) col[i] = value(c, SRC_COLORS);
                for (int i = 0; i < 8; i++) {
                    int v = value(c, SRC_PATTERN);
                    for (int j = 0; j < 8; j++, v >>= 1) dst[i * stride + j] = (uint8_t)col[v & 1];
                }
                break;
            }
            case RAW_BLOCK:
                for (int i = 0; i < 8; i++) {
                    Bundle *b = &c->bundle[SRC_COLORS];
                    if (b->data_end - b->ptr < 64) return -1;
                    memcpy(dst + i * stride, b->ptr + i * 8, 8);
                }
                c->bundle[SRC_COLORS].ptr += 64;
                break;
            default:
                return -1;
            }
        }
    }
    align32(g);
    return 0;
}

static bool decode_video(Bink *c, const uint8_t *p, size_t n)
{
    Bits g;
    bits_init(&g, p, n);
    if (c->version >= 'i') g.pos += 32;
    for (int plane = 0; plane < 3; plane++) {
        /* from 'h' the chroma planes come V first */
        int idx = plane == 0 ? 0 : plane ^ 3;
        if (decode_plane(c, &g, idx, plane != 0) < 0) return false;
        if (g.pos >= g.end) break;
    }
    c->last ^= 1;
    return true;
}

/* ---- audio (the RDFT variant: the channels interleaved in one transform) ---- */

static void audio_init(Bink *c)
{
    int frame_len_bits = c->rate < 22050 ? 9 : c->rate < 44100 ? 10 : 11;
    int rate = c->rate * c->channels;
    frame_len_bits += log2i((unsigned)c->channels);
    c->frame_len = 1 << frame_len_bits;
    c->overlap_len = c->frame_len / 16;
    c->block_size = c->frame_len - c->overlap_len;
    int half = (rate + 1) / 2;
    c->root = (float)(2.0 / (sqrt(c->frame_len) * 32768.0));
    for (int i = 0; i < 96; i++) c->quant_table[i] = expf((float)i * 0.15289164787221953823f) * c->root;
    for (c->num_bands = 1; c->num_bands < 25; c->num_bands++)
        if ((unsigned)half <= T.crit[c->num_bands - 1]) break;
    c->bands[0] = 2;
    for (int i = 1; i < c->num_bands; i++) c->bands[i] = (T.crit[i - 1] * (unsigned)c->frame_len / (unsigned)half) & ~1u;
    c->bands[c->num_bands] = (unsigned)c->frame_len;
    c->first = true;
    int n = c->frame_len;
    c->coeffs = calloc((size_t)n + 2, sizeof(float));
    c->out = calloc((size_t)n, sizeof(float));
    c->re = calloc((size_t)n, sizeof(float));
    c->im = calloc((size_t)n, sizeof(float));
    c->tw_re = calloc((size_t)n / 2, sizeof(float));
    c->tw_im = calloc((size_t)n / 2, sizeof(float));
    c->ring = calloc((size_t)RING * 2, sizeof(float));
    for (int k = 0; k < n / 2; k++) {
        c->tw_re[k] = (float)cos(2.0 * 3.14159265358979323846 * k / n);
        c->tw_im[k] = (float)sin(2.0 * 3.14159265358979323846 * k / n);
    }
}

static float get_float(Bits *g)
{
    int power = (int)get(g, 5);
    float f = ldexpf((float)get(g, 23), power - 23);
    if (get1(g)) f = -f;
    return f;
}

/* The inverse real transform: out[n] = 0.5 * sum over the full (conjugate-symmetric) spectrum of
   X[k] e^(2 pi i k n / N), X[k] = coeffs[2k] + i coeffs[2k+1] for k <= N/2. */
static void irdft(Bink *c)
{
    int n = c->frame_len;
    float *re = c->re, *im = c->im;
    for (int k = 0; k <= n / 2; k++) {
        re[k] = c->coeffs[2 * k];
        im[k] = c->coeffs[2 * k + 1];
    }
    for (int k = n / 2 + 1; k < n; k++) {
        re[k] = re[n - k];
        im[k] = -im[n - k];
    }
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        int step = n / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                float wr = c->tw_re[k * step], wi = c->tw_im[k * step];
                float xr = re[i + k + len / 2], xi = im[i + k + len / 2];
                float tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
                re[i + k + len / 2] = re[i + k] - tr;
                im[i + k + len / 2] = im[i + k] - ti;
                re[i + k] += tr;
                im[i + k] += ti;
            }
    }
    for (int i = 0; i < n; i++) c->out[i] = 0.5f * re[i];
}

static int decode_block(Bink *c, Bits *g)
{
    float q, quant[25];
    float *coeffs = c->coeffs;
    if (left(g) < 58) return -1;
    coeffs[0] = get_float(g) * c->root;
    coeffs[1] = get_float(g) * c->root;
    if (left(g) < c->num_bands * 8) return -1;
    for (int i = 0; i < c->num_bands; i++) {
        int v = (int)get(g, 8);
        quant[i] = c->quant_table[v < 95 ? v : 95];
    }
    int k = 0;
    q = quant[0];
    int i = 2;
    while (i < c->frame_len) {
        int j;
        if (get1(g)) {
            j = i + T.rle[get(g, 4)] * 8;
        } else {
            j = i + 8;
        }
        if (j > c->frame_len) j = c->frame_len;
        int width = (int)get(g, 4);
        if (width == 0) {
            memset(coeffs + i, 0, (size_t)(j - i) * sizeof *coeffs);
            i = j;
            while (c->bands[k] < (unsigned)i) q = quant[k++];
        } else {
            while (i < j) {
                if (c->bands[k] == (unsigned)i) q = quant[k++];
                int coeff = (int)get(g, width);
                if (coeff) {
                    coeffs[i] = get1(g) ? -q * (float)coeff : q * (float)coeff;
                } else {
                    coeffs[i] = 0.0f;
                }
                i++;
            }
        }
        if (left(g) < 0) return -1;
    }
    for (int m = 2; m < c->frame_len; m += 2) coeffs[m + 1] *= -1;
    coeffs[c->frame_len] = coeffs[1];
    coeffs[c->frame_len + 1] = coeffs[1] = 0;
    irdft(c);
    int count = c->overlap_len;
    if (!c->first)
        for (int m = 0; m < c->overlap_len; m++)
            c->out[m] = (c->previous[m] * (float)(count - m) + c->out[m] * (float)m) / (float)count;
    memcpy(c->previous, c->out + c->frame_len - c->overlap_len, (size_t)c->overlap_len * sizeof(float));
    c->first = false;
    return 0;
}

static void ring_push(Bink *c, const float *s, int frames)
{
    for (int i = 0; i < frames; i++) {
        if (c->ring_n == RING) return;   /* full: the rest is dropped */
        int w = (c->ring_r + c->ring_n) % RING;
        c->ring[2 * w] = s[c->channels == 2 ? 2 * i : i];
        c->ring[2 * w + 1] = s[c->channels == 2 ? 2 * i + 1 : i];
        c->ring_n++;
    }
}

static void decode_audio(Bink *c, const uint8_t *p, size_t n)
{
    Bits g;
    if (n < 4) return;
    bits_init(&g, p, n);
    g.pos = 32;   /* the decoded size */
    while (left(&g) > 0) {
        if (decode_block(c, &g) < 0) return;
        ring_push(c, c->out, c->block_size / c->channels);
        align32(&g);
    }
}

int bink_audio(Bink *b, float *dst, int n)
{
    int k = n < b->ring_n ? n : b->ring_n;
    for (int i = 0; i < k; i++) {
        dst[2 * i] = b->ring[2 * b->ring_r];
        dst[2 * i + 1] = b->ring[2 * b->ring_r + 1];
        b->ring_r = (b->ring_r + 1) % RING;
    }
    b->ring_n -= k;
    return k;
}

/* ---- the container ---- */

Bink *bink_open(const char *path)
{
    if (!tables_load()) return NULL;
    VfsFile *f = file_open(path);
    if (!f) return NULL;
    uint8_t h[64];
    Bink *c = NULL;
    if (vfs_read_at(f, 0, h, sizeof h) != (int64_t)sizeof h) goto fail;
    if (memcmp(h, "BIK", 3) || (h[3] != 'h' && h[3] != 'i')) goto fail;
    c = calloc(1, sizeof *c);
    if (!c) goto fail;
    c->f = f;
    c->version = h[3];
    c->nframes = (int)rd32(h + 8);
    c->w = (int)rd32(h + 20);
    c->h = (int)rd32(h + 24);
    c->fps_num = (int)rd32(h + 28);
    c->fps_den = (int)rd32(h + 32);
    uint32_t flags = rd32(h + 36), tracks = rd32(h + 40);
    if (c->nframes <= 0 || c->nframes > 100000 || c->w <= 0 || c->w > 2048 || c->h <= 0 || c->h > 2048 ||
        c->fps_num <= 0 || c->fps_den <= 0 || (flags & 0x100000) || tracks > 1)
        goto fail;
    uint32_t at = 44;
    c->tracks = (int)tracks;
    if (tracks) {
        uint16_t rate = (uint16_t)(h[48] | h[49] << 8), aflags = (uint16_t)(h[50] | h[51] << 8);
        /* the DCT variant (0x1000) isn't on the disc */
        if (!(aflags & 0x1000) && rate > 0) {
            c->rate = rate;
            c->channels = aflags & 0x2000 ? 2 : 1;
        }
        at += 12;
    }
    uint64_t size = vfs_file_size(f);
    c->index = malloc(4 * ((size_t)c->nframes + 1));
    if (!c->index || vfs_read_at(f, at, c->index, 4 * (size_t)c->nframes) != 4 * (int64_t)c->nframes) goto fail;
    size_t big = 0;
    for (int i = 0; i < c->nframes; i++) c->index[i] = rd32((uint8_t *)&c->index[i]) & ~1u;
    c->index[c->nframes] = (uint32_t)size;
    for (int i = 0; i < c->nframes; i++) {
        if (c->index[i + 1] <= c->index[i] || c->index[i + 1] > size) goto fail;
        if (c->index[i + 1] - c->index[i] > big) big = c->index[i + 1] - c->index[i];
    }
    c->pkt = malloc(big + 16);
    c->pkt_cap = big;
    if (!c->pkt) goto fail;

    int bw = (c->w + 7) >> 3, bh = (c->h + 7) >> 3, cw = (c->w + 15) >> 4, ch = (c->h + 15) >> 4;
    for (int k = 0; k < 2; k++) {
        c->pic[k].stride[0] = bw * 8;
        c->pic[k].rows[0] = bh * 8;
        c->pic[k].stride[1] = c->pic[k].stride[2] = cw * 8;
        c->pic[k].rows[1] = c->pic[k].rows[2] = ch * 8;
        for (int p = 0; p < 3; p++) {
            c->pic[k].p[p] = calloc((size_t)c->pic[k].stride[p] * (size_t)c->pic[k].rows[p], 1);
            if (!c->pic[k].p[p]) goto fail;
        }
    }
    size_t blocks = (size_t)bw * (size_t)bh;
    c->bundle_mem = calloc(blocks, 64 * NB_SRC);
    if (!c->bundle_mem) goto fail;
    for (int i = 0; i < NB_SRC; i++) {
        c->bundle[i].data = c->bundle_mem + blocks * 64 * (size_t)i;
        c->bundle[i].data_end = c->bundle[i].data + blocks * 64;
    }
    if (c->rate) audio_init(c);
    if (c->rate && !c->ring) goto fail;
    return c;
fail:
    if (c) bink_close(c);
    else vfs_close(f);
    return NULL;
}

void bink_close(Bink *c)
{
    if (!c) return;
    if (c->f) vfs_close(c->f);
    free(c->index);
    free(c->pkt);
    for (int k = 0; k < 2; k++)
        for (int p = 0; p < 3; p++) free(c->pic[k].p[p]);
    free(c->bundle_mem);
    free(c->coeffs);
    free(c->out);
    free(c->re);
    free(c->im);
    free(c->tw_re);
    free(c->tw_im);
    free(c->ring);
    free(c);
}

int bink_frames(const Bink *b) { return b->nframes; }
int bink_fps_num(const Bink *b) { return b->fps_num; }
int bink_fps_den(const Bink *b) { return b->fps_den; }
int bink_audio_rate(const Bink *b) { return b->rate; }

bool bink_decode(Bink *c)
{
    if (c->cur >= c->nframes) return false;
    uint32_t pos = c->index[c->cur], n = c->index[c->cur + 1] - pos;
    c->cur++;
    if (vfs_read_at(c->f, pos, c->pkt, n) != (int64_t)n) return false;
    memset(c->pkt + n, 0, 16);
    const uint8_t *p = c->pkt;
    if (c->tracks) {
        /* one audio track: its size, then its packet */
        uint32_t a = rd32(p);
        if (n < 4 || a > n - 4) return false;
        if (c->rate && a >= 4) decode_audio(c, p + 4, a);
        p += 4 + a;
        n -= 4 + a;
    }
    return decode_video(c, p, n);
}

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

void bink_copy(const Bink *c, uint16_t *dst, int pitch, int w, int h, int x, int y)
{
    /* BT.601, video range */
    const Picture *pic = &c->pic[c->last];
    for (int j = 0; j < c->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= h) continue;
        const uint8_t *py = pic->p[0] + (size_t)j * (size_t)pic->stride[0];
        const uint8_t *pu = pic->p[1] + (size_t)(j >> 1) * (size_t)pic->stride[1];
        const uint8_t *pv = pic->p[2] + (size_t)(j >> 1) * (size_t)pic->stride[2];
        for (int i = 0; i < c->w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= w) continue;
            int C = 298 * (py[i] - 16), D = pu[i >> 1] - 128, E = pv[i >> 1] - 128;
            int r = clamp8((C + 409 * E + 128) >> 8);
            int g = clamp8((C - 100 * D - 208 * E + 128) >> 8);
            int b = clamp8((C + 516 * D + 128) >> 8);
            dst[(size_t)dy * (size_t)pitch + (size_t)dx] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
        }
    }
}
