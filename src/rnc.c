/* RNC ProPack method 2 decoder. The original links the stock ProPack unpacker (called from the
   archive reader at 0x43b150); this is a port of the published algorithm, see tools/rnc.py. */
#include "rnc.h"
#include <string.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint16_t crc16(const uint8_t *p, size_t n)
{
    static uint16_t table[256];
    if (!table[1])
        for (int i = 0; i < 256; i++) {
            uint16_t c = (uint16_t)i;
            for (int k = 0; k < 8; k++) c = c & 1 ? (uint16_t)(c >> 1 ^ 0xA001) : (uint16_t)(c >> 1);
            table[i] = c;
        }
    uint16_t crc = 0;
    while (n--) crc = (uint16_t)(table[(crc ^ *p++) & 0xFF] ^ crc >> 8);
    return crc;
}

uint32_t rnc_unpacked_size(const uint8_t *src, size_t len)
{
    if (len < RNC_HEADER || memcmp(src, "RNC\x02", 4)) return 0;
    return be32(src + 4);
}

typedef struct {
    const uint8_t *p, *end;
    unsigned buf, cnt;
    bool bad;
} Bits;

static unsigned byte(Bits *b)
{
    if (b->p >= b->end) { b->bad = true; return 0; }
    return *b->p++;
}

static unsigned bit(Bits *b)
{
    if (!b->cnt) { b->buf = byte(b); b->cnt = 8; }
    unsigned r = b->buf >> 7 & 1;
    b->buf = b->buf << 1 & 0xFF;
    b->cnt--;
    return r;
}

static unsigned offset(Bits *b)
{
    unsigned o = 0;
    if (bit(b)) {
        o = bit(b);
        if (bit(b)) {
            o = (o << 1 | bit(b)) | 4;
            if (!bit(b)) o = o << 1 | bit(b);
        } else if (!o) {
            o = bit(b) + 2;
        }
    }
    return (o << 8 | byte(b)) + 1;
}

bool rnc_unpack(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    uint32_t size = rnc_unpacked_size(src, len);
    uint32_t packed = len >= RNC_HEADER ? be32(src + 8) : 0;
    if (!size && len >= RNC_HEADER && !memcmp(src, "RNC\x02", 4)) return true;
    if (!size || size > cap || packed > len - RNC_HEADER) return false;
    Bits b = { src + RNC_HEADER, src + RNC_HEADER + packed, 0, 0, false };
    uint8_t *o = dst, *oend = dst + size;
    bit(&b); bit(&b);                                      /* lock / key flags */
    while (o < oend && !b.bad) {
        unsigned n, off;
        if (!bit(&b)) { *o++ = (uint8_t)byte(&b); continue; }
        if (bit(&b)) {
            if (bit(&b)) {
                if (bit(&b)) {
                    n = byte(&b) + 8;
                    if (n == 8) { bit(&b); continue; }     /* end of chunk */
                } else {
                    n = 3;
                }
                off = offset(&b);
            } else {
                n = 2;
                off = byte(&b) + 1;
            }
        } else {
            n = bit(&b) + 4;
            if (bit(&b)) n = ((n - 1) << 1) + bit(&b);
            if (n == 9) {                                  /* raw run */
                n = 0;
                for (int k = 0; k < 4; k++) n = n << 1 | bit(&b);
                n = (n << 2) + 12;
                if ((size_t)(b.end - b.p) < n || (size_t)(oend - o) < n) return false;
                memcpy(o, b.p, n);
                o += n; b.p += n;
                continue;
            }
            off = offset(&b);
        }
        if (off > (size_t)(o - dst) || n > (size_t)(oend - o)) return false;
        for (; n; n--, o++) *o = o[-(ptrdiff_t)off];
    }
    return !b.bad && o == oend && crc16(dst, size) == (uint16_t)(src[12] << 8 | src[13]);
}
