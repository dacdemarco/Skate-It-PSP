#!/usr/bin/env python3
"""Group the flag-256 (and optionally 512) collision polygons of a level into connected objects and measure them as rails.
usage: rail_survey.py <Level> [flag=256] [layer-prefix=1]
A rail candidate = connected component (shared collision vertices). Reports per component: polygons, vertex count, principal axis (direction cosine y = slope),
length along the axis, cross-section size (the two smaller extents), height range, centre. Also the texture of the nearest render triangles."""
import sys, os, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import oct as octm, pvs_tex

level = sys.argv[1]
flag = int(sys.argv[2]) if len(sys.argv) > 2 else 256
only = sys.argv[3] if len(sys.argv) > 3 else "1"
lvl = "extracted/unpacked/DATA/LOCATIONS/%s/%s/" % (level, level)
table = pvs_tex.resolve_textures(level)
cverts = np.array(octm.load_cmn(lvl + "verts.cmn"), float)
polys = set(); cent = []; names = []
for f in sorted(glob.glob(lvl + "*.oct")):
    if os.path.basename(f)[0] != only:
        continue
    b = open(f, "rb").read(); S, O = octm.scale(b), octm.origin(b)
    for lf in octm.leaves(b, len(cverts)):
        for p in lf["polys"]:
            if p[4] == flag:
                polys.add((p[0], p[1], p[2], p[3]))
        for tid, tris in octm.batches(b, lf):
            nm = os.path.basename(table[tid]) if tid < len(table) and table[tid] else "(none)"
            for tri in tris:
                P = np.array([[O[a] + v[0][a] * S for a in range(3)] for v in tri])
                cent.append(P.mean(0)); names.append(nm)
cent = np.array(cent)
parent = {}
def find(x):
    while parent.setdefault(x, x) != x:
        parent[x] = parent[parent[x]]; x = parent[x]
    return x
def union(a, b): parent[find(a)] = find(b)
for p in polys:
    ids = [i for i in p if i != 0xFFFF]
    for i in ids[1:]:
        union(ids[0], i)
comp = collections.defaultdict(lambda: [set(), 0])
for p in polys:
    ids = [i for i in p if i != 0xFFFF]
    r = find(ids[0]); comp[r][0].update(ids); comp[r][1] += 1
rows = []
for r, (vs, npoly) in comp.items():
    P = cverts[sorted(vs)]
    c = P.mean(0)
    u, s, vt = np.linalg.svd(P - c, full_matrices=False)
    ax = vt[0]; proj = (P - c) @ vt.T
    ext = proj.max(0) - proj.min(0)
    d = np.linalg.norm(cent - c, axis=1); near = [names[i] for i in np.where(d < max(1.0, ext[0] / 2 + 0.5))[0]]
    tex = collections.Counter(near).most_common(2)
    rows.append((ext[0], npoly, len(vs), ax, ext, c, P[:, 1].min(), P[:, 1].max(), tex))
rows.sort(key=lambda r: -r[0])
print("flag %d: %d polygons in %d connected components" % (flag, len(polys), len(rows)))
print("length(m) polys verts  axis-slope(dy/len)  cross-section(m)   height range      centre                 texture")
for L, npoly, nv, ax, ext, c, y0, y1, tex in rows[:40]:
    print("%7.2f %6d %5d   %+.2f              %.2f x %.2f     %6.2f..%6.2f   (%7.1f %6.1f %7.1f)   %s" % (L, npoly, nv, ax[1] * (1 if ax[1] >= 0 else 1), ext[1], ext[2], y0, y1, c[0], c[1], c[2], tex))
lens = np.array([r[0] for r in rows])
print("\nlength percentiles (10/50/90/max): %.2f %.2f %.2f %.2f ; components longer than 2 m: %d, longer than 4 m: %d" % (
    np.percentile(lens, 10), np.percentile(lens, 50), np.percentile(lens, 90), lens.max(), (lens > 2).sum(), (lens > 4).sum()))
