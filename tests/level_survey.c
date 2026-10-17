/* Host survey of the converted levels (M10): for every level file, the places the career uses (the free-skate spawn, the beam and the start grid of every career event that is open to the game) must have ground
 * under them, and riders set off from them in 8 directions with two scripts (pushing and steering, an ollie) must not turn into NaN, fall out of the level or end up inside a solid.
 * usage: level_survey <career.dat> <level.lvl>...   prints one line per level and the problems; exit 1 if a spawn / beam / start has no ground or a ride produced a NaN. Falls and "inside" counts are reported
 * (they say where the collision is weak) and fail the run only above a limit (SURVEY_MAX_BAD per level). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "../src/career.h"
#include "../src/coll.h"
#include "../src/skate.h"
#include "../src/rails.h"
#include "../src/trick.h"
#include "../src/levelobj.h"

#define SURVEY_MAX_BAD 12

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
    uint32_t nrails, nrailpts, off_rails, off_polymat, off_railids, off_objs, off_paths, pad;
} Hdr;

static Coll C;
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&C, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&C, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&C, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }

/* is there a floor-like surface directly above (within 1.7 m)? = under a slab / inside a block; fills the height and normal of what covers it */
static int covered(const float p[3], float *over, float nrm[3])
{
    static const float off[4][2] = { { 0.12f, 0 }, { -0.12f, 0 }, { 0, 0.12f }, { 0, -0.12f } };
    for (int k = 0; k < 4; k++) {
        float a[3] = { p[0] + off[k][0], p[1] + 0.12f, p[2] + off[k][1] }, b[3] = { a[0], p[1] + 1.7f, a[2] }, h[3], n[3];
        if (!coll_ray(&C, a, b, COLL_FLOOR_NY, 2.0f, 0.0f, h, n)) return 0;
        *over = h[1] - p[1]; nrm[0] = n[0]; nrm[1] = n[1]; nrm[2] = n[2];
    }
    return 1;
}

typedef struct { int rides, falls, inside, nans, left, stuck, boxed; } Tally;
static float g_lo[3], g_hi[3];
static int g_have_play;            /* the level's play area (the outer bounds): past it the game puts the rider back, so it is no fall */

static void ride(const SkateWorld *w, const float bounds[6], float x, float y, float z, float heading, int script, Tally *t, const char *what)
{
    Skate s;
    skate_init(&s, x, y, z, heading);
    t->rides++;
    int fell = 0, inside = 0, left = 0;
    float px0 = 0, pz0 = 0, trail[40][4];
    int ntrail = 0;
    float over = 0, nrm[3] = { 0, 0, 0 }, ipos[3] = { 0, 0, 0 }, itime = 0;
    for (int i = 0; i < 60 * 9; i++) {
        float tm = (float)i / 60.0f;
        SkateInput in;
        memset(&in, 0, sizeof(in));
        if (i % 72 == 1 && tm < 6.0f) in.push = 1;
        if (script == 1) {
            if (tm > 3.0f && tm < 3.35f) in.ollie_held = 1;
            if (tm > 5.0f && tm < 6.5f) in.steer = 1.0f;
            if (tm > 7.0f && tm < 8.0f) in.steer = -1.0f;
        } else {
            if (tm > 2.0f && tm < 3.5f) in.steer = (heading > 0.0f) ? 0.7f : -0.7f;
        }
        skate_update(&s, &in, w, 1.0f / 60.0f);
        if (i == 180) { px0 = s.pos[0]; pz0 = s.pos[2]; }
        if (i % 30 == 0 && ntrail < 40) { trail[ntrail][0] = tm; trail[ntrail][1] = s.pos[0]; trail[ntrail][2] = s.pos[1]; trail[ntrail][3] = s.pos[2]; ntrail++; }
        if (i == 360) {
            float dx = s.pos[0] - px0, dz = s.pos[2] - pz0;
            if (dx * dx + dz * dz < 1.0f && !s.grinding) { t->stuck++; }
        }
        if (g_have_play && (s.pos[0] < g_lo[0] || s.pos[0] > g_hi[0] || s.pos[2] < g_lo[2] || s.pos[2] > g_hi[2])) { left = 1; break; }
        if (!isfinite(s.pos[0]) || !isfinite(s.pos[1]) || !isfinite(s.pos[2]) || !isfinite(s.speed)) { t->nans++; printf("  NaN: %s heading %.2f script %d\n", what, heading, script); return; }
        if (s.pos[1] < bounds[1] - 30.0f) { fell = 1; break; }
        if (!inside && s.grounded && i % 15 == 0 && covered(s.pos, &over, nrm)) { inside = 1; ipos[0] = s.pos[0]; ipos[1] = s.pos[1]; ipos[2] = s.pos[2]; itime = tm; }
    }
    if (left) t->left++;
    if (fell && getenv("SURVEY_TRAIL")) for (int k = 0; k < ntrail; k++) printf("      t %.1f  (%.1f %.1f %.1f)\n", trail[k][0], trail[k][1], trail[k][2], trail[k][3]);
    if (fell) { t->falls++; printf("  fell out: %s heading %.2f script %d (last %.1f %.1f %.1f)\n", what, heading, script, s.pos[0], s.pos[1], s.pos[2]); }
    if (inside) { t->inside++; printf("  inside a solid: %s heading %.2f script %d at (%.1f %.1f %.1f) after %.1f s, a surface %.2f m above, normal (%.2f %.2f %.2f)\n", what, heading, script, ipos[0], ipos[1], ipos[2], itime, over, nrm[0], nrm[1], nrm[2]); }
}

