#!/usr/bin/env python3
"""Evidence for the skeleton model (see docs/TECHNICAL.md). Runs checks that do not depend on how the model was found:
  1. seam test - parent/child mesh edges must join at the joint: compares quaternion order x plain/inverse x composition
  2. feet on the ground in every animation (never used in the fit)
  3. knees bend towards one heading within each animation
usage: skel_verify.py"""
import sys, os, itertools, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, mbf

m = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
off = [np.array(o, float) / 4096 for o in mbf.load_bof()]
par = mbf.parents(m["nodes"])
anims = [mbf.load_abf("%s/%d.ABF" % (mbf.ANIM, n)) for n in range(23)]


def rot(q, order, inverse):
    x, y, z, w = q if order == "xyzw" else (q[1], q[2], q[3], q[0])
    R = np.array(mbf.qmat((x, y, z, w)))
    return R.T if inverse else R


def seam_edges():
    edges = {}
    for t in m["tris"]:
        for i in range(3):
            u, v = t[i], t[(i + 1) % 3]
            if u[3] != v[3]:
                if par[v[3]] == u[3]: a, b = u, v
                elif par[u[3]] == v[3]: a, b = v, u
                else: continue
                edges.setdefault(b[3], []).append((np.array(a[0]), np.array(b[0])))
    return edges


def seam_test():
    edges = seam_edges()
    frames = [qs for a in anims for (_, qs, _) in a["frames"][::7]]
    print("1. seam error (mean squared parent/child edge length after posing; lower = better), %d frames" % len(frames))
    for order, inv, comp in itertools.product(("xyzw", "wxyz"), (False, True), ("T.R", "R.T")):
        tot = []
        for b, E in edges.items():
            P = np.array([e[0] for e in E]); C = np.array([e[1] for e in E])
            for qs in frames:
                R = rot(qs[b], order, inv)
                X = C @ R.T + off[b] if comp == "T.R" else (C + off[b]) @ R.T
                tot.append(((X - P) ** 2).sum(-1).mean())
        print("   %s %-7s %s  %.5f" % (order, "inverse" if inv else "plain", comp, np.mean(tot)))


def pose_checks():
    print("2/3. per animation: lowest mesh point z (model units, want 0) and knee-forward heading (deg, mean/spread)")
    for n, a in enumerate(anims):
        lows, heads = [], []
        for f, (rp, qs, _) in enumerate(a["frames"]):
            P = mbf.pose(m["nodes"], [tuple(o) for o in off], qs, tuple(c / 4096 for c in rp))
            if f % 6 == 0:
                lows.append(min((np.array(P[v[3]][0]) @ np.array(v[0]) + np.array(P[v[3]][1]))[2] for t in m["tris"] for v in t))
            v = np.zeros(2)
            for hip, knee, ank in ((1, 2, 3), (5, 6, 7)):
                k = np.array(P[knee][1]) - (np.array(P[hip][1]) + np.array(P[ank][1])) / 2
                v += k[:2]
            heads.append(math.atan2(v[1], v[0]))
        z = np.exp(1j * np.array(heads)).mean()
        print("   anim %2d: lowest z %+.4f..%+.4f   knee heading %4.0f / %2.0f" % (
            n, min(lows), max(lows), math.degrees(np.angle(z)), math.degrees(math.sqrt(-2 * math.log(abs(z))))))


if __name__ == "__main__":
    seam_test()
    pose_checks()
