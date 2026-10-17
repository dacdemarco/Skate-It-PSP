#!/usr/bin/env python3
"""Run the ORIGINAL ARM9 VX audio decoder (overlay 0, 0x01FFF708) in a tiny ARM interpreter and compare
its PCM output sample-for-sample against tools/vx_audio.py.

    python tools/vx_armcheck.py [ealogo|tempintro] [max_frames]

The interpreter implements only the ~35 instruction forms used in 0x1FFEF40..0x1FFF89C (checked by an
assertion: anything else raises).  Needs capstone (already used by tools/armdis.py).
"""
import os, re, struct, sys
import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vx_video import VXFile, VXVideoDecoder
from vx_audio import VXAudioDecoder, FRAME_LEN

OV0 = os.path.join(HERE, '..', 'extracted', 'decomp', 'ov9_0000_ram01FF9000.bin')
OV0_BASE = 0x01FF9000
FUNC = 0x01FFF708
STATE = 0x02800000          # state struct (0x14F4 bytes) lives here
DATA = 0x02810000           # frame bytes
OUT = 0x02820000            # pcm out
STACK = 0x02830000
SENTINEL = 0xFFFF0000
M32 = 0xFFFFFFFF
REGN = {'r%d' % i: i for i in range(13)}
REGN.update({'sb': 9, 'sl': 10, 'fp': 11, 'ip': 12, 'sp': 13, 'lr': 14, 'pc': 15})


