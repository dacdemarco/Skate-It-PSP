"""Actimagine VX audio decoder (128-sample LPC/pulse codec), pure Python.

Ported by hand from the ARM9 code that ships in the ROM (overlay 0, ITCM @ 0x01FF9000, functions
0x1FFEF40 .. 0x1FFF8A0: main = 0x1FFF708), NOT from a third-party decoder.  See docs/VX_FORMAT.md for the
address map of the original routines.  tools/vx_armcheck.py runs the original ARM code in a tiny
interpreter and compares it bit-exactly against this port.

Container side: each video frame is  u16 size | u16 n_audio | video bitstream | n_audio audio frames.
An audio frame is 10/12/14/20 bytes (pulse mode 3/2/1/0).  The 3124-byte "extradata" at the header's
audio offset holds 3 LPC code books (64 x 8 s16 each), 8 s16 scale modifiers, an 8 x s32 base filter and
an s32 start scale.
"""
import struct

FRAME_LEN = [20, 14, 12, 10]            # bytes per audio frame by pulse mode (table at 0x1FFF77E)
PULSE_WORDS = {1: (5, 3), 2: (4, 4), 3: (3, 5)}   # mode -> (words, spacing in samples)  (table at 0x1FFF776)


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


class VXAudioDecoder:
    def __init__(self, extradata):
        if len(extradata) < 3124:
            raise ValueError('extradata too short')
        e = extradata
        self.cb = []
        for t in range(3):
            tab = []
            for i in range(64):
                o = t * 1024 + i * 16
                tab.append(struct.unpack('<8h', e[o:o + 16]))
            self.cb.append(tab)
        self.scalemod = struct.unpack('<8h', e[3072:3088])
        self.base_filter = list(struct.unpack('<8i', e[3088:3120]))
        self.start_scale = struct.unpack('<i', e[3120:3124])[0]
        self.reset()

    def reset(self):
        self.scale = self.start_scale
        self.cur = 1                                   # [state+0x6c]
        self.hist = [[0] * 256, [0] * 256]             # two 256-int excitation buffers (slot, 128 old | 128 new)
        self.synth = [0] * 8                           # [state+0x78]
        self.refl = [0] * 8                            # [state+8]  (code-book domain filter)
        self.old_filter = [0] * 8                      # last final (direct form) filter

    # ---- pieces
    def _pulses(self, exc, w0, w1, data, pos):
        """add pulses to exc (list of 128 ints); returns new pos. scale already updated."""
        scale = self.scale
        start = (w1 >> 14) & 3
        mode = (w1 >> 12) & 3
        if mode == 0:
            p = start
            lsb = 0
            for _ in range(8):
                w = struct.unpack_from('<H', data, pos)[0]
                pos += 2
                for sh in (13, 10, 7, 4, 1):
                    v = (w >> sh) & 7
                    exc[p] += (2 * v - 7) * scale
                    p += 3
                lsb = ((w & 1) | (lsb << 1)) & 0xFFFFFFFF
            for sh in (5, 2):
                v = (lsb >> sh) & 7
                exc[p] += (2 * v - 7) * scale
                p += 3
        else:
            words, step = PULSE_WORDS[mode]
            p = start
            for _ in range(words):
                w = struct.unpack_from('<H', data, pos)[0]
                pos += 2
                for sh in range(14, -1, -2):
                    v = (w >> sh) & 3
                    exc[p] += (2 * v - 3) * scale
                    p += step
        return pos

    def _stepup(self, k):
        k = list(k)
        for m in range(1, 8):
            km = k[m]
            old = k[:m]
            for j in range(m):
                k[j] = s32(old[j] + ((s32(old[m - 1 - j] * km)) >> 15))
        return [-(x >> 1) for x in k]

    def _synth(self, exc, a, n, out):
        st = self.synth        # st[j] = y[n-8+j] (circular, always a multiple of 8 per call)
        for blk in range(n // 8):
            for j in range(8):
                x = exc[blk * 8 + j]
                acc = x << 14
                for i in range(8):
                    acc += a[i] * st[(j - 1 - i) & 7]
                y = s32(acc) >> 14
                st[j] = y
                out.append(y)

    # ---- one 128-sample audio frame
    def decode(self, data, pos):
        """-> (list of 128 int16, new pos)"""
        w0 = struct.unpack_from('<H', data, pos)[0]
        w1 = struct.unpack_from('<H', data, pos + 2)[0]
        off = w0 >> 9
        mode = (w1 >> 12) & 3
        out = []
        intra = off == 0x7F
        if intra:
            self.scale = self.start_scale
            self.synth = [0] * 8
            self.cur = 1
            exc = [0] * 128
            self.hist[self.cur][128:] = exc
        else:
            prev = self.cur ^ 1
            self.hist[self.cur][:128] = self.hist[prev][128:]
            if off == 0x7E:
                exc = [0] * 128
            else:
                e = self.hist[prev][127 - off:127 - off + 128]
                exc = [0] * 128
                exc[0] = e[0] >> 4
                for i in range(1, 7):
                    exc[i] = (e[i] * (i + 1)) >> 4
                for i in range(7, 121):
                    exc[i] = e[i] >> 1
                for i in range(121, 127):
                    exc[i] = (e[i] * (128 - i)) >> 4
                exc[127] = e[127] >> 4
        # parse header (0x1FFF784)
        self.scale = s32(self.scalemod[(w0 >> 6) & 7] * self.scale) >> 13
        i1, i2, i3 = w0 & 0x3F, (w1 >> 6) & 0x3F, w1 & 0x3F
        npos = self._pulses(exc, w0, w1, data, pos + 4)
        self.hist[self.cur][128:] = exc
        # filter (0x1FFF184)
        prev_f = self.base_filter if intra else self.refl
        c1, c2, c3 = self.cb[0][i1], self.cb[1][i2], self.cb[2][i3]
        k = [s32(prev_f[i] + c1[i] + c2[i] + c3[i]) for i in range(8)]
        self.refl = list(k)
        a = self._stepup(k)
        if intra:
            self._synth(exc, a, 128, out)
        else:
            old = self.old_filter
            avg = [(a[i] + old[i]) >> 1 for i in range(8)]
            q1 = [(avg[i] + old[i]) >> 1 for i in range(8)]
            q3 = [(avg[i] + a[i]) >> 1 for i in range(8)]
            self._synth(exc[0:32], q1, 32, out)
            self._synth(exc[32:64], avg, 32, out)
            self._synth(exc[64:96], q3, 32, out)
            self._synth(exc[96:128], a, 32, out)
        self.old_filter = a
        self.cur ^= 1
        pcm = [max(-32768, min(32767, y)) for y in out]
        return pcm, pos + FRAME_LEN[mode]
