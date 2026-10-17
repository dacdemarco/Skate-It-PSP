#!/usr/bin/env python3
"""Nintendo DS backward-LZ (BLZ) decompressor used for ARM9 and overlays.

Footer (last 8 bytes): u32 = (hdr_len << 24) | enc_len, u32 inc_len (extra bytes after decompression).
The first (L - enc_len) bytes are stored raw; the rest decodes backwards from the end.
"""
import struct, sys, os, glob


def decompress(data):
    L = len(data)
    inc = struct.unpack_from("<I", data, L - 4)[0]
    if inc == 0:
        return bytes(data)
    word = struct.unpack_from("<I", data, L - 8)[0]
    hdr, enc = word >> 24, word & 0xFFFFFF
    dec = L - enc
    raw = bytearray(L + inc)
    raw[:dec] = data[:dec]
    pak, out, mask, flags = L - hdr, L + inc, 0, 0
    while out > dec:
        mask >>= 1
        if mask == 0:
            pak -= 1; flags = data[pak]; mask = 0x80
        if not (flags & mask):
            pak -= 1; out -= 1; raw[out] = data[pak]
        else:
            pak -= 1; hi = data[pak]; pak -= 1; lo = data[pak]
            pos = (hi << 8) | lo
            n = (pos >> 12) + 3
            pos = (pos & 0xFFF) + 3
            for _ in range(n):
                out -= 1
                raw[out] = raw[out + pos]
    return bytes(raw)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for f in [os.path.join(src, "arm9.bin")] + sorted(glob.glob(os.path.join(src, "overlays", "*.bin"))):
        out = decompress(open(f, "rb").read())
        o = os.path.join(dst, os.path.basename(f))
        open(o, "wb").write(out)
        print("%-34s %7d -> %7d" % (os.path.basename(f), os.path.getsize(f), len(out)))
