/* Exit probe (analysis tool): where can a rider leave the world (fall into nothing)?
 * usage: exit_probe <level.lvl> [spacing_m] > exits.tsv
 * Starts a rider on the highest collision floor of every spacing x spacing cell, in 8 directions, 6 m/s, no input, 6 s with the real physics.
 * A start "exits" if at the end the rider is airborne and more than 8 m below where he started (he is falling with nothing under him).
 * Output TSV: start_x start_y start_z dir exit_x exit_y exit_z (the last grounded position). Summary on stderr. */
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
    float step = argc > 2 ? (float)atof(argv[2]) : 4.0f;
    const Hdr *h = (const Hdr *)b;
    if (!coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f)) return 1;
    SkateWorld w = { &C, g_ground, g_wall, g_surface, NULL };
    int starts = 0, exits = 0;
    printf("sx\tsy\tsz\tdir\tex\tey\tez\n");
    for (float x = h->bounds[0]; x <= h->bounds[3]; x += step)
        for (float z = h->bounds[2]; z <= h->bounds[5]; z += step) {
            float y;
            if (!coll_ground(&C, x, z, 1e6f, &y)) continue;
            starts++;
            for (int d = 0; d < 8; d++) {
                Skate s;
                skate_init(&s, x, y, z, d * 0.785398f);
                s.speed = 6.0f;
                SkateInput in = { 0, 0, 0, 0 };
                float lx = x, ly = y, lz = z;
                for (int i = 0; i < 360; i++) {
                    skate_update(&s, &in, &w, 1.0f / 60.0f);
                    if (s.grounded) { lx = s.pos[0]; ly = s.pos[1]; lz = s.pos[2]; }
                }
                if (!s.grounded && s.pos[1] < y - 8.0f) {
                    exits++;
                    printf("%.1f\t%.2f\t%.1f\t%d\t%.1f\t%.2f\t%.1f\n", x, y, z, d, lx, ly, lz);
                }
            }
        }
    fprintf(stderr, "%d start cells, %d of %d rides exit the world\n", starts, exits, starts * 8);
    return 0;
}
