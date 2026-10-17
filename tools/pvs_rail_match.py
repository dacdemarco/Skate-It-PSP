#!/usr/bin/env python3
"""Match the grind splines of a level's track1.pvs (the original game's grindable lines, col<L>_grind_<NNN>) against the rails and ledges in a converted .lvl.
usage: pvs_rail_match.py <Level> <file.lvl> [-v]
Prints, per kind (rail / ledge): how many of ours lie on a PVS spline, and how many PVS splines have one of ours on them (distance of the sampled points <= tol)."""
import sys, os, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign_pvs as P

TOL = 0.6


def load_lvl_rails(path):
    b = open(path, "rb").read()
    h = struct.unpack_from("<4sI5I5I6f3f3II", b, 0)
    nrails, off_rails = h[21], h[23]
    nrailpts = h[22]
    tab = [struct.unpack_from("<II", b, off_rails + 8 * i) for i in range(nrails)]
    pts = np.frombuffer(b, "<f4", nrailpts * 3, off_rails + 8 * nrails).reshape(-1, 3)
    out = []
    for first, word in tab:
        n = word & 0xFFFF
        out.append(dict(kind=(word >> 16) & 0xFF, mat=(word >> 24) & 3, pts=pts[first:first + n].astype(float)))
    return out


def resample(p, step=0.5):
    out = [p[0]]
    for a, b in zip(p[:-1], p[1:]):
        n = max(1, int(np.linalg.norm(b - a) / step))
        for k in range(1, n + 1):
            out.append(a + (b - a) * k / n)
    return np.array(out)


def dist_to_poly(pts, poly):
    """distance of each point in pts (n,3) to the polyline poly (m,3)"""
    best = np.full(len(pts), 1e9)
    for a, b in zip(poly[:-1], poly[1:]):
        ab = b - a
        t = np.clip(((pts - a) @ ab) / max(ab @ ab, 1e-9), 0, 1)
        d = np.linalg.norm(pts - (a + t[:, None] * ab), axis=1)
        best = np.minimum(best, d)
    return best


def main(level, lvl, verbose):
    spl = [(n, np.array(p, float)) for n, p in P.parse(P.level_pvs(level))["splines"] if "grind" in n]
    ours = load_lvl_rails(lvl)
    sp_s = [resample(p) if len(p) > 1 else p for n, p in spl]
    o_s = [resample(r["pts"]) for r in ours]
    # ours -> nearest spline (fraction of our sampled points within TOL of that spline)
    own_match = []
    for r, ps in zip(ours, o_s):
        best, bi = 0.0, -1
        for i, sp in enumerate(sp_s):
            f = float((dist_to_poly(ps, sp) <= TOL).mean())
            if f > best: best, bi = f, i
        own_match.append((best, bi))
    # spline -> best of ours
    sp_match = []
    for i, sp in enumerate(sp_s):
        best, bj = 0.0, -1
        for j, ps in enumerate(o_s):
            f = float((dist_to_poly(sp, ps) <= TOL).mean())
            if f > best: best, bj = f, j
        sp_match.append((best, bj))
    for kind, nm in ((0, "rails"), (1, "ledges")):
        idx = [j for j, r in enumerate(ours) if r["kind"] == kind]
        on = sum(1 for j in idx if own_match[j][0] >= 0.8)
        part = sum(1 for j in idx if 0.2 <= own_match[j][0] < 0.8)
        print("%s %-6s: %3d ours | on a PVS spline %3d | partly %3d | on none %3d" % (level, nm, len(idx), on, part, len(idx) - on - part))
    full = sum(1 for f, j in sp_match if f >= 0.8); part = sum(1 for f, j in sp_match if 0.2 <= f < 0.8)
    print("%s splines: %3d | covered by one of ours %3d | partly %3d | not covered %3d" % (level, len(spl), full, part, len(spl) - full - part))
    if verbose:
        for i, (f, j) in enumerate(sp_match):
            if f < 0.8:
                p = spl[i][1]
                print("   uncovered %-22s npts %2d len %5.1f  at (%.1f,%.1f,%.1f)  best %.2f" % (spl[i][0], len(p), np.linalg.norm(np.diff(p, axis=0), axis=1).sum(), *p.mean(0), f))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], "-v" in sys.argv)
