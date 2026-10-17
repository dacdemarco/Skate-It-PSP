/* Host test for src/nearclip.c on the real level: same set as an independent brute-force model, guard-band safety, no lost screen area. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../src/nearclip.h"

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
} Hdr;
typedef struct { uint16_t tex, flags; uint32_t first, count, pad; } Batch;

#define FOCAL (136.0 / 0.6370451769779303)        /* 136 / tan(32.5 deg) */
#ifndef TEST_RADIUS
#define TEST_RADIUS 1000.0f
#endif
#define NEAR  0.6
static float rnd(float lo, float hi) { return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX); }
static int fails;
static double gather_time;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 10) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* independent reference in double precision: is this triangle rejected by the GE model AND does it reach the clip frustum? */
typedef struct { double x, y, d; } P3;

static int clip_ref(const P3 *in, int n, P3 *out, double a, double b, double dd, double c)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const P3 *p = &in[i], *q = &in[(i + 1) % n];
        double fp = a * p->x + b * p->y + dd * p->d + c, fq = a * q->x + b * q->y + dd * q->d + c;
        if (fp >= 0) out[m++] = *p;
        if ((fp >= 0) != (fq >= 0)) {
            double t = fp / (fp - fq);
            out[m].x = p->x + (q->x - p->x) * t; out[m].y = p->y + (q->y - p->y) * t; out[m].d = p->d + (q->d - p->d) * t; m++;
        }
    }
    return m;
}

