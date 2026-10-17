#!/usr/bin/env python3
"""Annotated disassembly helper: campaign_dis.py <arm|thumb> <module> <addr> [n]
Like armdis.annotated but keeps going over undecodable words (prints .word)."""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdis, capstone

def run(mods, name, addr, n, thumb):
    base, b = mods[name]
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
    step = 2 if thumb else 4
    out = []
    a = addr
    while len(out) < n and a - base + step <= len(b):
        ins = list(md.disasm(b[a - base:a - base + 4], a, 1))
        if not ins:
            v = struct.unpack_from("<I" if not thumb else "<H", b, a - base)[0]
            out.append("%08x  .word   %#x" % (a, v)); a += step; continue
        i = ins[0]
        s = "%08x  %-7s %s" % (i.address, i.mnemonic, i.op_str)
        mm = re.match(r"(\w+), \[pc, #(-?0x[0-9a-f]+|-?\d+)\]", i.op_str) if i.mnemonic.startswith("ldr") else None
        if mm:
            d = int(mm.group(2), 0)
            la = (((i.address + 4) & ~3) + d) if thumb else (i.address + 8 + d)
            if base <= la < base + len(b) - 3:
                v = struct.unpack_from("<I", b, la - base)[0]
                s += "      ; =%#x" % v
                for nm, (bs, bb) in mods.items():
                    if bs <= v < bs + len(bb):
                        m = re.match(rb"[ -~]{4,60}", bb[v - bs:v - bs + 64])
                        if m: s += '  "%s"' % m.group().decode()
                        break
        out.append(s)
        a += i.size
    return out

if __name__ == "__main__":
    mods = armdis.modules()
    print("\n".join(run(mods, sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4]) if len(sys.argv) > 4 else 100, sys.argv[1] == "thumb")))
