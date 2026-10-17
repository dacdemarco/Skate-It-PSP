/* Actimagine VX decoder: see vx.h.  Ported from tools/vx_video.py and tools/vx_audio.py.
 *
 * Video: H.264-like, 16x16 macroblocks split down to 2x2, full-pel motion from 3 references, CAVLC
 * residue (H.264 tables), 4x4 integer transform; bits are 16-bit LE words read MSB first.
 * Audio: 128-sample LPC + pulse excitation frames, 10/12/14/20 bytes each, stored behind the video
 * bitstream of every frame.
 * All reads are bounds-checked (bit reader returns zeros past the end and flags the error), all pixel
 * accesses are inside guard rows, motion vectors are range checked, so corrupt data cannot crash.
 */
#include "vx.h"
#include <string.h>

static const uint8_t QUANT4[6][8] = {
    {10, 13, 10, 13, 13, 16, 13, 16},
    {11, 14, 11, 14, 14, 18, 14, 18},
    {13, 16, 13, 16, 16, 20, 16, 20},
    {14, 18, 14, 18, 18, 23, 18, 23},
    {16, 20, 16, 20, 20, 25, 20, 25},
    {18, 23, 18, 23, 23, 29, 23, 29},
};

static const uint8_t ZIGZAG[16] = {0, 4, 1, 2, 5, 8, 12, 9, 6, 3, 7, 10, 13, 14, 11, 15};

static const uint8_t CBP_NEW[32] = {
    0, 8, 4, 2, 1, 31, 15, 10, 5, 12, 3, 16, 14, 13, 11, 7,
    9, 6, 30, 27, 26, 29, 23, 21, 24, 18, 17, 28, 20, 19, 22, 25,
};

static const int LEVEL_LIMIT[6] = {2, 5, 11, 23, 47, 32768};

static const uint8_t CT_LEN[4][68] = {
  {
    1, 0, 0, 0, 6, 2, 0, 0, 8, 6, 3, 0, 9, 8, 7, 5, 10,
    9, 8, 6, 11, 10, 9, 7, 13, 11, 10, 8, 13, 13, 11, 9, 13, 13,
    13, 10, 14, 14, 13, 11, 14, 14, 14, 13, 15, 15, 14, 14, 15, 15, 15,
    14, 16, 15, 15, 15, 16, 16, 16, 15, 16, 16, 16, 16, 16, 16, 16, 16,
  },
  {
    2, 0, 0, 0, 6, 2, 0, 0, 6, 5, 3, 0, 7, 6, 6, 4, 8,
    6, 6, 4, 8, 7, 7, 5, 9, 8, 8, 6, 11, 9, 9, 6, 11, 11,
    11, 7, 12, 11, 11, 9, 12, 12, 12, 11, 12, 12, 12, 11, 13, 13, 13,
    12, 13, 13, 13, 13, 13, 14, 13, 13, 14, 14, 14, 13, 14, 14, 14, 14,
  },
  {
    4, 0, 0, 0, 6, 4, 0, 0, 6, 5, 4, 0, 6, 5, 5, 4, 7,
    5, 5, 4, 7, 5, 5, 4, 7, 6, 6, 4, 7, 6, 6, 4, 8, 7,
    7, 5, 8, 8, 7, 6, 9, 8, 8, 7, 9, 9, 8, 8, 9, 9, 9,
    8, 10, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
  },
  {
    6, 0, 0, 0, 6, 6, 0, 0, 6, 6, 6, 0, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
  },
};

static const uint8_t CT_BITS[4][68] = {
  {
    1, 0, 0, 0, 5, 1, 0, 0, 7, 4, 1, 0, 7, 6, 5, 3, 7,
    6, 5, 3, 7, 6, 5, 4, 15, 6, 5, 4, 11, 14, 5, 4, 8, 10,
    13, 4, 15, 14, 9, 4, 11, 10, 13, 12, 15, 14, 9, 12, 11, 10, 13,
    8, 15, 1, 9, 12, 11, 14, 13, 8, 7, 10, 9, 12, 4, 6, 5, 8,
  },
  {
    3, 0, 0, 0, 11, 2, 0, 0, 7, 7, 3, 0, 7, 10, 9, 5, 7,
    6, 5, 4, 4, 6, 5, 6, 7, 6, 5, 8, 15, 6, 5, 4, 11, 14,
    13, 4, 15, 10, 9, 4, 11, 14, 13, 12, 8, 10, 9, 8, 15, 14, 13,
    12, 11, 10, 9, 12, 7, 11, 6, 8, 9, 8, 10, 1, 7, 6, 5, 4,
  },
  {
    15, 0, 0, 0, 15, 14, 0, 0, 11, 15, 13, 0, 8, 12, 14, 12, 15,
    10, 11, 11, 11, 8, 9, 10, 9, 14, 13, 9, 8, 10, 9, 8, 15, 14,
    13, 13, 11, 14, 10, 12, 15, 10, 13, 12, 11, 14, 9, 12, 8, 10, 13,
    8, 13, 7, 9, 12, 9, 12, 11, 10, 5, 8, 7, 6, 1, 4, 3, 2,
  },
  {
    3, 0, 0, 0, 0, 1, 0, 0, 4, 5, 6, 0, 8, 9, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
    47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
  },
};

