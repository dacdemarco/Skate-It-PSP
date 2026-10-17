# Skate It FMV (.vx / "VXDS") - decode report

Status: **fully decoded.** Container, video (every frame) and audio (every sample) are decoded and were
checked bit-exactly against the game's own ARM9 code, which I ran in a small ARM interpreter
(see "Evidence"). Nothing below is a guess unless it is under "Not verified".

## Files

| ROM file | frames | size | length | audio | file size |
|---|---|---|---|---|---|
| `DATA/FMV/ealogo.vx` (EA logo) | 264 | 256x192 | 8.81 s | 22050 Hz mono | 186,640 B |
| `DATA/FMV/tempintro.vx` (intro movie: skaters, "skate it" logo) | 1187 | 256x192 | 39.61 s | 22050 Hz mono | 2,118,024 B |

Both: 29.9697 fps (header field 0x1DF840 = 16.16 fixed), quantiser 33, 4:2:0, one video + one audio stream.
(`ActimagineOriginal.bmp` is just the licence splash; the codec is Actimagine's "VX".)

Decoded output (already generated): `extracted/fmv/<name>/frame_0000.png ...`, `<name>.wav`, `sheet.png`
(contact sheet: 24 evenly spaced frames). Frame 0 of ealogo is a white fade-in; the EA logo appears from
about frame 80 on (see `extracted/fmv/ealogo/sheet.png`).

## Scripts (all new, nothing existing was modified)

| script | what |
|---|---|
| `tools/vx_video.py` | container parser (`VXFile`) + video decoder (`VXVideoDecoder`), pure Python |
| `tools/vx_audio.py` | audio decoder (`VXAudioDecoder`), hand-ported from the ROM's ARM code |
| `tools/vx_extract.py` | driver: PNG frames + WAV + contact sheet (+ `--jpeg-stats`). `python tools/vx_extract.py` does both files (~1 min) |
| `tools/vx_armcheck.py` | runs the ORIGINAL audio decoder (ov0 `0x01FFF708`) in an ARM interpreter, diffs vs `vx_audio.py` |
| `tools/vx_armcheck_video.py` | runs the ORIGINAL video decoder (ov0 `0x01FF9000`), diffs every plane of every frame vs `vx_video.py` |
| `tools/vx_armcheck_color.py` | runs the ORIGINAL YUV->RGB555 converter (ov0 `0x01FFE9E0`), diffs vs the formula below |

(`vx_armcheck*.py` are slow, 1-3 s per frame; needs `capstone`, already used by `tools/armdis.py`.)

## Container spec (verified on both files)

Little endian. Header, 12 x u32 at offset 0:

```
0x00 'VXDS'
0x04 frame count            0x08 width (256)          0x0C height (192)
0x10 fps, 16.16 fixed       0x14 quantiser (33)       0x18 audio sample rate (22050)
0x1C stream count (1)       0x20 max frame size       0x24 audio "extradata" file offset
0x28 seek-table offset      0x2C seek-table entries (1)
```
* Frames start at file offset 0x30 and are packed back to back, ending exactly at the extradata offset.
* Frame record: `u16 size | payload[size]`; the next record starts at `p + 2 + size` (sizes already include
  padding: each record is padded to a multiple of 4 bytes, so 0 or 2 trailing bytes). Payload =
  `u16 n_audio | video bitstream | n_audio audio frames | 0-2 pad bytes`.
* The video bitstream has no length field: the audio frames start at the next 16-bit boundary after
  the last macroblock, so you have to decode the video to find them (that is how the DS does it too,
  ARM9 `0x0203FA54`). Check: after decoding, `n_audio` audio frames consume the payload exactly in all 1451 frames.
* Audio extradata (3124 B) at `0x24` (0x2CCD4 / 0x20454C): 3 x 64 x 8 x s16 LPC code books (3072 B),
  8 x s16 scale modifiers, 8 x s32 base filter, s32 start scale.
* Seek table: (frame number, file offset) pairs; just one entry (0, 0x30) in both files.
* 5 or 6 audio frames per video frame (128 samples each; 1517 resp. 6823 total, = video duration).

## Video codec

H.264-flavoured, full-pel, intra + 3 reference frames, no B-frames, no deblocking; one codec for all frames.
Only frame 0 is a keyframe (intra modes only: 7/11/15/19/22/23 - checked); the seek table has the single entry (0, 0x30),
so movies must be decoded sequentially from the start (one GOP; no problem for an intro). Implemented in
`vx_video.py`; described by multimedia.cx "Actimagine Video Codec", the FFmpeg-devel patch by F. Nouwt and
NihAV `vx.rs`; I wrote my own code from those descriptions and then fixed/validated it against the ROM code.

* Bits are 16-bit LE words read MSB first. Modes/params use Elias-gamma' (= H.264 ue/se golomb).
* 16x16 macroblocks, recursive v/h splits down to 2x2, mode (ue) per block, 24 modes:
  0/2 split, 8/13 split+residue, 1/9/14 copy ref0/1/2 with predicted MV, 4/5/6 + MV delta, 12/20/21 copy+residue,
  16/17/18 MV delta+residue, 3/10 "ref0 + absolute MV + per-plane DC offset (x2)", 7/23 plane predict (+delta),
  11/22 whole-block intra (V/H/DC/plane, chroma DC/H/V/plane), 15/19 4x4 intra (9 H.264 modes, mode cache).
  Residue modes only for blocks >= 8x8. The MV predictor is the median of left / top / top-right MB.
* Residue: 8x8 groups, CBP via a remap table (ue -> 5-bit mask: 4 luma 4x4 + one U + one V 4x4), H.264 CAVLC
  (coeff_token tables 0-3 chosen from the average neighbour count, total_zeros, run_before), level
  suffix escalation limits 2,5,11,23,47. H.264 4x4 integer IDCT, dequant = `{10,13,16}`-style table << (q/6).
  The tables are in `vx_video.py` and were diffed against the FFmpeg patch's tables by a script.
* Plane prediction is the subtle one: bottom-right sample = (BL+TR+1)>>1 + 2*delta **wrapped mod 256**, then
  recursive midpoint interpolation. The ARM code agrees (see Evidence).
* There is an "old" bitstream variant (different mode/CBP numbering) in other games; Skate It uses the "new" one.
* Colour (DS converter, ARM9 `0x01FFE9E0`, verified): `R=Y+2V`, `G=Y-(U>>1)-V`, `B=Y+2U` with U,V = byte-128,
  clamped, >>3 to RGB555 (bit15 set). The DS also applies a 2x2 ordered dither (Y-4 on odd x+y); my PNGs use the
  undithered 8-bit values. Chroma is stored interleaved U,V on the DS; `vx_video.py` keeps planar planes.

## Audio codec (ARM9 overlay 0 `0x01FFEF40-0x01FFF8A0`, main = `0x01FFF708`)

An LPC / pulse-excitation codec, 128 samples per frame, frame length 20/14/12/10 bytes (pulse mode 0-3).
`vx_audio.py` is a direct port; the original is bit-exact reproduced. Per frame (words are u16 LE, MSB first):

```
w0: [15:9] source offset   0x7F = intra frame, 0x7E = silent excitation, else copy from history
    [8:6] scale-modifier index   [5:0] code book 1 index
w1: [15:14] pulse start   [13:12] pulse mode   [11:6] code book 2 idx   [5:0] code book 3 idx
```
* scale = (scale * scalemod[idx]) >> 13 (start scale from the extradata; reset on the intra frame).
* Excitation (128 ints): intra/0x7E: zeros. Otherwise copy 128 ints starting at `127-offset` in the 256-int excitation
  history (previous 2 frames) with a ramp: first sample >>4, next six *(2..7)>>4, middle 114 >>1, last six *(7..2)>>4, last >>4.
* Pulses are ADDED at `start + k*spacing`: mode 0: 8 words, spacing 3, five 3-bit values per word (MSB first) plus
  two 3-bit values built from the 8 low bits, value = (2v-7)*scale (42 pulses); mode 1/2/3: 5/4/3 words, spacing 3/4/5,
  eight 2-bit values per word, value = (2v-3)*scale.
* Filter: 8 coefficients in the "reflection" domain = previous frame's (base filter for intra) + cb1[i1] + cb2[i2] + cb3[i3]
  (s16 entries); converted to direct form by a Q15 step-up recursion, a_i = -(x>>1) (Q14). Inter frames interpolate old->new
  in quarters (0.75/0.25, 0.5/0.5, 0.25/0.75, 1.0 of the 32-sample quarters), intra frames use one filter.
* Synthesis: y[n] = (x[n]*2^14 + sum_{i<8} a_i*y[n-1-i]) >> 14 (32-bit wrap), PCM = clamp16(y).
  (This is the wiki's description made precise; the wiki had no fixed-point details.)
* Data rate: 23-24 kbit/s audio (both files); pulse-mode histogram (20/14/12/10 B) ealogo 858/373/211/75, intro 4295/1689/667/172.

## Evidence

1. Pictures: `extracted/fmv/*/sheet.png`. ealogo: film-strip fade, EA logo, burn-out - temporally consistent over
   all 264 frames (no drift, which would show immediately for a wrong inter predictor). tempintro: real skate footage,
   pro names, "skate it" logos, 1187 frames, no corruption.
2. Bitstream-consumption invariant (independent of how things look): for all 1451 frames the video decode ends so that
   `n_audio` audio frames fill the rest of the payload exactly (0 or 2 padding bytes, never anything else).
3. **Audio vs ROM code**: `vx_armcheck.py` - ARM interpreter running `0x01FFF708` on the real data:
   ealogo 1517/1517 audio frames identical (7.3 M ARM instructions), tempintro 6823/6823 identical (32.8 M).
4. **Video vs ROM code**: `vx_armcheck_video.py` - ARM interpreter running `0x01FF9000`, every plane compared:
   ealogo: **264/264 frames identical** (Y, U and V planes; 70.4 M ARM instructions, ~267 k per frame);
   tempintro: frames 0-400 (401 frames, 181 M instructions) identical, run stopped there for time (~3 s/frame in Python);
   frames 401-1186 were not run through the ARM code, but they decode with the same code, and every one of them passes
   invariant 2. This also settles the sub-details a third-party decoder could get wrong (plane-prediction wrap, chroma
   MV rounding, intra 4x4 mode cache, NC contexts, stale top-right pixels, 4-buffer reference rotation).
5. **Colour vs ROM code**: `vx_armcheck_color.py` - converter `0x01FFE9E0`: 0 of 49152 pixels differ on frames 0, 40, 150, 200
   (after adding the 2x2 dither, which my first guess lacked).

## Not verified / caveats

* The ARM checks cover the decode core and the colour converter, not the player glue (file seeking, timing, double
  buffering). Frame timing is taken from the header fps (65536/0x1DF840 = 33.37 ms); 5.74 audio frames per video frame.
* Multiple audio streams / stereo (other VX games) are untested - both files have one mono stream.
* Hardware behaviour on a PSP (sceJpeg speed etc. below) is estimated, not measured.

## PSP playback: measurements and recommendation

Output is 256x192 (4:3). On the 480x272 screen that is 363x272 (x1.417 bilinear on the GU) or letterboxed; frames are
tiny. Totals for both movies (1451 frames, 48.4 s):

| storage | ealogo | tempintro | both | notes |
|---|---|---|---|---|
| **(a) JPEG frames q75 (4:2:0)** | 1.61 MB | 10.76 MB | **12.4 MB** | avg 6.1 / 9.1 KB per frame |
| JPEG q85 | 2.08 MB | 13.95 MB | 16.0 MB | |
| JPEG q92 | 2.82 MB | 18.75 MB | 21.6 MB | |
| **(b) raw RGB565/5551 frames** | 25.95 MB | 116.69 MB | **142.6 MB** | 98,304 B/frame, 2.95 MB/s to stream; intro does NOT fit in RAM, must stream from the Memory Stick |
| raw 8-bit YUV420 | 19.5 MB | 87.5 MB | 107 MB | 73,728 B/frame |
| **audio PCM 22050 Hz mono s16** | 388 KB | 1.75 MB | 2.13 MB | `.wav` already produced; PSP SRC audio channel supports 22050 Hz |
| **(c) original .vx + C decoder** | 0.19 MB | 2.12 MB | **2.30 MB** (video + audio) | needs a C port of `vx_video.py`/`vx_audio.py` |

* (a) JPEG: sceJpeg (MJPEG decode of a 256x192 baseline JPEG is a few ms at most) + the 2.1 MB PCM => about 14.5 MB on the
  Memory Stick, trivial code, but lossy-on-lossy (a bit of softness at q75) and 6x the size of the original.
* (b) raw frames: simplest and bit-exact but 143 MB and the intro has to be streamed (about 3 MB/s, fine for a Pro Duo).
* (c) **Recommended: port the decoder and ship the `.vx` files unchanged.** 2.3 MB total, exact original quality, and
  the decoder is small. The ARM9 reference spends on average ~0.27 M instructions per frame (ealogo) and ~0.45 M (intro, frames 0-400)
  at 67 MHz; a straightforward C port on the PSP's 222-333 MHz MIPS should need roughly 1 ms per frame, plus a
  YUV->565 conversion of 49,152 pixels. State is tiny: 4 frame buffers of 256x192x1.5 = 295 KB, plus a few hundred bytes
  of audio state. The Python files are written to be transliterated (flat byte buffers, no tricks). Audio decode (8 MACs per sample) is negligible next to video. If the VX port is not wanted, (a) with JPEG q85 is the fallback.
* Playback loop: `fps = 29.9697`; present a frame every 33.37 ms; feed `sceAudioOutputBlocking` or an SRC channel with
  the decoded 128-sample blocks (5 or 6 per video frame) - audio decode order is bound to the video frame because audio
  frames sit at the end of each frame record.

## Sources / credits

Format descriptions used as the starting point: wiki.multimedia.cx "Actimagine Video Codec" (header, mode list, CBP
table, wording of the audio structure), the FFmpeg-devel actimagine patch (F. Nouwt, 2021; VLC tables, intra predictors),
NihAV `nihav-game/src/codecs/vx.rs` (K. Shishkov; layout/wrap-around cross-check). The audio decoder and all
"verified" claims come from the game's own ARM9 code (ROM `arm9`/overlay 0 as decompressed in `extracted/decomp`).

## C port (src/vx.h, src/vx.c) - done

Pure C99, integer only, no globals, ~310 KB `Vx` struct (4 reference frames + audio state), caller allocates.
API: `vx_open / vx_frames / vx_width / vx_height / vx_fps / vx_audio_rate / vx_next / vx_rewind` (see `src/vx.h`).
`vx_next` gives RGBA8888 (undithered conversion, stride in pixels) + that frame's mono s16 PCM (5-6 x 128 samples).

* Test: `tests/test_vx.c <ealogo.vx> <tempintro.vx> tests` (also line "== VX movies" in `tests/run_tests.sh`).
  Reference CRCs `tests/vx_ref_*.txt` come from the PYTHON decoder via `tools/vx_mkref.py` (per frame: CRC32 of the RGBA
  bytes and of the PCM + sample count). Result under ASan+UBSan: **all 264 + 1187 frames match** (picture and audio),
  also with a picture stride of 272, after `vx_rewind`, and with NULL picture/pcm outputs (state still advances).
* Fuzz: random bit/byte flips (header, first frames, anywhere), truncations and garbage bursts: vx_open/vx_next return 0,
  no crash/overread (exact-size heap copies under ASan), no hang; 3000-trial runs with several seeds are clean.
  Corrupt data is handled by bounds checks everywhere: bit reader returns zeros past the end and flags the error, all
  pixel accesses are inside guard rows, MVs are range checked, coefficient/level/run values are validated, the frame chain
  is walked and checked against the file size in `vx_open`, `pcm_max` too small fails.
* Limits checked by `vx_open`: 16..256 x 16..192 (multiple of 16), quantiser <= 51, one stream.
* Speed (host x86, gcc -O2, whole tempintro): 0.41 ms/frame including RGBA conversion (0.20 ms without), ealogo 0.32 ms/frame.
  The RGBA conversion is half the cost; if the PSP port needs it cheaper, upload the converted 565/8888 in the GU or convert
  rows lazily. Estimate for a 333 MHz PSP: a few ms per frame (not measured on hardware).
* Differences from the Python code, all harmless for valid streams: intra-4x4 mode cache reset per frame (the ARM code does
  this too), coefficient VLCs decoded by linear search (no tables, no globals), unsigned wrap-around arithmetic in the
  audio filter (matches the ARM 32-bit adds).
