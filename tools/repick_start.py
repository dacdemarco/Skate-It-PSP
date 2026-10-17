#!/usr/bin/env python3
"""Recompute the start point of already converted levels (convert_level.pick_start) and patch it into the .lvl header, without redoing the whole conversion.
usage: repick_start.py <file.lvl> [...]"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert_level as cl

for path in sys.argv[1:]:
    b = bytearray(open(path, "rb").read())
    f = struct.unpack_from("<4sI5I5I6f3f3II", b, 0)
    ncv, ncp, off_collv, off_collp = f[5], f[6], f[10], f[11]
    bounds = ((f[12], f[13], f[14]), (f[15], f[16], f[17]))
    old = f[18:21]
    cverts = [struct.unpack_from("<3f", b, off_collv + 12 * i) for i in range(ncv)]
    polys = [struct.unpack_from("<4HI", b, off_collp + 12 * i) for i in range(ncp)]
    new = cl.pick_start(cverts, set(polys), bounds)
    struct.pack_into("<3f", b, 72, *new)
    open(path, "wb").write(b)
    print("%-28s start (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f)" % (os.path.basename(path), *old, *new))
