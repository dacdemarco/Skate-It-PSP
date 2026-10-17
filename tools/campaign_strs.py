#!/usr/bin/env python3
"""List printable ASCII strings (len>=N) of arm9 + overlays with their RAM address.
Usage: campaign_strs.py [minlen] [regex]"""
import sys, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from armdis import modules
n = int(sys.argv[1]) if len(sys.argv) > 1 else 5
rx = re.compile(sys.argv[2], re.I) if len(sys.argv) > 2 else None
for name, (base, b) in modules().items():
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % n, b):
        s = m.group().decode()
        if rx is None or rx.search(s):
            print("%s %08x %s" % (name, base + m.start(), s))
