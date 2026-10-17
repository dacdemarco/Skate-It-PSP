#!/usr/bin/env python3
"""Compare the grind splines stored in a level's track1.pvs (col<N>_grind_<NNN>) with the rails our converter infers from collision flag 256
(tools/rails.py).  For each inferred rail, the distance from its end points / mid point to the nearest PVS spline polyline.
Usage: campaign_rail_check.py <Level>"""
import sys, os, glob
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oct as octm, rails, campaign_pvs as P

ROOT = "extracted/unpacked/DATA/LOCATIONS"


def seg_dist(p, a, b):
    ab = b - a; t = np.clip(np.dot(p - a, ab) / max(np.dot(ab, ab), 1e-9), 0, 1)
    return np.linalg.norm(p - (a + t * ab))


def main(level):
    lvl = "%s/%s/%s/" % (ROOT, level, level)
    cv = octm.load_cmn(lvl + "verts.cmn")
    cverts = [tuple(v) for v in cv]
    polys = set()
    for f in glob.glob(lvl + "*.oct"):
        b = open(f, "rb").read()
        for lf in octm.leaves(b, len(cv)):
            for p in lf["polys"]:
                polys.add((p[0], p[1], p[2], p[3], p[4]))
    rl = rails.extract_rails(cverts, sorted(polys), log=lambda *a: None)
    spl = P.parse(P.level_pvs(level))["splines"]
    spl = [(n, np.array(pts)) for n, pts in spl if "grind" in n]
    print(level, "pvs grind splines", len(spl), "inferred rails", len(rl))
    close = 0
    for r in rl:
        pts = np.array(r["pts"] if isinstance(r, dict) else r[0], float) if not isinstance(r, np.ndarray) else r
        mid = pts.mean(0)
        best = 1e9
        for n, sp in spl:
            for i in range(len(sp) - 1):
                best = min(best, seg_dist(mid, sp[i], sp[i + 1]))
        close += best < 0.6
    print("inferred rails whose centre is within 0.6 m of a PVS spline: %d / %d" % (close, len(rl)))


if __name__ == "__main__":
    main(sys.argv[1])
