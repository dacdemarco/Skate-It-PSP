#!/usr/bin/env python3
"""For every level: % of decoded visual vertices (root_origin + local*scale) that coincide
(<0.05 units) with a vertex of verts.cmn. Independent check of the OCT2/GX decoding."""
import sys, glob, os, collections
sys.path.insert(0, os.path.dirname(__file__))
import oct as octm, dsm

cell = 0.25
print("%-20s %8s %8s %7s" % ("level", "leaves", "verts", "match%"))
for lvl in sorted(glob.glob("extracted/unpacked/DATA/LOCATIONS/*/*/")):
    cm = glob.glob(lvl + "verts.cmn")
    if not cm:
        continue
    name = os.path.normpath(lvl).split(os.sep)[-1]
    verts = octm.load_cmn(cm[0]); n = len(verts)
    grid = collections.defaultdict(list)
    for v in verts:
        grid[(round(v[0] / cell), round(v[1] / cell), round(v[2] / cell))].append(v)

    def near(p):
        k = (round(p[0] / cell), round(p[1] / cell), round(p[2] / cell))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for v in grid.get((k[0] + dx, k[1] + dy, k[2] + dz), ()):
                        if max(abs(v[0] - p[0]), abs(v[1] - p[1]), abs(v[2] - p[2])) < 0.05:
                            return True
        return False
    ok = tot = nl = 0
    for f in glob.glob(lvl + "*.oct"):
        b = open(f, "rb").read(); S = octm.scale(b); O = octm.origin(b)
        for lf in octm.leaves(b, n):
            nl += 1
            seen = set()
            for t in dsm.replay(lf["cmds"]):
                for v in t:
                    p = tuple(O[a] + v[0][a] * S for a in range(3))
                    key = tuple(round(c, 2) for c in p)
                    if key in seen:
                        continue
                    seen.add(key); tot += 1; ok += near(p)
    print("%-20s %8d %8d %6.1f%%" % (name, nl, tot, 100 * ok / max(tot, 1)))
