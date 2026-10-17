#!/usr/bin/env python3
"""Collision and render height profile along a line. usage: profile_line.py <level.lvl> x0 z0 x1 z1 [step] [ymax]
Prints, every `step` metres, the highest collision floor at or below ymax (|n.y|>=0.5, like src/coll.c), the highest visible up-facing
render surface, and the rise since the previous sample (so stair risers / steep ramps stand out)."""
import sys, struct
import numpy as np

b = open(sys.argv[1], "rb").read()
x0, z0, x1, z1 = (float(v) for v in sys.argv[2:6])
step = float(sys.argv[6]) if len(sys.argv) > 6 else 0.5
ymax = float(sys.argv[7]) if len(sys.argv) > 7 else 1e9
h = struct.unpack_from("<4sI5I5I6f3f", b, 0)
ntex, nbatch, nverts, ncv, ncp = h[2:7]
off_tex, off_batch, off_verts, off_cv, off_cp = h[7:12]
V = np.frombuffer(b, dtype=np.dtype([("u", "<f4"), ("v", "<f4"), ("c", "<u4"), ("p", "<f4", 3)]), count=nverts, offset=off_verts)
R = V["p"].reshape(-1, 3, 3).astype(float)
CV = np.frombuffer(b, "<f4", ncv * 3, off_cv).reshape(-1, 3).astype(float)
polys = [struct.unpack_from("<4HI", b, off_cp + 12 * i) for i in range(ncp)]
C = []
for p in polys:
    C.append((CV[p[0]], CV[p[1]], CV[p[2]]))
    if p[3] != 0xFFFF:
        C.append((CV[p[0]], CV[p[2]], CV[p[3]]))
C = np.array(C)


def floors(T, x, z, need_up=False):
    out = []
    m = np.where((T[:, :, 0].min(1) <= x) & (T[:, :, 0].max(1) >= x) & (T[:, :, 2].min(1) <= z) & (T[:, :, 2].max(1) >= z))[0]
    for k in m:
        a, bq, c = T[k]
        n = np.cross(bq - a, c - a); ln = np.linalg.norm(n)
        if ln < 1e-12 or abs(n[1]) / ln < (0.5 if not need_up else 0.5) or (need_up and n[1] < 0):
            continue
        d = (bq[2] - c[2]) * (a[0] - c[0]) + (c[0] - bq[0]) * (a[2] - c[2])
        if abs(d) < 1e-12:
            continue
        l1 = ((bq[2] - c[2]) * (x - c[0]) + (c[0] - bq[0]) * (z - c[2])) / d
        l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
        if l1 >= -1e-4 and l2 >= -1e-4 and 1 - l1 - l2 >= -1e-4:
            out.append(l1 * a[1] + l2 * bq[1] + (1 - l1 - l2) * c[1])
    return sorted(out)


L = np.hypot(x1 - x0, z1 - z0)
n = int(L / step) + 1
prev = None
print("  dist    collision floor (highest <= ymax)   rise    | visible up-facing surface (highest <= ymax)")
for i in range(n + 1):
    t = min(i * step, L) / L
    x, z = x0 + (x1 - x0) * t, z0 + (z1 - z0) * t
    cf = [v for v in floors(C, x, z) if v <= ymax]
    rf = [v for v in floors(R, x, z, True) if v <= ymax]
    ch = cf[-1] if cf else None
    rh = rf[-1] if rf else None
    rise = (ch - prev) if (ch is not None and prev is not None) else None
    print("  %5.1f   %-34s %-8s | %s" % (min(i * step, L), "%.2f" % ch if ch is not None else "-", "%+.2f" % rise if rise is not None else "", "%.2f" % rh if rh is not None else "-"))
    prev = ch if ch is not None else prev
