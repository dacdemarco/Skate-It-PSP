/* Collision grid statistics (analysis tool): how many triangles does a floor query scan, per region and grid cell size?
 * usage: coll_stats <level.lvl>
 * The PSP is ~100-200x slower than the host on random-access code, so the ITEMS scanned per query is the number to watch. */
#include <stdio.h>
#include <stdlib.h>
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
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) return 2;
    fclose(f);
    const Hdr *h = (const Hdr *)b;
    struct { const char *name; float x0, x1, z0, z1; } R[] = {
        { "bowl district", -70, 45, -5, 70 }, { "library/north", -60, 40, -125, -5 }, { "whole level", h->bounds[0], h->bounds[3], h->bounds[2], h->bounds[5] } };
    float cells[] = { 8, 4, 3, 2, 1.5f };
    printf("polys %u, verts %u\n", h->ncp, h->ncv);
    for (int ci = 0; ci < 5; ci++) {
        Coll c;
        clock_t t0 = clock();
        if (!coll_build(&c, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, cells[ci])) return 1;
        double build_ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        size_t ncell = (size_t)c.gw * c.gh;
        printf("cell %.1f m: grid %dx%d, items %u (%.1f per tri), build %.0f ms, memory %.0f KB\n", cells[ci], c.gw, c.gh, c.cell_start[ncell],
               (double)c.cell_start[ncell] / (h->ncp * 2.0), build_ms, (c.cell_start[ncell] * 4.0 + ncell * 4.0) / 1024.0);
        for (int r = 0; r < 3; r++) {
            srand(5);
            double sum = 0; int n = 0, mx = 0; clock_t t1 = clock();
            for (int i = 0; i < 20000; i++) {
                float x = rnd(R[r].x0, R[r].x1), z = rnd(R[r].z0, R[r].z1), y;
                if (!coll_ground(&c, x, z, 1e6f, &y)) continue;
                int ii = (int)floorf((x - c.minx) / c.cell), jj = (int)floorf((z - c.minz) / c.cell);
                int cnt = (int)(c.cell_start[(size_t)jj * c.gw + ii + 1] - c.cell_start[(size_t)jj * c.gw + ii]);
                sum += cnt; n++; if (cnt > mx) mx = cnt;
                (void)y;
            }
            double us = (double)(clock() - t1) * 1e6 / CLOCKS_PER_SEC / 20000.0;
            printf("    %-14s floor queries scan avg %6.1f items (max %4d)   host %.2f us/query\n", R[r].name, n ? sum / n : 0, mx, us);
        }
        coll_free(&c);
    }
    return 0;
}
