/* Host test: the recorded runs played back (src/ghost.c) and staged (src/stage.c) on the real data.
 *   - every one of the 457 recordings of ai.pak plays from start to end without NaN, absurd speeds or a heading that spins
 *   - the original's flip ids map onto our flips exactly as src/goals.c maps ours onto the original's
 *   - the 24 career contests: Jam opponents stay in step with the contest clock, Best Trick opponents restart with every round, the S.K.A.T.E. opponent plays the recording of the trick he sets
 *   - the free-skate routes of all 21 levels: ambient skaters start away from the player, skate for minutes, move from route to route and never leave the level
 * usage: test_stage <career.dat> <data dir with ai.pak and the .lvl files> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include "../src/career.h"
#include "../src/contest.h"
#include "../src/goals.h"
#include "../src/ghost.h"
#include "../src/stage.h"
#include "../src/coll.h"
#include "../src/trick.h"
#include "../src/rider.h"

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

static Coll C;
static int ground(void *ctx, float x, float z, float y_hint, float *y) { (void)ctx; return coll_ground(&C, x, z, y_hint, y); }
static int finite3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
static float dist2(const float a[3], const float b[3]) { return hypotf(a[0] - b[0], a[2] - b[2]); }

/* ---- A: every recording ------------------------------------------------------------------------------------------------------------------------- */
static void test_recordings(const GhostPak *pak)
{
    long total = 0, jumps = 0, flips = 0, grounded = 0, steps = 0;
    for (uint32_t i = 0; i < pak->count; i++) {
        GhostTrack t;
        char lv[24];
        CHECK(ghost_entry(pak, i, &t, lv), "entry %u", i);
        GhostTrack f;
        CHECK(ghost_find(pak, lv, t.chal, t.mode, t.a, t.b, &f) && f.n == t.n, "%s %d_%d_%d_%d is found again", lv, t.chal, t.mode, t.a, t.b);
        CHECK(t.n >= 8 && t.dur >= 1.5f && t.len >= 0.0f && finite3(t.p), "%s %d_%d_%d_%d: %d points, %.1f s, %.0f m", lv, t.chal, t.mode, t.a, t.b, t.n, t.dur, t.len);
        GhostPlayer g;
        ghost_init(&g, &t, 1.0f, 0.0f);
        float prev_h = g.heading;
        int bad = 0, spins = 0;
        for (float tm = 0.0f; tm < t.dur + 0.1f; tm += 1.0f / 30.0f) {
            ghost_update(&g, 1.0f / 30.0f, NULL, NULL);
            if (!finite3(g.pos) || !finite3(g.fwd) || !finite3(g.up) || !isfinite(g.heading) || !isfinite(g.steer) || g.speed > 45.0f || g.speed < 0.0f) bad++;
            float dh = fabsf(g.heading - prev_h);
            if (dh > 3.14159f) dh = 6.2831853f - dh;
            if (dh > 1.2f && !g.jumped) spins++;                      /* a heading that turns more than ~70 degrees in a frame: not smooth */
            prev_h = g.heading;
            if (g.flip) flips++;
            if (g.jumped) jumps++;
            steps++;
        }
        total += t.n;
        CHECK(bad == 0, "%s %d_%d_%d_%d: %d frames with a NaN or an absurd speed", lv, t.chal, t.mode, t.a, t.b, bad);
        CHECK(spins <= 3, "%s %d_%d_%d_%d: the heading jumped %d times", lv, t.chal, t.mode, t.a, t.b, spins);
        CHECK(g.done, "%s %d_%d_%d_%d finishes", lv, t.chal, t.mode, t.a, t.b);
    }
    (void)grounded;
    printf("recordings: %u, %ld points, %ld frames played, %ld frames of a flip, %ld respawns\n", pak->count, total, steps, flips, jumps);
    CHECK(pak->count == 457, "457 recordings (%u)", pak->count);
    CHECK(flips > 5000 && jumps > 150, "flips and respawns are seen (%ld, %ld)", flips, jumps);
}

