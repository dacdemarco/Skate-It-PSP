/* Host test: grid collision == brute force on the real converted level; also timing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../src/coll.h"

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
} Hdr;

static float rnd(float lo, float hi) { return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX); }

int main(int argc, char **argv)
{
    (void)argc;
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 2; fclose(f);
    const Hdr *h = (const Hdr *)b;
    Coll c;
    if (!coll_build(&c, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f)) { printf("build failed\n"); return 1; }
    printf("polys %u, grid %dx%d, items %u\n", h->ncp, c.gw, c.gh, c.cell_start[(size_t)c.gw * c.gh]);
    srand(1234);
    int bad = 0, hits = 0, whits = 0;
    for (int i = 0; i < 40000; i++) {
        float x = rnd(h->bounds[0] - 5, h->bounds[3] + 5), z = rnd(h->bounds[2] - 5, h->bounds[5] + 5), ym = rnd(h->bounds[1] - 2, h->bounds[4] + 4);
        float y1 = 0, y2 = 0;
        int r1 = coll_ground(&c, x, z, ym, &y1), r2 = coll_ground_brute(&c, x, z, ym, &y2);
        if (r1 != r2 || (r1 && fabsf(y1 - y2) > 1e-4f)) { if (bad++ < 5) printf("ground mismatch at %.2f %.2f: %d %.3f vs %d %.3f\n", x, z, r1, y1, r2, y2); }
        hits += r1;
        uint32_t pid = 0xFFFFFFFFu; float yp = 0;
        int r3 = coll_ground_poly(&c, x, z, ym, &yp, &pid);          /* same surface as coll_ground, and the polygon it belongs to */
        if (r3 != r1 || (r3 && (fabsf(yp - y1) > 1e-6f || pid >= h->ncp))) { if (bad++ < 5) printf("ground_poly mismatch at %.2f %.2f: %d vs %d, poly %u\n", x, z, r3, r1, pid); }
    }
    for (int i = 0; i < 20000; i++) {
        float a[3] = { rnd(h->bounds[0], h->bounds[3]), rnd(h->bounds[1], h->bounds[4]), rnd(h->bounds[2], h->bounds[5]) };
        float d[3] = { rnd(-3, 3), rnd(-1, 1), rnd(-3, 3) };
        float bb[3] = { a[0] + d[0], a[1] + d[1], a[2] + d[2] };
        float h1[3], n1[3], h2[3], n2[3];
        int r1 = coll_wall(&c, a, bb, h1, n1), r2 = coll_wall_brute(&c, a, bb, h2, n2);
        if (r1 != r2 || (r1 && (fabsf(h1[0] - h2[0]) + fabsf(h1[1] - h2[1]) + fabsf(h1[2] - h2[2]) > 1e-3f))) { if (bad++ < 5) printf("wall mismatch: %d vs %d\n", r1, r2); }
        whits += r1;
    }
    int rhits = 0;
    for (int i = 0; i < 20000; i++) {                        /* rideable-surface rays, with and without the crack tolerance, as the physics casts them */
        float a[3] = { rnd(h->bounds[0], h->bounds[3]), rnd(h->bounds[1], h->bounds[4]), rnd(h->bounds[2], h->bounds[5]) };
        float d[3] = { rnd(-1, 1), rnd(-1, 1), rnd(-1, 1) };
        float bb[3] = { a[0] + d[0], a[1] + d[1], a[2] + d[2] };
        float h1[3], n1[3], h2[3], n2[3];
        float tol = (i & 1) ? 0.06f : 0.0f;
        int r1 = coll_ray(&c, a, bb, COLL_RIDE_NY, 2.0f, tol, h1, n1), r2 = coll_ray_brute(&c, a, bb, COLL_RIDE_NY, 2.0f, tol, h2, n2);
        if (r1 != r2 || (r1 && (fabsf(h1[0] - h2[0]) + fabsf(h1[1] - h2[1]) + fabsf(h1[2] - h2[2]) > 1e-3f))) { if (bad++ < 5) printf("ray mismatch (tol %.2f): %d vs %d\n", tol, r1, r2); }
        rhits += r1;
    }
    printf("ground hits %d / 40000, wall hits %d / 20000, ray hits %d / 20000, mismatches %d\n", hits, whits, rhits, bad);
    /* timing of the grid query */
    clock_t t0 = clock(); float y; volatile int sink = 0;
    for (int i = 0; i < 200000; i++) sink += coll_ground(&c, rnd(h->bounds[0], h->bounds[3]), rnd(h->bounds[2], h->bounds[5]), 100.0f, &y);
    printf("grid ground query: %.2f us\n", (double)(clock() - t0) / CLOCKS_PER_SEC / 200000 * 1e6);
    coll_free(&c);

    /* cracks between floor polygons must not be holes: two flat floors 0.05 m apart, and a flat floor next to a slope 0.05 m away */
    {
        static const float v[] = { 0, 0, 0,  0, 0, 4,  1, 0, 0,  1, 0, 4,          /* floor A, x in [0, 1] */
                                   1.05f, 0, 0,  1.05f, 0, 4,  2, 0.5f, 0,  2, 0.5f, 4,    /* floor B, x in [1.05, 2], rising */
                                   4, 0, 0,  4, 0, 4,  5, 0, 0 };                    /* far away: a lone triangle */
        static const CollPoly p[] = { { { 0, 1, 3, 2 }, 0 }, { { 4, 5, 7, 6 }, 0 }, { { 8, 9, 10, 0xFFFF }, 0 } };
        Coll k;
        float yy;
        if (!coll_build(&k, v, 11, p, 3, 8.0f)) { printf("crack test: build failed\n"); return 1; }
        int ok_crack = coll_ground(&k, 1.025f, 2.0f, 10.0f, &yy);                    /* in the middle of the 5 cm crack */
        int no_gap_far = !coll_ground(&k, 3.0f, 2.0f, 10.0f, &yy);                   /* 1 m of nothing between floor B and the far triangle stays a hole */
        printf("crack test: 5 cm crack closed: %s; real gaps still holes: %s\n", ok_crack ? "yes" : "NO", no_gap_far ? "yes" : "NO");
        if (!ok_crack || !no_gap_far) bad++;
        coll_free(&k);
    }
    printf(bad ? "FAILED\n" : "all collision tests passed\n");
    free(b);
    return bad != 0;
}
