#!/usr/bin/env python3
"""Does the PSP GE drop level triangles near the camera? Model: triangles are clipped against the near plane, then any triangle with a
vertex whose screen coordinate is further than 2048 from the viewport centre (GE screen range 0..4095) is rejected.
usage: guard_check.py <level.lvl> eyeX eyeY eyeZ lookX lookY lookZ near [out.png]"""
import sys, struct, math
import numpy as np
from PIL import Image, ImageDraw

def load(path):
    b = open(path, "rb").read()
    h = struct.unpack_from("<4sI5I5I6f3f", b, 0)
    nverts, off_verts = h[4], h[9]
    V = np.frombuffer(b, dtype=np.dtype([("u", "<f4"), ("v", "<f4"), ("c", "<u4"), ("p", "<f4", 3)]), count=nverts, offset=off_verts)
    return V["p"].reshape(-1, 3, 3).astype(np.float64)

def view_basis(eye, look):
    f = look - eye; f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r)
    u = np.cross(r, f)
    return r, u, f

def failing(tris, eye, look, near, fy=136.0 / math.tan(math.radians(32.5)), far=3000.0):
    r, u, f = view_basis(eye, look)
    rel = tris - eye
    X = rel @ r; Y = rel @ u; D = rel @ f                  # depth along the view direction
    bad = np.zeros(len(tris), bool); polys = {}
    for i in range(len(tris)):
        d = D[i]
        if (d >= near).all():
            pts = [(X[i, k], Y[i, k], d[k]) for k in range(3)]
        elif (d < near).all():
            continue
        else:                                              # Sutherland-Hodgman against d = near
            pts = []
            for k in range(3):
                a = (X[i, k], Y[i, k], d[k]); b = (X[i, (k + 1) % 3], Y[i, (k + 1) % 3], d[(k + 1) % 3])
                ain, bin_ = a[2] >= near, b[2] >= near
                if ain: pts.append(a)
                if ain != bin_:
                    t = (near - a[2]) / (b[2] - a[2]); pts.append(tuple(a[j] + (b[j] - a[j]) * t for j in range(3)))
        sc = [(p[0] / p[2] * fy, p[1] / p[2] * fy) for p in pts]
        if any(abs(x) > 2048 or abs(y) > 2048 for x, y in sc):
            bad[i] = True; polys[i] = sc
    return bad, polys

def main():
    T = load(sys.argv[1]); eye = np.array(sys.argv[2:5], float); look = np.array(sys.argv[5:8], float); near = float(sys.argv[8])
    # only triangles within 40 m of the camera can matter
    c = T.mean(1); keep = np.where(np.linalg.norm(c - eye, axis=1) < 60)[0]
    T = T[keep]
    bad, polys = failing(T, eye, look, near)
    print("near %.2f: %d of %d nearby triangles would be dropped" % (near, bad.sum(), len(T)))
    if len(sys.argv) > 9:
        r, u, f = view_basis(eye, look); fy = 136.0 / math.tan(math.radians(32.5))
        img = Image.new("RGB", (480, 272), (70, 90, 110)); dr = ImageDraw.Draw(img)
        for i, sc in polys.items():
            pts = [(240 + x, 136 - y) for x, y in sc]
            dr.polygon(pts, fill=(30, 140, 240))
        img.save(sys.argv[9]); print("wrote", sys.argv[9])

if __name__ == "__main__":
    main()
