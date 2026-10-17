#!/usr/bin/env python3
"""Pack the original's front-end art and fonts into data/ui.pak for the PSP (src/ui.c).
usage: convert_ui.py [out.pak]

File (little endian):
  Header (32 B): char magic[4] = 'SKUI'; u32 version = 1; u32 nimg, off_img; u32 nfont, off_font; u32 size; u32 pad
  Image entry (48 B) * nimg:  char name[24]; u16 w, h (the picture); u16 tw, th (the texture: power-of-two sizes, edge pixels repeated); u8 fmt (0 = T8 with a 256 entry
      RGBA8888 palette, 1 = RGBA8888); 3 pad; u32 off_pix, off_clut (0 for fmt 1); u32 pad
      T8 pixels: tw * th bytes, palette indices as in the BMP (the magenta key colour has alpha 0); clut: 256 * 4 bytes R,G,B,A
  Font entry (32 B) * nfont:  char name[16]; u8 height; u8 pad; u16 tw, th (atlas size); u32 off_pix (T8: tw * th bytes, the XFN pixel values 0..7: 0 = clear, 1 = fill,
      2..6 = shading towards the outline, 7 = outline); u32 off_glyphs (256 * {u16 x, y; u8 w; u8 advance}); (2 pad bytes before the offsets)
      A glyph with advance 0 does not exist; a glyph with w 0 and an advance is a space.
Sources: extracted/unpacked/DATA (BMPs: magenta 255,0,255 is the transparent colour), extracted/files/DATA/FONTS (XFN, tools/campaign_xfn.py)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
import campaign_xfn

ROOT = "extracted/unpacked/DATA"
FONTS = "extracted/files/DATA/FONTS"
IMAGES = [
    ("title", "IMAGES/BGS/MainTitle.bmp"), ("bg_top", "IMAGES/BGS/SktF_BG_TOP_v1.bmp"), ("bg_bottom", "IMAGES/BGS/SktF_BG_BOTTOM_v1.bmp"),
    ("bg2_top", "IMAGES/BGS/SktF_BG2_TOP_v1.bmp"), ("bg_dim", "IMAGES/BGS/SktF_BG_DIMMED.bmp"), ("actimagine", "IMAGES/BGS/ActimagineOriginal.bmp"),
    ("hdr1", "FRONTEND/COMP_TITLE/Skt_Header_1.bmp"), ("hdr2", "FRONTEND/COMP_TITLE/Skt_Header_2.bmp"),
    ("hub1row", "FRONTEND/COMP_HUBLIST/Skt_Hub_1Row.bmp"), ("hub2col", "FRONTEND/COMP_HUBLIST/Skt_Hub_2Col.bmp"), ("hub3col", "FRONTEND/COMP_HUBLIST/Skt_Hub_3Col.bmp"),
    ("hub3row", "FRONTEND/COMP_HUBLIST/Skt_Hub_3Row.bmp"), ("hub4col", "FRONTEND/COMP_HUBLIST/Skt_Hub_4Col.bmp"),
    ("hubgo", "FRONTEND/COMP_HUBLIST/Skt_Hub_GoButton.bmp"), ("hubgo_hl", "FRONTEND/COMP_HUBLIST/Skt_Hub_GoButton_HL.bmp"),
    ("hubgo_pr", "FRONTEND/COMP_HUBLIST/Skt_Hub_GoButton_Pressed.bmp"), ("hubgo_pr_hl", "FRONTEND/COMP_HUBLIST/Skt_Hub_GoButton_Pressed_HL.bmp"),
    ("locbox", "FRONTEND/BITS/SktF_locationbox_v1.bmp"),
    ("list_top", "FRONTEND/COMP_SCROLLLIST/Skt_list_Top.bmp"), ("list_top_nosub", "FRONTEND/COMP_SCROLLLIST/Skt_list_Top_NoSub.bmp"), ("list_sub", "FRONTEND/COMP_SCROLLLIST/Skt_list_Sub.bmp"),
    ("list_left", "FRONTEND/COMP_SCROLLLIST/Skt_list_Left.bmp"), ("list_right", "FRONTEND/COMP_SCROLLLIST/Skt_list_Right.bmp"), ("list_bottom", "FRONTEND/COMP_SCROLLLIST/Skt_list_Bottom.bmp"),
    ("list_arrow_prev", "FRONTEND/COMP_SCROLLLIST/Skt_list_SubArrow_Prev.bmp"), ("list_arrow_next", "FRONTEND/COMP_SCROLLLIST/Skt_list_SubArrow_Next.bmp"),
    ("scroll_up", "FRONTEND/COMP_SCROLLLIST/Skt_list_ScrollArrow_Up.bmp"), ("scroll_down", "FRONTEND/COMP_SCROLLLIST/Skt_list_ScrollArrow_Down.bmp"),
    ("locstatus", "IMAGES/CareerItems/LocStatusIcons.bmp"),
]
for lv in ["Amphitheatre", "Barcelona", "BigAssWall", "CommCentre", "DIYPark", "ElementarySchool", "EMB", "Hellavator", "LakeSherwin", "MegaPark", "OldTown", "Paris", "Rio",
           "SanFran", "Shanghai", "SouthBank", "Spillway", "SpotSculptWarehouse", "Warehouse", "Wilshire", "XGames"]:
    IMAGES.append(("loc_" + lv.lower(), "FRONTEND/LOCATIONS/%s.bmp" % lv))
# the story screens (M7): the sponsors' pictures (42 x 42) and the Thrasher "Skater Of The Year" cover
for key, f in [("planb", "PlanB"), ("almost", "Almost"), ("cliche", "Cliche"), ("alienws", "AlienWS"), ("girl", "Girl"), ("baker", "Baker"), ("blind", "Blind"), ("independent", "Independent"),
               ("venture", "Venture"), ("royal", "Royal"), ("silver", "Silver"), ("fillmore", "Fillmore"), ("spitfire", "Spitfire"), ("ricta", "Ricta"), ("autobahn", "Autobahn"),
               ("momentum", "Momentum"), ("dc", "DC"), ("lakai", "Lakai"), ("icecream", "Icecream"), ("adidas", "Adidas"), ("globe", "Globe")]:
    IMAGES.append(("sp_" + key, "FRONTEND/SPONSORICONS/%s.bmp" % f))
IMAGES.append(("thrasher_sm", "IMAGES/CareerItems/Magazines/thrasher_SM.bmp"))
SPRITES = "extracted/files/DATA/SPRITES"
# the HUD / menu sprites of the original (NCGR + NCER cell banks, tools/campaign_nitro.py): name -> cell of hudsprites_mainscreen
CELLS = {"spr_x15": 1, "spr_x2": 2, "spr_x3": 3, "spr_fill": 4, "spr_strip": 6, "spr_arrow_up": 7, "spr_d1": 8, "spr_d2": 9, "spr_d3": 10, "spr_back": 11}
FONT_FILES = ["fe_extralarge", "fe_large", "fe_medium", "fe_small", "fetiny", "pause_large", "pause_small"]
KEY = (255, 0, 255)


def pow2(n, lo=16):
    p = lo
    while p < n:
        p *= 2
    return p


def pack_image(path):
    im = path if isinstance(path, Image.Image) else Image.open(path)
    w, h = im.size
    tw, th = pow2(w), pow2(h, 8)
    if im.mode == "P":
        pal = im.getpalette()[:768]
        pal += [0] * (768 - len(pal))
        clut = bytearray()
        for i in range(256):
            r, g, b = pal[3 * i:3 * i + 3]
            clut += bytes((r, g, b, 0 if (r, g, b) == KEY else 255))
        px = im.tobytes()
        buf = bytearray(tw * th)
        for y in range(th):
            sy = min(y, h - 1)
            row = px[sy * w:(sy + 1) * w]
            buf[y * tw:y * tw + w] = row
            buf[y * tw + w:(y + 1) * tw] = bytes([row[-1]]) * (tw - w)
        return (w, h, tw, th, 0, bytes(buf), bytes(clut))
    rgba = im.convert("RGBA")
    buf = bytearray(tw * th * 4)
    src = rgba.load()
    for y in range(th):
        for x in range(tw):
            r, g, b, a = src[min(x, w - 1), min(y, h - 1)]
            if (r, g, b) == KEY:
                a = 0
            buf[(y * tw + x) * 4:(y * tw + x) * 4 + 4] = bytes((r, g, b, a))
    return (w, h, tw, th, 1, bytes(buf), b"")


def pack_font(path):
    f = campaign_xfn.read_xfn(path)
    h = f["height"]
    tw = 256
    # fe_small's ')' (and '*') hold junk in the original file (a width of 11 with stray bytes: 87, 184, 242 ...): the game never draws them. A ')' is made as the mirror image of its '(' .
    if 41 in f["glyphs"] and 40 in f["glyphs"] and max(f["glyphs"][41][2]) > 7:
        w, hh, d = f["glyphs"][40]
        mirrored = bytes(d[y * w + (w - 1 - x)] for y in range(hh) for x in range(w))
        f["glyphs"][41] = (w, hh, mirrored)
        f["widths"][41] = w
    x = y = 0
    place = {}
    for c in sorted(f["glyphs"]):
        w = f["glyphs"][c][0]
        if x + w + 1 > tw:
            x = 0; y += h + 1
        place[c] = (x, y)
        x += w + 1
    th = pow2(y + h + 1, 8)
    buf = bytearray(tw * th)
    for c, (gx, gy) in place.items():
        w, _, d = f["glyphs"][c]
        for yy in range(h):
            for xx in range(w):
                v = d[yy * w + xx]
                buf[(gy + yy) * tw + gx + xx] = v if v <= 7 else 0        # (stray values in the originals: clear)
    glyphs = bytearray()
    for c in range(256):
        w = f["widths"][c]
        gx, gy = place.get(c, (0, 0))
        glyphs += struct.pack("<HHBB", gx, gy, w if c in place else 0, w)
    return h, tw, th, bytes(buf), bytes(glyphs)


def main(out):
    imgs = []
    for name, rel in IMAGES:
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p):
            print("missing", rel)
            continue
        imgs.append((name, pack_image(p)))
    import campaign_nitro as N
    pal, _ = N.read_nclr(os.path.join(SPRITES, "pal_sprites.NCLR"))
    g = N.read_ncgr(os.path.join(SPRITES, "HUD", "hudsprites_mainscreen.NCGR"))
    cells = N.read_ncer(os.path.join(SPRITES, "HUD", "hudsprites_mainscreen.NCER"))
    for name, ci in CELLS.items():
        im = N.render_cell(cells[ci], g, pal)
        imgs.append((name, pack_image(im)))
        if name == "spr_arrow_up":                                  # the big arrow of the main menu points left: the same arrow turned
            imgs.append(("spr_arrow_left", pack_image(im.rotate(90, expand=True))))
    fonts = [(n, pack_font(os.path.join(FONTS, n + ".xfn"))) for n in FONT_FILES]
    off = 32
    off_img = off; off += 48 * len(imgs)
    off_font = off; off += 32 * len(fonts)
    ents, blobs = bytearray(), []

    def place(data, align=16):
        nonlocal off
        off = (off + align - 1) & ~(align - 1)
        o = off; blobs.append((o, data)); off += len(data)
        return o
    for name, (w, h, tw, th, fmt, pix, clut) in imgs:
        op = place(pix); oc = place(clut) if clut else 0
        ents += struct.pack("<24sHHHHB3xIII", name.encode(), w, h, tw, th, fmt, op, oc, 0)
    fents = bytearray()
    for name, (h, tw, th, pix, glyphs) in fonts:
        op = place(pix); og = place(glyphs)
        fents += struct.pack("<16sBxHH2xII", name.encode(), h, tw, th, op, og)
    size = (off + 15) & ~15
    buf = bytearray(size)
    buf[:32] = struct.pack("<4sIIIIIII", b"SKUI", 1, len(imgs), off_img, len(fonts), off_font, size, 0)
    buf[off_img:off_img + len(ents)] = ents
    buf[off_font:off_font + len(fents)] = fents
    for o, d in blobs:
        buf[o:o + len(d)] = d
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "wb").write(buf)
    print("%s: %d images, %d fonts, %d bytes (%.2f MB)" % (out, len(imgs), len(fonts), size, size / 1048576))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "dist/SkateItPSP/data/ui.pak")
