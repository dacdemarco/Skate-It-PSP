#!/usr/bin/env python3
"""Convert every .dst under <unpacked_dir> to PNG under <png_dir> (same layout)."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import dst
src, out = sys.argv[1], sys.argv[2]
ok = bad = 0
for d, _, fs in os.walk(src):
    for f in fs:
        if not f.lower().endswith(".dst"):
            continue
        p = os.path.join(d, f)
        b = open(p, "rb").read()
        if len(b) < 18:
            continue  # a few empty placeholder files
        rel = os.path.relpath(p, src)[:-4] + ".png"
        try:
            o = os.path.join(out, rel)
            os.makedirs(os.path.dirname(o), exist_ok=True)
            dst.to_image(dst.decode(b)).save(o)
            ok += 1
        except Exception as e:
            bad += 1
            print("FAIL", rel, e)
print("converted", ok, "failed", bad)
