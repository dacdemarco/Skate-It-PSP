"""Emulate main.c start-up on the converted files: ground search + a scene render near the skater."""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import lvl_check, skt_check, render_level
LVL = "dist/SkateItPSP/data/amphitheatre.lvl"; SKT = "dist/SkateItPSP/data/skater_aaron.skt"

def tri_height(a, b, c, x, z):
    d = (b[2]-c[2])*(a[0]-c[0]) + (c[0]-b[0])*(a[2]-c[2])
    if abs(d) < 1e-9: return None
    l1 = ((b[2]-c[2])*(x-c[0]) + (c[0]-b[0])*(z-c[2]))/d
    l2 = ((c[2]-a[2])*(x-c[0]) + (a[0]-c[0])*(z-c[2]))/d
    l3 = 1-l1-l2
    if min(l1, l2, l3) < -1e-4: return None
    u = np.subtract(b, a); v = np.subtract(c, a); n = np.cross(u, v); ln = np.linalg.norm(n)
    if ln < 1e-9 or abs(n[1])/ln < 0.5: return None
    return l1*a[1] + l2*b[1] + l3*c[1]

def ground(d, x, z, ymax):
    best = None
    for p in d["cpolys"]:
        a, b, c = (d["cverts"][i] for i in p[:3])
        y = tri_height(a, b, c, x, z)
        if y is not None and y <= ymax and (best is None or y > best): best = y
        if p[3] != 0xFFFF:
            y = tri_height(a, d["cverts"][p[2]], d["cverts"][p[3]], x, z)
            if y is not None and y <= ymax and (best is None or y > best): best = y
    return best

L = lvl_check.load(LVL); h = L["header"]
bounds, start = h[12:18], h[18:21]
sx, sz = start[0], start[2]; sy = None
for ring in range(40):
    for k in range(8):
        a = k*0.785398; r = ring*2.0
        tx, tz = sx+math.cos(a)*r, sz+math.sin(a)*r
        y = ground(L, tx, tz, bounds[4]+5.0)
        if y is not None: sx, sz, sy = tx, tz, y; break
    if sy is not None: break
print("level bounds", [round(x) for x in bounds], "start", [round(x, 1) for x in start])
print("skater placed at x=%.1f y=%.2f z=%.1f (ring search found ground: %s)" % (sx, sy if sy is not None else -999, sz, sy is not None))
S = skt_check.load(SKT)
# scene: level triangles near the skater + skater frame 0 of anim 3
tex = [t["tex"] if False else None for t in []]
level_tex = []
for tx_ in L["texs"]:
    pal = np.zeros((len(tx_["clut"]), 4), np.uint8)
    for i, c in enumerate(tx_["clut"]):
        e = lambda v: (v << 3) | (v >> 2)
        pal[i] = (e(c & 31), e((c >> 5) & 31), e((c >> 10) & 31), 255 if c & 0x8000 else 0)
    level_tex.append(pal[np.array(tx_["idx"])].reshape(tx_["h"], tx_["w"], 4))
tris = []
for t, flags, first, count, _ in L["batches"]:
    if flags & 1: continue
    w, hh = (L["texs"][t]["w"], L["texs"][t]["h"]) if t != 0xFFFF else (1, 1)
    for i in range(first, first+count, 3):
        vs = L["verts"][i:i+3]
        if any(abs(v[3]-sx) < 9 and abs(v[5]-sz) < 9 for v in vs) and all(abs(v[3]-sx) < 40 and abs(v[5]-sz) < 40 for v in vs):
            tris.append(([v[3:6] for v in vs], [(v[0]*w, v[1]*hh) for v in vs], t if t != 0xFFFF else -1))
nl = len(level_tex)
level_tex.append(S["tex"])
for (p, uv, _) in skt_check.skin(S, 3, 10):
    pass
sk = skt_check.skin(S, 3, 10)
for pts, uvs, _ in sk:
    tris.append(([(q[0]+sx, q[1]+sy, q[2]+sz) for q in pts], uvs, nl))
print("scene triangles", len(tris), "(skater head top y above ground: %.2f m)" % max(q[1] for pts, _, _ in sk for q in pts))
S_ = os.path.join(os.environ.get("TEMP", "."), "scene.png")
# focus the orbit camera on the skater by adding two tiny anchor triangles around its centre (render() centres on the median)
render_level.render(level_tex, tris, S_, 200, 12, 70, W=900, H=560)
