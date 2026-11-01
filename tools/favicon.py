#!/usr/bin/env python3
"""Draw the site's favicon and the app icon's source, docs/public/favicon.png: a 32x32 pixel sheep
on grass, drawn here (not taken from the game). Standard library only.

    tools/favicon.py [out.png]
"""
import struct
import sys
import zlib

ART = """
................................
.....gggggggggggggggggggggg.....
...gggggggggggggggggggggggggg...
..gggggggggggggggggggggggggggg..
.gggggggggggggggggggggggggggggg.
gggggggggggwwwwwwwgggggggggggggg
gggggggggwwwwwwwwwwwwwgggggggggg
ggggggggwwwwwwwwwwwwwwwwgggggggg
gggggggwwwwwwwwwwwwwwwwwwggggggg
ggggggwwwwwwwwwwwwwwwwwwwwgggggg
gggggwwwwwwwwwwwwwwwwwwwwwwggggg
ggggwwwwwwwwwwwwwwwwwwwwwwwwgggg
gggkkkwwwwwwwwwwwwwwwwwwwwwwsggg
ggkkkkkkwwwwwwwwwwwwwwwwwwwwsggg
gkkkekkkkwwwwwwwwwwwwwwwwwwwsggg
gkkkkkkkkwwwwwwwwwwwwwwwwwwssggg
gkkkkkkkkwwwwwwwwwwwwwwwwwwsgggg
ggkkkkkkwwwwwwwwwwwwwwwwwwssgggg
gggkkkkswwwwwwwwwwwwwwwwwssggggg
ggggggggswwwwwwwwwwwwwwwssgggggg
gggggggggsssssssssssssssgggggggg
ggggggggggkkgggkkgggkkggkkgggggg
ggggggggggkkgggkkgggkkggkkgggggg
ggggggggggkkgggkkgggkkggkkgggggg
ggggggggggkkgggkkgggkkggkkgggggg
gggggggggkkkggkkkggkkkgkkkgggggg
dddddddddddddddddddddddddddddddd
.dddddddddddddddddddddddddddddd.
.dddddddddddddddddddddddddddddd.
..dddddddddddddddddddddddddddd..
...dddddddddddddddddddddddddd...
.....dddddddddddddddddddddd.....
"""
# grass and its shade from level 1.1, the wool and its shadow, the face, an eye
COLOURS = {'.': (0, 0, 0, 0), 'g': (0x18, 0x51, 0x08, 255), 'd': (0x08, 0x45, 0x00, 255),
           'w': (0xf7, 0xf4, 0xea, 255), 's': (0xc9, 0xc2, 0xb0, 255), 'k': (0x22, 0x1e, 0x1a, 255),
           'e': (0xf7, 0xf4, 0xea, 255)}


def png(rows):
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(c for px in r for c in COLOURS[px]) for r in rows)
    chunk = lambda kind, body: struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


rows = ART.strip().split('\n')
assert len(rows) == 32 and all(len(r) == 32 for r in rows), [len(r) for r in rows]
open(sys.argv[1] if len(sys.argv) > 1 else 'docs/public/favicon.png', 'wb').write(png(rows))
