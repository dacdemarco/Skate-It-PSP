#!/usr/bin/env python3
"""List batches whose texture id is not in the level's own texture list.
usage: extras.py <Level>"""
import sys, os, glob, collections, statistics
sys.path.insert(0, os.path.dirname(__file__))
import oct as octm

def run(name):
    lvl = "extracted/unpacked/DATA/LOCATIONS/%s/%s/" % (name, name)
    nd = len(glob.glob(lvl + "*.dst")); n = len(octm.load_cmn(lvl + "verts.cmn"))
    by = collections.defaultdict(list)
    for f in sorted(glob.glob(lvl + "*.oct")):
        b = open(f, "rb").read(); S = octm.scale(b); O = octm.origin(b)
        for lf in octm.leaves(b, n):
            for tid, tris in octm.batches(b, lf):
                if tid < nd or not tris: continue
                pts = [tuple(O[a] + v[0][a] * S for a in range(3)) for t in tris for v in t]
                ext = [max(p[k] for p in pts) - min(p[k] for p in pts) for k in range(3)]
                uv = [v[1] for t in tris for v in t]
                du = max(u for u, _ in uv) - min(u for u, _ in uv); dv = max(v for _, v in uv) - min(v for _, v in uv)
                by[tid].append((os.path.basename(f), len(tris), ext, (du, dv), (min(u for u, _ in uv), min(v for _, v in uv))))
    return nd, by

if __name__ == "__main__":
    nd, by = run(sys.argv[1])
    print("level textures:", nd)
    for tid in sorted(by, key=lambda t: (t == 0xFFFF, t)):
        L = by[tid]
        files = collections.Counter(x[0] for x in L)
        print("id %5d: %3d batches in %s" % (tid, len(L), dict(list(files.items())[:3])))
        for x in L[:3]:
            print("      tris %2d ext %s uv-extent (%.0f,%.0f) uv-min (%.0f,%.0f)" % (x[1], [round(e, 1) for e in x[2]], x[3][0], x[3][1], x[4][0], x[4][1]))
