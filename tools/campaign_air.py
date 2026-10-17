#!/usr/bin/env python3
"""AIR / AIRT recording reader (DATA/AI/<LEVEL>/<chal>_<mode>_<a>_<b>.dat, 457 files).
Container (verified on all 457 files):
  +0   u8 5, 'AIRT'
  +5   02 05 0e 02 (type descriptor?), +9 u32 9
  +13  u32 usize  = size of the inflated payload  = 14*B + 8*C + 3*D + 7  (exact for the free-skate files)
  +17  u8 4 (tag), +18 u32 B, +22 u32 C, +26 u32 D
  +30  s32 x6 (1.19.12): start position xyz, end position xyz
  +54  u32 R (varies; unknown)   +58 u8 4 (tag)  +59 u32 origin = [01][mode][challenge idx][a]  (0 for free-skate recordings)
  +63  u8 0?, then 05 0e 02 + zlib stream (CMF 0x18: 512-byte window; inflates to usize)  -> found at +66
Payload: tagged records. 0x01/0x02 first record = tag + 12 bytes?; '07' = 7-byte sample (3 x s16), followed by a 7-byte snapshot
(3 x u16 + u8 + u8); '02' = 8-byte event [02][u16 frame][trick id][arg][..]; '03' = end. NOT fully decoded.
Usage: campaign_air.py [-t]   prints a table for every file."""
import struct, zlib, os, sys, re, glob

ROOT = "extracted/files/DATA/AI"


def read(path):
    b = open(path, "rb").read()
    assert b[:5] == b"\x05AIRT", path
    usize, = struct.unpack_from("<I", b, 13)
    B, C, D = struct.unpack_from("<III", b, 18)
    pos = [v / 4096.0 for v in struct.unpack_from("<6i", b, 30)]
    R, = struct.unpack_from("<I", b, 54)
    origin, = struct.unpack_from("<I", b, 59)
    d = zlib.decompressobj(15).decompress(b[66:])
    assert len(d) == usize
    return dict(B=B, C=C, D=D, start=pos[:3], end=pos[3:], R=R, origin=origin, payload=d, size=len(b))


if __name__ == "__main__":
    for f in sorted(glob.glob(ROOT + "/*/*.dat")):
        r = read(f)
        o = r["origin"]
        print("%-34s B=%4d C=%3d D=%2d usize=%5d start=(%7.1f %6.1f %7.1f) end=(%7.1f %6.1f %7.1f) origin=%08x" % (
            os.path.relpath(f, ROOT), r["B"], r["C"], r["D"], len(r["payload"]), *r["start"], *r["end"], o))


# ---- the recorded run (decoded 2026-10-15) -------------------------------------------------------------------------------------------------------------------
# The payload is a plain sequence of records from byte 0 to the last byte, which is a single 0x03:
#   07 + 3 x s16 (dx, dy, dz in 1.19.12, /4096 m) + 7 bytes of pose data   = a SAMPLE, 14 bytes: the position change since the last sample; 4 samples per second
#   02 + u16 frame + u8 kind + 4 bytes                                       = an EVENT, 8 bytes (frames count 60 per second: SanFran's last event is at frame 4600 = 76.7 s = 308 samples / 4)
#   06 + u16                                                                 = 3 bytes (D records)
#   01 + 3 x s32 (x, y, z, 1.19.12 ABSOLUTE) + 7 bytes of pose data         = a position fix, 20 bytes (every file starts with one right after its first event, or with it)
# The deltas added to the position fixes reproduce the stored end point EXACTLY on 437 of the 457 files (the others are off by whole multiples of 16 m: a teleport-like jump).
# Event kinds seen: 27 = a flip trick (arg0 = the original's trick id: 0 Ollie, 13 Kickflip, 14 Heelflip, 15 Nollie, 29 Nollie Heelflip ...), 49 = a LINE BANKED (args: byte0, byte1, u16 points; the
# points saturate at 4096), 10 = stance (1 / 2), 31 = landing, 6 / 7 = push / roll, 0x36 = the start / finish marker. In S.K.A.T.E. recordings a = 0..9 are the trick the opponent SETS (with the
# 49 of the landing, i.e. a success), a = 10 b = 0..3 recordings WITHOUT a 49 are failed attempts.
RECORD_SIZES = {7: 14, 2: 8, 6: 3, 1: 20}


def parse_records(path):
    """returns (header dict from read(), payload, [(tag, offset)]) or raises ValueError if the records do not run exactly to the final 0x03"""
    r = read(path)
    p = r["payload"]
    i, out = 0, []
    while i < len(p):
        t = p[i]
        if t == 3 and i == len(p) - 1:
            return r, p, out
        n = RECORD_SIZES.get(t)
        if n is None or i + n > len(p):
            raise ValueError("%s: unknown record %d at %d of %d" % (path, t, i, len(p)))
        out.append((t, i))
        i += n
    raise ValueError("%s: no terminator" % path)


def decode_run(path):
    """returns (positions, ok): the list of (x, y, z) after every sample, starting with the first fix; ok = the sum reached the stored end point"""
    r, p, recs = parse_records(path)
    pos, pts = None, []
    for t, i in recs:
        if t == 1:
            pos = [float(v) for v in struct.unpack_from("<3i", p, i + 1)]
            pts.append(tuple(v / 4096.0 for v in pos))
        elif t == 7 and pos is not None:
            d = struct.unpack_from("<3h", p, i + 1)
            pos = [pos[k] + d[k] for k in range(3)]
            pts.append(tuple(v / 4096.0 for v in pos))
    end = [v * 4096.0 for v in r["end"]]
    return pts, pos is not None and max(abs(pos[k] - end[k]) for k in range(3)) < 200.0


def events(path):
    """[(frame, kind, args bytes)] of a recording"""
    r, p, recs = parse_records(path)
    return [(struct.unpack_from("<H", p, i + 1)[0], p[i + 3], bytes(p[i + 4:i + 8])) for t, i in recs if t == 2]
