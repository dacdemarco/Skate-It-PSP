"""Actimagine "VX" (VXDS) video decoder, pure Python.

Written from the public descriptions of the format (multimedia.cx wiki, the FFmpeg-devel
'actimagine' patch by F. Nouwt and NihAV's vx.rs by K. Shishkov) and verified against the two
Skate It .vx files (frame sizes chain exactly; the video bitstream of every frame ends right
where the audio payload begins).

Bitstream: 16-bit little endian words, read MSB first.  Elias-gamma' (= H.264 exp-golomb) codes for
modes, CAVLC (H.264 tables) for 4x4 residual blocks.  YUV 4:2:0, 16x16 macroblocks split recursively
down to 2x2, full-pel motion compensation from 3 previous frames, H.264 4x4 integer transform.
"""
import struct

# ---------------------------------------------------------------- tables
QUANT4 = [
    [10, 13, 10, 13, 13, 16, 13, 16], [11, 14, 11, 14, 14, 18, 14, 18],
    [13, 16, 13, 16, 16, 20, 16, 20], [14, 18, 14, 18, 18, 23, 18, 23],
    [16, 20, 16, 20, 20, 25, 20, 25], [18, 23, 18, 23, 23, 29, 23, 29]]
ZIGZAG = [0, 4, 1, 2, 5, 8, 12, 9, 6, 3, 7, 10, 13, 14, 11, 15]   # level idx -> dct pos (4*fx+fy)
OLD_MODE_REMAP = [1, 2, 0, 4, 7, 3, 11, 15, 9, 5, 14, 6, 12, 13, 8, 16, 23, 10, 22, 19, 20, 17, 21, 18]
CBP_OLD = [0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x03, 0x05, 0x0A, 0x0C, 0x0F, 0x1F, 0x07, 0x0B, 0x0D, 0x0E,
           0x06, 0x09, 0x13, 0x15, 0x1A, 0x1C, 0x11, 0x12, 0x14, 0x18, 0x17, 0x1B, 0x1D, 0x1E, 0x16, 0x19]
CBP_NEW = [0x00, 0x08, 0x04, 0x02, 0x01, 0x1F, 0x0F, 0x0A, 0x05, 0x0C, 0x03, 0x10, 0x0E, 0x0D, 0x0B, 0x07,
           0x09, 0x06, 0x1E, 0x1B, 0x1A, 0x1D, 0x17, 0x15, 0x18, 0x12, 0x11, 0x1C, 0x14, 0x13, 0x16, 0x19]
LEVEL_LIMIT = [2, 5, 11, 23, 47, 32768]
COEFF_TOKEN_TABLE = [0, 0, 1, 1, 2, 2, 2, 2] + [3] * 9

CT_LEN = [
 [1,0,0,0, 6,2,0,0, 8,6,3,0, 9,8,7,5, 10,9,8,6, 11,10,9,7, 13,11,10,8, 13,13,11,9, 13,13,13,10,
  14,14,13,11, 14,14,14,13, 15,15,14,14, 15,15,15,14, 16,15,15,15, 16,16,16,15, 16,16,16,16, 16,16,16,16],
 [2,0,0,0, 6,2,0,0, 6,5,3,0, 7,6,6,4, 8,6,6,4, 8,7,7,5, 9,8,8,6, 11,9,9,6, 11,11,11,7,
  12,11,11,9, 12,12,12,11, 12,12,12,11, 13,13,13,12, 13,13,13,13, 13,14,13,13, 14,14,14,13, 14,14,14,14],
 [4,0,0,0, 6,4,0,0, 6,5,4,0, 6,5,5,4, 7,5,5,4, 7,5,5,4, 7,6,6,4, 7,6,6,4, 8,7,7,5,
  8,8,7,6, 9,8,8,7, 9,9,8,8, 9,9,9,8, 10,9,9,9, 10,10,10,10, 10,10,10,10, 10,10,10,10],
 [6,0,0,0, 6,6,0,0, 6,6,6,0] + [6] * 56]
