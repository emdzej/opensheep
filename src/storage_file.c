/* plat_storage_* on the host filesystem (the tests and tools): one file per key in
   $OPENSHEEP_STORAGE, default out/storage (never the user's real saves). */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *key_path(const char *key, bool make_dir)
{
    static char path[1024];
    const char *e = getenv("OPENSHEEP_STORAGE");
    const char *dir = e && *e ? e : "out/storage";
    if (make_dir) mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/%s", dir, key);
    return path;
}

const char *plat_storage_location(const char *key) { return key_path(key, true); }

int plat_storage_get(const char *key, void *dst, int cap)
{
    FILE *f = fopen(key_path(key, false), "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    if (dst && n <= cap && fread(dst, 1, (size_t)n, f) != (size_t)n) n = -1;
    fclose(f);
    return (int)n;
}

bool plat_storage_set(const char *key, const void *data, int len)
{
    FILE *f = fopen(key_path(key, true), "wb");
    bool ok = f && fwrite(data, 1, (size_t)len, f) == (size_t)len;
    if (f && fclose(f) != 0) ok = false;
    return ok;
}
