#!/usr/bin/env python3
"""Summarise tests/wall_probe.c output. usage: wall_probe_report.py probe.tsv level.lvl.names
Columns: x y z tex side floor_y hit_at_point hit_probes passed dy.  'passed' = a rider with the real physics (src/skate.c), heading straight at
the drawn wall point at 5 m/s from 0.8 m away, ended up more than 0.3 m beyond it = the wall is not solid. This is the number that matters."""
import sys, collections
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1])][1:]
names = [l.strip() for l in open(sys.argv[2], encoding="utf-8")]
pts = [(float(r[0]), float(r[1]), float(r[2]), int(r[3]), int(r[4]), float(r[5]), int(r[8]), float(r[9])) for r in rows]
MIN_RISE = 0.25     # points less than this above the rider's floor are step lips/curbs, not barriers (the floor logic handles those)
all_pts = pts
pts = [p for p in pts if p[1] - p[5] >= MIN_RISE]
tot = len(pts); gap = [p for p in pts if p[6] == 1]
print("probed %d rider-side points (all heights: %d, passed %d)" % (len(all_pts), len(all_pts), sum(p[6] for p in all_pts)))
print("faces rising >= %.2f m above the rider's floor: %d probed; the real physics lets the rider THROUGH at %d (%.1f%%)" % (MIN_RISE, tot, len(gap), 100 * len(gap) / max(tot, 1)))
print("  of the pass-throughs: falls > 1 m: %d, level: %d, climbs > 0.4 m: %d" % (sum(p[7] < -1 for p in gap), sum(-1 <= p[7] <= 0.4 for p in gap), sum(p[7] > 0.4 for p in gap)))
by = collections.defaultdict(lambda: [0, 0])
for p in pts:
    by[p[3]][0] += 1; by[p[3]][1] += p[6]
print("\nper texture (probed, passed through):")
for t, (n, g) in sorted(by.items(), key=lambda kv: -kv[1][1])[:14]:
    print("  %-34s %5d %5d  %3d%%" % (names[t] if t < len(names) else t, n, g, 100 * g // max(n, 1)))
cells = collections.defaultdict(list)
for p in gap:
    cells[(int(p[0] // 8) * 8, int(p[2] // 8) * 8)].append(p)
print("\n8 m cells with pass-through points (x z : n, y range, rider dy range, textures):")
for (cx, cz), L in sorted(cells.items(), key=lambda kv: -len(kv[1]))[:14]:
    print("  %5d %5d : %4d  y %.1f..%.1f  dy %.1f..%.1f  %s" % (cx, cz, len(L), min(q[1] for q in L), max(q[1] for q in L),
          min(q[7] for q in L), max(q[7] for q in L),
          ", ".join("%s x%d" % (names[t][:22], c) for t, c in collections.Counter(q[3] for q in L).most_common(3))))
