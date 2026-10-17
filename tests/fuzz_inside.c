/* Host fuzz on the real level: riders approaching a region from many positions / headings / speeds with ollies, grinds and flips. Reports the places where a rider ends up
 * INSIDE a solid (grounded with a floor-like surface directly above it) or falls out of the level.
 * usage: fuzz_inside level.lvl x0 x1 z0 z1 [ymax]   -> prints clusters (1 m cells) of "inside" events with the scenario that led there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/coll.h"
#include "../src/skate.h"
#include "../src/rails.h"
#include "../src/trick.h"

static Coll C;
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&C, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&C, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&C, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }

/* is there a floor-like surface directly above (within 1.7 m)? = under a slab / inside a block */
static int covered(const float p[3], float *top)
{
    static const float off[4][2] = { { 0.12f, 0 }, { -0.12f, 0 }, { 0, 0.12f }, { 0, -0.12f } };
    for (int k = 0; k < 4; k++) {                                   /* all four offset rays must hit: a rider standing on the boundary edge of a block is not inside it */
        float a[3] = { p[0] + off[k][0], p[1] + 0.12f, p[2] + off[k][1] }, b[3] = { a[0], p[1] + 1.7f, a[2] }, h[3], n[3];
        if (!coll_ray(&C, a, b, COLL_FLOOR_NY, 2.0f, 0.0f, h, n)) return 0;
        *top = h[1];
    }
    return 1;
}

