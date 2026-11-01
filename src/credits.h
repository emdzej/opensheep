/* The credits (state 11). */
#pragma once
#include "platform.h"

void credits_start(void);                 /* 0x40d7a0 */
void credits_frame(Framebuffer *fb);      /* 0x40d9e0: g_app_sub becomes 2 at a key or the end */
void credits_end(void);                   /* 0x40dc70 */