static int ref_rejected(const NcVertex *t, const double eye[3], const double r[3], const double u[3], const double f[3])
{
    P3 poly[3], a[12], b[12];
    for (int k = 0; k < 3; k++) {
        double p[3] = { t[k].x - eye[0], t[k].y - eye[1], t[k].z - eye[2] };
        poly[k].x = p[0] * r[0] + p[1] * r[1] + p[2] * r[2];
        poly[k].y = p[0] * u[0] + p[1] * u[1] + p[2] * u[2];
        poly[k].d = p[0] * f[0] + p[1] * f[1] + p[2] * f[2];
    }
    int n = clip_ref(poly, 3, a, 0, 0, 1, -NEAR);
    if (n < 3) return 0;
    int rej = 0;
    for (int k = 0; k < n; k++)
        if (fabs(a[k].x / a[k].d) * FOCAL > 1900.0 || fabs(a[k].y / a[k].d) * FOCAL > 1900.0) rej = 1;
    if (!rej) return 0;
    n = clip_ref(a, n, b, -1, 0, 3, 0); if (n < 3) return 0;
    n = clip_ref(b, n, a, 1, 0, 3, 0);  if (n < 3) return 0;
    n = clip_ref(a, n, b, 0, -1, 3, 0); if (n < 3) return 0;
    n = clip_ref(b, n, a, 0, 1, 3, 0);  if (n < 3) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 2; fclose(f);
    const Hdr *h = (const Hdr *)b;
    const NcVertex *verts = (const NcVertex *)(b + h->off_verts);
    const Batch *batch = (const Batch *)(b + h->off_batch);
    uint32_t nt = h->nverts / 3;
    uint16_t *tex = malloc(sizeof(uint16_t) * nt); uint8_t *skip = calloc(nt, 1);
    for (uint32_t i = 0; i < h->nbatch; i++)
        for (uint32_t k = batch[i].first / 3; k < (batch[i].first + batch[i].count) / 3; k++) { tex[k] = batch[i].tex; skip[k] = (batch[i].flags & 1) != 0; }
    NcGrid g;
    if (!nc_build(&g, verts, h->nverts, tex, skip, 16.0f)) { printf("build failed\n"); return 1; }
    printf("triangles %u, grid %dx%d, items %u\n", nt, g.gw, g.gh, g.cell_start[(size_t)g.gw * g.gh]);

    srand(77);
    static NcTri out[20000];
    int cams = 0, total_rej = 0, total_out = 0, cams_with_rej = 0;
    for (int c = 0; c < 300; c++) {
        float eye[3], center[3], up[3] = { 0, 1, 0 };
        if (c == 0) { eye[0] = -28; eye[1] = -63.42f; eye[2] = 19.74f; center[0] = -28; center[1] = -64.12f; center[2] = 23.94f; }
        else {
            eye[0] = rnd(-80, 30); eye[1] = -65.1f + rnd(0.8f, 3.0f); eye[2] = rnd(-20, 70);
            float yaw = rnd(0, 6.28f), pitch = rnd(-0.5f, 0.15f);
            center[0] = eye[0] + sinf(yaw) * cosf(pitch) * 4; center[1] = eye[1] + sinf(pitch) * 4; center[2] = eye[2] + cosf(yaw) * cosf(pitch) * 4;
        }
        float view[16]; nc_lookat(view, eye, center, up);
        int cnt = nc_gather(&g, view, eye, 1000.0f, (float)NEAR, (float)FOCAL, out, 20000);
        { clock_t t0 = clock(); static NcTri tmp2[2000];
          for (int rep = 0; rep < 20; rep++) (void)nc_gather(&g, view, eye, 20.0f, (float)NEAR, (float)FOCAL, tmp2, 2000);
          gather_time += (double)(clock() - t0) / CLOCKS_PER_SEC / 20.0; }
        /* reference basis (double) */
        double e[3] = { eye[0], eye[1], eye[2] }, fw[3] = { center[0] - eye[0], center[1] - eye[1], center[2] - eye[2] };
        double fl = sqrt(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]); for (int k = 0; k < 3; k++) fw[k] /= fl;
        double r[3] = { fw[1] * 0 - fw[2] * 1, fw[2] * 0 - fw[0] * 0, fw[0] * 1 - fw[1] * 0 };           /* f x up */
        double rl = sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); for (int k = 0; k < 3; k++) r[k] /= rl;
        double u[3] = { r[1] * fw[2] - r[2] * fw[1], r[2] * fw[0] - r[0] * fw[2], r[0] * fw[1] - r[1] * fw[0] };
        /* set comparison */
        static uint8_t got[200000]; memset(got, 0, nt);
        for (int i = 0; i < cnt; i++) got[out[i].src] = 1;
        int rej = 0, mism = 0;
        for (uint32_t t = 0; t < nt; t++) {
            if (skip[t]) continue;
            int r1 = ref_rejected(verts + t * 3, e, r, u, fw);
            rej += r1;
            if (r1 != got[t]) { mism++; }
        }
        CHECK(mism <= rej / 50 + 1, "cam %d: %d mismatches of %d rejected (float vs double edge cases allowed up to 2%%)", c, mism, rej);
        total_rej += rej; total_out += cnt; cams++; cams_with_rej += rej > 0;
        /* guard-band safety of every output vertex */
        for (int i = 0; i < cnt; i++)
            for (int k = 0; k < 3; k++) {
                double d = -out[i].v[k].z;
                CHECK(d >= NEAR - 1e-3, "cam %d: output behind near plane (%g)", c, d);
                CHECK(fabs(out[i].v[k].x / d) * FOCAL <= 3.0 * FOCAL + 1.0 && fabs(out[i].v[k].y / d) * FOCAL <= 3.0 * FOCAL + 1.0, "cam %d: output outside the guard band", c);
            }
        /* no lost screen area: sample points on rejected triangles that are on screen must be inside some output triangle
         * (output computed with TEST_RADIUS, the radius the game uses) */
        static NcTri outR[20000];
        int cntR = nc_gather(&g, view, eye, TEST_RADIUS, (float)NEAR, (float)FOCAL, outR, 20000);
        for (uint32_t t = 0; t < nt && c < 60; t++) {
            if (skip[t] || !got[t]) continue;
            for (int s = 0; s < 6; s++) {
                float a = rnd(0, 1), bb = rnd(0, 1); if (a + bb > 1) { a = 1 - a; bb = 1 - bb; }
                const NcVertex *v = verts + t * 3;
                double p[3] = { v[0].x + (v[1].x - v[0].x) * a + (v[2].x - v[0].x) * bb, v[0].y + (v[1].y - v[0].y) * a + (v[2].y - v[0].y) * bb,
                                v[0].z + (v[1].z - v[0].z) * a + (v[2].z - v[0].z) * bb };
                double q[3] = { p[0] - e[0], p[1] - e[1], p[2] - e[2] };
                double X = q[0] * r[0] + q[1] * r[1] + q[2] * r[2], Y = q[0] * u[0] + q[1] * u[1] + q[2] * u[2], D = q[0] * fw[0] + q[1] * fw[1] + q[2] * fw[2];
                if (D < NEAR + 1e-3 || fabs(X / D) * FOCAL > 235.0 || fabs(Y / D) * FOCAL > 131.0) continue;
                int inside = 0;
                for (int i = 0; i < cntR && !inside; i++) {
                    if (outR[i].src != t) continue;
                    const NcVertex *w = outR[i].v;
                    double A[3] = { w[0].x, w[0].y, -w[0].z }, B[3] = { w[1].x, w[1].y, -w[1].z }, C[3] = { w[2].x, w[2].y, -w[2].z };
                    double P[3] = { X, Y, D };
                    double v0[3], v1[3], v2[3];
                    for (int k = 0; k < 3; k++) { v0[k] = B[k] - A[k]; v1[k] = C[k] - A[k]; v2[k] = P[k] - A[k]; }
                    double d00 = v0[0]*v0[0]+v0[1]*v0[1]+v0[2]*v0[2], d01 = v0[0]*v1[0]+v0[1]*v1[1]+v0[2]*v1[2], d11 = v1[0]*v1[0]+v1[1]*v1[1]+v1[2]*v1[2];
                    double d20 = v2[0]*v0[0]+v2[1]*v0[1]+v2[2]*v0[2], d21 = v2[0]*v1[0]+v2[1]*v1[1]+v2[2]*v1[2];
                    double den = d00 * d11 - d01 * d01; if (fabs(den) < 1e-12) continue;
                    double l1 = (d11 * d20 - d01 * d21) / den, l2 = (d00 * d21 - d01 * d20) / den;
                    if (l1 >= -1e-3 && l2 >= -1e-3 && l1 + l2 <= 1.001) inside = 1;
                }
                CHECK(inside, "cam %d tri %u: visible point not covered by the clipped output", c, t);
            }
        }
    }
    printf("%d cameras, %d with rejected triangles, avg rejected %.0f, avg drawn-again triangles %.0f\n", cams, cams_with_rej, (double)total_rej / cams, (double)total_out / cams);
    printf("nc_gather (radius 20 m) on the host: %.1f us per call on average\n", gather_time / cams * 1e6);
    printf(fails ? "%d FAILURES\n" : "all nearclip tests passed\n", fails);
    nc_free(&g); free(tex); free(skip); free(b);
    return fails != 0;
}
