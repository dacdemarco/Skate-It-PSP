#!/usr/bin/env python3
"""Nintendo DS geometry-engine (GX) command-stream parser.

A stream is a sequence of 32-bit words each packing up to 4 command bytes
(low byte first), followed by the parameter words of those commands in order.
Command table / semantics: GBATEK "DS 3D Geometry Commands".
"""
import struct

PARAMS = {
    0x00: 0, 0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 0, 0x16: 16,
    0x17: 12, 0x18: 16, 0x19: 12, 0x1A: 9, 0x1B: 3, 0x1C: 3,
    0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2, 0x24: 1, 0x25: 1, 0x26: 1, 0x27: 1,
    0x28: 1, 0x29: 1, 0x2A: 1, 0x2B: 1, 0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1,
    0x34: 32, 0x40: 1, 0x41: 0, 0x50: 1, 0x60: 1, 0x70: 3, 0x71: 2, 0x72: 1,
}
NAMES = {
    0x10: "MTX_MODE", 0x11: "MTX_PUSH", 0x12: "MTX_POP", 0x13: "MTX_STORE",
    0x14: "MTX_RESTORE", 0x15: "MTX_IDENTITY", 0x16: "MTX_LOAD_4x4",
    0x17: "MTX_LOAD_4x3", 0x18: "MTX_MULT_4x4", 0x19: "MTX_MULT_4x3",
    0x1A: "MTX_MULT_3x3", 0x1B: "MTX_SCALE", 0x1C: "MTX_TRANS", 0x20: "COLOR",
    0x21: "NORMAL", 0x22: "TEXCOORD", 0x23: "VTX_16", 0x24: "VTX_10",
    0x25: "VTX_XY", 0x26: "VTX_XZ", 0x27: "VTX_YZ", 0x28: "VTX_DIFF",
    0x29: "POLYGON_ATTR", 0x2A: "TEXIMAGE_PARAM", 0x2B: "PLTT_BASE",
    0x30: "DIF_AMB", 0x31: "SPE_EMI", 0x32: "LIGHT_VECTOR", 0x33: "LIGHT_COLOR",
    0x34: "SHININESS", 0x40: "BEGIN_VTXS", 0x41: "END_VTXS", 0x50: "SWAP_BUFFERS",
    0x60: "VIEWPORT", 0x70: "BOX_TEST", 0x71: "POS_TEST", 0x72: "VEC_TEST",
}


def parse(data, start=0, end=None):
    """Yield (cmd, [params]) until the data ends or an unknown command shows up.
    Returns (commands, offset_where_it_stopped, reason)."""
    end = len(data) if end is None else end
    p, out = start, []
    while p + 4 <= end:
        cmds = data[p:p + 4]
        p += 4
        for c in cmds:
            if c == 0:
                continue
            if c not in PARAMS:
                return out, p - 4, "unknown cmd 0x%02X" % c
            n = PARAMS[c]
            if p + n * 4 > end:
                return out, p, "truncated"
            out.append((c, list(struct.unpack_from("<%dI" % n, data, p))))
            p += n * 4
    return out, p, "end"


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def vtx16(p):
    """VTX_16: 3 x s16 (1.3.12 fixed) packed in two words."""
    return (s16(p[0]) / 4096, s16(p[0] >> 16) / 4096, s16(p[1]) / 4096)


def vtx10(w):
    """VTX_10: 3 x s10 (1.3.6 fixed) packed in one word."""
    def f(v):
        v &= 0x3FF
        return (v - 0x400 if v & 0x200 else v) / 64
    return (f(w), f(w >> 10), f(w >> 20))
