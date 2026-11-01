/* Bink 1 video (revisions 'h' / 'i', what the disc's 17 movies use) and its RDFT audio, in place of
   binkw32.dll. The decoding follows FFmpeg's libavcodec/bink.c, binkaudio.c and binkdsp.c (LGPL 2.1+,
   used here as the reference for the format); the codec's constant tables (scan orders, patterns,
   Huffman trees, quantisers, band edges) are read from the disc's binkw32.dll at run time.

   Video comes out as YUV 4:2:0 and is converted to the rgb565 surface (BinkCopyToBuffer); the audio is
   interleaved float at the file's rate, queued as each frame is read (bink_audio pulls it). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct Bink Bink;

/* BinkOpen: NULL if the file is missing, not Bink 1 'h'/'i', or binkw32.dll's tables are unusable. */
Bink *bink_open(const char *path);
void bink_close(Bink *b);
int bink_frames(const Bink *b);
int bink_fps_num(const Bink *b);
int bink_fps_den(const Bink *b);
int bink_audio_rate(const Bink *b);   /* 0: no audio track */
/* BinkDoFrame + BinkNextFrame: decode the next frame (video and its audio). false at the end, or on
   a damaged frame (the movie stops there). */
bool bink_decode(Bink *b);
/* BinkCopyToBuffer: the last decoded picture into an rgb565 surface at (x, y), clipped to w x h. */
void bink_copy(const Bink *b, uint16_t *dst, int pitch, int w, int h, int x, int y);
/* Up to n stereo frames of queued audio into dst (interleaved L R); returns how many. */
int bink_audio(Bink *b, float *dst, int n);
