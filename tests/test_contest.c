/* Host test: the score contests (src/contest.c) on the real data: every career Jam / Best Trick / S.K.A.T.E. challenge with its level's zones and the opponents' recorded lines / tricks (career.dat).
 * The player is faked through the Score the real game fills (the banked line), as in tests/test_goals.c.
 * usage: test_contest <career.dat> <data dir with the .lvl files> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "../src/career.h"
#include "../src/contest.h"
#include "../src/goals.h"

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

static Score sc;

static TrickRec trick(int cls, int type, int nollie, const float pos[3])
{
    TrickRec r; memset(&r, 0, sizeof(r));
    r.cls = (short)cls; r.type = (short)type; r.bs = -1; r.nollie = (unsigned char)nollie; if (pos) memcpy(r.pos, pos, sizeof(r.pos));
    return r;
}

/* the player banks a line of the given points with these tricks */
static void bank(const TrickRec *r, int n, long pts)
{
    memcpy(sc.bank_log, r, sizeof(TrickRec) * (size_t)n);
    sc.bank_n = n; sc.bank_pts = pts; sc.bank_serial++; sc.total += pts;
}

static int step(Contest *k, float dt, const float pos[3], int bails)
{
    ContestIn in; memset(&in, 0, sizeof(in));
    in.dt = dt; in.bails = bails; in.score = &sc; memcpy(in.pos, pos, sizeof(in.pos));
    return contest_update(k, &in);
}

static int run(Contest *k, float secs, const float pos[3]) { int r = CT_RUNNING; for (int i = 0; i < (int)(secs * 60.0f) && r == CT_RUNNING; i++) r = step(k, 1.0f / 60.0f, pos, 0); return r; }

