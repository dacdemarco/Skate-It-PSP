#!/usr/bin/env python3
"""Pack the parts of the original's create-a-skater (DATA/SKATERS/CUSTOM) into data/custom.pak for the PSP (src/custom.c).
usage: convert_custom.py [out.pak]          (version 3: + the deck and wheel art of the board and the original's item numbers for the gear that career unlocks)

The original builds a custom skater from parts that all share the skeleton of the pro skaters and ONE 256 x 128 texture atlas (tools/custom_compose.py documents the layout and renders a sample):
    (42,0) 150x64 top | (42,0) 22x64 arms skin under it | (192,0) 64x64 shoes | (42,64) 82x64 legs skin + pants | (124,64) 100x64 face | (192,64) hair / beard overlay | (224,64) hat
The PSP editor offers: gender, build, skin tone, face (4 head shapes x 2 face textures), hair (9 styles + none: separate meshes on a bald head, 3 colours), beard (8 + none, 3 colours), hat (6 styles + none, 20 designs), top, logo on the top (70 + none), bottom, shoes. Board art is not in it.
The overlays (.dsb) are A3I5 bitmaps (alpha 3 bits, palette index 5 bits) blended over the atlas: hair and beard (100 x 64) over the face at (124,64) - the hair meshes use the right-hand 32 columns of the hair overlay -, logos (64 x 64) over the FRONT of the top at (64,0), mirrored; hats (32 x 64, plain) at (224,64).

File (little endian):
  Header (64 B)  char magic[4] = 'SKCP'; u32 version = 2, nmesh, off_mesh, ntex, off_tex, size, pad[9]
  Mesh (32 B) * nmesh   u8 kind (0 top, 1 pants, 2 bald head, 3 hair, 4 hat), gender (0 m, 1 f), a (tops / pants: build 0 average, 1 large, 2 small; heads: face shape 0..3; hair: style 0..8; hats: style 0..5), variant (top type 0 cardigan, 1 hoodie, 2 shirt, 3 tshirt; pants type
                        0 cargo, 1 shorts, 2 shortshorts, 3 trousers); u32 nverts, off_verts, off_vbone; pad[4]      vertices as in a .skt (Vertex 24 B: f32 u, v (atlas pixels / 256, / 128), u32 abgr, f32 x, y, z), u8 bone each
  Texture (64 B) * ntex char name[32] (display name); u8 kind (0 arms, 1 top, 2 shoes, 3 legs, 4 pants, 5 face, 6 hair overlay, 7 beard overlay, 8 logo overlay, 9 hat, 10 deck art 42 x 105, 11 wheel art 42 x 23), sub, gender, skin; u8 w, h; u16 rom; u32 off_pix (w * h palette indices; kinds 6-8: A3I5 bytes), off_pal (256 * RGBA8888); pad[4]
                        rom = the item number of the original's unlock tables (kinds 2 shoes, 8 deck art, 7 wheel art, 2 hats: docs/CAMPAIGN_FORMAT.md section 8; read from the pointer tables in the ARM9 code, see rom_orders), 0xFFFF = not in them
                        hats (kind 9): sub = 0 baseball cap, 1 diver, 2 helmet
                        kinds 6 / 7: sub = the hair texture type 1 / 2 (curly / smooth) or the beard style 1..8, skin = the colour 1 black, 2 blonde, 3 brown
                        kind 1: sub = the model type it goes with; kind 3: sub 0 long / 1 short legs, skin 1..4; kind 4: sub = the pants type; kind 5: sub = face texture 1 / 2, gender, skin 1..4
                        Palette entry 0 of a top or pants texture is transparent (the skin texture below shows through).
"""
import sys, os, struct, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
import mbf
from convert_skater import shade_color

