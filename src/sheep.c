#include "sheep.h"
#include <stdio.h>

int dir16_toward(int x0, int y0, int x1, int y1)
{
    /* 0x4034a0 */
    int dx = x1 - x0, dy = y1 - y0;
    if (dx < 1) {
        if (dy < 1) {
            if (dy != dx && -dy <= -dx) return (dy * 2 == dx || -dx < dy * -2) + 12;
            return (dx * -2 + dy == 0 || dx * -2 < -dy) + 14;
        }
        if (-dy != dx && dy <= -dx) return (dy * -2 != dx && dy * 2 <= -dx) + 10;
        return (dx * -2 - dy != 0 && dy <= dx * -2) + 8;
    }
    if (dy < 1) {
        if (-dy < dx) return (dy * -2 < dx) + 2;
        return -dy < dx * 2;
    }
    if (dy < dx) return (dx <= dy * 2) + 4;
    return (dx * 2 <= dy) + 6;
}

bool actor_walk_to(int32_t *s, int x, int y)
{
    /* 0x41a780: the y step uses the screen-squashed cosine (0x49fdb0) */
    Sheep *a = (Sheep *)s;
    int d = dir16_toward(a->ix, a->iy, x, y) * 16;
    a->dir = d;
    g_anim[a->slot].dir = d / 16;
    a->x = (float)((double)g_sin[d] * (double)a->speed + (double)a->x);
    double ny = (double)a->y - (double)g_cos_y[d] * (double)a->speed;
    a->y = (float)ny;
    int ey = ftol(ny) - y;               /* fst: the unrounded y */
    int ax = ftol((double)a->x) - x;
    return (ey < 0 ? -ey : ey) + (ax < 0 ? -ax : ax) < 4;
}

void actor_set_state(int32_t *s, int st)
{
    /* 0x419f50 (TODO: the per-state effects, with the flock) */
    Sheep *a = (Sheep *)s;
    if (a->state == st) return;
    a->state = st;
    g_anim[a->slot].sub = 0;
    if (st != 0) fprintf(stderr, "actor_set_state %d: effects not ported\n", st);
}
