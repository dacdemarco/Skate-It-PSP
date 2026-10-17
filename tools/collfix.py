#!/usr/bin/env python3
"""Fill collision gaps from the render triangles (used by convert_level.py).

Problem (measured with tests/wall_probe.c + tools/coll_gaps.py): the octree leaf collision lists leave some drawn walls non-solid - windows,
glass storefronts and interior wall panels on the library's upper levels are ridden straight through. Floors are fine (the remaining
floor 'gaps' are awnings/sills 6-9 m above the rideable floor and must stay non-solid).

Rule: a visible wall triangle (|n.y| < RIDE_NY, playable layers, not decor) becomes a collision wall when
  * it is not already solid: at least MIN_UNSOLID of its sample points (a lattice, ~0.7 m apart) are farther than SOLID_DIST from every existing collision wall
    polygon (|n.y| < RIDE_NY, the same threshold src/coll.c uses), AND
  * a rider can reach it: for one of those unsolid sample points there is collision floor 0.8 m in front of or behind it (either side), at most
    1.3 m below the point and at most 0.1 m above it (the rider's body height, same test as tests/wall_probe.c).
The new polygons use the visible triangle itself; their flags are FLAG_SYNTH so a later decoding of the original flags can tell them apart.
"""
import numpy as np

RIDE_NY = 0.15          # |normal.y| >= this = a surface the skate physics rolls on (src/coll.h COLL_RIDE_NY); below = a wall
SOLID_DIST = 0.3
MIN_UNSOLID = 2
REACH_FRONT = 0.8
FLAG_SYNTH = 0x8000
DECOR = ("banner", "sign", "trafficlight", "traffic_light", "book0", "glass_finawning", "hedge")
GRID = 4.0


def _bary_points(tri, spacing=0.7, nmax=16):
    """Barycentric lattice on one triangle (edges included): about one point every `spacing` metres along the longest edge, 3..nmax divisions.
    Tall facade triangles need the density: only the bottom ~1.3 m of them is within the rider's reach."""
    e = max(np.linalg.norm(tri[1] - tri[0]), np.linalg.norm(tri[2] - tri[1]), np.linalg.norm(tri[0] - tri[2]))
    m = int(min(nmax, max(3, np.ceil(e / spacing))))
    w = np.array([(i / m, j / m, (m - i - j) / m) for i in range(m + 1) for j in range(m + 1 - i)])
    return w @ tri


def _tri_dist(p, T):
    a, b, c = T[:, 0], T[:, 1], T[:, 2]
    ab, ac, ap = b - a, c - a, p - a
    d1 = (ab * ap).sum(1); d2 = (ac * ap).sum(1)
    bp = p - b; d3 = (ab * bp).sum(1); d4 = (ac * bp).sum(1)
    cp = p - c; d5 = (ab * cp).sum(1); d6 = (ac * cp).sum(1)
    vc = d1 * d4 - d3 * d2; vb = d5 * d2 - d1 * d6; va = d3 * d6 - d5 * d4
    den = va + vb + vc
    den = np.where(np.abs(den) < 1e-18, 1e-18, den)
    v = np.clip(vb / den, 0, 1); w = np.clip(vc / den, 0, 1); s = v + w
    v = np.where(s > 1, v / np.where(s > 0, s, 1), v); w = np.where(s > 1, w / np.where(s > 0, s, 1), w)
    return np.linalg.norm(a + ab * v[:, None] + ac * w[:, None] - p, axis=1)


class _Index:
    def __init__(self, T):
        self.T = T
        self.cells = {}
        lo = np.floor(np.minimum.reduce(T[:, :, [0, 2]], axis=1) / GRID).astype(int)
        hi = np.floor(np.maximum.reduce(T[:, :, [0, 2]], axis=1) / GRID).astype(int)
        for i in range(len(T)):
            for x in range(lo[i, 0], hi[i, 0] + 1):
                for z in range(lo[i, 1], hi[i, 1] + 1):
                    self.cells.setdefault((x, z), []).append(i)
        self.cells = {k: np.array(v) for k, v in self.cells.items()}

    def near(self, x, z, r=0.0):
        out = []
        for cx in range(int(np.floor((x - r) / GRID)), int(np.floor((x + r) / GRID)) + 1):
            for cz in range(int(np.floor((z - r) / GRID)), int(np.floor((z + r) / GRID)) + 1):
                if (cx, cz) in self.cells:
                    out.append(self.cells[(cx, cz)])
        return np.unique(np.concatenate(out)) if out else np.zeros(0, int)


