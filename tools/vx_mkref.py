#!/usr/bin/env python3
"""Write the reference data for tests/test_vx.c from the PYTHON decoder (tools/vx_video.py, vx_audio.py),
which was verified bit-exactly against the game's ARM code.

    python tools/vx_mkref.py [outdir]          (default: <repo>/tests)  ->  vx_ref_ealogo.txt, vx_ref_tempintro.txt

Line format:  <frame> <crc32 of RGBA bytes (R,G,B,255; rows of exactly width pixels), 8 hex digits>
              <crc32 of the frame's PCM (s16 little endian), 8 hex digits> <sample count>
"""
import os, struct, sys, zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vx_video import VXFile, VXVideoDecoder
from vx_audio import VXAudioDecoder
from vx_extract import yuv_to_rgb


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'tests')
    for name in ('ealogo', 'tempintro'):
        vx = VXFile(os.path.join(HERE, '..', 'extracted', 'files', 'DATA', 'FMV', name + '.vx'))
        vdec = VXVideoDecoder(vx.width, vx.height, vx.quant)
        adec = VXAudioDecoder(vx.extradata)
        lines = ['# vx reference (python decoder): %s frames=%d width=%d height=%d rate=%d' % (
            name, vx.nframes, vx.width, vx.height, vx.rate),
            '# frame crc32(rgba) crc32(pcm s16le) npcm']
        for i in range(vx.nframes):
            data, na = vx.frame_payload(i)
            end = vdec.decode_frame(data)
            pos = (end + 15) // 16 * 2
            pcm = []
            for _ in range(na):
                p, pos = adec.decode(data, pos)
                pcm += p
            y, u, v = vdec.planes()
            rgb = yuv_to_rgb(y, u, v, vx.width, vx.height)
            rgba = np.concatenate([rgb, np.full(rgb.shape[:2] + (1,), 255, np.uint8)], -1)
            lines.append('%d %08x %08x %d' % (i, zlib.crc32(rgba.tobytes()) & 0xFFFFFFFF,
                                              zlib.crc32(struct.pack('<%dh' % len(pcm), *pcm)) & 0xFFFFFFFF, len(pcm)))
        path = os.path.join(out, 'vx_ref_%s.txt' % name)
        open(path, 'w', newline='\n').write('\n'.join(lines) + '\n')
        print('wrote', path, vx.nframes, 'frames')


if __name__ == '__main__':
    main()
