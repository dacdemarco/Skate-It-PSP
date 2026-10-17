/* debug: vertical surface rays at two nearly identical positions (grid vs brute force) - usage: dbg_ray2 level.lvl x y z [dx] */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/coll.h"

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 2; fclose(f);
    uint32_t *h = (uint32_t *)b;
    Coll C;
    if (!coll_build(&C, (const float *)(b + h[10]), h[5], (const CollPoly *)(b + h[11]), h[6], 8.0f)) return 1;
    float x = atof(argv[2]), y = atof(argv[3]), z = atof(argv[4]);
    for (int k = -6; k <= 6; k++) {
        float px = x + k * 0.0002f;
        float a[3] = { px, y + 0.14f, z }, bb[3] = { px, y - 1.0f, z }, hh[3], nn[3], h2[3], n2[3];
        int g = coll_ray(&C, a, bb, COLL_RIDE_NY, 2.0f, 0.06f, hh, nn), r = coll_ray_brute(&C, a, bb, COLL_RIDE_NY, 2.0f, 0.06f, h2, n2);
        int g0 = coll_ray(&C, a, bb, COLL_RIDE_NY, 2.0f, 0.0f, hh, nn);
        printf("x %.4f  grid %s  brute %s  (no tolerance: %s)\n", px, g ? "HIT" : "none", r ? "HIT" : "none", g0 ? "HIT" : "none");
    }
    return 0;
}
