#!/usr/bin/env python3
"""Software-textured preview of one level: python render_level.py <LevelName> <out.png> [yaw pitch zoom]
Draws every leaf of layers 1,2,4 (not the 7_ backdrop) with its DST1 texture, z-buffered, UV wrap = repeat.
Unresolved texture ids are drawn flat magenta so gaps are visible."""
import sys, os, glob, math, struct
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import oct as octm, dst, pvs_tex
from PIL import Image


def load_level(name):
    lvl = "extracted/unpacked/DATA/LOCATIONS/%s/%s/" % (name, name)
    tex = []
    for path in pvs_tex.resolve_textures(name):     # index = texture id (PVS table; sky appended last)
        if path is None:
            tex.append(None); continue
        b = open(path, "rb").read()
        tex.append(None if len(b) < 18 else np.asarray(dst.to_image(dst.decode(b)).convert("RGBA")))
    nverts = len(octm.load_cmn(lvl + "verts.cmn"))
    tris = []   # (world verts[3], uv[3], tex_id)
    for f in sorted(glob.glob(lvl + "*.oct")):
        if os.path.basename(f).startswith("7_"):
            continue
        b = open(f, "rb").read(); S = octm.scale(b); O = octm.origin(b)
        for lf in octm.leaves(b, nverts):
            for tid, ts in octm.batches(b, lf):
                for t in ts:
                    tris.append(([tuple(O[a] + v[0][a] * S for a in range(3)) for v in t],
                                 [v[1] for v in t], tid))
    return tex, tris


def render(tex, tris, out, yaw=35, pitch=50, zoom=3.4, W=1000, H=760, center=None):
    yaw, pitch = math.radians(yaw), math.radians(pitch)
    pts = np.array([p for t, _, _ in tris for p in t])
    cx = np.median(pts, axis=0) if center is None else np.array(center, float)
    cy_, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)

    def proj(p):
        x, y, z = p[0] - cx[0], p[1] - cx[1], p[2] - cx[2]
        x, z = x * cy_ + z * sy, -x * sy + z * cy_
        y, z = y * cp - z * sp, y * sp + z * cp
        return W / 2 + x * zoom, H / 2 - y * zoom, z
    img = np.zeros((H, W, 3), np.uint8); img[:] = (30, 34, 46)
    zbuf = np.full((H, W), 1e9, np.float32)
    for verts, uvs, tid in tris:
        q = [proj(v) for v in verts]
        xs = [p[0] for p in q]; ys = [p[1] for p in q]
        x0, x1 = max(int(min(xs)), 0), min(int(max(xs)) + 1, W - 1)
        y0, y1 = max(int(min(ys)), 0), min(int(max(ys)) + 1, H - 1)
        if x0 > x1 or y0 > y1 or (x1 - x0) * (y1 - y0) > 400000:
            continue
        (ax, ay, az), (bx, by, bz), (cx2, cy2, cz) = q
        den = (by - cy2) * (ax - cx2) + (cx2 - bx) * (ay - cy2)
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        l1 = ((by - cy2) * (gx - cx2) + (cx2 - bx) * (gy - cy2)) / den
        l2 = ((cy2 - ay) * (gx - cx2) + (ax - cx2) * (gy - cy2)) / den
        l3 = 1 - l1 - l2
        m = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
        if not m.any():
            continue
        z = l1 * az + l2 * bz + l3 * cz
        m &= z < zbuf[y0:y1 + 1, x0:x1 + 1]
        if not m.any():
            continue
        T = tex[tid] if 0 <= tid < len(tex) else None
        if T is None:
            col = np.zeros(m.shape + (3,), np.uint8); col[:] = (255, 0, 255)
            alpha = np.ones(m.shape, bool)
        else:
            h, w = T.shape[:2]
            u = (l1 * uvs[0][0] + l2 * uvs[1][0] + l3 * uvs[2][0])
            v = (l1 * uvs[0][1] + l2 * uvs[1][1] + l3 * uvs[2][1])
            px = T[np.mod(np.floor(v).astype(int), h), np.mod(np.floor(u).astype(int), w)]
            col, alpha = px[..., :3], px[..., 3] > 0
        m &= alpha
        sub = img[y0:y1 + 1, x0:x1 + 1]; zs = zbuf[y0:y1 + 1, x0:x1 + 1]
        sub[m] = col[m]; zs[m] = z[m]
    Image.fromarray(img).save(out)


if __name__ == "__main__":
    name, out = sys.argv[1], sys.argv[2]
    yaw, pitch, zoom = (float(x) for x in sys.argv[3:6]) if len(sys.argv) >= 6 else (35, 50, 3.4)
    tex, tris = load_level(name)
    print("textures", len(tex), "triangles", len(tris))
    render(tex, tris, out, yaw, pitch, zoom)
