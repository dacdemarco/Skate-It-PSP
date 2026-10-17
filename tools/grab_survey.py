#!/usr/bin/env python3
"""Find the grab poses of the pose library: clips where a HAND bone is at the board while the board is held away from the feet.
Leaf bones of the skeleton (4, 8, 15, 18, 21): in the riding pose (clip 0) the two nearest to the board are the feet, the highest is the head, the other two are the hands.
For every clip and frame: distance of each hand to the board's box (in the board's own frame); a grab = a hand within GRAB_DIST of the board while the feet are NOT both on the deck.
usage: grab_survey.py"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf, clip_survey as cs

GRAB_DIST = 0.016              # model units: 1 unit = 8.6 m, so 0.016 = 14 cm
LEAVES = [4, 8, 15, 18, 21]


def box_dist(p, lo, hi):
    d = np.maximum(np.maximum(lo - p, p - hi), 0.0)
    return float(np.linalg.norm(d))


def main():
    sk = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
    off = [tuple(c / 4096 for c in o) for o in mbf.load_bof(cs.ARC_S + "/animdb.BOF")]
    bd = mbf.load_mbf_multi("%s/MODELS/board_generic_lod0.mbf" % mbf.SK)
    bv = np.array([v[0] for sec in bd["sections"] for t in sec[1] for v in t], float)
    lo, hi = bv.min(axis=0), bv.max(axis=0)
    print("board local extents", lo, hi)

    def frame_data(n, f):
        a = mbf.load_abf("%s/%d.abf" % (cs.ARC_S, n)); f = min(f, a["nframes"] - 1)
        rp, qs, _ = a["frames"][f]
        P = mbf.pose(sk["nodes"], off, qs, tuple(c / 4096 for c in rp))
        bf = cs.board_frames(n); bq, bp = bf[min(f, len(bf) - 1)]
        R = np.array(mbf.qmat((-bq[0], -bq[1], -bq[2], bq[3])))           # board local -> skater model
        leaf = {i: np.array(P[i][1]) for i in LEAVES}
        # board-local positions of the leaves
        loc = {i: R.T @ (leaf[i] - np.array(bp)) for i in LEAVES}
        return loc, leaf, R, np.array(bp)
    loc0, leaf0, _, _ = frame_data(0, 3)
    d0 = {i: box_dist(loc0[i], lo, hi) for i in LEAVES}
    order = sorted(LEAVES, key=lambda i: d0[i])
    head = max(LEAVES, key=lambda i: leaf0[i][1] if abs(leaf0[i][1]) > 0 else 0)
    # the highest of the leaves in the model's up direction is the head: model y is up? use the largest distance from the board instead
    far = sorted(LEAVES, key=lambda i: -np.linalg.norm(loc0[i] - 0.5 * (lo + hi)))
    print("riding pose: leaf distances to the board box", {i: round(d0[i], 2) for i in LEAVES}, "-> nearest two = feet:", order[:2], "farthest = head:", far[0])
    feet = order[:2]
    hands = [i for i in LEAVES if i not in feet and i != far[0]]
    print("hands:", hands)
    rows = []
    for n in range(376):
        nf = len(mbf.load_abf("%s/%d.abf" % (cs.ARC_S, n))["frames"])
        best = None
        for f in range(nf):
            loc, leaf, R, bp = frame_data(n, f)
            hd = min(box_dist(loc[h], lo, hi) for h in hands)
            fd = sum(box_dist(loc[i], lo, hi) for i in feet)
            hh = min(hands, key=lambda h: box_dist(loc[h], lo, hi))
            if best is None or hd < best[0]:
                best = (hd, fd, hh, f)
        rows.append((n, nf) + best)
        if best[0] < GRAB_DIST * 2.2 and best[1] > 0.0:
            print("%3d frames %2d  hand %d is %.3f from the board at frame %d, feet total %.2f from it" % (n, nf, best[2], best[0], best[3], best[1]))


if __name__ == "__main__":
    main()
