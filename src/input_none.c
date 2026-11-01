/* The native builds' input (platform.h): no mouse, no joystick, so the tests
   and tools run without them. */
#include "platform.h"

bool plat_joystick(float *x, float *y, uint32_t *buttons)
{
    *x = *y = 0.0f;
    *buttons = 0;
    return false;
}

void plat_mouse_capture(bool on) { (void)on; }

bool plat_mouse(int *x, int *y, int *dx, int *dy, uint32_t *buttons)
{
    *x = *y = *dx = *dy = 0;
    *buttons = 0;
    return false;
}
