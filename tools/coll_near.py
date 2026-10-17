#!/usr/bin/env python3
"""Collision polygons near a point. usage: coll_near.py <level.lvl> x y z [radius]"""
import sys, struct
import numpy as np
b = open(sys.argv[1], "rb").read()
q = np.array([float(v) for v in sys.argv[2:5]])
r = float(sys.argv[5]) if len(sys.argv) > 5 else 2.0
h = struct.unpack_from("<4sI5I5I6f3f", b, 0)
ncv, ncp = h[5], h[6]
off_cv, off_cp = h[10], h[11]
CV = np.frombuffer(b, "<f4", ncv * 3, off_cv).reshape(-1, 3).astype(float)
for i in range(ncp):
    p = struct.unpack_from("<4HI", b, off_cp + 12 * i)
    idx = [k for k in p[:4] if k != 0xFFFF]
    pts = CV[idx]
    if np.all(pts.max(0) >= q - r) and np.all(pts.min(0) <= q + r):
        n = np.cross(pts[1] - pts[0], pts[2] - pts[0]); l = np.linalg.norm(n); n = n / l if l > 1e-9 else n
        print("poly %5d flags %5d n=(%5.2f %5.2f %5.2f) verts %s" % (i, p[4], *n, " ".join("(%.1f %.2f %.1f)" % tuple(v) for v in pts)))
