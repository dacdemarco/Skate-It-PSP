#!/usr/bin/env python3
"""List individual rides that got worse. usage: ride_caught.py A.tsv B.tsv x0 x1 z0 z1 [n]
A ride is listed when it did not fall in A or B but travelled >= 3 m less in B."""
import sys


def load(p):
    d = {}
    for l in list(open(p))[1:]:
        r = l.rstrip("\n").split("\t")
        d[(float(r[0]), float(r[1]), int(r[2]))] = (float(r[3]), int(r[4]), float(r[5]))
    return d


A, B = load(sys.argv[1]), load(sys.argv[2])
x0, x1, z0, z1 = (float(v) for v in sys.argv[3:7])
n = int(sys.argv[7]) if len(sys.argv) > 7 else 25
shown = 0
for k in sorted(A):
    if k not in B or not (x0 <= k[0] <= x1 and z0 <= k[1] <= z1):
        continue
    if A[k][1] or B[k][1] or A[k][0] - B[k][0] < 3.0:
        continue
    print("start (%6.1f, %6.1f) dir %d (heading %3d deg): A travelled %5.2f m  B %5.2f m   final speed A %.2f B %.2f" % (
        k[0], k[1], k[2], k[2] * 45, A[k][0], B[k][0], A[k][2], B[k][2]))
    shown += 1
    if shown >= n:
        break
