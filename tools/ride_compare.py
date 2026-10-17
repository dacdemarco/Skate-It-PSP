#!/usr/bin/env python3
"""Compare two tests/ride_probe.c outputs (same start grid). usage: ride_compare.py A.tsv B.tsv [x0 x1 z0 z1]
Reports rides that are worse in B than in A: B falls where A did not, or B travels >= 3 m less than A (stuck/caught), grouped by 8 m cell.
An optional box limits the report to the inside of the plaza (to ignore the perimeter)."""
import sys, collections


def load(p):
    d = {}
    for l in list(open(p))[1:]:
        r = l.rstrip("\n").split("\t")
        d[(float(r[0]), float(r[1]), int(r[2]))] = (float(r[3]), int(r[4]), float(r[5]))
    return d


A, B = load(sys.argv[1]), load(sys.argv[2])
box = [float(v) for v in sys.argv[3:7]] if len(sys.argv) >= 7 else None
common = [k for k in A if k in B and (box is None or (box[0] <= k[0] <= box[1] and box[2] <= k[1] <= box[3]))]
print("%d rides compared (A: %s, B: %s)" % (len(common), sys.argv[1].split("\\")[-1], sys.argv[2].split("\\")[-1]))
print("falls: A %d, B %d" % (sum(A[k][1] for k in common), sum(B[k][1] for k in common)))
newfall = [k for k in common if B[k][1] and not A[k][1]]
caught = [k for k in common if not B[k][1] and not A[k][1] and A[k][0] - B[k][0] >= 3.0]
freed = [k for k in common if A[k][1] and not B[k][1]]
print("new falls in B: %d   caught (>= 3 m shorter, no fall): %d   falls fixed in B: %d" % (len(newfall), len(caught), len(freed)))
for name, L in (("new falls", newfall), ("caught", caught)):
    c = collections.Counter((int(k[0] // 8) * 8, int(k[1] // 8) * 8) for k in L)
    print("%s by 8 m cell (x z : n):" % name, ", ".join("(%d %d):%d" % (a, b, n) for (a, b), n in c.most_common(12)))
