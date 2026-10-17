#!/usr/bin/env python3
"""Find grindable ledges in a level's collision polygons (used by convert_level.py).

The data has no ledge markers (collision flags 4/8/16/32 are surface materials, tools/flag_survey.py), so a ledge is found from its shape: the convex top edge of a block =
an edge shared by a flat polygon (|n.y| >= TOP_NY: the top) and a steep polygon (|n.y| < WALL_NY: the side) whose other corners hang LOWER than the edge by
MIN_DROP..MAX_DROP (a wall rising from a floor into a concave corner is not a ledge: its other corners are higher). Edges that continue each other in a straight line
are merged; runs shorter than MIN_LEN are dropped. Each ledge is returned as a 2-point polyline ordered so that the TOP surface is on the LEFT of the direction of travel
(left = up x dir), which lets the runtime tell the grindable side from the drop side.

Flag-256 (rails), 512 (poles) and the walls synthesized by tools/collfix.py (0x8000 / 0x4000) are ignored.
usage: ledges.py <level.lvl>      (lists the ledges)"""
import sys, struct, collections
import numpy as np

SKIP_FLAGS = 256 | 512 | 0x8000 | 0x4000
TOP_NY = 0.9
WALL_NY = 0.15
MIN_DROP = 0.2
MAX_DROP = 1.2
MIN_LEN = 2.0
MAX_BEND_COS = 0.985          # edges merge when their directions agree to ~10 degrees
JOIN = 0.08                   # endpoints closer than this are the same point


def _key(p):
    return (int(round(p[0] * 50.0)), int(round(p[1] * 50.0)), int(round(p[2] * 50.0)))


