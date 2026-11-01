/* The game's files: loose files on the disc, then the mounted .xp archives (first mounted wins), as
   the original's file layer does (File_Load 0x4043d0: a CFile on the path, else the archive set
   0x43a920). Paths are the game's: "data\world01\sprites\c3mu_kikk_.spr", any case, '\' or '/'. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Mount an archive by its disc path ("main.xp"). XpSet_Mount 0x43a7f0 (at most 64). */
bool xp_mount(const char *disc_path);
/* Unmount every archive. XpSet_UnmountAll 0x404640 */
void xp_unmount_all(void);

/* Whole file, malloc'd (free() it); NULL if missing or corrupt. size may be NULL. */
uint8_t *file_load(const char *path, size_t *size);
/* Size of a file without loading it; -1 if missing. */
int64_t file_size(const char *path);
bool file_exists(const char *path);

/* Tools and tests: the entries of mounted archive `a` (0 = first mounted). NULL past the end. */
const char *xp_entry(int a, int i);
