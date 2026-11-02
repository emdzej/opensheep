#!/usr/bin/env python3
"""Make OpenSheep's icons from tools/icon/source.png (1024x1024). Needs Pillow; run it after changing
the source and commit what it writes (CI only copies and converts these files):

    python3 -I tools/icon/make-icons.py

  docs/public/favicon.png            64 px, rounded square (the site's favicon and logo)
  docs/public/apple-touch-icon.png   180 px, square (iOS rounds it)
  tools/icon/opensheep-macos.png     1024 px, macOS shape: rounded square inset on a clear canvas
                                     (tools/make-icns.sh makes the .app's .icns from it)
  tools/icon/opensheep.png           256 px, rounded square (the Linux bundle)
  tools/icon/opensheep.ico           16-256 px, rounded square (written into the Windows gasm-run.exe)

The source is our own: generated for OpenSheep with fal.ai (google/nano-banana-2.1, November 2026),
prompt "a single cute, round, fluffy white sheep with a dark charcoal face ... on a soft rolling green
grass hill under a warm light-blue sky ... no text". Nothing from the game.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
SRC = Image.open(ROOT / 'tools/icon/source.png').convert('RGBA')
SS = 4   # supersampling for the masks' edges


def rounded(size, radius, inset=0):
    """The source in a rounded square of `size` (inset px clear around it), antialiased."""
    inner = size - 2 * inset
    art = SRC.resize((inner, inner), Image.LANCZOS)
    mask = Image.new('L', (inner * SS, inner * SS), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, inner * SS - 1, inner * SS - 1), radius * SS, fill=255)
    mask = mask.resize((inner, inner), Image.LANCZOS)
    out = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    out.paste(art, (inset, inset), mask)
    return out


def macos():
    """Apple's grid: an 824 px rounded square (radius 185) in 1024, with a soft shadow below."""
    size, inset, radius = 1024, 100, 185
    shadow = Image.new('L', (size, size), 0)
    ImageDraw.Draw(shadow).rounded_rectangle((inset, inset + 12, size - inset, size - inset + 12), radius, fill=90)
    shadow = shadow.filter(ImageFilter.GaussianBlur(14))
    out = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    out.putalpha(shadow)
    icon = rounded(size, radius, inset)
    return Image.alpha_composite(out, icon)


def main():
    rounded(64, 12).save(ROOT / 'docs/public/favicon.png', optimize=True)
    SRC.convert('RGB').resize((180, 180), Image.LANCZOS).save(ROOT / 'docs/public/apple-touch-icon.png', optimize=True)
    macos().save(ROOT / 'tools/icon/opensheep-macos.png', optimize=True)
    rounded(256, 46).save(ROOT / 'tools/icon/opensheep.png', optimize=True)
    big = rounded(256, 46)
    big.save(ROOT / 'tools/icon/opensheep.ico', sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])


if __name__ == '__main__':
    main()