def _normals(T):
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    l = np.linalg.norm(n, axis=1)
    return n / np.where(l > 1e-12, l, 1)[:, None], l


def collision_triangles(cverts, polys):
    cv = np.array(cverts, float)
    tris = []
    for p in polys:
        tris.append((cv[p[0]], cv[p[1]], cv[p[2]]))
        if p[3] != 0xFFFF:
            tris.append((cv[p[0]], cv[p[2]], cv[p[3]]))
    return np.array(tris)


def floor_height_fn(C, min_ny=RIDE_NY):
    n, l = _normals(C)
    Cf = C[(np.abs(n[:, 1]) >= min_ny) & (l > 1e-9)]
    idx = _Index(Cf)

    def ground(x, z, ymax):
        best = None
        for k in idx.near(x, z):
            a, b, c = Cf[k]
            d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(d) < 1e-12:
                continue
            l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
            l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
            if l1 >= -1e-4 and l2 >= -1e-4 and 1 - l1 - l2 >= -1e-4:
                y = l1 * a[1] + l2 * b[1] + (1 - l1 - l2) * c[1]
                if y <= ymax and (best is None or y > best):
                    best = y
        return best
    return ground


def synthesize_walls(cverts, polys, cand, log=print):
    """cand: list of (3x3 float array, texture file name). Returns (new_vertices, new_polys, report)."""
    C = collision_triangles(cverts, polys)
    nC, lC = _normals(C)
    walls = C[(np.abs(nC[:, 1]) < RIDE_NY) & (lC > 1e-9)]
    widx = _Index(walls)
    ground = floor_height_fn(C)
    T = np.array([c[0] for c in cand], float)
    names = [c[1].lower() for c in cand]
    n, ln = _normals(T)
    take = []
    stats = {}
    for i in range(len(T)):
        if ln[i] * 0.5 < 0.02 or abs(n[i, 1]) >= RIDE_NY or any(d in names[i] for d in DECOR):
            continue
        nh = np.array([n[i, 0], n[i, 2]]); nl = np.linalg.norm(nh)
        if nl < 1e-9:
            continue
        nh /= nl
        unsolid = 0; reach = False
        for p in _bary_points(T[i]):
            near = widx.near(p[0], p[2], SOLID_DIST + 0.1)
            d = _tri_dist(p, walls[near]).min() if len(near) else 99.0
            if d <= SOLID_DIST:
                continue
            unsolid += 1
            for side in (-1, 1):
                qx, qz = p[0] + side * nh[0] * REACH_FRONT, p[2] + side * nh[1] * REACH_FRONT
                g = ground(qx, qz, p[1] + 0.3)
                if g is not None and -0.1 <= p[1] - g <= 1.3:
                    reach = True
        if unsolid >= MIN_UNSOLID and reach:
            take.append(i)
            s = stats.setdefault(names[i], [0, 0.0]); s[0] += 1; s[1] += ln[i] * 0.5
    nv = []; npoly = []
    index = {}
    base = len(cverts)
    for i in take:
        ids = []
        for v in T[i]:
            key = tuple(np.round(v, 4))
            if key not in index:
                index[key] = base + len(nv); nv.append(tuple(float(x) for x in v))
            ids.append(index[key])
        npoly.append((ids[0], ids[1], ids[2], 0xFFFF, FLAG_SYNTH))
    assert base + len(nv) < 0xFFFF, "collision vertex pool would overflow u16 indices"
    log("  collfix: %d wall triangles added (%d new collision vertices); by texture:" % (len(npoly), len(nv)))
    for k, (cnt, area) in sorted(stats.items(), key=lambda kv: -kv[1][0])[:12]:
        log("    %-34s %4d tris  %7.0f m2" % (k, cnt, area))
    return nv, npoly, stats


# ---- invisible edge walls: floor that ends over nothing --------------------------------------------------------------------------------
EDGE_FLAG = 0x4000
EDGE_BELOW = 0.5      # wall extends this far below the floor edge
EDGE_ABOVE = 2.5      # ... and this far above it (the rider's probes are at +0.3 / +0.9 m, an ollie apex is < 1 m)
EDGE_OUT = 0.6        # probe distance outward from the edge for the 'nothing there' test
EDGE_STEP = 1.0       # sampling spacing along an edge
EDGE_EXTEND = 0.25    # each wall run is extended by this much at both ends


