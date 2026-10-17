#!/usr/bin/env python3
"""Skater model (MBF), skeleton (ANIMDB.BOF + MBF tail tree) and animation (ABF) loaders.

MBF   [u32 version][u32 ?][u16 0][u32 stream_bytes][GX stream][tail]
      GX stream = triangle strips (BEGIN_VTXS mode 2) with MTX_RESTORE n before vertices: n = bone index whose
      matrix transforms the following vertices (vertex coords are in that bone's local space, VTX_16 + VTX_DIFF).
      tail = u32 nbones, then nbones x [u8 flag][u8 branch][u8 next][u8 1]: a tree in linked form
      (children of node i = {next if != 0xFF} + {branch if != 0xFF}).
BOF   [u32 nbones] + nbones x (s16 x, y, z): rest offset of each bone relative to its parent.
ABF   [s16 0x0416][s16 ?][s16 nframes][s16 2], then nframes x 113 s16 (file length = 4 + 113*nframes exactly), per frame, bone 0..21:
      [s16 x4 quaternion (x, y, z, w; 4096 = 1.0)][s16 marker +-1] and, for the root only, [s16 x3 position] after the marker.
      Layout recovered from the game's own sampler (ARM9 0x2093690/0x20933b4: quaternion at +0..6, marker at +8, root position at
      +10..14): the marker is the sign for nlerp towards the next frame (blend = (1-t)*qA + t*markerA*qB, renormalised).
      The quaternion is the bone's FULL local rotation (no bind pose): bone matrix = parent * T(offset) * inverse(rotation matrix of q).
      (An earlier parser read marker-first, which shifted every quaternion onto the previous bone.)
"""
import struct, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gx

SK = "extracted/unpacked/DATA/SKATERS"
ANIM = "extracted/files/DATA/SKATERS/ANIMEXTRAS"


def _replay(cmds):
    """GX command list -> triangles [(pos, uv, normal, bone_slot) x3]."""
    pos = [0.0, 0.0, 0.0]
    uv, nrm, bone = (0.0, 0.0), (0.0, 0.0, 1.0), 0
    mode, strip, tris = None, [], []

    def flush():
        nonlocal strip
        if mode == 2:
            for i in range(len(strip) - 2):
                tri = (strip[i], strip[i + 1], strip[i + 2]) if i % 2 == 0 else (strip[i + 1], strip[i], strip[i + 2])
                tris.append(tri)
        elif mode == 0:
            for i in range(0, len(strip) - 2, 3):
                tris.append((strip[i], strip[i + 1], strip[i + 2]))
        elif mode == 1:
            for i in range(0, len(strip) - 3, 4):
                tris.append((strip[i], strip[i + 1], strip[i + 2]))
                tris.append((strip[i], strip[i + 2], strip[i + 3]))
        strip = []

    def put():
        strip.append((tuple(pos), uv, nrm, bone))

    def d10(v):
        v &= 0x3FF
        return (v - 0x400 if v & 0x200 else v) / 4096

    def n10(v):
        v &= 0x3FF
        return (v - 0x400 if v & 0x200 else v) / 512

    for c, p in cmds:
        if c == 0x14:
            bone = p[0]
        elif c == 0x22:
            uv = (gx.s16(p[0]) / 16, gx.s16(p[0] >> 16) / 16)
        elif c == 0x21:
            w = p[0]
            nrm = (n10(w), n10(w >> 10), n10(w >> 20))
        elif c == 0x23:
            pos = list(gx.vtx16(p)); put()
        elif c == 0x25:
            pos[0], pos[1] = gx.s16(p[0]) / 4096, gx.s16(p[0] >> 16) / 4096; put()
        elif c == 0x26:
            pos[0], pos[2] = gx.s16(p[0]) / 4096, gx.s16(p[0] >> 16) / 4096; put()
        elif c == 0x27:
            pos[1], pos[2] = gx.s16(p[0]) / 4096, gx.s16(p[0] >> 16) / 4096; put()
        elif c == 0x28:
            w = p[0]
            pos[0] += d10(w); pos[1] += d10(w >> 10); pos[2] += d10(w >> 20); put()
        elif c == 0x40:
            flush(); mode = p[0] & 3
        elif c == 0x41:
            flush(); mode = None
    flush()
    return tris


def _tail(b, o):
    nb = struct.unpack_from("<I", b, o)[0]
    return [tuple(b[o + 4 + 4 * i:o + 8 + 4 * i]) for i in range(nb)]


