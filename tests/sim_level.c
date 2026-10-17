/* Host simulation: scripted skating session on the real converted level with the same physics/collision code as the PSP. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    (void)argc;
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 2; fclose(f);
    const Hdr *h = (const Hdr *)b;
    if (!coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f)) return 1;

    /* spawn exactly as src/main.c find_spawn() */
    float sx = h->start[0], sz = h->start[2], sy = h->bounds[1]; int found = 0;
    for (int ring = 0; ring < 40 && !found; ring++)
        for (int k = 0; k < 8 && !found; k++) {
            float a = (float)k * 0.785398f, r = (float)ring * 2.0f, tx = sx + cosf(a) * r, tz = sz + sinf(a) * r;
            if (coll_ground(&C, tx, tz, h->bounds[4] + 5.0f, &sy)) { sx = tx; sz = tz; found = 1; }
        }
    printf("spawn (%.1f, %.2f, %.1f) found=%d ; bounds y %.1f..%.1f\n", sx, sy, sz, found, h->bounds[1], h->bounds[4]);

    SkateWorld w = { NULL, g_ground, g_wall, g_surface, NULL };
    int bad = 0;
    for (int run = 0; run < 3; run++) {
        Skate s; skate_init(&s, sx, sy, sz, run * 2.1f);
        float t = 0; int grounded_frames = 0, frames = 0, airborne_events = 0, was = 1, wall_slows = 0;
        float miny = 1e9f, maxspeed = 0;
        printf("-- run %d (start heading %.1f rad)\n", run, run * 2.1f);
        for (int i = 0; i < 60 * 40; i++, t += 1.0f / 60.0f) {
            SkateInput in = { 0, 0, 0, 0, 0 , 0, 0, 0, 0, 0};
            /* script: push every 1.2 s for 6 s; steer left 7-9 s, right 12-14 s; ollie at 16 s; brake at 30 s */
            if (i % 72 == 1 && t < 6.0f) in.push = 1;
            if (t > 7 && t < 9) in.steer = 1.0f;
            if (t > 12 && t < 14) in.steer = -1.0f;
            if (t > 15.65f && t < 16.0f) in.ollie_held = 1;
            if (t > 30) in.brake = 1;
            float prev_speed = s.speed;
            skate_update(&s, &in, &w, 1.0f / 60.0f);
            frames++; grounded_frames += s.grounded;
            if (was && !s.grounded) airborne_events++;
            was = s.grounded;
            if (s.speed < prev_speed - 1.0f && s.grounded && !in.brake) wall_slows++;
            if (s.pos[1] < miny) miny = s.pos[1];
            if (s.speed > maxspeed) maxspeed = s.speed;
            if (!isfinite(s.pos[0]) || !isfinite(s.pos[1]) || !isfinite(s.pos[2]) || !isfinite(s.speed)) { printf("NaN at t=%.2f\n", t); bad++; break; }
            if (s.pos[1] < h->bounds[1] - 30.0f) { printf("fell out of the world at t=%.2f pos (%.1f %.1f %.1f)\n", t, s.pos[0], s.pos[1], s.pos[2]); bad++; break; }
            if (i % 300 == 0)
                printf("t=%5.1f pos (%7.2f %6.2f %7.2f) speed %5.2f heading %5.2f %s\n", t, s.pos[0], s.pos[1], s.pos[2], s.speed, s.heading, s.grounded ? "ground" : "AIR");
        }
        printf("   grounded %.0f%% of frames, airborne events %d, max speed %.2f, min y %.2f, sudden slowdowns %d\n",
               100.0f * grounded_frames / frames, airborne_events, maxspeed, miny, wall_slows);
    }
    printf(bad ? "SIM FAILED\n" : "simulation finished without NaN or falling out of the world\n");
    coll_free(&C); free(b);
    return bad != 0;
}
