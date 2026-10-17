#!/usr/bin/env python3
"""Find code that loads a constant: campaign_refs.py <hexvalue> [module]
Step 1: literal-pool words equal to value. Step 2: Thumb 'ldr rN,[pc,#imm]' and ARM 'ldr rN,[pc,#imm]' instructions that load those words.
Prints module, instruction address, thumb/arm, register."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdis
mods = armdis.modules()
val = int(sys.argv[1], 16)
only = sys.argv[2] if len(sys.argv) > 2 else None
for name, (base, b) in mods.items():
    if only and name != only: continue
    lits = [base + o for o in range(0, len(b) - 3, 4) if struct.unpack_from("<I", b, o)[0] == val]
    if not lits: continue
    ls = set(lits)
    for o in range(0, len(b) - 1, 2):
        h = struct.unpack_from("<H", b, o)[0]
        if (h & 0xF800) == 0x4800:
            a = base + o
            t = ((a + 4) & ~3) + (h & 0xFF) * 4
            if t in ls: print(name, "%08x" % a, "thumb ldr r%d" % ((h >> 8) & 7), "lit %08x" % t)
    for o in range(0, len(b) - 3, 4):
        w = struct.unpack_from("<I", b, o)[0]
        if (w & 0x0F7F0000) == 0x051F0000:
            a = base + o
            imm = w & 0xFFF
            t = a + 8 + (imm if (w >> 23) & 1 else -imm)
            if t in ls: print(name, "%08x" % a, "arm ldr r%d" % ((w >> 12) & 15), "lit %08x" % t)
