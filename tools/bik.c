/* Decode a movie off the disc: bik <path> [frame] [out.ppm] — checks every frame decodes, writes one
   frame as a PPM, and measures the audio's jumps at the transform blocks' seams against elsewhere. */
#include "bink.h"
#include "vfs_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2 || !vfs_mount_default()) { fprintf(stderr, "bik <path> [frame] [out.ppm]\n"); return 2; }
    Bink *b = bink_open(argv[1]);
    if (!b) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    int want = argc > 2 ? atoi(argv[2]) : -1, n = 0;
    static uint16_t fb[640 * 480];
    static float pcm[2 * 4096];
    double seam = 0, other = 0, prev = 0;
    long seams = 0, others = 0, at = 0;
    while (bink_decode(b)) {
        if (n == want && argc > 3) {
            bink_copy(b, fb, 640, 640, 480, 0, 0);
            FILE *f = fopen(argv[3], "wb");
            fprintf(f, "P6 640 480 255\n");
            for (int i = 0; i < 640 * 480; i++) {
                unsigned v = fb[i];
                fputc((v >> 11) << 3, f), fputc(((v >> 5) & 63) << 2, f), fputc((v & 31) << 3, f);
            }
            fclose(f);
        }
        int k;
        while ((k = bink_audio(b, pcm, 4096)) > 0)
            for (int i = 0; i < k; i++, at++) {
                double d = fabs(pcm[2 * i] - prev);
                prev = pcm[2 * i];
                if (at % 960 == 0) seam += d, seams++;
                else other += d, others++;
            }
        n++;
    }
    printf("%d of %d frames, audio %ld frames, seam jump %.5f, elsewhere %.5f\n", n, bink_frames(b), at,
           seams ? seam / seams : 0, others ? other / others : 0);
    int ok = n == bink_frames(b);
    bink_close(b);
    return ok ? 0 : 1;
}
