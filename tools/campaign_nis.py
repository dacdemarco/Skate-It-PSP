#!/usr/bin/env python3
"""Print the NIS (story scene) scripts of the ROM: the 12-byte scene table at arm9 0x020753C0 {u32 n, u32 ptr, u32 x} and the 16-byte records {u32 type, u32 end << 16 | start,
u32 param, u32 link} of each scene (see NOTES: the NIS script table). Reads extracted/decomp/arm9.bin (make_data.py extract step).
usage: campaign_nis.py [scene ...]        (scene numbers 1 .. 57; no argument = all)"""
import struct, sys, os

ARM9 = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extracted", "decomp", "arm9.bin")
BASE = 0x02000000
TABLE = 0x020753C0
NSCENES = 57
TYPES = {0: "subtitle", 2: "camera path", 3: "camera on actor", 4: "camera yaw", 18: "play clip", 19: "talk span (clip type = link)", 20: "talk span", 21: "visible", 22: "spawn skater",
         23: "actor count", 24: "talk pose off", 25: "talk pose on", 26: "camera zone", 27: "camera target", 28: "camera target (mode 3)", 29: "delay", 31: "camera p", 32: "camera distance"}


def scenes():
    b = open(ARM9, "rb").read()
    out = []
    for k in range(NSCENES):
        n, ptr, x = struct.unpack_from("<III", b, TABLE - BASE + 12 * k)
        recs = [struct.unpack_from("<IIII", b, ptr - BASE + 16 * r) for r in range(n)]
        out.append((k + 1, None if x == 0xFFFFFFFF else x, recs))
    return out


if __name__ == "__main__":
    want = {int(a) for a in sys.argv[1:]}
    for k, x, recs in scenes():
        if want and k not in want:
            continue
        print("scene %d  x=%s (the level id of the scene, - = none)  %d records" % (k, "-" if x is None else x, len(recs)))
        for t, tm, par, lk in recs:
            end = tm >> 16
            print("   %-24s start %5d end %s  param %#x  link %s" % ("%d %s" % (t, TYPES.get(t, "?")), tm & 0xFFFF, "open" if end == 0xFFFF else "%5d" % end, par, "-" if lk == 0xFFFFFFFF else lk))
