#!/usr/bin/env python3
"""Cross-section of the collision polygons. usage: coll_section.py <level.lvl> x=<value> | z=<value> [lo hi]
Cuts every collision triangle with the plane x=value (or z=value) and prints the resulting segments sorted along the other horizontal axis:
(coordinate, y) -> (coordinate, y), |normal.y| class (F floor >=0.5, R rideable slope >=0.15, W wall) and flags. lo/hi limit the range along the axis."""
import sys, struct
import numpy as np
b = open(sys.argv[1], "rb").read()
axis, val = sys.argv[2].split("="); val = float(val)
ax = 0 if axis == "x" else 2; other = 2 if ax == 0 else 0
lo = float(sys.argv[3]) if len(sys.argv) > 3 else -1e9
hi = float(sys.argv[4]) if len(sys.argv) > 4 else 1e9
h = struct.unpack_from("<4sI5I5I6f3f", b, 0)
ncv, ncp, off_cv, off_cp = h[5], h[6], h[10], h[11]
CV = np.frombuffer(b, "<f4", ncv * 3, off_cv).reshape(-1, 3).astype(float)
segs = []
for i in range(ncp):
    p = struct.unpack_from("<4HI", b, off_cp + 12 * i)
    tris = [(p[0], p[1], p[2])] + ([(p[0], p[2], p[3])] if p[3] != 0xFFFF else [])
    for t in tris:
        P = CV[list(t)]
        n = np.cross(P[1] - P[0], P[2] - P[0]); l = np.linalg.norm(n)
        if l < 1e-9:
            continue
        ny = abs(n[1] / l)
        pts = []
        for a, c in ((0, 1), (1, 2), (2, 0)):
            da, dc = P[a][ax] - val, P[c][ax] - val
            if da == 0 and dc == 0:
                continue
            if (da <= 0 <= dc) or (dc <= 0 <= da):
                tt = da / (da - dc) if da != dc else 0
                q = P[a] + (P[c] - P[a]) * tt
                pts.append((q[other], q[1]))
        if len(pts) >= 2:
            pts = sorted(set((round(x, 2), round(y, 2)) for x, y in pts))
            if len(pts) >= 2 and not (pts[-1][0] < lo or pts[0][0] > hi):
                segs.append((pts[0], pts[-1], ny, p[4], i))
segs.sort(key=lambda s: (s[0][0], s[0][1]))
for a, c, ny, fl, i in segs:
    cls = "F" if ny >= 0.5 else ("R" if ny >= 0.15 else "W")
    print("%s=%7.2f -> %7.2f  y %6.2f -> %6.2f   %s ny %.2f flags %5d poly %d" % ("z" if other == 2 else "x", a[0], c[0], a[1], c[1], cls, ny, fl, i))
