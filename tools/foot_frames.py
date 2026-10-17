#!/usr/bin/env python3
"""Feet in the board's frame, per frame of a pose clip: the two legs of the skater skeleton (node chains 1-2-3-4 and 5-6-7-8; 3/7 = ankle, 4/8 = toe end) expressed in the BOARD's
local axes (x width, y deck normal, z long axis, nose = +z). Used to tell which foot kicks which edge in the flip clips (kickflip = the front foot flicks off the toe side).
usage: foot_frames.py clip [clip ...]"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf, pose_sheet as ps

_sk = None


def load():
    global _sk
    if _sk is None:
        sk = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
        off = [tuple(c / 4096 for c in o) for o in mbf.load_bof(ps.ARC_S + "/animdb.BOF")]
        _sk = (sk, off)
    return _sk


def frames(n):
    """per frame: dict with board R (local->model), board pos, joint positions (model space) of nodes 0..21"""
    sk, off = load()
    a = mbf.load_abf("%s/%d.abf" % (ps.ARC_S, n))
    bf = ps.load_board_abf("%s/%d.abf" % (ps.ARC_B, n))
    out = []
    for f in range(a["nframes"]):
        rp, qs, _ = a["frames"][f]
        P = mbf.pose(sk["nodes"], off, qs, tuple(c / 4096 for c in rp))
        bq, bp = bf[min(f, len(bf) - 1)]
        R = ps.inv(bq)
        J = [np.array(P[i][1]) for i in range(len(sk["nodes"]))]
        out.append({"R": R, "bp": np.array(bp), "J": J, "P": P})
    return out


def to_board(fr, m):
    return fr["R"].T @ (np.asarray(m) - fr["bp"])


if __name__ == "__main__":
    for n in [int(x) for x in sys.argv[1:]]:
        fs = frames(n)
        print("clip", n, "frames", len(fs))
        for i, fr in enumerate(fs):
            row = []
            for name, a_i, t_i in (("A", 3, 4), ("B", 7, 8)):
                an = to_board(fr, fr["J"][a_i]); to = to_board(fr, fr["J"][t_i])
                row.append("%s ankle(x%+.2f y%+.2f z%+.2f) toe-dir(x%+.2f y%+.2f z%+.2f)" % (name, *(an * 8.6), *((to - an) / (np.linalg.norm(to - an) + 1e-9))))
            print("  f%2d  %s" % (i, "   ".join(row)))
