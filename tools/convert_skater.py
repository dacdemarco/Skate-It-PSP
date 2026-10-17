#!/usr/bin/env python3
"""Convert one skater (model + texture + the gameplay animation set) into a PSP-ready .skt (SKS2) file.

usage: convert_skater.py <name> <lod> <out.skt>      e.g.  convert_skater.py aaron 1 dist/SkateItPSP/data/skater_aaron.skt

The PSP does the skeleton maths (src/skel.c), so the file carries the raw data, not baked matrices:
  bone matrix G_i = G_parent * T(offset_i) * inverse(R(q_i));  the root translation is the ABF root position (see docs/TECHNICAL.md).
All 376 gameplay animations of anims.arc (6-frame pose clips, ARC_S) are stored together with the board's pose of animb.arc (ARC_B).
Data frame: z up, board long axis = +y. The PSP maps it to its y-up frame with (x,y,z) -> (-x, z, y) (det +1, board nose = +z) times `scale`.

File (little endian, sections 16-byte aligned):
  Header (96 B)  char magic[4]='SKS2'; u32 version=2, nverts, nbones, nanims; u32 off_tex, off_verts, off_vbone, off_skel, off_anims;
                 f32 scale, height; u32 pad[12]
  TexEntry (24 B) + clut + pixels                (same layout as .lvl, see convert_level.py)
  Vertex (24 B) * nverts   triangle list in bone-local space: f32 u, v; u32 abgr; f32 x, y, z   (uv normalised)
  u8 vbone[nverts]
  Skel: nbones * { i32 parent; f32 offset[3] }    (16 B, offsets in model units, from ARC_S/animdb.BOF)
  AnimTable: nanims * { u32 nframes, frame_off, board_nframes, board_off }   (16 B)
  skater frame (364 B) = f32 root[3], f32 quat[nbones][4] (x,y,z,w);   board frame (28 B) = f32 pos[3], f32 quat[4] (x,y,z,w)
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import mbf, dst, convert_level as cl

SKATER_SCALE = 8.6          # model units -> metres: standing height 0.2 model units -> 1.75 m (level unit ~ 1 m: stair risers are 0.17)
NB = 22
ARC_S = "extracted/unpacked/DATA/SKATERS/ARC_S"
ARC_B = "extracted/unpacked/DATA/SKATERS/ARC_B"
NANIM = 376


def load_board_abf(path):
    b = open(path, "rb").read()
    w = struct.unpack_from("<%dh" % (len(b) // 2), b, 0)
    assert len(w) == 4 + 8 * w[2], path
    return [(tuple(x / 4096 for x in w[4 + 8 * f:8 + 8 * f]), tuple(x / 4096 for x in w[9 + 8 * f:12 + 8 * f])) for f in range(w[2])]


def shade_color(nrm):
    n = np.array(nrm); n = n / (np.linalg.norm(n) or 1)
    shade = 0.62 + 0.38 * max(0.0, float(n @ np.array([0.35, 0.8, 0.5]) / np.linalg.norm([0.35, 0.8, 0.5])))
    c = int(min(255, shade * 255))
    return c | (c << 8) | (c << 16) | (255 << 24)


def main():
    name, lod, out = sys.argv[1], sys.argv[2], sys.argv[3]
    m = mbf.load_mbf("%s/MODELS/skater_%s_lod%s.mbf" % (mbf.SK, name, lod))
    tex = cl.prep_texture("%s/TEXTURES/%s.dst" % (mbf.SK, name))
    par = mbf.parents(m["nodes"])
    off = mbf.load_bof(ARC_S + "/animdb.BOF")
    assert len(off) == NB and len(par) == NB

    nverts = len(m["tris"]) * 3
    verts = bytearray(); vbone = bytearray()
    for tri in m["tris"]:
        for pos, uv, nrm, bone in tri:
            verts += struct.pack(cl.VERT_FMT, uv[0] / tex["w"], uv[1] / tex["h"], shade_color(nrm), *pos)
            vbone.append(bone)

    off_tex = 96
    pos = cl.align16(off_tex + 24)
    clut_off = pos; pos = cl.align16(pos + len(tex["clut"]))
    pix_off = pos; pos = cl.align16(pos + len(tex["pix"]))
    tex_entry = struct.pack(cl.TEX_FMT, tex["w"], tex["h"], tex["bpp"], tex["swz"], tex["alpha"], 0, clut_off, pix_off, tex["n"], 0)
    off_verts = pos; pos = cl.align16(pos + len(verts))
    off_vbone = pos; pos = cl.align16(pos + len(vbone))
    off_skel = pos; pos = cl.align16(pos + NB * 16)
    off_anims = pos; pos = cl.align16(pos + NANIM * 16)

    skel = b"".join(struct.pack("<i3f", par[i], *(c / 4096 for c in off[i])) for i in range(NB))
    table = bytearray(); data = bytearray()
    cur = pos
    for n in range(NANIM):
        a = mbf.load_abf("%s/%d.abf" % (ARC_S, n))
        bf = load_board_abf("%s/%d.abf" % (ARC_B, n))
        sk_off = cur
        for rp, qs, _marks in a["frames"]:
            data += struct.pack("<3f", *(c / 4096 for c in rp))
            for q in qs:
                data += struct.pack("<4f", *q)
        cur += a["nframes"] * (12 + NB * 16)
        bd_off = cur
        for bq, bp in bf:
            data += struct.pack("<3f", *bp) + struct.pack("<4f", *bq)
        cur += len(bf) * 28
        table += struct.pack("<4I", a["nframes"], sk_off, len(bf), bd_off)

    # standing height (model units * scale) from the neutral pose, for the file header only
    a0 = mbf.load_abf("%s/0.abf" % ARC_S)
    P = mbf.pose(m["nodes"], [tuple(c / 4096 for c in o) for o in off], a0["frames"][0][1], tuple(c / 4096 for c in a0["frames"][0][0]))
    zs = [(np.array(P[v[3]][0]) @ np.array(v[0]) + np.array(P[v[3]][1]))[2] for t in m["tris"] for v in t]
    height = float(SKATER_SCALE * max(zs))
    print("neutral pose: lowest vertex z %.4f (want ~0), height %.2f m" % (min(zs), height))

    hdr = struct.pack("<4sIIII IIIII ff 12I", b"SKS2", 2, nverts, NB, NANIM, off_tex, off_verts, off_vbone, off_skel, off_anims,
                      SKATER_SCALE, height, *([0] * 12))
    assert len(hdr) == 96
    buf = bytearray(cur)
    buf[:96] = hdr
    buf[off_tex:off_tex + 24] = tex_entry
    buf[clut_off:clut_off + len(tex["clut"])] = tex["clut"]
    buf[pix_off:pix_off + len(tex["pix"])] = tex["pix"]
    buf[off_verts:off_verts + len(verts)] = verts
    buf[off_vbone:off_vbone + len(vbone)] = vbone
    buf[off_skel:off_skel + len(skel)] = skel
    buf[off_anims:off_anims + len(table)] = table
    buf[pos:pos + len(data)] = data
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "wb").write(buf)
    print("%s lod%s: %d triangles, %d vertices, %d animations, %.2f MB; texture %dx%d" % (
        name, lod, len(m["tris"]), nverts, NANIM, len(buf) / 1048576, tex["w"], tex["h"]))


if __name__ == "__main__":
    main()
