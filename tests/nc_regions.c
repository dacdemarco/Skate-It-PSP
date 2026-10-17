/* Near-camera clipper cost by region (analysis tool). usage: nc_regions <level.lvl>
 * Follow-camera views (rider on the floor, camera 4.2 m behind and 1.7 m up, random heading) in the bowl district vs the rest of the level:
 * triangles redrawn (the HUD's CLIP n) and host time of nc_gather per call (the PSP is ~100-200x slower on this random-access code). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "../src/nearclip.h"
#include "../src/coll.h"

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
} Hdr;
typedef struct { uint16_t tex, flags; uint32_t first, count, pad; } Batch;

static float rnd(float lo, float hi) { return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX); }

int main(int argc, char **argv)
{
    (void)argc;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) return 2;
    fclose(f);
    const Hdr *h = (const Hdr *)b;
    const Batch *batch = (const Batch *)(b + h->off_batch);
    uint32_t nt = h->nverts / 3;
    uint16_t *tex = malloc(sizeof(uint16_t) * nt); uint8_t *skip = calloc(nt, 1);
    for (uint32_t i = 0; i < h->nbatch; i++)
        for (uint32_t k = batch[i].first / 3; k < (batch[i].first + batch[i].count) / 3; k++) { tex[k] = batch[i].tex; skip[k] = (batch[i].flags & 1) != 0; }
    NcGrid g; Coll c;
    if (!nc_build(&g, (const NcVertex *)(b + h->off_verts), h->nverts, tex, skip, 16.0f)) return 1;
    if (!coll_build(&c, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f)) return 1;
    struct { const char *name; float x0, x1, z0, z1; } R[] = {
        { "bowl district (x -70..45, z -5..70)", -70, 45, -5, 70 }, { "library/north (z -125..-5)", -60, 40, -125, -5 }, { "plaza west (x -90..-70)", -90, -70, -120, 70 } };
    static NcTri out[2000];
    const float FOCAL = 136.0f / 0.63704518f;
    for (int r = 0; r < 3; r++) {
        srand(3);
        double tsum = 0, csum = 0; int n = 0, cmax = 0; double tmax = 0;
        for (int i = 0; i < 400; i++) {
            float x = rnd(R[r].x0, R[r].x1), z = rnd(R[r].z0, R[r].z1), y;
            if (!coll_ground(&c, x, z, 1e6f, &y)) continue;
            float hd = rnd(0, 6.2832f), eye[3] = { x - sinf(hd) * 4.2f, y + 1.7f, z - cosf(hd) * 4.2f }, ctr[3] = { x, y + 1.0f, z }, up[3] = { 0, 1, 0 }, view[16];
            nc_lookat(view, eye, ctr, up);
            clock_t t0 = clock(); int cnt = 0;
            for (int rep = 0; rep < 20; rep++) cnt = nc_gather(&g, view, eye, 16.0f, 0.6f, FOCAL, out, 700);
            double us = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / 20.0;
            tsum += us; csum += cnt; n++; if (cnt > cmax) cmax = cnt; if (us > tmax) tmax = us;
        }
        printf("%-38s CLIP avg %5.1f (max %3d)   host nc_gather avg %5.1f us (max %5.1f)  [%d views]\n", R[r].name, csum / n, cmax, tsum / n, tmax, n);
    }
    return 0;
}
