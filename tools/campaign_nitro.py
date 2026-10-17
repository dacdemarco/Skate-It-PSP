#!/usr/bin/env python3
"""Minimal NITRO graphics readers: NCLR (palette), NCGR (tiles), NSCR (screen map), NCER (cell bank, header only), NANR.
All are 'NARC-less' standard files: [magic4][u16 0xFEFF][u16 ver][u32 filesize][u16 hdrsize][u16 nblocks] + blocks [tag4][u32 size]...
  NCLR 'RLCN': 'TTLP' block: u32 depth (3=4bpp,4=8bpp), u32 pad, u32 datasize, u32 offset(0x10), then RGB555 entries.
  NCGR 'RGCN': 'RAHC' block: u16 tilesH, u16 tilesW (0xFFFF = unknown), u32 depth (3=4bpp, 4=8bpp), u32 pad,
               u32 tiled flag(0=tiled), u32 datasize, u32 offset(0x18), tile data. optional 'SOPC'.
  NSCR 'RCSN': 'NRCS' block: u16 width_px, u16 height_px, u32 color mode, u32 data size, u16 entries
               (bits 0-9 tile, 10 hflip, 11 vflip, 12-15 palette).
Usage: campaign_nitro.py <dir with ncgr/nclr/nscr> -> writes PNGs to the scratchpad dir given as 2nd arg."""
import struct, sys, os
from PIL import Image


def blocks(b):
    nblk = struct.unpack_from("<H", b, 14)[0]
    p = struct.unpack_from("<H", b, 12)[0]
    out = {}
    for _ in range(nblk):
        tag = b[p:p + 4].decode("latin-1")
        sz = struct.unpack_from("<I", b, p + 4)[0]
        out[tag] = b[p + 8:p + sz]
        p += sz
    return out


