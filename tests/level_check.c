/* Host check of one converted level (.lvl): sizes, rails / ledges, the spawn, the memory the PSP would use for the collision and clip grids, and a short scripted ride from the spawn
 * (no NaN, no falling out of the world, no sinking). usage: level_check <file.lvl>. Prints one summary line per level, then problems. Exit code 1 on a problem. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/coll.h"
#include "../src/skate.h"
#include "../src/rails.h"
#include "../src/nearclip.h"
#include "../src/levelobj.h"

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
    uint32_t nrails, nrailpts, off_rails, off_polymat, off_railids, off_objs, off_paths, pad;
} Hdr;
typedef struct { uint16_t tex, flags; uint32_t first, count, pad; } Batch;

static Coll C;
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&C, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&C, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&C, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 2; fclose(f);
    const Hdr *h = (const Hdr *)b;
    int bad = 0;
    if (memcmp(h->magic, "SKLV", 4) || h->version != 4) { printf("bad magic / version\n"); return 1; }
    if (!coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 4.0f)) { printf("coll_build failed\n"); return 1; }
    Rails R;
    if (!rails_build(&R, (const uint32_t *)(b + h->off_rails), h->nrails, (const float *)(b + h->off_rails + 8 * h->nrails))) { printf("rails_build failed\n"); return 1; }
    int nrail = 0, nledge = 0;
    for (uint32_t i = 0; i < R.n; i++) { if (R.rail[i].kind == RAIL_KIND_LEDGE) nledge++; else nrail++; }

    /* clip grid, as main.c builds it */
    uint32_t nt = h->nverts / 3;
    uint16_t *tex = malloc(sizeof(uint16_t) * nt); uint8_t *skip = calloc(nt, 1);
    const Batch *bt = (const Batch *)(b + h->off_batch);
    for (uint32_t i = 0; i < h->nbatch; i++)
        for (uint32_t k = bt[i].first / 3; k < (bt[i].first + bt[i].count) / 3 && k < nt; k++) { tex[k] = bt[i].tex; skip[k] = (bt[i].flags & 1) != 0; }
    NcGrid nc;
    int nc_ok = nc_build(&nc, (const NcVertex *)(b + h->off_verts), h->nverts, tex, skip, 16.0f);
    if (!nc_ok) { printf("nc_build failed\n"); bad++; }

    /* spawn as main.c find_spawn(): the original's start grid slot 1, else the converter's guess + ring search */
    LevelObjs obj; int have_obj = levelobj_init(&obj, b, (size_t)n, h->off_objs);
    float sx = h->start[0], sz = h->start[2], sy = h->bounds[1], sh = 0.0f; int found = 0, from_grid = 0;
    float sp[3], hd;
    if (have_obj && levelobj_start(&obj, 1, sp, &hd) && coll_ground(&C, sp[0], sp[2], sp[1] + 1.0f, &sy)) { sx = sp[0]; sz = sp[2]; sh = hd; found = 1; from_grid = 1; }
    for (int ring = 0; ring < 40 && !found; ring++)
        for (int k = 0; k < 8 && !found; k++) {
            float a = (float)k * 0.785398f, r = (float)ring * 2.0f, tx = sx + cosf(a) * r, tz = sz + sinf(a) * r;
            if (coll_ground(&C, tx, tz, h->bounds[4] + 5.0f, &sy)) { sx = tx; sz = tz; found = 1; }
        }
    float lo[3] = { -1e9f, -1e9f, -1e9f }, hi[3] = { 1e9f, 1e9f, 1e9f };
    int have_bounds = have_obj && levelobj_bounds_outer(&obj, lo, hi);
    int nchal = 0;
    for (uint32_t i = 0; have_obj && i < obj.n; i++) if (strstr((const char *)obj.tab + 44 * i, "_chalstart")) nchal++;
    size_t gridbytes = (size_t)(C.gw * C.gh + 1) * 4 + (size_t)C.cell_start[C.gw * C.gh] * (64 + 4);
    printf("%-34s %6.2f MB | tris %6u verts %6u tex %3u | coll polys %5u grid %dx%d %.1f MB | rails %3d ledges %3d | spawn %s%s (%.0f,%.0f,%.0f) | objects %u, challenge beams %d, bounds %s\n",
           argv[1], n / 1048576.0, nt, h->nverts, h->ntex, h->ncp, C.gw, C.gh, gridbytes / 1048576.0, nrail, nledge, found ? "ok" : "NOT FOUND", from_grid ? " (start grid)" : " (guess)", sx, sy, sz,
           have_obj ? obj.n : 0, nchal, have_bounds ? "yes" : "NO");
    if (have_bounds && (sx < lo[0] || sx > hi[0] || sz < lo[2] || sz > hi[2])) { printf("    the spawn lies outside the play area\n"); bad++; }
    printf("    bounds x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n", h->bounds[0], h->bounds[3], h->bounds[1], h->bounds[4], h->bounds[2], h->bounds[5]);
    if (!found) bad++;

    SkateWorld w = { NULL, g_ground, g_wall, g_surface, &R };
    for (int run = 0; run < 4 && found; run++) {
        Skate s; skate_init(&s, sx, sy, sz, sh + run * 1.57f);
        float t = 0, miny = 1e9f, startdist = 0, maxspeed = 0; int grounded = 0, frames = 0;
        for (int i = 0; i < 60 * 20; i++, t += 1.0f / 60.0f) {
            SkateInput in; memset(&in, 0, sizeof(in));
            if (i % 72 == 1 && t < 8.0f) in.push = 1;
            if (t > 9 && t < 10) in.steer = 1.0f;
            if (t > 13 && t < 14.5f) in.steer = -1.0f;
            skate_update(&s, &in, &w, 1.0f / 60.0f);
            frames++; grounded += s.grounded;
            if (s.pos[1] < miny) miny = s.pos[1];
            if (s.speed > maxspeed) maxspeed = s.speed;
            if (!isfinite(s.pos[0]) || !isfinite(s.pos[1]) || !isfinite(s.speed)) { printf("    run %d: NaN at t=%.2f\n", run, t); bad++; break; }
            if (have_bounds && (s.pos[0] < lo[0] || s.pos[0] > hi[0] || s.pos[2] < lo[2] || s.pos[2] > hi[2])) { printf("    run %d: left the play area at t=%.2f pos (%.1f %.1f %.1f)\n", run, t, s.pos[0], s.pos[1], s.pos[2]); break; }
            if (s.pos[1] < h->bounds[1] - 3.0f) { printf("    run %d: fell below the level at t=%.2f pos (%.1f %.1f %.1f)\n", run, t, s.pos[0], s.pos[1], s.pos[2]); bad++; break; }
        }
        startdist = hypotf(s.pos[0] - sx, s.pos[2] - sz);
        printf("    ride %d: grounded %3.0f%%  max speed %4.1f  moved %5.1f m  min y %.1f\n", run, 100.0f * grounded / frames, maxspeed, startdist, miny);
        if (grounded * 4 < frames) { printf("    run %d: mostly airborne - the spawn is probably over nothing\n", run); bad++; }
    }
    return bad != 0;
}
