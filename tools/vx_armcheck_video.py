#!/usr/bin/env python3
"""Run the ORIGINAL ARM9 VX *video* decoder (overlay 0 @ 0x01FF9000) in the tiny ARM interpreter from
vx_armcheck.py and compare every decoded frame (Y, U, V planes) against tools/vx_video.py.

    python tools/vx_armcheck_video.py [ealogo|tempintro] [max_frames]

State handed to the decoder replicates what the game's VX player builds (ARM9 0x0203FA54): a 0x80-byte
parameter block {bitstream ptr, 4 x (Y ptr, UV ptr) = ref0, ref1, ref2, cur, work offsets, nonzero-coeff
caches, MV cache, 8 dequant ints, intra4x4 cache of 25 x 9, width, height}.  The DS keeps chroma as
interleaved U,V bytes (stride 256); this script de-interleaves for the comparison.
"""
import os, struct, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vx_armcheck import Mem, CPU, OV0, OV0_BASE, SENTINEL, M32
from vx_video import VXFile, VXVideoDecoder

ARM9 = os.path.join(HERE, '..', 'extracted', 'decomp', 'arm9.bin')
FUNC = 0x01FF9000
QTAB_ROM = 0x0207AFD0
F = 0x02A00000
STACK = 0x02A18000
BITS = 0x02A20000
NCY, NCUV, MVA = 0x02A30000, 0x02A32000, 0x02A34000
YB, UVB = 0x02B00000, 0x02C00000


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'ealogo'
    maxf = int(sys.argv[2]) if len(sys.argv) > 2 else 10 ** 9
    vx = VXFile(os.path.join(HERE, '..', 'extracted', 'files', 'DATA', 'FMV', name + '.vx'))
    W, H = vx.width, vx.height
    assert W == 256
    arm9 = open(ARM9, 'rb').read()
    mem = Mem()
    mem.add(0x02000000, arm9)
    mem.add(OV0_BASE, open(OV0, 'rb').read())
    mem.add(F - 0x100, bytearray(0x200))
    mem.add(STACK - 0x8000, bytearray(0x8000))
    mem.add(BITS, bytearray(0x10000))
    mem.add(NCY - 0x100, bytearray(0x2100))
    mem.add(NCUV - 0x100, bytearray(0x2100))
    mem.add(MVA - 0x100, bytearray(0x2100))
    ysz, uvsz = 256 * H, 256 * (H // 2)
    for k in range(4):
        mem.add(YB + k * 0x20000 - 0x1000, bytearray(b'\0' * (0x1000 + ysz + 0x1000)))
        mem.add(UVB + k * 0x20000 - 0x1000, bytearray(b'\x80' * (0x1000 + uvsz + 0x1000)))
    ybase = lambda k: YB + k * 0x20000
    uvbase = lambda k: UVB + k * 0x20000
    q = vx.quant
    qrow = [struct.unpack_from('<i', arm9, QTAB_ROM - 0x02000000 + (q % 6) * 32 + 4 * k)[0] << (q // 6) for k in range(8)]
    ids = [0, 1, 2, 3]            # slot k -> buffer id ; slot 3 = current
    cpu = CPU(mem, None)
    py = VXVideoDecoder(W, H, q)
    bad = 0
    t0 = time.time()
    for i in range(min(vx.nframes, maxf)):
        data, na = vx.frame_payload(i)
        d, o = mem.find(BITS, len(data))
        d[o:o + len(data)] = data
        mem.wr(F + 0x00, 4, BITS)
        for k in range(4):
            mem.wr(F + 0x04 + 8 * k, 4, ybase(ids[k]))
            mem.wr(F + 0x08 + 8 * k, 4, uvbase(ids[k]))
        mem.wr(F + 0x24, 4, 0)
        mem.wr(F + 0x28, 4, 0)
        mem.wr(F + 0x2C, 4, NCY + 0x42)
        mem.wr(F + 0x30, 4, NCUV + 0x22)
        mem.wr(F + 0x34, 4, MVA + 0x4C)
        for k in range(8):
            mem.wr(F + 0x38 + 4 * k, 4, qrow[k])
        for k in range(32):
            mem.wr(F + 0x58 + k, 1, 9 if k < 25 else 0)
        mem.wr(F + 0x78, 4, W)
        mem.wr(F + 0x7C, 4, H)
        cpu.r[:] = [0] * 16
        cpu.r[0] = F
        cpu.r[13] = STACK
        cpu.r[14] = SENTINEL
        cpu.run(FUNC, maxi=20_000_000)
        end = py.decode_frame(data)
        py_y, py_u, py_v = py.planes()
        cur = ids[3]
        ey = bytes(mem.find(ybase(cur), ysz)[0][mem.find(ybase(cur), ysz)[1]:][:ysz])
        d, o = mem.find(uvbase(cur), uvsz)
        uv = bytes(d[o:o + uvsz])
        eu = bytearray()
        ev = bytearray()
        for r in range(H // 2):
            row = uv[r * 256:r * 256 + W]
            eu += row[0::2]
            ev += row[1::2]
        ok = (ey == py_y, bytes(eu) == py_u, bytes(ev) == py_v)
        if not all(ok):
            bad += 1
            if bad <= 3:
                nbad = sum(1 for a, b in zip(ey, py_y) if a != b)
                print('frame %d MISMATCH planes ok=%s, luma differs in %d px' % (i, ok, nbad))
                if nbad:
                    first = next(j for j in range(len(ey)) if ey[j] != py_y[j])
                    print('   first luma diff at x=%d y=%d: arm=%d py=%d' % (first % W, first // W, ey[first], py_y[first]))
        ids = [ids[3], ids[0], ids[1], ids[2]]
        if i % 20 == 0:
            print('frame %d done, %d ARM instr, %.0fs' % (i, cpu.icount, time.time() - t0), flush=True)
    print('%s: %d frames compared, %d mismatching frames, %d ARM instructions' % (name, min(vx.nframes, maxf), bad, cpu.icount))


if __name__ == '__main__':
    main()
