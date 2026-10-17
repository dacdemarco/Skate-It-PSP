#!/usr/bin/env python3
"""Extract the grindable rails of a level from its collision polygons (used by convert_level.py).

Decoded from the Amphitheatre data (tools/flag_survey.py, tools/rail_survey.py): collision flag 256 (0x100) marks thin tubes and boxes of tiny polygons next to
metal_brushed_pipea / railbox textures - 26 connected objects: 20 long (4-16 m) tubes and rail boxes (stair handrails sloping 0.13-0.31, low flat rail boxes), the rest
vertical 1-2 m posts. (Flag 512 = tiny polygons at traffic light / sign poles; flags 4/8/16/32 are giant connected surface classes = materials of floors and walls.)

A rail = a connected component (shared collision vertices) of flag-256 polygons that is long (>= MIN_LEN) and not vertical (|axis.y| <= MAX_VERT). Its centre line is a polyline:
vertices are projected on the principal axis, binned every BIN metres; per bin the mean XZ and the HIGHEST y (the top of the tube / box: where a board rides)."""
import numpy as np

FLAG_RAIL = 256
MIN_LEN = 1.5
MAX_VERT = 0.85
BIN = 0.25            # (unused since the upper-envelope fit)
BEND = 0.12           # a rail gets an extra point where its top bends more than this (m) away from the straight line


def extract_rails(cverts, collpolys, log=print):
    cv = np.array(cverts, float)
    polys = [p for p in collpolys if p[4] & FLAG_RAIL]
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for p in polys:
        ids = [i for i in p[:4] if i != 0xFFFF]
        for i in ids[1:]:
            parent[find(ids[0])] = find(i)
    # vertical support posts hang off the rails and belong to the same connected object: their polygons are tall and thin (horizontal extent < 0.2 m, height >= 0.2 m);
    # the rail itself is made of polygons that run along it
    def is_post(ids):
        P = cv[ids]
        ext = P.max(0) - P.min(0)
        return max(ext[0], ext[2]) < 0.2 and ext[1] >= 0.2
    comp = {}
    for p in polys:
        ids = [i for i in p[:4] if i != 0xFFFF]
        if is_post(ids):
            continue
        comp.setdefault(find(ids[0]), set()).update(ids)
    rails = []
    polys_of = {}
    for p in polys:
        ids = [i for i in p[:4] if i != 0xFFFF]
        if is_post(ids):
            continue
        polys_of.setdefault(find(ids[0]), []).append(ids)
    for root, vs in comp.items():
        P = cv[sorted(vs)]
        c = P.mean(0)
        _, _, vt = np.linalg.svd(P - c, full_matrices=False)
        ax = vt[0]
        length = ((P - c) @ ax).max() - ((P - c) @ ax).min()
        if length < MIN_LEN or abs(ax[1]) > MAX_VERT:
            continue
        # candidates for the top of the rail: vertices of up-facing polygons (a board rides on those faces)
        up = set()
        for ids in polys_of[root]:
            Q = cv[ids]
            nrm = np.cross(Q[1] - Q[0], Q[2] - Q[0]); ln = np.linalg.norm(nrm)
            if ln > 1e-12 and abs(nrm[1] / ln) >= 0.5:
                up.update(ids)
        U = cv[sorted(up)] if len(up) >= 2 else P
        t = (U - c) @ ax

        def ring(tc):                                   # top of the rail around arc position tc: highest y, XZ averaged over the top vertices there
            m = np.abs(t - tc) <= 0.15
            V = U[m] if m.any() else U[[int(np.argmin(np.abs(t - tc)))]]
            y = V[:, 1].max()
            sel = V[V[:, 1] >= y - 0.1]
            return np.array([sel[:, 0].mean(), y, sel[:, 2].mean()]), tc

        pts = [ring(t.min()), ring(t.max())]
        # upper envelope refinement: add the candidate that sticks out most above the straight line, while it sticks out more than BEND
        def refine(i, depth=0):
            (pa, ta), (pb, tb) = pts[i], pts[i + 1]
            if depth > 6 or tb - ta < 0.6:
                return
            m = (t > ta + 0.3) & (t < tb - 0.3)
            if not m.any():
                return
            f = (t[m] - ta) / (tb - ta)
            line_y = pa[1] + (pb[1] - pa[1]) * f
            dev = U[m][:, 1] - line_y
            k = int(np.argmax(dev))
            if dev[k] < BEND:
                return
            pm = ring(float(t[m][k]))
            pts.insert(i + 1, pm)
            refine(i + 1, depth + 1)
            refine(i, depth + 1)
        refine(0)
        poly = np.array([q[0] for q in pts])
        if (poly[-1][0], poly[-1][2]) < (poly[0][0], poly[0][2]):
            poly = poly[::-1]
        rails.append(poly.astype(np.float32))
    rails.sort(key=lambda r: (round(float(r[0][0]), 1), round(float(r[0][2]), 1)))
    log("  rails: %d (from %d flag-256 polygons in %d objects after removing the vertical posts; %d too short)" % (len(rails), len(polys), len(comp), len(comp) - len(rails)))
    return rails
