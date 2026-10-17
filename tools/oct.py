#!/usr/bin/env python3
"""OCT2 level chunk reader (partial).

A file is a serialized octree (RAM pointers inside, 0x022dXXXX). Leaves found so far:
  L+0x00  s32 x3   leaf centre (1.19.12 fixed, world space)
  L+0x0C  6 words  bounds (meaning TBD)
  L+0x24  u32      RAM pointer
  L+0x28  k * 12B  collision tris: u16 v0,v1,v2 (index into verts.cmn), u16 0xFFFF, u32 flags
  ...     u32 size, GX display list: vertex coords are local, world = root_centre + local * scale (NOT the leaf centre)
Header: +0x08 u32 = uniform scale for the whole file (1.19.12 fixed), +0x14 = number of GX streams,
        +0x1C = total GX bytes, +0x24 = RAM pointer to the root node (at file offset 0x30).
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
import gx, dsm


def find_streams(b):
    found, p, n = [], 0x24, len(b)
    while p + 8 <= n:
        size = struct.unpack_from("<I", b, p)[0]
        if 8 <= size <= 0x20000 and size % 4 == 0 and p + 4 + size <= n:
            cmds, stop, why = gx.parse(b, p + 4, p + 4 + size)
            names = [c for c, _ in cmds]
            if why == "end" and stop == p + 4 + size and 0x40 in names and 0x41 in names \
                    and names[0] in (0x2B, 0x2A, 0x29, 0x40, 0x20, 0x22, 0x11, 0x1B):
                found.append((p, size, cmds)); p += 4 + size; continue
        p += 2  # leaves are 2-byte aligned (10-byte trailer after each stream)
    return found


def leaves(b, nverts=None):
    """Leaf record R: u32 hdr (lo16 = 1, hi16 = npolys), s32 x3 centre, 6 words, ptr at R+0x28,
    then npolys * 12B polygons (4 x u16 vertex index, 0xFFFF = none; u32 flags), then
    u32 size + GX stream. R is found by trying k = 0.. and requiring a consistent header,
    a plausible RAM pointer and (if nverts is given) valid polygon indices."""
    out = []
    for s, size, cmds in find_streams(b):
        for k in range(0, 600):
            R = s - 0x2C - 12 * k
            if R < 0x2C:
                break
            hdr = struct.unpack_from("<I", b, R)[0]
            ptr = struct.unpack_from("<I", b, R + 0x28)[0]
            if (hdr >> 16) != k or (hdr & 0xFFFF) != 1:
                continue
            if not (0x01C00000 <= ptr < 0x02400000):
                continue
            polys = [struct.unpack_from("<4HI", b, R + 0x2C + 12 * i) for i in range(k)]
            if nverts is not None and not all(
                    p[0] < nverts and p[1] < nverts and p[2] < nverts and (p[3] < nverts or p[3] == 0xFFFF)
                    and p[4] < 0x10000 for p in polys):
                continue
            c = struct.unpack_from("<3i", b, R + 4)
            out.append(dict(center=tuple(v / 4096 for v in c), polys=polys,
                            tris=[p for p in polys if p[3] == 0xFFFF],
                            cmds=cmds, off=s, rec=R, hdr=hdr))
            break
    return out


def origin(b):
    """Root node centre (file offset 0x30): the origin the GX local coords are relative to."""
    return tuple(v / 4096 for v in struct.unpack_from("<3i", b, 0x30))


def batches(b, lf):
    """Texture batches of a leaf: list of (texture_id, [triangles]) in stream order.
    Trailer after the stream: u16 a (= number of TEXIMAGE_PARAM cmds = batches; stored as u32), then 8*a
    bytes of 8-byte entries, read from file offset +2: (u16 dl_offset, u16 running count, u16 flag, u16 id).
    In this u32-aligned view of the 8*a bytes the id of batch i is u16 index 4*i+2.
    Texture id = index into the level's track1.pvs texture table (see pvs_tex.resolve_textures); id ==
    len(table) is the sky dome, 0xFFFF = none, an empty table slot = no texture."""
    size = struct.unpack_from("<I", b, lf["off"])[0]
    e = lf["off"] + 4 + size
    a = struct.unpack_from("<I", b, e)[0]
    raw = struct.unpack("<%dH" % (4 * a), b[e + 4:e + 4 + 8 * a].ljust(8 * a, b"\x00"))
    ids = [raw[4 * i + 2] for i in range(a)]
    out = [(t, []) for t in ids]
    for tri in dsm.replay(lf["cmds"]):
        k = tri[0][4]
        if 0 <= k < a:
            out[k][1].append(tri)
    return out


def scale(b):
    return struct.unpack_from("<I", b, 8)[0] / 4096


def load_cmn(path):
    v = open(path, "rb").read()
    n = struct.unpack_from("<I", v, 8)[0]
    return [tuple(x / 4096 for x in struct.unpack_from("<3i", v, 12 + i * 12)) for i in range(n)]
