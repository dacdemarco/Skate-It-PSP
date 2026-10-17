#!/usr/bin/env python3
"""Run the ORIGINAL ARM9 YUV->RGB555 converter (overlay 0, 0x01FFE9E0) on decoded VX frames and compare
with the formula  R=Y+2V, G=Y-(U>>1)-V, B=Y+2U  (Y reduced by 4 on odd x+y = 2x2 ordered dither), clamp,
>>3 per channel, bit15 = 1.

    python tools/vx_armcheck_color.py [ealogo|tempintro] [frame ...]
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vx_armcheck import Mem, CPU, OV0, OV0_BASE, SENTINEL
from vx_video import VXFile, VXVideoDecoder

FUNC = 0x01FFE9E0
S, YB, UVB, DST, STACK = 0x02A00000, 0x02B00000, 0x02B40000, 0x02B80000, 0x02A18000


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'ealogo'
    want = [int(a) for a in sys.argv[2:]] or [0, 40, 150, 200]
    vx = VXFile(os.path.join(HERE, '..', 'extracted', 'files', 'DATA', 'FMV', name + '.vx'))
    W, H = vx.width, vx.height
    mem = Mem()
    mem.add(0x02000000, open(os.path.join(HERE, '..', 'extracted', 'decomp', 'arm9.bin'), 'rb').read())
    mem.add(OV0_BASE, open(OV0, 'rb').read())
    mem.add(S - 0x100, bytearray(0x200))
    mem.add(STACK - 0x2000, bytearray(0x2000))
    mem.add(YB, bytearray(256 * H + 0x1000))
    mem.add(UVB, bytearray(256 * H // 2 + 0x1000))
    mem.add(DST, bytearray(W * H * 2 + 0x1000))
    dec = VXVideoDecoder(W, H, vx.quant)
    cpu = CPU(mem, None)
    ok_all = True
    for i in range(max(want) + 1):
        data, _ = vx.frame_payload(i)
        dec.decode_frame(data)
        if i not in want:
            continue
        y, u, v = dec.planes()
        d, o = mem.find(YB, 256 * H)
        d[o:o + 256 * H] = y
        d, o = mem.find(UVB, 256 * H // 2)
        for r in range(H // 2):
            row = bytearray(256)
            row[0:W:2] = u[r * (W // 2):(r + 1) * (W // 2)]
            row[1:W:2] = v[r * (W // 2):(r + 1) * (W // 2)]
            d[o + r * 256:o + r * 256 + 256] = row
        mem.wr(S + 0x00, 4, YB)
        mem.wr(S + 0x04, 4, UVB)
        mem.wr(S + 0x08, 4, DST)
        mem.wr(S + 0x0C, 4, W * 2)       # destination row stride in bytes
        mem.wr(S + 0x10, 4, W)
        mem.wr(S + 0x14, 4, H)
        cpu.r[:] = [0] * 16
        cpu.r[0] = S
        cpu.r[13] = STACK
        cpu.r[14] = SENTINEL
        cpu.run(FUNC, maxi=20_000_000)
        d, o = mem.find(DST, W * H * 2)
        emu = np.frombuffer(bytes(d[o:o + W * H * 2]), '<u2').reshape(H, W)
        Y = np.frombuffer(y, np.uint8).reshape(H, W).astype(np.int32)
        U = np.frombuffer(u, np.uint8).reshape(H // 2, W // 2).astype(np.int32) - 128
        V = np.frombuffer(v, np.uint8).reshape(H // 2, W // 2).astype(np.int32) - 128
        U = U.repeat(2, 0).repeat(2, 1)
        V = V.repeat(2, 0).repeat(2, 1)
        yy, xx = np.mgrid[0:H, 0:W]
        Yd = Y - 4 * ((xx + yy) & 1)                       # the DS converter dithers: luma-4 on odd (x+y)
        R = np.clip(Yd + 2 * V, 0, 255) >> 3
        G = np.clip(Yd - (U >> 1) - V, 0, 255) >> 3
        B = np.clip(Yd + 2 * U, 0, 255) >> 3
        mine = (R | (G << 5) | (B << 10) | 0x8000).astype(np.uint16)
        diff = int((emu != mine).sum())
        print('frame %d: ARM converter vs formula: %d of %d pixels differ' % (i, diff, W * H))
        ok_all &= diff == 0
    print('ALL MATCH' if ok_all else 'DIFFERENCES FOUND')


if __name__ == '__main__':
    main()
