#!/usr/bin/env python3
"""Surface materials for the sound effects (used by convert_level.py).

The collision data has no material (its flag classes 4/8/16/32 are not reliable for it), but every drawn surface has a texture and the art assets have telling names
(ground_brick_01, trn_pebble_plaza_01, marble_tile2_d, road_clean_base02_d, metal_brushed_pipea, railbox, ...). A collision polygon gets the material of the texture drawn at
the same spot (sampled at the centre and at four points towards its corners, majority vote); a rail or ledge gets the texture it is made of / lies on.

Material byte (one per collision polygon): bits 0-1 = what it is (0 concrete / stone / tile / brick = the default, 1 metal, 2 wood), bits 2-3 = how it rolls (0 medium, 1 rough, 2 smooth).
Rails / ledges: the same 2-bit material, stored in bits 24-25 of the rail table's npoints word.
"""
import collections
import numpy as np

CONC, METAL, WOOD = 0, 1, 2
MEDIUM, ROUGH, SMOOTH = 0, 1, 2
MAT_NAMES = ["CONC", "METAL", "WOOD"]
ROLL_NAMES = ["MEDIUM", "ROUGH", "SMOOTH"]

WOOD_WORDS = ("wood", "plank", "timber", "deck", "boardwalk", "parquet", "fence")
METAL_WORDS = ("metal", "steel", "iron", "railbox", "pipe", "grate", "drain", "manhole", "copper", "brushed")
ROUGH_WORDS = ("pebble", "brick", "cobble", "gravel", "crack", "grass", "dirt", "sand", "paver", "slat", "rough")
SMOOTH_WORDS = ("marble", "smooth", "clean", "polish", "asphalt", "road_", "bowl", "glass")


def classify(name):
    """texture file name -> material byte"""
    n = name.lower()
    mat = CONC
    if any(w in n for w in WOOD_WORDS):
        mat = WOOD
    elif any(w in n for w in METAL_WORDS):
        mat = METAL
    roll = MEDIUM
    if mat == WOOD or any(w in n for w in ROUGH_WORDS):
        roll = ROUGH
    elif any(w in n for w in SMOOTH_WORDS):
        roll = SMOOTH
    return mat | (roll << 2)


class RenderIndex:
    """Grid over the drawn triangles: which texture is at (x, z, y)?"""

    def __init__(self, tris, names, cell=4.0):
        self.tris = tris                                   # (n, 3, 3)
        self.mat = np.array([classify(n) for n in names], np.uint8)
        self.cell = cell
        self.grid = collections.defaultdict(list)
        ny = np.abs(np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])[:, 1])
        ln = np.linalg.norm(np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]), axis=1) + 1e-12
        self.up = (ny / ln) >= 0.3
        lo = tris.min(axis=1); hi = tris.max(axis=1)
        for i in range(len(tris)):
            for gx in range(int(np.floor(lo[i][0] / cell)), int(np.floor(hi[i][0] / cell)) + 1):
                for gz in range(int(np.floor(lo[i][2] / cell)), int(np.floor(hi[i][2] / cell)) + 1):
                    self.grid[(gx, gz)].append(i)

    def at_floor(self, x, z, y, max_dy=0.4):
        """material byte of the upward-ish drawn triangle covering (x, z) whose surface is closest to height y, or None"""
        best, bd = None, max_dy
        for i in self.grid.get((int(np.floor(x / self.cell)), int(np.floor(z / self.cell))), ()):
            if not self.up[i]:
                continue
            a, b, c = self.tris[i]
            d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(d) < 1e-9:
                continue
            u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
            v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
            w = 1 - u - v
            if u < -0.03 or v < -0.03 or w < -0.03:
                continue
            yy = u * a[1] + v * b[1] + w * c[1]
            if abs(yy - y) < bd:
                bd, best = abs(yy - y), i
        return best

    def near(self, p, radius=0.8):
        """index of the drawn triangle nearest to the 3D point p (by centroid), or None"""
        best, bd = None, radius
        gx, gz = int(np.floor(p[0] / self.cell)), int(np.floor(p[2] / self.cell))
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for i in self.grid.get((gx + dx, gz + dz), ()):
                    d = np.linalg.norm(self.tris[i].mean(axis=0) - p)
                    if d < bd:
                        bd, best = d, i
        return best


def build(cverts, collpolys, tri_array, tri_tex, names, rails, kinds, log=print):
    """-> (poly material bytes (one per entry of collpolys), rail material codes 0..2 (one per rail))
    tri_array: (n, 3, 3) drawn triangles, tri_tex: texture index of each (-1 = none), names: texture file names by index"""
    keep = np.array([t >= 0 for t in tri_tex])
    tris = tri_array[keep]
    tex = np.array(tri_tex)[keep]
    idx = RenderIndex(tris, names)
    cv = np.array(cverts, float)
    out = np.zeros(len(collpolys), np.uint8)
    area_by = collections.Counter()
    for k, p in enumerate(collpolys):
        ids = [i for i in p[:4] if i != 0xFFFF]
        P = cv[ids]
        n = np.cross(P[1] - P[0], P[2] - P[0]); ln = np.linalg.norm(n)
        if ln < 1e-9 or abs(n[1]) / ln < 0.15:
            continue
        c = P.mean(axis=0)
        votes = collections.Counter()
        for q in [c] + [c + 0.8 * (v - c) for v in P]:
            i = idx.at_floor(q[0], q[2], q[1])
            if i is not None:
                votes[int(idx.mat[tex[i]])] += 1
        if votes:
            out[k] = votes.most_common(1)[0][0]
        area = 0.5 * ln * (1 if len(ids) == 3 else 2)
        area_by[int(out[k])] += area
    tot = sum(area_by.values()) or 1
    log("  materials (rideable collision area): " + ", ".join("%s/%s %.0f%%" % (MAT_NAMES[m & 3], ROLL_NAMES[m >> 2], 100 * a / tot) for m, a in sorted(area_by.items(), key=lambda t: -t[1])))
    rm = []
    for r, kind in zip(rails, kinds):
        mid = 0.5 * (r[0] + r[-1]) if len(r) == 2 else r[len(r) // 2]
        m = CONC
        if kind == 1:                                      # a ledge: the surface of its top, a hand's width inside the edge (left of the direction)
            d = r[-1] - r[0]
            d = d / (np.linalg.norm(d) + 1e-9)
            for off in (0.15, 0.4, 0.8):
                q = mid + np.array([d[2], 0.0, -d[0]]) * off
                i = idx.at_floor(q[0], q[2], q[1], 0.35)
                if i is not None:
                    m = int(idx.mat[tex[i]]) & 3
                    break
        else:                                              # a rail: what it is made of
            i = idx.near(mid, 1.0)
            if i is not None:
                m = int(idx.mat[tex[i]]) & 3
            else:
                m = METAL
        rm.append(m)
    log("  rail / ledge materials: " + ", ".join("%s %d" % (MAT_NAMES[m], c) for m, c in sorted(collections.Counter(rm).items())))
    return out, rm