def load_mbf(path):
    """Single-section model (skaters): [u32 ver][u32 'RJRJ'][u16 0][u32 stream_bytes][GX stream][tail]."""
    b = open(path, "rb").read()
    ver = struct.unpack_from("<I", b, 0)[0]
    size = struct.unpack_from("<I", b, 10)[0]
    cmds, stop, why = gx.parse(b, 0x0E, 0x0E + size)
    assert why == "end", (path, why)
    nodes = _tail(b, 0x0E + size)
    sm = slot_map(nodes)
    tris = [tuple((v[0], v[1], v[2], sm.get(v[3], 0)) for v in t) for t in _replay(cmds)]
    return dict(tris=tris, nodes=nodes, nbones=len(nodes), version=ver, slot_map=sm)


def load_mbf_multi(path):
    """Multi-section model (board): [u16 nsections][u16 0] then per section [RJRJ][u16 0][u32 stream_bytes][GX stream][u16 id of the
    next section], then the bone tail. Returns sections = [(material_id, tris)]; material ids are the u16 before each 'RJRJ'."""
    b = open(path, "rb").read()
    n = struct.unpack_from("<H", b, 0)[0]
    o, mat, sections = 4, 0, []
    for k in range(n):
        assert b[o:o + 4] == b"RJRJ", (path, k, hex(o))
        size = struct.unpack_from("<I", b, o + 6)[0]
        cmds, stop, why = gx.parse(b, o + 10, o + 10 + size)
        assert why == "end", (path, k, why)
        sections.append((mat, _replay(cmds)))
        o += 10 + size
        if k + 1 < n:
            mat = struct.unpack_from("<H", b, o)[0]; o += 2
    nodes = _tail(b, o)
    sm = slot_map(nodes)
    sections = [(m_, [tuple((v[0], v[1], v[2], sm.get(v[3], 0)) for v in t) for t in tris]) for m_, tris in sections]
    return dict(sections=sections, nodes=nodes, slot_map=sm)


def parents(nodes):
    """Tail record i = [flag][sibling][child][1]: byte2 = first child, byte1 = next sibling (0xFF = none).
    (Verified against the BOF offsets: hips at +/-36, symmetric limbs, shoulders at (45,32,+/-12).)"""
    par = [-1] * len(nodes)
    for i in range(len(nodes)):
        flag, sib, child, _ = nodes[i]
        if child != 0xFF:
            par[child] = i
        if sib != 0xFF:
            par[sib] = par[i]
    return par


def slot_map(nodes):
    """MTX_RESTORE slot -> node index. Nodes with flag 1 own a matrix slot, numbered from 2 in node order."""
    m, slot = {}, 2
    for i, nd in enumerate(nodes):
        if nd[0] == 1:
            m[slot] = i
            slot += 1
    return m


def load_bof(path=ANIM + "/ANIMDB.BOF"):
    b = open(path, "rb").read()
    n = struct.unpack_from("<I", b, 0)[0]
    return [struct.unpack_from("<3h", b, 4 + 6 * i) for i in range(n)]


def load_abf(path, nb=22):
    b = open(path, "rb").read()
    w = struct.unpack_from("<%dh" % (len(b) // 2), b, 0)
    nframes = w[2]
    assert len(w) == 4 + (8 + 5 * (nb - 1)) * nframes, path
    i, frames = 4, []
    for f in range(nframes):
        pos, quats, marks = None, [], []
        for bone in range(nb):
            quats.append(tuple(x / 4096 for x in w[i:i + 4])); marks.append(w[i + 4]); i += 5
            if bone == 0:
                pos = tuple(w[i:i + 3]); i += 3
        frames.append((pos, quats, marks))
    return dict(nframes=nframes, frames=frames, header=w[:4])


# ---- small quaternion / matrix helpers --------------------------------------------------------
def qmat(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def mmul3(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def mvec(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


def pose(nodes, offsets, quats, root_pos, offset_scale=1.0):
    """Forward kinematics -> list of (R 3x3, t) per bone in model space. world = R*local + t.
    G_i = G_parent * T(offset_i) * Rinv(q_i); the root translation is the ABF root position."""
    par = parents(nodes)
    out = [None] * len(nodes)
    for i in range(len(nodes)):            # parents always precede children in this tree
        R = qmat((-quats[i][0], -quats[i][1], -quats[i][2], quats[i][3]))     # inverse rotation
        if par[i] < 0:
            out[i] = (R, tuple(c * offset_scale for c in root_pos))
        else:
            pR, pt = out[par[i]]
            off = tuple(c * offset_scale for c in offsets[i])
            t = tuple(pt[k] + mvec(pR, off)[k] for k in range(3))
            out[i] = (mmul3(pR, R), t)
    return out
