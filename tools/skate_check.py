#!/usr/bin/env python3
"""Verify .skt (SKS2) and .brd (SKBD) independently of the converters, and render poses from the file data only.
usage: skate_check.py <skater.skt> <board.brd> <out.png> [anim ...]
Also provides load_skt/load_brd/fk for other checks (the C skeleton code in src/skel.c is compared against fk())."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import convert_level as cl, render_level
from PIL import Image

A2 = np.array([[-1.0, 0, 0], [0, 0, 1.0], [0, 1.0, 0]])      # data (x,y,z; z up, board nose +y) -> viewer (y up, nose +z), det +1


def tex_rgba(b, off):
    w, h, bpp, swz, alpha, _, clut_off, pix_off, n, _ = struct.unpack_from(cl.TEX_FMT, b, off)
    wb = w * bpp // 8
    pix = b[pix_off:pix_off + wb * h]
    if swz:
        pix = cl.unswizzle(pix, wb, h)
    idx = []
    if bpp == 4:
        for byte in pix:
            idx += [byte & 15, byte >> 4]
    else:
        idx = list(pix)
    clut = struct.unpack_from("<%dH" % n, b, clut_off)
    pal = np.zeros((n, 4), np.uint8)
    for i, c in enumerate(clut):
        e = lambda v: (v << 3) | (v >> 2)
        pal[i] = (e(c & 31), e((c >> 5) & 31), e((c >> 10) & 31), 255 if c & 0x8000 else 0)
    return pal[np.array(idx)].reshape(h, w, 4), w, h


def load_skt(path):
    b = open(path, "rb").read()
    magic, ver, nverts, nbones, nanims, off_tex, off_verts, off_vbone, off_skel, off_anims, scale, height = struct.unpack_from("<4sIIII IIIII ff", b, 0)
    assert magic == b"SKS2" and ver == 2
    tex = tex_rgba(b, off_tex)
    verts = [struct.unpack_from(cl.VERT_FMT, b, off_verts + 24 * i) for i in range(nverts)]
    vbone = list(b[off_vbone:off_vbone + nverts])
    skel = [struct.unpack_from("<i3f", b, off_skel + 16 * i) for i in range(nbones)]
    anims = []
    for a in range(nanims):
        nf, fo, bn, bo = struct.unpack_from("<4I", b, off_anims + 16 * a)
        fr = [(np.frombuffer(b, "<f4", 3, fo + i * 364), np.frombuffer(b, "<f4", nbones * 4, fo + i * 364 + 12).reshape(nbones, 4)) for i in range(nf)]
        bf = [(np.frombuffer(b, "<f4", 3, bo + i * 28), np.frombuffer(b, "<f4", 4, bo + i * 28 + 12)) for i in range(bn)]
        anims.append((fr, bf))
    return dict(tex=tex, verts=verts, vbone=vbone, skel=skel, anims=anims, scale=scale, height=height, nbones=nbones)


def load_brd(path):
    b = open(path, "rb").read()
    magic, ver, ntex, nverts, off_tex, off_batch, off_verts, scale = struct.unpack_from("<4sIIIIII f", b, 0)
    assert magic == b"SKBD" and ver == 1
    texs = [tex_rgba(b, off_tex + 24 * i) for i in range(ntex)]
    batches = [struct.unpack_from(cl.BATCH_FMT, b, off_batch + 16 * i) for i in range(3)]
    verts = [struct.unpack_from(cl.VERT_FMT, b, off_verts + 24 * i) for i in range(nverts)]
    return dict(texs=texs, batches=batches, verts=verts, scale=scale)


def qmat_inv(q):
    x, y, z, w = (float(v) for v in q); x, y, z = -x, -y, -z
    n = (x * x + y * y + z * z + w * w) ** .5; x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def fk(skel, root, quats):
    G = []
    for i, (par, *o) in enumerate(skel):
        R = qmat_inv(quats[i])
        if par < 0:
            G.append((R, np.array(root, float)))
        else:
            pR, pt = G[par]
            G.append((pR @ R, pt + pR @ np.array(o)))
    return G


def pose_tris(sk, bd, anim, frame, tex_base=0):
    fr, bf = sk["anims"][anim]
    root, quats = fr[min(frame, len(fr) - 1)]
    G = fk(sk["skel"], root, quats)
    S = sk["scale"]
    tris = []
    V = sk["verts"]; vb = sk["vbone"]
    for i in range(0, len(V), 3):
        pts, uvs = [], []
        for k in range(3):
            v = V[i + k]; R, t = G[vb[i + k]]
            pts.append(S * (A2 @ (R @ np.array(v[3:6]) + t))); uvs.append((v[0] * sk["tex"][1], v[1] * sk["tex"][2]))
        tris.append((pts, uvs, tex_base))
    bp, bq = bf[min(frame, len(bf) - 1)]
    R = qmat_inv(bq)
    for tex, first, count in [(b[0], b[2], b[3]) for b in bd["batches"]]:
        t_ = bd["texs"][tex]
        for i in range(first, first + count, 3):
            pts, uvs = [], []
            for k in range(3):
                v = bd["verts"][i + k]
                pts.append(S * (A2 @ (R @ np.array(v[3:6]) + bp))); uvs.append((v[0] * t_[1], v[1] * t_[2]))
            tris.append((pts, uvs, tex_base + 1 + tex))
    return tris


def main():
    sk = load_skt(sys.argv[1]); bd = load_brd(sys.argv[2]); out = sys.argv[3]
    anims = [int(a) for a in sys.argv[4:]] or [0, 1, 2, 3, 42, 244]
    print("skater: %d verts, %d bones, %d anims, scale %.2f, height %.2f m; board: %d verts" % (len(sk["verts"]), sk["nbones"], len(sk["anims"]), sk["scale"], sk["height"], len(bd["verts"])))
    tex = [sk["tex"][0]] + [t[0] for t in bd["texs"]]
    tiles = []
    for a in anims:
        for yaw in (60, 200):
            tris = pose_tris(sk, bd, a, 0)
            render_level.render(tex, tris, "_tmp.png", yaw, 10, 120, W=260, H=330, center=(0, 0.9, 0))
            tiles.append(Image.open("_tmp.png").copy())
    cols = 4
    sheet = Image.new("RGB", (260 * cols, 330 * ((len(tiles) + cols - 1) // cols)))
    for i, im in enumerate(tiles):
        sheet.paste(im, ((i % cols) * 260, (i // cols) * 330))
    sheet.save(out); os.remove("_tmp.png")


if __name__ == "__main__":
    main()
