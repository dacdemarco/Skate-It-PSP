/* Host test for src/vx.c (Actimagine VX movie decoder).
 *   test_vx <ealogo.vx> <tempintro.vx> <ref dir>
 * 1. every frame of both movies must match the reference CRCs written by tools/vx_mkref.py from the
 *    Python decoder (RGBA picture + PCM of the frame; the Python decoder itself was checked bit-exactly
 *    against the game's ARM code, see docs/VX_FORMAT.md);
 * 2. picture stride != width, NULL picture / NULL pcm, rewind;
 * 3. fuzz: random byte flips / truncations of ealogo.vx must give 0 from vx_open / vx_next, never a
 *    crash (run under ASan/UBSan) or a hang;
 * 4. speed: milliseconds per frame on the host (build with -O2 and no sanitizers for a meaningful number).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/vx.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static Vx vx;

static uint32_t crc_table[256];
static void crc_init(void)
{
    uint32_t i, k;
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}
static uint32_t crc32(uint32_t c, const void *data, size_t n)
{
    const uint8_t *p = data;
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static uint8_t *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { printf("read error %s\n", path); exit(2); }
    fclose(f);
    *size = (size_t)n;
    return b;
}

typedef struct { uint32_t pic, pcm; int n; } Ref;

static Ref *load_ref(const char *dir, const char *name, int *count)
{
    char path[512], line[256];
    FILE *f;
    Ref *r = NULL;
    int n = 0, cap = 0;
    snprintf(path, sizeof(path), "%s/vx_ref_%s.txt", dir, name);
    f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); exit(2); }
    while (fgets(line, sizeof(line), f)) {
        unsigned idx, a, b;
        int cnt;
        if (line[0] == '#') continue;
        if (sscanf(line, "%u %x %x %d", &idx, &a, &b, &cnt) != 4) continue;
        if (n == cap) { cap = cap ? cap * 2 : 512; r = realloc(r, (size_t)cap * sizeof(Ref)); }
        r[n].pic = a; r[n].pcm = b; r[n].n = cnt;
        n++;
    }
    fclose(f);
    *count = n;
    return r;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static uint32_t pic[256 * 192], pic2[272 * 192];
static int16_t pcm[2048];

/* decode a whole movie against its reference; stride is the picture stride in pixels */
static void check_movie(const char *name, const uint8_t *data, size_t size, const Ref *ref, int nref, int stride, int maxframes)
{
    int i = 0, n, W, H, y;
    uint32_t *buf = stride == 256 ? pic : pic2;
    CHECK(vx_open(&vx, data, size), "%s: vx_open failed", name);
    if (fails) return;
    W = vx_width(&vx);
    H = vx_height(&vx);
    CHECK(vx_frames(&vx) == nref, "%s: frames %d vs ref %d", name, vx_frames(&vx), nref);
    CHECK(W == 256 && H == 192 && vx_audio_rate(&vx) == 22050, "%s: bad header values", name);
    CHECK(vx_fps(&vx) > 29.96f && vx_fps(&vx) < 29.98f, "%s: fps %f", name, vx_fps(&vx));
    memset(buf, 0xAB, sizeof(uint32_t) * (size_t)stride * (size_t)H);
    while (i < nref && i < maxframes && vx_next(&vx, buf, stride, pcm, 2048, &n)) {
        uint32_t c = 0;
        for (y = 0; y < H; y++) c = crc32(c, buf + (size_t)y * (size_t)stride, (size_t)W * 4);
        if (c != ref[i].pic) { CHECK(0, "%s frame %d: picture crc %08x vs ref %08x", name, i, c, ref[i].pic); if (fails > 5) return; }
        if (n != ref[i].n || crc32(0, pcm, (size_t)n * 2) != ref[i].pcm) {
            CHECK(0, "%s frame %d: audio mismatch (n=%d ref %d)", name, i, n, ref[i].n);
            if (fails > 5) return;
        }
        i++;
    }
    if (maxframes >= nref) {
        CHECK(i == nref, "%s: decoded only %d of %d frames", name, i, nref);
        CHECK(vx_next(&vx, buf, stride, pcm, 2048, &n) == 0, "%s: vx_next after the last frame must return 0", name);
    }
    if (!fails) printf("  %s: %d frames match the Python reference (stride %d)\n", name, i, stride);
}

static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

/* decode everything, count calls; must terminate */
static int decode_all(const uint8_t *buf, size_t size)
{
    int calls = 0, n;
    if (!vx_open(&vx, buf, size)) return -1;
    while (calls < vx_frames(&vx) + 2 && vx_next(&vx, pic, 256, pcm, 2048, &n)) calls++;
    return calls;
}