static const uint8_t TZ_LEN[15][16] = {
    {1, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9},
    {3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 0},
    {4, 3, 3, 3, 4, 4, 3, 3, 4, 5, 5, 6, 5, 6, 0, 0},
    {5, 3, 4, 4, 3, 3, 3, 4, 3, 4, 5, 5, 5, 0, 0, 0},
    {4, 4, 4, 3, 3, 3, 3, 3, 4, 5, 4, 5, 0, 0, 0, 0},
    {6, 5, 3, 3, 3, 3, 3, 3, 4, 3, 6, 0, 0, 0, 0, 0},
    {6, 5, 3, 3, 3, 2, 3, 4, 3, 6, 0, 0, 0, 0, 0, 0},
    {6, 4, 5, 3, 2, 2, 3, 3, 6, 0, 0, 0, 0, 0, 0, 0},
    {6, 6, 4, 2, 2, 3, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0},
    {5, 5, 3, 2, 2, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {4, 4, 3, 3, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {4, 4, 2, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {3, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

static const uint8_t TZ_BITS[15][16] = {
    {1, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 1},
    {7, 6, 5, 4, 3, 5, 4, 3, 2, 3, 2, 3, 2, 1, 0, 0},
    {5, 7, 6, 5, 4, 3, 4, 3, 2, 3, 2, 1, 1, 0, 0, 0},
    {3, 7, 5, 4, 6, 5, 4, 3, 3, 2, 2, 1, 0, 0, 0, 0},
    {5, 4, 3, 7, 6, 5, 4, 3, 2, 1, 1, 0, 0, 0, 0, 0},
    {1, 1, 7, 6, 5, 4, 3, 2, 1, 1, 0, 0, 0, 0, 0, 0},
    {1, 1, 5, 4, 3, 3, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 3, 3, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 1, 3, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 1, 3, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 2, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

static const uint8_t RUN_LEN[7][15] = {
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 2, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0},
    {3, 3, 3, 3, 3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11},
};

static const uint8_t RUN_BITS[7][15] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {3, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {3, 2, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {3, 0, 1, 3, 2, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0},
    {7, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};


static const uint8_t AUDIO_FRAME_LEN[4] = { 20, 14, 12, 10 };

/* ------------------------------------------------------------------ little endian reads */
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }

/* ------------------------------------------------------------------ bit reader */
typedef struct {
    const uint8_t *p;
    size_t len;          /* bytes */
    size_t pos;          /* bits */
    size_t nbits;
    int err;
} Br;

static uint32_t br_w16(const Br *b, size_t w)
{
    size_t o = w * 2;
    if (o + 1 < b->len) return rd16(b->p + o);
    if (o < b->len) return b->p[o];
    return 0;
}

/* next n bits (n <= 25) without consuming */
static uint32_t br_peek_at(const Br *b, size_t pos, int n)
{
    size_t w = pos >> 4;
    uint64_t t = ((uint64_t)br_w16(b, w) << 32) | ((uint64_t)br_w16(b, w + 1) << 16) | br_w16(b, w + 2);
    return (uint32_t)((t >> (48 - (int)(pos & 15) - n)) & ((1u << n) - 1u));
}
static uint32_t br_peek(const Br *b, int n) { return br_peek_at(b, b->pos, n); }

static uint32_t br_read(Br *b, int n)
{
    uint32_t v;
    if (n == 0) return 0;
    v = br_peek(b, n);
    b->pos += (size_t)n;
    if (b->pos > b->nbits) b->err = 1;
    return v;
}
static int br_bit(Br *b) { return (int)br_read(b, 1); }

/* number of 0 bits before the next 1 bit (consumes the 1); more than 31 -> error */
static int br_unary(Br *b)
{
    int z = 0;
    while (z < 32 && br_peek_at(b, b->pos + (size_t)z, 1) == 0) z++;
    if (z >= 32) { b->err = 1; return 0; }
    b->pos += (size_t)z + 1;
    if (b->pos > b->nbits) b->err = 1;
    return z;
}

/* Elias-gamma' / ue golomb */
static int br_ue(Br *b)
{
    int z = br_unary(b);
    if (z > 24) { b->err = 1; return 0; }
    if (z == 0) return 0;
    return (int)((1u << z) - 1u + br_read(b, z));
}
static int br_se(Br *b)
{
    int v = br_ue(b) + 1;
    return (v & 1) == 0 ? (v >> 1) : ((1 - v) >> 1);
}

static int vlc_ct(Br *b, int tab)
{
    uint32_t pk = br_peek(b, 16);
    int s;
    for (s = 0; s < 68; s++) {
        int ln = CT_LEN[tab][s];
        if (ln && (pk >> (16 - ln)) == CT_BITS[tab][s]) {
            b->pos += (size_t)ln;
            if (b->pos > b->nbits) b->err = 1;
            return s;
        }
    }
    b->err = 1;
    return -1;
}
static int vlc_tz(Br *b, int t)     /* t = total_coeff - 1 */
{
    uint32_t pk = br_peek(b, 16);
    int s;
    for (s = 0; s < 16; s++) {
        int ln = TZ_LEN[t][s];
        if (ln && (pk >> (16 - ln)) == TZ_BITS[t][s]) {
            b->pos += (size_t)ln;
            if (b->pos > b->nbits) b->err = 1;
            return s;
        }
    }
    b->err = 1;
    return -1;
}
static int vlc_run(Br *b, int t)    /* t = min(zeros_left, 7) - 1 */
{
    uint32_t pk = br_peek(b, 16);
    int s;
    for (s = 0; s < 15; s++) {
        int ln = RUN_LEN[t][s];
        if (ln && (pk >> (16 - ln)) == RUN_BITS[t][s]) {
            b->pos += (size_t)ln;
            if (b->pos > b->nbits) b->err = 1;
            return s;
        }
    }
    b->err = 1;
    return -1;
}

/* ------------------------------------------------------------------ video decoder */
typedef struct {
    Vx *v;
    Br br;
    int W, H, cw, ch;
    uint8_t *cur[3];            /* planes of the frame being decoded, pointing at pixel (0,0) */
    uint8_t *ref[3][3];         /* reference frames 0..2, planes */
    int S[3];                   /* strides */
    int pmx, pmy;               /* predicted MV of the macroblock */
    int cmx, cmy;               /* last MV used inside the macroblock */
    int ipred4[25];
} Dec;

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static int median3(int a, int b, int c)
{
    int t;
    if (a > b) { t = a; a = b; b = t; }
    if (b > c) b = c;
    return a > b ? a : b;
}

static int mc_copy(Dec *d, int x, int y, int w, int h, int mx, int my, uint8_t *const src[3],
                   int dY, int dU, int dV)
{
    int r, k;
    if (x + mx < 0 || x + mx + w > d->W || y + my < 0 || y + my + h > d->H) return -1;
    for (r = 0; r < h; r++) {
        const uint8_t *s = src[0] + (y + my + r) * d->S[0] + x + mx;
        uint8_t *o = d->cur[0] + (y + r) * d->S[0] + x;
        if (dY == 0) memcpy(o, s, (size_t)w);
        else for (k = 0; k < w; k++) o[k] = (uint8_t)clamp8(s[k] + dY);
    }
    {
        int sx = (x + mx) >> 1, sy = (y + my) >> 1, cx = x >> 1, cy = y >> 1, cwid = w >> 1, p;
        for (p = 1; p <= 2; p++) {
            int dl = p == 1 ? dU : dV;
            for (r = 0; r < (h >> 1); r++) {
                const uint8_t *s = src[p] + (sy + r) * d->S[p] + sx;
                uint8_t *o = d->cur[p] + (cy + r) * d->S[p] + cx;
                if (dl == 0) memcpy(o, s, (size_t)cwid);
                else for (k = 0; k < cwid; k++) o[k] = (uint8_t)clamp8(s[k] + dl);
            }
        }
    }
    return 0;
}

static int do_mc(Dec *d, int x, int y, int w, int h, int ref, int has_delta)
{
    int mx = d->pmx, my = d->pmy;
    if (has_delta) {
        int dx = br_se(&d->br);
        int dy = br_se(&d->br);
        mx += dx;
        my += dy;
    }
    d->cmx = mx;
    d->cmy = my;
    return mc_copy(d, x, y, w, h, mx, my, d->ref[ref], 0, 0, 0);
}

static int do_mc_bias(Dec *d, int x, int y, int w, int h)
{
    int mx = br_se(&d->br);
    int my = br_se(&d->br);
    int dy = br_se(&d->br) * 2;
    int du = br_se(&d->br) * 2;
    int dv = br_se(&d->br) * 2;
    return mc_copy(d, x, y, w, h, mx, my, d->ref[0], dy, du, dv);
}

/* ---- intra prediction */
static void pl_vert(Dec *d, int p, int x, int y, int w, int h)
{
    int S = d->S[p], r;
    uint8_t *b = d->cur[p];
    for (r = 0; r < h; r++) memcpy(b + (y + r) * S + x, b + (y - 1) * S + x, (size_t)w);
}
static void pl_hor(Dec *d, int p, int x, int y, int w, int h)
{
    int S = d->S[p], r;
    uint8_t *b = d->cur[p];
    for (r = 0; r < h; r++) memset(b + (y + r) * S + x, b[(y + r) * S + x - 1], (size_t)w);
}
static void pl_dc(Dec *d, int p, int x, int y, int w, int h)
{
    int S = d->S[p], r, k, dc;
    uint8_t *b = d->cur[p];
    if (x == 0 && y == 0) dc = 128;
    else {
        int top = 0, left = 0;
        if (y) for (k = 0; k < w; k++) top += b[(y - 1) * S + x + k];
        if (x) for (k = 0; k < h; k++) left += b[(y + k) * S + x - 1];
        if (y == 0) dc = (left + h / 2) / h;
        else if (x == 0) dc = (top + w / 2) / w;
        else dc = (((top + w / 2) / w) + ((left + h / 2) / h) + 1) >> 1;
    }
    for (r = 0; r < h; r++) memset(b + (y + r) * S + x, dc, (size_t)w);
}

static int ctz_int(int v) { int n = 0; while (!(v & 1)) { v >>= 1; n++; } return n; }

static void plane_rec(uint8_t *o0, int S, int w, int h)
{
    int hw, hh;
    if (w == 1 && h == 1) return;
    if (h == 1) {
        o0[w / 2 - 1] = (uint8_t)((o0[-1] + o0[w - 1]) >> 1);
        if (w > 2) { plane_rec(o0, S, w / 2, 1); plane_rec(o0 + w / 2, S, w / 2, 1); }
        return;
    }
    if (w == 1) {
        o0[(h / 2 - 1) * S] = (uint8_t)((o0[-S] + o0[(h - 1) * S]) >> 1);
        if (h > 2) { plane_rec(o0, S, 1, h / 2); plane_rec(o0 + (h / 2) * S, S, 1, h / 2); }
        return;
    }
    {
        int even = ((ctz_int(w) + ctz_int(h)) & 1) == 0;
        int dr = o0[(h - 1) * S + w - 1];
        int tr = o0[-S + w - 1];
        int dl = o0[(h - 1) * S - 1];
        int dm = (dl + dr) >> 1;
        int cr = (tr + dr) >> 1;
        int c;
        o0[(h - 1) * S + w / 2 - 1] = (uint8_t)dm;
        o0[(h / 2 - 1) * S + w - 1] = (uint8_t)cr;
        if (even) c = (dm + o0[-S + w / 2 - 1]) >> 1;
        else c = (cr + o0[(h / 2 - 1) * S - 1]) >> 1;
        o0[(h / 2 - 1) * S + w / 2 - 1] = (uint8_t)c;
    }
    hw = w / 2;
    hh = h / 2;
    plane_rec(o0, S, hw, hh);
    plane_rec(o0 + hw, S, hw, hh);
    plane_rec(o0 + hh * S, S, hw, hh);
    plane_rec(o0 + hh * S + hw, S, hw, hh);
}

static void pl_plane(Dec *d, int p, int x, int y, int w, int h, int delta)
{
    int S = d->S[p];
    uint8_t *bp = d->cur[p] + y * S + x;
    int tr = bp[(w - 1) - S];
    int dl = bp[(h - 1) * S - 1];
    int v = ((tr + dl + 1) >> 1) + delta;
    bp[(h - 1) * S + w - 1] = (uint8_t)(v & 255);      /* wraps mod 256 like the DS code */
    plane_rec(bp, S, w, h);
}

static void do_plane_delta(Dec *d, int x, int y, int w, int h)
{
    int dy = br_se(&d->br) * 2;
    int du = br_se(&d->br) * 2;
    int dv = br_se(&d->br) * 2;
    pl_plane(d, 0, x, y, w, h, dy);
    pl_plane(d, 1, x >> 1, y >> 1, w >> 1, h >> 1, du);
    pl_plane(d, 2, x >> 1, y >> 1, w >> 1, h >> 1, dv);
}

static int chroma_intra(Dec *d, int x, int y, int w, int h)
{
    int cmode = br_ue(&d->br), p;
    int cx = x >> 1, cy = y >> 1, cw = w >> 1, ch = h >> 1;
    if (cmode > 3) return -1;
    for (p = 1; p <= 2; p++) {
        if (cmode == 0) pl_dc(d, p, cx, cy, cw, ch);
        else if (cmode == 1) pl_hor(d, p, cx, cy, cw, ch);
        else if (cmode == 2) pl_vert(d, p, cx, cy, cw, ch);
        else pl_plane(d, p, cx, cy, cw, ch, 0);
    }
    return 0;
}

static int intra_full(Dec *d, int x, int y, int w, int h)
{
    int ymode = br_ue(&d->br);
    if (ymode == 0) pl_vert(d, 0, x, y, w, h);
    else if (ymode == 1) pl_hor(d, 0, x, y, w, h);
    else if (ymode == 2) pl_dc(d, 0, x, y, w, h);
    else if (ymode == 3) pl_plane(d, 0, x, y, w, h, 0);
    else return -1;
    return chroma_intra(d, x, y, w, h);
}

static int p3(int a, int b, int c) { return (a + 2 * b + c + 2) >> 2; }
static int p2(int a, int b) { return (a + b + 1) >> 1; }

static void pred4(Dec *d, int mode, int x, int y)
{
    int S = d->S[0];
    uint8_t *b = d->cur[0] + y * S + x;
    int T[8], L[4], M, out[4][4], r, c, i;
    if (mode == 0) { pl_vert(d, 0, x, y, 4, 4); return; }
    if (mode == 1) { pl_hor(d, 0, x, y, 4, 4); return; }
    for (i = 0; i < 8; i++) T[i] = b[-S + i];
    for (i = 0; i < 4; i++) L[i] = b[i * S - 1];
    M = b[-S - 1];
    memset(out, 0, sizeof(out));
    if (mode == 2) {
        int dc;
        if (x == 0 && y == 0) dc = 128;
        else {
            int sm = 0, sh = 1;
            if (x) { sh++; sm += L[0] + L[1] + L[2] + L[3] + 2; }
            if (y) { sh++; sm += T[0] + T[1] + T[2] + T[3] + 2; }
            dc = sm >> sh;
        }
        for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) out[r][c] = dc;
    } else if (mode == 3) {
        for (r = 0; r < 4; r++)
            for (c = 0; c < 4; c++) {
                i = r + c;
                out[r][c] = i < 6 ? p3(T[i], T[i + 1], T[i + 2]) : p3(T[6], T[7], T[7]);
            }
    } else if (mode == 4) {
        int e[9];
        e[0] = L[3]; e[1] = L[2]; e[2] = L[1]; e[3] = L[0]; e[4] = M; e[5] = T[0]; e[6] = T[1]; e[7] = T[2]; e[8] = T[3];
        for (r = 0; r < 4; r++)
            for (c = 0; c < 4; c++) {
                int k = 4 - r + c;
                out[r][c] = p3(e[k - 1], e[k], e[k + 1]);
            }
    } else if (mode == 5) {
        int a = T[0], bb = T[1], c2 = T[2], dd = T[3], ii = L[0], jj = L[1], kk = L[2];
        out[3][0] = p3(ii, jj, kk);
        out[2][0] = p3(M, ii, jj);
        out[1][0] = out[3][1] = p3(a, M, ii);
        out[0][0] = out[2][1] = p2(a, M);
        out[1][1] = out[3][2] = p3(bb, a, M);
        out[0][1] = out[2][2] = p2(bb, a);
        out[1][2] = out[3][3] = p3(c2, bb, a);
        out[0][2] = out[2][3] = p2(c2, bb);
        out[1][3] = p3(dd, c2, bb);
        out[0][3] = p2(dd, c2);
    } else if (mode == 6) {
        int a = T[0], bb = T[1], c2 = T[2], ii = L[0], jj = L[1], kk = L[2], ll = L[3];
        out[3][0] = p2(kk, ll);
        out[3][1] = p3(jj, kk, ll);
        out[2][0] = out[3][2] = p2(jj, kk);
        out[2][1] = out[3][3] = p3(ii, jj, kk);
        out[1][0] = out[2][2] = p2(ii, jj);
        out[1][1] = out[2][3] = p3(M, ii, jj);
        out[0][0] = out[1][2] = p2(ii, M);
        out[0][1] = out[1][3] = p3(ii, M, a);
        out[0][2] = p3(M, a, bb);
        out[0][3] = p3(a, bb, c2);
    } else if (mode == 7) {
        int a = T[0], bb = T[1], c2 = T[2], dd = T[3], e = T[4], f = T[5], g = T[6];
        out[3][3] = p3(e, f, g);
        out[2][3] = p2(e, f);
        out[1][3] = out[3][2] = p3(dd, e, f);
        out[0][3] = out[2][2] = p2(dd, e);
        out[1][2] = out[3][1] = p3(c2, dd, e);
        out[0][2] = out[2][1] = p2(c2, dd);
        out[1][1] = out[3][0] = p3(bb, c2, dd);
        out[0][1] = out[2][0] = p2(bb, c2);
        out[1][0] = p3(a, bb, c2);
        out[0][0] = p2(a, bb);
    } else {                    /* 8: horizontal up */
        int ii = L[0], jj = L[1], kk = L[2], ll = L[3];
        out[0][0] = p2(ii, jj); out[0][1] = p3(ii, jj, kk); out[0][2] = p2(jj, kk); out[0][3] = p3(jj, kk, ll);
        out[1][0] = p2(jj, kk); out[1][1] = p3(jj, kk, ll); out[1][2] = p2(kk, ll); out[1][3] = p3(kk, ll, ll);
        out[2][0] = p2(kk, ll); out[2][1] = p3(kk, ll, ll); out[2][2] = ll; out[2][3] = ll;
        out[3][0] = ll; out[3][1] = ll; out[3][2] = ll; out[3][3] = ll;
    }
    for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) b[r * S + c] = (uint8_t)out[r][c];
}

static int intra4(Dec *d, int x, int y, int w, int h)
{
    int by, bx;
    int *c = d->ipred4;
    for (by = 0; by < (h >> 2); by++)
        for (bx = 0; bx < (w >> 2); bx++) {
            int mode = MIN(c[by * 5 + 1 + bx], c[(by + 1) * 5 + bx]);
            if (mode == 9) mode = 2;
            if (!br_bit(&d->br)) {
                int val = (int)br_read(&d->br, 3);
                mode = val >= mode ? val + 1 : val;
            }
            c[(by + 1) * 5 + 1 + bx] = mode;
            pred4(d, mode, x + bx * 4, y + by * 4);
        }
    return chroma_intra(d, x, y, w, h);
}

/* ---- residue */
static int coeffs(Dec *d, int plane, int x, int y, int nc)
{
    Br *br = &d->br;
    int tab = nc >= 8 ? 3 : (nc >= 4 ? 2 : (nc >= 2 ? 1 : 0));
    int tok = vlc_ct(br, tab);
    int total, t1, level[16], dct[16], tmp[16], i = 15, zl = 0, suffix = 0, left, k, r;
    uint8_t *buf;
    int S;
    if (tok < 0) return -1;
    total = tok >> 2;
    t1 = tok & 3;
    if (total == 0) return 0;
    memset(level, 0, sizeof(level));
    if (total != 16) {
        int tz;
        zl = vlc_tz(br, total - 1);
        if (zl < 0) return -1;
        tz = 16 - (total + zl);
        if (tz > 0) i -= tz;
    }
    left = total;
    for (;;) {
        int lv;
        if (br->err || i < 0) return -1;
        if (t1 > 0) {
            t1--;
            lv = br_bit(br) ? -1 : 1;
        } else {
            int prefix = br_unary(br), sfx, code;
            if (prefix == 15) sfx = (int)br_read(br, 11);
            else sfx = suffix ? (int)br_read(br, suffix) : 0;
            code = sfx + (prefix << suffix);
            if (code > LEVEL_LIMIT[suffix] && suffix < 5) suffix++;
            code++;
            lv = br_bit(br) ? -code : code;
        }
        level[i--] = lv;
        if (--left == 0) break;
        if (zl == 0) continue;
        {
            int rb = vlc_run(br, (zl < 7 ? zl : 7) - 1);
            if (rb < 0 || rb > zl) return -1;
            zl -= rb;
            i -= rb;
        }
    }
    if (br->err) return -1;
    memset(dct, 0, sizeof(dct));
    for (k = 0; k < 16; k++)
        if (level[k]) dct[ZIGZAG[k]] = level[k] * d->v->dq[ZIGZAG[k]];
    dct[0] += 32;
    for (r = 0; r < 4; r++) {
        int a = dct[r * 4], b = dct[r * 4 + 1], c = dct[r * 4 + 2], dd = dct[r * 4 + 3], e, f;
        a += c;
        c = a - 2 * c;
        e = (b >> 1) - dd;
        f = b + (dd >> 1);
        tmp[r] = a + f;
        tmp[4 + r] = c + e;
        tmp[8 + r] = c - e;
        tmp[12 + r] = a - f;
    }
    buf = d->cur[plane];
    S = d->S[plane];
    for (r = 0; r < 4; r++) {
        int a = tmp[r * 4], b = tmp[r * 4 + 1], c = tmp[r * 4 + 2], dd = tmp[r * 4 + 3], e, f;
        uint8_t *o = buf + (y + r) * S + x;
        a += c;
        c = a - 2 * c;
        e = (b >> 1) - dd;
        f = b + (dd >> 1);
        o[0] = (uint8_t)clamp8(o[0] + ((a + f) >> 6));
        o[1] = (uint8_t)clamp8(o[1] + ((c + e) >> 6));
        o[2] = (uint8_t)clamp8(o[2] + ((c - e) >> 6));
        o[3] = (uint8_t)clamp8(o[3] + ((a - f) >> 6));
    }
    return total;
}

static int residue(Dec *d, int x, int y, int w, int h)
{
    Br *br = &d->br;
    Vx *v = d->v;
    int ynS = d->W / 4 + 1, cnS = d->W / 8 + 1;
    int y2, x2, k;
    for (y2 = 0; y2 < (h >> 3); y2++)
        for (x2 = 0; x2 < (w >> 3); x2++) {
            int code = br_ue(br), mask, px = x + x2 * 8, py = y + y2 * 8, bx = px >> 2, by = py >> 2, cidx;
            if (br->err || code > 31) return -1;
            mask = CBP_NEW[code];
            for (k = 0; k < 4; k++) {
                int kx = bx + (k & 1), ky = by + (k >> 1), idx = (ky + 1) * ynS + kx + 1;
                if (mask & (1 << k)) {
                    int nc = (v->yn[idx - 1] + v->yn[idx - ynS] + 1) >> 1;
                    int n = coeffs(d, 0, px + (k & 1) * 4, py + (k >> 1) * 4, nc);
                    if (n < 0) return -1;
                    v->yn[idx] = (uint8_t)n;
                } else v->yn[idx] = 0;
            }
            cidx = ((py >> 3) + 1) * cnS + (px >> 3) + 1;
            if (mask & 16) {
                int nc = (v->cn[cidx - 1] + v->cn[cidx - cnS] + 1) >> 1;
                int u = coeffs(d, 1, px >> 1, py >> 1, nc);
                int vv;
                if (u < 0) return -1;
                vv = coeffs(d, 2, px >> 1, py >> 1, nc);
                if (vv < 0) return -1;
                v->cn[cidx] = (uint8_t)((u + vv + 1) >> 1);
            } else v->cn[cidx] = 0;
        }
    return 0;
}

/* ---- block syntax */
static int block(Dec *d, int x, int y, int w, int h)
{
    int mode, large, ref;
    if (d->br.err) return -1;
    mode = br_ue(&d->br);
    if (d->br.err || mode > 23) return -1;
    large = MIN(w, h) >= 8;
    if (mode >= 16 && !large) return -1;
    switch (mode) {
    case 0: case 8:
        if (w == 2 || (mode == 8 && !large)) return -1;
        if (block(d, x, y, w / 2, h) < 0) return -1;
        if (block(d, x + w / 2, y, w / 2, h) < 0) return -1;
        if (mode == 8 && residue(d, x, y, w, h) < 0) return -1;
        return 0;
    case 2: case 13:
        if (h == 2 || (mode == 13 && !large)) return -1;
        if (block(d, x, y, w, h / 2) < 0) return -1;
        if (block(d, x, y + h / 2, w, h / 2) < 0) return -1;
        if (mode == 13 && residue(d, x, y, w, h) < 0) return -1;
        return 0;
    case 1: case 9: case 14: case 12: case 20: case 21:
        ref = (mode == 1 || mode == 12) ? 0 : ((mode == 9 || mode == 20) ? 1 : 2);
        if ((mode == 12 || mode == 20 || mode == 21) && !large) return -1;
        if (do_mc(d, x, y, w, h, ref, 0) < 0) return -1;
        if ((mode == 12 || mode == 20 || mode == 21) && residue(d, x, y, w, h) < 0) return -1;
        return 0;
    case 4: case 5: case 6: case 16: case 17: case 18:
        ref = mode < 16 ? mode - 4 : mode - 16;
        if (do_mc(d, x, y, w, h, ref, 1) < 0) return -1;
        if (mode >= 16 && residue(d, x, y, w, h) < 0) return -1;
        return 0;
    case 3: case 10:
        if (mode == 10 && !large) return -1;
        if (do_mc_bias(d, x, y, w, h) < 0) return -1;
        if (mode == 10 && residue(d, x, y, w, h) < 0) return -1;
        return 0;
    case 7: case 23:
        do_plane_delta(d, x, y, w, h);
        if (mode == 23 && residue(d, x, y, w, h) < 0) return -1;
        return 0;
    case 11: case 22:
        if (MIN(w, h) >= 4 && intra_full(d, x, y, w, h) < 0) return -1;
        if (mode == 22 && residue(d, x, y, w, h) < 0) return -1;
        return 0;
    default:                    /* 15, 19 */
        if (MIN(w, h) >= 4 && intra4(d, x, y, w, h) < 0) return -1;
        if (mode == 19 && residue(d, x, y, w, h) < 0) return -1;
        return 0;
    }
}

static uint8_t *frame_plane(Vx *v, int id, int p)
{
    uint8_t *f = v->pix[id];
    int W = (int)v->width;
    int H = (int)v->height;
    if (p == 0) return f + 4 * W;
    f += 8 * W + W * H;                                     /* past Y region */
    if (p == 2) f += 8 * (W / 2) + (W / 2) * (H / 2);       /* past U region */
    return f + 4 * (W / 2);
}

/* decode one video frame from `data`; returns the end bit position or -1 */
static long decode_video(Vx *v, const uint8_t *data, size_t len)
{
    Dec d;
    int mx, my, i, mvw;
    memset(&d, 0, sizeof(d));
    d.v = v;
    d.br.p = data;
    d.br.len = len;
    d.br.nbits = len * 8;
    d.W = (int)v->width;
    d.H = (int)v->height;
    d.cw = d.W / 2;
    d.ch = d.H / 2;
    d.S[0] = d.W;
    d.S[1] = d.S[2] = d.cw;
    for (i = 0; i < 3; i++) {
        int p;
        d.cur[i] = frame_plane(v, v->refs[3], i);
        for (p = 0; p < 3; p++) d.ref[p][i] = frame_plane(v, v->refs[p], i);
    }
    for (i = 0; i < 25; i++) d.ipred4[i] = 9;
    memset(v->yn, 0, sizeof(v->yn));
    memset(v->cn, 0, sizeof(v->cn));
    memset(v->mvs, 0, sizeof(v->mvs));
    mvw = d.W / 16 + 2;
    for (my = 0; my < d.H / 16; my++)
        for (mx = 0; mx < d.W / 16; mx++) {
            int l = (my + 1) * mvw + mx, t = my * mvw + mx + 1, tr = my * mvw + mx + 2;
            d.pmx = median3(v->mvs[0][l], v->mvs[0][t], v->mvs[0][tr]);
            d.pmy = median3(v->mvs[1][l], v->mvs[1][t], v->mvs[1][tr]);
            d.cmx = d.cmy = 0;
            if (block(&d, mx * 16, my * 16, 16, 16) < 0 || d.br.err) return -1;
            v->mvs[0][(my + 1) * mvw + mx + 1] = d.cmx;
            v->mvs[1][(my + 1) * mvw + mx + 1] = d.cmy;
        }
    return (long)d.br.pos;
}

/* ------------------------------------------------------------------ audio decoder */
static uint32_t u32add(int32_t a, int32_t b) { return (uint32_t)a + (uint32_t)b; }
static int32_t i32(uint32_t v) { return (int32_t)v; }

static void audio_reset(Vx *v)
{
    const uint8_t *e = v->data + v->extra_off;
    v->a_scale = (int32_t)rd32(e + 3120);
    v->a_cur = 1;
    memset(v->a_hist, 0, sizeof(v->a_hist));
    memset(v->a_synth, 0, sizeof(v->a_synth));
    memset(v->a_refl, 0, sizeof(v->a_refl));
    memset(v->a_old, 0, sizeof(v->a_old));
}

static void synth(Vx *v, const int32_t *exc, const int32_t *a, int n, int16_t *out_pcm)
{
    int32_t *st = v->a_synth;
    int blk, j, i;
    for (blk = 0; blk < n / 8; blk++)
        for (j = 0; j < 8; j++) {
            uint32_t acc = (uint32_t)exc[blk * 8 + j] << 14;
            int32_t y;
            for (i = 0; i < 8; i++) acc += (uint32_t)a[i] * (uint32_t)st[(j - 1 - i) & 7];
            y = i32(acc) >> 14;
            st[j] = y;
            out_pcm[blk * 8 + j] = (int16_t)(y > 32767 ? 32767 : (y < -32768 ? -32768 : y));
        }
}

/* decode one 128-sample frame at data+pos (caller checked that AUDIO_FRAME_LEN bytes are available) */
static void audio_frame(Vx *v, const uint8_t *data, int16_t *pcm)
{
    const uint8_t *e = v->data + v->extra_off;
    uint32_t w0 = rd16(data), w1 = rd16(data + 2);
    int off = (int)(w0 >> 9), mode = (int)((w1 >> 12) & 3), start = (int)((w1 >> 14) & 3);
    int intra = off == 0x7F, i, k;
    int32_t exc[128], kf[8], a[8];
    int32_t scale;
    const uint8_t *pp = data + 4;
    int32_t base[8];

    if (intra) {
        v->a_scale = (int32_t)rd32(e + 3120);
        memset(v->a_synth, 0, sizeof(v->a_synth));
        v->a_cur = 1;
        memset(exc, 0, sizeof(exc));
        memset(v->a_hist[v->a_cur] + 128, 0, 128 * sizeof(int32_t));
    } else {
        int prev = v->a_cur ^ 1;
        memcpy(v->a_hist[v->a_cur], v->a_hist[prev] + 128, 128 * sizeof(int32_t));
        if (off == 0x7E) memset(exc, 0, sizeof(exc));
        else {
            const int32_t *s = v->a_hist[prev] + (127 - off);
            exc[0] = s[0] >> 4;
            for (i = 1; i < 7; i++) exc[i] = i32((uint32_t)s[i] * (uint32_t)(i + 1)) >> 4;
            for (i = 7; i < 121; i++) exc[i] = s[i] >> 1;
            for (i = 121; i < 127; i++) exc[i] = i32((uint32_t)s[i] * (uint32_t)(128 - i)) >> 4;
            exc[127] = s[127] >> 4;
        }
    }
    /* scale update + pulses */
    v->a_scale = i32((uint32_t)(int32_t)(int16_t)rd16(e + 3072 + 2 * ((w0 >> 6) & 7)) * (uint32_t)v->a_scale) >> 13;
    scale = v->a_scale;
    if (mode == 0) {
        int p = start, w, sh;
        unsigned lsb = 0;
        for (w = 0; w < 8; w++) {
            uint32_t wd = rd16(pp);
            pp += 2;
            for (sh = 13; sh >= 1; sh -= 3) {
                int val = (int)((wd >> sh) & 7);
                exc[p] = i32(u32add(exc[p], i32((uint32_t)(2 * val - 7) * (uint32_t)scale)));
                p += 3;
            }
            lsb = ((wd & 1) | (lsb << 1)) & 0xFFFFFFFFu;
        }
        for (sh = 5; sh >= 2; sh -= 3) {
            int val = (int)((lsb >> sh) & 7);
            exc[p] = i32(u32add(exc[p], i32((uint32_t)(2 * val - 7) * (uint32_t)scale)));
            p += 3;
        }
    } else {
        static const uint8_t words[4] = { 0, 5, 4, 3 }, step[4] = { 0, 3, 4, 5 };
        int p = start, w, sh;
        for (w = 0; w < words[mode]; w++) {
            uint32_t wd = rd16(pp);
            pp += 2;
            for (sh = 14; sh >= 0; sh -= 2) {
                int val = (int)((wd >> sh) & 3);
                exc[p] = i32(u32add(exc[p], i32((uint32_t)(2 * val - 3) * (uint32_t)scale)));
                p += step[mode];
            }
        }
    }
    memcpy(v->a_hist[v->a_cur] + 128, exc, sizeof(exc));
    /* filter: code-book domain, then Q15 step-up to direct form */
    for (i = 0; i < 8; i++) {
        base[i] = intra ? (int32_t)rd32(e + 3088 + 4 * i) : v->a_refl[i];
        kf[i] = i32(u32add(u32add(u32add(base[i], (int16_t)rd16(e + ((w0 & 0x3F) * 8 + i) * 2)),
                                  (int16_t)rd16(e + 1024 + (((w1 >> 6) & 0x3F) * 8 + i) * 2)),
                           (int16_t)rd16(e + 2048 + ((w1 & 0x3F) * 8 + i) * 2)));
    }
    memcpy(v->a_refl, kf, sizeof(kf));
    for (k = 1; k < 8; k++) {
        int32_t km = kf[k], old[8];
        int j;
        memcpy(old, kf, sizeof(old));
        for (j = 0; j < k; j++) kf[j] = i32(u32add(old[j], i32((uint32_t)old[k - 1 - j] * (uint32_t)km) >> 15));
    }
    for (i = 0; i < 8; i++) a[i] = -(kf[i] >> 1);
    if (intra) synth(v, exc, a, 128, pcm);
    else {
        int32_t avg[8], q1[8], q3[8];
        for (i = 0; i < 8; i++) {
            avg[i] = i32(u32add(a[i], v->a_old[i])) >> 1;
            q1[i] = i32(u32add(avg[i], v->a_old[i])) >> 1;
            q3[i] = i32(u32add(avg[i], a[i])) >> 1;
        }
        synth(v, exc, q1, 32, pcm);
        synth(v, exc + 32, avg, 32, pcm + 32);
        synth(v, exc + 64, q3, 32, pcm + 64);
        synth(v, exc + 96, a, 32, pcm + 96);
    }
    memcpy(v->a_old, a, sizeof(a));
    v->a_cur ^= 1;
}

/* ------------------------------------------------------------------ container / public API */
static void reset_state(Vx *v)
{
    int i, q = (int)v->quant, qx = q % 6, qy = q / 6, j, c;
    size_t W = v->width, H = v->height;
    size_t yreg = 8 * W + W * H, creg = 8 * (W / 2) + (W / 2) * (H / 2);
    for (i = 0; i < 4; i++) {
        memset(v->pix[i], 0, yreg);
        memset(v->pix[i] + yreg, 0x80, 2 * creg);
        v->refs[i] = i;
    }
    for (j = 0; j < 4; j++)
        for (c = 0; c < 4; c++) v->dq[j * 4 + c] = (int)QUANT4[qx][(c & 1) * 4 + j] << qy;
    audio_reset(v);
    v->frame_no = 0;
    v->frame_off = 0x30;
    v->failed = 0;
}

int vx_open(Vx *v, const uint8_t *data, size_t size)
{
    uint32_t i, seek_off, seek_n, nstreams;
    size_t p;
    if (!v || !data) return 0;
    memset(v, 0, sizeof(*v));
    if (size < 0x30 || memcmp(data, "VXDS", 4) != 0) return 0;
    v->data = data;
    v->size = size;
    v->nframes = rd32(data + 4);
    v->width = rd32(data + 8);
    v->height = rd32(data + 12);
    v->fps16 = rd32(data + 16);
    v->quant = rd32(data + 20);
    v->rate = rd32(data + 24);
    nstreams = rd32(data + 28);
    v->extra_off = rd32(data + 36);
    seek_off = rd32(data + 40);
    seek_n = rd32(data + 44);
    if (v->nframes == 0 || v->nframes > (1u << 22)) return 0;
    if (v->width < 16 || v->width > VX_MAX_W || (v->width & 15)) return 0;
    if (v->height < 16 || v->height > VX_MAX_H || (v->height & 15)) return 0;
    if (v->quant > 51) return 0;
    if (v->fps16 < 65536u || v->fps16 > 65536u * 120u) return 0;
    if (v->rate < 4000 || v->rate > 96000) return 0;
    if (nstreams != 1) return 0;
    if ((uint64_t)v->extra_off + 3124u > size || v->extra_off < 0x30) return 0;
    if (seek_n > (1u << 22) || (uint64_t)seek_off + 8ull * seek_n > size) return 0;
    /* walk the frame chain once so that every later offset is known to be inside the file */
    p = 0x30;
    for (i = 0; i < v->nframes; i++) {
        uint32_t fs;
        if (p + 4 > size) return 0;
        fs = rd16(data + p);
        if (fs < 2 || p + 2 + fs > size) return 0;
        p += 2 + fs;
    }
    reset_state(v);
    return 1;
}

int vx_frames(const Vx *v) { return (int)v->nframes; }
int vx_width(const Vx *v) { return (int)v->width; }
int vx_height(const Vx *v) { return (int)v->height; }
float vx_fps(const Vx *v) { return (float)v->fps16 / 65536.0f; }
int vx_audio_rate(const Vx *v) { return (int)v->rate; }

void vx_rewind(Vx *v)
{
    if (v && v->data) reset_state(v);
}

int vx_next(Vx *v, uint32_t *rgba, int stride, int16_t *pcm, int pcm_max, int *npcm)
{
    size_t off, plen, apos;
    uint32_t fsz, na, a;
    const uint8_t *payload;
    long endbits;
    int total = 0;
    int W, H, x, y;
    const uint8_t *Y, *U, *V;

    if (npcm) *npcm = 0;
    if (!v || !v->data || v->failed || v->frame_no >= v->nframes) return 0;
    if (rgba && stride < (int)v->width) return 0;
    off = v->frame_off;
    if (off + 4 > v->size) goto fail;
    fsz = rd16(v->data + off);
    na = rd16(v->data + off + 2);
    if (fsz < 2 || off + 2 + fsz > v->size) goto fail;
    payload = v->data + off + 4;
    plen = fsz - 2;

    endbits = decode_video(v, payload, plen);
    if (endbits < 0) goto fail;
    apos = ((size_t)endbits + 15) / 16 * 2;
    if (apos > plen) goto fail;
    for (a = 0; a < na; a++) {
        int16_t blk[128];
        int mode;
        if (apos + 4 > plen) goto fail;
        mode = (int)((rd16(payload + apos + 2) >> 12) & 3);
        if (apos + AUDIO_FRAME_LEN[mode] > plen) goto fail;
        audio_frame(v, payload + apos, blk);
        apos += AUDIO_FRAME_LEN[mode];
        if (pcm) {
            if (total + 128 > pcm_max) goto fail;
            memcpy(pcm + total, blk, sizeof(blk));
        }
        total += 128;
    }
    if (npcm) *npcm = total;

    if (rgba) {
        W = (int)v->width;
        H = (int)v->height;
        Y = frame_plane(v, v->refs[3], 0);
        U = frame_plane(v, v->refs[3], 1);
        V = frame_plane(v, v->refs[3], 2);
        for (y = 0; y < H; y++) {
            uint32_t *o = rgba + (size_t)y * (size_t)stride;
            for (x = 0; x < W; x++) {
                int yy = Y[y * W + x];
                int u = U[(y >> 1) * (W >> 1) + (x >> 1)] - 128;
                int vv = V[(y >> 1) * (W >> 1) + (x >> 1)] - 128;
                int r = clamp8(yy + 2 * vv);
                int g = clamp8(yy - (u >> 1) - vv);
                int b = clamp8(yy + 2 * u);
                o[x] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
            }
        }
    }
    {   /* rotate the reference ring: the frame just decoded becomes refs[0] */
        int t = v->refs[3];
        v->refs[3] = v->refs[2];
        v->refs[2] = v->refs[1];
        v->refs[1] = v->refs[0];
        v->refs[0] = t;
    }
    v->frame_off = off + 2 + fsz;
    v->frame_no++;
    return 1;
fail:
    v->failed = 1;
    return 0;
}
