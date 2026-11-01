#include "paths.h"
#include "game.h"
#include "world.h"
#include "xp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_path_x, g_path_y;
static uint8_t *npf;
static size_t npf_size;

void paths_load(void)
{
    paths_free();
    npf = file_load(world_level_path(".npf"), &npf_size);
}

void paths_free(void)
{
    free(npf);
    npf = NULL;
    npf_size = 0;
}

/* 0x411b40 */
static const uint8_t *point(int p, int i)
{
    uint32_t off;
    memcpy(&off, npf + 8 + 4 * p, 4);
    return npf + off + 8 * (size_t)i;
}

static int16_t rd16(const uint8_t *q, int off)
{
    int16_t v;
    memcpy(&v, q + off, 2);
    return v;
}

void path_point(int p, int i, int *x, int *y)
{
    const uint8_t *q = npf ? point(p, i) : NULL;
    *x = q ? rd16(q, 4) : 0;
    *y = q ? rd16(q, 6) : 0;
}

int path_ticks(int p, int i)
{
    const uint8_t *q = npf ? point(p, i) : NULL;
    return q ? rd16(q, 2) : 0;
}

int path_flag(int p, int i)
{
    const uint8_t *q = npf ? point(p, i) : NULL;
    return q ? rd16(q, 0) : 2;
}

void path_start(int p)
{
    const uint8_t *q = npf ? point(p, 0) : NULL;
    if (q && rd16(q, 0) == 0) {
        g_path_x = rd16(q, 4);
        g_path_y = rd16(q, 6);
        return;
    }
    g_path_x = g_path_y = 0;
    fprintf(stderr, "GetPathStartPos: Can't find start %d\n", p);
}

void path_begin(AnimObj *a, int p, int mode, int flags)
{
    a->d[0x24] = 0;
    a->d[0x25] = 0;
    a->d[0x23] = p;
    a->d[0x26] = mode;
    if (flags & 1) {
        path_start(p);
        a->x = g_path_x;
        a->y = g_path_y;
    }
}

void anim_turn16(AnimObj *a, int d16)
{
    unsigned t = (unsigned)(d16 - a->dir + 0x10);
    if (t != 0x10) {
        if (t & 8) { a->dir = (a->dir - 1) & 0xf; return; }
        a->dir++;
    }
    a->dir &= 0xf;
}

int path_follow(AnimObj *a)
{
    const uint8_t *q = point(a->d[0x23], a->d[0x24]);
    int ticks = rd16(q, 2), x0 = rd16(q, 4), y0 = rd16(q, 6);
    int dx = rd16(q, 0xc) - x0, dy = rd16(q, 0xe) - y0;
    int t = a->d[0x25];
    a->x = t * dx / ticks + x0;
    a->y = dy * t / ticks + y0;
    if (t == 0) {
        switch (a->d[0x26]) {
        case 0: case 3:
            a->dir = (dir_of(dx, dy) + 8) >> 4 & 0xf;
            a->d[0x25]++;
            break;
        case 1: case 4:
            a->d[0x25] = 1;
            break;
        case 2: {
            int d = (int)((unsigned)(dir_of(dx, dy) + 8) & 0xff) >> 4;
            if (a->dir == d) a->d[0x25]++;
            else if (a->frame != a->prev_frame) anim_turn16(a, d);
            break;
        }
        }
    } else {
        a->d[0x25] = t + 1;
    }
    if (ticks <= a->d[0x25]) {
        a->d[0x24]++;
        a->d[0x25] = 0;
        if (rd16(q, 0x10) == 2) {
            if (a->d[0x26] != 3 && a->d[0x26] != 4) {
                a->d[0x24] = a->d[0x25] = 0;
                return 1;
            }
            anim_kill(a);
            return 1;
        }
    }
    return 0;
}
