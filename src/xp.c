/* .xp archives (notes/formats.md): data first, then a directory tree, then a 20-byte footer
   {u32 ?, u32 dir_off, u32 dir_len, "XCELZLIB"} (Xp_Open 0x43ab10). The directory is flattened at
   mount into a sorted list of full upper-case paths. */
#include "xp.h"
#include "rnc.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *path;                 /* "DATA/WORLD01/SPRITES/C3MU_KIKK_.SPR" */
    uint32_t off, stored, unpacked;
    uint8_t flags;              /* bit 0: RNC-packed */
} XpEntry;

typedef struct {
    VfsFile *file;
    XpEntry *e;
    int n, cap;
} Xp;

enum { XP_MAX = 64 };
static Xp *mounted[XP_MAX];
static int nmounted;

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Normalise a game path: '/' separators, upper case, no leading or repeated separators (some level
   tables give names with a leading '\\' that the code appends after one; Windows ignores the double). */
static void norm(const char *in, char *out, size_t cap)
{
    while (*in == '\\' || *in == '/') in++;
    size_t o = 0;
    for (; *in && o + 1 < cap; in++) {
        char c = *in == '\\' ? '/' : *in;
        if (c == '/' && o && out[o - 1] == '/') continue;
        out[o++] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
    }
    out[o] = 0;
}

static void add(Xp *x, const char *path, uint32_t off, uint32_t stored, uint32_t unpacked, uint8_t flags)
{
    if (x->n == x->cap) {
        x->cap = x->cap ? x->cap * 2 : 256;
        x->e = realloc(x->e, (size_t)x->cap * sizeof *x->e);
    }
    char p[512];
    norm(path, p, sizeof p);
    x->e[x->n++] = (XpEntry){ strdup(p), off, stored, unpacked, flags };
}

/* listing: u32 first_node, u32 count, name; node: u32 data_off, u32 node_size, u32 stored,
   u32 unpacked_or_listing, u32 next, u8 flags, u32 name_len, name. */
static void walk(Xp *x, const uint8_t *d, uint32_t n, uint32_t at, const char *prefix, int depth)
{
    if (depth > 32 || at + 8 > n) return;
    uint32_t node = le32(d + at), count = le32(d + at + 4);
    for (uint32_t i = 0; i < count && node + 25 <= n; i++) {
        const uint8_t *p = d + node;
        uint32_t off = le32(p), stored = le32(p + 8), unp = le32(p + 12), next = le32(p + 16);
        uint8_t flags = p[20];
        uint32_t len = le32(p + 21);
        if (node + 25 + len > n) return;
        char name[256], path[512];
        snprintf(name, sizeof name, "%.*s", (int)len, (const char *)p + 25);
        snprintf(path, sizeof path, "%s%s%s", prefix, *prefix ? "/" : "", name);
        if (stored == 0xFFFFFFFFu) {
            if (strcmp(name, "..") && unp && unp != at) walk(x, d, n, unp, path, depth + 1);
        } else {
            add(x, path, off, stored, unp, flags);
        }
        if (!next) break;
        node = next;
    }
}

static int cmp_entry(const void *a, const void *b) { return strcmp(((const XpEntry *)a)->path, ((const XpEntry *)b)->path); }

bool xp_mount(const char *disc_path)
{
    if (nmounted >= XP_MAX) return false;
    VfsFile *f = vfs_open(disc_path);
    if (!f) return false;
    uint64_t size = vfs_file_size(f);
    uint8_t foot[20];
    if (size < 20 || vfs_read_at(f, size - 20, foot, 20) != 20 || memcmp(foot + 12, "XCELZLIB", 8)) {
        vfs_close(f);
        return false;
    }
    uint32_t doff = le32(foot + 4), dlen = le32(foot + 8);
    uint8_t *d = malloc(dlen ? dlen : 1);
    if (!d || doff + (uint64_t)dlen > size || vfs_read_at(f, doff, d, dlen) != dlen) {
        free(d);
        vfs_close(f);
        return false;
    }
    Xp *x = calloc(1, sizeof *x);
    x->file = f;
    walk(x, d, dlen, 0, "", 0);
    free(d);
    qsort(x->e, (size_t)x->n, sizeof *x->e, cmp_entry);
    mounted[nmounted++] = x;
    return true;
}

void xp_unmount_all(void)
{
    for (int i = 0; i < nmounted; i++) {
        Xp *x = mounted[i];
        for (int k = 0; k < x->n; k++) free(x->e[k].path);
        free(x->e);
        vfs_close(x->file);
        free(x);
    }
    nmounted = 0;
}

static const XpEntry *find(const char *path, Xp **owner)
{
    char p[512];
    norm(path, p, sizeof p);
    XpEntry key = { p, 0, 0, 0, 0 };
    for (int i = 0; i < nmounted; i++) {
        const XpEntry *e = bsearch(&key, mounted[i]->e, (size_t)mounted[i]->n, sizeof key, cmp_entry);
        if (e) { *owner = mounted[i]; return e; }
    }
    return NULL;
}

uint8_t *file_load(const char *path, size_t *size)
{
    char p[512];
    norm(path, p, sizeof p);
    uint8_t *data = vfs_read_all(p, size);                 /* a loose file on the disc */
    if (data) return data;
    Xp *x;
    const XpEntry *e = find(path, &x);
    if (!e) return NULL;
    uint8_t *raw = malloc(e->stored ? e->stored : 1);
    if (!raw || vfs_read_at(x->file, e->off, raw, e->stored) != (int64_t)e->stored) { free(raw); return NULL; }
    if (!(e->flags & 1)) {
        if (size) *size = e->stored;
        return raw;
    }
    uint8_t *out = malloc(e->unpacked ? e->unpacked : 1);
    if (!out || !rnc_unpack(raw, e->stored, out, e->unpacked)) {
        fprintf(stderr, "xp: corrupt packed file %s\n", path);
        free(raw); free(out);
        return NULL;
    }
    free(raw);
    if (size) *size = e->unpacked;
    return out;
}

int64_t file_size(const char *path)
{
    char p[512];
    norm(path, p, sizeof p);
    int64_t n = vfs_size(p);
    if (n >= 0) return n;
    Xp *x;
    const XpEntry *e = find(path, &x);
    return e ? (int64_t)((e->flags & 1) ? e->unpacked : e->stored) : -1;
}

bool file_exists(const char *path) { return file_size(path) >= 0; }

VfsFile *file_open(const char *path)
{
    char p[512];
    norm(path, p, sizeof p);
    return vfs_open(p);
}

const char *xp_entry(int a, int i)
{
    if (a < 0 || a >= nmounted || i < 0 || i >= mounted[a]->n) return NULL;
    return mounted[a]->e[i].path;
}