static void test_flip_ids(void)
{
    for (int type = 0; type < NFLIP_TYPES; type++)
        for (int nollie = 0; nollie < 2; nollie++) {
            TrickRec r;
            memset(&r, 0, sizeof(r));
            r.cls = TRICK_FLIP; r.type = (short)type; r.bs = -1; r.nollie = (unsigned char)nollie;
            int id = goals_trick_id(&r), nl = 0;
            int back = id >= 0 ? ghost_flip_of_id(id, &nl) : 0;
            if (id < 0) continue;                                         /* body flips have no id */
            int expect_nl = nollie && type < FLIP_LATE_KICK;                  /* (a late flip has no nollie version: the same id) */
            CHECK(back == type + 1 && nl == expect_nl, "flip %d (nollie %d) has id %d which maps back to flip %d (nollie %d)", type, nollie, id, back - 1, nl);
        }
    int nl;
    CHECK(ghost_flip_of_id(0, &nl) == 0 && ghost_flip_of_id(15, &nl) == 0, "a plain ollie / nollie shows no flip");
    CHECK(ghost_flip_of_id(70, &nl) == 0 && ghost_flip_of_id(-1, &nl) == 0, "a grab id is no flip");
    for (int type = 0; type < NGRIND_TYPES; type++)
        for (int bs = 0; bs < 2; bs++) {
            TrickRec r;
            memset(&r, 0, sizeof(r));
            r.cls = TRICK_GRIND; r.type = (short)type; r.bs = (signed char)bs;
            int id = goals_trick_id(&r), got_bs = -1;
            CHECK(ghost_grind_of_id(id, &got_bs) == type + 1 && got_bs == bs, "grind %d bs %d has id %d, which maps to %d bs %d", type, bs, id, ghost_grind_of_id(id, &got_bs) - 1, got_bs);
        }
    for (int type = 0; type < NGRAB_TYPES; type++) {
        TrickRec r;
        memset(&r, 0, sizeof(r));
        r.cls = TRICK_GRAB; r.type = (short)type; r.bs = -1;
        CHECK(ghost_grab_of_id(goals_trick_id(&r)) == type + 1, "grab %d has id %d which maps back to %d", type, goals_trick_id(&r), ghost_grab_of_id(goals_trick_id(&r)) - 1);
    }
    { int bs; CHECK(ghost_grind_of_id(13, &bs) == 0 && ghost_grab_of_id(36) == 0 && ghost_grab_of_id(0) == 0, "a flip id is neither a grind nor a grab"); }
}

