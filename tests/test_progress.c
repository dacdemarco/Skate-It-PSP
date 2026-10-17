/* Host test: the career progression and the profile file (src/progress.c) against the real data/career.dat (the ROM's pool and unlock-set tables).
 * usage: test_progress <career.dat> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/progress.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz); if (fread(b, 1, (size_t)sz, f) != (size_t)sz) exit(2); fclose(f);
    *n = (size_t)sz;
    return b;
}

static unsigned rng_state = 1;
static unsigned rng(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

static int in_pool(const Career *c, int pool, int chal)
{
    for (int k = 0; k < c->pool[pool].nchal; k++) if (c->pool[pool].chal[k] == chal) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    size_t n;
    unsigned char *cb = slurp(argv[1], &n);
    Career c;
    CHECK(career_load(&c, cb, n), "career.dat loads");
    CHECK(c.npools == 19 && c.nsets == 118 && c.nsetentries == 684, "%u pools, %u sets, %u entries", c.npools, c.nsets, c.nsetentries);
    { int req = 0; for (uint32_t i = 0; i < c.npools; i++) req += c.pool[i].required; CHECK(req == 49, "the pools ask for %d completions in all (the ROM: 49)", req); }
    CHECK(!career_load(&c, cb, 100), "a short file is refused");
    { unsigned char bad[128]; memcpy(bad, cb, 128); bad[4] = 1; Career c2; CHECK(!career_load(&c2, bad, n), "version 1 is refused"); }
    career_load(&c, cb, n);
    for (uint32_t i = 0; i < c.nchal; i++) {                     /* the pool lists agree with the challenge table */
        int pools = 0;
        for (uint32_t pl = 0; pl < c.npools; pl++) if (in_pool(&c, (int)pl, (int)i)) { pools++; CHECK(c.chal[i].pool == pl, "challenge %u: table says pool %d, the pool list says %u", i, c.chal[i].pool, pl); }
        CHECK(pools <= 1 && (pools == 1 || c.chal[i].pool == 0xFF), "challenge %u is in %d pools (table pool %d)", i, pools, c.chal[i].pool);
    }
    CHECK(c.chal[21].reward == 51 && c.chal[0].reward == 30 && c.chal[93].reward == CAREER_NO_SET && c.chal[88].reward == CAREER_NO_SET, "reward sets: %d %d %d", c.chal[21].reward, c.chal[0].reward, c.chal[93].reward);

    /* ---- a new profile ---- */
    Profile p;
    profile_init(&p, &c, "TEST");
    CHECK(strcmp(p.name, "TEST") == 0 && progress_pool(&p, &c) == 0 && !progress_finished(&p, &c), "new profile starts in pool 0");
    CHECK(progress_unlocked(&p, UNLOCK_SKATER, 12) && progress_unlocked(&p, UNLOCK_SKATER, 2) && progress_unlocked(&p, UNLOCK_SKATER, 11) && !progress_unlocked(&p, UNLOCK_SKATER, 0), "starting skaters: Aaron, Rob Dyrdek, Mike (not Danny Way)");
    { int open[32], k = progress_open_list(&p, &c, open, 32);
      CHECK(k == 5 && open[0] == 93 && open[4] == 97, "pool 0 opens the five tutorials (%d open)", k);
      CHECK(!progress_chal_open(&p, &c, 0), "the first pool's challenges are closed until the tutorials are done");
      CHECK(!progress_chal_open(&p, &c, 98) && !progress_chal_open(&p, &c, -1) && !progress_chal_open(&p, &c, 500), "extra tutorials / bad indices are not open"); }

    /* ---- the first steps in detail ---- */
    ProgressEvent ev;
    for (int i = 93; i <= 96; i++) { CHECK(progress_complete(&p, &c, i, &ev) == 1 && ev.pool_after == 0, "tutorial %d: the pool stays", i); }
    CHECK(progress_complete(&p, &c, 93, &ev) == 0 && !ev.newly_done, "a challenge counts once");
    CHECK(progress_pool_done(&p, &c, 0) == 4, "4 of 5 tutorials done");
    CHECK(progress_complete(&p, &c, 97, &ev) == 1 && ev.pool_before == 0 && ev.pool_after == 1 && ev.unlocked_total >= 7, "the fifth tutorial opens pool 1 (%d -> %d, %d unlocks)", ev.pool_before, ev.pool_after, ev.unlocked_total);
    { int open[32], k = progress_open_list(&p, &c, open, 32);
      CHECK(k == 7 && progress_chal_open(&p, &c, 0) && progress_chal_open(&p, &c, 1) && progress_chal_open(&p, &c, 8), "pool 1 opens seven challenges (%d)", k);
      CHECK(!progress_chal_open(&p, &c, 4) && !progress_chal_open(&p, &c, 6) && !progress_chal_unlocked(&p, 4), "Filming 101 and Stay On Course wait behind their predecessors");
      CHECK(!progress_chal_open(&p, &c, 9), "the next pool's challenge is closed"); }
    CHECK(progress_complete(&p, &c, 1, &ev) == 1 && progress_chal_open(&p, &c, 4) && !progress_chal_open(&p, &c, 1), "challenge 1 done: Filming 101 (4) opens, 1 closes");
    CHECK(progress_complete(&p, &c, 2, &ev) == 1 && progress_chal_open(&p, &c, 6), "challenge 2 done: Stay On Course (6) opens");
    progress_complete(&p, &c, 0, &ev); progress_complete(&p, &c, 4, &ev);
    CHECK(ev.pool_after == 2 && progress_chal_open(&p, &c, 9) && !progress_chal_open(&p, &c, 3), "four of pool 1 done: pool 2 (Pop Quiz) is current, the rest of pool 1 is closed (pool %d)", ev.pool_after);
    CHECK(progress_pool_done(&p, &c, 1) == 4, "pool 1 counts 4");

    /* ---- sponsors: the pool of board sponsors needs one completion and keeps its sponsor ---- */
    {
        Profile q;
        profile_init(&q, &c, "S");
        for (uint32_t i = 0; i < c.npools; i++) {                /* up to pool 6 by raw flags (no unlock side effects), then through the engine */
            if (i >= 6) break;
            for (int k = 0; k < c.pool[i].nchal; k++) q.done[c.pool[i].chal[k]] = 1;
        }
        progress_complete(&q, &c, 99, NULL);                     /* any completion re-evaluates: the start sets of the pools passed are applied */
        CHECK(progress_pool(&q, &c) == 6 && q.pools_started == 7, "the pools passed have started (current %d, started %d)", progress_pool(&q, &c), q.pools_started);
        CHECK(progress_unlocked(&q, UNLOCK_SKATER, 14) && progress_unlocked(&q, UNLOCK_SKATER, 1) && progress_unlocked(&q, UNLOCK_SKATER, 7) && !progress_unlocked(&q, UNLOCK_SKATER, 8), "the start sets of the pools passed unlocked Emo Crys, Terry, Jake Brown (not Shingo yet)");
        int before = progress_unlocked(&q, UNLOCK_DECK, 4);                                     /* Almost's first deck (the table order of the ARM9: generic 0 - 3, then Almost 4 - 9) */
        CHECK(!before && progress_unlocked(&q, UNLOCK_DECK, 0), "no board sponsor yet: the generic decks only");
        CHECK(progress_chal_open(&q, &c, 21) && progress_chal_open(&q, &c, 27), "all seven board sponsor challenges are open");
        CHECK(progress_complete(&q, &c, 22, &ev) == 1 && q.sponsor[0] == 23 && ev.pool_before == 6 && ev.pool_after == 7, "Almost chosen: sponsor[0] = %d, pool %d -> %d", q.sponsor[0], ev.pool_before, ev.pool_after);
        CHECK(progress_unlocked(&q, UNLOCK_DECK, 4) && progress_unlocked(&q, UNLOCK_DECK, 9) && !progress_unlocked(&q, UNLOCK_DECK, 10) && progress_unlocked(&q, UNLOCK_WHEELS, 4) && progress_unlocked(&q, UNLOCK_HAT, 1), "Almost's gear is unlocked, nobody else's");
        CHECK(!progress_chal_open(&q, &c, 21), "the other sponsors close with the pool");
        /* a pool that is complete by its flags but has no sponsor stays current (the original's rule) */
        Profile r = q; r.sponsor[0] = 0; r.pools_started = 7;
        CHECK(progress_pool(&r, &c) == 6, "all required done but no sponsor chosen: the pool stays current (%d)", progress_pool(&r, &c));
    }

    /* ---- the whole campaign, headless: complete open challenges in random order until it is finished ---- */
    int worst_open = 1000, best_total = 0, worst_total = 1000;
    for (unsigned seed = 1; seed <= 300; seed++) {
        rng_state = seed * 7919u;
        Profile s;
        profile_init(&s, &c, "SIM");
        int steps = 0, req = 0;
        for (uint32_t i = 0; i < c.npools; i++) req += c.pool[i].required;
        while (!progress_finished(&s, &c) && steps < 400) {
            int open[64], k = progress_open_list(&s, &c, open, 64);
            if (k == 0) { CHECK(0, "seed %u: the career is stuck in pool %d after %d completions", seed, progress_pool(&s, &c), progress_total_done(&s)); break; }
            if (k < worst_open) worst_open = k;
            int pick = open[rng() % (unsigned)k], pool = progress_pool(&s, &c);
            CHECK(in_pool(&c, pool, pick), "seed %u: challenge %d is open but not in pool %d", seed, pick, pool);
            ProgressEvent e;
            CHECK(progress_complete(&s, &c, pick, &e) == 1, "seed %u: completing %d", seed, pick);
            CHECK(e.pool_after >= e.pool_before, "pools never go back");
            steps++;
        }
        CHECK(progress_finished(&s, &c), "seed %u: finished", seed);
        CHECK(steps == req || (steps == req + 1 && s.done[82]), "seed %u: finished after %d completions, the pools ask for %d (one more when the bonus A Piece Of China, which has no sponsor, was played before the shoe sponsor)", seed, steps, req);
        if (steps > best_total) best_total = steps;
        if (steps < worst_total) worst_total = steps;
        for (uint32_t i = 0; i < c.nskaters && i < CAREER_NRIDERS; i++)
            if (seed == 1) CHECK(progress_unlocked(&s, UNLOCK_SKATER, (int)i), "skater %u is unlocked at the end of the career", i);
        CHECK(progress_open_list(&s, &c, (int[1]){ 0 }, 1) == 0, "nothing is open at the end");
    }
    printf("headless campaigns: 300 random orders, all finished in %d completions (fewest open challenges seen at once: %d)\n", worst_total, worst_open);

    /* ---- the campaign needs the opponent modes: which pools cannot be finished with the modes the game can run today ---- */
    {
        Profile s;
        profile_init(&s, &c, "RUN");
        int stuck_pool = -1, steps = 0;
        while (!progress_finished(&s, &c) && steps < 400) {
            int open[64], k = progress_open_list(&s, &c, open, 64), pick = -1;
            for (int i = 0; i < k && pick < 0; i++)
                if (career_chal_runnable(&c, open[i])) pick = open[i];
            if (pick < 0) { stuck_pool = progress_pool(&s, &c); break; }
            progress_complete(&s, &c, pick, NULL);
            steps++;
        }
        printf("with the modes the game can run today (everything the career asks for) it finishes: stuck in pool %d after %d completions\n", stuck_pool, steps);
        CHECK(stuck_pool == -1 && steps == 49 && progress_finished(&s, &c), "the whole career can be played: finished after %d completions (stuck in pool %d)", steps, stuck_pool);
    }

    /* ---- the file ---- */
    {
        Profile s;
        profile_init(&s, &c, "FILE TEST");
        for (int i = 93; i <= 97; i++) progress_complete(&s, &c, i, NULL);
        progress_complete(&s, &c, 1, NULL);
        s.skater = 5; s.stat[STAT_PLAY_TIME] = 12345; s.sponsor[2] = 48;
        uint8_t buf[2048];
        size_t sz = profile_save(&s, buf, sizeof(buf));
        CHECK(sz == profile_size() && sz > 600 && sz < 1024, "profile size %zu", sz);
        CHECK(profile_save(&s, buf, sz - 1) == 0, "a small buffer is refused");
        Profile t;
        CHECK(profile_load(&t, &c, buf, sz) == 1 && memcmp(&s, &t, sizeof(s)) == 0, "save / load round trip is exact");
        CHECK(progress_pool(&t, &c) == progress_pool(&s, &c) && progress_chal_open(&t, &c, 4), "state survives the file");
        CHECK(profile_load(&t, &c, buf, sz - 1) == 0 && profile_load(&t, &c, buf, sz + 1) == 0 && profile_load(&t, &c, buf, 0) == 0, "wrong sizes are refused");
        int accepted = 0;
        for (size_t i = 0; i < sz; i++) {                         /* every single flipped byte is caught by the checksum */
            uint8_t bad[2048]; memcpy(bad, buf, sz); bad[i] ^= 0x5A;
            Profile u; accepted += profile_load(&u, &c, bad, sz);
        }
        CHECK(accepted == 0, "%d corrupted files were accepted", accepted);
        { uint8_t bad[2048]; memcpy(bad, buf, sz); memset(bad + 8, 'A', 16);                          /* an unterminated name is made safe (the checksum is stale, so this is refused too) */
          CHECK(profile_load(&t, &c, bad, sz) == 0, "edited file refused"); }
        Profile keep = s; keep.stat[0] = 1;
        profile_load(&keep, &c, buf, sz - 4);
        CHECK(keep.stat[0] == 1, "a refused load leaves the profile alone");
    }

    /* ---- the old career.sav ---- */
    {
        uint8_t old[8 + 128];
        memset(old, 0, sizeof(old));
        memcpy(old, "SKSV", 4); old[4] = 1;
        for (int i = 93; i <= 97; i++) old[8 + i] = 1;
        old[8 + 15] = 1;                                         /* Book It! done out of order in the sandbox */
        Profile m;
        CHECK(profile_import_legacy(&m, &c, old, sizeof(old), "OLD") == 1, "the legacy file imports");
        CHECK(progress_pool(&m, &c) == 1 && progress_chal_done(&m, 15) && progress_chal_done(&m, 97) && m.stat[STAT_CHALLENGES] == 6, "its flags are kept; tutorials done = pool 1 is current (%d)", progress_pool(&m, &c));
        old[0] = 'X';
        CHECK(profile_import_legacy(&m, &c, old, sizeof(old), "OLD") == 0 && profile_import_legacy(&m, &c, old, 10, "OLD") == 0, "bad legacy files are refused");
    }

    printf(fails ? "%d FAILURES\n" : "all progress tests passed\n", fails);
    return fails != 0;
}
