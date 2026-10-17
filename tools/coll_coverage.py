#!/usr/bin/env python3
"""Where is there visible floor/wall but no collision? usage: coll_coverage.py <level.lvl>
Samples points on the up-facing render triangles of the playable layers and asks the collision polygons (same rules as src/coll.c:
|normal.y| >= 0.5 is walkable) whether there is a floor within 0.4 m of the visible one."""
import sys, struct, collections
import numpy as np

b = open(sys.argv[1], "rb").read()
h = struct.unpack_from("<4sI5I5I6f3f", b, 0)
ntex, nbatch, nverts, ncv, ncp = h[2:7]
off_tex, off_batch, off_verts, off_cv, off_cp = h[7:12]
V = np.frombuffer(b, dtype=np.dtype([("u", "<f4"), ("v", "<f4"), ("c", "<u4"), ("p", "<f4", 3)]), count=nverts, offset=off_verts)
P = V["p"].reshape(-1, 3, 3).astype(float)
batches = [struct.unpack_from("<HHIII", b, off_batch + 16 * i) for i in range(nbatch)]
bg = np.zeros(len(P), bool)
for t, f, first, count, _ in batches:
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
    N = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); L = np.linalg.norm(N, axis=1)
    return N / np.where(L > 1e-12, L, 1)[:, None], L


NC, LC = normals(C)
floor_c = np.where((np.abs(NC[:, 1]) >= 0.5) & (LC > 1e-9))[0]
Cf = C[floor_c]
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
    return out


NR, LR = normals(P)
render = np.where((~bg) & (NR[:, 1] > 0.9) & (LR > 1e-9))[0]
area = LR[render] / 2
rng = np.random.default_rng(11)
pick = rng.choice(render, size=min(3000, len(render)), p=area / area.sum())
miss, tot = [], 0
for i in pick:
    u, v = rng.random(2)
    if u + v > 1:
        u, v = 1 - u, 1 - v
    p = P[i][0] + (P[i][1] - P[i][0]) * u + (P[i][2] - P[i][0]) * v
    hs = coll_heights(p[0], p[2])
    tot += 1
    if not any(abs(hh - p[1]) < 0.4 for hh in hs):
        miss.append((p, hs))