/* ---- A2: the poses a played recording gives a rider --------------------------------------------------------------------------------------------- */
typedef struct { char magic[4]; uint32_t version, nverts, nbones, nanims, off_tex, off_verts, off_vbone, off_skel, off_anims; float scale, height; uint32_t pad[12]; } SktHdr;
static void test_poses(const GhostPak *pak, const char *dir)
{
    char path[400];
    snprintf(path, sizeof(path), "%s/skater_aaron.skt", dir);
    size_t n;
    unsigned char *b = slurp(path, &n);
    const SktHdr *h = (const SktHdr *)b;
    AnimSet as = { b, (const SkelAnim *)(b + h->off_anims), h->nanims };
    long frames = 0, bad = 0, with_flip = 0, with_bail = 0, with_air = 0, with_push = 0, with_grind = 0, with_grab = 0;
    for (uint32_t i = 0; i < pak->count; i += 3) {
        GhostTrack t;
        char lv[24];
        ghost_entry(pak, i, &t, lv);
        GhostPlayer g;
        Rider rd;
        Skate sk;
        skate_init(&sk, 0, 0, 0, 0);
        rider_init(&rd);
        ghost_init(&g, &t, 1.0f, 0.0f);
        for (float tm = 0.0f; tm < t.dur; tm += 1.0f / 30.0f) {
            ghost_update(&g, 1.0f / 30.0f, NULL, NULL);
            ghost_fill_skate(&g, &sk);
            rider_update(&rd, &sk, 1.0f / 30.0f);
            SkelFrame fr; BoardFrame bf;
            rider_pose(&rd, &sk, &as, &fr, &bf);
            frames++;
            for (int k = 0; k < 91; k++) if (!isfinite(((const float *)&fr)[k])) { bad++; break; }
            for (int k = 0; k < SKEL_NBONES; k++) {
                const float *q = fr.q[k];
                float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                if (fabsf(l - 1.0f) > 1e-2f) { bad++; break; }
            }
            if (g.flip) with_flip++;
            if (g.bail_t > 0.0f) with_bail++;
            if (!g.grounded) with_air++;
            if (g.push_t >= 0.0f) with_push++;
            if (g.grind) with_grind++;
            if (g.grab) with_grab++;
        }
    }
    CHECK(bad == 0, "%ld of %ld posed frames are broken (NaN or a quaternion that is not unit length)", bad, frames);
    CHECK(with_flip > 1000 && with_bail > 300 && with_push > 1000 && with_grind > 1000 && with_grab > 300, "flips %ld, bails %ld, pushes %ld, grinds %ld, grabs %ld frames posed", with_flip, with_bail, with_push, with_grind, with_grab);
    printf("poses: %ld frames posed (every third recording): %ld with a flip, %ld bailing, %ld pushing, %ld grinding, %ld grabbing, %ld in the air (no ground to compare)\n", frames, with_flip, with_bail, with_push, with_grind, with_grab, with_air);
    free(b);
}

/* ---- B: contests --------------------------------------------------------------------------------------------------------------------------------- */
static Score sc;
static int step_contest(Contest *k, float dt, int bails)
{
    ContestIn in; memset(&in, 0, sizeof(in));
    in.dt = dt; in.bails = bails; in.score = &sc;
    return contest_update(k, &in);
}

static int check_stage(const Stage *s, const char *what)
{
    int bad = 0;
    for (int j = 0; j < s->n; j++) {
        const GhostPlayer *g = &s->r[j].gp;
        if (!finite3(g->pos) || !finite3(g->fwd) || !finite3(g->up) || !isfinite(g->heading) || g->speed > 45.0f) bad++;
    }
    CHECK(bad == 0, "%s: a rider's state is broken", what);
    return bad;
}