CT_BITS = [
 [1,0,0,0, 5,1,0,0, 7,4,1,0, 7,6,5,3, 7,6,5,3, 7,6,5,4, 15,6,5,4, 11,14,5,4, 8,10,13,4,
  15,14,9,4, 11,10,13,12, 15,14,9,12, 11,10,13,8, 15,1,9,12, 11,14,13,8, 7,10,9,12, 4,6,5,8],
 [3,0,0,0, 11,2,0,0, 7,7,3,0, 7,10,9,5, 7,6,5,4, 4,6,5,6, 7,6,5,8, 15,6,5,4, 11,14,13,4,
  15,10,9,4, 11,14,13,12, 8,10,9,8, 15,14,13,12, 11,10,9,12, 7,11,6,8, 9,8,10,1, 7,6,5,4],
 [15,0,0,0, 15,14,0,0, 11,15,13,0, 8,12,14,12, 15,10,11,11, 11,8,9,10, 9,14,13,9, 8,10,9,8, 15,14,13,13,
  11,14,10,12, 15,10,13,12, 11,14,9,12, 8,10,13,8, 13,7,9,12, 9,12,11,10, 5,8,7,6, 1,4,3,2],
 [3,0,0,0, 0,1,0,0, 4,5,6,0, 8,9,10,11, 12,13,14,15, 16,17,18,19, 20,21,22,23, 24,25,26,27, 28,29,30,31,
  32,33,34,35, 36,37,38,39, 40,41,42,43, 44,45,46,47, 48,49,50,51, 52,53,54,55, 56,57,58,59, 60,61,62,63]]
TZ_LEN = [
 [1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9], [3,3,3,3,3,4,4,4,4,5,5,6,6,6,6], [4,3,3,3,4,4,3,3,4,5,5,6,5,6],
 [5,3,4,4,3,3,3,4,3,4,5,5,5], [4,4,4,3,3,3,3,3,4,5,4,5], [6,5,3,3,3,3,3,3,4,3,6], [6,5,3,3,3,2,3,4,3,6],
 [6,4,5,3,2,2,3,3,6], [6,6,4,2,2,3,2,5], [5,5,3,2,2,2,4], [4,4,3,3,1,3], [4,4,2,1,3], [3,3,1,2], [2,2,1], [1,1]]
TZ_BITS = [
 [1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1], [7,6,5,4,3,5,4,3,2,3,2,3,2,1,0], [5,7,6,5,4,3,4,3,2,3,2,1,1,0],
 [3,7,5,4,6,5,4,3,3,2,2,1,0], [5,4,3,7,6,5,4,3,2,1,1,0], [1,1,7,6,5,4,3,2,1,1,0], [1,1,5,4,3,3,2,1,1,0],
 [1,1,1,3,3,2,2,1,0], [1,0,1,3,2,1,1,1], [1,0,1,3,2,1,1], [0,1,1,2,1,3], [0,1,1,1,1], [0,1,1,1], [0,1,1], [0,1]]
RUN_LEN = [[1,1], [1,2,2], [2,2,2,2], [2,2,2,3,3], [2,2,3,3,3,3], [2,3,3,3,3,3,3],
           [3,3,3,3,3,3,3,4,5,6,7,8,9,10,11]]
RUN_BITS = [[1,0], [1,1,0], [3,2,1,0], [3,2,1,1,0], [3,2,3,2,1,0], [3,0,1,3,2,5,4],
            [7,6,5,4,3,2,1,1,1,1,1,1,1,1,1]]


def _build_vlc(lens, bits):
    """-> (maxlen, table) ; table[peek(maxlen)] = (symbol, length)"""
    L = max(lens)
    t = [None] * (1 << L)
    for sym, (ln, b) in enumerate(zip(lens, bits)):
        if ln == 0:
            continue
        base = b << (L - ln)
        for k in range(1 << (L - ln)):
            t[base + k] = (sym, ln)
    return L, t


