"""Sheep .SPR sprite decoder (reader: Sprite_Draw 0x41f5e0).

Header: u32 frames, u32 size, u32 flags, char name[32]; then per frame {u32 offset, u16 group,
u16 index} at 44 + 8*i. Frame at offset: u32 size, i32 width, i32 height, i32 hot_x, i32 hot_y,
then rows of byte codes:
  0x00        end of row
  0x80..0xff  skip 256-b pixels (transparent)
  0x01..0x3f  b pixels of {u8 alpha 1..4 (25/50/75/100 %), u16 rgb565}
  0x40..0x7f  b-0x3f opaque u16 rgb565 pixels
"""
import struct
import sys

ALPHA = {1: 64, 2: 128, 3: 192, 4: 255}


def rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def header(d):
    frames, size, flags = struct.unpack_from("<III", d, 0)
    name = d[12:44].split(b"\0")[0].decode("latin1")
    table = [struct.unpack_from("<IHH", d, 44 + 8 * i) for i in range(frames)]
    return name, flags, table


def frame(d, off):
    """Return (w, h, hot_x, hot_y, rgba bytes)."""
    size, w, h, hx, hy = struct.unpack_from("<Iiiii", d, off)
    px = bytearray(w * h * 4)
    p = off + 20
    for y in range(h):
        x = 0
        while True:
            b = d[p]
            p += 1
            if b == 0:
                break
            if b >= 0x80:
                x += 256 - b
            elif b < 0x40:
                for _ in range(b):
                    a = d[p]
                    c = struct.unpack_from("<H", d, p + 1)[0]
                    p += 3
                    if 0 <= x < w:
                        i = (y * w + x) * 4
                        px[i:i + 4] = bytes(rgb565(c) + (ALPHA.get(a, 0),))
                    x += 1
            else:
                for _ in range(b - 0x3F):
                    c = struct.unpack_from("<H", d, p)[0]
                    p += 2
                    if 0 <= x < w:
                        i = (y * w + x) * 4
                        px[i:i + 4] = bytes(rgb565(c) + (255,))
                    x += 1
    return w, h, hx, hy, bytes(px)


def sheet(d, cols=8):
    from PIL import Image
    name, flags, table = header(d)
    frames = [frame(d, off) for off, g, i in table]
    cw = max(f[0] for f in frames) + 2
    ch = max(f[1] for f in frames) + 2
    rows = (len(frames) + cols - 1) // cols
    img = Image.new("RGBA", (cw * min(cols, len(frames)), ch * rows), (40, 40, 60, 255))
    for n, (w, h, hx, hy, px) in enumerate(frames):
        if w and h:
            im = Image.frombytes("RGBA", (w, h), px)
            img.alpha_composite(im, ((n % cols) * cw + 1, (n // cols) * ch + 1))
    return img


if __name__ == "__main__":
    for path in sys.argv[1:-1]:
        d = open(path, "rb").read()
        name, flags, table = header(d)
        print(path, name, "frames", len(table), "flags", flags, [(g, i) for o, g, i in table[:4]])
        sheet(d).save(sys.argv[-1] + "/" + path.split("/")[-1] + ".png")
