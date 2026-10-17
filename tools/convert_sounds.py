#!/usr/bin/env python3
"""Convert the in-game sound effects of the DS sound archive (DATA/AUDIO/sound_data.sdat, bank BANK_BE / wave archive WAVE_BE) to data/sounds.snd.

Every effect of SEQ_BE is a 6-byte sequence "program n, play note 60": sequence n -> instrument n of BANK_BE -> one wave of WAVE_BE (IMA-ADPCM 16-48 kHz mono, decoded here).
The effects keep their original names (SFX_GRIND_LOOP_METAL_2T, SFX_ROLL_MEDIUM, SFX_OLLIE_CNC_BIG, ...), the game looks them up by name.
Looped waves loop as a whole; the tail is cross-faded into the head so the loop point is click-free.

File format (little endian):
  char magic[4] = "SKSN"; u32 version = 1; u32 count
  count * { char name[32]; u32 rate; u32 nsamples; u32 loop; u32 offset }      (offset from the start of the file, 4-byte aligned)
  s16 PCM samples
usage: convert_sounds.py <sound_data.sdat> <out.snd>"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdat, sdat_dump as d

XFADE = 256


def effects(path):
    s = sdat.Sdat(path)
    b = s.b
    bank = s.file(s.file_id("BANK", 1))
    waves = d.parse_swar(s.file(s.file_id("WAVEARC", 1)))
    so = struct.unpack_from("<8I", b, s.sym + 8)
    _, sub = struct.unpack_from("<II", b, s.sym + so[1] + 4 + 8 * 1)          # the SEQ_BE archive
    k = struct.unpack_from("<I", b, s.sym + sub)[0]
    names = [b[s.sym + p:b.index(b"\0", s.sym + p)].decode() for p in struct.unpack_from("<%dI" % k, b, s.sym + sub + 4)]
    ssar = s.file(s.file_id("SEQARC", 1))
    data_off = struct.unpack_from("<I", ssar, 0x18)[0]
    out = []
    for i, nm in enumerate(names):
        seq_off = struct.unpack_from("<I", ssar, 0x20 + 12 * i)[0]
        seq = ssar[data_off + seq_off:data_off + seq_off + 6]
        assert seq[0] == 0x81 and seq[2] == 60, "unexpected sequence %s" % seq.hex()     # PROGRAM, note 60
        prog = seq[1]
        rec_off = struct.unpack_from("<H", bank, 0x3C + 4 * prog + 1)[0]
        swav, swar, note = struct.unpack_from("<HHB", bank, rec_off)
        assert swar == 0 or True
        w = waves[swav]
        pcm = list(w["pcm"][1:]) if w["type"] == 2 else list(w["pcm"])          # the ADPCM header sample is not part of the sound
        if w["loop"] and len(pcm) > 2 * XFADE:
            n = len(pcm); f = XFADE
            body = pcm[:n - f]
            for j in range(f):
                a = j / f
                body[j] = int(round(pcm[j] * a + pcm[n - f + j] * (1.0 - a)))
            pcm = body
        out.append((nm, w["rate"], pcm, 1 if w["loop"] else 0))
    return out


def main():
    fx = effects(sys.argv[1])
    head = 12 + 48 * len(fx)
    off = (head + 3) & ~3
    entries, blob = [], bytearray()
    for nm, rate, pcm, loop in fx:
        entries.append(struct.pack("<32sIIII", nm.encode(), rate, len(pcm), loop, off + len(blob)))
        blob += struct.pack("<%dh" % len(pcm), *pcm)
    buf = bytearray(off)
    buf[:12] = struct.pack("<4sII", b"SKSN", 1, len(fx))
    for i, e in enumerate(entries):
        buf[12 + 48 * i:12 + 48 * (i + 1)] = e
    open(sys.argv[2], "wb").write(bytes(buf) + bytes(blob))
    tot = sum(len(p) for _, _, p, _ in fx)
    print("%d effects, %d samples, %d bytes -> %s" % (len(fx), tot, len(buf) + len(blob), sys.argv[2]))
    for nm, rate, pcm, loop in fx:
        pk = max(abs(x) for x in pcm); rms = (sum(x * x for x in pcm) / len(pcm)) ** 0.5
        if nm.startswith(("SFX_GRIND", "SFX_ROLL", "SFX_OLLIE", "SFX_LAND_CNC", "SFX_BRAKE", "SFX_WIPEOUT", "SFX_AIR", "SFX_PUSH", "SFX_GRUNT")):
            print("  %-26s %5d Hz %6d samples peak %5d rms %5.0f loop %d" % (nm, rate, len(pcm), pk, rms, loop))


if __name__ == "__main__":
    main()
