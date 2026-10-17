#!/usr/bin/env python3
"""Raw-scan all modules for calls to an address: campaign_callers.py <hexaddr>...
ARM: BL/BLX/B (cond any) imm24; Thumb: BL / BLX pairs (+ B.W not covered). Does not stop at data words."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdis

def scan(mods, targets):
    res = []
    for name, (base, b) in mods.items():
        n = len(b)
        for o in range(0, n - 3, 4):
            w = struct.unpack_from("<I", b, o)[0]
            if (w >> 25) & 7 == 5:  # B/BL (cond!=0xF) or BLX imm (cond==0xF)
                imm = w & 0xFFFFFF
                if imm & 0x800000: imm -= 1 << 24
                t = base + o + 8 + imm * 4
                if (w >> 28) == 0xF:
                    t += ((w >> 24) & 1) * 2  # blx: H bit
                    kind = "blx"
                else:
                    kind = "bl" if (w >> 24) & 1 else "b"
                if t in targets: res.append((name, base + o, "arm " + kind, t))
        for o in range(0, n - 3, 2):
            h1 = struct.unpack_from("<H", b, o)[0]
            if (h1 & 0xF800) == 0xF000:
                h2 = struct.unpack_from("<H", b, o + 2)[0]
                if (h2 & 0xC000) == 0xC000:  # BL (0xF800) or BLX (0xE800)
                    off = ((h1 & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
                    if off & 0x400000: off -= 1 << 23
                    t = base + o + 4 + off
                    if (h2 & 0x1000) == 0:  # blx -> ARM, align
                        t &= ~3
                    if t in targets: res.append((name, base + o, "thumb bl", t))
    return res

if __name__ == "__main__":
    mods = armdis.modules()
    tg = {int(x, 16) for x in sys.argv[1:]}
    for r in scan(mods, tg):
        print("%s %08x %s -> %08x" % r)
