/* Host test: the career data (src/career.c on data/career.dat), the mapping of our tricks onto the original's trick ids, and the goal rules (src/goals.c) on real challenges of the real levels.
 * usage: test_goals <career.dat> <amphitheatre.lvl> <elementaryschool.lvl> <paris.lvl> <commcentre.lvl>
 * The scenarios fake the Score (the banked combo's trick log) and the rider's frames: what is tested is how the ROM's objective records are read, not the physics. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "../src/career.h"
#include "../src/goals.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
    uint32_t nrails, nrailpts, off_rails, off_polymat, off_railids, off_objs, off_paths, pad;
} Hdr;

typedef struct { unsigned char *b; size_t n; LevelObjs o; const uint16_t *ids; uint32_t nrails; } Lvl;

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(sz); if (fread(b, 1, sz, f) != (size_t)sz) exit(2); fclose(f);
    *n = (size_t)sz;
    return b;
}

static void lvl_open(Lvl *l, const char *path)
{
    l->b = slurp(path, &l->n);
    const Hdr *h = (const Hdr *)l->b;
    if (!levelobj_init(&l->o, l->b, l->n, h->off_objs)) { printf("no objects in %s\n", path); exit(2); }
    levelobj_init_paths(&l->o, l->b, l->n, h->off_paths);
    l->ids = (const uint16_t *)(l->b + h->off_railids);
    l->nrails = h->nrails;
}

static void upper(char *d, const char *s) { while (*s) *d++ = (char)toupper((unsigned char)*s++); *d = 0; }

/* ---- a fake rider ------------------------------------------------------------------------------- */
static Score sc;
static Goals g;
static float t_now;

static void frame(int grounded, int grinding, float x, float y, float z, int pushes)
{
    GoalIn in; memset(&in, 0, sizeof(in));
    in.dt = 1.0f / 60; in.grounded = grounded; in.grinding = grinding; in.pos[0] = x; in.pos[1] = y; in.pos[2] = z; in.pushes = pushes; in.score = &sc;
    in.air_t = 0.0f;
    if (!grounded) { in.air_t = 0.5f; }
    goals_update(&g, &in);
    t_now += in.dt;
}

static void run_secs(float s, int grounded, float x, float y, float z) { for (int i = 0; i < (int)(s * 60); i++) frame(grounded, 0, x, y, z, 0); }

static TrickRec rec(int cls, int type, int bs, int nollie, int deg, int flight, const float pos[3])
{
    TrickRec r; memset(&r, 0, sizeof(r));
    r.cls = (short)cls; r.type = (short)type; r.bs = (signed char)bs; r.nollie = (unsigned char)nollie; r.deg = (short)deg; r.flight = flight;
    if (pos) { r.pos[0] = pos[0]; r.pos[1] = pos[1]; r.pos[2] = pos[2]; }
    return r;
}

/* the combo banks: the log, the points, and the total */
static void bank(const TrickRec *r, int n, long pts)
{
    memcpy(sc.bank_log, r, sizeof(TrickRec) * (size_t)n);
    sc.bank_n = n; sc.bank_pts = pts; sc.bank_serial++; sc.total += pts;
}

