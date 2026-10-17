#!/usr/bin/env python3
"""What do the collision polygon flags mean? usage: flag_survey.py <Level> [layer-prefix]
For every flag value: polygon count, orientation classes (|n.y|), size, thickness, height range, and which render textures sit within 0.5 m of the polygon centroid.
Uses the octree leaf polygon lists (layer 1 by default) and the same render triangles as the converter."""
import sys, os, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import oct as octm, pvs_tex

level = sys.argv[1]
only = sys.argv[2] if len(sys.argv) > 2 else "1"
lvl = "extracted/unpacked/DATA/LOCATIONS/%s/%s/" % (level, level)
table = pvs_tex.resolve_textures(level)
cverts = np.array(octm.load_cmn(lvl + "verts.cmn"), float)
polys = {}            # (v0..v3, flags) -> centroid, normal, area
tris_by_tex = []      # (centroid, texture name)
cent = []; names = []
for f in sorted(glob.glob(lvl + "*.oct")):
    if os.path.basename(f)[0] != only:
        continue
    b = open(f, "rb").read(); S, O = octm.scale(b), octm.origin(b)
    for lf in octm.leaves(b, len(cverts)):
        for p in lf["polys"]:
            polys[(p[0], p[1], p[2], p[3], p[4])] = 1
        for tid, tris in octm.batches(b, lf):
            nm = os.path.basename(table[tid]) if tid < len(table) and table[tid] else "(none)"
            for tri in tris:
                P = np.array([[O[a] + v[0][a] * S for a in range(3)] for v in tri])
                cent.append(P.mean(0)); names.append(nm)
cent = np.array(cent)
by = collections.defaultdict(list)
for (v0, v1, v2, v3, fl) in polys:
    idx = [i for i in (v0, v1, v2, v3) if i != 0xFFFF]
    P = cverts[idx]
    n = np.cross(P[1] - P[0], P[2] - P[0]); l = np.linalg.norm(n)
    if l < 1e-9:
        continue
    ny = abs(n[1] / l)
    # thickness = smallest bbox extent; area of the triangle fan
    area = sum(np.linalg.norm(np.cross(P[k] - P[0], P[k + 1] - P[0])) / 2 for k in range(1, len(P) - 1))
    ext = P.max(0) - P.min(0)
    by[fl].append((P.mean(0), ny, area, ext, P))
print("%d polygons in layer %s; flag value : count" % (len(polys), only), {k: len(v) for k, v in sorted(by.items())})
for fl, L in sorted(by.items()):
    ny = np.array([x[1] for x in L]); ar = np.array([x[2] for x in L]); ext = np.array([x[3] for x in L])
    ys = np.array([x[0][1] for x in L])
    print("\nflag %d (0x%X): %d polys  |n.y| floor(>=.5) %d ramp(.15-.5) %d wall(<.15) %d   area median %.2f m2 (max %.1f)  y %.1f..%.1f" % (
        fl, fl, len(L), (ny >= .5).sum(), ((ny >= .15) & (ny < .5)).sum(), (ny < .15).sum(), np.median(ar), ar.max(), ys.min(), ys.max()))
    print("   bbox extents median x %.2f y %.2f z %.2f ; thinnest dimension < 0.3 m: %d polys" % (np.median(ext[:, 0]), np.median(ext[:, 1]), np.median(ext[:, 2]), (ext.min(1) < 0.3).sum()))
    cnt = collections.Counter()
    for c, *_ in L:
        d = np.linalg.norm(cent - c, axis=1)
        k = np.where(d < 0.6)[0]
        for nm in set(names[i] for i in k):
            cnt[nm] += 1
    print("   render textures within 0.6 m of the centroid:", cnt.most_common(6))
