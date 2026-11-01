"""Sheep .BLO level decoder (reader: Level_LoadBLO 0x4072d0).

Header (little-endian u32 unless noted):
  0x00 "PC", 0x02 u8 version (>= 13)
  0x04 width (cells), 0x08 height (cells); a cell is 32x24 pixels
  0x0c ?, 0x10 tile count, 0x14 count A, 0x18 count B, 0x1c count C, 0x20 ?, 0x24 string bytes,
  0x28, 0x2c, 0x30, 0x34: copied to globals (meaning TBD)
Body, in order from 0x38:
  strings (0x24 bytes), C x 32-byte entries, B x 20-byte entries, A x 256-byte entries, A x u32,
  map: width*height u16 (tile index per cell),
  tiles: count x 768 u16 rgb565 (32x24),
  layer1: width*height u16, layer2: width*height u32 (low byte = cell type; the map renderer
  0x407ba0 skips types 1 and 2), layer3: width*height u32, then 0x30000 and 0x9000 bytes of
  tables; that is the end of the file.
Map cells: value & 0x3fff is a tile number, remapped through the tile-animation table; 0 = empty.
"""
import struct
import sys


def parse(d):
    h = dict(zip(["w", "h", "u0c", "ntiles", "na", "nb", "nc", "u20", "nstr", "u28", "u2c", "u30", "u34"],
                 struct.unpack_from("<13I", d, 4)))
    h["version"] = d[2]
    o = 0x38 + h["nstr"]
    h["off_c"] = o; o += h["nc"] * 32
    h["off_b"] = o; o += h["nb"] * 20
    h["off_a"] = o; o += h["na"] * 256
    h["off_a4"] = o; o += h["na"] * 4
    h["off_map"] = o; o += h["w"] * h["h"] * 2
    h["off_tiles"] = o; o += h["ntiles"] * 768 * 2
    h["off_l1"] = o; o += h["w"] * h["h"] * 2
    h["off_l2"] = o; o += h["w"] * h["h"] * 4
    h["off_l3"] = o; o += h["w"] * h["h"] * 4
    h["off_tab"] = o; o += 0x30000
    h["off_tab2"] = o; o += 0x9000
    h["end"] = o
    return h


def render(d, h):
    from PIL import Image
    import array
    w, hh = h["w"], h["h"]
    m = array.array("H"); m.frombytes(d[h["off_map"]:h["off_map"] + w * hh * 2])
    tiles = h["off_tiles"]
    img = Image.new("RGB", (w * 32, hh * 24))
    cache = {}
    for cy in range(hh):
        for cx in range(w):
            t = m[cy * w + cx] & 0x3FFF
            if t not in cache:
                if t == 0 or t >= h["ntiles"]:
                    cache[t] = Image.new("RGB", (32, 24), (255, 0, 255))
                else:
                    px = array.array("H"); px.frombytes(d[tiles + t * 1536: tiles + t * 1536 + 1536])
                    rgb = bytearray()
                    for c in px:
                        r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
                        rgb += bytes((r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2))
                    cache[t] = Image.frombytes("RGB", (32, 24), bytes(rgb))
            img.paste(cache[t], (cx * 32, cy * 24))
    return img


if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    h = parse(d)
    print({k: (hex(v) if k.startswith("off") or k == "end" else v) for k, v in h.items()}, "file", hex(len(d)))
    if len(sys.argv) > 2:
        render(d, h).save(sys.argv[2])