int main(int argc, char **argv)
{
    size_t s1, s2;
    uint8_t *d1, *d2, *tmp;
    Ref *r1, *r2;
    int n1, n2, t, ok, bad_open = 0, full = 0, partial = 0, nn;
    double t0, t1;
    int ntrial = getenv("VX_FUZZ_N") ? atoi(getenv("VX_FUZZ_N")) : 400;
    if (getenv("VX_FUZZ_SEED")) rng_state = (uint32_t)atoi(getenv("VX_FUZZ_SEED")) | 1u;

    if (argc < 4) { printf("usage: test_vx <ealogo.vx> <tempintro.vx> <ref dir>\n"); return 2; }
    crc_init();
    d1 = load(argv[1], &s1);
    d2 = load(argv[2], &s2);
    r1 = load_ref(argv[3], "ealogo", &n1);
    r2 = load_ref(argv[3], "tempintro", &n2);
    printf("vx: references %d + %d frames\n", n1, n2);

    /* 1. all frames of both movies */
    check_movie("ealogo", d1, s1, r1, n1, 256, 1 << 30);
    check_movie("tempintro", d2, s2, r2, n2, 256, 1 << 30);
    /* 2. odd stride, rewind, NULL outputs */
    check_movie("ealogo(stride 272)", d1, s1, r1, n1, 272, 1 << 30);
    vx_rewind(&vx);
    CHECK(vx_next(&vx, NULL, 0, NULL, 0, &nn) == 1 && nn == r1[0].n, "NULL outputs, frame 0");
    vx_rewind(&vx);
    check_movie("ealogo(rewound)", d1, s1, r1, n1, 256, 40);
    {   /* decode only, NULL picture, the state must still advance identically: compare the last frame */
        int i;
        CHECK(vx_open(&vx, d1, s1), "open");
        for (i = 0; i < n1 - 1; i++) vx_next(&vx, NULL, 0, NULL, 0, NULL);
        CHECK(vx_next(&vx, pic, 256, pcm, 2048, &nn) == 1, "last frame after NULL-output decoding");
        {
            uint32_t c = crc32(0, pic, sizeof(pic));
            CHECK(c == r1[n1 - 1].pic, "last frame crc after NULL-output decoding");
            CHECK(crc32(0, pcm, (size_t)nn * 2) == r1[n1 - 1].pcm, "last frame audio after NULL-output decoding");
        }
        CHECK(vx_open(&vx, d1, s1), "open");
        CHECK(vx_next(&vx, pic, 256, pcm, 100, &nn) == 0, "pcm buffer too small must fail");
        CHECK(vx_next(&vx, pic, 256, pcm, 2048, &nn) == 0, "decoder stays failed");
        CHECK(vx_open(&vx, d1, s1) && vx_next(&vx, pic, 100, pcm, 2048, &nn) == 0, "stride < width must fail");
    }
    if (fails) { printf("vx: FAILED (%d)\n", fails); return 1; }

    /* 3. fuzz (each trial gets an exact-size heap copy so ASan sees any overread) */
    printf("  fuzz: rng seed state %u, %d trials\n", rng_state, ntrial);
    for (t = 0; t < ntrial; t++) {
        size_t sz = s1;
        int nflip = 1 + (int)(rnd() % 8), k, r;
        tmp = malloc(sz);
        memcpy(tmp, d1, sz);
        for (k = 0; k < nflip; k++) {
            size_t pos;
            if (t % 4 == 0) pos = rnd() % 0x40;                 /* header */
            else if (t % 4 == 1) pos = 0x30 + rnd() % 3000;     /* first frames (keyframe!) */
            else pos = rnd() % sz;
            tmp[pos] ^= (uint8_t)(1u << (rnd() % 8));
            if (rnd() % 3 == 0) tmp[pos] = (uint8_t)rnd();
        }
        r = decode_all(tmp, sz);
        if (r < 0) bad_open++; else if (r >= (int)n1) full++; else partial++;
        free(tmp);
    }
    for (t = 0; t < ntrial / 3; t++) {                           /* truncations */
        size_t sz = rnd() % (s1 + 1);
        tmp = malloc(sz ? sz : 1);
        memcpy(tmp, d1, sz);
        (void)decode_all(tmp, sz);
        free(tmp);
    }
    for (t = 0; t < ntrial / 4; t++) {                           /* the other movie: garbage in the middle */
        size_t sz = s2;
        size_t pos = 0x30 + rnd() % (sz - 0x30);
        int k;
        tmp = malloc(sz);
        memcpy(tmp, d2, sz);
        for (k = 0; k < 16 && pos + (size_t)k < sz; k++) tmp[pos + (size_t)k] = (uint8_t)rnd();
        {
            int calls = 0, n;
            if (vx_open(&vx, tmp, sz))
                while (calls < vx_frames(&vx) + 2 && vx_next(&vx, pic, 256, pcm, 2048, &n)) calls++;
        }
        free(tmp);
    }
    printf("  fuzz: %d byte-flip trials (%d rejected by vx_open, %d decoded to the end, %d stopped early), "
           "%d truncations, %d garbage-burst trials: no crash, no hang\n", ntrial, bad_open, full, partial, ntrial / 3, ntrial / 4);
    ok = 1;
    { uint8_t z[64] = { 0 }; if (vx_open(&vx, z, sizeof(z))) ok = 0; if (vx_open(&vx, z, 3)) ok = 0; if (vx_open(&vx, NULL, 0)) ok = 0; }
    CHECK(ok, "garbage must not open");

    /* 4. speed */
    if (!getenv("VX_NOSPEED")) {
        int n, f = 0;
        double total;
        vx_open(&vx, d2, s2);
        t0 = now_ms();
        while (vx_next(&vx, pic, 256, pcm, 2048, &n)) f++;
        t1 = now_ms();
        total = t1 - t0;
        printf("  speed tempintro: %d frames in %.1f ms = %.3f ms/frame (video + audio + RGBA)\n", f, total, total / f);
        vx_open(&vx, d2, s2);
        f = 0;
        t0 = now_ms();
        while (vx_next(&vx, NULL, 0, pcm, 2048, &n)) f++;
        t1 = now_ms();
        printf("  speed tempintro without RGBA conversion: %.3f ms/frame\n", (t1 - t0) / f);
        vx_open(&vx, d1, s1);
        f = 0;
        t0 = now_ms();
        while (vx_next(&vx, pic, 256, pcm, 2048, &n)) f++;
        t1 = now_ms();
        printf("  speed ealogo: %d frames, %.3f ms/frame\n", f, (t1 - t0) / f);
    }
    free(d1); free(d2); free(r1); free(r2);
    if (fails) { printf("vx: FAILED (%d)\n", fails); return 1; }
    printf("vx: all OK\n");
    return 0;
}
