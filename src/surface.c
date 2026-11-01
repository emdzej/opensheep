#include "surface.h"
#include <stdlib.h>

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