static int survey_point(const SkateWorld *w, const float bounds[6], const float p[3], float heading0, Tally *t, const char *what, int nheadings)
{
    float gy;
    if (!coll_ground(&C, p[0], p[2], p[1] + 3.0f, &gy) || fabsf(gy - p[1]) > 4.0f) {
        printf("  NO GROUND under %s (%.1f %.1f %.1f)\n", what, p[0], p[1], p[2]);
        return 0;
    }
    int stuck0 = t->stuck, rides = 0;
    for (int k = 0; k < nheadings; k++)
        for (int sc = 0; sc < 2; sc++) {
            ride(w, bounds, p[0], gy, p[2], heading0 + (float)k * 6.2831853f / (float)nheadings, sc, t, what);
            rides++;
        }
    if (t->stuck - stuck0 >= rides - rides / 5 && rides >= 4) {            /* boxed in: nearly every way out is blocked */
        t->boxed++;
        printf("  BOXED IN at %s (%.1f %.1f %.1f): %d of %d rides went nowhere\n", what, p[0], gy, p[2], t->stuck - stuck0, rides);
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *cf = fopen(argv[1], "rb");
    if (!cf) { printf("cannot open %s\n", argv[1]); return 2; }
    fseek(cf, 0, SEEK_END); long cn = ftell(cf); fseek(cf, 0, SEEK_SET);
    unsigned char *cb = malloc((size_t)cn);
    if (fread(cb, 1, (size_t)cn, cf) != (size_t)cn) return 2;
    fclose(cf);
    static Career career;
    if (!career_load(&career, cb, (size_t)cn)) { printf("career.dat does not load\n"); return 2; }
    tricks_defaults();
    int hard = 0, total_bad = 0;
    for (int a = 2; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) { printf("cannot open %s\n", argv[a]); return 2; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *b = malloc((size_t)n);
        if (fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
        fclose(f);
        const Hdr *h = (const Hdr *)b;
        if (!coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f)) { printf("%s: collision grid failed\n", argv[a]); return 2; }
        Rails rl;
        if (!rails_build(&rl, (const uint32_t *)(b + h->off_rails), h->nrails, (const float *)(b + h->off_rails + 8 * h->nrails))) { printf("%s: rails failed\n", argv[a]); return 2; }
        LevelObjs o;
        int have_objs = levelobj_init(&o, b, (size_t)n, h->off_objs);
        SkateWorld w = { NULL, g_ground, g_wall, g_surface, &rl };
        char stem[64];
        const char *sl = strrchr(argv[a], '/');
        snprintf(stem, sizeof(stem), "%s", sl ? sl + 1 : argv[a]);
        char *dot = strrchr(stem, '.');
        if (dot) *dot = 0;
        int lvl = career_level_by_stem(&career, stem);
        printf("== %s (level %d, collision %u polygons, %u rails)\n", stem, lvl, h->ncp, h->nrails);
        Tally t = { 0, 0, 0, 0, 0, 0, 0 };
        g_have_play = have_objs && levelobj_bounds_outer(&o, g_lo, g_hi);
        int points = 0, noground = 0;
        {   /* the free-skate spawn, as src/main.c find_spawn does: the start grid slot 1, else the header's start */
            float p[3] = { h->start[0], h->bounds[1], h->start[2] }, hd = 0.0f;
            if (have_objs && levelobj_start(&o, 1, p, &hd)) { /* p is the slot's position */ }
            float gy;
            int found = coll_ground(&C, p[0], p[2], h->bounds[4] + 5.0f, &gy);
            for (int ring = 1; ring < 40 && !found; ring++)
                for (int k = 0; k < 8 && !found; k++) {
                    float ang = (float)k * 0.785398f, r = (float)ring * 2.0f;
                    if (coll_ground(&C, p[0] + cosf(ang) * r, p[2] + sinf(ang) * r, h->bounds[4] + 5.0f, &gy)) { p[0] += cosf(ang) * r; p[2] += sinf(ang) * r; found = 1; }
                }
            if (!found) { printf("  NO GROUND anywhere near the spawn\n"); noground++; }
            else { p[1] = gy; points++; if (!survey_point(&w, h->bounds, p, hd, &t, "spawn", 8)) noground++; }
        }
        int cand[32], nc = lvl >= 0 && have_objs ? career_challenges_at(&career, lvl, cand, 32) : 0;
        for (int i = 0; i < nc; i++) {
            if (!career_chal_runnable(&career, cand[i])) continue;
            const CareerChal *ch = &career.chal[cand[i]];
            char what[64], name[40];
            float p[3], d[3];
            snprintf(what, sizeof(what), "beam of %d", cand[i]);
            if (levelobj_find_num(&o, "chal", 3, ch->slot, "_chalstart", p)) { points++; if (!survey_point(&w, h->bounds, p, 0.0f, &t, what, 4)) noground++; }
            else printf("  (no beam for event %d, slot %d: it starts from the map)\n", cand[i], ch->slot);
            snprintf(name, sizeof(name), "_start_pos_1");
            if (levelobj_find_num(&o, "chal", 3, ch->slot, name, p)) {
                float hd = 0.0f;
                if (levelobj_find_num(&o, "chal", 3, ch->slot, "_start_dir_1", d)) hd = atan2f(d[0] - p[0], d[2] - p[2]);
                snprintf(what, sizeof(what), "start of %d", cand[i]);
                points++;
                if (!survey_point(&w, h->bounds, p, hd, &t, what, 2)) noground++;
            }
        }
        int bad = t.falls + t.boxed;
        printf("   %d places, %d rides: %d without ground, %d NaN, %d fell out inside the play area, %d places boxed in (%d rides ended against a wall), %d under a low surface (%d rides left the play area)\n", points, t.rides, noground, t.nans, t.falls, t.boxed, t.stuck, t.inside, t.left);
        if (noground || t.nans) hard++;
        if (bad > SURVEY_MAX_BAD) { printf("   too many bad rides (%d > %d)\n", bad, SURVEY_MAX_BAD); hard++; }
        total_bad += bad;
        coll_free(&C);
        free(b);
    }
    if (hard) printf("SURVEY: %d level(s) with hard problems (%d bad rides in all)\n", hard, total_bad);
    else printf("survey done: no hard problems (%d bad rides in all)\n", total_bad);
    return hard != 0;
}