print("sampled %d points on visible up-facing floor; %d (%.1f%%) have NO collision floor within 0.4 m" % (tot, len(miss), 100 * len(miss) / tot))
if miss:
    pts = np.array([m[0] for m in miss])
    print("missing points: x %.0f..%.0f, y %.1f..%.1f, z %.0f..%.0f" % (pts[:, 0].min(), pts[:, 0].max(), pts[:, 1].min(), pts[:, 1].max(), pts[:, 2].min(), pts[:, 2].max()))
    cells = collections.Counter((int(p[0] // 10) * 10, int(p[2] // 10) * 10) for p, _ in miss)
    print("by 10 m cell (x, z): count  ->", cells.most_common(8))
    print("of those, with a collision floor ABOVE/BELOW instead:", sum(1 for _, hs in miss if hs), "; with nothing at all:", sum(1 for _, hs in miss if not hs))


# ---- height mismatch of the floor samples that have collision at another height ----
dh = []
for p, hs in miss:
    if hs:
        dh.append(min(hs, key=lambda hh: abs(hh - p[1])) - p[1])
if dh:
    dh = np.array(dh)
    print("collision floor minus visible floor for those: median %.2f m, range %.2f .. %.2f; collision ABOVE visible by >0.4 m: %d, BELOW by >0.4 m: %d" % (
        np.median(dh), dh.min(), dh.max(), int((dh > 0.4).sum()), int((dh < -0.4).sum())))


# ---- visible walls with no collision surface nearby ----
def point_tri_dist(p, T):
    """distance from point p to each triangle in T (n,3,3), Ericson closest-point"""
    a, b_, c = T[:, 0], T[:, 1], T[:, 2]
    ab, ac, ap = b_ - a, c - a, p - a
    d1 = (ab * ap).sum(1); d2 = (ac * ap).sum(1)
    bp = p - b_; d3 = (ab * bp).sum(1); d4 = (ac * bp).sum(1)
    cp = p - c; d5 = (ab * cp).sum(1); d6 = (ac * cp).sum(1)
    vc = d1 * d4 - d3 * d2; vb = d5 * d2 - d1 * d6; va = d3 * d6 - d5 * d4
    den = va + vb + vc
    den = np.where(np.abs(den) < 1e-18, 1e-18, den)
    v = vb / den; w = vc / den
    q = a + ab * v[:, None] + ac * w[:, None]
    # clamp barycentrics (approximation good enough for a 0.3 m threshold): project inside by clamping v,w
    v = np.clip(v, 0, 1); w = np.clip(w, 0, 1); s = v + w
    v = np.where(s > 1, v / np.where(s > 0, s, 1), v); w = np.where(s > 1, w / np.where(s > 0, s, 1), w)
    q = a + ab * v[:, None] + ac * w[:, None]
    return np.linalg.norm(q - p, axis=1)


walls = np.where((~bg) & (np.abs(NR[:, 1]) < 0.3) & (LR > 1e-9))[0]
areaw = LR[walls] / 2
pickw = rng.choice(walls, size=min(1200, len(walls)), p=areaw / areaw.sum())
cbox = np.stack([C[:, :, 0].min(1), C[:, :, 0].max(1), C[:, :, 1].min(1), C[:, :, 1].max(1), C[:, :, 2].min(1), C[:, :, 2].max(1)], 1)
nocol = 0; far = []
for i in pickw:
    u, v = rng.random(2)
    if u + v > 1:
        u, v = 1 - u, 1 - v
    p = P[i][0] + (P[i][1] - P[i][0]) * u + (P[i][2] - P[i][0]) * v
    near = np.where((cbox[:, 0] - 0.4 <= p[0]) & (cbox[:, 1] + 0.4 >= p[0]) & (cbox[:, 2] - 0.4 <= p[1]) & (cbox[:, 3] + 0.4 >= p[1]) &
                    (cbox[:, 4] - 0.4 <= p[2]) & (cbox[:, 5] + 0.4 >= p[2]))[0]
    d = point_tri_dist(p, C[near]).min() if len(near) else 99.0
    if d > 0.3:
        nocol += 1; far.append(p)
print("sampled %d points on visible wall triangles: %d (%.1f%%) have no collision surface of ANY orientation within 0.3 m" % (len(pickw), nocol, 100 * nocol / len(pickw)))
if far:
    f = np.array(far)
    cells = collections.Counter((int(p[0] // 10) * 10, int(p[2] // 10) * 10) for p in far)
    print("   by 10 m cell (x, z):", cells.most_common(8))


# ---- only walls a rider can actually reach: a collision floor within 2.5 m below the sample and the sample within 2.5 m above it ----
reach_total = reach_missing = 0
rows = []
for p in far if far else []:
    pass
allw = []
for i in pickw:
    u, v = rng.random(2)
    if u + v > 1:
        u, v = 1 - u, 1 - v
    p = P[i][0] + (P[i][1] - P[i][0]) * u + (P[i][2] - P[i][0]) * v
    hs = [hh for hh in coll_heights(p[0] + 0.0, p[2] + 0.0) if -0.05 <= p[1] - hh <= 2.5]
    if not hs:
        continue
    reach_total += 1
    near = np.where((cbox[:, 0] - 0.4 <= p[0]) & (cbox[:, 1] + 0.4 >= p[0]) & (cbox[:, 2] - 0.4 <= p[1]) & (cbox[:, 3] + 0.4 >= p[1]) &
                    (cbox[:, 4] - 0.4 <= p[2]) & (cbox[:, 5] + 0.4 >= p[2]))[0]
    d = point_tri_dist(p, C[near]).min() if len(near) else 99.0
    if d > 0.3:
        reach_missing += 1; rows.append(p)
print("walls within rider reach (collision floor under them within 2.5 m): %d sampled, %d (%.1f%%) have no collision surface within 0.3 m" % (
    reach_total, reach_missing, 100 * reach_missing / max(reach_total, 1)))
if rows:
    cells = collections.Counter((int(p[0] // 10) * 10, int(p[2] // 10) * 10) for p in rows)
    print("   by 10 m cell (x, z):", cells.most_common(8))
