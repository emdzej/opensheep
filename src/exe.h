/* Run-time access to the original game executable (Sheep.exe on the CD root, a Win32 PE, image base
   0x400000). OpenSheep ships no data copied from it: the tables the port needs (level descriptors, flock
   tuning, object descriptors, sound tables, ...) are read from the user's own Sheep.exe at start-up. The generated *_tables.c / models_data.c files contain only addresses,
   counts and types, plus the loader code that fills the C structures from the mapped image.

   Usage: exe_load() once after the data is mounted (vfs.h) (it runs every registered table loader), then
   exe_ptr()/exe_u32()/... by virtual address. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EXE_FILE       "Sheep.exe"        /* looked up case-insensitively in the data root */
#define EXE_IMAGE_BASE 0x400000u
#define EXE_SIZE       380928u            /* the supported build (Sheep, Empire Interactive, Jan 2001) */
#define EXE_CRC32      0xb7bcd581u        /* CRC-32 (IEEE) of the whole file */

/* Loads EXE_FILE from the mounted data (vfs_read_all), checks size + CRC-32, maps the sections and runs
   the registered table loaders. Idempotent. On failure returns false and exe_error() explains why. */
bool exe_load(void);
const char *exe_error(void);
bool exe_loaded(void);

/* Mapped image [EXE_IMAGE_BASE, EXE_IMAGE_BASE + SizeOfImage); uninitialised data (.bss) reads as 0. */
bool exe_contains(uint32_t va, size_t n);
const void *exe_ptr(uint32_t va);                 /* NULL outside the image */
/* The same bytes, writable: the few tables the original changes in place (the menu pages' items and
   their text buffers). Aborts outside the image. */
void *exe_mut(uint32_t va, size_t n);
void exe_read(void *dst, uint32_t va, size_t n);  /* aborts outside the image */
uint32_t exe_u32(uint32_t va);
int32_t exe_s32(uint32_t va);
uint16_t exe_u16(uint32_t va);
uint8_t exe_u8(uint32_t va);
int8_t exe_s8(uint32_t va);
int16_t exe_s16(uint32_t va);
float exe_f32(uint32_t va);
const char *exe_str(uint32_t va);                 /* NUL-terminated string inside the image ("" if none) */
/* UTF-16LE string at va, narrowed to ASCII ('?' for anything else) into dst; returns dst. */
char *exe_wstr(uint32_t va, char *dst, size_t cap);

/* Runs the routine at va, which may only move constants and data between registers and fixed
   addresses (mov, xor, or/add/sub with an 8-bit immediate, push / pop, jmp, ret): the compiler's
   initialisers of the tables that are not constant data in the file (the menu pages' items). false
   if it meets anything else, after the writes so far. */
bool exe_run(uint32_t va);

/* Table loaders: each generated table module registers one with EXE_LOADER(fn); exe_load() calls them
   all (in registration order) after the image is mapped. */
typedef struct ExeLoader { void (*fn)(void); struct ExeLoader *next; } ExeLoader;
void exe_register(ExeLoader *l);
#define EXE_LOADER(fn)                                                             \
    static ExeLoader fn##_exe_node = { fn, NULL };                                 \
    __attribute__((constructor)) static void fn##_exe_register(void) { exe_register(&fn##_exe_node); }
