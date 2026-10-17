#!/usr/bin/env python3
"""Decode the Skate It FMV files (.vx, 'VXDS') to PNG frames, a WAV and a contact sheet.

    python tools/vx_extract.py [name ...] [--out DIR] [--no-png] [--no-wav] [--jpeg-stats]
        name: ealogo | tempintro | path to a .vx   (default: both ROM files)
        --out: default <repo>/extracted/fmv

Outputs per file:   <out>/<name>/frame_0000.png ...   <out>/<name>/<name>.wav   <out>/<name>/sheet.png
Colour: the DS converts YUV with  R=Y+2V  G=Y-(U>>1)-V  B=Y+2U  (U,V = byte-128; ARM9 0x01FFE9E0).
"""
import os, struct, sys, time, wave
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vx_video import VXFile, VXVideoDecoder
from vx_audio import VXAudioDecoder, FRAME_LEN

ROM_DIR = os.path.join(HERE, '..', 'extracted', 'files', 'DATA', 'FMV')


def yuv_to_rgb(y, u, v, W, H):
    Y = np.frombuffer(y, np.uint8).reshape(H, W).astype(np.int32)
    U = np.frombuffer(u, np.uint8).reshape(H // 2, W // 2).astype(np.int32) - 128
    V = np.frombuffer(v, np.uint8).reshape(H // 2, W // 2).astype(np.int32) - 128
    U = U.repeat(2, 0).repeat(2, 1)
    V = V.repeat(2, 0).repeat(2, 1)
    rgb = np.stack([Y + 2 * V, Y - (U >> 1) - V, Y + 2 * U], -1)
    return np.clip(rgb, 0, 255).astype(np.uint8)


def decode_file(path, outdir, png=True, wav=True, jpeg_stats=False):
    name = os.path.splitext(os.path.basename(path))[0]
    vx = VXFile(path)
    os.makedirs(outdir, exist_ok=True)
    print('%s: %d frames %dx%d %.4f fps quant %d, audio %d Hz x%d' % (
        name, vx.nframes, vx.width, vx.height, vx.fps, vx.quant, vx.rate, vx.nstreams))
    vdec = VXVideoDecoder(vx.width, vx.height, vx.quant)
    adec = VXAudioDecoder(vx.extradata)
    pcm = []
    pad_hist = {}
    sheet_idx = set(np.linspace(0, vx.nframes - 1, 24).astype(int).tolist())
    sheet = []
    raw_bytes = 0
    jpg = {75: 0, 85: 0, 92: 0}
    t0 = time.time()
    for i in range(vx.nframes):
        data, na = vx.frame_payload(i)
        end = vdec.decode_frame(data)
        pos = (end + 15) // 16 * 2                       # audio starts at the next 16-bit boundary
        for _ in range(na):
            p, pos = adec.decode(data, pos)
            pcm += p
        pad = len(data) - pos                            # 0 or 2: frame is padded to 4 bytes
        pad_hist[pad] = pad_hist.get(pad, 0) + 1
        y, u, v = vdec.planes()
        rgb = yuv_to_rgb(y, u, v, vx.width, vx.height)
        img = Image.fromarray(rgb)
        if png:
            img.save(os.path.join(outdir, 'frame_%04d.png' % i), optimize=False, compress_level=3)
        if i in sheet_idx:
            sheet.append(rgb)
        if jpeg_stats:
            import io
            for q in jpg:
                b = io.BytesIO()
                img.save(b, 'JPEG', quality=q, subsampling=2)      # 4:2:0
                jpg[q] += len(b.getvalue())
        if i % 200 == 0:
            print('  frame %d/%d  (%.0fs)' % (i, vx.nframes, time.time() - t0))
    print('  audio/video tail check: trailing bytes per frame histogram', pad_hist, '(only 0 and 2 = 4-byte padding is correct)')
    if wav:
        w = wave.open(os.path.join(outdir, name + '.wav'), 'wb')
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(vx.rate)
        w.writeframes(struct.pack('<%dh' % len(pcm), *pcm))
        w.close()
        print('  wav: %d samples = %.3f s' % (len(pcm), len(pcm) / vx.rate))
    cols = 6
    rows = (len(sheet) + cols - 1) // cols
    sw, sh = vx.width // 2, vx.height // 2
    canvas = Image.new('RGB', (cols * sw, rows * sh))
    for k, rgb in enumerate(sheet):
        canvas.paste(Image.fromarray(rgb).resize((sw, sh), Image.LANCZOS), ((k % cols) * sw, (k // cols) * sh))
    canvas.save(os.path.join(outdir, 'sheet.png'))
    n = vx.nframes
    stats = {'name': name, 'frames': n, 'w': vx.width, 'h': vx.height, 'fps': vx.fps, 'rate': vx.rate,
             'pcm_bytes': len(pcm) * 2, 'vx_bytes': os.path.getsize(path), 'raw565': n * vx.width * vx.height * 2}
    if jpeg_stats:
        stats['jpeg'] = dict(jpg)
    print('  ', stats)
    return stats


def main(argv):
    names = []
    out = os.path.join(HERE, '..', 'extracted', 'fmv')
    png = wav = True
    js = False
    it = iter(argv)
    for a in it:
        if a == '--out':
            out = next(it)
        elif a == '--no-png':
            png = False
        elif a == '--no-wav':
            wav = False
        elif a == '--jpeg-stats':
            js = True
        else:
            names.append(a)
    if not names:
        names = ['ealogo', 'tempintro']
    for n in names:
        path = n if os.path.exists(n) else os.path.join(ROM_DIR, n + '.vx')
        base = os.path.splitext(os.path.basename(path))[0]
        decode_file(path, os.path.join(out, base), png, wav, js)


if __name__ == '__main__':
    main(sys.argv[1:])
