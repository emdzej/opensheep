#include "surface.h"
#include "xp.h"
#include <stdlib.h>
#include <string.h>

Surface *surface_from_pixels(const uint16_t *src, int w, int h, int xform)
{
    Surface *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->px = malloc((size_t)w * h * sizeof *s->px);
    if (!s->px) { free(s); return NULL; }
    s->w = w;
    s->h = h;
    /* 0x437e50 */
    for (int y = 0; y < h; y++) {
        int sy = xform & XFORM_FLIP ? h - 1 - y : y;
        for (int x = 0; x < w; x++) {
            int sx = xform & XFORM_MIRROR ? w - 1 - x : x;
            s->px[y * w + x] = src[sy * w + sx];
        }
    }
    return s;
}

void surface_free(Surface *s)
{
    if (!s) return;
    free(s->px);
    free(s);
}

bool surface_blt_fast(Framebuffer *dst, int x, int y, const Surface *src, const Rect *r)
{
    Rect all = { 0, 0, src->w, src->h };
    if (!r) r = &all;
    else if (r->left == r->right || r->top == r->bottom || r->left > r->right || r->top > r->bottom)
        return true;                                       /* 0x437aa0 skips these without a blit */
    int w = r->right - r->left, h = r->bottom - r->top;
    if (r->left < 0 || r->top < 0 || r->right > src->w || r->bottom > src->h ||
        x < 0 || y < 0 || x + w > dst->w || y + h > dst->h)
        return false;                                      /* DDERR_INVALIDRECT: nothing drawn */
    for (int j = 0; j < h; j++) {
        const uint16_t *s = src->px + (r->top + j) * src->w + r->left;
        uint16_t *d = dst->pixels + (y + j) * dst->pitch + x;
        if (src->keyed) {
            for (int i = 0; i < w; i++)
                if (s[i] != src->key) d[i] = s[i];
        } else {
            for (int i = 0; i < w; i++) d[i] = s[i];
        }
    }
    return true;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

Surface *surface_load_bmp(const char *path)
{
    /* 0x437b20: the file into a DIB section, then GDI's copy into the 16-bit surface */
    size_t n = 0;
    uint8_t *f = file_load(path, &n);
    if (!f) return NULL;
    Surface *s = NULL;
    if (n < 54 || f[0] != 'B' || f[1] != 'M') goto out;
    uint32_t off = le32(f + 10), hdr = le32(f + 14);
    int w = (int32_t)le32(f + 18), h = (int32_t)le32(f + 22), bpp = f[28] | f[29] << 8;
    bool top_down = h < 0;
    if (top_down) h = -h;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || (bpp != 24 && bpp != 8) || le32(f + 30)) goto out;
    size_t stride = ((size_t)w * (size_t)bpp / 8 + 3) & ~(size_t)3;
    const uint8_t *pal = f + 14 + hdr;
    if ((size_t)off + stride * (size_t)h > n || (bpp == 8 && (size_t)(pal - f) + 1024 > n)) goto out;
    uint16_t *px = malloc((size_t)w * (size_t)h * sizeof *px);
    if (!px) goto out;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = f + off + stride * (size_t)(top_down ? y : h - 1 - y);
        for (int x = 0; x < w; x++) {
            const uint8_t *c = bpp == 24 ? row + 3 * x : pal + 4 * row[x];   /* b, g, r */
            px[y * w + x] = (uint16_t)((c[2] >> 3) << 11 | (c[1] >> 2) << 5 | c[0] >> 3);
        }
    }
    s = surface_from_pixels(px, w, h, XFORM_NONE);
    free(px);
out:
    free(f);
    return s;
}

Surface *surface_load_tga(const char *path)
{
    /* 0x408fb0: the file (0x422b50), decoded to 32 bits (0x422b80), then to rgb565 (0x4232c0). Only the
       run-length truecolour type (10) at 24 and 32 bits is here: every .tgb on the CD is one. */
    size_t n = 0;
    uint8_t *f = file_load(path, &n);
    if (!f) return NULL;
    Surface *s = NULL;
    uint8_t *raw = NULL;
    uint16_t *px = NULL;
    if (n < 18 || f[2] != 10 || (f[16] != 24 && f[16] != 32)) goto out;
    int w = (int16_t)(f[12] | f[13] << 8), h = (int16_t)(f[14] | f[15] << 8), bpp = f[16] / 8;
    if (w <= 0 || h <= 0) goto out;
    size_t total = (size_t)w * (size_t)h * (size_t)bpp;
    raw = calloc(total, 1);
    px = malloc((size_t)w * (size_t)h * sizeof *px);
    if (!raw || !px) goto out;
    /* the packets: (h & 0x7f) + 1 pixels, one repeated when h & 0x80, else as many stored */
    const uint8_t *p = f + 18 + f[0], *end = f + n;
    for (size_t o = 0; o < total && p < end;) {
        int count = (*p & 0x7f) + 1;
        bool rep = *p++ & 0x80;
        for (int i = 0; i < count && o < total; i++, o += (size_t)bpp) {
            /* (the original reads and writes past either buffer on a short or long stream) */
            if (p + bpp > end) { o = total; break; }
            memcpy(raw + o, p, (size_t)bpp);
            if (!rep) p += bpp;
        }
        if (rep) p += bpp;
    }
    /* the rows bottom up whatever the descriptor says; b, g, r stored */
    for (int y = 0; y < h; y++) {
        const uint8_t *row = raw + (size_t)(h - 1 - y) * (size_t)w * (size_t)bpp;
        for (int x = 0; x < w; x++) {
            const uint8_t *c = row + (size_t)x * (size_t)bpp;
            int r = c[2], g = c[1], b = c[0];
            /* the near-blacks lifted off the colour key */
            if (r && r < 8 && g && g < 4 && b && b < 8) r = 8, g = 4, b = 8;
            px[y * w + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
        }
    }
    s = surface_from_pixels(px, w, h, XFORM_NONE);
out:
    free(px);
    free(raw);
    free(f);
    return s;
}