def extract_ledges(cverts, collpolys, log=print):
    cv = np.array(cverts, float)
    polys = []
    for p in collpolys:
        if p[4] & SKIP_FLAGS:
            continue
        ids = [i for i in p[:4] if i != 0xFFFF]
        if len(ids) < 3:
            continue
        P = cv[ids]
        n = np.cross(P[1] - P[0], P[2] - P[0])
        ln = np.linalg.norm(n)
        if ln < 1e-9:
            continue
        ny = abs(n[1] / ln)
        polys.append((P, ny))
    edges = collections.defaultdict(list)                 # quantized endpoint pair -> [(poly index, other-corner mean y, the edge)]
    for pi, (P, ny) in enumerate(polys):
        n = len(P)
        for k in range(n):
            a, b = P[k], P[(k + 1) % n]
            others = [P[j] for j in range(n) if j != k and j != (k + 1) % n]
            oy = float(np.mean([o[1] for o in others]))
            ka, kb = _key(a), _key(b)
            if ka == kb:
                continue
            oc = np.mean(others, axis=0)
            e = (b - a) / max(np.linalg.norm(b - a), 1e-9)
            w = oc - 0.5 * (a + b)
            wp = w - e * float(np.dot(w, e))                      # the offset of the polygon's far side, measured ACROSS the edge
            across = float(wp[1]) / max(float(np.linalg.norm(wp)), 0.05)       # the slope of the surface away from the edge (sin of the angle)
            edges[(min(ka, kb), max(ka, kb))].append((pi, oy, a, b, oc, across))
    segs = []                                             # (a, b, top_centroid_xz, drop)
    for key, lst in edges.items():
        tops = [e for e in lst if polys[e[0]][1] >= TOP_NY]
        walls = [e for e in lst if polys[e[0]][1] < WALL_NY]
        if not tops or not walls:
            continue
        for w in walls:
            a, b = w[2], w[3]
            ey = 0.5 * (a[1] + b[1])
            drop = ey - w[1]
            if not (MIN_DROP <= drop <= MAX_DROP):
                continue
            for t in tops:
                if t[1] < ey - 0.12:                      # the top must not fall away below the edge (a floor under an overhang)
                    continue
                if abs(t[1] - ey) > 1.0 and abs(t[5]) > 0.25:          # a "top" that climbs steeply away from the edge is a ramp, not a ledge top (measured ACROSS the edge: a curb whose top slopes
                    continue                                           # along its length, like the ones beside a staircase, has a far corner much higher but is still a ledge top)
                if abs(a[1] - b[1]) > 0.25 * np.linalg.norm(a - b):      # a ledge edge is level
                    continue
                segs.append((a, b, t[4], drop))
                break
    log("  ledges: %d candidate edges" % len(segs))
    # merge colinear touching segments: greedy chains over endpoints
    used = [False] * len(segs)
    out = []
    order = sorted(range(len(segs)), key=lambda i: -np.linalg.norm(segs[i][0] - segs[i][1]))
    for i0 in order:
        if used[i0]:
            continue
        used[i0] = True
        a, b, top, drop = segs[i0]
        d = (b - a) / max(np.linalg.norm(b - a), 1e-9)
        pts = [a, b]
        tops = [top]
        drops = [drop]
        grew = True
        while grew:
            grew = False
            for j in range(len(segs)):
                if used[j]:
                    continue
                c, e, tj, dj = segs[j]
                dj_dir = (e - c) / max(np.linalg.norm(e - c), 1e-9)
                if abs(float(np.dot(dj_dir, d))) < MAX_BEND_COS:
                    continue
                # the candidate must lie on the chain's line (within JOIN) and touch one of its ends
                for end, other in ((0, 1), (1, 0)):
                    for q, r in ((c, e), (e, c)):
                        tail = pts[-1] if end else pts[0]
                        if np.linalg.norm(q - tail) <= JOIN:
                            if end:
                                pts.append(r)
                            else:
                                pts.insert(0, r)
                            used[j] = True; grew = True
                            tops.append(tj); drops.append(dj)
                            break
                    if used[j]:
                        break
        P = np.array(pts)
        c0 = P.mean(0)
        _, _, vt = np.linalg.svd(P - c0, full_matrices=False)
        ax = vt[0]
        tt = (P - c0) @ ax
        lo, hi = P[int(np.argmin(tt))], P[int(np.argmax(tt))]
        length = float(np.linalg.norm(hi - lo))
        if length < MIN_LEN:
            continue
        # orient so that the top is on the left: left = up x dir
        dirv = (hi - lo) / length
        left = np.cross([0.0, 1.0, 0.0], dirv)
        mid = 0.5 * (lo + hi)
        inside = np.mean(tops, axis=0) - mid
        if float(np.dot(left, inside)) < 0.0:
            lo, hi = hi, lo
        out.append((np.array([lo, hi], np.float32), float(np.mean(drops))))
    out.sort(key=lambda r: (round(float(r[0][0][0]), 1), round(float(r[0][0][2]), 1)))
    log("  ledges: %d (>= %.1f m, drop %.2f..%.2f m)" % (len(out), MIN_LEN, MIN_DROP, MAX_DROP))
    return [o[0] for o in out], [o[1] for o in out]


def load_lvl(path):
    b = open(path, "rb").read()
    h = struct.unpack_from("<4sI5I5I6f3f3I", b, 0)
    ncv, ncp, off_cv, off_cp = h[5], h[6], h[10], h[11]
    cv = [struct.unpack_from("<3f", b, off_cv + 12 * i) for i in range(ncv)]
    cp = [struct.unpack_from("<4HI", b, off_cp + 12 * i) for i in range(ncp)]
    return cv, cp


if __name__ == "__main__":
    cv, cp = load_lvl(sys.argv[1])
    L, D = extract_ledges(cv, cp)
    tot = 0.0
    for i, (p, d) in enumerate(zip(L, D)):
        ln = float(np.linalg.norm(p[1] - p[0])); tot += ln
        print("%3d  (%7.1f %6.2f %7.1f) -> (%7.1f %6.2f %7.1f)  len %5.1f  drop %.2f" % (i, *p[0], *p[1], ln, d))
    print("total length %.0f m" % tot)
