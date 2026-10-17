#!/usr/bin/env python3
"""Thumb / ARM disassembler for the overlays with literal pools resolved (linear sweep that skips undecodable words).
usage: thumb_dis.py <module: arm9|ovN> <start hex> <end hex> [arm]"""
import sys, struct, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdis, capstone

mods = armdis.modules()
name, a0, a1 = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
base, b = mods[name]


def word_at(a):
    for nm, (bs, bb) in mods.items():
        if bs <= a < bs + len(bb) - 3:
            return struct.unpack_from("<I", bb, a - bs)[0]
    return None


def string_at(a):
    for nm, (bs, bb) in mods.items():
        if bs <= a < bs + len(bb):
            m = re.match(rb"[ -~]{3,48}", bb[a - bs:a - bs + 56])
            if m:
                return m.group(0).decode()
    return None


ARM = len(sys.argv) > 4 and sys.argv[4] == "arm"
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM if ARM else capstone.CS_MODE_THUMB)
a = a0
while a < a1:
    code = b[a - base:a - base + 4]
    ins = list(md.disasm(code, a, 1))
    if not ins:
        if ARM:
            print("%08x  .word    %08x" % (a, struct.unpack_from("<I", b, a - base)[0]))
            a += 4
        else:
            print("%08x  .hword   %04x" % (a, struct.unpack_from("<H", b, a - base)[0]))
            a += 2
        continue
    i = ins[0]
    note = ""
    if i.mnemonic.startswith("ldr") and "[pc," in i.op_str and "#" in i.op_str:
        imm = int(i.op_str.split("#")[1].rstrip("]!"), 16)
        w = word_at(((a + (8 if ARM else 4)) & ~3) + imm)
        if w is not None:
            note = "   ; =0x%x" % w
            s = string_at(w)
            if s: note += ' "%s"' % s
    elif i.mnemonic in ("bl", "blx"):
        pass
    print("%08x  %-8s %s%s" % (a, i.mnemonic, i.op_str, note))
    a += i.size
