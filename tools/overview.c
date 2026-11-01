/* overview W.L [scale] [ticks] > out.ppm: the whole level after level_start and that many ticks (map,
   objects, flock, shepherd)
   rendered screen by screen and shrunk by scale (default 4). A development aid; reads the data like
   the tests (./cd or OPENSHEEP_DATA). */
#include "exe.h"
#include "level.h"
#include "play.h"
#include "platform.h"
#include "vfs_host.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>

static uint16_t screen[640 * 480];

int main(int argc, char **argv)
{
    int world = 1, level = 1, scale = argc > 2 ? atoi(argv[2]) : 4;
    if (argc > 1) sscanf(argv[1], "%d.%d", &world, &level);
    if (scale < 1) scale = 1;
    if (!vfs_mount_default()) { fprintf(stderr, "no data (./cd or OPENSHEEP_DATA)\n"); return 2; }
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }
    game_new();
    if (!world_select(world, level) || !level_load_resources()) { fprintf(stderr, "cannot load %d.%d\n", world, level); return 1; }
    level_start();
    for (int t = argc > 3 ? atoi(argv[3]) : 0; t > 0; t--) play_tick();
    /* the part the camera can show (cam_max_* are the right and bottom edges) */
    int W = g_lv.cam_max_x, H = g_lv.cam_max_y;
    if (W > g_lv.px_w || g_lv.cam_max_x <= g_lv.cam_min_x) W = g_lv.px_w;
    if (H > g_lv.px_h || g_lv.cam_max_y <= g_lv.cam_min_y) H = g_lv.px_h;
    int ow = W / scale, oh = H / scale;
    uint8_t *img = calloc((size_t)ow * oh, 3);
    Framebuffer fb = { 640, 480, 640, screen };
    g_lv.cam_min_x = g_lv.cam_min_y = 0;
    g_lv.cam_max_x = W;
    g_lv.cam_max_y = H;
    for (int sy = 0; sy < H; sy += 480)
        for (int sx = 0; sx < W; sx += 640) {
            level_set_camera(sx, sy);
            play_draw(&fb);
            for (int y = 0; y < 480; y += scale)
                for (int x = 0; x < 640; x += scale) {
                    int wx = g_lv.cam_x + x, wy = g_lv.cam_y + y;
                    if (wx >= W || wy >= H || wx / scale >= ow || wy / scale >= oh) continue;
                    uint16_t p = screen[y * 640 + x];
                    uint8_t *o = img + 3 * ((size_t)(wy / scale) * ow + wx / scale);
                    o[0] = (uint8_t)((p >> 11) << 3);
                    o[1] = (uint8_t)((p >> 5 & 63) << 2);
                    o[2] = (uint8_t)((p & 31) << 3);
                }
        }
    printf("P6\n%d %d\n255\n", ow, oh);
    fwrite(img, 3, (size_t)ow * oh, stdout);
    fprintf(stderr, "%dx%d px, scale %d\n", W, H, scale);
    return 0;
}
