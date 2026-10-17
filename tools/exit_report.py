#!/usr/bin/env python3
"""Summarise tests/exit_probe.c output. usage: exit_report.py exits.tsv [x0 x1 z0 z1]   (optional box: only rides STARTING inside it - starts right on the map edge are artifacts)
Where (last grounded position, 8 m cells) do riders leave the world into nothing?"""
import sys, collections
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1])][1:]
box = [float(v) for v in sys.argv[2:6]] if len(sys.argv) >= 6 else None
if box:
    rows = [r for r in rows if box[0] <= float(r[0]) <= box[1] and box[2] <= float(r[2]) <= box[3]]
E = [tuple(float(v) for v in r[4:7]) for r in rows]
print(len(E), "exits")
if E:
    c = collections.Counter((int(e[0] // 8) * 8, int(e[2] // 8) * 8) for e in E)
    print("last grounded position, 8 m cells (x z : exits):")
    for k, v in c.most_common(25):
        print("  x %5d z %5d : %d" % (k[0], k[1], v))
    xs = [e[0] for e in E]; zs = [e[2] for e in E]; ys = [e[1] for e in E]
    print("x %.0f..%.0f  z %.0f..%.0f  y %.1f..%.1f   distinct cells: %d" % (min(xs), max(xs), min(zs), max(zs), min(ys), max(ys), len(c)))
