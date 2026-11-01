/* The application as a frame-driven state machine (the original's App_Frame 0x40a170). The platform
   backend calls app_init once, app_frame until it returns false, then app_exit (platform.h). */
#pragma once
#include <stdbool.h>

/* Options: --level <world>.<level> (e.g. 1.1; 0.0 = the menu level). The game data must be mounted
   (vfs.h). false: fatal error, already reported with plat_error. */
bool app_init(int argc, char **argv);
/* One presented frame: poll input, advance, render, present. false = quit. */
bool app_frame(void);
void app_exit(void);
