#!/usr/bin/env python3
"""Parse the OBJECT sections of a level's track1.pvs (the part after the octree-name/visibility table and before the
texture-name table). Layout (all LE, found by reverse engineering, verified on all 21 levels):
    [u32 nobj] nobj x 76 B records: char name[32] (NUL padded, tail = uninitialised stack junk), u32 junk[8], s32 x,y,z  (1.19.12)
    [u32 nspl] nspl x { char name[32], u32 npts, npts x (s32 x,y,z) }     -> 'progress_spline', 'col<N>_grind_<NNN>' (grind rails)
    [u32 ncam] ncam x { char name[32], u32 npts, npts x (s32 x,y,z) }     -> 'camera<FOV?>_<NNN>' camera paths
    then the 64-byte texture-name table (see pvs_tex.py).
    Camera path records have an extra 8 bytes (2 x s32) per point after the npts*12 position bytes.
Usage: campaign_pvs.py <LevelName> [-v]"""
import sys, os, re, struct, glob

ROOT = "extracted/unpacked/DATA/LOCATIONS"
FX = 4096.0


def cstr(b):
    return b.split(b"\0")[0].decode("latin-1")


def okname(b):
    n = b.split(b"\0")[0]
    return len(n) >= 3 and all(32 < c < 127 for c in n) and (chr(n[0]).isalpha())


def find_obj_table(b):
    # last '.oct' string, then search forward for [count][name...] where records chain consistently
    octs = [m.end() for m in re.finditer(rb"\.oct\0", b)]
    start = octs[-1] if octs else 0
    for p in range(start, start + 96):
        if p + 4 > len(b): break
        c = struct.unpack_from("<I", b, p)[0]
        if 0 < c < 4000 and p + 4 + 76 * c + 8 < len(b):
            if all(okname(b[p + 4 + 76 * i:p + 4 + 76 * i + 32]) for i in range(min(c, 6))) and okname(b[p + 4 + 76 * (c - 1):p + 4 + 76 * (c - 1) + 32]):
                return p
    raise ValueError("no object table")


def parse(path):
    b = open(path, "rb").read()
    p = find_obj_table(b)
    n = struct.unpack_from("<I", b, p)[0]; p += 4
    objs = []
    for i in range(n):
        r = b[p:p + 76]
        x, y, z = struct.unpack_from("<3i", r, 64)
        objs.append((cstr(r[:32]), x / FX, y / FX, z / FX))
        p += 76

    def paths(p, aux):
        n = struct.unpack_from("<I", b, p)[0]; p += 4
        out = []
        for i in range(n):
            nm = cstr(b[p:p + 32]); p += 32
            k = struct.unpack_from("<I", b, p)[0]; p += 4
            pts = [tuple(v / FX for v in struct.unpack_from("<3i", b, p + 12 * j)) for j in range(k)]
            p += 12 * k + aux * k          # cameras carry 8 more bytes per point (2 x s32, meaning unknown)
            out.append((nm, pts))
        return out, p
    spl, p = paths(p, 0)
    cam, p = paths(p, 8)
    first_dst = min(m.start() for m in re.finditer(rb"[ -~]{3,}\.dst\0", b))
    return dict(objs=objs, splines=spl, cams=cam, end=p, tex_start=first_dst)


def level_pvs(level):
    return glob.glob("%s/%s/*/track1.pvs" % (ROOT, level))[0]


if __name__ == "__main__":
    lv = sys.argv[1]
    d = parse(level_pvs(lv))
    print(lv, "objects", len(d["objs"]), "splines", len(d["splines"]), "cams", len(d["cams"]), "end", hex(d["end"]), "tex_start", hex(d["tex_start"]))
    if "-v" in sys.argv:
        for o in d["objs"]: print("  %-28s %9.3f %9.3f %9.3f" % o)
        for nm, pts in d["splines"]: print("  spline %-24s n=%d first=%s" % (nm, len(pts), tuple(round(v, 2) for v in pts[0]) if pts else None))
        for nm, pts in d["cams"]: print("  cam %-24s n=%d" % (nm, len(pts)))
