"""Directory of a Sheep .xp archive.

Footer (last 16 bytes): u32 dir_off, u32 dir_len, "XCELZLIB".
Directory (offsets relative to dir_off):
  listing: u32 first_child, u32 count, name, NUL      (the root listing is at 0, named ROOT)
  node:    u32 data_off, u32 node_size, u32 stored, u32 unpacked_or_listing, u32 next_sibling,
           u8 flags, u32 namelen, name, NUL
  A directory node has stored == 0xFFFFFFFF and its 4th field points to its listing; ".." nodes
  point back (listing 0 / parent). Files: data_off is absolute in the archive, `stored` bytes
  there are either an RNC block (unpacked = size after unpacking) or the raw file.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rnc  # noqa: E402


def _node(d, at):
    off, size, stored, unp, nxt, flags, ln = struct.unpack_from("<IIIIIBI", d, at)
    name = d[at + 25: at + 25 + ln].decode("latin1")
    return off, size, stored, unp, nxt, flags, name


def walk(buf):
    if buf[-8:] != b"XCELZLIB":
        raise ValueError("no XCELZLIB footer")
    base, n = struct.unpack_from("<II", buf, len(buf) - 16)
    d = buf[base:base + n]

    def listing(at, path, seen):
        first, count = struct.unpack_from("<II", d, at)
        node = first
        for _ in range(count):
            off, size, stored, unp, nxt, flags, name = _node(d, node)
            if stored == 0xFFFFFFFF:
                if name != ".." and unp not in seen:
                    yield from listing(unp, path + [name], seen | {unp})
            else:
                yield "/".join(path + [name]), off, stored, unp, flags
            if not nxt:
                break
            node = nxt

    yield from listing(0, [], {0})


def read(buf, off, stored, unpacked):
    if buf[off:off + 4] == b"RNC\x02":
        data, _ = rnc.unpack(buf, off)
        return data
    return buf[off:off + stored]


if __name__ == "__main__":
    buf = open(sys.argv[1], "rb").read()
    outdir = sys.argv[2] if len(sys.argv) > 2 else None
    for path, off, stored, unp, flags in walk(buf):
        kind = "rnc" if buf[off:off + 4] == b"RNC\x02" else "raw"
        print("%-48s 0x%08x %9d %9d %s fl=%d %s" % (path, off, stored, unp, kind, flags, buf[off + (18 if kind == 'rnc' else 0):][:0].hex()))
        if outdir:
            dst = os.path.join(outdir, path)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, "wb").write(read(buf, off, stored, unp))