class Mem:
    def __init__(self):
        self.reg = {}

    def add(self, base, data):
        self.reg[base] = bytearray(data)

    def find(self, a, n):
        for b, d in self.reg.items():
            if b <= a and a + n <= b + len(d):
                return d, a - b
        raise IndexError('bad mem access %#x' % a)

    def rd(self, a, n, signed=False):
        d, o = self.find(a, n)
        return int.from_bytes(d[o:o + n], 'little', signed=signed)

    def wr(self, a, n, v):
        d, o = self.find(a, n)
        d[o:o + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


class CPU:
    def __init__(self, mem, code):
        self.m = mem
        self.r = [0] * 16
        self.N = self.Z = self.C = self.V = 0
        self.md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        self.md.detail = True
        self.code = code
        self.cache = {}
        self.icount = 0

    def decode(self, pc):
        i = self.cache.get(pc)
        if i is None:
            raw = self.m.rd(pc, 4).to_bytes(4, 'little')
            ins = list(self.md.disasm(raw, pc, 1))
            assert ins, 'undecodable %#x' % pc
            i = ins[0]
            self.cache[pc] = i
        return i

    def cond(self, cc):
        C = capstone.arm_const
        N, Z, Cf, V = self.N, self.Z, self.C, self.V
        table = {C.ARM_CC_EQ: Z, C.ARM_CC_NE: not Z, C.ARM_CC_HS: Cf, C.ARM_CC_LO: not Cf,
                 C.ARM_CC_MI: N, C.ARM_CC_PL: not N, C.ARM_CC_VS: V, C.ARM_CC_VC: not V,
                 C.ARM_CC_HI: Cf and not Z, C.ARM_CC_LS: (not Cf) or Z, C.ARM_CC_GE: N == V,
                 C.ARM_CC_LT: N != V, C.ARM_CC_GT: (not Z) and N == V, C.ARM_CC_LE: Z or N != V,
                 C.ARM_CC_AL: True}
        return bool(table[cc])

    def val(self, tok, pc):
        tok = tok.strip()
        if tok.startswith('#'):
            return int(tok[1:], 0) & M32
        r = REGN[tok]
        return (pc + 8) & M32 if r == 15 else self.r[r]

    def op2(self, parts, pc):
        """parts: tokens after rn (e.g. ['r3'] or ['r3','lsl #2'] or ['#4']) -> (value, carry)"""
        v = self.val(parts[0], pc)
        c = self.C
        if len(parts) > 1:
            m = re.match(r'(lsl|lsr|asr|ror) (#?\w+)', parts[1].strip())
            kind = m.group(1)
            n = int(m.group(2)[1:], 0) if m.group(2).startswith('#') else (self.r[REGN[m.group(2)]] & 0xFF)
            if kind == 'lsl':
                v = (v << n) & M32 if n < 32 else 0
            elif kind == 'lsr':
                v = v >> n if n < 32 else 0
            elif kind == 'asr':
                v = (s32(v) >> n) & M32 if n else v
            else:
                v = ((v >> n) | (v << (32 - n))) & M32
        return v, c

    def setnz(self, v):
        self.N = (v >> 31) & 1
        self.Z = 1 if (v & M32) == 0 else 0

    def run(self, entry, maxi=5_000_000):
        self.r[15] = entry
        start = self.icount
        while self.r[15] != SENTINEL:
            self.icount += 1
            assert self.icount - start < maxi
            pc = self.r[15]
            i = self.decode(pc)
            self.r[15] = pc + 4
            if not self.cond(i.cc):
                continue
            self.exec(i, pc)

    def exec(self, i, pc):
        mn = i.mnemonic
        ops = i.op_str
        upd = i.update_flags
        # strip condition suffix
        base = None
        for b in ('push', 'pop', 'ldmdb', 'stmdb', 'ldm', 'stm', 'ldrsh', 'ldrsb', 'ldrh', 'ldrb', 'strb', 'strh', 'ldr', 'str',
                  'mla', 'mul', 'smulbb', 'clz', 'tst', 'cmn', 'bl', 'bx', 'b', 'mov', 'mvn', 'add', 'sub', 'rsb', 'and',
                  'orr', 'eor', 'bic', 'cmp', 'asr', 'lsl', 'lsr', 'ror'):
            if mn.startswith(b):
                rest = mn[len(b):]
                if rest in ('', 's', 'eq', 'ne', 'hs', 'lo', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', 'al') \
                        or (rest[:-1] in ('eq', 'ne', 'hs', 'lo', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le') and rest.endswith('s')):
                    base = b
                    break
        assert base, 'unsupported %s %s @%#x' % (mn, ops, pc)
        R = self.r
        if base in ('stmdb', 'ldmdb'):
            m = re.match(r'(\w+)(!?), \{(.*)\}', ops)
            regs = sorted(REGN[x.strip()] for x in m.group(3).split(','))
            rn = REGN[m.group(1)]
            a = (R[rn] - 4 * len(regs)) & M32
            for k, rr in enumerate(regs):
                if base == 'ldmdb':
                    R[rr] = self.m.rd(a + 4 * k, 4)
                else:
                    self.m.wr(a + 4 * k, 4, R[rr] if rr != 15 else pc + 8)
            if m.group(2):
                R[rn] = a
            return
        if base in ('push', 'pop', 'ldm', 'stm'):
            m = re.match(r'(?:(\w+)(!?), )?\{(.*)\}', ops)
            regs = [REGN[x.strip()] for x in m.group(3).split(',')]
            regs.sort()
            if base == 'push':
                sp = R[13] - 4 * len(regs)
                for k, rr in enumerate(regs):
                    self.m.wr(sp + 4 * k, 4, R[rr] if rr != 15 else pc + 8)
                R[13] = sp
            elif base == 'pop':
                sp = R[13]
                for k, rr in enumerate(regs):
                    R[rr] = self.m.rd(sp + 4 * k, 4)
                R[13] = sp + 4 * len(regs)
            else:
                rn = REGN[m.group(1)]
                a = R[rn]
                for k, rr in enumerate(regs):
                    if base == 'ldm':
                        R[rr] = self.m.rd(a + 4 * k, 4)
                    else:
                        self.m.wr(a + 4 * k, 4, R[rr])
                if m.group(2):
                    R[rn] = (a + 4 * len(regs)) & M32
            return
        if base in ('b', 'bl', 'bx'):
            if base == 'bx':
                R[15] = R[REGN[ops.strip()]] & ~1
                return
            tgt = int(ops.strip().lstrip('#'), 0)
            if base == 'bl':
                R[14] = pc + 4
            R[15] = tgt
            return
        if base in ('ldr', 'str', 'ldrb', 'strb', 'ldrh', 'strh', 'ldrsh', 'ldrsb'):
            m = re.match(r'(\w+), \[(\w+)(?:, ([^\]]+))?\](!?)(?:, (.*))?$', ops)
            rt, rn, off, wb, post = m.group(1), REGN[m.group(2)], m.group(3), m.group(4), m.group(5)
            basev = (pc + 8) & M32 if rn == 15 else R[rn]
            if off is None:
                o = 0
            else:
                parts = [x.strip() for x in off.split(',')]
                neg = parts[0].startswith('-')
                if neg:
                    parts[0] = parts[0][1:]
                o, _ = self.op2(parts, pc)
                if neg:
                    o = -o
            if post is not None:
                pparts = [x.strip() for x in post.split(',')]
                neg = pparts[0].startswith('#-')
                pv, _ = self.op2(pparts, pc)
                pv = s32(pv)
                addr = basev
                newbase = basev + pv
            else:
                addr = (basev + s32(o)) & M32
                newbase = addr
            size = {'ldr': 4, 'str': 4, 'ldrb': 1, 'strb': 1, 'ldrh': 2, 'strh': 2, 'ldrsh': 2, 'ldrsb': 1}[base]
            if base.startswith('ldr'):
                v = self.m.rd(addr, size, signed=(base in ('ldrsh', 'ldrsb'))) & M32
                R[REGN[rt]] = v
            else:
                self.m.wr(addr, size, R[REGN[rt]])
            if post is not None or wb:
                R[rn] = newbase & M32
            return
        parts = [x.strip() for x in re.split(r',(?![^\[]*\])', ops)]
        if base in ('mul', 'mla'):
            rd = REGN[parts[0]]
            a, b = self.val(parts[1], pc), self.val(parts[2], pc)
            v = (a * b + (self.val(parts[3], pc) if base == 'mla' else 0)) & M32
            R[rd] = v
            if upd:
                self.setnz(v)
            return
        if base in ('asr', 'lsl', 'lsr', 'ror'):
            rd = REGN[parts[0]]
            src = self.val(parts[1], pc)
            n = self.val(parts[2], pc) if len(parts) > 2 else 0
            cflag = self.C
            if base == 'lsl':
                v = (src << n) & M32
                if n:
                    cflag = (src >> (32 - n)) & 1 if n <= 32 else 0
            elif base == 'lsr':
                v = src >> n if n < 32 else 0
                if n:
                    cflag = (src >> (n - 1)) & 1 if n <= 32 else 0
            elif base == 'asr':
                v = (s32(src) >> min(n, 31)) & M32
                if n:
                    cflag = (s32(src) >> min(n - 1, 31)) & 1
            else:
                v = ((src >> n) | (src << (32 - n))) & M32
            R[rd] = v
            if upd:
                self.setnz(v)
                self.C = cflag
            return
        if base == 'smulbb':
            rd = REGN[parts[0]]
            a = self.val(parts[1], pc) & 0xFFFF
            b = self.val(parts[2], pc) & 0xFFFF
            a = a - 0x10000 if a & 0x8000 else a
            b = b - 0x10000 if b & 0x8000 else b
            R[rd] = (a * b) & M32
            return
        if base == 'clz':
            v = self.val(parts[1], pc)
            R[REGN[parts[0]]] = 32 - v.bit_length()
            return
        if base in ('tst', 'cmn'):
            a = self.val(parts[0], pc)
            b, _ = self.op2(parts[1:], pc)
            if base == 'tst':
                self.setnz(a & b)
            else:
                res = (a + b) & M32
                self.setnz(res)
                self.C = 1 if (a + b) > M32 else 0
                self.V = 1 if (s32(a) + s32(b)) != s32(res) else 0
            return
        if base in ('mov', 'mvn'):
            rd = REGN[parts[0]]
            v, _ = self.op2(parts[1:], pc)
            if base == 'mvn':
                v = ~v & M32
            R[rd] = v & M32
            if upd:
                self.setnz(v)
            return
        if base == 'cmp':
            a = self.val(parts[0], pc)
            b, _ = self.op2(parts[1:], pc)
            res = (a - b) & M32
            self.setnz(res)
            self.C = 1 if a >= b else 0
            self.V = 1 if ((s32(a) - s32(b)) != s32(res)) else 0
            return
        rd = REGN[parts[0]]
        a = self.val(parts[1], pc)
        b, _ = self.op2(parts[2:], pc)
        if base == 'add':
            v = a + b
        elif base == 'sub':
            v = a - b
        elif base == 'rsb':
            v = b - a
        elif base == 'and':
            v = a & b
        elif base == 'orr':
            v = a | b
        elif base == 'eor':
            v = a ^ b
        elif base == 'bic':
            v = a & ~b
        else:
            raise AssertionError(base)
        v &= M32
        R[rd] = v
        if upd:
            self.setnz(v)
            if base == 'sub':
                self.C = 1 if a >= b else 0
                self.V = 1 if (s32(a) - s32(b)) != s32(v) else 0
            elif base == 'add':
                self.C = 1 if (a + b) > M32 else 0
                self.V = 1 if (s32(a) + s32(b)) != s32(v) else 0
            elif base == 'rsb':
                self.C = 1 if b >= a else 0
                self.V = 1 if (s32(b) - s32(a)) != s32(v) else 0


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'ealogo'
    maxf = int(sys.argv[2]) if len(sys.argv) > 2 else 10 ** 9
    vx = VXFile(os.path.join(HERE, '..', 'extracted', 'files', 'DATA', 'FMV', name + '.vx'))
    mem = Mem()
    mem.add(OV0_BASE, open(OV0, 'rb').read())
    mem.add(STATE, bytearray(0x1000 + 0x14F4))
    mem.add(DATA, bytearray(0x10000))
    mem.add(OUT, bytearray(0x1000))
    mem.add(STACK - 0x4000, bytearray(0x4000))
    mem.wr(STATE + 0x8C0 + 0, 0, 0)
    d, o = mem.find(STATE + 0x8C0, 3124)
    d[o:o + 3124] = vx.extradata
    cpu = CPU(mem, None)
    ref = VXAudioDecoder(vx.extradata)
    vdec = VXVideoDecoder(vx.width, vx.height, vx.quant)
    nfr = 0
    mism = 0
    for i in range(min(vx.nframes, maxf)):
        data, na = vx.frame_payload(i)
        end = vdec.decode_frame(data)
        pos = (end + 15) // 16 * 2
        for _ in range(na):
            fl = FRAME_LEN[(struct.unpack_from('<H', data, pos + 2)[0] >> 12) & 3]
            frame = data[pos:pos + fl]
            pcm, _ = ref.decode(data, pos)
            d, o = mem.find(DATA, 32)
            d[o:o + len(frame)] = frame
            mem.wr(STATE, 4, DATA)
            mem.wr(STATE + 4, 4, OUT)
            cpu.r[:] = [0] * 16
            cpu.r[0] = STATE
            cpu.r[13] = STACK
            cpu.r[14] = SENTINEL
            cpu.run(FUNC)
            nbytes = cpu.r[0]
            emu = [mem.rd(OUT + 2 * k, 2, signed=True) for k in range(128)]
            nfr += 1
            if emu != pcm or nbytes != fl:
                mism += 1
                if mism <= 3:
                    bad = [k for k in range(128) if emu[k] != pcm[k]]
                    print('MISMATCH video frame %d: first bad sample %s, bytes %d vs %d' % (i, bad[:5], nbytes, fl))
            pos += fl
    print('%s: %d audio frames compared, %d mismatches, %d ARM instructions executed' % (name, nfr, mism, cpu.icount))


if __name__ == '__main__':
    main()
