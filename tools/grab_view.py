#!/usr/bin/env python3
"""Board-frame views of pose-library clips: the board is always drawn level (x width/toe side, y up, z nose), the camera looks from the toe side ('T'),
the nose ('N') or from above ('U'). usage: grab_view.py out.png views clip[:frame] ...   (views e.g. TN)"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image, ImageDraw
import mbf, dst, render_level, grab_classify as gc

TEX = None
def tex():
    global TEX
    if TEX is None:
        t = dst.decode(open("%s/TEXTURES/aaron.dst" % mbf.SK, "rb").read())
        TEX = [np.asarray(dst.to_image(t).convert("RGBA")), np.full((2, 2, 4), (150, 90, 40, 255), np.uint8)]
    return TEX

VIEWS = {  # board local (x,y,z) -> viewer (X right, Y up, Z away)
    "T": (lambda p: (-p[2], p[1], -p[0]), 0, 0),
    "N": (lambda p: (p[0], p[1], -p[2]), 0, 0),
    "U": (lambda p: (p[2], p[0], -p[1]), 0, 0),   # looking down on the deck from above, nose right, toe side up on screen
}

def tris_for(n, f, view):
    loc, nodes, nf = gc.skin(n, f)
    m = VIEWS[view][0]
    out = []
    for i in range(0, len(loc), 3):
        out.append(([m(loc[i + k]) for k in range(3)], [(0, 0)] * 3, 0))
    # real uvs
    k = 0
    uv = [v[1] for t in gc.SK["tris"] for v in t]
    out = [(o[0], uv[i * 3:i * 3 + 3], 0) for i, o in enumerate(out)]
    for mat, ts in gc.BD["sections"]:
        for t in ts:
            out.append(([m(gc.S * np.array(v[0])) for v in t], [(0, 0)] * 3, 1))
    return out

def sheet(clips, views, outp, W=240, H=260, zoom=125):
    img = Image.new("RGB", (len(views) * W, len(clips) * H), (28, 30, 42))
    for r, c in enumerate(clips):
        n, f = (c if isinstance(c, tuple) else (c, 99))
        for k, v in enumerate(views):
            tr = tris_for(n, f, v)
            ctr = (0, 0.55, 0) if v != "U" else (0, 0, 0.0)
            render_level.render(tex(), tr, "_tmpv.png", 0, 0, zoom, W=W, H=H, center=(0, 0.7, 0) if v != "U" else (0, 0.0, 0.5))
            im = Image.open("_tmpv.png").convert("RGB")
            t = gc.adb(n)
            ImageDraw.Draw(im).text((3, 3), "%d %s g%d a=%x b=%x" % (n, v, t["group"], t["a"], t["b"]), fill=(255, 255, 0))
            img.paste(im, (k * W, r * H))
    os.remove("_tmpv.png")
    img.save(outp)

if __name__ == "__main__":
    outp, views = sys.argv[1], sys.argv[2]
    clips = []
    for a in sys.argv[3:]:
        if ":" in a: n, f = a.split(":"); clips.append((int(n), int(f)))
        else: clips.append(int(a))
    sheet(clips, views, outp)
