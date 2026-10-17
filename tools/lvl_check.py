#!/usr/bin/env python3
"""Verify a .lvl independently of the converter and render a preview straight from the binary.

usage: lvl_check.py <file.lvl> <LevelName> [preview.png [yaw pitch zoom]]
Checks: header/offset sanity, every texture (unswizzle -> compare with the original DST1 decode, palette
included), batch/vertex ranges, collision indices. The preview uses ONLY data read from the .lvl."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import convert_level as cl
import render_level, dst, pvs_tex


def load(path):
    b = open(path, "rb").read()
    h = struct.unpack_from(cl.HDR_FMT, b, 0)
    assert h[0] == b"SKLV" and h[1] == 1, "bad magic/version"
    ntex, nbatch, nverts, ncv, ncp = h[2:7]
    off_tex, off_batch, off_verts, off_collv, off_collp = h[7:12]
    for o in (off_tex, off_batch, off_verts, off_collv, off_collp):
        assert o % 16 == 0 and o <= len(b)
    texs = []
    for i in range(ntex):
        w, hh, bpp, swz, alpha, _, clut_off, pix_off, n, _ = struct.unpack_from(cl.TEX_FMT, b, off_tex + i * struct.calcsize(cl.TEX_FMT))
        wb = w * bpp // 8
        pix = b[pix_off:pix_off + wb * hh]
        if swz:
            pix = cl.unswizzle(pix, wb, hh)
        if bpp == 4:
            idx = []
            for byte in pix:
                idx += [byte & 15, byte >> 4]
        else:
            idx = list(pix)
        clut = struct.unpack_from("<%dH" % n, b, clut_off)
        texs.append(dict(w=w, h=hh, bpp=bpp, swz=swz, alpha=alpha, idx=idx, clut=clut))
    batches = [struct.unpack_from(cl.BATCH_FMT, b, off_batch + i * 16) for i in range(nbatch)]
    verts = [struct.unpack_from(cl.VERT_FMT, b, off_verts + i * 24) for i in range(nverts)]
    cverts = [struct.unpack_from("<3f", b, off_collv + i * 12) for i in range(ncv)]
    cpolys = [struct.unpack_from("<4HI", b, off_collp + i * 12) for i in range(ncp)]
    return dict(size=len(b), header=h, texs=texs, batches=batches, verts=verts, cverts=cverts, cpolys=cpolys)


def main():
    path, level = sys.argv[1], sys.argv[2]
    d = load(path)
    print("file %d bytes: %d textures, %d batches, %d vertices, %d collision verts, %d polys" % (
        d["size"], len(d["texs"]), len(d["batches"]), len(d["verts"]), len(d["cverts"]), len(d["cpolys"])))
    # ---- textures vs original decode
    table = pvs_tex.resolve_textures(level)
    paths = []
    for p in table:
        if p is not None and os.path.normcase(p) not in [os.path.normcase(x) for x in paths]:
            paths.append(p)
    # converter assigns indices in first-use order, so match each stored texture to a source by content
    by_sig = {}
    for p in paths:
        t = dst.decode(open(p, "rb").read())
        by_sig.setdefault((t["w"], t["h"]), []).append((p, t))
    ok = bad = 0
    for tx in d["texs"]:
        matched = False
        for (sw, sh), lst in by_sig.items():
            if sh != tx["h"] or tx["w"] % sw:
                continue
            k = tx["w"] // sw
            for p, t in lst:
                src = [t["idx"][y * sw + (x % sw)] for y in range(sh) for x in range(sw * k)]
                if src == tx["idx"][:len(src)]:
                    pal = list(t["pal"])
                    if t["flag"] == 1 and pal:
                        pal[0] &= 0x7FFF
                    pal = (pal + [0] * tx["clut"].__len__())[:len(tx["clut"])]
                    if list(tx["clut"]) == pal:
                        matched = True
                        break
            if matched:
                break
        ok += matched; bad += (not matched)
    print("textures matching an original DST1 exactly (pixels + palette, after unswizzle): %d / %d" % (ok, ok + bad))
    # ---- ranges
    nv = len(d["verts"])
    tot = 0
    for tex, flags, first, count, _ in d["batches"]:
        assert first + count <= nv and count % 3 == 0 and (tex == 0xFFFF or tex < len(d["texs"]))
        tot += count
    assert tot == nv
    ncv = len(d["cverts"])
    assert all(p[i] < ncv for p in d["cpolys"] for i in range(3)) and all(p[3] == 0xFFFF or p[3] < ncv for p in d["cpolys"])
    print("batch ranges, vertex counts, collision indices: OK")
    # ---- preview from the file only
    if len(sys.argv) > 3:
        tex = []
        for tx in d["texs"]:
            pal = np.zeros((len(tx["clut"]), 4), np.uint8)
            for i, c in enumerate(tx["clut"]):
                r, g, b_ = c & 31, (c >> 5) & 31, (c >> 10) & 31
                e = lambda v: (v << 3) | (v >> 2)
                pal[i] = (e(r), e(g), e(b_), 255 if c & 0x8000 else 0)
            tex.append(pal[np.array(tx["idx"], np.int64)].reshape(tx["h"], tx["w"], 4))
        tris = []
        for t, flags, first, count, _ in d["batches"]:
            if flags & 1:
                continue
            w, h = (d["texs"][t]["w"], d["texs"][t]["h"]) if t != 0xFFFF else (1, 1)
            for i in range(first, first + count, 3):
                vs = d["verts"][i:i + 3]
                tris.append(([v[3:6] for v in vs], [(v[0] * w, v[1] * h) for v in vs], t if t != 0xFFFF else -1))
        yaw, pitch, zoom = (float(x) for x in sys.argv[4:7]) if len(sys.argv) >= 7 else (25, 24, 6.2)
        render_level.render(tex, tris, sys.argv[3], yaw, pitch, zoom)
        print("preview written:", sys.argv[3], "(%d triangles, background layer skipped)" % len(tris))


if __name__ == "__main__":
    main()
