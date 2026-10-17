#!/usr/bin/env python3
"""Where do rides fall into nothing? usage: ride_falls.py rides.tsv [x0 x1 z0 z1] [n]
Lists 8 m cells with falling rides and then up to n example rides (start, direction)."""
import sys, collections
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1])][1:]
box = [float(v) for v in sys.argv[2:6]] if len(sys.argv) >= 6 else None
n = int(sys.argv[6]) if len(sys.argv) > 6 else 12
F = [(float(r[0]), float(r[1]), int(r[2]), float(r[3])) for r in rows if int(r[4]) and (box is None or (box[0] <= float(r[0]) <= box[1] and box[2] <= float(r[1]) <= box[3]))]
print(len(F), "falling rides")
c = collections.Counter((int(f[0] // 8) * 8, int(f[1] // 8) * 8) for f in F)
print("8 m cells (x z : n):", ", ".join("(%d %d):%d" % (a, b, k) for (a, b), k in c.most_common(20)))
seen = set()
for f in F:
    key = (int(f[0] // 8), int(f[1] // 8))
    if key in seen:
        continue
    seen.add(key)
    print("  example: start (%.4f, %.4f) dir %d (heading %d deg) travelled %.1f m before the fall ended" % (f[0], f[1], f[2], f[2] * 45, f[3]))
    if len(seen) >= n:
        break
