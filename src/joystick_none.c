/* The native builds' joystick (platform.h): none, so the tests and tools run without one. */
#include "platform.h"

bool plat_joystick(float *x, float *y, uint32_t *buttons)
{
    *x = *y = 0.0f;
    *buttons = 0;
    return false;
}
