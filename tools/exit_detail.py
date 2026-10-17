#!/usr/bin/env python3
"""List the exits of tests/exit_probe.c output grouped by exit level. usage: exit_detail.py exits.tsv [n]"""
import sys, collections
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1])][1:]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 30
R = [(float(r[0]), float(r[1]), float(r[2]), int(r[3]), float(r[4]), float(r[5]), float(r[6])) for r in rows]
print("exit height histogram (last grounded y, 2 m bins):", sorted(collections.Counter(int(r[5] // 2) * 2 for r in R).items()))
for r in sorted(R, key=lambda r: (round(r[4] / 8), round(r[6] / 8)))[:n]:
    print("start (%6.1f %6.2f %6.1f) dir %d  left the floor at (%6.1f %6.2f %6.1f)  travelled %.1f m" % (
        r[0], r[1], r[2], r[3], r[4], r[5], r[6], ((r[4] - r[0]) ** 2 + (r[6] - r[2]) ** 2) ** 0.5))
