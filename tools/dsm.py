#!/usr/bin/env python3
"""DSM1 mesh loader: [DSM1][u32 ver][u32 stream_bytes][GX stream][tail].

The tail holds (at least) a 64-byte NUL-padded texture name. Geometry is a raw
DS GX display list, replayed here into triangles in model space.
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
import gx

IDENT = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]


def mmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def xform(m, v):
    x, y, z = v
    return tuple(m[0][j] * x + m[1][j] * y + m[2][j] * z + m[3][j] for j in range(3))


def fx(w):  # s32 1.19.12
    return (w - (1 << 32) if w & 0x80000000 else w) / 4096


def replay(cmds):
    """Return list of triangles: each ((x,y,z),(u,v),(r,g,b)) x3. UV in texels."""
    cur, stack = [r[:] for r in IDENT], []
    pos = [0.0, 0.0, 0.0]
    uv, col = (0.0, 0.0), (1.0, 1.0, 1.0)
    mode, verts, tris = None, [], []
    last = 0
    batch = -1
    mtx_mode = 2

    def emit():
        nonlocal verts
        if mode is None:
            return
        v = verts
        if mode == 0:
            for i in range(0, len(v) - 2, 3):
                tris.append((v[i], v[i + 1], v[i + 2]))
        elif mode == 1:
            for i in range(0, len(v) - 3, 4):
                tris.append((v[i], v[i + 1], v[i + 2]))
                tris.append((v[i], v[i + 2], v[i + 3]))
        elif mode == 2:
            for i in range(len(v) - 2):
                a, b, c = (v[i], v[i + 1], v[i + 2]) if i % 2 == 0 else (v[i + 1], v[i], v[i + 2])
                tris.append((a, b, c))
        elif mode == 3:
            for i in range(0, len(v) - 3, 2):
                tris.append((v[i], v[i + 1], v[i + 3]))
                tris.append((v[i], v[i + 3], v[i + 2]))
        verts = []

    def put():
        verts.append((xform(cur, pos), uv, col, last, batch))

    for c, p in cmds:
        if 0x23 <= c <= 0x28:
            last = c
        if c == 0x2A:
            batch += 1
        if c == 0x10:
            mtx_mode = p[0] & 3
        elif c == 0x11:
            stack.append([r[:] for r in cur])
        elif c == 0x12:
            for _ in range(gx.s16(p[0] & 0x3F) if p[0] & 0x20 == 0 else 1):
                if stack:
                    cur = stack.pop()
        elif c == 0x15:
            cur = [r[:] for r in IDENT]
        elif c == 0x1B:
            s = [fx(x) for x in p]
            cur = mmul([[s[0], 0, 0, 0], [0, s[1], 0, 0], [0, 0, s[2], 0], [0, 0, 0, 1]], cur)
        elif c == 0x1C:
            t = [fx(x) for x in p]
            cur = mmul([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [t[0], t[1], t[2], 1]], cur)
        elif c == 0x17 or c == 0x19:
            f = [fx(x) for x in p]
            m = [f[0:3] + [0], f[3:6] + [0], f[6:9] + [0], f[9:12] + [1]]
            cur = m if c == 0x17 else mmul(m, cur)
        elif c == 0x20:
            w = p[0]
            col = ((w & 31) / 31, ((w >> 5) & 31) / 31, ((w >> 10) & 31) / 31)
        elif c == 0x22:
            uv = (gx.s16(p[0]) / 16, gx.s16(p[0] >> 16) / 16)
        elif c == 0x23:
            pos = list(gx.vtx16(p)); put()
        elif c == 0x24:
            pos = list(gx.vtx10(p[0])[:2]) + [gx.vtx10(p[0])[2]]
            # VTX_10 is 1.3.6 fixed
            put()
        elif c == 0x25:
            pos[0], pos[1] = gx.s16(p[0]) / 4096, gx.s16(p[0] >> 16) / 4096; put()
        elif c == 0x26:
            pos[0], pos[2] = gx.s16(p[0]) / 4096, gx.s16(p[0] >> 16) / 4096; put()
        elif c == 0x27:
            pos[1], pos[2] = gx.s16(p[0]) / 4096, gx.s16(p[0] >> 16) / 4096; put()
        elif c == 0x28:
            def d(v):
                v &= 0x3FF
                return (v - 0x400 if v & 0x200 else v) / 4096  # raw 10-bit delta added straight to the 1.3.12 coordinate (verified 100%)
            w = p[0]
            pos[0] += d(w); pos[1] += d(w >> 10); pos[2] += d(w >> 20); put()
        elif c == 0x40:
            emit(); mode = p[0] & 3
        elif c == 0x41:
            emit(); mode = None
    emit()
    return tris


def load(path):
    b = open(path, "rb").read()
    assert b[:4] == b"DSM1"
    size = struct.unpack_from("<I", b, 8)[0]
    cmds, stop, why = gx.parse(b, 12, 12 + size)
    tail = b[12 + size:]
    name = ""
    i = tail.find(b".dst")
    if i > 0:
        j = i
        while j > 0 and 32 <= tail[j - 1] < 127:
            j -= 1
        name = tail[j:i + 4].decode()
    return dict(tris=replay(cmds), tex=name, why=why, ncmds=len(cmds), tail=tail)


def write_obj(m, path):
    with open(path, "w") as f:
        n = 1
        for t in m["tris"]:
            for v in t:
                f.write("v %f %f %f\n" % v[0])
            for v in t:
                f.write("vt %f %f\n" % (v[1][0], -v[1][1]))
            f.write("f %d/%d %d/%d %d/%d\n" % (n, n, n + 1, n + 1, n + 2, n + 2))
            n += 3


def wire_png(m, path, yaw=0.6, pitch=0.35, size=640):
    import math
    from PIL import Image, ImageDraw
    pts = [v[0] for t in m["tris"] for v in t]
    if not pts:
        return
    cx = [sum(p[i] for p in pts) / len(pts) for i in range(3)]
    r = max(max(abs(p[i] - cx[i]) for p in pts) for i in range(3)) or 1
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)

    def proj(p):
        x, y, z = [(p[i] - cx[i]) / r for i in range(3)]
        x, z = x * cy + z * sy, -x * sy + z * cy
        y, z = y * cp - z * sp, y * sp + z * cp
        return (size / 2 + x * size * 0.45, size / 2 - y * size * 0.45)
    im = Image.new("RGB", (size, size), (20, 20, 28))
    d = ImageDraw.Draw(im)
    for t in m["tris"]:
        q = [proj(v[0]) for v in t]
        d.line([q[0], q[1], q[2], q[0]], fill=(150, 220, 150))
    im.save(path)


if __name__ == "__main__":
    m = load(sys.argv[1])
    print("tex", m["tex"], "tris", len(m["tris"]), "stream:", m["why"])
    if len(sys.argv) > 2:
        wire_png(m, sys.argv[2])