typedef struct { int cx, cz, n; float x, y, z; int scen; unsigned rail; int was_grind; float top; } Hit;
static Hit hits[4000]; static int nhits;
typedef struct { int cx, cz, n; float x, y, z; int scen, grind; unsigned rail; int hd, spd; float sx, sz; } Fall;      /* a run that fell out of the level, grouped by the 1 m cell of the last surface it touched */
static Fall falls[4000]; static int nfalls;

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(n); if (fread(buf, 1, n, f) != (size_t)n) return 2; fclose(f);
    uint32_t *h = (uint32_t *)buf;
    if (!coll_build(&C, (const float *)(buf + h[10]), h[5], (const CollPoly *)(buf + h[11]), h[6], 8.0f)) return 1;
    Rails rl;
    if (!rails_build(&rl, (const uint32_t *)(buf + h[23]), h[21], (const float *)(buf + h[23] + 8 * h[21]))) return 1;
    tricks_defaults();
    float x0 = atof(argv[2]), x1 = atof(argv[3]), z0 = atof(argv[4]), z1 = atof(argv[5]);
    float ymax = argc > 6 ? atof(argv[6]) : -63.0f;
    SkateWorld w = { NULL, g_ground, g_wall, g_surface, &rl };
    if (argc >= 12) {                                               /* trace: sx sz hd spd scen */
        float sx = atof(argv[7]), sz = atof(argv[8]); int hd = atoi(argv[9]), spd = atoi(argv[10]), scen = atoi(argv[11]);
        float sy;
        if (!coll_ground(&C, sx, sz, ymax, &sy)) { puts("no ground"); return 1; }
        Skate s; skate_init(&s, sx, sy, sz, hd * (6.2832f / 16.0f)); s.speed = 3.5f + 2.2f * spd;
        for (int i = 0; i < 60 * 4; i++) {
            float t = i / 60.0f;
            SkateInput in = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            if (scen >= 1) { float t0 = 0.2f + 0.45f * (float)((hd + spd) % 3); if (t > t0 && t < t0 + 0.30f) in.ollie_held = 1; }
            if (scen == 2 || scen == 3) in.grind = 1 + (hd % 5);
            if (scen == 3 && !s.grounded && !s.grinding && s.air_t > 0.1f && s.air_t < 0.14f) in.flip = 1 + (hd % 5);
            if (scen == 4) in.steer = ((i / 40) % 2) ? 1.0f : -1.0f;
            if (s.grinding) { in.steer = -(2.5f * s.lean + 0.8f * s.lean_v); if (in.steer > 1) in.steer = 1; if (in.steer < -1) in.steer = -1; }
            skate_update(&s, &in, &w, 1.0f / 60.0f);
            printf("%3d t=%.2f pos (%.4f %.4f %.4f) spd %.1f vel (%.1f %.1f %.1f) %s%s cn (%.2f %.2f %.2f)\n", i, t, s.pos[0], s.pos[1], s.pos[2], s.speed, s.vel[0], s.vel[1], s.vel[2], s.grounded ? "GROUND" : "AIR", (s.grinding ? " GRIND" : (s.wallriding ? " WALL" : "")), s.cn[0], s.cn[1], s.cn[2]);
            {
                float a[3] = { s.pos[0], s.pos[1] + 0.14f, s.pos[2] }, b[3] = { s.pos[0], s.pos[1] - 1.0f, s.pos[2] }, hh[3], nn[3], gy = -999.0f;
                int hs = coll_ray(&C, a, b, COLL_RIDE_NY, 2.0f, 0.06f, hh, nn);
                int hg = coll_ground(&C, s.pos[0], s.pos[2], s.pos[1] + 0.14f, &gy);
                printf("      down: surface %s y %.4f n (%.2f %.2f %.2f) ; ground %s y %.2f\n", hs ? "HIT" : "none", hs ? hh[1] : 0.0f, hs ? nn[0] : 0.0f, hs ? nn[1] : 0.0f, hs ? nn[2] : 0.0f, hg ? "yes" : "none", gy);
            }
            if (!s.grounded && !s.grinding && i < 70) {                        /* which walls are around? (probes of 0.4 m in the 4 horizontal directions at three heights) */
                static const float dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (int dd = 0; dd < 4; dd++)
                    for (int hk = 0; hk < 3; hk++) {
                        float off = hk == 0 ? 0.16f : (hk == 1 ? 0.45f : 0.9f);
                        float a[3] = { s.pos[0], s.pos[1] + off, s.pos[2] }, b[3] = { a[0] + dirs[dd][0] * 0.4f, a[1], a[2] + dirs[dd][1] * 0.4f }, hh[3], nn[3];
                        if (coll_wall(&C, a, b, hh, nn)) printf("      wall dir (%+.0f %+.0f) at +%.2f m height %.2f: hit at (%.3f %.3f %.3f) n (%.2f %.2f %.2f)\n", dirs[dd][0], dirs[dd][1], off, off, hh[0], hh[1], hh[2], nn[0], nn[1], nn[2]);
                    }
            }
            if (s.pos[1] < sy - 12.0f) break;
        }
        return 0;
    }
    long runs = 0, flagged = 0, fell = 0;
    float step = getenv("STEP") ? (float)atof(getenv("STEP")) : 1.5f;
    for (float sx = x0; sx <= x1; sx += step)
        for (float sz = z0; sz <= z1; sz += step) {
            float sy;
            if (!coll_ground(&C, sx, sz, ymax, &sy)) continue;
            for (int hd = 0; hd < 16; hd++)
                for (int spd = 0; spd < 3; spd++)
                    for (int scen = 0; scen < 5; scen++) {
                        Skate s; skate_init(&s, sx, sy, sz, hd * (6.2832f / 16.0f));
                        s.speed = 3.5f + 2.2f * spd;
                        int flagged_run = 0, prev_grind = 0; unsigned last_rail = 0; float lg[3] = { sx, sy, sz }; int lgr = 0;
                        runs++;
                        for (int i = 0; i < 60 * 4; i++) {
                            float t = i / 60.0f;
                            SkateInput in = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                            if (scen >= 1) {                             /* ollie at 0.2 s + 0.3 s * k: charge 0.3 s then release */
                                float t0 = 0.2f + 0.45f * (float)((hd + spd) % 3);
                                if (t > t0 && t < t0 + 0.30f) in.ollie_held = 1;
                            }
                            if (scen == 2 || scen == 3) in.grind = 1 + (hd % 5);
                            if (scen == 3 && !s.grounded && !s.grinding && s.air_t > 0.1f && s.air_t < 0.14f) in.flip = 1 + (hd % 5);
                            if (scen == 4) in.steer = ((i / 40) % 2) ? 1.0f : -1.0f;
                            if (s.grinding) { in.steer = -(2.5f * s.lean + 0.8f * s.lean_v); if (in.steer > 1) in.steer = 1; if (in.steer < -1) in.steer = -1; }
                            skate_update(&s, &in, &w, 1.0f / 60.0f);
                            if (s.grinding) { prev_grind = 1; last_rail = s.rail; }
                            if (s.grounded || s.grinding) { lg[0] = s.pos[0]; lg[1] = s.pos[1]; lg[2] = s.pos[2]; lgr = s.grinding; }
                            if (s.pos[1] < sy - 12.0f) {
                                fell++;
                                int cx = (int)floorf(lg[0]), cz = (int)floorf(lg[2]), k;
                                for (k = 0; k < nfalls; k++) if (falls[k].cx == cx && falls[k].cz == cz) break;
                                if (k == nfalls && nfalls < 4000) { falls[nfalls] = (Fall){ cx, cz, 0, lg[0], lg[1], lg[2], scen, lgr, last_rail, hd, spd, sx, sz }; nfalls++; }
                                if (k < nfalls) falls[k].n++;
                                break;
                            }
                            float top;
                            if (s.grounded && !s.grinding && covered(s.pos, &top) && !flagged_run) {
                                flagged_run = 1; flagged++;
                                int cx = (int)floorf(s.pos[0]), cz = (int)floorf(s.pos[2]), k;
                                for (k = 0; k < nhits; k++) if (hits[k].cx == cx && hits[k].cz == cz) break;
                                if (k == nhits && nhits < 4000) { hits[nhits] = (Hit){ cx, cz, 0, s.pos[0], s.pos[1], s.pos[2], scen, last_rail, prev_grind, top }; nhits++; }
                                if (k < nhits) hits[k].n++;
                            }
                        }
                    }
        }
    printf("%ld runs, %ld ended inside a solid, %ld fell out\n", runs, flagged, fell);
    for (int k = 0; k < nfalls; k++)
        printf("  FALL cell (%d, %d): %d runs, last on a surface at (%.1f %.2f %.1f) %s (rail %u); e.g. start (%.1f, %.1f) hd %d spd %d scen %d\n", falls[k].cx, falls[k].cz, falls[k].n, falls[k].x, falls[k].y, falls[k].z,
               falls[k].grind ? "grinding" : "rolling", falls[k].rail, falls[k].sx, falls[k].sz, falls[k].hd, falls[k].spd, falls[k].scen);
    for (int k = 0; k < nhits; k++)
        printf("  cell (%d, %d): %d runs, e.g. at (%.1f %.2f %.1f), surface %.2f above, scen %d, after a grind %d (rail %u)\n", hits[k].cx, hits[k].cz, hits[k].n, hits[k].x, hits[k].y, hits[k].z, hits[k].top - hits[k].y, hits[k].scen, hits[k].was_grind, hits[k].rail);
    return fell != 0 || flagged != 0;                                 /* a clean region: nobody falls out of the level or ends up inside a solid */
}
