#!/usr/bin/env python3
"""Dump the sequence archive names (SSAR), the wave archive (SWAR) entries and the instrument banks (SBNK) of the SDAT, and write the waves as .wav files.
usage: sdat_dump.py <sound_data.sdat> <out_dir>"""
import sys, os, struct, wave
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdat

IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]
IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]


def decode_adpcm(data):
    """SWAV ADPCM: 4-byte header (initial sample s16, step index u16), then 4-bit IMA nibbles, low nibble first."""
    pred, idx = struct.unpack_from("<hH", data, 0)
    out = [pred]
    for byte in data[4:]:
        for nib in (byte & 15, byte >> 4):
            step = IMA_STEP[idx]
            diff = step >> 3
            if nib & 1: diff += step >> 2
            if nib & 2: diff += step >> 1
            if nib & 4: diff += step
            pred += -diff if nib & 8 else diff
            pred = max(-32768, min(32767, pred))
            idx = max(0, min(88, idx + IMA_INDEX[nib & 7]))
            out.append(pred)
    return out


def parse_swar(b):
    assert b[:4] == b"SWAR"
    n = struct.unpack_from("<I", b, 0x38)[0]
    offs = struct.unpack_from("<%dI" % n, b, 0x3C)
    waves = []
    for i, o in enumerate(offs):
        wtype, loop, rate, tm, loop_start, loop_len = struct.unpack_from("<BBHHHI", b, o)
        end = offs[i + 1] if i + 1 < n else len(b)
        data = b[o + 12:end]
        if wtype == 0:
            pcm = [(x - 128 if x >= 128 else x) * 256 for x in data]
        elif wtype == 1:
            pcm = list(struct.unpack_from("<%dh" % (len(data) // 2), data, 0))
        else:
            pcm = decode_adpcm(data)
        waves.append(dict(type=wtype, loop=loop, rate=rate, loop_start=loop_start, loop_len=loop_len, pcm=pcm))
    return waves


def parse_ssar(b):
    assert b[:4] == b"SSAR"
    n = struct.unpack_from("<I", b, 0x1C)[0]
    out = []
    for i in range(n):
        off, bank, vol, cpr, ppr, ply = struct.unpack_from("<IHBBBB", b, 0x20 + 12 * i)
        out.append(dict(off=off, bank=bank, vol=vol))
    return out


def parse_sbnk(b):
    assert b[:4] == b"SBNK"
    n = struct.unpack_from("<I", b, 0x38)[0]
    inst = []
    for i in range(n):
        t, off = struct.unpack_from("<BH", b, 0x3C + 8 * i)       # record: u8 type, u16 offset, u8 pad, ...
        inst.append((t, off))
    return inst


if __name__ == "__main__":
    s = sdat.Sdat(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    b = s.b
    # SEQARC names: SYMB table kind 1 entries point to {name offset, sub-table offset}
    so = struct.unpack_from("<8I", b, s.sym + 8)
    cnt = struct.unpack_from("<I", b, s.sym + so[1])[0]
    print("SEQARC:", cnt)
    for i in range(cnt):
        no, sub = struct.unpack_from("<II", b, s.sym + so[1] + 4 + 8 * i)       # entries are inline: {name offset, sub-table offset}
        nm = b[s.sym + no:b.index(b"\0", s.sym + no)].decode() if no else ""
        k = struct.unpack_from("<I", b, s.sym + sub)[0]
        names = []
        for j in range(k):
            p = struct.unpack_from("<I", b, s.sym + sub + 4 + 4 * j)[0]
            names.append(b[s.sym + p:b.index(b"\0", s.sym + p)].decode() if p else "")
        print(" archive %d %r: %d sequences" % (i, nm, k))
        ssar = parse_ssar(s.file(s.file_id("SEQARC", i)))
        for j, q in enumerate(ssar):
            print("   %2d %-28s bank %d vol %3d off %d" % (j, names[j] if j < len(names) else "", q["bank"], q["vol"], q["off"]))
    for k in range(2):
        sw = parse_swar(s.file(s.file_id("WAVEARC", k)))
        print("WAVEARC", s.names["WAVEARC"][k], len(sw), "waves")
        for i, w in enumerate(sw):
            print("   wave %2d type %d rate %5d samples %6d (%.2f s) loop %d start %d len %d" % (i, w["type"], w["rate"], len(w["pcm"]), len(w["pcm"]) / max(1, w["rate"]), w["loop"], w["loop_start"], w["loop_len"]))
            f = wave.open(os.path.join(out, "%s_%02d.wav" % (s.names["WAVEARC"][k], i)), "wb")
            f.setnchannels(1); f.setsampwidth(2); f.setframerate(w["rate"])
            f.writeframes(struct.pack("<%dh" % len(w["pcm"]), *w["pcm"]))
            f.close()
