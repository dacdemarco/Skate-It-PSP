#!/usr/bin/env python3
"""Split a Thumb module into functions (a `push {.., lr}` starts one, the next `push {.., lr}` or the end ends it) and print the ones that use a literal in a range,
with the disassembly. usage: thumb_funcs.py <module> <lit_lo hex> <lit_hi hex> [from hex] [to hex]"""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdis, capstone

mods = armdis.modules()
name, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
base, b = mods[name]
a0 = int(sys.argv[4], 16) if len(sys.argv) > 4 else base
a1 = int(sys.argv[5], 16) if len(sys.argv) > 5 else base + len(b)
ARM = len(sys.argv) > 6 and sys.argv[6] == "arm"
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM if ARM else capstone.CS_MODE_THUMB)


def lit(a):
    for nm, (bs, bb) in mods.items():
        if bs <= a < bs + len(bb) - 3:
            return struct.unpack_from("<I", bb, a - bs)[0]
    return None


starts = []
if ARM:
    for a in range(a0, a1 - 3, 4):
        w = struct.unpack_from("<I", b, a - base)[0]
        if (w & 0xFFFF4000) == 0xE92D4000:            # push {..., lr}
            starts.append(a)
else:
    for a in range(a0, a1 - 1, 2):
        h = struct.unpack_from("<H", b, a - base)[0]
        if (h & 0xFF00) == 0xB500:                  # push {..., lr}
            starts.append(a)
starts.append(a1)
for i in range(len(starts) - 1):
    s, e = starts[i], starts[i + 1]
    lines, hit = [], False
    a = s
    while a < e:
        ins = list(md.disasm(b[a - base:a - base + 4], a, 1))
        if not ins:
            lines.append("%08x  .hword %04x" % (a, struct.unpack_from("<H", b, a - base)[0])); a += 4 if ARM else 2; continue
        x = ins[0]
        note = ""
        if x.mnemonic.startswith("ldr") and "[pc," in x.op_str and "#" in x.op_str:
            imm = int(x.op_str.split("#")[1].rstrip("]!"), 16)
            w = lit(((a + (8 if ARM else 4)) & ~3) + imm)
            if w is not None:
                note = "   ; =0x%x" % w
                if lo <= w <= hi: hit = True
        lines.append("%08x  %-8s %s%s" % (a, x.mnemonic, x.op_str, note))
        a += x.size
    if hit:
        print("=== function %#x .. %#x" % (s, e))
        print("\n".join(lines))
