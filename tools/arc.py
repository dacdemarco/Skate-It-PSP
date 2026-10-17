#!/usr/bin/env python3
"""Unpack the skater animation archives (anims.arc / animb.arc).
Layout: [u32 count][u32 name_bytes][names, concatenated, no separators] then 5 tables of `count` u32:
name lengths, file offsets (absolute), name offsets, compressed sizes, uncompressed sizes; file data is zlib."""
import struct, zlib, os, sys

def read_arc(path):
    b = open(path, "rb").read()
    n, nsz = struct.unpack_from("<II", b, 0)
    names = b[8:8 + nsz]
    o = 8 + nsz
    t = [struct.unpack_from("<%dI" % n, b, o + 4 * n * k) for k in range(5)]
    out = {}
    for i in range(n):
        name = names[t[2][i]:t[2][i] + t[0][i]].decode()
        data = b[t[1][i]:t[1][i] + t[3][i]]
        raw = zlib.decompress(data) if t[3][i] != t[4][i] or data[:1] == b"\x78" else data
        assert len(raw) == t[4][i], (name, len(raw), t[4][i])
        out[name] = raw
    return out

if __name__ == "__main__":
    base = "extracted/files/DATA/SKATERS"
    for arc, dest in (("anims.arc", "extracted/unpacked/DATA/SKATERS/ARC_S"), ("animb.arc", "extracted/unpacked/DATA/SKATERS/ARC_B")):
        files = read_arc(os.path.join(base, arc))
        os.makedirs(dest, exist_ok=True)
        for k, v in files.items():
            open(os.path.join(dest, k), "wb").write(v)
        print(arc, len(files), "files ->", dest)