CU = "extracted/unpacked/DATA/SKATERS/CUSTOM"
TOP_TYPES = ["cardigan", "hoodie", "shirt", "tshirt"]
PANTS_TYPES = ["cargo", "shorts", "shortshorts", "trousers"]
ABBR = {"blk": "Black", "wht": "White", "grn": "Green", "rd": "Red", "bl": "Blue", "bwn": "Brown", "gry": "Grey", "pnk": "Pink", "org": "Orange", "ltd": "Ltd", "genric": "Generic", "addidas": "Adidas",
        "adiddas": "Adidas", "lakia": "Lakai", "laki": "Lakai", "dannywayred": "Danny Way Red", "robdyrdekblk": "Rob Dyrdek Black", "erickostonltd": "Eric Koston Ltd", "mcarrollblk": "Mike Carroll Black",
        "boardflipb": "Board Flip", "boardflipbbb": "Board Flip 2", "boardflipvbb": "Board Flip 3", "icecream": "Ice Cream", "dc": "DC", "telfordblkwhite": "Telford", "lucas puig": "Lucas Puig"}


ARM9 = "extracted/decomp/arm9.bin"
GEAR_TABLE = (0x02083BA0, 0x020840A4)          # the gear name tables of the USA ARM9: tops, logos, bottoms, shoes, hair, hats, beards, decks, wheels (pointers to the file names, in the order of the unlock numbers)


def rom_orders():
    """the original's item numbers: {'shoes': {name: n}, 'hats': {name: n}, 'decks': {...}, 'wheels': {...}} with the names lower case and without .bmp. Shoes, decks and wheels count from 0 in the table order
    (checked against the sponsors' unlock sets: DC gives shoes 7 - 11 and the table holds DC's five shoes there ...); hats count from 1 (0 = no hat)."""
    b = open(ARM9, "rb").read()
    base = 0x02000000
    out = {"shoes": {}, "hats": {}, "decks": {}, "wheels": {}}
    n = {k: 0 for k in out}
    n["hats"] = 1
    for off in range(GEAR_TABLE[0] - base, GEAR_TABLE[1] - base, 4):
        ptr, = struct.unpack_from("<I", b, off)
        if not base <= ptr < base + len(b):
            continue
        end = b.index(0, ptr - base)
        name = b[ptr - base:end].decode("latin1")
        low = name.lower()
        if not low.endswith(".bmp"):
            continue
        stem = low[:-4]
        key = ("shoes" if low.startswith(("shoe_", "shoes_")) else "hats" if low.startswith("hat_") else "decks" if low.startswith("board_") else "wheels" if low.startswith("wheels_") else None)
        if key:
            out[key][stem] = n[key]
            n[key] += 1
    return out


def pretty(stem):
    parts = [p for p in stem.replace(" ", "_").split("_") if p]
    return " ".join(ABBR.get(p.lower(), p.capitalize()) for p in parts)[:31]


def load_part(path):
    m = mbf.load_mbf(path)
    verts, vbone = bytearray(), bytearray()
    for tri in m["tris"]:
        for pos, uv, nrm, bone in tri:
            verts += struct.pack("<ffI3f", uv[0] / 256.0, uv[1] / 128.0, shade_color(nrm), *pos)
            vbone.append(bone)
    return bytes(verts), bytes(vbone), len(m["tris"]) * 3


def load_tex(path):
    im = Image.open(path)
    assert im.mode == "P", path
    pal = im.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    rgba = b"".join(bytes((pal[3 * i], pal[3 * i + 1], pal[3 * i + 2], 255)) for i in range(256))
    return im.size[0], im.size[1], bytes(im.tobytes()), rgba


def load_dsb(path):
    b = open(path, "rb").read()
    w, h = struct.unpack_from("<2I", b, 0)
    n = (len(b) - 16 - w * h) // 2
    pal = struct.unpack_from("<%dH" % n, b, 16)
    rgba = bytearray()
    for i in range(256):
        c = pal[i] if i < n else 0
        r, g, bb = c & 31, (c >> 5) & 31, (c >> 10) & 31
        rgba += bytes((r << 3 | r >> 2, g << 3 | g >> 2, bb << 3 | bb >> 2, 255))
    assert w < 256 and h < 256
    return w, h, bytes(b[16 + 2 * n:16 + 2 * n + w * h]), bytes(rgba)


