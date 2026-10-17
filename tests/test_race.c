/* Host test: the race modes (src/race.c) on the real data: all 17 Checkpoint / Deathrace challenges with their levels' gates, start grids and progress splines, and the opponents' recorded times
 * (career.dat) against the races' time limits.
 * usage: test_race <career.dat> <data dir with the .lvl files> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "../src/career.h"
#include "../src/race.h"
#include "../src/ghost.h"
#include "../src/trick.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
    uint32_t nrails, nrailpts, off_rails, off_polymat, off_railids, off_objs, off_paths, pad;
} Hdr;

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz); if (fread(b, 1, (size_t)sz, f) != (size_t)sz) exit(2); fclose(f);
    *n = (size_t)sz;
    return b;
}

static float ground_calls;
static int flat_ground(void *ctx, float x, float z, float y_hint, float *y) { (void)ctx; (void)x; (void)z; ground_calls++; *y = y_hint - 2.0f; return 1; }

/* walk the course at speed v (m/s), 60 steps a second; returns the events seen in order in ev[], the time at the finish */
static float walk(Race *r, float v, const float *from_s, int *gates_seen, int skip_first)
{
    float s = from_s ? *from_s : 0.0f, p[3], t[3];
    *gates_seen = 0;
    int guard = 0;
    while (s < r->course.len + 5.0f && guard++ < 600000) {
        s += v / 60.0f;
        race_path_at(&r->course, s < r->course.len ? s : r->course.len, p, t);
        if (s > r->course.len) for (int k = 0; k < 3; k++) p[k] += t[k] * (s - r->course.len);          /* on past the finish line */
        if (skip_first && s < 5.0f) { r->t += 1.0f / 60.0f; continue; }
        int ev = race_update(r, 1.0f / 60.0f, p, flat_ground, NULL);
        if (ev & RACE_EV_GATE) (*gates_seen)++;
        if (ev & RACE_EV_FINISH) return r->player_time;
    }
    return -1.0f;
}

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    size_t n;
    unsigned char *cb = slurp(argv[1], &n);
    Career c;
    CHECK(career_load(&c, cb, n), "career.dat loads");
    tricks_defaults();
    char pakpath[400];
    snprintf(pakpath, sizeof(pakpath), "%s/ai.pak", argv[2]);
    size_t pn;
    unsigned char *pb = slurp(pakpath, &pn);
    GhostPak pak;
    CHECK(ghost_pak_load(&pak, pb, pn), "ai.pak loads");
    int recorded_opps = 0;
    int races = 0;
    printf("%-4s %-22s %-10s %5s %6s %4s %9s %9s %8s %s\n", "chal", "name", "level", "gates", "length", "opps", "budget s", "opp s", "avg m/s", "walker");
    for (uint32_t i = 0; i < c.nchal; i++) {
        const CareerChal *ch = &c.chal[i];
        if (!(ch->mode == MODE_CHECKPOINT || ch->mode == MODE_DEATHRACE) || !(ch->flags & CHAL_CAREER)) continue;
        races++;
        char path[400], zip[48];
        const char *z = NULL;
        for (uint32_t l = 0; l < c.nlevels; l++) if (c.level[l].id == ch->level) z = career_str(&c, c.level[l].zip_off);
        snprintf(zip, sizeof(zip), "%s", z ? z : "");
        for (char *q = zip; *q; q++) *q = (char)tolower((unsigned char)*q);
        snprintf(path, sizeof(path), "%s/%s.lvl", argv[2], zip);
        size_t ln;
        unsigned char *lb = slurp(path, &ln);
        const Hdr *h = (const Hdr *)lb;
        LevelObjs o;
        CHECK(levelobj_init(&o, lb, ln, h->off_objs) && levelobj_init_paths(&o, lb, ln, h->off_paths), "%s: objects and paths", zip);
        static Race r;
        int ok = race_setup(&r, &c, (int)i, &o);
        CHECK(ok, "challenge %u (%s): race_setup", i, career_str(&c, ch->name_off));
        if (!ok) { free(lb); continue; }
        CHECK(r.ngates == ch->nobj && r.gate[r.ngates - 1].finish, "%u: %d gates, the last is the finish", i, r.ngates);
        int expect_opps = 0;
        for (int k = 0; k < 3; k++) expect_opps += ch->opp[k] != 0xFE && career_opponent_samples(&c, (int)i, k) > 0;
        CHECK(r.nopp == expect_opps && r.nopp >= 1, "%u: %d opponents (%d expected)", i, r.nopp, expect_opps);
        /* ground truth: the recorded opponent runs fit inside what the original gave the player (4 recorded samples a second) */
        float budget = (float)ch->time, fastest = 1e9f;
        if (ch->mode == MODE_CHECKPOINT) for (int g = 0; g < r.ngates; g++) budget += r.gate[g].bonus;
        for (int k = 0; k < r.nopp; k++) {
            CHECK((float)career_opponent_samples(&c, (int)i, k) / 4.0f <= budget + 0.01f, "%u: opponent %d finishes (as recorded) in %.1f s but the player's clock only reaches %.1f s", i, k, (float)career_opponent_samples(&c, (int)i, k) / 4.0f, budget);
            if (r.opp[k].finish_time < fastest) fastest = r.opp[k].finish_time;
            CHECK(r.opp[k].finish_time <= budget * 1.05f, "%u: opponent %d (after the speed cap) needs %.1f s but the player's clock reaches %.1f s", i, k, r.opp[k].finish_time, budget);
            CHECK(r.opp[k].cruise > 2.0f && r.opp[k].cruise < 30.0f, "%u: opponent %d cruises at %.1f m/s", i, k, r.opp[k].cruise);
        }
        /* the walker follows the player's line at a fixed speed: every gate in order, then the finish */
        int seen;
        memset(r.prev, 0, sizeof(r.prev));
        float tfin = walk(&r, 12.0f, NULL, &seen, 0);
        CHECK(tfin > 0.0f && seen == r.ngates && r.player_done, "%u: the walker passes all %d gates (%d) and finishes", i, r.ngates, seen);
        CHECK(r.next == r.ngates, "%u: next gate index %d", i, r.next);
        CHECK(race_place(&r) == r.place && r.place >= 1 && r.place <= r.nopp + 1, "%u: place %d", i, r.place);
        /* opponents end up at the finish gate when their time is up, and not before */
        {
            static Race q;
            race_setup(&q, &c, (int)i, &o);
            float fm[3], d_start;
            for (int k = 0; k < 3; k++) fm[k] = (q.gate[q.ngates - 1].a[k] + q.gate[q.ngates - 1].b[k]) * 0.5f;
            float far[3] = { 1e5f, 0, 1e5f };
            d_start = hypotf(q.opp[0].pos[0] - fm[0], q.opp[0].pos[2] - fm[2]);
            float t_end = q.opp[0].finish_time;
            int steps = (int)(t_end * 60.0f);
            for (int s = 0; s < steps - 1; s++) race_update(&q, 1.0f / 60.0f, far, flat_ground, NULL);
            float d_before = hypotf(q.opp[0].pos[0] - fm[0], q.opp[0].pos[2] - fm[2]);
            CHECK(!q.opp[0].finished && d_before < 6.0f + q.opp[0].cruise * 0.05f, "%u: opponent 0 one frame before his time is %.1f m from the finish line (started %.0f m away)", i, d_before, d_start);
            for (int s = 0; s < 3; s++) race_update(&q, 1.0f / 60.0f, far, flat_ground, NULL);
            CHECK(q.opp[0].finished, "%u: opponent 0 finished at his recorded time", i);
            for (int s = 0; s < 400; s++) race_update(&q, 1.0f / 60.0f, far, flat_ground, NULL);
            CHECK(q.opp[0].speed < 0.01f, "%u: opponents stop after the finish (%.2f m/s)", i, q.opp[0].speed);
        }
        /* the recorded runs: every opponent has one, it starts on his grid slot, the pace cap only slows it, he is at the finish gate when his time is up and the placing follows him */
        {
            static Race q;
            race_setup(&q, &c, (int)i, &o);
            int nrec = race_use_recordings(&q, &c, (int)i, &pak, zip);
            CHECK(nrec == q.nopp, "%u: %d of %d opponents have a recording", i, nrec, q.nopp);
            recorded_opps += nrec;
            race_preview(&q, flat_ground, NULL);
            float far[3] = { 1e5f, 0, 1e5f }, fm[3], last_s[3] = { -1, -1, -1 };
            for (int k = 0; k < 3; k++) fm[k] = (q.gate[q.ngates - 1].a[k] + q.gate[q.ngates - 1].b[k]) * 0.5f;
            float t_max = 0.0f;
            for (int k = 0; k < q.nopp; k++) if (q.opp[k].finish_time > t_max) t_max = q.opp[k].finish_time;
            for (int k = 0; k < q.nopp; k++) {
                float st[3];
                CHECK(levelobj_find_num(&o, "chal", 3, ch->slot, k == 0 ? "_start_pos_2" : (k == 1 ? "_start_pos_3" : "_start_pos_4"), st), "%u: start slot of opponent %d", i, k);
                CHECK(hypotf(q.opp[k].pos[0] - st[0], q.opp[k].pos[2] - st[2]) < 4.0f, "%u: recorded opponent %d starts %.1f m from his grid slot", i, k, hypotf(q.opp[k].pos[0] - st[0], q.opp[k].pos[2] - st[2]));
                CHECK(q.opp[k].rate > 0.3f && q.opp[k].rate <= 1.0f, "%u: opponent %d plays at rate %.2f", i, k, q.opp[k].rate);
                CHECK(q.opp[k].finish_time > 5.0f && q.opp[k].finish_time <= q.opp[k].rec.dur / q.opp[k].rate + 0.01f, "%u: opponent %d finishes at %.1f s of a %.1f s recording at rate %.2f", i, k, q.opp[k].finish_time, q.opp[k].rec.dur, q.opp[k].rate);
                CHECK(q.opp[k].finish_time <= budget * 1.05f, "%u: recorded opponent %d needs %.1f s but the player's clock reaches %.1f s", i, k, q.opp[k].finish_time, budget);
            }
            int steps = (int)((t_max + 1.0f) * 60.0f), bad = 0, nonmono = 0;
            float fin_d[3] = { -1, -1, -1 };
            for (int s = 0; s < steps; s++) {
                race_update(&q, 1.0f / 60.0f, far, flat_ground, NULL);
                for (int k = 0; k < q.nopp; k++) {
                    const RaceOpp *op = &q.opp[k];
                    if (!isfinite(op->pos[0]) || !isfinite(op->pos[1]) || !isfinite(op->pos[2]) || !isfinite(op->heading) || !isfinite(op->speed) || op->speed > 25.0f || fabsf(op->view.pos[0] - op->pos[0]) > 1e-3f) bad++;
                    if (op->s + 1e-3f < last_s[k]) nonmono++;
                    last_s[k] = op->s;
                    if (fin_d[k] < 0.0f && q.t >= op->finish_time) fin_d[k] = hypotf(op->pos[0] - fm[0], op->pos[2] - fm[2]);
                }
            }
            CHECK(bad == 0, "%u: %d bad opponent states while playing the recordings", i, bad);
            CHECK(nonmono == 0, "%u: opponents' progress went backwards %d times", i, nonmono);
            for (int k = 0; k < q.nopp; k++)
                CHECK(fin_d[k] >= 0.0f && fin_d[k] < 14.0f, "%u: recorded opponent %d is %.1f m from the finish gate when his time is up", i, k, fin_d[k]);
        }
        if (i == 19) {                                                  /* the rules in detail on Speed School: Checkpoint, 6 gates, Shingo */
            static Race q;
            race_setup(&q, &c, 19, &o);
            float m[3][3], nrm[2];                                     /* gate 0 and 1: their middles and the way across */
            for (int g = 0; g < 2; g++) {
                for (int k = 0; k < 3; k++) m[g][k] = (q.gate[g].a[k] + q.gate[g].b[k]) * 0.5f;
            }
            float away[3] = { m[0][0] + 300.0f, m[0][1], m[0][2] + 300.0f }, p0[3], p1[3];
            race_update(&q, 1.0f / 60.0f, away, NULL, NULL);
            { float dx = q.gate[1].b[0] - q.gate[1].a[0], dz = q.gate[1].b[2] - q.gate[1].a[2], l = hypotf(dx, dz); nrm[0] = -dz / l; nrm[1] = dx / l; }
            p0[0] = m[1][0] - nrm[0] * 2.0f; p0[1] = m[1][1]; p0[2] = m[1][2] - nrm[1] * 2.0f;
            p1[0] = m[1][0] + nrm[0] * 2.0f; p1[1] = m[1][1]; p1[2] = m[1][2] + nrm[1] * 2.0f;
            race_update(&q, 1.0f / 60.0f, p0, NULL, NULL);
            int ev = race_update(&q, 1.0f / 60.0f, p1, NULL, NULL);
            CHECK(ev == 0 && q.next == 0, "gate 1 first: it does not count while gate 0 is next (next %d)", q.next);
            { float dx = q.gate[0].b[0] - q.gate[0].a[0], dz = q.gate[0].b[2] - q.gate[0].a[2], l = hypotf(dx, dz); nrm[0] = -dz / l; nrm[1] = dx / l; }
            p0[0] = m[0][0] - nrm[0] * 2.0f; p0[1] = m[0][1]; p0[2] = m[0][2] - nrm[1] * 2.0f;
            p1[0] = m[0][0] + nrm[0] * 2.0f; p1[1] = m[0][1]; p1[2] = m[0][2] + nrm[1] * 2.0f;
            race_update(&q, 1.0f / 60.0f, p0, NULL, NULL);
            ev = race_update(&q, 1.0f / 60.0f, p1, NULL, NULL);
            CHECK((ev & RACE_EV_GATE) && q.next == 1 && q.last_bonus == 7.0f, "gate 0 passed: +%.0f s, next gate %d", q.last_bonus, q.next);
            ev = race_update(&q, 1.0f / 60.0f, p1, NULL, NULL);
            CHECK(ev == 0 && q.last_bonus == 0.0f, "standing still passes nothing");
            race_bail(&q); race_bail(&q);
            CHECK(fabsf(q.penalty - 2.0f * RACE_BAIL_PENALTY) < 1e-4f, "two bails cost %.1f s", q.penalty);
            CHECK(race_place_at(&q, 0.5f) == 1 && race_place_at(&q, 1000.0f) == q.nopp + 1, "places by time: first when fast, last when slow");
            CHECK(race_place(&q) >= 1 && race_place(&q) <= q.nopp + 1, "a live place");
        }
        printf("%-4u %-22.22s %-10.10s %5d %6.0f %4d %9.1f %9.1f %8.1f %5.1f s place %d\n", i, career_str(&c, ch->name_off), zip, r.ngates, r.course.len, r.nopp, budget, fastest, r.course.len / fastest, tfin, r.place);
        free(lb);
    }
    CHECK(races == 17, "17 race challenges in the career (%d)", races);
    CHECK(recorded_opps >= 28, "recorded opponents: %d (28 expected)", recorded_opps);

    printf(fails ? "%d FAILURES\n" : "all race tests passed\n", fails);
    return fails != 0;
}
