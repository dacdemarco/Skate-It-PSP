/* Wall probe (analysis tool, not a pass/fail test): can a rider ride THROUGH the walls that are drawn?
 * usage: wall_probe <level.lvl> [samples] > probe.tsv
 * Takes area-weighted points on the visible wall triangles (|n.y| < 0.3, not the backdrop), stands a rider 0.8 m in front of the point on
 * either side where the collision has a floor, and casts a horizontal segment at the height of the point towards it with the real
 * coll_wall(). Output: one TSV row per probed point: x y z texture side floor_y hit(0/1).  hit = 0 means the drawn wall is not solid. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/coll.h"
#include "../src/skate.h"

static Coll *g_coll;
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(g_coll, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(g_coll, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(g_coll, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
} Hdr;
typedef struct { uint16_t tex, flags; uint32_t first, count, pad; } Batch;
typedef struct { float u, v; uint32_t color; float x, y, z; } Vert;

static double rnd(void) { return (double)rand() / ((double)RAND_MAX + 1.0); }

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) return 2;
    fclose(f);
    int N = argc > 2 ? atoi(argv[2]) : 20000;
    const Hdr *h = (const Hdr *)b;
    const Vert *V = (const Vert *)(b + h->off_verts);
    const Batch *B = (const Batch *)(b + h->off_batch);
    Coll C;
    if (!coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f)) return 1;

    uint32_t nt = h->nverts / 3;
    float *cum = malloc(sizeof(float) * (nt + 1));
    unsigned short *tex = calloc(nt, sizeof(unsigned short));
    cum[0] = 0;
    for (uint32_t i = 0; i < h->nbatch; i++)
        for (uint32_t k = B[i].first / 3; k < (B[i].first + B[i].count) / 3 && k < nt; k++)
            tex[k] = (B[i].flags & 1) ? 0xFFFF : B[i].tex;          /* 0xFFFF = skip (backdrop) */
    for (uint32_t t = 0; t < nt; t++) {
        const Vert *a = &V[t * 3], *bq = &V[t * 3 + 1], *c = &V[t * 3 + 2];
        float e1[3] = { bq->x - a->x, bq->y - a->y, bq->z - a->z }, e2[3] = { c->x - a->x, c->y - a->y, c->z - a->z };
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        float w = (tex[t] != 0xFFFF && len > 1e-9f && fabsf(n[1]) / len < 0.3f) ? len * 0.5f : 0.0f;
        cum[t + 1] = cum[t] + w;
    }
    srand(7);
    g_coll = &C;
    printf("x\ty\tz\ttex\tside\tfloor_y\thit_at_point\thit_probes\tpassed\tdy\n");
    for (int s = 0; s < N; s++) {
        float r = (float)(rnd() * cum[nt]);
        uint32_t lo = 0, hi = nt;
        while (hi - lo > 1) { uint32_t m = (lo + hi) / 2; if (cum[m] <= r) lo = m; else hi = m; }
        uint32_t t = lo;
        const Vert *a = &V[t * 3], *bq = &V[t * 3 + 1], *c = &V[t * 3 + 2];
        float u = (float)rnd(), v = (float)rnd();
        if (u + v > 1) { u = 1 - u; v = 1 - v; }
        float p[3] = { a->x + (bq->x - a->x) * u + (c->x - a->x) * v, a->y + (bq->y - a->y) * u + (c->y - a->y) * v,
                       a->z + (bq->z - a->z) * u + (c->z - a->z) * v };
        float e1[3] = { bq->x - a->x, bq->y - a->y, bq->z - a->z }, e2[3] = { c->x - a->x, c->y - a->y, c->z - a->z };
        float nx = e1[1] * e2[2] - e1[2] * e2[1], nz = e1[0] * e2[1] - e1[1] * e2[0];
        float nl = sqrtf(nx * nx + nz * nz);
        if (nl < 1e-9f) continue;
        nx /= nl; nz /= nl;
        for (int side = -1; side <= 1; side += 2) {
            float q[3] = { p[0] + side * nx * 0.8f, 0, p[2] + side * nz * 0.8f }, gq;
            if (!coll_ground(&C, q[0], q[2], p[1] + 0.3f, &gq)) continue;
            if (p[1] - gq < -0.1f || p[1] - gq > 1.3f) continue;     /* the point must be within the rider's body height */
            float from[3] = { q[0], p[1], q[2] }, to[3] = { p[0] - side * nx * 0.2f, p[1], p[2] - side * nz * 0.2f }, hit[3], nn[3];
            int hitw = coll_wall(&C, from, to, hit, nn);
            /* what the physics does (src/skate.c move_horizontal): two probes, 0.3 m and 0.9 m above the ground, blocked if either hits */
            float flo[3] = { q[0], gq + 0.30f, q[2] }, tlo[3] = { to[0], gq + 0.30f, to[2] };
            float fhi[3] = { q[0], gq + 0.90f, q[2] }, thi[3] = { to[0], gq + 0.90f, to[2] };
            int hitp = coll_wall(&C, flo, tlo, hit, nn) || coll_wall(&C, fhi, thi, hit, nn);
            /* ride into the wall with the real physics: start at q, heading at the point, 5 m/s, no input, 1.2 s */
            Skate sk;
            float dx = -side * nx, dz = -side * nz;
            skate_init(&sk, q[0], gq, q[2], atan2f(dx, dz));
            sk.speed = 5.0f;
            SkateInput in = { 0, 0, 0, 0 };
            SkateWorld sw = { &C, g_ground, g_wall, g_surface, NULL };
            for (int i = 0; i < 72; i++) skate_update(&sk, &in, &sw, 1.0f / 60.0f);
            float beyond = (sk.pos[0] - p[0]) * dx + (sk.pos[2] - p[2]) * dz;     /* > 0: ended up past the drawn wall */
            int passed = beyond > 0.3f;
            printf("%.2f\t%.2f\t%.2f\t%u\t%d\t%.2f\t%d\t%d\t%d\t%.2f\n", p[0], p[1], p[2], (unsigned)tex[t], side, gq, hitw, hitp, passed, sk.pos[1] - gq);
        }
    }
    return 0;
}
