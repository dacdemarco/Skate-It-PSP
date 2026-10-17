#!/usr/bin/env python3
"""Run the story (NIS) code of the original ARM9 overlays in Unicorn to see what it does: where the actors are put, how the camera is set. The engine around it is NOT emulated: calls that
leave the allowed ranges are skipped (r0 = 0 or a value from STUBS), so only the code under study really runs. usage: nis_emu.py <scene 1-based> [frames]
Needs `pip install unicorn capstone` and the decompressed modules in extracted/decomp (make_data.py, step extract)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import armdis, capstone
from unicorn import *
from unicorn.arm_const import *

mods = armdis.modules()
uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
uc.mem_map(0x01FF8000, 0x8000)
uc.mem_map(0x02000000, 0x00400000)
uc.mem_map(0x027E0000, 0x4000)
uc.mem_map(0x04000000, 0x2000)
for nm in ("arm9", "itcm", "ov12", "ov14", "ov8"):
    base, b = mods[nm]
    if nm == "arm9":
        b = b[:0x8D780]
    uc.mem_write(base, b)
# the DTCM autoload block (0x27E0000, 0xE0 bytes of data)
uc.mem_write(0x027E0000, mods["arm9"][1][0x95460:0x95460 + 0xE0])

ALLOW = [(0x020ED9C0, 0x0210B7C0), (0x0210F180, 0x02112B60), (0x01FF8000, 0x02000000), (0x02048000, 0x02050000), (0x02063000, 0x02067000), (0x0206C000, 0x0206D000), (0x02016B00, 0x02017E00), (0x0201C7C0, 0x0201C900)]
STUBS = {0x0202A060: 0x02210000, 0x02017434: 0x02210000, 0x0201A530: 0x02210000, 0x0201749C: 0x02210000, 0x0201F5A0: 0x02210000, 0x0201A484: 0x02210000}          # target -> return value (string getters: an empty string)
HEAP = [0x02200000]


def alloc(u):
    size = (u.reg_read(UC_ARM_REG_R0) + 31) & ~31
    a = HEAP[0]
    HEAP[0] += max(size, 32)
    return a


HLE = {0x01FFBB08: alloc, 0x01FFBB20: alloc, 0x01FFBAE0: alloc}      # allocators of the ITCM code
ENT = 0x0210D984    # the player entities (0x530 bytes each, overlay 12 bss)
STACK = 0x023F0000
LOG = []
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
mda = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)


def allowed(a):
    return any(lo <= a < hi for lo, hi in ALLOW)


def hook_code(u, addr, size, ud):
    cpsr = u.reg_read(UC_ARM_REG_CPSR)
    thumb = bool(cpsr & 0x20)
    code = bytes(u.mem_read(addr, 4))
    ins = list((md if thumb else mda).disasm(code, addr, 1))
    if not ins:
        return
    i = ins[0]
    if i.mnemonic in ("bl", "blx") and i.op_str.startswith("#"):
        t = int(i.op_str[1:], 16)
        if t in HLE:
            u.reg_write(UC_ARM_REG_R0, HLE[t](u))
            u.reg_write(UC_ARM_REG_PC, (addr + i.size) | (1 if thumb else 0))
        elif not allowed(t):
            ret = STUBS.get(t, 0)
            LOG.append("stub %08x from %08x -> %d" % (t, addr, ret))
            u.reg_write(UC_ARM_REG_R0, ret)
            u.reg_write(UC_ARM_REG_PC, (addr + i.size) | (1 if thumb else 0))
    elif i.mnemonic in ("bx", "blx") and not i.op_str.startswith("#"):
        pass


def hook_write(u, access, addr, size, value, ud):
    off = addr - ENT
    pc = u.reg_read(UC_ARM_REG_PC)
    if 0 <= off < 0x530 * 8 and (off % 0x530) in range(0x240, 0x270):
        LOG.append("ENTITY %d +%#x <- %#x (%d) at pc %#x" % (off // 0x530, off % 0x530, value & 0xFFFFFFFF, size, pc))


uc.hook_add(UC_HOOK_CODE, hook_code)
uc.hook_add(UC_HOOK_MEM_WRITE, hook_write)


def hook_unmapped(u, access, addr, size, value, ud):
    LOG.append("UNMAPPED %s %#x size %d at pc %#x lr %#x" % ("write" if access in (UC_MEM_WRITE_UNMAPPED,) else "read", addr, size, u.reg_read(UC_ARM_REG_PC), u.reg_read(UC_ARM_REG_LR)))
    return False


uc.hook_add(UC_HOOK_MEM_UNMAPPED, hook_unmapped)


def call(addr, *args, thumb=True, max_ins=2000000):
    for i, a in enumerate(args[:4]):
        uc.reg_write(UC_ARM_REG_R0 + i, a & 0xFFFFFFFF)
    uc.reg_write(UC_ARM_REG_SP, STACK)
    sentinel = 0x02300000
    uc.mem_write(sentinel, b"\x00\xbe\x00\xbe")                   # bkpt in Thumb
    uc.reg_write(UC_ARM_REG_LR, sentinel | 1)
    try:
        uc.emu_start(addr | (1 if thumb else 0), sentinel, count=max_ins)
    except UcError as e:
        LOG.append("UcError %s at pc %#x" % (e, uc.reg_read(UC_ARM_REG_PC)))
    return uc.reg_read(UC_ARM_REG_R0)


def w32(a, v): uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))
def r32(a): return struct.unpack("<I", bytes(uc.mem_read(a, 4)))[0]


def level_tables(chal):
    """fill the challenge block and the global start grid from the converted level objects (tools/campaign_levels.json), the way overlay 13 does"""
    import json, math
    camp = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "campaign_data.json")))
    lv = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "campaign_levels.json")))
    c = camp["challenges"][chal]
    zip_ = [l["zip"] for l in camp["levels"] if l["id"] == c["location"]][0]
    objs = lv[zip_]["objects"]
    slot = c["slot"]
    fx = lambda v: int(round(v * 4096.0))
    deg = lambda a, b: int(round(math.atan2(b[0] - a[0], b[2] - a[2]) / (2 * math.pi) * 65536)) % 65536      # the game's heading: 1/65536 turn from +z towards +x (routine 0x0210F21C)
    blk = 0x02095D28 + 0xB0 * slot
    n = 0
    for k in range(1, 5):
        p = objs.get("chal%03d_start_pos_%d" % (slot, k)); d = objs.get("chal%03d_start_dir_%d" % (slot, k))
        if p:
            for j in range(3): w32(blk + 0x1C + 12 * n + 4 * j, fx(p[0][j]))
            w32(blk + 0x4C + 4 * n, deg(p[0], d[0]) if d else 0)
            n += 1
    uc.mem_write(blk + 1, bytes([n]))
    g = 0
    for k in range(1, 5):
        p = objs.get("start_pos_%d" % k); d = objs.get("start_dir_%d" % k)
        if p:
            for j in range(3): w32(0x02095A40 + 12 * g + 4 * j, fx(p[0][j]))
            w32(0x02095A70 + 4 * g, deg(p[0], d[0]) if d else 0)
            g += 1
    w32(0x02095A38, g); w32(0x02095A3C, g)
    w32(0x0210CD84 + 0xC, slot)            # the current challenge
    return c, zip_, n, g


if __name__ == "__main__":
    scene = int(sys.argv[1])
    if len(sys.argv) > 3:
        print("challenge", sys.argv[3], level_tables(int(sys.argv[3])))
    w32(0x0210B760, 1)                     # the NIS is enabled
    w32(0x027E11F8, 0)                     # the current player entity
    w32(0x027E11FC, 1)                     # one player
    print("Player_Init(0):", call(0x02102368, ENT, 0, 1, 0))     # entity 0, slot 0, kind 1 (human), skater 0
    LOG.append("-- NIS_Start --")
    r = call(0x021070B8, scene - 1)
    print("NIS_Start returned", r)
    LOG.append("-- frames --")
    for f in range(int(sys.argv[2]) if len(sys.argv) > 2 else 3):
        call(0x02107B90, 0x1000)
    for l in LOG[-120:]:
        print(l)
    for k in range(2):
        e = ENT + 0x530 * k
        print("entity", k, "kind", r32(e + 8), "slot", r32(e), "skater", r32(e + 4), "pos", [struct.unpack('<i', struct.pack('<I', r32(e + 0x26c + 4 * j)))[0] / 4096.0 for j in range(3)])
    print("clock", r32(0x0210EF20 + 0x30), "len", r32(0x0210EF20 + 0x2c), "records", r32(0x0210EF20 + 0x28))
    e1 = ENT + 0x530
    print("entity 1 physics block +0x24c..+0x2b0:", " ".join("%08x" % r32(e1 + 0x24C + 4 * j) for j in range(0, 0x64 // 4)))
    S, C = 0x02112748, 0x021133E8
    print("camera state: mode", r32(S), "target actor", r32(S + 0x14), "style", r32(S + 0x10), "distance", r32(S + 0xC) / 4096.0, "yaw", r32(C + 8), "flag", r32(C))
    w32(C, 1)
    call(0x02111270, thumb=False)
    sg = lambda a: [struct.unpack("<i", struct.pack("<I", r32(a + 4 * j)))[0] / 4096.0 for j in range(3)]
    print("look-at", sg(0x0210EFDC), "camera", sg(0x0210EFE8))
