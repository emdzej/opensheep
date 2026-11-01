/* PE loader for the original game executable (see exe.h). */
#include "exe.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *image;          /* SizeOfImage bytes, sections copied to their RVAs */
static uint32_t image_size;
static char err[1024];
static ExeLoader *loaders, **loaders_tail = &loaders;

void exe_register(ExeLoader *l)
{
    l->next = NULL;
    *loaders_tail = l;
    loaders_tail = &l->next;
}

const char *exe_error(void) { return err; }
bool exe_loaded(void) { return image != NULL; }

static uint32_t crc32_ieee(const uint8_t *p, size_t n)
{
    static uint32_t tab[256];
    if (!tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
    uint32_t c = 0xffffffffu;
    while (n--) c = tab[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool map_pe(const uint8_t *d, size_t n)
{
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return false;
    uint32_t pe = rd32(d + 0x3c);
    if ((size_t)pe + 24 > n || memcmp(d + pe, "PE\0\0", 4)) return false;
    uint32_t nsec = rd16(d + pe + 6), optsz = rd16(d + pe + 20);
    const uint8_t *opt = d + pe + 24;
    if ((size_t)pe + 24 + optsz + nsec * 40 > n || optsz < 60) return false;
    if (rd32(opt + 28) != EXE_IMAGE_BASE) return false;
    image_size = rd32(opt + 56);
    image = calloc(1, image_size);
    if (!image) return false;
    for (uint32_t i = 0; i < nsec; i++) {
        const uint8_t *s = opt + optsz + i * 40;
        uint32_t rva = rd32(s + 12), len = rd32(s + 16), rawoff = rd32(s + 20);  /* raw data; the rest reads 0 */
        if ((uint64_t)rva + len > image_size) len = rva < image_size ? image_size - rva : 0;
        if ((uint64_t)rawoff + len > n) return false;
        memcpy(image + rva, d + rawoff, len);
    }
    return true;
}

static bool finish(const uint8_t *d, size_t n, const char *where)
{
    uint32_t crc = crc32_ieee(d, n);
    if (n != EXE_SIZE || crc != EXE_CRC32) {
        snprintf(err, sizeof err,
                 "%s is not the supported version of the Sheep game program "
                 "(found %zu bytes, CRC-32 %08x; expected %u bytes, CRC-32 %08x).\n\n"
                 "Use Sheep.exe from the root of the Sheep CD.",
                 where, n, crc, EXE_SIZE, EXE_CRC32);
        return false;
    }
    if (!map_pe(d, n)) {
        free(image); image = NULL;
        snprintf(err, sizeof err, "%s: not a valid Win32 executable image.", where);
        return false;
    }
    for (ExeLoader *l = loaders; l; l = l->next) l->fn();
    return true;
}

bool exe_load(void)
{
    if (image) return true;
    size_t n = 0;
    uint8_t *d = vfs_read_all(EXE_FILE, &n);
    if (!d) {
        snprintf(err, sizeof err,
                 "%s was not found in %s.\n\nOpenSheep reads the original game's tables from Sheep.exe: it is on the "
                 "root of the Sheep CD, next to main.xp and the world*.xp archives.", EXE_FILE,
                 vfs_mounted() ? vfs_describe() : "the game data");
        return false;
    }
    bool ok = finish(d, n, EXE_FILE);
    free(d);
    return ok;
}

bool exe_contains(uint32_t va, size_t n)
{
    return image && va >= EXE_IMAGE_BASE && (uint64_t)(va - EXE_IMAGE_BASE) + n <= image_size;
}

const void *exe_ptr(uint32_t va) { return exe_contains(va, 1) ? image + (va - EXE_IMAGE_BASE) : NULL; }

void *exe_mut(uint32_t va, size_t n)
{
    if (!exe_contains(va, n)) {
        fprintf(stderr, "exe_mut: 0x%08x+%zu outside the Sheep.exe image\n", va, n);
        abort();
    }
    return image + (va - EXE_IMAGE_BASE);
}

void exe_read(void *dst, uint32_t va, size_t n)
{
    if (!exe_contains(va, n)) {
        fprintf(stderr, "exe_read: 0x%08x+%zu outside the Sheep.exe image%s\n", va, n,
                image ? "" : " (not loaded: call exe_load() first)");
        abort();
    }
    memcpy(dst, image + (va - EXE_IMAGE_BASE), n);
}

uint32_t exe_u32(uint32_t va) { uint8_t b[4]; exe_read(b, va, 4); return rd32(b); }
int32_t exe_s32(uint32_t va) { return (int32_t)exe_u32(va); }
uint16_t exe_u16(uint32_t va) { uint8_t b[2]; exe_read(b, va, 2); return (uint16_t)rd16(b); }
uint8_t exe_u8(uint32_t va) { uint8_t b; exe_read(&b, va, 1); return b; }
int8_t exe_s8(uint32_t va) { return (int8_t)exe_u8(va); }
int16_t exe_s16(uint32_t va) { return (int16_t)exe_u16(va); }
float exe_f32(uint32_t va) { uint32_t u = exe_u32(va); float f; memcpy(&f, &u, 4); return f; }

double exe_f64(uint32_t va)
{
    uint64_t u = (uint64_t)exe_u32(va) | (uint64_t)exe_u32(va + 4) << 32;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

const char *exe_str(uint32_t va)
{
    if (!exe_contains(va, 1)) return "";
    const char *s = (const char *)image + (va - EXE_IMAGE_BASE);
    if (!memchr(s, 0, image_size - (va - EXE_IMAGE_BASE))) return "";
    return s;
}

char *exe_wstr(uint32_t va, char *dst, size_t cap)
{
    size_t i = 0;
    for (; i + 1 < cap && exe_contains(va + 2 * (uint32_t)i, 2); i++) {
        uint16_t c = exe_u16(va + 2 * (uint32_t)i);
        if (!c) break;
        dst[i] = c < 0x80 ? (char)c : '?';
    }
    if (cap) dst[i] = 0;
    return dst;
}

/* ---------------------------------------------------------------- static initialisers */

/* The registers of exe_run; 8-bit registers 4-7 are the high bytes of 0-3. */
static uint32_t reg8(const uint32_t *r, int n) { return n < 4 ? r[n] & 0xff : (r[n - 4] >> 8) & 0xff; }
static void set_reg8(uint32_t *r, int n, uint32_t v)
{
    if (n < 4) r[n] = (r[n] & ~0xffu) | (v & 0xff);
    else r[n - 4] = (r[n - 4] & ~0xff00u) | (v & 0xff) << 8;
}

static void store(uint32_t va, uint32_t v, int size) { memcpy(exe_mut(va, (size_t)size), &v, (size_t)size); }
static uint32_t load(uint32_t va, int size)
{
    uint32_t v = 0;
    exe_read(&v, va, (size_t)size);
    return v;
}

bool exe_run(uint32_t va)
{
    uint32_t r[8] = { 0 }, stack[16];
    int sp = 0;
    for (int steps = 0; steps < 10000; steps++) {
        int size = 4;
        uint8_t op = exe_u8(va++);
        if (op == 0x66) {
            size = 2;
            op = exe_u8(va++);
        }
        uint32_t mask = size == 4 ? 0xffffffffu : 0xffffu;
        if (op == 0x90) continue;
        if (op == 0xc3) return true;
        if (op == 0xe9) { va += exe_u32(va) + 4; continue; }
        if (op == 0xeb) { va += (uint32_t)(int32_t)exe_s8(va) + 1; continue; }
        if (op >= 0x50 && op <= 0x57) { if (sp == 16) break; stack[sp++] = r[op - 0x50]; continue; }
        if (op >= 0x58 && op <= 0x5f) { if (sp == 0) break; r[op - 0x58] = stack[--sp]; continue; }
        if (op >= 0xb8 && op <= 0xbf) {
            uint32_t v = size == 4 ? exe_u32(va) : exe_u16(va);
            va += (uint32_t)size;
            r[op - 0xb8] = (r[op - 0xb8] & ~mask) | v;
            continue;
        }
        if (op >= 0xb0 && op <= 0xb7) { set_reg8(r, op - 0xb0, exe_u8(va++)); continue; }
        if (op == 0xa0 || op == 0xa1) {
            int n = op == 0xa0 ? 1 : size;
            uint32_t v = load(exe_u32(va), n);
            va += 4;
            if (n == 1) set_reg8(r, 0, v);
            else r[0] = (r[0] & ~mask) | v;
            continue;
        }
        if (op == 0xa2 || op == 0xa3) {
            store(exe_u32(va), op == 0xa2 ? r[0] & 0xff : r[0] & mask, op == 0xa2 ? 1 : size);
            va += 4;
            continue;
        }
        uint8_t m = exe_u8(va++);
        int mod = m >> 6, rg = (m >> 3) & 7, rm = m & 7;
        bool mem = mod == 0 && rm == 5;
        if (!mem && mod != 3) break;            /* only [disp32] and registers */
        uint32_t addr = 0;
        if (mem) { addr = exe_u32(va); va += 4; }
        switch (op) {
        case 0x88: if (mem) store(addr, reg8(r, rg), 1); else set_reg8(r, rm, reg8(r, rg)); continue;
        case 0x8a: set_reg8(r, rg, mem ? load(addr, 1) : reg8(r, rm)); continue;
        case 0x89:
            if (mem) store(addr, r[rg] & mask, size);
            else r[rm] = (r[rm] & ~mask) | (r[rg] & mask);
            continue;
        case 0x8b: r[rg] = (r[rg] & ~mask) | ((mem ? load(addr, size) : r[rm]) & mask); continue;
        case 0xc6: if (rg) break; if (mem) store(addr, exe_u8(va), 1); else set_reg8(r, rm, exe_u8(va)); va++; continue;
        case 0xc7: {
            if (rg) break;
            uint32_t v = size == 4 ? exe_u32(va) : exe_u16(va);
            va += (uint32_t)size;
            if (mem) store(addr, v, size);
            else r[rm] = (r[rm] & ~mask) | v;
            continue;
        }
        case 0x31: case 0x33:
            if (mem || rm != rg) break;         /* only xor r, r */
            r[rm] &= ~mask;
            continue;
        case 0x83: {
            if (mem) break;
            uint32_t v = (uint32_t)(int32_t)exe_s8(va++), x = r[rm];
            if (rg == 0) x += v;
            else if (rg == 1) x |= v;
            else if (rg == 4) x &= v;
            else if (rg == 5) x -= v;
            else if (rg == 6) x ^= v;
            else break;
            r[rm] = (r[rm] & ~mask) | (x & mask);
            continue;
        }
        }
        break;
    }
    fprintf(stderr, "exe_run: unsupported code at 0x%08x\n", va);
    return false;
}