static void test_contests(const Career *c, const GhostPak *pak, const char *dir)
{
    int ran = 0, with_rec = 0, skate_plays = 0;
    for (uint32_t i = 0; i < c->nchal; i++) {
        const CareerChal *ch = &c->chal[i];
        if (!(ch->mode == MODE_JAM || ch->mode == MODE_SKATE || ch->mode == MODE_BESTTRICK) || !(ch->flags & CHAL_CAREER) || !career_chal_runnable(c, (int)i)) continue;
        const char *z = NULL;
        for (uint32_t l = 0; l < c->nlevels; l++) if (c->level[l].id == ch->level) z = career_str(c, c->level[l].zip_off);
        char zip[48], path[400];
        snprintf(zip, sizeof(zip), "%s", z);
        for (char *q = zip; *q; q++) *q = (char)tolower((unsigned char)*q);
        snprintf(path, sizeof(path), "%s/%s.lvl", dir, zip);
        size_t ln;
        unsigned char *lb = slurp(path, &ln);
        const Hdr *h = (const Hdr *)lb;
        LevelObjs o;
        levelobj_init(&o, lb, ln, h->off_objs);
        coll_build(&C, (const float *)(lb + h->off_collv), h->ncv, (const CollPoly *)(lb + h->off_collp), h->ncp, 8.0f);
        static Contest k;
        static Stage st;
        char what[80];
        snprintf(what, sizeof(what), "%u %s %s", i, career_mode_name(ch->mode), zip);
        CHECK(contest_setup(&k, c, (int)i, &o), "%s: contest_setup", what);
        stage_init(&st, pak, zip);
        stage_contest_begin(&st, &k, &o, ground, NULL);
        CHECK(st.n == k.nopp, "%s: %d riders for %d opponents", what, st.n, k.nopp);
        for (int j = 0; j < st.n; j++) CHECK(st.r[j].skater == k.opp_skater[j], "%s: rider %d is skater %d", what, j, st.r[j].skater);
        score_init(&sc);
        contest_start(&k);
        const float dt = 1.0f / 30.0f;
        int res = CT_RUNNING, frames = 0, jam_off = 0, rounds_seen = 0, last_round = -1, any_track = 0;
        long keys = 0;
        float maxdiff = 0.0f;
        while (res == CT_RUNNING && frames < 30 * 600) {
            if (k.mode == MODE_SKATE) {                                  /* the player: fails the first trick, then sets a kickflip every time he can, and lets the opponent copy or not */
                if (k.state == SK_PLAYER_SETS && k.brk <= 0.0f) {
                    TrickRec r; memset(&r, 0, sizeof(r));
                    r.cls = TRICK_FLIP; r.type = FLIP_KICKFLIP; r.bs = -1;
                    memcpy(sc.bank_log, &r, sizeof(r)); sc.bank_n = 1; sc.bank_pts = 200; sc.bank_serial++;
                }
            }
            res = step_contest(&k, dt, 0);
            stage_contest_update(&st, &k, dt, ground, NULL);
            frames++;
            if (check_stage(&st, what)) break;
            for (int j = 0; j < st.n; j++) {
                const StageRider *r = &st.r[j];
                if (r->has_trk) any_track = 1;
                if (k.mode == MODE_JAM && r->has_trk && k.t < r->trk.dur - 0.5f) {
                    float d = fabsf(r->gp.time - k.t);
                    if (d > maxdiff) maxdiff = d;
                    if (d > 0.2f) jam_off++;
                }
            }
            if (k.mode == MODE_BESTTRICK && k.round != last_round) { last_round = k.round; rounds_seen++; }
            if (k.mode == MODE_SKATE) for (int j = 0; j < st.n; j++) keys += st.r[j].key >= 0 && st.r[j].has_trk;
        }
        CHECK(res != CT_RUNNING, "%s: the contest ends (%d frames)", what, frames);
        if (k.mode == MODE_JAM) CHECK(jam_off == 0 && any_track, "%s: Jam riders follow the contest clock (off by up to %.2f s, %d frames > 0.2 s)", what, maxdiff, jam_off);
        if (k.mode == MODE_BESTTRICK) CHECK(rounds_seen >= k.rounds, "%s: %d rounds seen of %d", what, rounds_seen, k.rounds);
        if (k.mode == MODE_SKATE) { CHECK(keys > 0, "%s: the opponent skates the recording of the trick he sets", what); skate_plays += keys > 0; }
        with_rec += any_track;
        ran++;
        printf("%-4u %-11s %-10s riders %d  %s  (%d frames, ended %d)\n", i, career_mode_name(ch->mode), zip, st.n, any_track ? "recordings play" : "no recordings (they stand)", frames, res);
        coll_free(&C);
        free(lb);
    }
    CHECK(ran == 24, "24 contests ran (%d)", ran);
    CHECK(with_rec >= 18, "contests with a recording on screen: %d", with_rec);
    printf("contests: %d ran, %d with recordings on screen, %d S.K.A.T.E. with a staged trick\n", ran, with_rec, skate_plays);
}

