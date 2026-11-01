/* RNC ProPack method 2 (Rob Northen Compression), the packer of the .xp archives. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

enum { RNC_HEADER = 18 };

/* Unpacked size of the block at src (0 if it isn't an RNC method 2 block). */
uint32_t rnc_unpacked_size(const uint8_t *src, size_t len);
/* Unpack the block at src into dst (cap >= rnc_unpacked_size). false on a bad block or CRC. */
bool rnc_unpack(const uint8_t *src, size_t len, uint8_t *dst, size_t cap);
