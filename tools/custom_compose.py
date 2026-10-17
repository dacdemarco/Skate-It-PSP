#!/usr/bin/env python3
"""Custom skater (M11) prototype / reference: assembles a skater out of the parts of DATA/SKATERS/CUSTOM and shows it (tools/render_level.py software renderer).

The 256 x 128 atlas the parts share (found from the UV islands of the models; row 0 = the top; the DS wraps negative t, so row = 128 + t):
    (42,  0) 150 x 64   top texture (cardigan / hoodie / shirt / tshirt colours; palette entry 0 is transparent: the arms texture shows through where the sleeves are short)
    (42,  0)  22 x 64   arms_s<skin>   (pasted FIRST, under the top)
    (192, 0)  64 x 64   shoes
    (42, 64)  82 x 64   legs_s<skin> / legs_short_s<skin> (under), then the pants texture
    (124,64) 100 x 64   face_s<skin>_f<1|2>_<m|f>
    (192,64)  32 x 64   hair / beard overlay (.dsb: not decoded yet)      (224,64) 32 x 64 hat
usage: custom_compose.py <out.png> [gender m|f] [build a|l|s] [skin 1-4] [face 1-4] [hair 1-5] [top e.g. tshirt_plain_red] [pants e.g. pants_jeans_blue] [shoes file stem]
"""
import sys, os, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
import mbf, dst, render_level, pose_sheet

CU = "extracted/unpacked/DATA/SKATERS/CUSTOM"


def bmp(path):
    im = Image.open(path)
    pal = im.getpalette()
    rgba = np.array(im.convert("RGBA"))
    idx = np.array(im)
    if idx.ndim == 2:
        rgba[idx == 0, 3] = 0                                 # palette entry 0 = transparent
    return rgba


def paste(atlas, img, x, row):
    h, w = img.shape[:2]
    reg = atlas[row:row + h, x:x + w]
    m = img[..., 3] > 0
    reg[m] = img[m]


def compose(gender, build, skin, face, hair, top, pants, shoes):
    atlas = np.zeros((128, 256, 4), np.uint8)
    atlas[..., 3] = 255
    atlas[..., :3] = (255, 0, 255)
    paste(atlas, bmp("%s/Textures/Top/arms_s%d.bmp" % (CU, skin)), 42, 0)
    paste(atlas, bmp("%s/Textures/Top/%s.bmp" % (CU, top)), 42, 0)
    paste(atlas, bmp("%s/Textures/Shoes/%s.bmp" % (CU, shoes)), 192, 0)
    legs = "legs_short" if "short" in pants else "legs"
    paste(atlas, bmp("%s/Textures/Pants/%s_s%d.bmp" % (CU, legs, skin)), 42, 64)
    paste(atlas, bmp("%s/Textures/Pants/%s.bmp" % (CU, pants)), 42, 64)
    f = 1 if face <= 2 else 2
    paste(atlas, bmp("%s/Textures/Face/face_s%d_f%d_%s.bmp" % (CU, skin, f, gender)), 124, 64)
    return atlas


def part_tris(rel):
    return mbf.load_mbf("%s/Models/%s" % (CU, rel))["tris"]


def build_model(gender, build, face, hair, top_type, pants_type):
    tris = []
    tris += part_tris("Top/top_%s_%s_%s_lod1.mbf" % (top_type, gender, build))
    tris += part_tris("Pants/pants_%s_%s_%s_lod1.mbf" % (pants_type, gender, build))
    tris += part_tris("Face/face_hair_%s_%d_%d_lod1.mbf" % (gender, hair, face))
    return tris


def main():
    out = sys.argv[1]
    a = sys.argv[2:]
    gender = a[0] if len(a) > 0 else "m"
    build = a[1] if len(a) > 1 else "a"
    skin = int(a[2]) if len(a) > 2 else 1
    face = int(a[3]) if len(a) > 3 else 1
    hair = int(a[4]) if len(a) > 4 else 1
    top = a[5] if len(a) > 5 else "tshirt_plain_red"
    pants = a[6] if len(a) > 6 else "pants_jeans_blue"
    shoes = a[7] if len(a) > 7 else "shoes_genric_black"
    ttype = top.split("_")[0] if not top.startswith("shirts") else "shirt"
    ptype = {"pants_jeans": "trousers", "pants_khaki": "trousers", "pants_cargo": "cargo"}.get(pants.rsplit("_", 1)[0], "shorts")
    if pants.startswith("pants_shorts_short"):
        ptype = "shortshorts"
    atlas = compose(gender, build, skin, face, hair, top, pants, shoes)
    Image.fromarray(atlas).save(out.replace(".png", "_atlas.png"))
    tris = build_model(gender, build, face, hair, ttype, ptype)
    sk = mbf.load_mbf("%s/MODELS/skater_aaron_lod1.mbf" % mbf.SK)
    sk["tris"] = tris
    off = [tuple(c / 4096 for c in o) for o in mbf.load_bof(pose_sheet.ARC_S + "/animdb.BOF")]
    bd = mbf.load_mbf_multi("%s/MODELS/board_generic_lod0.mbf" % mbf.SK)
    tex = [atlas, np.full((2, 2, 4), (150, 90, 40, 255), np.uint8)]
    from PIL import ImageDraw
    sheet = Image.new("RGB", (3 * 300, 360))
    for i, yaw in enumerate((0, 90, 180)):
        pts = pose_sheet.pose_tris(sk, off, bd, 0, 0, 1)
        render_level.render(tex, pts, "_tmp.png", yaw, 8, 150, W=300, H=360, center=(0, 0.9, 0))
        sheet.paste(Image.open("_tmp.png").convert("RGB"), (i * 300, 0))
    os.remove("_tmp.png")
    sheet.save(out)
    print(out, len(tris), "triangles")


if __name__ == "__main__":
    main()
