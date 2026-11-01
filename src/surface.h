/* Off-screen surfaces: the port's stand-in for the original's DirectDraw draw manager (0x436000-0x438200).
   The game runs in a 640x480 16-bit mode and keeps every surface in rgb565, so a surface here is a plain
   rgb565 pixel block. Blits follow IDirectDrawSurface::BltFast: no clipping (a rectangle that doesn't fit
   the destination fails and draws nothing), optional source colour key. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "platform.h"

typedef struct {
    int w, h;
    uint16_t *px;          /* w * h, rows packed */
    bool keyed;            /* blits skip pixels equal to key (DDBLTFAST_SRCCOLORKEY) */
    uint16_t key;
} Surface;

/* Source rectangle, DirectDraw RECT order: right/bottom exclusive. */
typedef struct { int left, top, right, bottom; } Rect;

/* Image transform when filling a surface (0x437e50): none, mirror left-right, flip top-bottom, both. */
enum { XFORM_NONE = 0, XFORM_MIRROR = 1, XFORM_FLIP = 2, XFORM_BOTH = 3 };

/* A w x h surface filled from rgb565 pixels (w per row) through xform (CreateSurface 0x4367a0 and its
   transformed variants 0x4368b0 / 0x4369c0 / 0x436ad0). NULL on allocation failure. */
Surface *surface_from_pixels(const uint16_t *src, int w, int h, int xform);
void surface_free(Surface *s);
/* A .bmp file (24 or 8 bits) as a surface (0x436be0 / 0x437b20); NULL if it is missing or not a bmp. */
Surface *surface_load_bmp(const char *path);

/* BltFast of src (r = NULL: all of it) to (x, y) of the framebuffer (0x437a50 whole, 0x437aa0 with a
   rectangle; an empty or inverted rectangle draws nothing and succeeds). */
bool surface_blt_fast(Framebuffer *dst, int x, int y, const Surface *src, const Rect *r);