/* ---- C: ambient skaters -------------------------------------------------------------------------------------------------------------------------- */
static void test_ambient(const Career *c, const GhostPak *pak, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int levels = 0;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 5 || strcmp(e->d_name + l - 4, ".lvl") != 0) continue;
        char path[400], stem[48];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        snprintf(stem, sizeof(stem), "%.*s", (int)(l - 4), e->d_name);
        size_t ln;
        unsigned char *lb = slurp(path, &ln);
        const Hdr *h = (const Hdr *)lb;
        coll_build(&C, (const float *)(lb + h->off_collv), h->ncv, (const CollPoly *)(lb + h->off_collp), h->ncp, 8.0f);
        LevelObjs o;
        levelobj_init(&o, lb, ln, h->off_objs);
        float player[3] = { h->start[0], h->bounds[1], h->start[2] };
        levelobj_start(&o, 1, player, &(float){ 0 });
        static Stage st;
        stage_init(&st, pak, stem);
        int skaters[3] = { 1, 2, 3 };
        int n = stage_ambient_begin(&st, skaters, 3, 1234u, player, ground, NULL);
        int routes = ghost_route_count(pak, stem);
        CHECK(routes == 0 ? n == 0 : (n >= 1 && n <= 3), "%s: %d routes, %d ambient skaters started", stem, routes, n);
        if (routes == 0) { printf("%-18s no routes: nobody roams here\n", stem); coll_free(&C); free(lb); levels++; continue; }
        int near = 0, bad = 0, switches = 0, in_grounded = 0, frames = 0, too_close_start = 0;
        for (int j = 0; j < st.n; j++) if (dist2(st.r[j].gp.pos, player) < STAGE_AMBIENT_KEEP_OFF - 0.5f) near++;
        int route[3] = { st.r[0].route, st.r[1].route, st.r[2].route };
        n = st.n;
        float mn = 1e30f;
        for (int f = 0; f < 30 * 240; f++) {                                       /* four minutes, the player standing still */
            stage_ambient_update(&st, 1.0f / 30.0f, player, ground, NULL);
            frames++;
            for (int j = 0; j < st.n; j++) {
                const GhostPlayer *g = &st.r[j].gp;
                if (!finite3(g->pos) || g->pos[0] < h->bounds[0] - 50.0f || g->pos[0] > h->bounds[3] + 50.0f || g->pos[2] < h->bounds[2] - 50.0f || g->pos[2] > h->bounds[5] + 50.0f) bad++;
                if (g->grounded) in_grounded++;
                if (st.r[j].route != route[j]) {
                    switches++; route[j] = st.r[j].route;
                    if (dist2(st.r[j].gp.pos, player) < STAGE_AMBIENT_KEEP_OFF - 0.5f) too_close_start++;
                }
                float dd = dist2(g->pos, player);
                if (dd < mn) mn = dd;
            }
        }
        CHECK(near == 0, "%s: %d ambient skaters start near the player", stem, near);
        CHECK(bad == 0, "%s: %d frames with an ambient skater outside the level", stem, bad);
        CHECK(switches >= 3, "%s: only %d route changes in four minutes", stem, switches);
        (void)n;
        CHECK(too_close_start == 0, "%s: %d routes started on top of the player", stem, too_close_start);
        printf("%-18s %2d routes, 3 skaters, %2d route changes in 4 min, grounded %d%%, nearest approach to the player %.0f m\n", stem, routes, switches, 100 * in_grounded / (frames * 3), mn);
        coll_free(&C);
        free(lb);
        levels++;
    }
    closedir(d);
    CHECK(levels >= 20, "levels tested: %d", levels);
    (void)c;
}

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    tricks_defaults();
    size_t n, pn;
    unsigned char *cb = slurp(argv[1], &n);
    Career c;
    CHECK(career_load(&c, cb, n), "career.dat loads");
    char pakpath[400];
    snprintf(pakpath, sizeof(pakpath), "%s/ai.pak", argv[2]);
    unsigned char *pb = slurp(pakpath, &pn);
    GhostPak pak;
    CHECK(ghost_pak_load(&pak, pb, pn), "ai.pak loads");
    CHECK(!ghost_pak_load(&(GhostPak){ 0 }, pb, 7) && !ghost_pak_load(&(GhostPak){ 0 }, pb, pn / 2), "a cut-off pak is refused");
    test_flip_ids();
    test_recordings(&pak);
    test_poses(&pak, argv[2]);
    test_contests(&c, &pak, argv[2]);
    test_ambient(&c, &pak, argv[2]);
    printf(fails ? "%d FAILURES\n" : "all stage tests passed\n", fails);
    return fails != 0;
}
