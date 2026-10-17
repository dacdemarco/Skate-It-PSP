/* Host tool: a simple bot rides every race of the career with the REAL skate physics on the REAL collision of the level, to see what the port's physics can reach against the original's opponent times
 * (the opponents finish in samples / 4 s). The bot only steers towards the next point of the course and pushes when it is slow - no ollies, no tricks - so its time is a ceiling for what a player
 * can do, not the best possible. usage: race_bot <career.dat> <data dir> [chal]    prints a table; always exits 0 unless a level fails to load. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "../src/career.h"
#include "../src/race.h"
#include "../src/coll.h"
#include "../src/skate.h"
#include "../src/trick.h"

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
    uint32_t nrails, nrailpts, off_rails, off_polymat, off_railids, off_objs, off_paths, pad;
} Hdr;

static Coll C;
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&C, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&C, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&C, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }
static int race_ground(void *ctx, float x, float z, float yh, float *y) { (void)ctx; return coll_ground(&C, x, z, yh, y); }

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz); if (fread(b, 1, (size_t)sz, f) != (size_t)sz) exit(2); fclose(f);
    *n = (size_t)sz;
    return b;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    size_t n;
    unsigned char *cb = slurp(argv[1], &n);
    Career c;
    if (!career_load(&c, cb, n)) return 2;
    int only = argc > 3 ? atoi(argv[3]) : -1;
    tricks_defaults();
    printf("%-4s %-22s %6s %9s %9s %8s %7s %s\n", "chal", "name", "length", "budget", "opp s", "bot s", "bails", "result");
    for (uint32_t i = 0; i < c.nchal; i++) {
        const CareerChal *ch = &c.chal[i];
        if (!(ch->mode == MODE_CHECKPOINT || ch->mode == MODE_DEATHRACE) || !(ch->flags & CHAL_CAREER) || (only >= 0 && (int)i != only)) continue;
        const char *z = NULL;
        for (uint32_t l = 0; l < c.nlevels; l++) if (c.level[l].id == ch->level) z = career_str(&c, c.level[l].zip_off);
        char zip[48], path[400];
        snprintf(zip, sizeof(zip), "%s", z);
        for (char *q = zip; *q; q++) *q = (char)tolower((unsigned char)*q);
        snprintf(path, sizeof(path), "%s/%s.lvl", argv[2], zip);
        size_t ln;
        unsigned char *lb = slurp(path, &ln);
        const Hdr *h = (const Hdr *)lb;
        if (!coll_build(&C, (const float *)(lb + h->off_collv), h->ncv, (const CollPoly *)(lb + h->off_collp), h->ncp, 4.0f)) return 1;
        LevelObjs o;
        levelobj_init(&o, lb, ln, h->off_objs);
        levelobj_init_paths(&o, lb, ln, h->off_paths);
        static Race r;
        if (!race_setup(&r, &c, (int)i, &o)) { printf("%-4u setup failed\n", i); coll_free(&C); free(lb); continue; }
        float sp[3], hd;
        char nm[40]; snprintf(nm, sizeof(nm), "_start_pos_1");
        levelobj_find_num(&o, "chal", 3, ch->slot, nm, sp);
        { float d[3]; levelobj_find_num(&o, "chal", 3, ch->slot, "_start_dir_1", d); hd = atan2f(d[0] - sp[0], d[2] - sp[2]); }
        float gy;
        if (!coll_ground(&C, sp[0], sp[2], sp[1] + 1.0f, &gy)) gy = sp[1];
        SkateWorld w = { NULL, g_ground, g_wall, g_surface, NULL };
        Skate s;
        skate_init(&s, sp[0], gy, sp[2], hd);
        float budget = (float)ch->time, fastest = 1e9f, clock = (float)ch->time, t = 0.0f, push_t = 0.0f;
        if (ch->mode == MODE_CHECKPOINT) for (int g = 0; g < r.ngates; g++) budget += r.gate[g].bonus;
        for (int k = 0; k < r.nopp; k++) if (r.opp[k].finish_time < fastest) fastest = r.opp[k].finish_time;
        int bails = 0, done = 0, prev_bail = 0;
        const char *why = "time up";
        for (int step = 0; step < 60 * 400; step++, t += 1.0f / 60.0f) {
            /* aim at a point 9 m further along the course than the nearest point */
            float tgt[3], tn[3];
            race_path_at(&r.course, r.prog + 9.0f, tgt, tn);
            if (r.prog + 9.0f > r.course.len) for (int k = 0; k < 3; k++) tgt[k] += tn[k] * 20.0f;
            float want = atan2f(tgt[0] - s.pos[0], tgt[2] - s.pos[2]), dh = want - s.heading;
            while (dh > 3.14159f) dh -= 6.28318f;
            while (dh < -3.14159f) dh += 6.28318f;
            SkateInput in;
            memset(&in, 0, sizeof(in));
            in.steer = dh * 2.0f > 1.0f ? 1.0f : (dh * 2.0f < -1.0f ? -1.0f : dh * 2.0f);
            push_t -= 1.0f / 60.0f;
            if (s.grounded && s.speed < 6.5f && push_t <= 0.0f && s.bail_t <= 0.0f) { in.push = 1; push_t = 0.8f; }
            if (fabsf(dh) > 1.2f && s.speed > 3.0f) in.brake = 1;
            skate_update(&s, &in, &w, 1.0f / 60.0f);
            if (s.bail_t > 0.0f && !prev_bail) { bails++; race_bail(&r); clock -= RACE_BAIL_PENALTY; }
            prev_bail = s.bail_t > 0.0f;
            int ev = race_update(&r, 1.0f / 60.0f, s.pos, race_ground, NULL);
            clock -= 1.0f / 60.0f;
            if (ev & RACE_EV_GATE) clock += r.last_bonus;
            if (ev & RACE_EV_FINISH) { done = 1; why = "finished"; break; }
            if (ch->mode == MODE_CHECKPOINT && clock <= 0.0f) { why = "clock ran out"; break; }
            if (ch->mode == MODE_DEATHRACE && t > (float)ch->time) { why = "time up"; break; }
            if (s.pos[1] < h->bounds[1] - 40.0f) { why = "fell out"; break; }
            if (getenv("BOT_TRACE") && step % 120 == 0) printf("   t=%5.1f pos (%7.1f %6.1f %7.1f) speed %5.2f head %5.2f want %5.2f %s prog %.0f\n", t, s.pos[0], s.pos[1], s.pos[2], s.speed, s.heading, want, s.grounded ? "ground" : "AIR", r.prog);
        }
        printf("%-4u %-22.22s %6.0f %9.1f %9.1f ", i, career_str(&c, ch->name_off), r.course.len, budget, fastest);
        if (done) printf("%8.1f %7d place %d of %d (gates %d/%d)\n", r.player_time, bails, r.place, r.nopp + 1, r.next, r.ngates);
        else printf("%8s %7d %s at %.0f of %.0f m, gate %d/%d\n", "-", bails, why, r.prog, r.course.len, r.next, r.ngates);
        coll_free(&C);
        free(lb);
    }
    return 0;
}
