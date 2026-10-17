/* Physics cost benchmark (analysis tool): how many collision queries / triangles does one rendered frame of skating cost, per place?
 * usage: bench_phys <level.lvl> [cell_m]      (build with -DCOLL_STATS)
 * The game runs skate_update with substeps of at most 1/90 s (1 or 2 per 60 Hz frame, more when the frame takes longer). The PSP is ~100-200x slower than
 * the host on random-access code, so TRIANGLES EXAMINED per frame is the number to watch (the measured PSP physics time was 16.5 ms/frame in the bowl). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "../src/coll.h"
#include "../src/skate.h"

extern unsigned long coll_stat_tris, coll_stat_queries;

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
} Hdr;

static Coll C;
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&C, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&C, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&C, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) return 2;
    fclose(f);
    const Hdr *h = (const Hdr *)b;
    if (!coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, argc > 2 ? (float)atof(argv[2]) : 4.0f)) return 1;
    SkateWorld w = { &C, g_ground, g_wall, g_surface, NULL };
    struct { const char *name; float x, z, hd, speed; } R[] = {
        { "flat plaza, riding", -28.0f, 24.0f, 0.0f, 5.0f },
        { "stairs, going down (9.4, 16)", 9.4f, 16.0f, 0.0f, 5.0f },
        { "stairs, going down (-14, 8)", -14.0f, 7.0f, 0.0f, 5.0f },
        { "SW pit, up the side (-60.6, 62)", -60.6343f, 62.0162f, 0.0f, 5.0f },
        { "SW pit floor, circling", -60.0f, 54.0f, 0.0f, 5.0f },
    };
    for (int r = 0; r < 5; r++) {
        float y;
        if (!coll_ground(&C, R[r].x, R[r].z, 1e6f, &y)) { printf("%s: no ground\n", R[r].name); continue; }
        Skate s; skate_init(&s, R[r].x, y, R[r].z, R[r].hd); s.speed = R[r].speed;
        SkateInput in = { r == 4 ? 0.6f : 0.0f, 0, 0, 0 };
        coll_stat_tris = coll_stat_queries = 0;
        clock_t t0 = clock();
        int frames = 120;
        for (int fr = 0; fr < frames; fr++) { skate_update(&s, &in, &(SkateWorld){ &C, g_ground, g_wall, g_surface, NULL }, 1.0f / 90.0f); skate_update(&s, &in, &w, 1.0f / 90.0f); }
        double us = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / frames;
        printf("%-34s per 60 Hz frame (2 substeps): %5.1f queries, %7.0f triangles examined, host %.1f us\n", R[r].name,
               (double)coll_stat_queries / frames, (double)coll_stat_tris / frames, us);
    }
    return 0;
}
