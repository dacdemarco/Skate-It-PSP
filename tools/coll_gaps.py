#!/usr/bin/env python3
"""What exactly are the collision gaps? usage: coll_gaps.py <level.lvl> [samples]
Samples area-weighted points on the visible up-facing (n.y > 0.9) floor triangles of the playable layers and on walls (|n.y| < 0.3),
and for each point lacking collision (same rules as src/coll.c / coll_coverage.py) reports texture id, height of the nearest collision
floor below, and clusters the gaps by 4 m cell so they can be looked at in region_probe.py / the viewer."""
import sys, struct, collections
import numpy as np

b = open(sys.argv[1], "rb").read()
N = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
h = struct.unpack_from("<4sI5I5I6f3f", b, 0)
ntex, nbatch, nverts, ncv, ncp = h[2:7]
off_tex, off_batch, off_verts, off_cv, off_cp = h[7:12]
V = np.frombuffer(b, dtype=np.dtype([("u", "<f4"), ("v", "<f4"), ("c", "<u4"), ("p", "<f4", 3)]), count=nverts, offset=off_verts)
P = V["p"].reshape(-1, 3, 3).astype(float)
tri_tex = np.zeros(len(P), int); bg = np.zeros(len(P), bool)
for i in range(nbatch):
    t, f, first, count, _ = struct.unpack_from("<HHIII", b, off_batch + 16 * i)
    tri_tex[first // 3:(first + count) // 3] = t
    bg[first // 3:(first + count) // 3] = (f & 1) != 0
CV = np.frombuffer(b, "<f4", ncv * 3, off_cv).reshape(-1, 3).astype(float)
polys = [struct.unpack_from("<4HI", b, off_cp + 12 * i) for i in range(ncp)]
ct = []
for p in polys:
    ct.append((CV[p[0]], CV[p[1]], CV[p[2]]))
    if p[3] != 0xFFFF:
        ct.append((CV[p[0]], CV[p[2]], CV[p[3]]))
C = np.array(ct)


def normals(T):
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); l = np.linalg.norm(n, axis=1)
    return n / np.where(l > 1e-12, l, 1)[:, None], l


NC, LC = normals(C)
Cf = C[(np.abs(NC[:, 1]) >= 0.5) & (LC > 1e-9)]
bb = np.stack([Cf[:, :, 0].min(1), Cf[:, :, 0].max(1), Cf[:, :, 2].min(1), Cf[:, :, 2].max(1)], 1)


def coll_heights(x, z):
    m = np.where((bb[:, 0] <= x) & (bb[:, 1] >= x) & (bb[:, 2] <= z) & (bb[:, 3] >= z))[0]
    out = []
    for k in m:
        a, bq, c = Cf[k]
        d = (bq[2] - c[2]) * (a[0] - c[0]) + (c[0] - bq[0]) * (a[2] - c[2])
        if abs(d) < 1e-12:
            continue
        l1 = ((bq[2] - c[2]) * (x - c[0]) + (c[0] - bq[0]) * (z - c[2])) / d
        l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
        if l1 >= -1e-4 and l2 >= -1e-4 and 1 - l1 - l2 >= -1e-4:
            out.append(l1 * a[1] + l2 * bq[1] + (1 - l1 - l2) * c[1])
    return sorted(out)


NR, LR = normals(P)
rng = np.random.default_rng(5)
ok = (~bg) & (LR > 1e-9)
flo = np.where(ok & (NR[:, 1] > 0.9))[0]
area = LR[flo] / 2
miss = []
for i in rng.choice(flo, size=N, p=area / area.sum()):
    u, v = rng.random(2)
    if u + v > 1:
        u, v = 1 - u, 1 - v
    p = P[i][0] + (P[i][1] - P[i][0]) * u + (P[i][2] - P[i][0]) * v
    hs = coll_heights(p[0], p[2])
    if not any(abs(hh - p[1]) < 0.4 for hh in hs):
        below = [hh for hh in hs if hh < p[1]]
        miss.append((p, tri_tex[i], (p[1] - below[-1]) if below else None, area[np.where(flo == i)[0][0]] if False else 0))
print("floor: %d of %d samples (%.1f%%) have no collision floor within 0.4 m" % (len(miss), N, 100 * len(miss) / N))
cells = collections.defaultdict(list)
for p, t, hb, _ in miss:
    cells[(int(np.floor(p[0] / 4)) * 4, int(np.floor(p[2] / 4)) * 4)].append((p, t, hb))
print("clusters (4 m cells with gaps): x z : n  y-range  texture ids  visible-height-above-collision-floor-below (m)")
for (cx, cz), L in sorted(cells.items(), key=lambda kv: -len(kv[1]))[:25]:
    ys = [q[0][1] for q in L]
    hbs = [q[2] for q in L if q[2] is not None]
    print("  %5d %5d : %3d  y %6.1f..%6.1f  tex %s  above-coll %s" % (cx, cz, len(L), min(ys), max(ys), sorted({q[1] for q in L}),
          "%.1f..%.1f" % (min(hbs), max(hbs)) if hbs else "none below"))
hb_all = np.array([m[2] for m in miss if m[2] is not None])
if len(hb_all):
    print("height of the visible gap floor above the collision floor below it: percentiles 10/50/90 = %.1f / %.1f / %.1f m" % tuple(np.percentile(hb_all, [10, 50, 90])))
print("textures involved:", collections.Counter(m[1] for m in miss).most_common(10))


# ---- walls within rider reach with no collision surface of any orientation nearby ----
def pt_tri_dist(p, T):
    a, b_, c = T[:, 0], T[:, 1], T[:, 2]
    ab, ac, ap = b_ - a, c - a, p - a
    d1 = (ab * ap).sum(1); d2 = (ac * ap).sum(1)
    bp = p - b_; d3 = (ab * bp).sum(1); d4 = (ac * bp).sum(1)
    cp = p - c; d5 = (ab * cp).sum(1); d6 = (ac * cp).sum(1)
    vc = d1 * d4 - d3 * d2; vb = d5 * d2 - d1 * d6; va = d3 * d6 - d5 * d4
    den = va + vb + vc
    den = np.where(np.abs(den) < 1e-18, 1e-18, den)
    v = np.clip(vb / den, 0, 1); w = np.clip(vc / den, 0, 1); s = v + w
    v = np.where(s > 1, v / np.where(s > 0, s, 1), v); w = np.where(s > 1, w / np.where(s > 0, s, 1), w)
    return np.linalg.norm(a + ab * v[:, None] + ac * w[:, None] - p, axis=1)


cbox = np.stack([C[:, :, 0].min(1), C[:, :, 0].max(1), C[:, :, 1].min(1), C[:, :, 1].max(1), C[:, :, 2].min(1), C[:, :, 2].max(1)], 1)
wal = np.where(ok & (np.abs(NR[:, 1]) < 0.3))[0]
aw = LR[wal] / 2
reach = 0; gaps = []
for i in rng.choice(wal, size=N, p=aw / aw.sum()):
    u, v = rng.random(2)
    if u + v > 1:
        u, v = 1 - u, 1 - v
    p = P[i][0] + (P[i][1] - P[i][0]) * u + (P[i][2] - P[i][0]) * v
    hs = [hh for hh in coll_heights(p[0], p[2]) if -0.05 <= p[1] - hh <= 2.5]
    if not hs:
        continue
    reach += 1
    near = np.where((cbox[:, 0] - 0.4 <= p[0]) & (cbox[:, 1] + 0.4 >= p[0]) & (cbox[:, 2] - 0.4 <= p[1]) & (cbox[:, 3] + 0.4 >= p[1]) &
                    (cbox[:, 4] - 0.4 <= p[2]) & (cbox[:, 5] + 0.4 >= p[2]))[0]
    d = pt_tri_dist(p, C[near]).min() if len(near) else 99.0
    if d > 0.3:
        gaps.append((p, tri_tex[i], d, p[1] - hs[-1]))
print("\nwalls within reach: %d sampled, %d (%.1f%%) have no collision surface within 0.3 m" % (reach, len(gaps), 100 * len(gaps) / max(reach, 1)))
cells = collections.defaultdict(list)
for g in gaps:
    cells[(int(np.floor(g[0][0] / 4)) * 4, int(np.floor(g[0][2] / 4)) * 4)].append(g)
for (cx, cz), L in sorted(cells.items(), key=lambda kv: -len(kv[1]))[:25]:
    print("  %5d %5d : %3d  y %6.1f..%6.1f  tex %s  height-over-floor %.1f..%.1f  nearest-coll %.1f..%.1f m" % (
        cx, cz, len(L), min(g[0][1] for g in L), max(g[0][1] for g in L), sorted({int(g[1]) for g in L}),
        min(g[3] for g in L), max(g[3] for g in L), min(g[2] for g in L), max(g[2] for g in L)))
print("wall-gap textures:", collections.Counter(int(g[1]) for g in gaps).most_common(10))
