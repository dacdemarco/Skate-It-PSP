#!/usr/bin/env python3
"""Contact sheet of gameplay poses (anims.arc / animb.arc) with the board, side view.
usage: pose_sheet.py <first> <count> <out.png> [frame] [yaw]"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, mbf, dst, convert_level as cl, render_level
from PIL import Image, ImageDraw

ARC_S, ARC_B = "extracted/unpacked/DATA/SKATERS/ARC_S", "extracted/unpacked/DATA/SKATERS/ARC_B"
S = 8.6
A = np.array([[0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [1.0, 0.0, 0.0]])
inv = lambda q: np.array(mbf.qmat((-q[0], -q[1], -q[2], q[3])))


def load_board_abf(path):
    b = open(path, "rb").read(); w = struct.unpack_from("<%dh" % (len(b) // 2), b, 0)
    return [(tuple(x / 4096 for x in w[4 + 8 * f:8 + 8 * f]), tuple(x / 4096 for x in w[9 + 8 * f:12 + 8 * f])) for f in range(w[2])]


def pose_tris(sk, off, bd, n, frame, tex_id_board):
    a = mbf.load_abf("%s/%d.abf" % (ARC_S, n)); f = min(frame, a["nframes"] - 1)
    rp, qs, _ = a["frames"][f]
    P = mbf.pose(sk["nodes"], off, qs, tuple(c / 4096 for c in rp))
    out = []
    for t in sk["tris"]:
        pts = [S * (A @ (np.array(P[v[3]][0]) @ np.array(v[0]) + np.array(P[v[3]][1]))) for v in t]
        out.append((pts, [v[1] for v in t], 0))
    bf = load_board_abf("%s/%d.abf" % (ARC_B, n)); bq, bp = bf[min(f, len(bf) - 1)]
    R = inv(bq)
    for mat, tris in bd["sections"]:
        for t in tris:
            pts = [S * (A @ (R @ np.array(v[0]) + np.array(bp))) for v in t]
            out.append((pts, [(0, 0)] * 3, tex_id_board))
    return out


def sheet(tiles, outp, yaw=90, cols=6):
    """tiles = [(pose, frame)]"""
    sk = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
    off = [tuple(c / 4096 for c in o) for o in mbf.load_bof(ARC_S + "/animdb.BOF")]
    bd = mbf.load_mbf_multi("%s/MODELS/board_generic_lod0.mbf" % mbf.SK)
    t = dst.decode(open("%s/TEXTURES/aaron.dst" % mbf.SK, "rb").read())
    tex = [np.asarray(dst.to_image(t).convert("RGBA")), np.full((2, 2, 4), (150, 90, 40, 255), np.uint8)]
    W, H = 200, 260
    img = Image.new("RGB", (cols * W, ((len(tiles) + cols - 1) // cols) * H))
    for i, (n, f) in enumerate(tiles):
        tris = pose_tris(sk, off, bd, n, f, 1)
        render_level.render(tex, tris, "_tmp.png", yaw, 8, 105, W=W, H=H, center=(0, 0.9, 0))
        im = Image.open("_tmp.png").convert("RGB"); ImageDraw.Draw(im).text((4, 4), "%d f%d" % (n, f), fill=(255, 255, 0))
        img.paste(im, ((i % cols) * W, (i // cols) * H))
    img.save(outp); os.remove("_tmp.png")


def main():
    first, count, outp = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
    frame = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    yaw = float(sys.argv[5]) if len(sys.argv) > 5 else 90
    sk = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
    off = [tuple(c / 4096 for c in o) for o in mbf.load_bof(ARC_S + "/animdb.BOF")]
    bd = mbf.load_mbf_multi("%s/MODELS/board_generic_lod0.mbf" % mbf.SK)
    t = dst.decode(open("%s/TEXTURES/aaron.dst" % mbf.SK, "rb").read())
    tex = [np.asarray(dst.to_image(t).convert("RGBA")), np.full((2, 2, 4), (150, 90, 40, 255), np.uint8)]
    cols, W, H = 8, 200, 260
    rows = (count + cols - 1) // cols
    sheet = Image.new("RGB", (cols * W, rows * H))
    for i in range(count):
        n = first + i
        tris = pose_tris(sk, off, bd, n, frame, 1)
        render_level.render(tex, tris, "_tmp.png", yaw, 8, 105, W=W, H=H, center=(0, 0.9, 0))
        im = Image.open("_tmp.png").convert("RGB"); ImageDraw.Draw(im).text((4, 4), str(n), fill=(255, 255, 0))
        sheet.paste(im, ((i % cols) * W, (i // cols) * H))
    sheet.save(outp); os.remove("_tmp.png")


if __name__ == "__main__":
    main()
