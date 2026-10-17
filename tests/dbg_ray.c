/* temporary debug harness: coll_ray queries at a point. usage: dbg_ray <lvl> x y z   (casts straight down 1 m, and lists every triangle near the point) */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/coll.h"
typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
} Hdr;
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz); if (fread(b, 1, (size_t)sz, f) != (size_t)sz) return 2; fclose(f);
    const Hdr *h = (const Hdr *)b; Coll c;
    coll_build(&c, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f);
    float p[3] = { (float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4]) }, a[3] = { p[0], p[1] + 0.3f, p[2] }, e[3] = { p[0], p[1] - 1.0f, p[2] }, hit[3], n[3];
    for (int tolk = 0; tolk < 2; tolk++) {
        float tol = tolk ? 0.06f : 0.0f;
        int r = coll_ray(&c, a, e, COLL_RIDE_NY, 2.0f, tol, hit, n);
        printf("down ray tol %.2f: %s", tol, r ? "HIT" : "miss");
        if (r) printf(" at y %.3f normal (%.2f %.2f %.2f)", hit[1], n[0], n[1], n[2]);
        printf("\n");
    }
    if (argc > 7) {                                        /* optional contact normal: also cast the physics' snap ray (pos + cn*0.28 -> pos - cn*0.30) */
        float cn[3] = { (float)atof(argv[5]), (float)atof(argv[6]), (float)atof(argv[7]) };
        float a2[3] = { p[0] + cn[0] * 0.28f, p[1] + cn[1] * 0.28f, p[2] + cn[2] * 0.28f }, b2[3] = { p[0] - cn[0] * 0.30f, p[1] - cn[1] * 0.30f, p[2] - cn[2] * 0.30f };
        int r = coll_ray(&c, a2, b2, COLL_RIDE_NY, 2.0f, 0.06f, hit, n);
        printf("snap ray: %s", r ? "HIT" : "miss");
        if (r) printf(" at (%.3f %.3f %.3f) normal (%.2f %.2f %.2f)", hit[0], hit[1], hit[2], n[0], n[1], n[2]);
        printf("\n");
    }
    for (uint32_t i = 0; i < c.npolys; i++) {
        const CollPoly *q = &c.poly[i];
        int nv = q->v[3] == 0xFFFF ? 3 : 4;
        float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
        for (int k = 0; k < nv; k++) for (int d = 0; d < 3; d++) { float v = c.vert[q->v[k] * 3 + d]; if (v < lo[d]) lo[d] = v; if (v > hi[d]) hi[d] = v; }
        if (p[0] < lo[0] - 0.3f || p[0] > hi[0] + 0.3f || p[1] < lo[1] - 1.0f || p[1] > hi[1] + 1.0f || p[2] < lo[2] - 0.3f || p[2] > hi[2] + 0.3f) continue;
        printf("poly %d flags %u verts:", i, (unsigned)q->flags);
        for (int k = 0; k < nv; k++) printf(" (%.2f %.2f %.2f)", c.vert[q->v[k] * 3], c.vert[q->v[k] * 3 + 1], c.vert[q->v[k] * 3 + 2]);
        printf("\n");
    }
    return 0;
}