def synthesize_edge_walls(cverts, polys, log=print):
    """Walls along floor edges where the rider would fall into nothing (no collision floor at or below the point just outside the edge
    up to the wall's top, at any depth below) and where there is no wall yet. Legitimate drops onto lower floors (bowl rim, ledges, stair edges) stay open.
    The DS data has no collision for the roads/outskirts, so without this the whole plaza perimeter is an exit from the world.
    Returns (new_vertices, new_polys)."""
    C = collision_triangles(cverts, polys)
    nC, lC = _normals(C)
    walls = C[(np.abs(nC[:, 1]) < RIDE_NY) & (lC > 1e-9)]
    widx = _Index(walls)
    fl = (np.abs(nC[:, 1]) >= RIDE_NY) & (lC > 1e-9)          # every surface the rider can roll on, steep bowl flanks included
    F = C[fl]
    ground = floor_height_fn(C)
    # edges used by exactly one floor triangle are boundary candidates (the shared quad diagonal and abutting polygons cancel out)
    key = lambda v: tuple(np.round(v, 3))
    use = {}
    for t in F:
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            e = tuple(sorted((key(a), key(b))))
            use.setdefault(e, []).append((a, b, t[(k + 2) % 3]))
    nv = []; npoly = []; index = {}
    base = len(cverts)

    def vid(v):
        k = key(v)
        if k not in index:
            index[k] = base + len(nv); nv.append(tuple(float(x) for x in v))
        return index[k]
    total_len = 0.0
    for e, lst in use.items():
        if len(lst) != 1:
            continue
        a, b, c = lst[0]
        d = np.array([b[0] - a[0], b[2] - a[2]]); L = np.linalg.norm(d)
        if L < 0.05:
            continue
        d /= L
        out = np.array([d[1], -d[0]])
        if out @ np.array([c[0] - a[0], c[2] - a[2]]) > 0:       # point away from the triangle's third vertex
            out = -out
        n = max(1, int(np.ceil(L / EDGE_STEP)))
        ts = np.linspace(0.0, 1.0, n + 1)
        P = a[None, :] + (b - a)[None, :] * ts[:, None]
        void = []
        for p in P:
            o = p[[0, 2]] + out * EDGE_OUT
            if ground(o[0], o[1], p[1] + EDGE_ABOVE) is not None:     # any floor up to the wall's full height counts (stacked floors: the rider may be on the one above)
                void.append(False); continue
            near = widx.near(p[0], p[2], 0.5)                       # a wall already stands here (within 0.3 m of the edge point)?
            q = np.array([p[0], p[1] + 0.6, p[2]])
            solid = len(near) and _tri_dist(q, walls[near]).min() <= 0.3
            void.append(not solid)
        i = 0
        while i < len(ts):
            if not void[i]:
                i += 1; continue
            j = i
            while j + 1 < len(ts) and void[j + 1]:
                j += 1
            if j > i:                                               # a run of >= 2 consecutive void samples -> one quad
                p0, p1 = P[i], P[j]
                dv = (b - a) / np.linalg.norm(b - a)
                p0 = p0 - dv * EDGE_EXTEND; p1 = p1 + dv * EDGE_EXTEND     # overlap at the ends so a ride exactly through a corner cannot slip between two walls
                lo0 = p0 + [0, -EDGE_BELOW, 0]; lo1 = p1 + [0, -EDGE_BELOW, 0]
                hi1 = p1 + [0, EDGE_ABOVE, 0]; hi0 = p0 + [0, EDGE_ABOVE, 0]
                npoly.append((vid(lo0), vid(lo1), vid(hi1), vid(hi0), EDGE_FLAG))
                total_len += np.linalg.norm((p1 - p0)[[0, 2]])
            i = j + 1
    assert base + len(nv) < 0xFFFF, "collision vertex pool would overflow u16 indices"
    log("  collfix: %d edge-wall quads (%.0f m of floor edge over nothing, %d new collision vertices)" % (len(npoly), total_len, len(nv)))
    return nv, npoly
