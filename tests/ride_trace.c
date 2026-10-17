/* Ride trace (analysis tool): one ride with the real physics, frame by frame, naming the collision polygon that blocks.
 * usage: ride_trace <level.lvl> x z heading_deg [speed] [seconds]
 * Prints every 6th frame: position, speed, grounded, ground height, and the polygon (index, flags, normal) that blocks the lo/hi wall probes. */
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

/* which polygon does the segment a->b hit first? (Moller-Trumbore, same as src/coll.c) */
static int who(const float a[3], const float b[3], float *nrm)
{
    int best = -1; float bt = 1e30f;
    for (uint32_t i = 0; i < C.npolys; i++)
        for (int half = 0; half < 2; half++) {
            const CollPoly *p = &C.poly[i];
            if (half == 1 && p->v[3] == 0xFFFF) continue;
            const float *v0 = C.vert + p->v[0] * 3, *v1 = C.vert + p->v[half ? 2 : 1] * 3, *v2 = C.vert + p->v[half ? 3 : 2] * 3;
            float e1[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] }, e2[3] = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
            float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (l < 1e-12f || fabsf(n[1] / l) >= COLL_RIDE_NY) continue;
            float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            float hh[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
            float det = e1[0] * hh[0] + e1[1] * hh[1] + e1[2] * hh[2];
            if (fabsf(det) < 1e-9f) continue;
            float inv = 1.0f / det, s[3] = { a[0] - v0[0], a[1] - v0[1], a[2] - v0[2] };
            float u = (s[0] * hh[0] + s[1] * hh[1] + s[2] * hh[2]) * inv;
            if (u < 0 || u > 1) continue;
            float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
            float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
            if (v < 0 || u + v > 1) continue;
            float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
            if (t < 0 || t > 1 || t >= bt) continue;
            bt = t; best = (int)i; nrm[0] = n[0] / l; nrm[1] = n[1] / l; nrm[2] = n[2] / l;
        }
    return best;
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) return 2;
    fclose(f);
    float x = (float)atof(argv[2]), z = (float)atof(argv[3]), hd = (float)atof(argv[4]) * 0.0174533f;
    float speed = argc > 5 ? (float)atof(argv[5]) : 5.0f, secs = argc > 6 ? (float)atof(argv[6]) : 3.0f;
    const Hdr *h = (const Hdr *)b;
    if (!coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f)) return 1;
    SkateWorld w = { &C, g_ground, g_wall, g_surface, NULL };
    float y;
    if (!coll_ground(&C, x, z, 1e6f, &y)) { printf("no ground at start\n"); return 1; }
    Skate s;
    skate_init(&s, x, y, z, hd);
    s.speed = speed;
    SkateInput in = { 0, 0, 0, 0 };
    int every = argc > 7 ? atoi(argv[7]) : 6;
    for (int i = 0; i < (int)(secs * 60); i++) {
        float px = s.pos[0], pz = s.pos[2], ps = s.speed;
        skate_update(&s, &in, &w, 1.0f / 60.0f);
        if (i % every == 0 || (s.speed < ps - 1.0f)) {
            float gy = 0; int hg = coll_ground(&C, s.pos[0], s.pos[2], s.pos[1] + 0.3f, &gy);
            printf("t=%.2f pos (%6.2f %6.2f %6.2f) speed %5.2f %s cn (%.2f %.2f %.2f) ground %s%.2f", i / 60.0f, s.pos[0], s.pos[1], s.pos[2], s.speed, s.grounded ? "G" : "air", s.cn[0], s.cn[1], s.cn[2], hg ? "" : "none ", hg ? gy : 0.0f);
            /* probes towards the heading, like move_horizontal() */
            float fw[3] = { sinf(s.heading), 0, cosf(s.heading) };
            for (int k = 0; k < 2; k++) {
                float from[3] = { px, s.pos[1] + (k ? 0.9f : 0.3f), pz }, to[3] = { px + fw[0] * 0.7f, from[1], pz + fw[2] * 0.7f }, nn[3];
                int p = who(from, to, nn);
                if (p >= 0) printf("  | %s probe hits poly %d flags %u n=(%.2f %.2f %.2f)", k ? "hi" : "lo", p, (unsigned)C.poly[p].flags, nn[0], nn[1], nn[2]);
            }
            printf("\n");
        }
    }
    return 0;
}
