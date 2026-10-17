#!/usr/bin/env python3
"""Custom skater v2 prototype (M11): the same assembly as custom_compose.py plus the overlays the .dsb files hold: hair colour, beards, hats and logos.
.dsb (DS bitmap with alpha, A3I5): u32 w, h, 2 unused; then the palette (u16 RGB555, 32 used entries out of the 64 stored) and w * h bytes: index = byte & 31, alpha = byte >> 5 (0..7).
usage: custom_compose2.py <out.png> key=value ...      keys: g=m|f b=a|l|s skin=1-4 face=1-4 tex=1|2 hair=<style|none> hcol=black|blonde|brown beard=<1-8|0> bcol=... hat=<style|none> hattex=<stem> logo=<stem> top= pants= shoes= yaw=
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
import mbf, render_level, pose_sheet
import custom_compose as cc

CU = cc.CU


def dsb(path):
    b = open(path, "rb").read()
    w, h = struct.unpack_from("<2I", b, 0)
    n = (len(b) - 16 - w * h) // 2
    pal = struct.unpack_from("<%dH" % n, b, 16)
    out = np.zeros((h, w, 4), np.uint8)
    pix = np.frombuffer(b, np.uint8, w * h, 16 + 2 * n).reshape(h, w)
    for y in range(h):
        for x in range(w):
            v = int(pix[y, x]); c = pal[v & 31] if (v & 31) < n else 0
            r, g, bb = c & 31, (c >> 5) & 31, (c >> 10) & 31
            out[y, x] = (r << 3 | r >> 2, g << 3 | g >> 2, bb << 3 | bb >> 2, (v >> 5) * 255 // 7)
    return out


def blend(atlas, img, x, row):
    h, w = img.shape[:2]
    reg = atlas[row:row + h, x:x + w].astype(np.float32)
    a = img[..., 3:4].astype(np.float32) / 255.0
    reg[..., :3] = reg[..., :3] * (1 - a) + img[..., :3].astype(np.float32) * a
    atlas[row:row + h, x:x + w] = reg.astype(np.uint8)


def main():
    out = sys.argv[1]
    o = dict(a.split("=", 1) for a in sys.argv[2:])
    g, b, skin, face, tex = o.get("g", "m"), o.get("b", "a"), int(o.get("skin", 1)), int(o.get("face", 1)), int(o.get("tex", 1))
    hair, hcol, beard, bcol = o.get("hair", "none"), o.get("hcol", "black"), int(o.get("beard", 0)), o.get("bcol", "brown")
    hat, hattex, logo = o.get("hat", "none"), o.get("hattex", "hat_baseballcap_dcblkwht"), o.get("logo", "")
    top, pants, shoes = o.get("top", "tshirt_plain_white"), o.get("pants", "pants_jeans_blue"), o.get("shoes", "shoes_genric_black")
    yaws = [float(v) for v in o.get("yaw", "0,90,180").split(",")]
    zoom, cy = float(o.get("zoom", 150)), float(o.get("cy", 0.9))
    atlas = np.zeros((128, 256, 4), np.uint8); atlas[..., 3] = 255; atlas[..., :3] = (255, 0, 255)
    cc.paste(atlas, cc.bmp("%s/Textures/Top/arms_s%d.bmp" % (CU, skin)), 42, 0)
    cc.paste(atlas, cc.bmp("%s/Textures/Top/%s.bmp" % (CU, top)), 42, 0)
    if logo:
        lx = int(o.get("lx", 128))
        blend(atlas, dsb("%s/Textures/Logos/%s.dsb" % (CU, logo)), lx, 0)
    cc.paste(atlas, cc.bmp("%s/Textures/Shoes/%s.bmp" % (CU, shoes)), 192, 0)
    legs = "legs_short" if "short" in pants else "legs"
    cc.paste(atlas, cc.bmp("%s/Textures/Pants/%s_s%d.bmp" % (CU, legs, skin)), 42, 64)
    cc.paste(atlas, cc.bmp("%s/Textures/Pants/%s.bmp" % (CU, pants)), 42, 64)
    cc.paste(atlas, cc.bmp("%s/Textures/Face/face_s%d_f%d_%s.bmp" % (CU, skin, tex, g)), 124, 64)
    if beard:
        blend(atlas, dsb("%s/Textures/Face/beard_%02d_%s.dsb" % (CU, beard, bcol)), 124, 64)
    if hair != "none":
        hn = o.get("hairtex", "01")
        blend(atlas, dsb("%s/Textures/Face/hair_%s_%s.dsb" % (CU, hn, hcol)), 124, 64)
    if hat != "none":
        cc.paste(atlas, cc.bmp("%s/Textures/Face/%s.bmp" % (CU, hattex)), 224, 64)
    Image.fromarray(atlas).save(out.replace(".png", "_atlas.png"))
    ttype = "shirt" if top.startswith("shirts") else top.split("_")[0]
    ptype = {"pants_jeans": "trousers", "pants_khaki": "trousers", "pants_cargo": "cargo"}.get(pants.rsplit("_", 1)[0], "shorts")
    if pants.startswith("pants_shorts_short"): ptype = "shortshorts"
    tris = cc.part_tris("Top/top_%s_%s_%s_lod1.mbf" % (ttype, g, b)) + cc.part_tris("Pants/pants_%s_%s_%s_lod1.mbf" % (ptype, g, b))
    tris += cc.part_tris("Face/face_hair_%s_1_%d_lod1_bald.mbf" % (g, face))
    if hair != "none":
        tris += cc.part_tris("Face/hair_%s_%s_lod1.mbf" % (hair, g))
    if hat != "none":
        tris += cc.part_tris("Face/hat_%s_%s_lod1.mbf" % (hat, g))
    sk = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
    sk["tris"] = tris
    off = [tuple(c / 4096 for c in v) for v in mbf.load_bof(pose_sheet.ARC_S + "/animdb.BOF")]
    bd = mbf.load_mbf_multi("%s/MODELS/board_generic_lod0.mbf" % mbf.SK)
    tx = [atlas, np.full((2, 2, 4), (150, 90, 40, 255), np.uint8)]
    W, H = 300, 360
    sheet = Image.new("RGB", (len(yaws) * W, H))
    for i, yaw in enumerate(yaws):
        pts = pose_sheet.pose_tris(sk, off, bd, 0, 0, 1)
        render_level.render(tx, pts, "_tmp2.png", yaw, 8, zoom, W=W, H=H, center=(0, cy, 0))
        sheet.paste(Image.open("_tmp2.png").convert("RGB"), (i * W, 0))
    os.remove("_tmp2.png")
    sheet.save(out)
    print(out, len(tris), "triangles")


if __name__ == "__main__":
    main()