static unsigned char *g_level;
static int open_contest(const Career *c, const char *dir, int chal, Contest *k)
{
    const CareerChal *ch = &c->chal[chal];
    const char *z = NULL;
    for (uint32_t l = 0; l < c->nlevels; l++) if (c->level[l].id == ch->level) z = career_str(c, c->level[l].zip_off);
    char zip[48], path[400];
    snprintf(zip, sizeof(zip), "%s", z);
    for (char *q = zip; *q; q++) *q = (char)tolower((unsigned char)*q);
    snprintf(path, sizeof(path), "%s/%s.lvl", dir, zip);
    size_t ln;
    free(g_level);
    g_level = slurp(path, &ln);
    static LevelObjs o;
    levelobj_init(&o, g_level, ln, ((const Hdr *)g_level)->off_objs);
    return contest_setup(k, c, chal, &o);
}

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    tricks_defaults();
    size_t n;
    unsigned char *cb = slurp(argv[1], &n);
    Career c;
    CHECK(career_load(&c, cb, n), "career.dat loads");
    int nj = 0, nb = 0, ns = 0;
    printf("%-4s %-24s %-11s %-10s %5s %4s %s\n", "chal", "name", "mode", "level", "limit", "opps", "opponents (recorded)");
    for (uint32_t i = 0; i < c.nchal; i++) {
        const CareerChal *ch = &c.chal[i];
        if (!(ch->mode == MODE_JAM || ch->mode == MODE_SKATE || ch->mode == MODE_BESTTRICK) || !(ch->flags & CHAL_CAREER) || !career_chal_runnable(&c, (int)i)) continue;
        const char *z = NULL;
        for (uint32_t l = 0; l < c.nlevels; l++) if (c.level[l].id == ch->level) z = career_str(&c, c.level[l].zip_off);
        char zip[48], path[400];
        snprintf(zip, sizeof(zip), "%s", z);
        for (char *q = zip; *q; q++) *q = (char)tolower((unsigned char)*q);
        snprintf(path, sizeof(path), "%s/%s.lvl", argv[2], zip);
        size_t ln;
        unsigned char *lb = slurp(path, &ln);
        const Hdr *h = (const Hdr *)lb;
        LevelObjs o;
        CHECK(levelobj_init(&o, lb, ln, h->off_objs), "%s: objects", zip);
        static Contest k;
        CHECK(contest_setup(&k, &c, (int)i, &o), "challenge %u (%s): contest_setup", i, career_str(&c, ch->name_off));
        char what[160] = "";
        if (ch->mode == MODE_JAM) {
            nj++;
            for (int j = 0; j < k.nopp; j++) {
                long fin = contest_opp_score_at(&k, j, k.limit + 5.0f), half = contest_opp_score_at(&k, j, k.limit * 0.5f);
                char b[40]; snprintf(b, sizeof(b), " %ld", fin); strcat(what, b);
                CHECK(fin > 3000 && fin < 80000 && half > 0 && half < fin, "%u: opponent %d: %ld in all, %ld at half time", i, j, fin, half);
                CHECK(contest_opp_score_at(&k, j, 0.0f) == 0, "%u: nobody has scored at the start", i);
            }
        } else if (ch->mode == MODE_BESTTRICK) {
            nb++;
            CHECK(k.rounds == ch->nobj, "%u: %d rounds for %d records", i, k.rounds, ch->nobj);
            snprintf(what, sizeof(what), "%d rounds of %.0f s", k.rounds, k.limit);
        } else {
            ns++;
            CHECK(k.nset >= 3, "%u: the opponent sets %d tricks", i, k.nset);
            for (int t = 0; t < k.nset; t++) {
                CHECK(k.set_ids[t] >= 0 && (uint32_t)k.set_ids[t] < c.ntricks, "%u: set trick %d", i, k.set_ids[t]);
                char b[40]; snprintf(b, sizeof(b), " %s", career_str(&c, c.trick[k.set_ids[t]].name_off)); strcat(what, b);
            }
        }
        printf("%-4u %-24.24s %-11s %-10.10s %5.0f %4d %s%s\n", i, career_str(&c, ch->name_off), career_mode_name(ch->mode), zip, k.limit, k.nopp, k.nzones ? "[zone] " : "", what);
        free(lb);
    }
    CHECK(nj == 8 && nb == 8 && ns == 8, "career contests: %d Jam, %d Best Trick, %d S.K.A.T.E. (expected 8 / 8 / 8)", nj, nb, ns);

    /* ---- the rules on one real challenge of each kind, with a faked player ---- */
    static Contest k;
    const float here[3] = { 0.0f, 0.0f, 0.0f };
    {   /* JAM: The Paris Connection (10), 120 s, one opponent (Lucas Puig), no zone */
        CHECK(open_contest(&c, argv[2], 10, &k) && k.mode == MODE_JAM && k.nzones == 0, "Paris Connection is a Jam without a zone");
        long opp = contest_opp_score_at(&k, 0, 1000.0f);
        score_init(&sc);
        contest_start(&k);
        TrickRec r[2] = { trick(TRICK_FLIP, FLIP_KICKFLIP, 0, here), trick(TRICK_GRIND, GRIND_5050, 0, here) };
        bank(r, 2, 500);
        step(&k, 1.0f / 60.0f, here, 0);
        CHECK(k.player_score == 500, "a banked line of 500 counts (%ld)", k.player_score);
        bank(r, 2, 9000);
        step(&k, 1.0f / 60.0f, here, 0);
        CHECK(k.player_score == 500 + CONTEST_SCORE_CAP, "a line is capped at %d like the original's (%ld)", CONTEST_SCORE_CAP, k.player_score);
        int res = run(&k, 130.0f, here);
        CHECK(res == CT_LOST && k.place == 2, "4596 points against %ld: lost (result %d)", opp, res);
        score_init(&sc);
        contest_start(&k); k.result = CT_RUNNING; k.t = 0; k.player_score = 0;
        for (int i = 0; i < 12; i++) { bank(r, 2, 4000); step(&k, 1.0f / 60.0f, here, 0); }
        res = run(&k, 130.0f, here);
        CHECK(res == CT_WON && k.place == 1 && k.player_score == 48000, "48000 points: won (%ld, result %d, opponent %ld)", k.player_score, res, k.opp_score[0]);
        CHECK(k.t > 119.0f, "a Jam lasts its whole time limit (%.1f s)", k.t);
    }
    {   /* a zone: Brazilian Girl (25) at Rio counts only lines with a trick in its zone */
        CHECK(open_contest(&c, argv[2], 25, &k) && k.nzones >= 1, "Brazilian Girl has a zone (%d)", k.nzones);
        score_init(&sc);
        contest_start(&k);
        const float inside[3] = { (k.zlo[0][0] + k.zhi[0][0]) * 0.5f, (k.zlo[0][1] + k.zhi[0][1]) * 0.5f, (k.zlo[0][2] + k.zhi[0][2]) * 0.5f }, outside[3] = { k.zhi[0][0] + 80.0f, k.zhi[0][1], k.zhi[0][2] + 80.0f };
        TrickRec r1[1] = { trick(TRICK_FLIP, FLIP_KICKFLIP, 0, outside) }, r2[2] = { trick(TRICK_FLIP, FLIP_KICKFLIP, 0, outside), trick(TRICK_GRIND, GRIND_5050, 0, inside) };
        bank(r1, 1, 700); step(&k, 1.0f / 60.0f, here, 0);
        CHECK(k.player_score == 0, "a line outside the zone is worth nothing (%ld)", k.player_score);
        bank(r2, 2, 900); step(&k, 1.0f / 60.0f, here, 0);
        CHECK(k.player_score == 900, "a line with a trick in the zone counts (%ld)", k.player_score);
    }
    {   /* BEST TRICK: Community Pride (33), 3 rounds of 90 s, three opponents with recorded rounds */
        CHECK(open_contest(&c, argv[2], 33, &k) && k.mode == MODE_BESTTRICK && k.rounds == 3 && k.nopp == 3, "Community Pride: %d rounds, %d opponents", k.rounds, k.nopp);
        score_init(&sc);
        k.nzones = 0;                                                 /* (the zones are tested above) */
        contest_start(&k);
        TrickRec r[1] = { trick(TRICK_FLIP, FLIP_HEELFLIP, 0, here) };
        bank(r, 1, 800); step(&k, 1.0f / 60.0f, here, 0);
        bank(r, 1, 1500); step(&k, 1.0f / 60.0f, here, 0);
        bank(r, 1, 600); step(&k, 1.0f / 60.0f, here, 0);
        CHECK(k.player_round == 1500, "the round's best line is what counts (%ld)", k.player_round);
        run(&k, 91.0f, here);
        CHECK(k.round == 1 && k.round_score[0] == 1500 && k.player_score == 1500 && k.brk > 0.0f, "round 1 ends: %ld (round %d, break %.1f)", k.player_score, k.round, k.brk);
        CHECK(k.opp_round[0] > 1000 && k.opp_round[0] < 2000, "the opponents' recorded best of round 1: %ld %ld %ld", k.opp_round[0], k.opp_round[1], k.opp_round[2]);
        bank(r, 1, 3000); step(&k, 1.0f / 60.0f, here, 0);
        CHECK(k.player_round == 0 && k.player_score == 1500, "nothing counts during the break");
        run(&k, 4.0f, here);                                           /* the break is over */
        bank(r, 1, 2500); step(&k, 1.0f / 60.0f, here, 0);
        run(&k, 91.0f, here);
        bank(r, 1, 4000); step(&k, 1.0f / 60.0f, here, 0);
        int res = run(&k, 95.0f, here);
        CHECK(res == CT_WON && k.player_score == 1500 + 2500 + 4000 && k.round == 3, "three rounds: 8000 beats the recorded opponents (%ld against %ld %ld %ld, result %d)", k.player_score, k.opp_score[0], k.opp_score[1], k.opp_score[2], res);
        /* a player who never banks anything loses */
        score_init(&sc);
        contest_start(&k); k.result = CT_RUNNING; k.round = 0; k.t = 0; k.brk = 0; k.player_score = 0; k.player_round = 0; memset(k.opp_score, 0, sizeof(k.opp_score));
        res = 0;
        for (int i = 0; i < 400 && res == CT_RUNNING; i++) res = run(&k, 5.0f, here);
        CHECK(res == CT_LOST && k.place == 4, "no lines at all: last place (%d, place %d)", res, k.place);
        /* the 20 s five-round contest has no recordings: made-up rounds that are the same every time */
        CHECK(open_contest(&c, argv[2], 21, &k) && k.rounds == 5 && k.limit == 20.0f, "Plan B: %d rounds of %.0f s", k.rounds, k.limit);
        score_init(&sc); contest_start(&k);
        run(&k, 21.0f, here);
        long a = k.opp_round[0];
        open_contest(&c, argv[2], 21, &k); score_init(&sc); contest_start(&k); run(&k, 21.0f, here);
        CHECK(a == k.opp_round[0] && a >= 700 && a < 1600, "made-up opponent round: %ld, the same on a second run", a);
    }
    {   /* S.K.A.T.E.: EduSKATE (5) against Sammy, difficulty 1 */
        CHECK(open_contest(&c, argv[2], 5, &k) && k.mode == MODE_SKATE && k.limit == 10.0f && k.nset >= 5, "EduSKATE: 10 s attempts, %d set tricks", k.nset);
        CHECK(k.set_ids[0] == 13 && k.set_ids[1] == 15, "his first tricks: Kickflip, Nollie (%d, %d)", k.set_ids[0], k.set_ids[1]);
        k.nzones = 0;
        score_init(&sc);
        contest_start(&k);
        CHECK(k.state == SK_OPP_SETS && k.target == 13 && k.letters_player == 0, "he sets a Kickflip");
        TrickRec kick[1] = { trick(TRICK_FLIP, FLIP_KICKFLIP, 0, here) }, heel[1] = { trick(TRICK_FLIP, FLIP_HEELFLIP, 0, here) };
        bank(heel, 1, 100); step(&k, 1.0f / 60.0f, here, 0);
        CHECK(k.letters_player == 1 && k.msg == CT_MSG_MISSED && k.state == SK_PLAYER_SETS, "a heelflip is not a kickflip: S (%d)", k.letters_player);
        run(&k, 4.0f, here);
        bank(kick, 1, 150); step(&k, 1.0f / 60.0f, here, 0);
        CHECK(k.state == SK_OPP_SETS && k.target == 15 && (k.msg == CT_MSG_OPP_MATCHED || k.msg == CT_MSG_OPP_MISSED), "the player sets a kickflip, the opponent answers (%d); next he sets %d", k.msg, k.target);
        run(&k, 4.0f, here);
        step(&k, 1.0f / 60.0f, here, 1);
        CHECK(k.letters_player == 2 && k.state == SK_PLAYER_SETS, "a bail while matching: K");
        run(&k, 4.0f, here);
        step(&k, 1.0f / 60.0f, here, 1);
        CHECK(k.letters_player == 2 && k.msg == CT_MSG_SET_FAILED && k.state == SK_OPP_SETS, "a bail while setting costs no letter, the lead passes");
        /* never landing anything: the 10 s run out each time, 5 letters lose */
        score_init(&sc);
        contest_start(&k); k.result = CT_RUNNING; k.letters_player = 0; k.letters_opp = 0;
        int res = CT_RUNNING;
        for (int i = 0; i < 200 && res == CT_RUNNING; i++) res = run(&k, 6.0f, here);
        CHECK(res == CT_LOST && k.letters_player == 5 && k.place == 2, "doing nothing: five letters, lost (%d letters, result %d)", k.letters_player, res);
        /* always landing what he sets and setting easy tricks: he runs out of luck sooner or later */
        int wins = 0;
        for (int game = 0; game < 40; game++) {
            open_contest(&c, argv[2], 5, &k); k.nzones = 0; k.rng += (unsigned)game * 977u;
            score_init(&sc); contest_start(&k);
            res = CT_RUNNING;
            for (int i = 0; i < 400 && res == CT_RUNNING; i++) {
                if (k.brk <= 0.0f && k.state == SK_OPP_SETS) {                /* copy exactly what he set */
                    TrickRec t2[1] = { trick(TRICK_FLIP, k.target == 15 ? TREC_OLLIE : (k.target == 0 ? TREC_OLLIE : FLIP_KICKFLIP), k.target == 15, here) };
                    if (k.target == 0 || k.target == 15 || k.target == 13) { bank(t2, 1, 100); }
                    else { TrickRec t3[1] = { trick(TRICK_FLIP, FLIP_HEELFLIP, k.target > 14, here) }; bank(t3, 1, 100); }
                } else if (k.brk <= 0.0f && k.state == SK_PLAYER_SETS) {
                    TrickRec t2[1] = { trick(TRICK_FLIP, FLIP_KICKFLIP, 0, here) };
                    bank(t2, 1, 150);
                }
                res = run(&k, 1.0f / 6.0f, here);
            }
            wins += res == CT_WON;
        }
        CHECK(wins >= 25, "a player who lands everything wins %d of 40 games", wins);
    }
    free(g_level);
    printf(fails ? "%d FAILURES\n" : "all contest tests passed\n", fails);
    return fails != 0;
}
