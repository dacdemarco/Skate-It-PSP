#!/usr/bin/env python3
"""Tiny DS disassembly helper (ARM9, ARMv5TE). Modules: arm9 @0x02000000 and overlays at their RAM bases.

  armdis.py xref <addr>            find literal-pool words == addr in every module
  armdis.py str <text>             find a string in every module, print its address
  armdis.py arm <module> <addr> [n]   disassemble n ARM instructions from addr
  armdis.py thumb <module> <addr> [n]
"""
import sys, os, glob, re, struct
import capstone

DEC = os.path.join(os.path.dirname(__file__), "..", "extracted", "decomp")


def modules():
    a9 = open(os.path.join(DEC, "arm9.bin"), "rb").read()
    mods = {"arm9": (0x02000000, a9)}
    # the autoload blocks at the end of the decompressed ARM9 (ModuleParams, list at 0x02095540): the resident ITCM code (0x01FF8000, 0x7CE0 bytes, file offset 0x8D780) and the DTCM data
    mods["itcm"] = (0x01FF8000, a9[0x8D780:0x8D780 + 0x7CE0])
    for f in sorted(glob.glob(os.path.join(DEC, "ov9_*.bin"))):
        m = re.match(r"ov9_(\d+)_ram([0-9A-Fa-f]+)\.bin", os.path.basename(f))
        mods["ov%d" % int(m.group(1))] = (int(m.group(2), 16), open(f, "rb").read())
    return mods


def find_str(mods, text):
    t = text.encode()
    for name, (base, b) in mods.items():
        for m in re.finditer(re.escape(t), b):
            yield name, base + m.start()


def xrefs(mods, addr):
    for name, (base, b) in mods.items():
        for o in range(0, len(b) - 3, 4):
            if struct.unpack_from("<I", b, o)[0] == addr:
                yield name, base + o


def disasm(mods, name, addr, n=60, thumb=False):
    base, b = mods[name]
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
    md.detail = False
    off = addr - base
    out = []
    for i in md.disasm(b[off:off + n * (2 if thumb else 4)], addr):
        out.append("%08x  %-8s %s" % (i.address, i.mnemonic, i.op_str))
        if len(out) >= n:
            break
    return out


def lit_comment(mods, line_addr_text, name):
    return ""


if __name__ == "__main__":
    mods = modules()
    cmd = sys.argv[1]
    if cmd == "xref":
        for name, a in xrefs(mods, int(sys.argv[2], 16)):
            print(name, hex(a))
    elif cmd == "str":
        for name, a in find_str(mods, sys.argv[2]):
            print(name, hex(a))
    else:
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 60
        print("\n".join(disasm(mods, sys.argv[2], int(sys.argv[3], 16), n, cmd == "thumb")))


def annotated(mods, name, addr, n=200, thumb=False):
    """Disassemble with ldr-literal values and string pointers resolved."""
    base, b = mods[name]
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
    off = addr - base
    out = []

    def string_at(a):
        for nm, (bs, bb) in mods.items():
            if bs <= a < bs + len(bb):
                o = a - bs
                m = re.match(rb"[ -~]{3,60}", bb[o:o + 64])
                if m:
                    return m.group().decode()
        return None
    for i in md.disasm(b[off:off + n * (2 if thumb else 4)], addr):
        s = "%08x  %-7s %s" % (i.address, i.mnemonic, i.op_str)
        mm = re.match(r"(\w+), \[pc, #(0x[0-9a-f]+|\d+)\]", i.op_str) if i.mnemonic.startswith("ldr") else None
        if mm:
            d = int(mm.group(2), 0)
            la = (((i.address + 4) & ~3) + d) if thumb else (i.address + 8 + d)
            if base <= la < base + len(b) - 3:
                v = struct.unpack_from("<I", b, la - base)[0]
                st = string_at(v)
                s += "      ; =%#x%s" % (v, ('  "%s"' % st) if st else "")
        out.append(s)
        if len(out) >= n:
            break
    return out


def func_start(mods, name, addr, thumb=True, limit=0x600):
    """Scan backwards for a push {.., lr} (thumb) / stmdb sp!,{..,lr} (arm)."""
    base, b = mods[name]
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
    step = 2 if thumb else 4
    for a in range(addr, max(addr - limit, base), -step):
        i = list(md.disasm(b[a - base:a - base + step], a))
        if i and i[0].mnemonic in ("push", "stmdb") and "lr" in i[0].op_str:
            return a
    return None


def callers(mods, target):
    """Find bl/blx instructions (thumb and arm) whose target == target, in all modules."""
    out = []
    for name, (base, b) in mods.items():
        for mode, step in ((capstone.CS_MODE_THUMB, 2), (capstone.CS_MODE_ARM, 4)):
            md = capstone.Cs(capstone.CS_ARCH_ARM, mode)
            for ins in md.disasm(b, base):
                if ins.mnemonic in ("bl", "blx", "b") and ins.op_str.startswith("#"):
                    try:
                        if int(ins.op_str[1:], 0) == target:
                            out.append((name, "thumb" if mode == capstone.CS_MODE_THUMB else "arm", ins.address))
                    except ValueError:
                        pass
    return out
