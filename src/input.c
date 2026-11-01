#include "input.h"
#include "exe.h"
#include "game.h"
#include "keys.h"
#include "player.h"

uint8_t g_keydown[256];
int8_t g_keyheld[256];
int g_last_key;
uint8_t g_mouse_button;
static int pad_held;                    /* 0x4ae0bc: pad button 3 (bit 2), ticks held */

enum { KEY_RUN = 0x44ad4c, KEY_CREEP = 0x44ad50, KEY_ACTION = 0x44ad58 };   /* DIK codes, configurable */

/* keys.h (USB HID usage) -> DIK */
static const struct { uint8_t hid, dik; } keymap[] = {
    { KEY_ESCAPE, 0x01 }, { KEY_1, 0x02 }, { KEY_2, 0x03 }, { KEY_3, 0x04 }, { KEY_4, 0x05 }, { KEY_5, 0x06 },
    { KEY_6, 0x07 }, { KEY_7, 0x08 }, { KEY_8, 0x09 }, { KEY_9, 0x0a }, { KEY_0, 0x0b }, { KEY_BACKSPACE, 0x0e },
    { KEY_TAB, 0x0f }, { KEY_Q, 0x10 }, { KEY_W, 0x11 }, { KEY_E, 0x12 }, { KEY_R, 0x13 }, { KEY_T, 0x14 },
    { KEY_Y, 0x15 }, { KEY_U, 0x16 }, { KEY_I, 0x17 }, { KEY_O, 0x18 }, { KEY_P, 0x19 },
    { KEY_LEFTBRACKET, 0x1a }, { KEY_RIGHTBRACKET, 0x1b }, { KEY_RETURN, 0x1c }, { KEY_LCTRL, 0x1d },
    { KEY_A, 0x1e }, { KEY_S, 0x1f }, { KEY_D, 0x20 }, { KEY_F, 0x21 }, { KEY_G, 0x22 }, { KEY_H, 0x23 },
    { KEY_J, 0x24 }, { KEY_K, 0x25 }, { KEY_L, 0x26 }, { KEY_LSHIFT, 0x2a }, { KEY_Z, 0x2c }, { KEY_X, 0x2d },
    { KEY_C, 0x2e }, { KEY_V, 0x2f }, { KEY_B, 0x30 }, { KEY_N, 0x31 }, { KEY_M, 0x32 }, { KEY_RSHIFT, 0x36 },
    { KEY_KP_MULTIPLY, 0x37 }, { KEY_LALT, 0x38 }, { KEY_SPACE, 0x39 }, { KEY_F1, 0x3b }, { KEY_F2, 0x3c },
    { KEY_F3, 0x3d }, { KEY_F4, 0x3e }, { KEY_F5, 0x3f }, { KEY_F6, 0x40 }, { KEY_F7, 0x41 }, { KEY_F8, 0x42 },
    { KEY_F9, 0x43 }, { KEY_F10, 0x44 }, { KEY_KP_7, 0x47 }, { KEY_KP_8, 0x48 }, { KEY_KP_9, 0x49 },
    { KEY_KP_MINUS, 0x4a }, { KEY_KP_4, 0x4b }, { KEY_KP_5, 0x4c }, { KEY_KP_6, 0x4d }, { KEY_KP_PLUS, 0x4e },
    { KEY_KP_1, 0x4f }, { KEY_KP_2, 0x50 }, { KEY_KP_3, 0x51 }, { KEY_KP_0, 0x52 }, { KEY_KP_PERIOD, 0x53 },
    { KEY_F11, 0x57 }, { KEY_F12, 0x58 }, { KEY_KP_ENTER, 0x9c }, { KEY_RCTRL, 0x9d }, { KEY_KP_DIVIDE, 0xb5 },
    { KEY_RALT, 0xb8 }, { KEY_HOME, 0xc7 }, { KEY_UP, 0xc8 }, { KEY_PAGEUP, 0xc9 }, { KEY_LEFT, 0xcb },
    { KEY_RIGHT, 0xcd }, { KEY_END, 0xcf }, { KEY_DOWN, 0xd0 }, { KEY_PAGEDOWN, 0xd1 }, { KEY_INSERT, 0xd2 },
    { KEY_DELETE, 0xd3 }, { KEY_LGUI, 0xdb }, { KEY_RGUI, 0xdc },
};

void input_read(int (*is_down)(int key))
{
    for (int i = 0; i < 256; i++) g_keydown[i] = 0;
    for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
        if (is_down(keymap[i].hid)) g_keydown[keymap[i].dik] = 0x80;
}

void input_keys_tick(void)
{
    g_last_key = 0;
    for (int i = 0; i < 256; i++) {
        if (!g_keydown[i]) {
            g_keyheld[i] = 0;
        } else {
            g_last_key = i;
            if (g_keyheld[i] < 60) g_keyheld[i]++;
        }
    }
}

static int8_t held(uint32_t cfg) { return g_keyheld[exe_u32(cfg) & 0xff]; }

void input_map(void)
{
    /* the pad's button 3 also acts (TODO: the pads, 0x422760; the mouse, 0x405640) */
    if (!(g_pad_buttons & 4)) pad_held = 0;
    else if (pad_held < 60) pad_held++;
    g_key_creep = held(KEY_CREEP) != 0;
    g_key_run = held(KEY_RUN) != 0;
    g_action = held(KEY_ACTION) == 1 || pad_held == 1;
    g_key_x = g_key_y = 0;
    if (g_keyheld[DIK_LEFT]) g_key_x = -1;
    if (g_keyheld[DIK_RIGHT]) g_key_x = 1;
    if (g_keyheld[DIK_UP]) g_key_y = -1;
    if (g_keyheld[DIK_DOWN]) g_key_y = 1;
    /* M / K / J pick mouse, keyboard or pad (TODO: 0x4395a0 re-acquires the devices) */
    if (g_keyheld[DIK_M]) { g_input_mode = 0; g_pad_buttons = 0; }
    if (g_keyheld[DIK_K]) { g_input_mode = 1; g_pad_buttons = 0; }
    if (g_keyheld[DIK_J]) g_input_mode = 2;
}

void input_pad_keys(void)
{
    /* 0x410330: the pad pushed past 500 (released inside 200) or a button down counts as one more
       tick on the arrow keys / space */
    static uint8_t latch[5];   /* 0x4b9628 */
    static const struct { int axis; int sign; uint8_t key; } map[4] = {
        { 0, -1, DIK_LEFT }, { 0, 1, DIK_RIGHT }, { 1, -1, DIK_UP }, { 1, 1, DIK_DOWN },
    };
    for (int i = 0; i < 4; i++) {
        int v = (map[i].axis ? g_pad_y : g_pad_x) * map[i].sign;
        if (!latch[i]) {
            if (v > 500) { latch[i] = 1; g_keyheld[map[i].key]++; }
        } else if (v < 200) {
            latch[i] = 0;
        }
    }
    if (!latch[4]) {
        if (g_pad_buttons != 0) { latch[4] = 1; g_keyheld[DIK_SPACE]++; }
    } else if ((g_pad_buttons & 1) == 0) {
        latch[4] = 0;
    }
}

void pad_read(void)
{
    /* 0x422760 reads the DirectInput joystick: the stick's x / y and buttons 1-4 as bits 0-3 */
}
