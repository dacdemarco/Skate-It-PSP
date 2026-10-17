/* Ride probe (analysis tool): compare how rides behave on different collision versions of a level.
 * usage: ride_probe <level.lvl> [spacing_m] [speed] > rides.tsv
 * From the highest collision floor of every spacing x spacing cell, rides in 8 directions at `speed` (default 5 m/s), no input, 3 s, real physics.
 * Output TSV: sx sz dir dist fell speed   (dist = horizontal distance travelled, fell = airborne >8 m below the start at the end).
 * Compare two versions with tools/ride_compare.py. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/coll.h"
#include "../src/skate.h"

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
    float step = argc > 2 ? (float)atof(argv[2]) : 2.0f;
    float speed = argc > 3 ? (float)atof(argv[3]) : 5.0f;
    const Hdr *h = (const Hdr *)b;
    if (!coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f)) return 1;
    SkateWorld w = { &C, g_ground, g_wall, g_surface, NULL };
    printf("sx\tsz\tdir\tdist\tfell\tspeed\n");
    for (float x = h->bounds[0]; x <= h->bounds[3]; x += step)
        for (float z = h->bounds[2]; z <= h->bounds[5]; z += step) {
            float y;
            if (!coll_ground(&C, x, z, 1e6f, &y)) continue;
            for (int d = 0; d < 8; d++) {
                Skate s;
                skate_init(&s, x, y, z, d * 0.785398f);
                s.speed = speed;
                SkateInput in = { 0, 0, 0, 0 };
                for (int i = 0; i < 180; i++) skate_update(&s, &in, &w, 1.0f / 60.0f);
                float dist = sqrtf((s.pos[0] - x) * (s.pos[0] - x) + (s.pos[2] - z) * (s.pos[2] - z));
                int fell = !s.grounded && s.pos[1] < y - 8.0f;
                printf("%.4f\t%.4f\t%d\t%.2f\t%d\t%.2f\n", x, z, d, dist, fell, s.speed);
            }
        }
    return 0;
}
