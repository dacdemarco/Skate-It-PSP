#!/usr/bin/env python3
"""Where do textures whose name contains a keyword appear, per octree layer? usage: find_features.py <Level> <keyword> [keyword ...]"""
import sys, os, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import oct as octm, pvs_tex

level = sys.argv[1]
keys = [k.lower() for k in sys.argv[2:]]
lvl = "extracted/unpacked/DATA/LOCATIONS/%s/%s/" % (level, level)
table = pvs_tex.resolve_textures(level)
nverts = len(octm.load_cmn(lvl + "verts.cmn"))
hits = collections.defaultdict(list)       # (layer, file, texname) -> triangles (3x3 arrays)
for f in sorted(glob.glob(lvl + "*.oct")):
    layer = os.path.basename(f)[0]
    b = open(f, "rb").read(); S, O = octm.scale(b), octm.origin(b)
    for lf in octm.leaves(b, nverts):
        for tid, tris in octm.batches(b, lf):
            if tid >= len(table) or table[tid] is None:
                continue
            name = os.path.basename(table[tid]).lower()
            if not any(k in name for k in keys):
                continue
            for tri in tris:
                P = np.array([[O[a] + v[0][a] * S for a in range(3)] for v in tri])
                hits[(layer, os.path.basename(f), name)].append(P)
for (layer, f, name), T in sorted(hits.items()):
    A = np.array(T)
    N = np.cross(A[:, 1] - A[:, 0], A[:, 2] - A[:, 0]); L = np.linalg.norm(N, axis=1); ok = L > 1e-9
    ny = N[ok, 1] / L[ok]
    print("layer %s %-26s %-28s %4d tris  x %7.1f..%7.1f  y %6.1f..%6.1f  z %7.1f..%7.1f   n.y: min %.2f median %.2f max %.2f  area %.0f m2" % (
        layer, f, name, len(T), A[:, :, 0].min(), A[:, :, 0].max(), A[:, :, 1].min(), A[:, :, 1].max(), A[:, :, 2].min(), A[:, :, 2].max(),
        ny.min(), np.median(ny), ny.max(), (L[ok] / 2).sum()))
