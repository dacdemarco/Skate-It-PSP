#!/usr/bin/env python3
"""Is layer 4 redundant with layer 1? usage: layer_compare.py <Level>
For every 4_X.oct, sample points on its triangles and check whether the same-named 1_X.oct (or any layer-1 chunk) covers that XZ
footprint with up-facing geometry within a few metres in height; also compares triangle counts and areas per chunk."""
import sys, os, glob, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import oct as octm

level = sys.argv[1]
lvl = "extracted/unpacked/DATA/LOCATIONS/%s/%s/" % (level, level)
nverts = len(octm.load_cmn(lvl + "verts.cmn"))


def load(f):
    b = open(f, "rb").read(); S, O = octm.scale(b), octm.origin(b)
    T = []
    for lf in octm.leaves(b, nverts):
        for tid, tris in octm.batches(b, lf):
            for tri in tris:
                T.append(np.array([[O[a] + v[0][a] * S for a in range(3)] for v in tri]))
    return np.array(T) if T else np.zeros((0, 3, 3))


def area(T):
    if len(T) == 0:
        return 0.0
    return float((np.linalg.norm(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]), axis=1) / 2).sum())


def covered(T1, x, z, y, band):
    """highest layer-1 surface at (x,z) within +-band of y that is up/steep-facing: returns True if any triangle contains the point"""
    if len(T1) == 0:
        return False
    m = (T1[:, :, 0].min(1) <= x) & (T1[:, :, 0].max(1) >= x) & (T1[:, :, 2].min(1) <= z) & (T1[:, :, 2].max(1) >= z)
    for a, b, c in T1[m]:
        d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
        if abs(d) < 1e-12:
            continue
        l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
        l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
        if l1 >= -1e-4 and l2 >= -1e-4 and 1 - l1 - l2 >= -1e-4 and abs(l1 * a[1] + l2 * b[1] + (1 - l1 - l2) * c[1] - y) <= band:
            return True
    return False


files4 = sorted(glob.glob(lvl + "4_*.oct"))
all1 = {os.path.basename(f)[2:].lower(): load(f) for f in glob.glob(lvl + "1_*.oct")}
rng = np.random.default_rng(2)
print("%-26s %6s %8s | %6s %8s | %s" % ("chunk", "tris4", "area4", "tris1", "area1", "layer-4 sample points also covered by the same-named layer-1 chunk (within 1 m / 5 m in height)"))
tot = [0, 0, 0]
for f in files4:
    name = os.path.basename(f)[2:].lower()
    T4 = load(f); T1 = all1.get(name, np.zeros((0, 3, 3)))
    ar = np.linalg.norm(np.cross(T4[:, 1] - T4[:, 0], T4[:, 2] - T4[:, 0]), axis=1) / 2 if len(T4) else np.zeros(0)
    n = 0; c1 = 0; c5 = 0
    if len(T4):
        pick = rng.choice(len(T4), size=min(120, len(T4) * 8), p=ar / ar.sum())
        for i in pick:
            u, v = rng.random(2)
            if u + v > 1:
                u, v = 1 - u, 1 - v
            p = T4[i][0] + (T4[i][1] - T4[i][0]) * u + (T4[i][2] - T4[i][0]) * v
            n += 1; c1 += covered(T1, p[0], p[2], p[1], 1.0); c5 += covered(T1, p[0], p[2], p[1], 5.0)
    tot[0] += n; tot[1] += c1; tot[2] += c5
    print("%-26s %6d %8.0f | %6d %8.0f | %3d%% / %3d%%" % (name, len(T4), area(T4), len(T1), area(T1), 100 * c1 // max(n, 1), 100 * c5 // max(n, 1)))
print("overall: %d%% of layer-4 sample points are covered by layer-1 geometry within 1 m, %d%% within 5 m" % (100 * tot[1] // max(tot[0], 1), 100 * tot[2] // max(tot[0], 1)))


# ---- 3-D proximity: distance from layer-4 sample points to the nearest triangle of ANY layer-1 chunk (works for walls too) ----
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
    q = a + ab * v[:, None] + ac * w[:, None]
    return np.linalg.norm(q - p, axis=1)


T1all = np.concatenate([t for t in all1.values() if len(t)])
lo, hi = T1all.min(axis=(0, 1)), T1all.max(axis=(0, 1))
cen1 = T1all.mean(1)
res = collections.defaultdict(lambda: [0, 0, 0, 0])
for f in files4:
    name = os.path.basename(f)[2:].lower()
    T4 = load(f)
    if not len(T4):
        continue
    ar = np.linalg.norm(np.cross(T4[:, 1] - T4[:, 0], T4[:, 2] - T4[:, 0]), axis=1) / 2
    for i in rng.choice(len(T4), size=min(60, len(T4) * 6), p=ar / ar.sum()):
        u, v = rng.random(2)
        if u + v > 1:
            u, v = 1 - u, 1 - v
        p = T4[i][0] + (T4[i][1] - T4[i][0]) * u + (T4[i][2] - T4[i][0]) * v
        near = np.where(np.linalg.norm(cen1 - p, axis=1) < 40)[0]
        d = pt_tri_dist(p, T1all[near]).min() if len(near) else 99.0
        r = res[name]; r[0] += 1; r[1] += d < 1.0; r[2] += d < 3.0; r[3] += d < 10.0
print("\n3-D distance from layer-4 points to the nearest layer-1 triangle (any chunk):")
tt = [0, 0, 0, 0]
for name, r in sorted(res.items()):
    print("  %-26s n=%3d  within 1 m: %3d%%   3 m: %3d%%   10 m: %3d%%" % (name, r[0], 100 * r[1] // r[0], 100 * r[2] // r[0], 100 * r[3] // r[0]))
    tt = [tt[k] + r[k] for k in range(4)]
print("  overall: within 1 m %d%%, 3 m %d%%, 10 m %d%%" % (100 * tt[1] // tt[0], 100 * tt[2] // tt[0], 100 * tt[3] // tt[0]))
