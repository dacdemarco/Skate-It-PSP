/* Host test: the statistics and the trick book (src/stats.c) driven by hand-made rider states, and the profile file that carries them (version 3, and the older versions).
 * usage: test_stats <career.dat> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/stats.h"

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

static uint32_t crc32(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); }
    return ~c;
}

static void frames(Profile *p, StatState *st, const Skate *s, const Score *sc, float secs)
{
    for (float t = 0; t < secs - 1e-6f; t += 1.0f / 60.0f) stats_frame(p, st, s, sc, 1.0f / 60.0f);
}

int main(int argc, char **argv)
{
    (void)argc;
    size_t n;
    unsigned char *cb = slurp(argv[1], &n);
    Career c;
    CHECK(career_load(&c, cb, n), "career.dat loads");

    /* ---- the trick book's bits ---- */
    CHECK(STATS_NTRICKS == 81 && STATS_NTRICKS <= PROFILE_NLANDED * 8, "%d tricks fit in the %d flag bytes", STATS_NTRICKS, PROFILE_NLANDED);
    {
        int seen[STATS_NTRICKS];
        memset(seen, 0, sizeof(seen));
        for (int cls = 0; cls < 5; cls++)
            for (int t = 0; t < 40; t++) {
                int b = stats_landed_bit(cls, t);
                if (b < 0) continue;
                CHECK(b < STATS_NTRICKS && !seen[b], "bit %d is used once", b);
                seen[b] = 1;
                int c2, t2;
                stats_bit_trick(b, &c2, &t2);
                CHECK(c2 == cls && t2 == t, "bit %d is (%d, %d) and back (%d, %d)", b, cls, t, c2, t2);
            }
        int all = 1;
        for (int b = 0; b < STATS_NTRICKS; b++) all &= seen[b];
        CHECK(all, "every bit belongs to a trick");
        CHECK(stats_landed_bit(5, 0) == -1 && stats_landed_bit(0, NGRIND_TYPES) == -1 && stats_landed_bit(-1, 0) == -1 && stats_landed_bit(2, -1) == -1, "bad tricks have no bit");
    }

    /* ---- play: time, distances, the personal bests ---- */
    static Profile p;
    profile_init(&p, &c, "STATS");
    static Skate s;
    static Score sc;
    memset(&s, 0, sizeof(s)); memset(&sc, 0, sizeof(sc));
    StatState st;
    stats_begin(&st, &sc);
    s.grounded = 1; s.speed = 6.0f; s.push_t = -1.0f;
    frames(&p, &st, &s, &sc, 10.0f);
    CHECK(p.stat[STAT_PLAY_TIME] >= 9 && p.stat[STAT_PLAY_TIME] <= 10, "10 s of play: %u s", p.stat[STAT_PLAY_TIME]);
    CHECK(abs((int)p.stat[STAT_DIST] - 6000) <= 20, "6 m/s for 10 s = 60 m (%u cm)", p.stat[STAT_DIST]);
    CHECK(p.stat[STAT_DIST_GRIND] == 0 && p.stat[STAT_BEST_GRIND] == 0 && p.stat[STAT_BEST_AIR] == 0, "no grind, no air yet");
    frames(&p, &st, &s, &sc, 0.0f);
    s.grounded = 0; s.vel[0] = 3.0f; s.vel[2] = 4.0f; s.air_t = 0.0f;                   /* a jump: 0.8 s in the air at 5 m/s along the ground */
    for (int i = 0; i < 48; i++) { s.air_t = (float)i / 60.0f; stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f); }
    CHECK(p.stat[STAT_BEST_AIR] >= 770 && p.stat[STAT_BEST_AIR] <= 800, "best airtime in ms: %u", p.stat[STAT_BEST_AIR]);
    s.grab = 3; for (int i = 0; i < 30; i++) stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f); s.grab = 0;
    CHECK(p.stat[STAT_AIRGRAB_TIME] == 0 || p.stat[STAT_AIRGRAB_TIME] == 1, "half a second of grab is not yet a whole one (%u)", p.stat[STAT_AIRGRAB_TIME]);
    s.grab = 3; for (int i = 0; i < 60; i++) stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f); s.grab = 0;
    CHECK(p.stat[STAT_AIRGRAB_TIME] >= 1, "the grab time adds up (%u s)", p.stat[STAT_AIRGRAB_TIME]);
    s.air_t = 0.3f;
    for (int i = 0; i < 10; i++) stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    CHECK(p.stat[STAT_BEST_AIR] >= 770, "a shorter hop does not lower the best (%u)", p.stat[STAT_BEST_AIR]);
    s.grounded = 0; s.grinding = 1; s.speed = 4.0f;                                           /* a grind of 2 s at 4 m/s = 8 m */
    frames(&p, &st, &s, &sc, 2.0f);
    CHECK(abs((int)p.stat[STAT_DIST_GRIND] - 800) <= 10 && abs((int)p.stat[STAT_BEST_GRIND] - 800) <= 10, "an 8 m grind: total %u cm, longest %u cm", p.stat[STAT_DIST_GRIND], p.stat[STAT_BEST_GRIND]);
    s.grinding = 0; s.grounded = 1;
    frames(&p, &st, &s, &sc, 0.1f);
    s.grounded = 0; s.grinding = 1;
    frames(&p, &st, &s, &sc, 1.0f);
    CHECK(abs((int)p.stat[STAT_DIST_GRIND] - 1200) <= 15 && abs((int)p.stat[STAT_BEST_GRIND] - 800) <= 10, "a 4 m grind adds to the total (%u) but not to the longest (%u)", p.stat[STAT_DIST_GRIND], p.stat[STAT_BEST_GRIND]);
    s.grinding = 0; s.grounded = 1; s.manual = 1; s.speed = 5.0f;
    frames(&p, &st, &s, &sc, 3.0f);
    CHECK(abs((int)p.stat[STAT_DIST_MANUAL] - 1500) <= 15 && abs((int)p.stat[STAT_BEST_MANUAL] - 1500) <= 15, "a 15 m manual (%u, %u)", p.stat[STAT_DIST_MANUAL], p.stat[STAT_BEST_MANUAL]);
    s.manual = 0; s.sliding = 1; s.speed = 3.0f;
    frames(&p, &st, &s, &sc, 1.0f);
    CHECK(abs((int)p.stat[STAT_DIST_SLIDE] - 300) <= 5 && abs((int)p.stat[STAT_BEST_SLIDE] - 300) <= 5, "a 3 m powerslide (%u, %u)", p.stat[STAT_DIST_SLIDE], p.stat[STAT_BEST_SLIDE]);
    s.sliding = 0;

    /* ---- pushes and wipeouts count once each ---- */
    s.speed = 0.0f;
    s.push_t = 0.0f; stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    s.push_t = 0.1f; stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    s.push_t = -1.0f; stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    s.push_t = 0.0f; stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    s.push_t = -1.0f;
    CHECK(p.stat[STAT_PUSHES] == 2, "two pushes (%u)", p.stat[STAT_PUSHES]);
    s.bail_t = 1.5f; for (int i = 0; i < 5; i++) { stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f); s.bail_t -= 0.1f; }
    s.bail_t = 0.0f; stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    s.bail_t = 1.5f; stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    s.bail_t = 0.0f;
    CHECK(p.stat[STAT_BAILS] == 2, "two wipeouts (%u)", p.stat[STAT_BAILS]);

    /* ---- a banked combo: its tricks are landed, the points a candidate for the best sequence ---- */
    CHECK(stats_landed_count(&p) == 0 && p.stat[STAT_TRICKS] == 0, "nothing landed yet");
    sc.bank_serial = 1; sc.bank_n = 4; sc.bank_pts = 1800;
    sc.bank_log[0] = (TrickRec){ .cls = TRICK_FLIP, .type = TREC_OLLIE };
    sc.bank_log[1] = (TrickRec){ .cls = TRICK_FLIP, .type = FLIP_KICKFLIP };
    sc.bank_log[2] = (TrickRec){ .cls = TRICK_GRIND, .type = GRIND_5050, .bs = 1 };
    sc.bank_log[3] = (TrickRec){ .cls = TRICK_MOVE, .type = MOVE_BS_SPIN };
    stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    CHECK(stats_landed(&p, TRICK_FLIP, FLIP_KICKFLIP) && stats_landed(&p, TRICK_GRIND, GRIND_5050) && stats_landed(&p, TRICK_MOVE, MOVE_BS_SPIN) && !stats_landed(&p, TRICK_FLIP, FLIP_HEELFLIP), "the combo's tricks are in the book");
    CHECK(stats_landed_count(&p) == 3 && p.stat[STAT_TRICKS] == 3 && p.stat[STAT_BEST_LINE] == 1800, "three tricks (the ollie is not one), best line 1800: %d %u %u", stats_landed_count(&p), p.stat[STAT_TRICKS], p.stat[STAT_BEST_LINE]);
    stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    CHECK(p.stat[STAT_TRICKS] == 3, "the same bank is counted once");
    sc.bank_serial = 2; sc.bank_n = 1; sc.bank_pts = 900;
    sc.bank_log[0] = (TrickRec){ .cls = TRICK_FLIP, .type = FLIP_KICKFLIP };
    stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    CHECK(p.stat[STAT_TRICKS] == 4 && stats_landed_count(&p) == 3 && p.stat[STAT_BEST_LINE] == 1800, "a repeat counts as a trick but not as a new one in the book; a smaller line is not the best");
    sc.bank_serial = 0;                                                                        /* a new Score (a new event) starts counting from zero again */
    sc.bank_n = 1; sc.bank_log[0] = (TrickRec){ .cls = TRICK_GRAB, .type = GRAB_MELON };
    stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    CHECK(stats_landed(&p, TRICK_GRAB, GRAB_MELON), "after a reset of the score it still notices a bank");
    stats_begin(&st, &sc);
    stats_frame(&p, &st, &s, &sc, 1.0f / 60.0f);
    CHECK(p.stat[STAT_TRICKS] == 5, "stats_begin starts from the score as it is");
    stats_frame(&p, &st, &s, &sc, 0.0f);

    /* ---- the file: version 3 carries all of it ---- */
    uint8_t buf[2048];
    size_t sz = profile_save(&p, buf, sizeof(buf));
    Profile q;
    CHECK(sz == profile_size() && profile_load(&q, &c, buf, sz) == 1 && memcmp(&p, &q, sizeof(p)) == 0, "save / load keeps the statistics and the book (%zu bytes)", sz);
    CHECK(q.landed[0] == p.landed[0] && stats_landed_count(&q) == 4, "the trick book survives (%d)", stats_landed_count(&q));
    {   /* the custom skater: 16 bytes in version 5, 8 in version 4 (mapped onto the new fields), none before */
        Profile cp = p;
        cp.has_custom = 1; cp.skater = PROFILE_CUSTOM_SKATER;
        cp.custom = (CustomSel){ .gender = 1, .build = 2, .skin = 3, .face = 5, .hair = 7, .hair_col = 1, .beard = 0, .beard_col = 2, .hat = 4, .hat_tex = 11, .top = 30, .logo = 40, .pants = 20, .shoes = 11 };
        uint8_t fbuf[2048];
        size_t cs = profile_save(&cp, fbuf, sizeof(fbuf));
        Profile cq;
        CHECK(profile_load(&cq, &c, fbuf, cs) == 1 && cq.has_custom && cq.skater == PROFILE_CUSTOM_SKATER && memcmp(&cq.custom, &cp.custom, sizeof(CustomSel)) == 0, "the custom skater survives the file");
        uint8_t bad[2048];
        memcpy(bad, fbuf, cs); bad[cs - 4 - 16] = 2;                                                /* gender 2: the checksum is stale, so it is refused anyway */
        CHECK(profile_load(&cq, &c, bad, cs) == 0, "a damaged choice is refused");
        Profile nc = cp;
        nc.has_custom = 0;
        cs = profile_save(&nc, fbuf, sizeof(fbuf));
        CHECK(profile_load(&cq, &c, fbuf, cs) == 0, "skater 100 without a custom skater is refused");
        nc.has_custom = 1; nc.custom.build = 3;
        cs = profile_save(&nc, fbuf, sizeof(fbuf));
        CHECK(profile_load(&cq, &c, fbuf, cs) == 0, "a choice out of range is refused (build 3)");
        nc.custom = cp.custom; nc.custom.gender = 0; nc.custom.hat = 3;                          /* a man in a woman's cap */
        cs = profile_save(&nc, fbuf, sizeof(fbuf));
        CHECK(profile_load(&cq, &c, fbuf, cs) == 0, "a hat that does not exist for the gender is refused");
        nc.custom = cp.custom; nc.custom.hair = 10;
        cs = profile_save(&nc, fbuf, sizeof(fbuf));
        CHECK(profile_load(&cq, &c, fbuf, cs) == 0, "hair 10 is refused");
        /* a version 4 file: the file of the first editor, 8 bytes of choice: gender, build, skin, face texture, built-in hair, top, pants, shoes */
        cs = profile_save(&cp, fbuf, sizeof(fbuf));
        uint8_t v4[2048];
        size_t body = cs - 4 - 17;
        memcpy(v4, fbuf, body); v4[4] = 4;
        size_t o4 = body;
        v4[o4++] = 1;
        { const uint8_t old[8] = { 1, 2, 3, 1, 7, 30, 20, 11 }; memcpy(v4 + o4, old, 8); o4 += 8; }
        uint32_t crc4 = crc32(v4, o4);
        for (int i = 0; i < 4; i++) v4[o4++] = (uint8_t)(crc4 >> (8 * i));
        CHECK(profile_load(&cq, &c, v4, o4) == 1 && cq.has_custom && cq.custom.gender == 1 && cq.custom.build == 2 && cq.custom.skin == 3 && cq.custom.face == 1 && cq.custom.hair == 3 && cq.custom.hair_col == 2 &&
              cq.custom.top == 30 && cq.custom.pants == 20 && cq.custom.shoes == 11 && cq.custom.hat == 0 && cq.custom.beard == 0 && cq.custom.logo == 0, "a version 4 file's skater is mapped onto the new fields (hair 7 -> style 3, brown)");
        /* a version 3 file: no custom skater at all */
        uint8_t v3[2048];
        memcpy(v3, fbuf, body); v3[4] = 3; v3[8 + 16] = 12;                                           /* (and Aaron rides: a file of that version has no custom skater) */
        uint32_t crc = crc32(v3, body);
        size_t o3 = body;
        for (int i = 0; i < 4; i++) v3[o3++] = (uint8_t)(crc >> (8 * i));
        Profile p0 = p;
        cs = profile_save(&p0, fbuf, sizeof(fbuf));
        CHECK(profile_load(&cq, &c, v3, o3) == 1 && !cq.has_custom && cq.stat[STAT_BEST_AIR] == p.stat[STAT_BEST_AIR], "a version 3 file loads without a custom skater");
    }
    {   /* a version 2 file (8 statistics, story flags, no book) and a version 1 file load with what they have */
        uint8_t v[2048];
        size_t o = 0;
        memcpy(v, buf, 8 + 16 + 4 + 8); o = 8 + 16 + 4 + 8;                                       /* header, name, skater / pools, sponsors */
        memcpy(v + o, buf + o, 8 * 4); o += 8 * 4;                                                /* the first 8 statistics */
        size_t rest = 128 + 512;
        memcpy(v + o, buf + 8 + 16 + 4 + 8 + PROFILE_NSTAT * 4, rest); o += rest;                 /* done, unlocked */
        size_t before_seen = o;
        memcpy(v + o, p.seen, PROFILE_NSEEN); o += PROFILE_NSEEN;
        v[4] = 2;
        uint32_t crc = crc32(v, o);
        for (int i = 0; i < 4; i++) v[o++] = (uint8_t)(crc >> (8 * i));
        Profile r;
        CHECK(profile_load(&r, &c, v, o) == 1 && r.stat[STAT_PLAY_TIME] == p.stat[STAT_PLAY_TIME] && r.stat[STAT_BEST_AIR] == 0 && stats_landed_count(&r) == 0, "a version 2 file loads: the first statistics kept, the new ones and the book empty");
        o = before_seen; v[4] = 1;
        crc = crc32(v, o);
        for (int i = 0; i < 4; i++) v[o++] = (uint8_t)(crc >> (8 * i));
        CHECK(profile_load(&r, &c, v, o) == 2 && r.stat[STAT_PLAY_TIME] == p.stat[STAT_PLAY_TIME], "a version 1 file loads and says so");
        CHECK(!profile_load(&r, &c, v, o - 1) && !profile_load(&r, &c, buf, sz - 1), "a wrong size is refused");
    }
    {
        int accepted = 0;
        for (size_t i = 0; i < sz; i++) { uint8_t bad[2048]; memcpy(bad, buf, sz); bad[i] ^= 0x5A; Profile u; accepted += profile_load(&u, &c, bad, sz); }
        CHECK(accepted == 0, "%d corrupted files were accepted", accepted);
    }

    printf(fails ? "%d FAILURES\n" : "all stats tests passed\n", fails);
    return fails != 0;
}
