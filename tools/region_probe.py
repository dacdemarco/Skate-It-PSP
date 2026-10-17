#!/usr/bin/env python3
"""What geometry is in a box? usage: region_probe.py <Level> x0 x1 z0 z1 [layer-prefix]
Lists render triangles per layer/texture with normal.y statistics and height range, and the collision polygons in the box."""
import sys, os, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import oct as octm, pvs_tex

level = sys.argv[1]
x0, x1, z0, z1 = (float(v) for v in sys.argv[2:6])
only = sys.argv[6] if len(sys.argv) > 6 else None
lvl = "extracted/unpacked/DATA/LOCATIONS/%s/%s/" % (level, level)
table = pvs_tex.resolve_textures(level)
nverts = len(octm.load_cmn(lvl + "verts.cmn"))
cverts = octm.load_cmn(lvl + "verts.cmn")
rows = collections.defaultdict(list)
coll = []
for f in sorted(glob.glob(lvl + "*.oct")):
    layer = os.path.basename(f)[0]
    if only and layer != only:
        continue
    b = open(f, "rb").read(); S, O = octm.scale(b), octm.origin(b)
    for lf in octm.leaves(b, nverts):
        for p in lf["polys"]:
            idx = [i for i in p[:4] if i != 0xFFFF]
            pts = np.array([cverts[i] for i in idx])
            if x0 <= pts[:, 0].mean() <= x1 and z0 <= pts[:, 2].mean() <= z1:
                coll.append((layer, p[4], pts))
        for tid, tris in octm.batches(b, lf):
            name = os.path.basename(table[tid]) if tid < len(table) and table[tid] else "(none)"
            for tri in tris:
                P = np.array([[O[a] + v[0][a] * S for a in range(3)] for v in tri])
                c = P.mean(0)
                if x0 <= c[0] <= x1 and z0 <= c[2] <= z1:
                    rows[(layer, name)].append(P)
print("render triangles with centroid in the box:")
for (layer, name), T in sorted(rows.items()):
    A = np.array(T)
    N = np.cross(A[:, 1] - A[:, 0], A[:, 2] - A[:, 0]); L = np.linalg.norm(N, axis=1); ok = L > 1e-9
    ny = np.abs(N[ok, 1] / L[ok])
    print("  layer %s %-34s %4d tris  y %7.2f..%7.2f   |n.y| flat(>0.9) %3d  slope(0.3-0.9) %3d  steep(<0.3) %3d" % (
        layer, name, len(T), A[:, :, 1].min(), A[:, :, 1].max(), (ny > 0.9).sum(), ((ny >= 0.3) & (ny <= 0.9)).sum(), (ny < 0.3).sum()))
print("collision polygons with centroid in the box (from the same leaves):")
byl = collections.defaultdict(list)
for layer, fl, pts in coll:
    n = np.cross(pts[1] - pts[0], pts[2] - pts[0]); ln = np.linalg.norm(n)
    byl[layer].append((abs(n[1] / ln) if ln > 1e-9 else 0.0, fl, pts[:, 1].min(), pts[:, 1].max()))
for layer, v in sorted(byl.items()):
    ny = np.array([a[0] for a in v])
    print("  layer %s: %d polys, y %.2f..%.2f, |n.y| flat(>0.9) %d slope(0.5-0.9) %d slope(0.3-0.5) %d steep(<0.3) %d; flags %s" % (
        layer, len(v), min(a[2] for a in v), max(a[3] for a in v), (ny > 0.9).sum(), ((ny >= 0.5) & (ny <= 0.9)).sum(),
        ((ny >= 0.3) & (ny < 0.5)).sum(), (ny < 0.3).sum(), dict(collections.Counter(a[1] for a in v))))