VLC_CT = [_build_vlc(CT_LEN[i], CT_BITS[i]) for i in range(4)]
VLC_TZ = [_build_vlc(TZ_LEN[i], TZ_BITS[i]) for i in range(15)]
VLC_RUN = [_build_vlc(RUN_LEN[i], RUN_BITS[i]) for i in range(7)]

_clip_cache = {}


def _clip_table(delta):
    t = _clip_cache.get(delta)
    if t is None:
        t = bytes(max(0, min(255, i + delta)) for i in range(256))
        _clip_cache[delta] = t
    return t


class BitReader:
    def __init__(self, data):
        if len(data) & 1:
            data = data + b'\0'
        sw = bytearray(len(data))
        sw[0::2] = data[1::2]
        sw[1::2] = data[0::2]
        n = len(sw) * 8
        self.bits = bin(int.from_bytes(sw, 'big') | (1 << n))[3:] + '0' * 64
        self.pos = 0
        self.nbits = n

    def bit(self):
        b = self.bits[self.pos] == '1'
        self.pos += 1
        return b

    def read(self, n):
        if n == 0:
            return 0
        v = int(self.bits[self.pos:self.pos + n], 2)
        self.pos += n
        return v

    def unary(self):  # number of zeros before a 1
        p = self.bits.find('1', self.pos)
        if p < 0:
            raise ValueError('bitstream overrun')
        z = p - self.pos
        self.pos = p + 1
        return z

    def ue(self):
        z = self.unary()
        if z == 0:
            return 0
        return (1 << z) - 1 + self.read(z)

    def se(self):
        v = self.ue() + 1
        return (v >> 1) if (v & 1) == 0 else ((1 - v) >> 1)

    def vlc(self, tab):
        L, t = tab
        e = t[int(self.bits[self.pos:self.pos + L], 2)]
        if e is None:
            raise ValueError('bad vlc')
        self.pos += e[1]
        return e[0]


class VXError(Exception):
    pass


