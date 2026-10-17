#!/usr/bin/env python3
"""Convert the streamed music tracks of the DS sound archive (DATA/AUDIO/sound_data.sdat, STRM records) to data/music<N>.mus.

A DS STRM stream: file header, HEAD block (wave type, channels, sample rate, number of samples, block layout) and DATA block. The Skate It tracks are mono IMA-ADPCM at 22767 Hz in
512-byte blocks; every block starts with its own 4-byte ADPCM header (predictor s16, step index u16) followed by 508 bytes = 1016 samples (the last block is shorter). The game decodes
the blocks while it plays, so the file keeps the ADPCM: ~1.4-2.6 MB per track.

File format (little endian):
  char magic[4] = "SKMU"; u32 version = 1; u32 rate; u32 nsamples; u32 nblocks; u32 blocksize (512); u32 lastblock_bytes; u32 lastblock_samples; u32 reserved
  nblocks * blocksize bytes of ADPCM blocks (the last one only lastblock_bytes long)
usage: convert_music.py <sound_data.sdat> <out_dir>      writes music0.mus .. for MUSIC_TRACK_*"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdat
from sdat_dump import IMA_INDEX, IMA_STEP


def decode_block(b):
    pred, idx = struct.unpack_from("<hH", b, 0)
    out = []
    for byte in b[4:]:
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


def main():
    s = sdat.Sdat(sys.argv[1])
    out_dir = sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    n_out = 0
    for i, name in enumerate(s.names["STRM"]):
        if not name.startswith("MUSIC_TRACK"):
            continue
        b = s.file(s.file_id("STRM", i))
        assert b[:4] == b"STRM" and b[0x10:0x14] == b"HEAD"
        wtype, loop, ch = struct.unpack_from("<BBH", b, 0x18)
        rate, _ = struct.unpack_from("<HH", b, 0x1C)
        loopoff, nsamp, dataoff, nblocks, blocksz, blocksamp, lastsz, lastsamp = struct.unpack_from("<8I", b, 0x20)
        assert wtype == 2 and ch == 1 and blocksz == 512 and blocksamp == 1016, (wtype, ch, blocksz, blocksamp)
        assert b[dataoff - 8:dataoff - 4] == b"DATA", b[dataoff - 8:dataoff - 4]
        data = b[dataoff:dataoff + (nblocks - 1) * blocksz + lastsz]
        # check: every block's header sample continues the previous block (it is the decoder state after its last sample)
        worst, peak, sq, cnt = 0, 0, 0.0, 0
        prev = None
        for k in range(0, nblocks, max(1, nblocks // 400)):             # a sample of the blocks
            blk = data[k * blocksz:(k + 1) * blocksz] if k < nblocks - 1 else data[k * blocksz:]
            pcm = decode_block(blk)
            hdr = struct.unpack_from("<h", blk, 0)[0]
            if k > 0:
                pb = data[(k - 1) * blocksz:k * blocksz]
                last = decode_block(pb)[-1]
                worst = max(worst, abs(last - hdr))
            peak = max(peak, max(abs(x) for x in pcm)); sq += sum(x * x for x in pcm); cnt += len(pcm)
        print("%s: %d Hz, %d samples (%.1f s), %d blocks; max |last sample - next header| = %d, peak %d, rms %.0f" % (name, rate, nsamp, nsamp / rate, nblocks, worst, peak, (sq / cnt) ** 0.5))
        path = os.path.join(out_dir, "music%d.mus" % n_out)
        with open(path, "wb") as f:
            f.write(struct.pack("<4sIIIIIIII", b"SKMU", 1, rate, nsamp, nblocks, blocksz, lastsz, lastsamp, 0))
            f.write(data)
        n_out += 1
    print("%d tracks written to %s" % (n_out, out_dir))


if __name__ == "__main__":
    main()