static void start(Lvl *l, const Career *c, int chal)
{
    score_init(&sc);
    goals_init(&g, c, &l->o, l->ids, l->nrails);
    if (!goals_begin(&g, chal, &sc)) { printf("FAIL: challenge %d cannot begin\n", chal); fails++; return; }
    run_secs(3.1f, 1, 0, 0, 0);                                   /* the countdown */
}

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    tricks_defaults();
    size_t csz; unsigned char *cb = slurp(argv[1], &csz);
    Career c;
    CHECK(career_load(&c, cb, csz), "career.dat loads");
    CHECK(c.nchal == 103 && c.nobj == 216 && c.ntricks == 101 && c.nlevels == 21, "counts %u %u %u %u", c.nchal, c.nobj, c.ntricks, c.nlevels);
    CHECK(!career_load(&c, cb, 40), "a short file is refused");
    { unsigned char bad[64]; memcpy(bad, cb, 64); bad[0] = 'X'; Career c2; CHECK(!career_load(&c2, bad, csz), "bad magic is refused"); }
    career_load(&c, cb, csz);
    CHECK(strcmp(career_str(&c, c.chal[15].name_off), "Book It!") == 0, "challenge 15 is '%s'", career_str(&c, c.chal[15].name_off));
    int amph = career_level_by_stem(&c, "amphitheatre");
    CHECK(amph >= 0 && career_level_by_stem(&c, "AMPHITHEATRE") == amph && career_level_by_stem(&c, "nowhere") == -1, "level by stem");
    int at[32], n = career_challenges_at(&c, amph, at, 32);
    int has15 = 0, has18 = 0; for (int i = 0; i < n; i++) { has15 |= at[i] == 15; has18 |= at[i] == 18; if (i) CHECK(c.chal[at[i - 1]].slot <= c.chal[at[i]].slot, "challenges sorted by slot"); }
    CHECK(has15 && has18, "the library has Book It! and Where's Jerry? (%d challenges)", n);

    /* ---- the trick mapping: our table against the original's names ---- */
    int bad = 0;
    for (int t = 0; t < NFLIP_TYPES; t++)
        for (int nol = 0; nol < 2; nol++) {
            TrickRec r = rec(TRICK_FLIP, t, -1, nol, 0, 0, NULL);
            int id = goals_trick_id(&r);
            if (t == FLIP_BACKFLIP || t == FLIP_FRONTFLIP) { if (id != -1) { printf("  body flip %d has id %d\n", t, id); bad++; } continue; }
            if (id < 0 || id >= (int)c.ntricks) { printf("  flip %d has id %d\n", t, id); bad++; continue; }
            if (nol && t >= FLIP_LATE_KICK) continue;
            char a[64], b[64], full[80];
            upper(a, tricks.flip[t].name); snprintf(full, sizeof(full), "%s%s", nol ? "NOLLIE " : "", a);
            upper(b, career_str(&c, c.trick[id].name_off));
            if (strcmp(full, b) != 0) { printf("  flip %d nollie %d: ours '%s' = id %d '%s'\n", t, nol, full, id, b); bad++; }
        }
    for (int t = 0; t < NGRAB_TYPES; t++) {
        TrickRec r = rec(TRICK_GRAB, t, -1, 0, 0, 0, NULL);
        int id = goals_trick_id(&r);
        char a[64], b[64]; upper(a, tricks.grab[t].name);
        if (id < 70 || id > 98) { printf("  grab %d id %d\n", t, id); bad++; continue; }
        upper(b, career_str(&c, c.trick[id].name_off));
        if (strcmp(a, b) != 0) { printf("  grab %d: ours '%s' = id %d '%s'\n", t, a, id, b); bad++; }
    }
    for (int t = 0; t < NGRIND_TYPES; t++)
        for (int bs = 0; bs < 2; bs++) {
            TrickRec r = rec(TRICK_GRIND, t, bs, 0, 0, 0, NULL);
            int id = goals_trick_id(&r);
            if (id < 36 || id > 69) { printf("  grind %d id %d\n", t, id); bad++; continue; }
            char a[64], b[64], full[80]; upper(a, tricks.grind[t].name);
            if (strcmp(a, "NOSESLIDE") == 0) strcpy(a, "NOSE"); else if (strcmp(a, "TAILSLIDE") == 0) strcpy(a, "TAIL");
            snprintf(full, sizeof(full), "%s %s", bs ? "BS" : "FS", a);
            upper(b, career_str(&c, c.trick[id].name_off));
            if (strcmp(full, b) != 0) { printf("  grind %d bs %d: ours '%s' = id %d '%s'\n", t, bs, full, id, b); bad++; }
        }
    { TrickRec o = rec(TRICK_FLIP, TREC_OLLIE, -1, 0, 0, 0, NULL), nl = rec(TRICK_FLIP, TREC_OLLIE, -1, 1, 0, 0, NULL), mn = rec(TRICK_MANUAL, 0, -1, 0, 0, 0, NULL), nm = rec(TRICK_MANUAL, 1, -1, 0, 0, 0, NULL), sp = rec(TRICK_MOVE, MOVE_BS_SPIN, -1, 0, 360, 0, NULL);
      CHECK(goals_trick_id(&o) == 0 && goals_trick_id(&nl) == 15 && goals_trick_id(&mn) == 100 && goals_trick_id(&nm) == 99 && goals_trick_id(&sp) == -1, "ollie / nollie / manual / spin ids"); }
    CHECK(bad == 0, "%d tricks whose original id or name does not fit", bad);

    /* ---- classes ---- */
    { TrickRec k = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 0, NULL), v = rec(TRICK_FLIP, FLIP_VARIAL_KICK, -1, 0, 0, 0, NULL), ol = rec(TRICK_FLIP, TREC_OLLIE, -1, 0, 0, 0, NULL),
             g5 = rec(TRICK_GRIND, GRIND_5050, 1, 0, 0, 0, NULL), ng = rec(TRICK_GRIND, GRIND_NOSEGRIND, 1, 0, 0, 0, NULL), fv = rec(TRICK_GRIND, GRIND_50, 0, 0, 0, 0, NULL),
             gb = rec(TRICK_GRAB, GRAB_MELON, -1, 0, 0, 0, NULL), gm = rec(TRICK_GRAB, GRAB_METHOD, -1, 0, 0, 0, NULL);
      CHECK((goals_trick_classes(&k) & 0x1) && !(goals_trick_classes(&k) & 0x4) && (goals_trick_classes(&v) & 0x4), "flip classes");
      CHECK(goals_trick_classes(&ol) == 0, "an ollie is in no class");
      CHECK((goals_trick_classes(&g5) & 0x8) && !(goals_trick_classes(&g5) & 0x400), "50-50 is a basic grind");
      CHECK((goals_trick_classes(&ng) & 0x80) && (goals_trick_classes(&ng) & 0x400) && !(goals_trick_classes(&ng) & 0x100), "nosegrind: front trucks, advanced");
      CHECK((goals_trick_classes(&fv) & 0x100) && !(goals_trick_classes(&fv) & 0x80), "5-0: back trucks");
      CHECK(!(goals_trick_classes(&gb) & 0x200) && (goals_trick_classes(&gm) & 0x200) && (goals_trick_classes(&gm) & 0x20), "grab classes"); }

    Lvl la, le, lp, lc;
    lvl_open(&la, argv[2]); lvl_open(&le, argv[3]); lvl_open(&lp, argv[4]); lvl_open(&lc, argv[5]);

    /* ---- Book It! (15, Photo): grind a rail (line 171) and get 600 points ---- */
    int rail171 = -1; for (uint32_t i = 0; i < la.nrails; i++) if (la.ids[i] == 171) rail171 = (int)i;
    CHECK(rail171 >= 0 && goals_supported(&(Goals){ .career = &c, .objs = &la.o }, 15), "Book It! is runnable and rail 171 exists");
    start(&la, &c, 15);
    CHECK(g.phase == GOAL_ACTIVE && g.nobj == 2 && (g.ch->flags & CHAL_LINE), "Book It!: active, 2 objectives, a line challenge (phase %d)", g.phase);
    { float p[3] = { 0, 0, 0 }; TrickRec r[1]; r[0] = rec(TRICK_GRIND, GRIND_5050, 1, 0, 0, 1, p); r[0].rail = (unsigned short)(rail171 + 1);
      r[0].rail = (unsigned short)((rail171 == 0 ? 2 : 1)); bank(r, 1, 700); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_ACTIVE, "a grind on another rail does not do it");
      r[0].rail = (unsigned short)(rail171 + 1); bank(r, 1, 500); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_ACTIVE, "rail 171 with only 500 points does not do it");
      bank(r, 1, 650); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_WON, "rail 171 and 650 points: done (phase %d)", g.phase); }

    /* ---- Spin Doctor (16, Photo): a flip trick and a grab with a 360 spin ---- */
    start(&la, &c, 16);
    { TrickRec r[3]; r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_MOVE, MOVE_BS_SPIN, -1, 0, 360, 1, NULL); r[2] = rec(TRICK_GRAB, GRAB_MELON, -1, 0, 0, 1, NULL);
      bank(r, 2, 400); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "no grab: not done");
      r[1] = rec(TRICK_GRAB, GRAB_MELON, -1, 0, 0, 1, NULL); r[2] = rec(TRICK_MOVE, MOVE_BS_SPIN, -1, 0, 180, 1, NULL); bank(r, 3, 400); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_ACTIVE, "only a 180: not done");
      r[2].deg = 360; bank(r, 3, 400); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_WON, "flip + grab + a 360 in one line: done (phase %d, bank_n %d, serial %u seen %u, nobj %d, done %d %d)", g.phase, sc.bank_n, sc.bank_serial, g.seen_bank, g.nobj, g.done[0], g.done[1]); }

    /* ---- Where's Jerry? (18, Own The Spot, zone 1, 350..1000) ---- */
    start(&la, &c, 18);
    CHECK(g.own_target == 675 && g.zone_mask & 2u, "target %ld (expected 675), zone 1 present %d", g.own_target, (g.zone_mask & 2u) != 0);
    { const GoalBox *z = &g.zone[1]; float in_[3] = { (z->lo[0] + z->hi[0]) / 2, z->lo[1], (z->lo[2] + z->hi[2]) / 2 }, out_[3] = { z->hi[0] + 30, z->lo[1], z->hi[2] + 30 };
      TrickRec r[1]; r[0] = rec(TRICK_GRIND, GRIND_5050, 1, 0, 0, 1, out_);
      bank(r, 1, 900); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "a big line away from the spot does not count");
      r[0] = rec(TRICK_GRIND, GRIND_5050, 1, 0, 0, 1, in_); bank(r, 1, 600); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "in the spot but only 600");
      bank(r, 1, 700); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "in the spot with 700: owned"); }

    /* ---- Hit The Books (31, Film, 3000 points in 30 s) ---- */
    start(&la, &c, 31);
    CHECK(g.timed && fabsf(g.time_left - 30.0f) < 0.1f && !(g.ch->flags & CHAL_LINE), "Film: timed, independent objectives");
    { TrickRec r[1]; r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL);
      bank(r, 1, 1500); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE && g.prog[0] == 1500, "half way: %d", g.prog[0]);
      run_secs(10, 1, 0, 0, 0); bank(r, 1, 1600); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "3100 points in time: done"); }
    start(&la, &c, 31);
    run_secs(30.5f, 1, 0, 0, 0);
    CHECK(g.phase == GOAL_LOST, "no points in 30 s: lost (phase %d)", g.phase);
    frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_LOST, "stays lost");
    { Score s2; score_init(&s2); goals_init(&g, &c, &la.o, la.ids, la.nrails); CHECK(goals_begin(&g, 31, &s2) && g.phase == GOAL_COUNTDOWN_PHASE, "begin: countdown");
      CHECK(goals_supported(&g, 5), "S.K.A.T.E. games are runnable now"); CHECK(!goals_supported(&g, 88), "a My Spot challenge (no objectives) is not"); CHECK(!goals_supported(&g, 999), "no such challenge"); }

    /* ---- No Picnic (0, Photo, ElementarySchool): an ollie over the picnic table (zone 1) ---- */
    CHECK(goals_supported(&(Goals){ .career = &c, .objs = &le.o }, 0), "No Picnic is runnable");
    start(&le, &c, 0);
    { const GoalBox *z = &g.zone[1];
      float cx = (z->lo[0] + z->hi[0]) / 2, cz = (z->lo[2] + z->hi[2]) / 2, y = z->lo[1] + 0.5f;
      /* a jump that does not go over the table */
      sc.flight = 1; frame(0, 0, cx + 20, y, cz, 0); frame(0, 0, cx + 21, y, cz, 0); frame(1, 0, cx + 22, y, cz, 0);
      TrickRec r[1]; r[0] = rec(TRICK_FLIP, TREC_OLLIE, -1, 0, 0, 1, NULL); bank(r, 1, 0); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_ACTIVE, "an ollie somewhere else is not enough");
      /* one over it */
      sc.flight = 2; frame(0, 0, cx - 3, y, cz, 0); frame(0, 0, cx, y, cz, 0); frame(0, 0, cx + 3, y, cz, 0); frame(1, 0, cx + 5, y, cz, 0);
      r[0] = rec(TRICK_FLIP, TREC_OLLIE, -1, 0, 0, 2, NULL); bank(r, 1, 0); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_WON, "an ollie over the table: done (phase %d)", g.phase); }

    /* ---- That's So Cliche (23, Photo, Paris): a 360 flip over the gap (zone 1) and landing in zone 2 ---- */
    CHECK(goals_supported(&(Goals){ .career = &c, .objs = &lp.o }, 23), "Cliche is runnable");
    start(&lp, &c, 23);
    CHECK((g.zone_mask & 6u) == 6u, "zones 1 and 2 present");
    { const GoalBox *z1 = &g.zone[1], *z2 = &g.zone[2];
      float p1[3] = { (z1->lo[0] + z1->hi[0]) / 2, z1->lo[1] + 1.0f, (z1->lo[2] + z1->hi[2]) / 2 }, p2[3] = { (z2->lo[0] + z2->hi[0]) / 2, z2->lo[1], (z2->lo[2] + z2->hi[2]) / 2 }, far[3] = { z1->hi[0] + 40, z1->lo[1], z1->hi[2] + 40 };
      TrickRec r[1];
      sc.flight = 1; frame(0, 0, p1[0], p1[1], p1[2], 0); frame(1, 0, far[0], far[1], far[2], 0);          /* over the gap but lands far away */
      r[0] = rec(TRICK_FLIP, FLIP_360_FLIP, -1, 0, 0, 1, NULL); bank(r, 1, 300); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_ACTIVE, "over the gap but landing elsewhere: not done");
      sc.flight = 2; frame(0, 0, p1[0], p1[1], p1[2], 0); frame(0, 0, p2[0], p2[1] + 0.5f, p2[2], 0); frame(1, 0, p2[0], p2[1], p2[2], 0);        /* over zone 1, lands in zone 2 */
      r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 2, NULL); bank(r, 1, 300); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_ACTIVE, "a kickflip is not a 360 flip");
      r[0] = rec(TRICK_FLIP, FLIP_360_FLIP, -1, 0, 0, 2, NULL); bank(r, 1, 300); frame(1, 0, 0, 0, 0, 0);
      CHECK(g.phase == GOAL_WON, "360 flip over zone 1, landed in zone 2: done (phase %d)", g.phase); }

    /* ---- Flickit! (93, Tutorial, CommCentre): five tricks in any order, one at a time ---- */
    start(&lc, &c, 93);
    CHECK(!(g.ch->flags & CHAL_LINE) && g.nobj == 5, "the tutorial is not a line: %d objectives", g.nobj);
    { int order[5][3] = { { TRICK_FLIP, FLIP_HEELFLIP, 0 }, { TRICK_FLIP, TREC_OLLIE, 0 }, { TRICK_FLIP, FLIP_FS_POP_SHUVIT, 0 }, { TRICK_FLIP, FLIP_KICKFLIP, 0 }, { TRICK_FLIP, FLIP_POP_SHUVIT, 0 } };
      for (int i = 0; i < 5; i++) {
          TrickRec r[1]; r[0] = rec(order[i][0], order[i][1], -1, 0, 0, i + 1, NULL);
          bank(r, 1, 50); frame(1, 0, 0, 0, 0, 0);
          CHECK((g.phase == GOAL_WON) == (i == 4), "after trick %d: phase %d", i + 1, g.phase);
      } }

    /* ---- Grinds (96): two grinds, any, one per combo is fine; Pushing (94) counts pushes ---- */
    start(&lc, &c, 96);
    { TrickRec r[1]; r[0] = rec(TRICK_GRIND, GRIND_5050, 1, 0, 0, 1, NULL);
      bank(r, 1, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "one grind of two");
      bank(r, 1, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "two grinds: done"); }
    start(&lc, &c, 94);
    CHECK(g.nobj == 2 && g.need[0] == 4, "pushing: %d objectives, wants %d pushes", g.nobj, g.need[0]);
    for (int i = 0; i < 4; i++) frame(1, 0, 0, 0, 0, 1);
    CHECK(g.done[0] == 1, "4 pushes counted");

    /* ---- Manuals (98, Tutorial advanced): a flip trick INTO a manual (mod 0x10), then a flip trick OUT of it (mod 0x80), in one line ---- */
    start(&lc, &c, 98);
    CHECK((g.ch->flags & CHAL_LINE) && g.nobj == 2, "manuals is a line of two objectives (flags %d, %d objectives)", g.ch->flags, g.nobj);
    {
        TrickRec r[4];
        r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_FLIP, FLIP_HEELFLIP, -1, 0, 0, 1, NULL);
        bank(r, 2, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "two flips with no manual: not done");
        r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_MANUAL, 0, -1, 0, 0, 1, NULL);
        bank(r, 2, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "a flip into a manual, no flip out: not done");
        r[0] = rec(TRICK_MANUAL, 0, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL);
        bank(r, 2, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "a flip out of a manual with none into it: not done");
        r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_MANUAL, 0, -1, 0, 0, 1, NULL); r[2] = rec(TRICK_GRAB, GRAB_MELON, -1, 0, 0, 2, NULL);
        bank(r, 3, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "flip, manual, grab: the grab is not a flip out");
        r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_MANUAL, 0, -1, 0, 0, 1, NULL); r[2] = rec(TRICK_FLIP, FLIP_HEELFLIP, -1, 0, 0, 2, NULL);
        bank(r, 3, 200); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "flip, manual, flip: done (phase %d)", g.phase);
    }
    start(&lc, &c, 98);
    {   /* a spin between the manual and the flip does not hide the manual; the ollie is no flip trick; the nose manual counts as a manual */
        TrickRec r[5];
        r[0] = rec(TRICK_FLIP, FLIP_360_FLIP, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_MANUAL, 1, -1, 0, 0, 1, NULL); r[2] = rec(TRICK_MOVE, MOVE_BS_SPIN, -1, 0, 180, 2, NULL); r[3] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 2, NULL);
        bank(r, 4, 300); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "flip, nose manual, spin, flip: done (phase %d)", g.phase);
    }
    start(&lc, &c, 98);
    {
        TrickRec r[3];
        r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_MANUAL, 0, -1, 0, 0, 1, NULL); r[2] = rec(TRICK_FLIP, TREC_OLLIE, -1, 0, 0, 2, NULL);
        bank(r, 3, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "flip, manual, plain ollie: an ollie is no flip trick");
    }

    /* ---- the rest of the tutorials: each can be finished with the tricks its text asks for, and not with the wrong ones ---- */
    start(&lc, &c, 97);                                                                              /* Nollies: a plain ollie is not a nollie */
    { TrickRec r[1]; r[0] = rec(TRICK_FLIP, TREC_OLLIE, -1, 0, 0, 1, NULL); bank(r, 1, 0); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "a plain ollie is not a nollie");
      r[0] = rec(TRICK_FLIP, TREC_OLLIE, -1, 1, 0, 2, NULL); bank(r, 1, 0); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "a nollie: done"); }
    start(&lc, &c, 99);                                                                              /* Advanced flips: varial kick, 360 flip, nollie inward heel (any order) */
    { TrickRec r[1]; int order[3][2] = { { FLIP_INWARD_HEEL, 1 }, { FLIP_VARIAL_KICK, 0 }, { FLIP_360_FLIP, 0 } };
      for (int i = 0; i < 3; i++) {
          r[0] = rec(TRICK_FLIP, order[i][0], -1, order[i][1], 0, i + 1, NULL); bank(r, 1, 200); frame(1, 0, 0, 0, 0, 0);
          CHECK((g.phase == GOAL_WON) == (i == 2), "advanced flips: after %d, phase %d", i + 1, g.phase);
      } }
    start(&lc, &c, 100);                                                                             /* Advanced grinds: front trucks (nosegrind) and back trucks (5-0) */
    { TrickRec r[1];
      r[0] = rec(TRICK_GRIND, GRIND_5050, 1, 0, 0, 1, NULL); bank(r, 1, 50); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE && !g.done[0] && !g.done[1], "a 50-50 is no advanced grind");
      r[0] = rec(TRICK_GRIND, GRIND_NOSEGRIND, 1, 0, 0, 2, NULL); bank(r, 1, 50); frame(1, 0, 0, 0, 0, 0); CHECK(g.done[0] && !g.done[1], "a nosegrind is the front trucks");
      r[0] = rec(TRICK_GRIND, GRIND_FEEBLE, 0, 0, 0, 3, NULL); bank(r, 1, 50); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "a feeble grind is the back trucks: done"); }
    start(&lc, &c, 101);                                                                             /* Advanced grabs: not a melon, a nosebone */
    { TrickRec r[1];
      r[0] = rec(TRICK_GRAB, GRAB_MELON, -1, 0, 0, 1, NULL); bank(r, 1, 50); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "a melon is a basic grab");
      r[0] = rec(TRICK_GRAB, GRAB_NOSEBONE, -1, 0, 0, 2, NULL); bank(r, 1, 50); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "a nosebone is an advanced grab"); }
    start(&lc, &c, 102);                                                                             /* Lines: two tricks and 400 points in one line */
    { TrickRec r[3];
      r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL); bank(r, 1, 450); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "one trick is no line");
      r[1] = rec(TRICK_GRIND, GRIND_5050, 1, 0, 0, 1, NULL); bank(r, 2, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_ACTIVE, "a line of two worth 100 is not enough");
      bank(r, 2, 450); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "a line of two worth 450: done (phase %d)", g.phase); }
    start(&lc, &c, 95);                                                                              /* Air & grabs: some air, a grab, and a 180 spin with a trick */
    { TrickRec r[2];
      run_secs(0.1f, 0, 0, 1, 0); for (int i = 0; i < 70; i++) { GoalIn in; memset(&in, 0, sizeof(in)); in.dt = 1.0f / 60; in.grounded = 0; in.air_t = 1.0f; in.score = &sc; goals_update(&g, &in); }
      run_secs(0.1f, 1, 0, 0, 0);
      CHECK(g.done[0], "more than a second of air");
      r[0] = rec(TRICK_GRAB, GRAB_BS_GRAB, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_MOVE, MOVE_BS_SPIN, -1, 0, 180, 1, NULL);
      bank(r, 2, 100); frame(1, 0, 0, 0, 0, 0); CHECK(g.phase == GOAL_WON, "a grab with a 180 spin completes the grab and the spin objectives (phase %d, done %d %d %d)", g.phase, g.done[0], g.done[1], g.done[2]); }

    /* ---- Speed School (19, Checkpoint at the Elementary School): the race inside the goal system ---- */
    {
        CHECK(goals_supported(&(Goals){ .career = &c, .objs = &le.o }, 19) && career_chal_runnable(&c, 19), "a Checkpoint challenge is runnable");
        start(&le, &c, 19);
        CHECK(g.is_race && g.timed && g.nobj == 0 && g.race.ngates == 6 && g.race.nopp == 1 && fabsf(g.time_left - 12.0f) < 0.2f, "speed school: race=%d gates %d, opponents %d, clock %.1f", g.is_race, g.race.ngates, g.race.nopp, g.time_left);
        CHECK(g.have_start && g.have_beam, "its start and its beam are in the level");
        int gates_seen = 0, last_next = 0;
        float bonus_seen = 0.0f;
        for (int pass = 0; pass < 3; pass++) {                       /* 0: fast, 1: slow (the clock runs out), 2: a bail on the way */
            start(&le, &c, 19);
            float v = pass == 1 ? 3.0f : 8.0f, dist = 0.0f, p[3], tn[3];
            int steps = 0, bailed = 0;
            gates_seen = 0; last_next = 0;
            while (g.phase == GOAL_ACTIVE && steps++ < 60 * 200) {
                dist += v / 60.0f;
                race_path_at(&g.race.course, dist < g.race.course.len ? dist : g.race.course.len, p, tn);
                if (dist > g.race.course.len) for (int k = 0; k < 3; k++) p[k] += tn[k] * (dist - g.race.course.len);
                float before = g.time_left;
                GoalIn in; memset(&in, 0, sizeof(in));
                in.dt = 1.0f / 60; in.grounded = 1; in.pos[0] = p[0]; in.pos[1] = p[1]; in.pos[2] = p[2]; in.score = &sc;
                if (pass == 2 && !bailed && dist > 60.0f) { in.bails = 1; bailed = 1; }
                goals_update(&g, &in);
                if (g.race.next > last_next) { gates_seen++; last_next = g.race.next; if (g.time_left > before) bonus_seen = g.time_left - before + 1.0f / 60; }
            }
            if (pass == 0) CHECK(g.phase == GOAL_WON && g.race.place == 1 && gates_seen == 6, "a fast run wins (phase %d, place %d, gates %d, %.1f s)", g.phase, g.race.place, gates_seen, g.t);
            if (pass == 1) CHECK(g.phase == GOAL_LOST && gates_seen < 6, "a slow run loses (phase %d, gates %d)", g.phase, gates_seen);
            if (pass == 2) CHECK(g.phase == GOAL_WON && fabsf(g.race.penalty - RACE_BAIL_PENALTY) < 0.01f, "a bail costs %.1f s (phase %d)", g.race.penalty, g.phase);
        }
        CHECK(bonus_seen > 6.5f && bonus_seen < 7.5f, "a gate gives its 7 s (%.2f)", bonus_seen);
    }

    /* ---- a Jam through the goal system: Paris Connection (10) ---- */
    {
        start(&lp, &c, 10);
        CHECK(g.is_contest && g.timed && g.nobj == 0 && fabsf(g.time_left - 120.0f) < 0.3f && g.contest.mode == MODE_JAM, "Paris Connection: a contest with a 120 s clock (%.1f)", g.time_left);
        TrickRec r[2]; memset(r, 0, sizeof(r));
        r[0] = rec(TRICK_FLIP, FLIP_KICKFLIP, -1, 0, 0, 1, NULL); r[1] = rec(TRICK_GRIND, GRIND_5050, 1, 0, 0, 1, NULL);
        for (int i = 0; i < 8; i++) { bank(r, 2, 4000); for (int k = 0; k < 30; k++) frame(1, 0, 0, 0, 0, 0); }
        CHECK(g.phase == GOAL_ACTIVE && g.contest.player_score == 32000, "32000 banked, still playing (%ld, phase %d)", g.contest.player_score, g.phase);
        for (int i = 0; i < 60 * 130 && g.phase == GOAL_ACTIVE; i++) frame(1, 0, 0, 0, 0, 0);
        CHECK(g.phase == GOAL_WON, "more than the recorded opponent's %ld after the time limit: won (phase %d)", g.contest.opp_score[0], g.phase);
        start(&lp, &c, 10);
        for (int i = 0; i < 60 * 130 && g.phase == GOAL_ACTIVE; i++) frame(1, 0, 0, 0, 0, 0);
        CHECK(g.phase == GOAL_LOST, "no lines: lost (phase %d)", g.phase);
    }

    /* ---- the modifier is read only on the objectives that carry it: Flippin' Mannies (61: three advanced flips into manuals) ---- */
    { const CareerChal *fm = &c.chal[61]; const CareerObj *o = career_objs(&c, fm);
      CHECK(fm->nobj >= 1 && o[0].mod == 0x10, "challenge 61 asks for flips into manuals (mod 0x%x)", o[0].mod); }

    /* ---- how many career challenges can be run? ---- */
    { int run = 0, total = 0; Goals gg; goals_init(&gg, &c, &la.o, la.ids, la.nrails);
      for (uint32_t i = 0; i < c.nchal; i++) if (c.chal[i].flags & CHAL_CAREER) total++;
      /* (only the library's beams exist in this object table, so count by mode and objective types) */
      for (uint32_t i = 0; i < c.nchal; i++) {
          const CareerChal *ch = &c.chal[i];
          if (!(ch->flags & CHAL_CAREER)) continue;
          if (career_chal_runnable(&c, (int)i)) run++;
      }
      printf("%d of %d career challenges are in modes the goal system runs\n", run, total); }

    printf(fails ? "%d FAILURES\n" : "all goal tests passed\n", fails);
    return fails != 0;
}
