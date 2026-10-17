#!/usr/bin/env python3
"""Dump DATA/TEXT/textdb.arc: id key -> string for every language.
textdb.arc members: <Lang>.dat = [u32 offsets x1544][NUL-terminated Latin-1 strings], stringid.dat = same layout holding the
string KEY NAMES (LANG_0000NULL, TXT_..., ...), textdb.idx = [u32 count=1544][u32 ?]. Index i of stringid <-> index i of <Lang>.dat.
Usage: python tools/campaign_text.py [outfile]   (default writes tools/campaign_strings.tsv: index, key, English text)"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from arc import read_arc


def load(root="extracted/files/DATA/TEXT/textdb.arc"):
    f = read_arc(root)
    n = struct.unpack_from("<I", f["textdb.idx"], 0)[0]

    def table(b):
        offs = struct.unpack_from("<%dI" % n, b, 0)
        base = n * 4
        out = []
        for o in offs:
            e = b.index(b"\0", base + o)
            out.append(b[base + o:e].decode("latin-1"))
        return out

    keys = table(f["stringid.dat"])
    langs = {k[:-4]: table(v) for k, v in f.items() if k.endswith(".dat") and k != "stringid.dat"}
    return keys, langs


def esc(s):
    return s.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")


if __name__ == "__main__":
    keys, langs = load()
    en = langs["English"]
    out = sys.argv[1] if len(sys.argv) > 1 else "tools/campaign_strings.tsv"
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        for i, (k, s) in enumerate(zip(keys, en)):
            fh.write("%d\t%s\t%s\n" % (i, k, esc(s)))
    print(len(keys), "strings ->", out)