HAIR_STYLES = ["afro", "curtains", "emo", "fauxhawk", "fringe", "long", "longtiedback", "mohawk", "shortbald"]
HAT_STYLES = ["capforwards", "capbackwards", "caplonghair", "fedora", "flatcap", "helmet"]
COLOURS = ["black", "blonde", "brown"]


def main(out):
    meshes, texs = [], []
    rom = rom_orders()
    rom["decks"].update({"board_white_custom": 58, "board_whitedirt_custom": 59})        # the two blank boards of the default set 4 (items 58, 59; they are not in the name table: the art editor's canvases)
    assert len(rom["shoes"]) == 24 and len(rom["hats"]) == 20 and len(rom["decks"]) == 60 and len(rom["wheels"]) == 52, {k: len(v) for k, v in rom.items()}

    def rom_of(kind, f):
        return rom[kind].get(os.path.splitext(os.path.basename(f))[0].lower(), 0xFFFF)
    for ti, t in enumerate(TOP_TYPES):
        for g, gn in enumerate("mf"):
            for b, bn in enumerate("als"):
                meshes.append((0, g, b, ti, load_part("%s/Models/Top/top_%s_%s_%s_lod1.mbf" % (CU, t, gn, bn))))
    for pi, t in enumerate(PANTS_TYPES):
        for g, gn in enumerate("mf"):
            for b, bn in enumerate("als"):
                meshes.append((1, g, b, pi, load_part("%s/Models/Pants/pants_%s_%s_%s_lod1.mbf" % (CU, t, gn, bn))))
    for g, gn in enumerate("mf"):
        for m in range(4):
            meshes.append((2, g, m, 0, load_part("%s/Models/Face/face_hair_%s_1_%d_lod1_bald.mbf" % (CU, gn, m + 1))))
        for hi, hs in enumerate(HAIR_STYLES):
            meshes.append((3, g, hi, 0, load_part("%s/Models/Face/hair_%s_%s_lod1.mbf" % (CU, hs, gn))))
        for hi, hs in enumerate(HAT_STYLES):
            path = "%s/Models/Face/hat_%s_%s_lod1.mbf" % (CU, hs, gn)
            if os.path.exists(path):
                meshes.append((4, g, hi, 0, load_part(path)))
    for s in range(1, 5):
        texs.append(("Skin %d" % s, 0, 0, 0, s, load_tex("%s/Textures/Top/arms_s%d.bmp" % (CU, s))))
    for f in sorted(glob.glob(CU + "/Textures/Top/*.bmp")):
        stem = os.path.splitext(os.path.basename(f))[0]
        if stem.startswith("arms_"):
            continue
        ty = 2 if stem.startswith("shirts_") else (3 if stem.startswith("tshirt") else (1 if stem.startswith("hoodie") else 0))
        texs.append((pretty(stem.replace("shirts_", "shirt_").replace("tshirt_", "t-shirt_")), 1, ty, 0, 0, load_tex(f)))
    for f in sorted(glob.glob(CU + "/Textures/Shoes/*.bmp")):
        texs.append((pretty(os.path.splitext(os.path.basename(f))[0].replace("shoes_", "").replace("shoe_", "").replace("Shoes_", "")), 2, 0, 0, 0, load_tex(f), rom_of("shoes", f)))
    for s in range(1, 5):
        texs.append(("Legs %d" % s, 3, 0, 0, s, load_tex("%s/Textures/Pants/legs_s%d.bmp" % (CU, s))))
        texs.append(("Short legs %d" % s, 3, 1, 0, s, load_tex("%s/Textures/Pants/legs_short_s%d.bmp" % (CU, s))))
    for f in sorted(glob.glob(CU + "/Textures/Pants/pants_*.bmp")):
        stem = os.path.splitext(os.path.basename(f))[0]
        if stem.startswith("pants_cargo_"): ty = 0
        elif stem.startswith("pants_jeans_") or stem.startswith("pants_khaki_"): ty = 3
        elif stem.startswith("pants_shorts_short"): ty = 2
        else: ty = 1
        texs.append((pretty(stem[len("pants_"):]), 4, ty, 0, 0, load_tex(f)))
    for ti in (1, 2):
        for ci, cn in enumerate(COLOURS):
            w, h, pix, pal = load_dsb("%s/Textures/Face/hair_%02d_%s.dsb" % (CU, ti, cn))
            texs.append(("Hair %d %s" % (ti, cn), 6, ti, 0, ci + 1, (w, h, pix, pal)))
    for bi in range(1, 9):
        for ci, cn in enumerate(COLOURS):
            w, h, pix, pal = load_dsb("%s/Textures/Face/beard_%02d_%s.dsb" % (CU, bi, cn))
            texs.append(("Beard %d %s" % (bi, cn), 7, bi, 0, ci + 1, (w, h, pix, pal)))
    for f in sorted(glob.glob(CU + "/Textures/Logos/*.dsb")):
        w, h, pix, pal = load_dsb(f)
        texs.append((pretty(os.path.splitext(os.path.basename(f))[0]), 8, 0, 0, 0, (w, h, pix, pal)))
    for f in sorted(glob.glob(CU + "/Textures/Face/hat_*.bmp")):
        stem = os.path.splitext(os.path.basename(f))[0]
        fam = 1 if stem.startswith("hat_diver") else (2 if stem.startswith("hat_helmet") else 0)
        texs.append((pretty(stem[len("hat_"):]), 9, fam, 0, 0, load_tex(f), rom_of("hats", f)))
    for f in sorted(glob.glob(CU + "/Textures/Board/board_*.bmp")):                  # the art of the deck (42 x 105) and of the wheels (42 x 23): the board's texture is made of one of each
        texs.append((pretty(os.path.splitext(os.path.basename(f))[0][len("board_"):]), 10, 0, 0, 0, load_tex(f), rom_of("decks", f)))
    for f in sorted(glob.glob(CU + "/Textures/Board/wheels_*.bmp")):
        texs.append((pretty(os.path.splitext(os.path.basename(f))[0][len("wheels_"):]), 11, 0, 0, 0, load_tex(f), rom_of("wheels", f)))
    for f in sorted(glob.glob(CU + "/Textures/Face/face_s*.bmp")):
        stem = os.path.splitext(os.path.basename(f))[0]                      # face_s1_f2_m
        _, s, fv, g = stem.split("_")
        texs.append(("Face %s" % fv[1:], 5, int(fv[1:]), 0 if g == "m" else 1, int(s[1:]), load_tex(f)))
    off = 64
    off_mesh = off; off += 32 * len(meshes)
    off_tex = off; off += 64 * len(texs)
    blobs = []

    def place(data, align=16):
        nonlocal off
        off = (off + align - 1) & ~(align - 1)
        o = off; blobs.append((o, data)); off += len(data)
        return o
    mesh_ents = bytearray()
    for kind, g, b, var, (verts, vbone, nv) in meshes:
        ov, ob = place(verts), place(vbone)
        mesh_ents += struct.pack("<4BIII16x", kind, g, b, var, nv, ov, ob)
    tex_ents = bytearray()
    for t in texs:
        name, kind, sub, g, skin, (w, h, pix, pal) = t[:6]
        romn = t[6] if len(t) > 6 else 0xFFFF
        assert w < 256 and h < 256
        op, opal = place(pix), place(pal)
        tex_ents += struct.pack("<32s4B2BHII16x", name.encode("ascii", "replace"), kind, sub, g, skin, w, h, romn, op, opal)
    size = (off + 15) & ~15
    buf = bytearray(size)
    buf[:64] = struct.pack("<4sIIIIII36x", b"SKCP", 3, len(meshes), off_mesh, len(texs), off_tex, size)
    buf[off_mesh:off_mesh + len(mesh_ents)] = mesh_ents
    buf[off_tex:off_tex + len(tex_ents)] = tex_ents
    for o, d in blobs:
        buf[o:o + len(d)] = d
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "wb").write(buf)
    kinds = {}
    for t in texs: kinds[t[1]] = kinds.get(t[1], 0) + 1
    print("%s: %d meshes, %d textures %s, %.2f MB" % (out, len(meshes), len(texs), kinds, size / 1048576))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "dist/SkateItPSP/data/custom.pak")
