/* Actimagine "VX" (VXDS) movie decoder - video + audio, pure C99, integer only, no globals.
 *
 * Ported from tools/vx_video.py / tools/vx_audio.py, which were checked bit-exactly against the
 * game's own ARM9 decoder (see docs/VX_FORMAT.md).  Skate It's two movies (ealogo.vx, tempintro.vx)
 * are 256x192, 29.97 fps, 22050 Hz mono.
 *
 *   static Vx v;                                   // ~310 KB, caller allocates
 *   if (!vx_open(&v, file_bytes, file_size)) ...   // the file stays owned by the caller, must outlive v
 *   uint32_t pic[256 * 192]; int16_t pcm[1024]; int n;
 *   while (vx_next(&v, pic, 256, pcm, 1024, &n)) { show pic; queue pcm[0..n) at vx_audio_rate(&v) }
 *
 * Decoding is strictly sequential (frame 0 is the only key frame).  Corrupt or truncated input never
 * reads or writes out of bounds and never loops forever: vx_open / vx_next just return 0.
 */
#ifndef VX_H
#define VX_H

#include <stddef.h>
#include <stdint.h>

#define VX_MAX_W 256            /* larger movies are rejected by vx_open */
#define VX_MAX_H 192
#define VX_YREG  (8 * VX_MAX_W + VX_MAX_W * VX_MAX_H)                                   /* Y plane + guard rows */
#define VX_CREG  (8 * (VX_MAX_W / 2) + (VX_MAX_W / 2) * (VX_MAX_H / 2))                 /* one chroma plane + guard */
#define VX_FRAME_BYTES (VX_YREG + 2 * VX_CREG)

typedef struct Vx {
    const uint8_t *data;
    size_t size;
    uint32_t nframes, width, height, fps16, quant, rate, extra_off;
    uint32_t frame_no;          /* next frame to decode */
    size_t frame_off;           /* file offset of its record */
    int failed;
    int dq[16];                 /* dequantiser by coefficient position */
    int refs[4];                /* reference ring: refs[0] newest ... refs[3] = frame being decoded */
    uint8_t pix[4][VX_FRAME_BYTES];
    uint8_t yn[(VX_MAX_W / 4 + 1) * (VX_MAX_H / 4 + 1)];      /* non-zero coefficient counts (luma, per 4x4) */
    uint8_t cn[(VX_MAX_W / 8 + 1) * (VX_MAX_H / 8 + 1)];      /* ... chroma, per 8x8 */
    int mvs[2][(VX_MAX_W / 16 + 2) * (VX_MAX_H / 16 + 2)];    /* macroblock motion vectors (x, y) */
    /* audio decoder state */
    int32_t a_scale;
    int a_cur;
    int32_t a_hist[2][256];
    int32_t a_synth[8];
    int32_t a_refl[8];
    int32_t a_old[8];
} Vx;

/* The whole .vx file in memory. Validates the header and every frame offset against `size`.
   Returns 1 on success, 0 on a bad file. */
int vx_open(Vx *v, const uint8_t *data, size_t size);

int vx_frames(const Vx *v);
int vx_width(const Vx *v);
int vx_height(const Vx *v);
float vx_fps(const Vx *v);
int vx_audio_rate(const Vx *v);

/* Decode the next frame.
     rgba:  picture as bytes R,G,B,255 (uint32 value 0xFFBBGGRR on little endian), `stride` pixels per row
            (stride >= width); may be NULL to skip the picture (the decoder state still advances).
     pcm:   this frame's audio, mono signed 16 bit at vx_audio_rate(); 5 or 6 blocks of 128 samples per frame
            (n_audio * 128 in general, so pcm_max = 1024 is plenty for the shipped movies); may be NULL.
            If pcm is too small for the frame the call fails (returns 0).
     *npcm: number of samples produced (may be NULL).
   Returns 1 = ok, 0 = no more frames or corrupt frame (all later calls return 0 until vx_rewind). */
int vx_next(Vx *v, uint32_t *rgba, int stride, int16_t *pcm, int pcm_max, int *npcm);

/* Start again at frame 0 (resets all decoder state). */
void vx_rewind(Vx *v);

#endif
