/* Platform contract: everything the portable game core needs from the host. The core (every file in
   src/ except the backends) includes no platform headers; a backend implements the functions below and
   drives the app (app.h). Backend: platform_gasm.c (gasm). The tests supply their own pieces
   (vfs_host.c, storage_file.c, input_none.c).

   A backend's job, in order:
   1. Mount the game data (vfs.h): a directory, a disc image, or a VfsSource over host-provided bytes.
   2. app_init(argc, argv) once (it calls plat_init). false: exit with an error (already reported
      through plat_error).
   3. Call app_frame() once per 16 ms tick (62.5 Hz) until it returns false. Each call ends with one
      plat_present unless the app is quitting.
   4. app_exit() (it calls plat_shutdown).

   The core runs on one thread; the mixer is pulled by the backend after each frame. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "keys.h"

/* The original draws into a 16-bit DirectDraw surface (Sprite_Draw 0x41f5e0 blends with rgb565
   masks); the port keeps that surface, so blending and quantisation match. */
typedef struct {
    int w, h;              /* 640 x 480 */
    int pitch;             /* pixels per row */
    uint16_t *pixels;      /* rgb565 */
} Framebuffer;

/* ---- lifecycle ---- */
bool plat_init(const char *title, int w, int h);
void plat_shutdown(void);
/* Report a fatal error to the user (log line). May be called before plat_init. */
void plat_error(const char *title, const char *msg);

/* ---- video ---- */
Framebuffer *plat_fb(void);
/* Show the framebuffer. */
void plat_present(void);

/* ---- input ---- */
/* Gather input. false when the user asked to quit. */
bool plat_poll(void);
/* Keyboard state, KEY_* codes (keys.h, = USB HID usages). */
bool plat_key_down(int key);
/* Keys that went down since the previous plat_poll, in order; returns the count (<= cap). */
int plat_key_presses(int *keys, int cap);
/* Mouse: position in framebuffer pixels, buttons bit 0 left, bit 1 right, bit 2 middle; relative
   motion since the previous poll. false if there is no mouse. */
bool plat_mouse(int *x, int *y, int *dx, int *dy, uint32_t *buttons);
/* Capture the pointer (hidden, locked, relative motion unbounded by the window) for the mouse control,
   which reads DirectInput's relative motion; false gives it back. Best effort (a browser needs a
   click first). */
void plat_mouse_capture(bool on);
/* Buttons held on virtual pad 0..3 (gasm layout), and the left stick in [-32768, 32767]. */
enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 4, PAD_R = 1 << 5,
    PAD_SELECT = 1 << 6, PAD_START = 1 << 7, PAD_UP = 1 << 8, PAD_DOWN = 1 << 9, PAD_LEFT = 1 << 10,
    PAD_RIGHT = 1 << 11,
};
uint32_t plat_pad(int player);
/* A joystick, as the original's DirectInput device: the first connected gamepad's left stick (x, y in
   [-1, 1], y > 0 down) and its first four buttons (bit 0 south, 1 east, 2 west, 3 north). Real
   devices only, not the keyboard's pad bindings. false: none connected. */
bool plat_joystick(float *x, float *y, uint32_t *buttons);

/* ---- time ---- */
/* Milliseconds since start: a virtual clock, 16 ms per present (the original's tick). */
uint64_t plat_ticks_ms(void);

/* ---- storage (saves, high scores, settings) ---- */
/* Persistent key/value store (keys: [A-Za-z0-9._-], 1-128 bytes). get returns the value's length or -1
   if missing; copies it only if it fits in cap (cap 0 = query the length). */
int plat_storage_get(const char *key, void *dst, int cap);
bool plat_storage_set(const char *key, const void *data, int len);
const char *plat_storage_location(const char *key);
