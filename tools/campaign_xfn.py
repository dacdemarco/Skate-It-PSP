#!/usr/bin/env python3
"""XFN bitmap font reader (DATA/FONTS/*.xfn, 9 files).  Verified format:
  +0x000 u8 line height (8/14/19/22/27), +0x001 u8 = 1 (version?), rest of 0x000..0x100 zero
  +0x101 u8 advance width[256] indexed by character code (chars 0x00-0x1f = 0; Latin-1 / the game's own 'ALL_SPECIAL_CHARACTERS' order above 0x7f)
  +0x201 glyph pixels: for each code c = 0..255 with width[c] > 0, in code order: width[c] * height bytes, 8 bits per pixel (palette index /
         coverage; the proportional fonts use only 0/1, the large ones use a few shades), row-major.  The SPACE glyph (0x20) is stored
         with NO pixel data (size check is exact for all 9 files: file size == 0x201 + sum(width*height) - width[0x20]*height).
Usage: campaign_xfn.py <out_dir>  -> renders a sample sheet per font into <out_dir>."""
import sys, os, glob
from PIL import Image


def read_xfn(path):
    b = open(path, "rb").read()
    h = b[0]
    widths = list(b[0x101:0x201])
    p = 0x201
    glyphs = {}
    for c in range(256):
        w = widths[c]
        if w == 0 or c == 0x20:
            continue
        glyphs[c] = (w, h, b[p:p + w * h])
        p += w * h
    return dict(height=h, widths=widths, glyphs=glyphs, used=p, size=len(b))


def render(font, text):
    h = font["height"]
    W = sum(font["widths"][ord(ch) & 255] for ch in text) + 2
    im = Image.new("L", (W, h))
    px = im.load()
    x = 0
    maxv = max((max(g[2]) for g in font["glyphs"].values() if g[2]), default=1) or 1
    for ch in text:
        c = ord(ch) & 255
        if c in font["glyphs"]:
            w, hh, d = font["glyphs"][c]
            for i, v in enumerate(d):
                px[x + i % w, i // w] = int(255 * v / maxv)
        x += font["widths"][c]
    return im


if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for f in sorted(glob.glob("extracted/files/DATA/FONTS/*.xfn")):
        ft = read_xfn(f)
        print(os.path.basename(f), "h", ft["height"], "glyphs", len(ft["glyphs"]), "bytes used", ft["used"], "of", ft["size"],
              "maxpix", max(max(g[2]) for g in ft["glyphs"].values()))
        render(ft, "Skate It - Challenges 0123456789 Touch to start").save(os.path.join(out, os.path.basename(f) + ".png"))
