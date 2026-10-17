#!/usr/bin/env python3
"""Second pass over extracted/files: inflate *.ZL (zlib) files and unzip *.zip
into extracted/unpacked/ (same relative layout; zips become folders).

usage: unpack_layers.py <extracted_dir>
"""
import os, sys, zlib, zipfile

src = os.path.join(sys.argv[1], "files")
dst = os.path.join(sys.argv[1], "unpacked")
nz = nzip = bad = 0
for d, _, fs in os.walk(src):
    for f in fs:
        p = os.path.join(d, f)
        rel = os.path.relpath(p, src)
        low = f.lower()
        try:
            if low.endswith(".zl"):
                out = os.path.join(dst, rel[:-3])
                os.makedirs(os.path.dirname(out), exist_ok=True)
                open(out, "wb").write(zlib.decompress(open(p, "rb").read()))
                nz += 1
            elif low.endswith(".zip"):
                zipfile.ZipFile(p).extractall(os.path.join(dst, rel[:-4]))
                nzip += 1
        except Exception as e:
            bad += 1
            print("FAIL", rel, e)
print("inflated", nz, "unzipped", nzip, "failed", bad)
