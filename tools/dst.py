#!/usr/bin/env python3
"""DST1 texture decoder (EA Skate It DS).

Layout (little endian):
  0   'DST1'
  4   u32 version word (02 01 01 01)
  8   u16 format   2=2bpp 3=4bpp 4=8bpp (indexed)
  10  u16 flag     1 = palette entry 0 is the magenta colour key (transparent)
  12  u16 width
  14  u16 height
  16  u16 palette entry count
  18  palette: count * u16, DS RGB555 (R=bits0-4, G=5-9, B=10-14), bit15 = opaque
  ..  pixels, packed LSB-first (first pixel in the low bits), row-major
Variant (DATA/TRAFFIC/TEXTURES/variant*.dst): 16-byte header with no flag field:
  8 u16 format, 10 u16 width, 12 u16 height, 14 u16 palette count, palette at 16.
"""
import struct, sys, os

BPP = {2: 2, 3: 4, 4: 8}


def decode(b):
    assert b[:4] == b"DST1", "not DST1"
    fmt, flag, w, h, npal = struct.unpack_from("<5H", b, 8)
    hdr = 18
    if 18 + npal * 2 + w * h * BPP.get(fmt, 99) // 8 != len(b):
        fmt, w, h, npal = struct.unpack_from("<4H", b, 8)
        flag, hdr = 0, 16
    bpp = BPP[fmt]
    pal = struct.unpack_from("<%dH" % npal, b, hdr)
    pix = b[hdr + npal * 2:]
    per = 8 // bpp
    mask = (1 << bpp) - 1
    idx = []
    for byte in pix[: w * h // per]:
        for k in range(per):
            idx.append((byte >> (k * bpp)) & mask)
    return dict(fmt=fmt, flag=flag, w=w, h=h, pal=pal, idx=idx)


def rgba(c):
    r, g, b_ = c & 31, (c >> 5) & 31, (c >> 10) & 31
    e = lambda v: (v << 3) | (v >> 2)
    return (e(r), e(g), e(b_), 255 if c & 0x8000 else 0)


def to_image(t):
    from PIL import Image
    pal = [rgba(c) for c in t["pal"]] + [(255, 0, 255, 255)] * 256
    if t["flag"] == 1:
        pal[0] = pal[0][:3] + (0,)
    im = Image.new("RGBA", (t["w"], t["h"]))
    im.putdata([pal[i] for i in t["idx"]])
    return im


if __name__ == "__main__":
    t = decode(open(sys.argv[1], "rb").read())
    to_image(t).save(sys.argv[2])
