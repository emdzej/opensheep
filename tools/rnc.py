"""RNC ProPack method 2 decompressor (Rob Northen Compression), as used by Sheep's .xp archives."""
import struct

HEADER = 18


def header(buf, off=0):
    if buf[off:off + 3] != b"RNC":
        raise ValueError("not an RNC block at 0x%x" % off)
    method = buf[off + 3]
    unpacked, packed, ucrc, pcrc, leeway, chunks = struct.unpack_from(">IIHHBB", buf, off + 4)
    return dict(method=method, unpacked=unpacked, packed=packed, ucrc=ucrc, pcrc=pcrc,
                leeway=leeway, chunks=chunks)


def crc16(data):
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
        table.append(c)
    crc = 0
    for b in data:
        crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc


def unpack_m2(src, size):
    out = bytearray()
    pos = 0
    bitbuf = 0
    bitcnt = 0

    def byte():
        nonlocal pos
        b = src[pos]
        pos += 1
        return b

    def bit():
        nonlocal bitbuf, bitcnt
        if bitcnt == 0:
            bitbuf = byte()
            bitcnt = 8
        r = (bitbuf >> 7) & 1
        bitbuf = (bitbuf << 1) & 0xFF
        bitcnt -= 1
        return r

    def offset():
        o = 0
        if bit():
            o = bit()
            if bit():
                o = ((o << 1) | bit()) | 4
                if not bit():
                    o = (o << 1) | bit()
            elif not o:
                o = bit() + 2
        return ((o << 8) | byte()) + 1

    def copy(off, n):
        for _ in range(n):
            out.append(out[-off])

    bit(); bit()  # lock / key flags
    while len(out) < size:
        if not bit():
            out.append(byte())
            continue
        if bit():
            if bit():
                if bit():
                    n = byte() + 8
                    if n == 8:
                        bit()
                        continue  # end of chunk
                else:
                    n = 3
                copy(offset(), n)
            else:
                copy(byte() + 1, 2)
        else:
            n = bit() + 4
            if bit():
                n = ((n - 1) << 1) + bit()
            if n != 9:
                copy(offset(), n)
            else:
                n = ((bit() << 3 | bit() << 2 | bit() << 1 | bit()) << 2) + 12
                out += src[pos:pos + n]
                pos += n
    return bytes(out[:size])


def unpack(buf, off=0):
    h = header(buf, off)
    if h["method"] != 2:
        raise NotImplementedError("RNC method %d" % h["method"])
    data = buf[off + HEADER: off + HEADER + h["packed"]]
    out = unpack_m2(data, h["unpacked"])
    if crc16(out) != h["ucrc"]:
        raise ValueError("CRC mismatch at 0x%x" % off)
    return out, off + HEADER + h["packed"]
