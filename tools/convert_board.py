#!/usr/bin/env python3
"""Convert the generic skateboard into a PSP-ready .brd (SKBD) file.

usage: convert_board.py <deck_bmp_name> <wheels_bmp_name> <out.brd>
       e.g. convert_board.py board_generic_toon wheels_generic_wht dist/SkateItPSP/data/board.brd

The board MBF (board_generic_lod0.mbf) has three sections; their materials were identified from the UV extents (see docs/TECHNICAL.md):
  material 0 = deck underside + wheels + edges, 42x128 UV window = deck bitmap (42x105, rows 0..104) stacked on the wheel bitmap
               (42x23, rows 105..127); stored padded to 64x128, colour key = index 0 (magenta in the BMPs)
  material 1 = top surface, DATA/SKATERS/TEXTURES/griptape.dst (32x128)
  material 2 = trucks, DATA/SKATERS/TEXTURES/undersidetrucks.dst (32x64)
  texture 3 (no batch) = DATA/SKATERS/TEXTURES/boardshadow.dst (32x32, colour key), the blob shadow drawn under the rider by src/main.c
DS UVs are negative (wrap); v' = v + texture height puts them in range. The mesh is a single bone: board-local coordinates, y up
(deck normal), long axis z (nose = +z). The per-frame board transform comes from the pose archive (see convert_skater.py).

File (little endian, 16-byte aligned sections):
  Header (64 B)  char magic[4]='SKBD'; u32 version=1, ntex(4), nverts, off_tex, off_batch, off_verts; f32 scale; u32 pad[8]
  TexEntry (24 B) * ntex, then clut + pixels per texture (same layout as .lvl)
  Batch (16 B) * 3   u16 tex, u16 flags, u32 first_vertex, u32 vertex_count, u32 pad     (same layout as .lvl)
  Vertex (24 B) * nverts   triangle list, board-local model units: f32 u, v; u32 abgr; f32 x, y, z
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
import mbf, convert_level as cl

BOARD_TEX = "extracted/unpacked/DATA/SKATERS/CUSTOM/Textures/Board/"
TEXDIR = "extracted/unpacked/DATA/SKATERS/TEXTURES/"
KEY = (255, 0, 255)


def composite_texture(deck_name, wheels_name):
    deck = Image.open(BOARD_TEX + deck_name + ".bmp").convert("RGB")
    wheels = Image.open(BOARD_TEX + wheels_name + ".bmp").convert("RGB")
    assert deck.size == (42, 105) and wheels.size == (42, 23), (deck.size, wheels.size)
    W, H = 64, 128
    img = Image.new("RGB", (W, H), KEY)
    img.paste(deck, (0, 0)); img.paste(wheels, (0, 105))
    px = list(img.getdata())
    colours = sorted({c for c in px if c != KEY})
    if len(colours) > 255:                               # reduce while keeping index 0 for the colour key
        q = img.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
        pal = q.getpalette()[:255 * 3]
        colours = [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
        idx = [0 if c == KEY else q.getpixel((i % W, i // W)) + 1 for i, c in enumerate(px)]
    else:
        lut = {c: i + 1 for i, c in enumerate(colours)}
        idx = [0 if c == KEY else lut[c] for c in px]
    pal555 = [0] + [((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000) for r, g, b in colours]
    pal555 = (pal555 + [0] * 256)[:256]
    wb = W
    pix = bytes(idx)
    pix = cl.swizzle(pix, wb, H)
    return dict(w=W, h=H, bpp=8, swz=1, alpha=1, clut=struct.pack("<256H", *pal555), pix=pix, n=256)


def main():
    deck, wheels, out = sys.argv[1], sys.argv[2], sys.argv[3]
    d = mbf.load_mbf_multi("%s/MODELS/board_generic_lod0.mbf" % mbf.SK)
    texs = [composite_texture(deck, wheels), cl.prep_texture(TEXDIR + "griptape.dst"), cl.prep_texture(TEXDIR + "undersidetrucks.dst"),
            cl.prep_texture(TEXDIR + "boardshadow.dst")]
    verts = bytearray(); batches = []
    for mat, tris in sorted(d["sections"], key=lambda s: s[0]):
        t = texs[mat]               # (texs[3], the shadow, has no mesh section)
        first = len(verts) // 24
        for tri in tris:
            for pos, uv, nrm, bone in tri:
                u, v = uv[0], uv[1] + t["h"] * (1 if uv[1] < 0 else 0)
                n = np.array(nrm); n = n / (np.linalg.norm(n) or 1)
                shade = 0.62 + 0.38 * max(0.0, float(n @ np.array([0.35, 0.8, 0.5]) / np.linalg.norm([0.35, 0.8, 0.5])))
                c = int(min(255, shade * 255))
                verts += struct.pack(cl.VERT_FMT, u / t["w"], v / t["h"], c | (c << 8) | (c << 16) | (255 << 24), *pos)
        batches.append((mat, first, len(verts) // 24 - first))
    # merge sections of the same material? (lod0 has exactly one section per material)
    assert [b[0] for b in batches] == [0, 1, 2], batches

    off_tex = 64
    pos = cl.align16(off_tex + 24 * len(texs))
    entries = []
    for t in texs:
        clut_off = pos; pos = cl.align16(pos + len(t["clut"]))
        pix_off = pos; pos = cl.align16(pos + len(t["pix"]))
        entries.append((clut_off, pix_off))
    off_batch = pos; pos = cl.align16(pos + 16 * len(batches))
    off_verts = pos; pos = cl.align16(pos + len(verts))
    buf = bytearray(pos)
    buf[:64] = struct.pack("<4sIIIIII f 8I", b"SKBD", 1, len(texs), len(verts) // 24, off_tex, off_batch, off_verts, 8.6, *([0] * 8))
    for i, (t, (co, po)) in enumerate(zip(texs, entries)):
        buf[off_tex + 24 * i:off_tex + 24 * i + 24] = struct.pack(cl.TEX_FMT, t["w"], t["h"], t["bpp"], t["swz"], t["alpha"], 0, co, po, t["n"], 0)
        buf[co:co + len(t["clut"])] = t["clut"]
        buf[po:po + len(t["pix"])] = t["pix"]
    for i, (mat, first, count) in enumerate(batches):
        buf[off_batch + 16 * i:off_batch + 16 * i + 16] = struct.pack(cl.BATCH_FMT, mat, 0, first, count, 0)
    buf[off_verts:off_verts + len(verts)] = verts
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "wb").write(buf)
    print("board: %d vertices (%d triangles), textures %s, %.2f MB" % (len(verts) // 24, len(verts) // 72,
          [(t["w"], t["h"]) for t in texs], len(buf) / 1048576))


if __name__ == "__main__":
    main()