def read_nclr(path):
    d = blocks(open(path, "rb").read())["TTLP"]
    depth, _, size, off = struct.unpack_from("<IIII", d, 0)
    n = (len(d) - 16) // 2
    pal = []
    for i in range(n):
        v = struct.unpack_from("<H", d, 16 + 2 * i)[0]
        pal.append((((v & 31) * 255) // 31, (((v >> 5) & 31) * 255) // 31, (((v >> 10) & 31) * 255) // 31))
    return pal, depth


def read_ncgr(path):
    bl = blocks(open(path, "rb").read())
    d = bl["RAHC"]
    th, tw, depth = struct.unpack_from("<HHI", d, 0)
    tiled, size, off = struct.unpack_from("<III", d, 12)
    data = d[24:24 + size]
    bpp = 4 if depth == 3 else 8
    return dict(th=th, tw=tw, bpp=bpp, tiled=(tiled == 0), data=data)


def tiles(g):
    """-> list of 8x8 tiles, each a list of 64 palette indices"""
    bpp = g["bpp"]; data = g["data"]
    ts = 32 if bpp == 4 else 64
    out = []
    for t in range(len(data) // ts):
        raw = data[t * ts:(t + 1) * ts]
        if bpp == 4:
            px = []
            for c in raw:
                px += [c & 15, c >> 4]
        else:
            px = list(raw)
        out.append(px)
    return out


def render_tiles(g, pal, width_tiles=None, pal_off=0):
    ts = tiles(g)
    if g["tw"] not in (0xFFFF, 0) and g["tiled"]:
        wt = g["tw"]
    else:
        wt = width_tiles or 32
    ht = (len(ts) + wt - 1) // wt
    im = Image.new("RGB", (wt * 8, ht * 8))
    px = im.load()
    for i, t in enumerate(ts):
        tx, ty = (i % wt) * 8, (i // wt) * 8
        for j, v in enumerate(t):
            c = pal[(pal_off + v) % len(pal)]
            px[tx + j % 8, ty + j // 8] = c
    return im


def read_nscr(path):
    d = blocks(open(path, "rb").read())["NRCS"]
    w, h, mode, size = struct.unpack_from("<HHII", d, 0)
    ents = struct.unpack_from("<%dH" % (size // 2), d, 12)
    return w, h, ents


def render_screen(path_scr, g, pal):
    w, h, ents = read_nscr(path_scr)
    ts = tiles(g)
    bpp = g["bpp"]
    im = Image.new("RGB", (w, h))
    px = im.load()
    wt = w // 8
    for i, e in enumerate(ents):
        if i >= wt * (h // 8): break
        ti, hf, vf, pl = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1, e >> 12
        t = ts[ti] if ti < len(ts) else [0] * 64
        for j, v in enumerate(t):
            x, y = j % 8, j // 8
            if hf: x = 7 - x
            if vf: y = 7 - y
            ci = v + (pl * 16 if bpp == 4 else 0)
            px[(i % wt) * 8 + x, (i // wt) * 8 + y] = pal[ci % len(pal)]
    return im


if __name__ == "__main__":
    root = "extracted/files/DATA/TILES"
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    pal, _ = read_nclr(root + "/CareerMap.NCLR")
    g = read_ncgr(root + "/CareerMap.NCGR")
    print({k: v for k, v in g.items() if k != "data"}, len(g["data"]), len(pal))
    render_screen(root + "/CareerMap.NSCR", g, pal).save(out + "/careermap.png")
    render_tiles(g, pal).save(out + "/careermap_tiles.png")


# ----------------------------------------------------------------------------------------------------------------- NCER (cell bank)
OBJ_SIZES = {0: [(8, 8), (16, 16), (32, 32), (64, 64)], 1: [(16, 8), (32, 8), (32, 16), (64, 32)], 2: [(8, 16), (8, 32), (16, 32), (32, 64)]}


def read_ncer(path):
    """-> list of cells, each a list of OAM dicts {x,y,w,h,tile,pal,hf,vf,bpp8}.
    NCER 'RECN': 'KBEC' block body: u16 ncells, u16 bank type, u32 cell info offset (0x18), ... cell info = {u16 noam, u16 attr, u32 oam offset}
    (+8 bytes bbox each when bank type == 1); OAM = 3 x u16 (GBA/DS OBJ attr0..2).  Verified on hudsprites_mainscreen (29 cells)."""
    b = open(path, "rb").read()
    d = blocks(b)["KBEC"]
    ncell, btype, off = struct.unpack_from("<HHI", d, 0)
    stride = 16 if btype == 1 else 8
    oam_base = off + ncell * stride
    cells = []
    for c in range(ncell):
        noam, attr, ooff = struct.unpack_from("<HHI", d, off + c * stride)
        oams = []
        for k in range(noam):
            a0, a1, a2 = struct.unpack_from("<3H", d, oam_base + ooff + 6 * k)
            y = a0 & 0xFF
            if y >= 128: y -= 256
            x = a1 & 0x1FF
            if x >= 256: x -= 512
            shape, size = (a0 >> 14) & 3, (a1 >> 14) & 3
            w, h = OBJ_SIZES[shape][size]
            oams.append(dict(x=x, y=y, w=w, h=h, tile=a2 & 0x3FF, pal=a2 >> 12, hf=(a1 >> 12) & 1, vf=(a1 >> 13) & 1, bpp8=(a0 >> 13) & 1))
        cells.append(oams)
    return cells


def render_cell(oams, g, pal):
    ts = tiles(g)
    if not oams: return None
    x0 = min(o["x"] for o in oams); y0 = min(o["y"] for o in oams)
    x1 = max(o["x"] + o["w"] for o in oams); y1 = max(o["y"] + o["h"] for o in oams)
    im = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
    px = im.load()
    bpp = g["bpp"]
    for o in oams:
        tw, th = o["w"] // 8, o["h"] // 8
        for ty in range(th):
            for tx in range(tw):
                ti = o["tile"] + ty * tw + tx           # 1D mapping, one index step per 8x8 tile of the file's bit depth
                t = ts[ti] if ti < len(ts) else [0] * 64
                for j, v in enumerate(t):
                    if v == 0: continue
                    xx, yy = tx * 8 + j % 8, ty * 8 + j // 8
                    if o["hf"]: xx = o["w"] - 1 - xx
                    if o["vf"]: yy = o["h"] - 1 - yy
                    ci = v + (o["pal"] * 16 if bpp == 4 else 0)
                    c = pal[ci % len(pal)]
                    px[o["x"] - x0 + xx, o["y"] - y0 + yy] = c + (255,)
    return im
