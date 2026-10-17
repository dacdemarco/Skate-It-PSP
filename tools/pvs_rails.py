#!/usr/bin/env python3
"""The original game's grindable lines, from a level's track1.pvs (col<L>_grind_<NNN> splines, found by tools/campaign_pvs.py), in the form the converter / runtime want:
polylines that are either RAILS (a tube / box you slide over the top of: nothing solid beside the line) or LEDGES (the edge of a block: the top surface is on ONE side, and the line
is ordered so that the top is on the LEFT of the direction of travel, left = up x dir).  The kind and the side come from probing the level's collision floor 0.3 m to either
side of the line (the PVS data only has the line).  Used by convert_level.py; `python pvs_rails.py <Level> <file.lvl>` prints the classification.

classify_spline(points, floor_y) -> (kind, points, info)   floor_y(x, z, ymax) -> height of the highest walkable collision surface at or under ymax (or None)
"""
import sys, os, struct
import numpy as np

SKIP_FLAGS = 256 | 512 | 0x8000 | 0x4000
PROBE = 0.3            # metres to either side of the line
ON_TOP = 0.12          # a surface this close to the line's height (below it, or a little above) counts as "the top is here"
STEP = 0.4             # spacing of the probes along the line


class Floor:
    """walkable collision triangles with a bounding-box prefilter (|n.y| >= 0.5; rail tubes, poles and the converter's synthetic walls are not floor)"""

    def __init__(self, cverts, polys):
        cv = np.array(cverts, float)
        A, B, C = [], [], []
        for p in polys:
            if p[4] & SKIP_FLAGS:
                continue
            ids = [i for i in p[:4] if i != 0xFFFF]
            if len(ids) < 3:
                continue
            tris = [(ids[0], ids[1], ids[2])] + ([(ids[0], ids[2], ids[3])] if len(ids) == 4 else [])
            for a, b, c in tris:
                n = np.cross(cv[b] - cv[a], cv[c] - cv[a])
                ln = np.linalg.norm(n)
                if ln < 1e-9 or abs(n[1]) / ln < 0.5:
                    continue
                A.append(cv[a]); B.append(cv[b]); C.append(cv[c])
        self.A, self.B, self.C = np.array(A), np.array(B), np.array(C)
        P = np.stack([self.A, self.B, self.C])
        self.lo = P.min(axis=0)[:, [0, 2]]; self.hi = P.max(axis=0)[:, [0, 2]]
        self.d0 = self.B - self.A; self.d1 = self.C - self.A
        self.den = self.d0[:, 0] * self.d1[:, 2] - self.d1[:, 0] * self.d0[:, 2]

    def __call__(self, x, z, ymax):
        m = (self.lo[:, 0] <= x) & (self.hi[:, 0] >= x) & (self.lo[:, 1] <= z) & (self.hi[:, 1] >= z) & (np.abs(self.den) > 1e-9)
        if not m.any():
            return None
        i = np.nonzero(m)[0]
        px = x - self.A[i, 0]; pz = z - self.A[i, 2]
        den = self.den[i]
        u = (px * self.d1[i, 2] - self.d1[i, 0] * pz) / den
        v = (self.d0[i, 0] * pz - px * self.d0[i, 2]) / den
        ok = (u >= -1e-4) & (v >= -1e-4) & (u + v <= 1 + 1e-4)
        if not ok.any():
            return None
        i, u, v = i[ok], u[ok], v[ok]
        y = self.A[i, 1] + u * self.d0[i, 1] + v * self.d1[i, 1]
        y = y[y <= ymax]
        return float(y.max()) if len(y) else None


def resample(p, step=STEP):
    p = np.asarray(p, float)
    out, tans = [p[0]], []
    for a, b in zip(p[:-1], p[1:]):
        L = float(np.linalg.norm(b - a))
        n = max(1, int(round(L / step)))
        t = (b - a) / max(L, 1e-9)
        for k in range(1, n + 1):
            out.append(a + (b - a) * k / n)
        tans += [t] * n
    tans.append(tans[-1])
    return np.array(out), np.array(tans)


def classify_spline(pts, floor_y):
    """returns (kind, pts, info): kind 0 rail / 1 ledge (pts reversed when needed so that the top is on the left), info = counts of probe votes"""
    pts = np.asarray(pts, float)
    if len(pts) < 2:
        return 0, pts, dict(left=0, right=0, both=0, none=0)
    s, tg = resample(pts)
    votes = dict(left=0, right=0, both=0, none=0)
    for p, t in zip(s, tg):
        d = np.array([t[0], 0.0, t[2]]); n = np.linalg.norm(d)
        if n < 1e-6:
            continue
        d /= n
        left = np.array([d[2], 0.0, -d[0]])                         # up x dir
        ys = []
        for sgn in (1, -1):
            q = p + sgn * PROBE * left
            h = floor_y(q[0], q[2], p[1] + ON_TOP)
            ys.append(h is not None and h >= p[1] - ON_TOP)
        votes["both" if ys[0] and ys[1] else "left" if ys[0] else "right" if ys[1] else "none"] += 1
    if votes["left"] + votes["right"] > votes["none"] + votes["both"] and votes["right"] > votes["left"]:
        return 1, pts[::-1].copy(), votes
    if votes["left"] + votes["right"] > votes["none"] + votes["both"]:
        return 1, pts, votes
    return 0, pts, votes


def load_lvl_collision(path):
    b = open(path, "rb").read()
    h = struct.unpack_from("<4sI5I5I6f3f3II", b, 0)
    ncv, ncp, off_collv, off_collp = h[5], h[6], h[10], h[11]
    cverts = [struct.unpack_from("<3f", b, off_collv + 12 * i) for i in range(ncv)]
    polys = [struct.unpack_from("<4HI", b, off_collp + 12 * i) for i in range(ncp)]
    return cverts, polys


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import campaign_pvs as P
    level, lvl = sys.argv[1], sys.argv[2]
    fl = Floor(*load_lvl_collision(lvl))
    kinds = [0, 0]
    for n, pts in P.parse(P.level_pvs(level))["splines"]:
        if "grind" not in n:
            continue
        k, q, v = classify_spline(pts, fl)
        kinds[k] += 1
        if "-v" in sys.argv:
            print("%-18s npts %2d -> %-5s %s" % (n, len(pts), "LEDGE" if k else "rail", v))
    print("%s: %d rails, %d ledges" % (level, kinds[0], kinds[1]))
