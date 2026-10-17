#!/usr/bin/env python3
"""Rides that fall in B but not in A. usage: ride_newfalls.py A.tsv B.tsv x0 x1 z0 z1 [n]  (one example per 8 m cell)"""
import sys


def load(p):
    d = {}
    for l in list(open(p))[1:]:
        r = l.rstrip("\n").split("\t")
        d[(float(r[0]), float(r[1]), int(r[2]))] = (float(r[3]), int(r[4]), float(r[5]))
    return d


A, B = load(sys.argv[1]), load(sys.argv[2])
x0, x1, z0, z1 = (float(v) for v in sys.argv[3:7])
n = int(sys.argv[7]) if len(sys.argv) > 7 else 12
seen = set(); shown = 0
for k in sorted(B):
    if k not in A or not (x0 <= k[0] <= x1 and z0 <= k[1] <= z1) or not B[k][1] or A[k][1]:
        continue
    cell = (int(k[0] // 8), int(k[1] // 8))
    if cell in seen:
        continue
    seen.add(cell)
    print("%.4f %.4f %d   (A travelled %.1f m, B %.1f m)" % (k[0], k[1], k[2] * 45, A[k][0], B[k][0]))
    shown += 1
    if shown >= n:
        break
