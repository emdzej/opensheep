/* The keyboard as the original sees it: DirectInput's 256-key state (0x4a1d6c, read by 0x422750) and
   per-key hold counters (0x4a4108: ticks held, up to 60; 1 on the tick a key goes down), turned into
   the game's controls by 0x408ca0. The backend's keys (keys.h) are mapped onto DIK scan codes. */
#pragma once
#include <stdint.h>

enum {
    DIK_ESCAPE = 0x01, DIK_1 = 0x02, DIK_TAB = 0x0f, DIK_RETURN = 0x1c, DIK_LCONTROL = 0x1d, DIK_A = 0x1e, DIK_D = 0x20,
    DIK_J = 0x24, DIK_K = 0x25, DIK_LSHIFT = 0x2a, DIK_M = 0x32, DIK_LMENU = 0x38, DIK_SPACE = 0x39,
    DIK_F1 = 0x3b, DIK_UP = 0xc8, DIK_LEFT = 0xcb, DIK_RIGHT = 0xcd, DIK_DOWN = 0xd0,
};

extern uint8_t g_keydown[256];          /* 0x4a1d6c: non-zero while down */
extern int8_t g_keyheld[256];           /* 0x4a4108 */
extern uint8_t g_mouse_button;          /* 0x4bc91c: the left button is down */
extern int g_last_key;                  /* 0x4a40e4: the highest key down this tick */

/* Fill g_keydown from the backend (keys.h codes), with is_down(key) = plat_key_down. */
void input_read(int (*is_down)(int key));
/* 0x4056f0: advance the hold counters. */
void input_keys_tick(void);
/* 0x408ca0 (keyboard part): the shepherd's controls from the configured keys (0x44ad4c..). */
void input_map(void);
/* 0x405640: the mouse, DirectInput's relative motion (g_mouse_dx / g_mouse_dy); its left button runs,
   the right creeps. */
void mouse_read(void);
/* 0x422760: the pad (g_pad_x / g_pad_y -1000..1000, g_pad_buttons), from plat_joystick. */
void pad_read(void);
/* 0x410330: the pad's stick and buttons as presses of the arrow keys and space (the bonus races). */
void input_pad_keys(void);
