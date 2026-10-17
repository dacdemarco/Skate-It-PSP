#!/usr/bin/env python3
"""Board animation placement test: does the board sit under the skater's feet, wheels on the ground?
Board ABF = one bone per frame: [q x4][marker][pos x3] (8 s16, same sampler layout as the root bone)."""
import sys, os, struct, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, mbf

BOARD_ANIM = "extracted/files/DATA/SKATERS/ANIMEXTRAB"

def load_board_abf(n):
    b = open("%s/%d.ABF" % (BOARD_ANIM, n), "rb").read()
    w = struct.unpack_from("<%dh" % (len(b) // 2), b, 0)
    N = w[2]
    assert len(w) == 4 + 8 * N
    fr = []
    for f in range(N):
        i = 4 + 8 * f
        fr.append((tuple(x / 4096 for x in w[i:i + 4]), w[i + 4], tuple(x / 4096 for x in w[i + 5:i + 8])))
    return fr

def main():
    sk = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
    off = [tuple(c / 4096 for c in o) for o in mbf.load_bof()]
    bd = mbf.load_mbf_multi("%s/MODELS/board_generic_lod0.mbf" % mbf.SK)
    bverts = np.array([v[0] for _, tris in bd["sections"] for t in tris for v in t])
    boff = np.array(mbf.load_bof(mbf.ANIM.replace("ANIMEXTRAS", "ANIMEXTRAB") + "/ANIMDB.BOF")[0]) / 4096
    print("board rest offset", np.round(boff, 4), "board verts", len(bverts))
    for n in (0, 1, 2, 11, 12, 21):
        sa = mbf.load_abf("%s/%d.ABF" % (mbf.ANIM, n))
        ba = load_board_abf(n)
        print("anim", n, "skater frames", sa["nframes"], "board frames", len(ba), "ratio %.2f" % (len(ba) / sa["nframes"]))
        for use_off, inv in itertools.product((False, True), (True, False)):
            lows, dx = [], []
            for f in range(0, sa["nframes"], 8):
                rp, qs, _ = sa["frames"][f]
                P = mbf.pose(sk["nodes"], off, qs, tuple(c / 4096 for c in rp))
                bq, _, bp = ba[min(len(ba) - 1, 2 * f)]
                R = np.array(mbf.qmat((-bq[0], -bq[1], -bq[2], bq[3]) if inv else bq))
                t = np.array(bp) + (R @ boff if use_off else 0)
                W = bverts @ R.T + t
                lows.append(W[:, 2].min())
                feet = (np.array(P[4][1]) + np.array(P[8][1])) / 2
                c = W.mean(0)
                dx.append(np.linalg.norm((feet - c)[:2]))
            print("   offset=%d inverse=%d  board lowest z %.4f..%.4f  |feet-board centre| xy mean %.4f" % (use_off, inv, min(lows), max(lows), np.mean(dx)))

if __name__ == "__main__":
    main()