class VXVideoDecoder:
    def __init__(self, width, height, quantiser, old_format=False, wrap_plane=True):
        if width & 15 or height & 15:
            raise VXError('size not multiple of 16')
        self.W, self.H = width, height
        self.cw, self.ch = width // 2, height // 2
        self.old = old_format
        self.wrap_plane = wrap_plane
        qx, qy = quantiser % 6, quantiser // 6
        q = [v << qy for v in QUANT4[qx]]
        # dequant factor for dct pos 4*fx+fy
        self.dq = [q[(c & 1) * 4 + j] for j in range(4) for c in range(4)]
        self.cbp = CBP_OLD if old_format else CBP_NEW
        # pixel buffers: Y stride W, U/V stride W/2, with a margin so stray reads never go negative
        self.mY = 4 * width
        self.mC = 4 * self.cw
        self.bufs = []
        for _ in range(4):
            self.bufs.append([bytearray(self.mY * 2 + width * height),
                              bytearray(b'\x80' * (self.mC * 2 + self.cw * self.ch)),
                              bytearray(b'\x80' * (self.mC * 2 + self.cw * self.ch))])
        self.refs = [0, 1, 2, 3]
        self.ipred4 = [9] * 25

    def decode_frame(self, data):
        """data: video bitstream bytes (16-bit LE words). returns end bit position."""
        br = self.br = BitReader(data)
        W, H = self.W, self.H
        self.cur = self.bufs[self.refs[3]]
        self.ref = [self.bufs[self.refs[i]] for i in range(3)]
        self.yn = [0] * ((W // 4 + 1) * (H // 4 + 1))
        self.cn = [0] * ((W // 8 + 1) * (H // 8 + 1))
        self.ynS, self.cnS = W // 4 + 1, W // 8 + 1
        mvw = W // 16 + 2
        self.mvS = mvw
        self.mvs = [(0, 0)] * (mvw * (H // 16 + 2))
        for my in range(H // 16):
            for mx in range(W // 16):
                l = self.mvs[(my + 1) * mvw + mx]
                t = self.mvs[my * mvw + mx + 1]
                tr = self.mvs[my * mvw + mx + 2]
                self.pred_mv = (sorted((l[0], t[0], tr[0]))[1], sorted((l[1], t[1], tr[1]))[1])
                self.cur_mv = (0, 0)
                self.block(mx * 16, my * 16, 16, 16)
                self.mvs[(my + 1) * mvw + mx + 1] = self.cur_mv
        self.refs = [(r + 3) & 3 for r in self.refs]
        return br.pos

    def planes(self):
        """last decoded frame -> (Y, U, V) bytes (cropped)"""
        b = self.bufs[self.refs[0]]
        W, H = self.W, self.H
        y = bytes(b[0][self.mY:self.mY + W * H])
        u = bytes(b[1][self.mC:self.mC + self.cw * self.ch])
        v = bytes(b[2][self.mC:self.mC + self.cw * self.ch])
        return y, u, v

    # ---- block syntax
    def block(self, x, y, w, h):
        br = self.br
        code = br.ue()
        if code > 23:
            raise VXError('bad mode %d' % code)
        mode = OLD_MODE_REMAP[code] if self.old else code
        large = min(w, h) >= 8
        if mode >= 16 and not large:
            raise VXError('residue mode in small block')
        if mode == 0 or mode == 8:
            if w == 2 or (mode == 8 and not large):
                raise VXError('bad vsplit')
            self.block(x, y, w // 2, h)
            self.block(x + w // 2, y, w // 2, h)
            if mode == 8:
                self.residue(x, y, w, h)
        elif mode == 2 or mode == 13:
            if h == 2 or (mode == 13 and not large):
                raise VXError('bad hsplit')
            self.block(x, y, w, h // 2)
            self.block(x, y + h // 2, w, h // 2)
            if mode == 13:
                self.residue(x, y, w, h)
        elif mode in (1, 9, 14, 12, 20, 21):
            ref = {1: 0, 12: 0, 9: 1, 20: 1, 14: 2, 21: 2}[mode]
            if mode in (12, 20, 21) and not large:
                raise VXError('res')
            self.mc(x, y, w, h, ref, False)
            if mode in (12, 20, 21):
                self.residue(x, y, w, h)
        elif mode in (4, 5, 6, 16, 17, 18):
            ref = (mode - 4) if mode < 16 else (mode - 16)
            self.mc(x, y, w, h, ref, True)
            if mode >= 16:
                self.residue(x, y, w, h)
        elif mode == 3 or mode == 10:
            if mode == 10 and not large:
                raise VXError('res')
            self.mc_bias(x, y, w, h)
            if mode == 10:
                self.residue(x, y, w, h)
        elif mode == 7 or mode == 23:
            self.plane_delta(x, y, w, h)
            if mode == 23:
                self.residue(x, y, w, h)
        elif mode == 11 or mode == 22:
            if min(w, h) >= 4:
                self.intra_full(x, y, w, h)
            if mode == 22:
                self.residue(x, y, w, h)
        elif mode == 15 or mode == 19:
            if min(w, h) >= 4:
                self.intra4(x, y, w, h)
            if mode == 19:
                self.residue(x, y, w, h)
        else:
            raise VXError('mode %d' % mode)

    # ---- inter
    def _mc_copy(self, x, y, w, h, mx, my, src, dy_=0, du=0, dv=0):
        W, H = self.W, self.H
        if x + mx < 0 or x + mx + w > W or y + my < 0 or y + my + h > H:
            raise VXError('mv out of range')
        cur = self.cur
        mY, mC = self.mY, self.mC
        tY = _clip_table(dy_) if dy_ else None
        for r in range(h):
            s = mY + (y + my + r) * W + x + mx
            d = mY + (y + r) * W + x
            row = src[0][s:s + w]
            cur[0][d:d + w] = row.translate(tY) if tY else row
        cw = self.cw
        sx, sy = (x + mx) >> 1, (y + my) >> 1
        cx, cy = x >> 1, y >> 1
        for p, dl in ((1, du), (2, dv)):
            t = _clip_table(dl) if dl else None
            for r in range(h >> 1):
                s = mC + (sy + r) * cw + sx
                d = mC + (cy + r) * cw + cx
                row = src[p][s:s + (w >> 1)]
                cur[p][d:d + (w >> 1)] = row.translate(t) if t else row

    def mc(self, x, y, w, h, ref, has_delta):
        br = self.br
        mv = self.pred_mv
        if has_delta:
            dx = br.se()
            dy = br.se()
            mv = (mv[0] + dx, mv[1] + dy)
        self.cur_mv = mv
        self._mc_copy(x, y, w, h, mv[0], mv[1], self.ref[ref])

    def mc_bias(self, x, y, w, h):
        br = self.br
        mx = br.se()
        my = br.se()
        dy_ = br.se() * 2
        du = br.se() * 2
        dv = br.se() * 2
        self._mc_copy(x, y, w, h, mx, my, self.ref[0], dy_, du, dv)

    # ---- intra
    def _pl(self, p):
        if p == 0:
            return self.cur[0], self.mY, self.W
        return self.cur[p], self.mC, self.cw

    def pl_vert(self, p, x, y, w, h):
        b, m, S = self._pl(p)
        for r in range(h):
            o = m + (y + r) * S + x
            b[o:o + w] = b[m + (y - 1) * S + x:m + (y - 1) * S + x + w]

    def pl_hor(self, p, x, y, w, h):
        b, m, S = self._pl(p)
        for r in range(h):
            o = m + (y + r) * S + x
            b[o:o + w] = bytes([b[o - 1]]) * w

    def pl_dc(self, p, x, y, w, h):
        b, m, S = self._pl(p)
        if x == 0 and y == 0:
            dc = 128
        else:
            top = sum(b[m + (y - 1) * S + x: m + (y - 1) * S + x + w]) if y else None
            left = sum(b[m + (y + r) * S + x - 1] for r in range(h)) if x else None
            if y == 0:
                dc = (left + h // 2) // h
            elif x == 0:
                dc = (top + w // 2) // w
            else:
                dc = (((top + w // 2) // w) + ((left + h // 2) // h) + 1) >> 1
        for r in range(h):
            o = m + (y + r) * S + x
            b[o:o + w] = bytes([dc]) * w

    def pl_plane(self, p, x, y, w, h, delta):
        b, m, S = self._pl(p)
        base = m + y * S + x
        tr = b[base + (w - 1) - S]
        dl = b[base + (h - 1) * S - 1]
        v = ((tr + dl + 1) >> 1) + delta
        if self.wrap_plane:
            v &= 255
        else:
            v = max(0, min(255, v))
        b[base + (h - 1) * S + w - 1] = v
        self._plane_rec(b, base, S, 0, 0, w, h)

    def _plane_rec(self, b, base, S, ox, oy, w, h):
        if w == 1 and h == 1:
            return
        o0 = base + oy * S + ox
        if h == 1:
            b[o0 + w // 2 - 1] = (b[o0 - 1] + b[o0 + w - 1]) >> 1
            if w > 2:
                self._plane_rec(b, base, S, ox, oy, w // 2, 1)
                self._plane_rec(b, base, S, ox + w // 2, oy, w // 2, 1)
            return
        if w == 1:
            b[o0 + (h // 2 - 1) * S] = (b[o0 - S] + b[o0 + (h - 1) * S]) >> 1
            if h > 2:
                self._plane_rec(b, base, S, ox, oy, 1, h // 2)
                self._plane_rec(b, base, S, ox, oy + h // 2, 1, h // 2)
            return
        even = ((((w & -w).bit_length() - 1) + ((h & -h).bit_length() - 1)) & 1) == 0
        dr = b[o0 + (h - 1) * S + w - 1]
        tr = b[o0 - S + w - 1]
        dl = b[o0 + (h - 1) * S - 1]
        dm = (dl + dr) >> 1
        b[o0 + (h - 1) * S + w // 2 - 1] = dm
        cr = (tr + dr) >> 1
        b[o0 + (h // 2 - 1) * S + w - 1] = cr
        if even:
            v = (dm + b[o0 - S + w // 2 - 1]) >> 1
        else:
            v = (cr + b[o0 + (h // 2 - 1) * S - 1]) >> 1
        b[o0 + (h // 2 - 1) * S + w // 2 - 1] = v
        hw, hh = w // 2, h // 2
        self._plane_rec(b, base, S, ox, oy, hw, hh)
        self._plane_rec(b, base, S, ox + hw, oy, hw, hh)
        self._plane_rec(b, base, S, ox, oy + hh, hw, hh)
        self._plane_rec(b, base, S, ox + hw, oy + hh, hw, hh)

    def plane_delta(self, x, y, w, h):
        br = self.br
        dy_ = br.se() * 2
        du = br.se() * 2
        dv = br.se() * 2
        self.pl_plane(0, x, y, w, h, dy_)
        self.pl_plane(1, x >> 1, y >> 1, w >> 1, h >> 1, du)
        self.pl_plane(2, x >> 1, y >> 1, w >> 1, h >> 1, dv)

    def _chroma_intra(self, x, y, w, h):
        cmode = self.br.ue()
        cx, cy, cw_, ch_ = x >> 1, y >> 1, w >> 1, h >> 1
        for p in (1, 2):
            if cmode == 0:
                self.pl_dc(p, cx, cy, cw_, ch_)
            elif cmode == 1:
                self.pl_hor(p, cx, cy, cw_, ch_)
            elif cmode == 2:
                self.pl_vert(p, cx, cy, cw_, ch_)
            elif cmode == 3:
                self.pl_plane(p, cx, cy, cw_, ch_, 0)
            else:
                raise VXError('bad chroma mode')

    def intra_full(self, x, y, w, h):
        ymode = self.br.ue()
        if ymode == 0:
            self.pl_vert(0, x, y, w, h)
        elif ymode == 1:
            self.pl_hor(0, x, y, w, h)
        elif ymode == 2:
            self.pl_dc(0, x, y, w, h)
        elif ymode == 3:
            self.pl_plane(0, x, y, w, h, 0)
        else:
            raise VXError('bad luma mode')
        self._chroma_intra(x, y, w, h)

    def intra4(self, x, y, w, h):
        br = self.br
        c = self.ipred4
        for by in range(h >> 2):
            for bx in range(w >> 2):
                mode = min(c[by * 5 + 1 + bx], c[(by + 1) * 5 + bx])
                if mode == 9:
                    mode = 2
                if not br.bit():
                    v = br.read(3)
                    mode = v + 1 if v >= mode else v
                c[(by + 1) * 5 + 1 + bx] = mode
                self.pred4(mode, x + bx * 4, y + by * 4)
        self._chroma_intra(x, y, w, h)

    def pred4(self, mode, x, y):
        b, m, S = self.cur[0], self.mY, self.W
        base = m + y * S + x
        if mode == 0:
            return self.pl_vert(0, x, y, 4, 4)
        if mode == 1:
            return self.pl_hor(0, x, y, 4, 4)
        T = [b[base - S + i] for i in range(8)]
        L = [b[base + r * S - 1] for r in range(4)]
        M = b[base - S - 1]
        out = [[0] * 4 for _ in range(4)]   # out[row][col]

        def p3(a, b_, c):
            return (a + 2 * b_ + c + 2) >> 2

        def p2(a, b_):
            return (a + b_ + 1) >> 1
        if mode == 2:
            if x == 0 and y == 0:
                dc = 128
            else:
                sm, sh = 0, 1
                if x:
                    sh += 1
                    sm += sum(L) + 2
                if y:
                    sh += 1
                    sm += sum(T[:4]) + 2
                dc = sm >> sh
            out = [[dc] * 4 for _ in range(4)]
        elif mode == 3:      # diagonal down left
            for r in range(4):
                for c_ in range(4):
                    i = r + c_
                    out[r][c_] = p3(T[i], T[i + 1], T[i + 2]) if i < 6 else p3(T[6], T[7], T[7])
        elif mode == 4:      # diagonal down right
            e = [L[3], L[2], L[1], L[0], M, T[0], T[1], T[2], T[3]]
            for r in range(4):
                for c_ in range(4):
                    k = 4 - r + c_
                    out[r][c_] = p3(e[k - 1], e[k], e[k + 1])
        elif mode == 5:      # vertical right
            a, bb, c2, d = T[0], T[1], T[2], T[3]
            i, j, k = L[0], L[1], L[2]
            out[3][0] = p3(i, j, k)
            out[2][0] = p3(M, i, j)
            out[1][0] = out[3][1] = p3(a, M, i)
            out[0][0] = out[2][1] = p2(a, M)
            out[1][1] = out[3][2] = p3(bb, a, M)
            out[0][1] = out[2][2] = p2(bb, a)
            out[1][2] = out[3][3] = p3(c2, bb, a)
            out[0][2] = out[2][3] = p2(c2, bb)
            out[1][3] = p3(d, c2, bb)
            out[0][3] = p2(d, c2)
        elif mode == 6:      # horizontal down
            a, bb, c2 = T[0], T[1], T[2]
            i, j, k, l = L
            out[3][0] = p2(k, l)
            out[3][1] = p3(j, k, l)
            out[2][0] = out[3][2] = p2(j, k)
            out[2][1] = out[3][3] = p3(i, j, k)
            out[1][0] = out[2][2] = p2(i, j)
            out[1][1] = out[2][3] = p3(M, i, j)
            out[0][0] = out[1][2] = p2(i, M)
            out[0][1] = out[1][3] = p3(i, M, a)
            out[0][2] = p3(M, a, bb)
            out[0][3] = p3(a, bb, c2)
        elif mode == 7:      # vertical left
            a, bb, c2, d, e, f, g = T[:7]
            out[3][3] = p3(e, f, g)
            out[2][3] = p2(e, f)
            out[1][3] = out[3][2] = p3(d, e, f)
            out[0][3] = out[2][2] = p2(d, e)
            out[1][2] = out[3][1] = p3(c2, d, e)
            out[0][2] = out[2][1] = p2(c2, d)
            out[1][1] = out[3][0] = p3(bb, c2, d)
            out[0][1] = out[2][0] = p2(bb, c2)
            out[1][0] = p3(a, bb, c2)
            out[0][0] = p2(a, bb)
        elif mode == 8:      # horizontal up
            i, j, k, l = L
            out[0] = [p2(i, j), p3(i, j, k), p2(j, k), p3(j, k, l)]
            out[1] = [p2(j, k), p3(j, k, l), p2(k, l), p3(k, l, l)]
            out[2] = [p2(k, l), p3(k, l, l), l, l]
            out[3] = [l, l, l, l]
        else:
            raise VXError('bad pred4 mode')
        for r in range(4):
            o = base + r * S
            b[o:o + 4] = bytes(out[r])

    # ---- residue
    def residue(self, x, y, w, h):
        br = self.br
        ynS, cnS = self.ynS, self.cnS
        yn, cn = self.yn, self.cn
        for y2 in range(h >> 3):
            for x2 in range(w >> 3):
                code = br.ue()
                if code > 31:
                    raise VXError('bad cbp')
                mask = self.cbp[code]
                px, py = x + x2 * 8, y + y2 * 8
                bx, by = px >> 2, py >> 2
                for k in range(4):
                    kx, ky = bx + (k & 1), by + (k >> 1)
                    idx = (ky + 1) * ynS + kx + 1
                    if mask & (1 << k):
                        nc = (yn[idx - 1] + yn[idx - ynS] + 1) >> 1
                        yn[idx] = self.coeffs(0, px + (k & 1) * 4, py + (k >> 1) * 4, nc)
                    else:
                        yn[idx] = 0
                cidx = ((py >> 3) + 1) * cnS + (px >> 3) + 1
                if mask & 16:
                    nc = (cn[cidx - 1] + cn[cidx - cnS] + 1) >> 1
                    u = self.coeffs(1, px >> 1, py >> 1, nc)
                    v = self.coeffs(2, px >> 1, py >> 1, nc)
                    cn[cidx] = (u + v + 1) >> 1
                else:
                    cn[cidx] = 0

    def coeffs(self, plane, x, y, nc):
        br = self.br
        tab = COEFF_TOKEN_TABLE[min(nc, 16)]
        tok = br.vlc(VLC_CT[tab])
        total, t1 = tok >> 2, tok & 3
        if total == 0:
            return 0
        level = [0] * 16
        i = 15
        if total != 16:
            zl = br.vlc(VLC_TZ[total - 1])
            i -= max(0, 16 - (total + zl))
        else:
            zl = 0
        suffix = 0
        left = total
        while True:
            if t1 > 0:
                t1 -= 1
                lv = -1 if br.bit() else 1
            else:
                prefix = br.unary()
                if prefix == 15:
                    sfx = br.read(11)
                else:
                    sfx = br.read(suffix) if suffix else 0
                code = sfx + (prefix << suffix)
                if code > LEVEL_LIMIT[suffix]:
                    suffix += 1
                code += 1
                lv = -code if br.bit() else code
            level[i] = lv
            i -= 1
            left -= 1
            if left == 0:
                break
            if zl == 0:
                continue
            rb = br.vlc(VLC_RUN[zl - 1] if zl < 7 else VLC_RUN[6])
            zl -= rb
            i -= rb
        dct = [0] * 16
        dq = self.dq
        for k in range(16):
            if level[k]:
                p = ZIGZAG[k]
                dct[p] = level[k] * dq[p]
        dct[0] += 32
        tmp = [0] * 16
        for r in range(4):
            a, b_, c, d = dct[r * 4:r * 4 + 4]
            a += c
            c = a - 2 * c
            e = (b_ >> 1) - d
            f = b_ + (d >> 1)
            tmp[r] = a + f
            tmp[4 + r] = c + e
            tmp[8 + r] = c - e
            tmp[12 + r] = a - f
        buf, m, S = self._pl(plane)
        for r in range(4):
            a, b_, c, d = tmp[r * 4:r * 4 + 4]
            a += c
            c = a - 2 * c
            e = (b_ >> 1) - d
            f = b_ + (d >> 1)
            o = m + (y + r) * S + x
            for k, v in enumerate(((a + f) >> 6, (c + e) >> 6, (c - e) >> 6, (a - f) >> 6)):
                q = buf[o + k] + v
                buf[o + k] = 0 if q < 0 else (255 if q > 255 else q)
        return total


# ---------------------------------------------------------------- container
class VXFile:
    def __init__(self, path):
        d = self.data = open(path, 'rb').read()
        if d[:4] != b'VXDS':
            raise VXError('not VXDS')
        (self.nframes, self.width, self.height, fps, self.quant, self.rate, self.nstreams,
         self.max_frame, self.extra_off, self.seek_off, self.seek_n) = struct.unpack('<11I', d[4:48])
        self.fps = fps / 65536.0
        self.extradata = d[self.extra_off:self.extra_off + 3124]
        self.seek = [struct.unpack('<2I', d[self.seek_off + 8 * i:self.seek_off + 8 * i + 8])
                     for i in range(self.seek_n)]
        p = 0x30
        self.frames = []     # (payload offset (after size+audio count), payload length, n_audio)
        for i in range(self.nframes):
            size, na = struct.unpack('<HH', d[p:p + 4])
            self.frames.append((p + 4, size - 2, na))
            p += 2 + size
        self.data_end = p

    def frame_payload(self, i):
        off, n, na = self.frames[i]
        return self.data[off:off + n], na
