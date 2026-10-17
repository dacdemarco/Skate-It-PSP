import sys, math, struct
sys.path.insert(0, "tools")
import numpy as np
import guard_check as gc

def subdivide_tris(T, max_edge):
    out = []
    stack = [t for t in T]
    while stack:
        t = stack.pop()
        e = [np.linalg.norm(t[(i + 1) % 3] - t[i]) for i in range(3)]
        k = int(np.argmax(e))
        if e[k] <= max_edge:
            out.append(t); continue
        a, b, c = t[k], t[(k + 1) % 3], t[(k + 2) % 3]
        m = (a + b) / 2
        stack.append(np.array([a, m, c])); stack.append(np.array([m, b, c]))
    return np.array(out)

def visible_drops(T, eye, look, near):
    c = T.mean(1); keep = np.where(np.linalg.norm(c - eye, axis=1) < 70)[0]
    bad, polys = gc.failing(T[keep], eye, look, near)
    n = 0
    for i, sc in polys.items():
        xs = [p[0] for p in sc]; ys = [p[1] for p in sc]
        if max(xs) > -240 and min(xs) < 240 and max(ys) > -136 and min(ys) < 136: n += 1
    return n

if __name__ == "__main__":
    T0 = gc.load("dist/SkateItPSP/data/amphitheatre.lvl")
    # only triangles that are not the far backdrop (keep playable area), the backdrop is far from every camera anyway
    rng = np.random.default_rng(5)
    cams = []
    for _ in range(40):
        x = rng.uniform(-70, 10); z = rng.uniform(-10, 60); y = -65.1 + rng.uniform(1.0, 2.6)
        yaw = rng.uniform(0, 2 * math.pi); pitch = rng.uniform(-0.35, 0.05)
        d = np.array([math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch)])
        cams.append((np.array([x, y, z]), np.array([x, y, z]) + d * 4))
    print("cameras with at least one visible dropped triangle (of 40), and total triangle count")
    for me in (1e9, 14, 6, 4, 3):
        T = T0 if me > 1e8 else subdivide_tris(T0, me)
        row = []
        for near in (0.3, 0.5, 1.0):
            bad = sum(1 for e, l in cams if visible_drops(T, e, l, near) > 0)
            row.append("near %.1f: %2d" % (near, bad))
        print("max edge %-5s tris %6d | %s" % ("inf" if me > 1e8 else me, len(T), "  ".join(row)))
