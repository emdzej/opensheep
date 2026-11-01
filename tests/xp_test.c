/* Mounts every archive and unpacks every file in it (CRC-checked). With out/xp/<archive>/ from
   tools/xp.py present, also compares the bytes. */
#include "vfs_host.h"
#include "xp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *archives[] = { "main.xp", "Sound.xp", "world00.xp", "world01.xp", "world02.xp", "world03.xp",
                                  "world04.xp", "world05.xp", "world06.xp", "world07.xp", "world09.xp" };

int main(void)
{
    if (!vfs_mount_default()) { fprintf(stderr, "no data (./cd or OPENSHEEP_DATA)\n"); return 2; }
    int bad = 0, files = 0, compared = 0;
    for (size_t a = 0; a < sizeof archives / sizeof *archives; a++) {
        xp_unmount_all();
        if (!xp_mount(archives[a])) { fprintf(stderr, "cannot mount %s\n", archives[a]); return 1; }
        char base[64];
        snprintf(base, sizeof base, "%s", archives[a]);
        *strrchr(base, '.') = 0;
        for (int i = 0; xp_entry(0, i); i++, files++) {
            const char *p = xp_entry(0, i);
            size_t n;
            uint8_t *d = file_load(p, &n);
            if (!d) { printf("FAIL %s: %s\n", archives[a], p); bad++; continue; }
            char ref[600];
            snprintf(ref, sizeof ref, "out/xp/%s/%s", base, p);
            FILE *f = fopen(ref, "rb");
            if (!f) {                                      /* tools/xp.py keeps the directory's case */
                free(d);
                continue;
            }
            uint8_t *r = malloc(n + 1);
            size_t rn = fread(r, 1, n + 1, f);
            fclose(f);
            if (rn != n || memcmp(r, d, n)) { printf("DIFF %s\n", ref); bad++; }
            compared++;
            free(r); free(d);
        }
    }
    printf("%d files, %d compared, %d bad\n", files, compared, bad);
    return bad != 0;
}
