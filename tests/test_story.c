/* Host test: the career's story (src/story.c, the story section of data/career.dat): the scenes, which one is due when, that every scene can be seen, and the old profile files.
 * usage: test_story <career.dat> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/story.h"

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

static uint32_t crc32(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); }
    return ~c;
}

/* shows everything that is due, in order; records the scenes in `log` (n entries so far in *nlog); returns how many items were shown */
static int show_all(Profile *p, const Career *c, int *log, int *nlog, int *kinds)
{
    StoryItem it;
    int n = 0;
    while (story_next(p, c, &it) && n < 100) {
        CHECK(!progress_seen(p, it.bit), "an item that is due is not shown yet");
        if (it.kind == STORY_SCENE) log[(*nlog)++] = it.scene;
        else if (it.kind == STORY_SIGNED) log[(*nlog)++] = 1000 + it.slot;
        else if (it.kind == STORY_ENDING) log[(*nlog)++] = 2000;
        else if (it.kind == STORY_CREDITS) log[(*nlog)++] = 3000;
        if (kinds) kinds[it.kind]++;
        story_done(p, &it);
        n++;
    }
    return n;
}

int main(int argc, char **argv)
{
    (void)argc;
    size_t n;
    unsigned char *cb = slurp(argv[1], &n);
    Career c;
    CHECK(career_load(&c, cb, n), "career.dat loads");
    CHECK(c.nscenes == 57 && c.nlines == 107 && c.ncredits == 255 && c.npools == 19, "%u scenes, %u lines, %u credit lines", c.nscenes, c.nlines, c.ncredits);

    /* ---- the text ---- */
    CHECK(career_scene_nlines(&c, 1) == 5 && strncmp(career_scene_line(&c, 1, 0), "Reda: Hey kid.", 14) == 0, "scene 1: Reda's introduction (%d lines): %s", career_scene_nlines(&c, 1), career_scene_line(&c, 1, 0));
    CHECK(career_scene_nlines(&c, 0) == 0 && career_scene_nlines(&c, 58) == 0 && career_scene_line(&c, 1, 99)[0] == 0, "bad scene / line numbers give nothing");
    CHECK(strstr(career_scene_line(&c, 53, 2), "skater of the year") != NULL, "scene 53 is the Skater Of The Year call: %s", career_scene_line(&c, 53, 2));
    for (int s = 1; s <= 57; s++) CHECK(career_scene_nlines(&c, s) >= 1, "scene %d has text", s);
    CHECK(strstr(career_str(&c, c.sign_str), "%s has been signed to") && strstr(career_str(&c, c.soty_str), "%s has been named Thrasher") && strstr(career_str(&c, c.end_str), "Career Mode"), "the fixed texts: %s | %s", career_str(&c, c.sign_str), career_str(&c, c.soty_str));
    CHECK(strcmp(career_str(&c, c.credit[0]), "SKATE IT") == 0 && strcmp(career_str(&c, c.credit[2]), "Danny Way") == 0, "the credits start with the title and the stars: %s", career_str(&c, c.credit[0]));
    {
        int tot = 0;
        for (int s = 1; s <= 57; s++) for (int i = 0; i < career_scene_nlines(&c, s); i++) { const char *l = career_scene_line(&c, s, i); tot++; CHECK(l[0] && !strstr(l, "\\n"), "scene %d line %d is clean", s, i); }
        CHECK(tot == 107, "107 dialogue lines (%d)", tot);
    }

    /* ---- stance: the ROM's flag of each skater (1 = regular) ---- */
    CHECK(career_skater_regular(&c, 0) == 0 && career_skater_regular(&c, 6) == 1 && career_skater_regular(&c, 12) == 0 && career_skater_regular(&c, 13) == 1 && career_skater_regular(&c, 5) == 1 && career_skater_regular(&c, -1) == 0 && career_skater_regular(&c, 99) == 0,
          "stance flags: Danny Way goofy, Koston / Puig / Carl regular, Aaron goofy");
    {
        int reg = 0;
        for (int i = 0; i < CAREER_NRIDERS; i++) reg += career_skater_regular(&c, i);
        CHECK(reg == 13, "13 of the 19 riders are regular (%d)", reg);
    }

    /* ---- sponsors and the scenes of the challenges ---- */
    CHECK(c.story[21].intro == 6 && c.story[21].win == 7 && c.story[21].slot == 0 && strcmp(career_sponsor_name(&c, 21), "Plan B") == 0 && strcmp(career_sponsor_icon(&c, 21), "planb") == 0, "challenge 21 is Plan B (scenes %d / %d)", c.story[21].intro, c.story[21].win);
    CHECK(c.story[26].intro == 16 && c.story[26].win == 54 && c.story[47].intro == 55 && c.story[47].win == 33 && c.story[10].intro == 3 && c.story[10].win == 4, "the scenes the ROM's bytes do not name: Baker, Spitfire, Paris");
    CHECK(career_sponsor_name(&c, 10) == NULL && career_sponsor_name(&c, -1) == NULL && career_sponsor_name(&c, 500) == NULL && career_sponsor_icon(&c, 0) == NULL, "only the sponsor challenges have a sponsor");
    {
        int per_slot[4] = { 0, 0, 0, 0 }, total = 0;
        for (uint32_t i = 0; i < c.nchal; i++) {
            if (c.story[i].slot == 0xFF) continue;
            total++; per_slot[c.story[i].slot]++;
            CHECK(c.story[i].intro && c.story[i].win && career_sponsor_name(&c, (int)i)[0] && career_sponsor_icon(&c, (int)i)[0], "sponsor challenge %u has scenes, a name and an icon", i);
            CHECK(c.pool[c.chal[i].pool].sponsor == c.story[i].slot, "challenge %u: the sponsor slot agrees with its pool", i);
        }
        CHECK(total == 21 && per_slot[0] == 7 && per_slot[1] == 4 && per_slot[2] == 5 && per_slot[3] == 5, "21 sponsor challenges: %d board, %d trucks, %d wheels, %d shoes", per_slot[0], per_slot[1], per_slot[2], per_slot[3]);
    }

    /* ---- speakers and pages ---- */
    {
        char who[32];
        const char *t = story_line_text("Reda: Hello there.  Nice board", who, sizeof(who));
        CHECK(strcmp(who, "Reda") == 0 && strncmp(t, "Hello th", 8) == 0, "speaker split: [%s] [%s]", who, t);
        t = story_line_text("Pat Example: Hello there.", who, sizeof(who));
        CHECK(strcmp(who, "Pat Example") == 0 && strcmp(t, "Hello there.") == 0, "two words: [%s] [%s]", who, t);
        t = story_line_text("A line with no speaker in it.", who, sizeof(who));
        CHECK(who[0] == 0 && strcmp(t, "A line with no speaker in it.") == 0, "no speaker: [%s]", who);
        t = story_line_text("Wait for it: 3 tricks", who, sizeof(who));
        CHECK(who[0] == 0 && strcmp(t, "Wait for it: 3 tricks") == 0, "a long phrase before the colon is not a name");
        StoryPage pg[STORY_MAX_PAGES];
        int np = story_pages(&c, 1, pg, STORY_MAX_PAGES);
        CHECK(np == 2 && pg[0].first == 0 && pg[0].n == 3 && pg[1].first == 3 && pg[1].n == 2 && strcmp(pg[0].speaker, "Reda") == 0 && strcmp(pg[1].speaker, "Reda") == 0, "scene 1 in %d pages (3 + 2 lines, Reda)", np);
        np = story_pages(&c, 3, pg, STORY_MAX_PAGES);
        CHECK(np == 2 && strcmp(pg[0].speaker, "Lucas") == 0 && strcmp(pg[1].speaker, "Reda") == 0 && pg[0].n == 2 && pg[1].n == 2, "scene 3: Lucas, then Reda (%d pages: %s / %s)", np, pg[0].speaker, pg[1].speaker);
        for (int s = 1; s <= 57; s++) {
            np = story_pages(&c, s, pg, STORY_MAX_PAGES);
            int lines = 0;
            for (int i = 0; i < np; i++) { lines += pg[i].n; CHECK(pg[i].n >= 1 && pg[i].n <= STORY_PAGE_LINES && pg[i].speaker[0], "scene %d page %d: %d lines, speaker [%s]", s, i, pg[i].n, pg[i].speaker); }
            CHECK(np >= 1 && lines == career_scene_nlines(&c, s), "scene %d: %d pages hold all %d lines", s, np, career_scene_nlines(&c, s));
        }
    }

    /* ---- the first steps of a new profile ---- */
    Profile p;
    profile_init(&p, &c, "STORY");
    {
        StoryItem it;
        CHECK(story_next(&p, &c, &it) && it.kind == STORY_SCENE && it.scene == 1, "a new career starts with scene 1 (Reda)");
        story_done(&p, &it);
        CHECK(!story_next(&p, &c, &it), "then nothing is due");
        CHECK(story_intro(&p, &c, 93) == 0 && story_intro(&p, &c, 0) == 0, "no scene before the early events");
        for (int i = 93; i <= 97; i++) progress_complete(&p, &c, i, NULL);
        CHECK(!story_next(&p, &c, &it), "the tutorials have no scenes");
    }

    /* ---- Paris: the trip, the intro of the event, the win ---- */
    {
        Profile q;
        profile_init(&q, &c, "PARIS");
        for (uint32_t i = 0; i < 3; i++) for (int k = 0; k < c.pool[i].nchal; k++) q.done[c.pool[i].chal[k]] = 1;
        progress_complete(&q, &c, 99, NULL);
        CHECK(progress_pool(&q, &c) == 3, "pool 3 (Paris) is current");
        int log[64], nlog = 0;
        show_all(&q, &c, log, &nlog, NULL);
        CHECK(nlog == 2 && log[0] == 1 && log[1] == 2, "scenes due on arrival: the intro of the career, then the trip to Paris (%d scenes: %d %d)", nlog, log[0], log[1]);
        CHECK(story_intro(&q, &c, 10) == 3, "before Paris Connection: scene 3");
        StoryItem it = { STORY_SCENE, 3, -1, 10, 3 };
        story_done(&q, &it);
        CHECK(story_intro(&q, &c, 10) == 0, "scene 3 is shown once");
        progress_complete(&q, &c, 10, NULL);
        CHECK(story_intro(&q, &c, 10) == 0, "an event that is done has no intro");
        nlog = 0;
        show_all(&q, &c, log, &nlog, NULL);
        CHECK(nlog == 1 && log[0] == 4, "after winning: scene 4 (%d scenes)", nlog);
    }

    /* ---- a whole career, many orders: every scene shown at most once, in the story's order; every scene reachable ---- */
    int reach[58];
    memset(reach, 0, sizeof(reach));
    for (unsigned seed = 1; seed <= 400; seed++) {
        rng_state = seed * 104729u;
        Profile s;
        profile_init(&s, &c, "SIM");
        int log[300], nlog = 0, kinds[5] = { 0, 0, 0, 0, 0 };
        show_all(&s, &c, log, &nlog, kinds);
        int steps = 0;
        while (!progress_finished(&s, &c) && steps < 400) {
            int open[64], k = progress_open_list(&s, &c, open, 64);
            if (k == 0) break;
            int pick = open[rng() % (unsigned)k];
            int intro = story_intro(&s, &c, pick);
            if (intro) { log[nlog++] = intro; StoryItem it = { STORY_SCENE, intro, -1, pick, intro }; story_done(&s, &it); }      /* "Go To Event" */
            progress_complete(&s, &c, pick, NULL);
            show_all(&s, &c, log, &nlog, kinds);
            steps++;
        }
        CHECK(progress_finished(&s, &c) && (steps == 49 || (steps == 50 && s.done[82])), "seed %u: finished (%d completions)", seed, steps);
        CHECK(kinds[STORY_SIGNED] == 4 && kinds[STORY_ENDING] == 1 && kinds[STORY_CREDITS] == 1, "seed %u: 4 signings, one ending, the credits (%d %d %d)", seed, kinds[STORY_SIGNED], kinds[STORY_ENDING], kinds[STORY_CREDITS]);
        int seen_scene[58] = { 0 };
        int last_pool_pos = -1, pos_of_53 = -1, pos_end = -1, pos_credits = -1, pos_signed[4] = { -1, -1, -1, -1 };
        for (int i = 0; i < nlog; i++) {
            int v = log[i];
            if (v >= 1 && v <= 57) { CHECK(!seen_scene[v], "seed %u: scene %d shown twice", seed, v); seen_scene[v] = 1; reach[v] = 1; if (v == 53) pos_of_53 = i; }
            else if (v >= 1000 && v < 1004) pos_signed[v - 1000] = i;
            else if (v == 2000) pos_end = i;
            else if (v == 3000) pos_credits = i;
        }
        (void)last_pool_pos;
        CHECK(log[0] == 1, "seed %u: the first scene is scene 1", seed);
        CHECK(pos_of_53 >= 0 && pos_end == pos_of_53 + 1 && pos_credits == pos_end + 1 && pos_credits == nlog - 1, "seed %u: the ending comes last: scene 53, the cover, the credits (%d %d %d of %d)", seed, pos_of_53, pos_end, pos_credits, nlog);
        CHECK(pos_signed[0] < pos_signed[1] && pos_signed[1] < pos_signed[2] && pos_signed[2] < pos_signed[3], "seed %u: the sponsors are signed in the order board, trucks, wheels, shoes", seed);
        for (int sl = 0; sl < 4; sl++) {                           /* each signing comes right after the win scene of its sponsor and before the pool's end scene */
            int ch = s.sponsor[sl] - 1;
            CHECK(ch >= 0 && c.story[ch].slot == sl, "seed %u: slot %d holds a challenge of its slot", seed, sl);
            int w = c.story[ch].win, wp = -1, ip = -1;
            for (int i = 0; i < nlog; i++) { if (log[i] == w) wp = i; if (log[i] == c.story[ch].intro) ip = i; }
            CHECK(wp >= 0 && ip >= 0 && ip < wp && wp < pos_signed[sl], "seed %u: sponsor %d: intro %d, win %d, signed (%d < %d < %d)", seed, ch, c.story[ch].intro, w, ip, wp, pos_signed[sl]);
        }
        if (seed == 1) {
            printf("one career's story, seed 1:");
            for (int i = 0; i < nlog; i++) { if (log[i] < 1000) printf(" %d", log[i]); else printf(" [%s]", log[i] < 2000 ? "signed" : log[i] == 2000 ? "cover" : "credits"); }
            printf("\n");
        }
    }
    {
        int missing = 0;
        for (int v = 1; v <= 57; v++) if (!reach[v]) { printf("scene %d is never shown\n", v); missing++; }
        CHECK(missing == 0, "every scene can be seen in some career (%d never shown)", missing);
    }

    /* ---- the story flags in the profile file, and a version 1 file ---- */
    {
        Profile s;
        profile_init(&s, &c, "FILE");
        for (int i = 93; i <= 97; i++) progress_complete(&s, &c, i, NULL);
        StoryItem it;
        story_next(&s, &c, &it); story_done(&s, &it);
        progress_set_seen(&s, 33); progress_set_seen(&s, 70);
        uint8_t buf[2048], v1[2048];
        size_t sz = profile_save(&s, buf, sizeof(buf));
        Profile t;
        CHECK(profile_load(&t, &c, buf, sz) == 1 && memcmp(&s, &t, sizeof(s)) == 0 && progress_seen(&t, 1) && progress_seen(&t, 33) && progress_seen(&t, 70) && !progress_seen(&t, 2), "the story flags survive the file");
        CHECK(!progress_seen(&t, -1) && !progress_seen(&t, 128) && !progress_seen(&t, 5000), "flags out of range read as not shown");
        /* the first version: 8 statistics, no flags, version 1 (built from the current file: header, 8 statistics, done flags and unlocks) */
        size_t hdr = 8 + 16 + 4 + 8, o1 = 0;
        memcpy(v1, buf, hdr + 8 * 4); o1 = hdr + 8 * 4;
        memcpy(v1 + o1, buf + hdr + PROFILE_NSTAT * 4, 128 + 512); o1 += 128 + 512;
        v1[4] = 1;
        uint32_t crc = crc32(v1, o1);
        for (int i = 0; i < 4; i++) v1[o1++] = (uint8_t)(crc >> (8 * i));
        size_t sz1 = o1;
        Profile o;
        CHECK(profile_load(&o, &c, v1, sz1) == 2, "a version 1 file loads (and says so)");
        CHECK(progress_chal_done(&o, 97) && progress_pool(&o, &c) == 1 && !progress_seen(&o, 1), "its progress is kept and it has no story flags");
        story_catch_up(&o, &c);
        CHECK(!story_next(&o, &c, &it) && progress_seen(&o, 1), "caught up: nothing that is already behind it is shown again");
        progress_complete(&o, &c, 1, NULL);
        CHECK(!story_next(&o, &c, &it), "and the story goes on from there (nothing due after challenge 1)");
        /* a finished career in an old file: everything counts as seen */
        Profile f;
        profile_init(&f, &c, "OLDFIN");
        for (int guard = 0; guard < 200 && !progress_finished(&f, &c); guard++) { int open[64]; int k = progress_open_list(&f, &c, open, 64); if (!k) break; progress_complete(&f, &c, open[0], NULL); }
        CHECK(progress_finished(&f, &c) && story_next(&f, &c, &it), "a finished career has a lot due");
        story_catch_up(&f, &c);
        CHECK(!story_next(&f, &c, &it), "after the catch-up nothing is due: no replaying of the whole story");
        /* flipping any byte of the new file is still caught */
        int accepted = 0;
        for (size_t i = 0; i < sz; i++) { uint8_t bad[2048]; memcpy(bad, buf, sz); bad[i] ^= 0x5A; Profile u; accepted += profile_load(&u, &c, bad, sz); }
        CHECK(accepted == 0, "%d corrupted files were accepted", accepted);
    }

    printf(fails ? "%d FAILURES\n" : "all story tests passed\n", fails);
    return fails != 0;
}
