/* Host test: the tutorial list and texts (src/tutorial.c) against the career data (data/career.dat).
 * usage: test_tutorial <career.dat> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/career.h"
#include "../src/settings.h"
#include "../src/tutorial.h"

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

int main(int argc, char **argv)
{
    (void)argc;
    size_t n;
    unsigned char *cb = slurp(argv[1], &n);
    Career c;
    CHECK(career_load(&c, cb, n), "career.dat loads");
    int list[TUT_MAX], nt = tutorial_list(&c, list, TUT_MAX);
    CHECK(nt == 10, "ten tutorials (%d)", nt);
    CHECK(tutorial_list(&c, list, 3) == 3, "the list stops at max");
    nt = tutorial_list(&c, list, TUT_MAX);
    for (int i = 0; i < nt; i++) {
        const CareerChal *ch = &c.chal[list[i]];
        CHECK(ch->idx == list[i], "the table index is the challenge index (%d vs %d)", ch->idx, list[i]);
        CHECK(list[i] == 93 + i, "tutorial %d is challenge %d", i, list[i]);
        CHECK(ch->mode == MODE_TUTORIAL || ch->mode == MODE_TUTORIAL_ADV, "mode of %d", list[i]);
        CHECK(strcmp(career_str(&c, ch->name_off), "") != 0, "tutorial %d has a name", list[i]);
        CHECK(tutorial_summary(list[i])[0] != 0, "tutorial %d has a summary", list[i]);
        /* a tip for every objective and none beyond, short lines, only the known tokens, no mention of the DS's stylus */
        for (int o = 0; o < 6; o++) {
            const char *t = tutorial_tip(list[i], o);
            if (o >= ch->nobj) { CHECK(t == NULL, "challenge %d: a tip for objective %d but only %d objectives", list[i], o, ch->nobj); continue; }
            CHECK(t != NULL && t[0], "challenge %d objective %d has no tip", list[i], o);
            if (!t) continue;
            int lines = 1, len = 0;
            for (const char *p = t; *p; p++) {
                if (*p == '\n') { lines++; len = 0; continue; }
                if (++len > 46) { CHECK(0, "challenge %d objective %d: a line longer than 46 characters", list[i], o); break; }
                if (*p == '{') {
                    static const char *const ok[] = { "{POP}", "{ADV}", "{PUSH}", "{BRAKE}", "{GL}", "{GR}" };
                    int known = 0;
                    for (unsigned k = 0; k < sizeof(ok) / sizeof(ok[0]); k++) known |= strncmp(p, ok[k], strlen(ok[k])) == 0;
                    CHECK(known, "challenge %d objective %d: unknown token in '%s'", list[i], o, t);
                }
            }
            CHECK(lines <= 2, "challenge %d objective %d: %d lines", list[i], o, lines);
            CHECK(!strstr(t, "stylus") && !strstr(t, "Touch Screen"), "challenge %d objective %d mentions the stylus", list[i], o);
        }
    }
    CHECK(tutorial_tip(1, 0) == NULL && tutorial_summary(1)[0] == 0, "a challenge that is no tutorial has no text");
    CHECK(tutorial_tip(93, -1) == NULL && tutorial_tip(93, 99) == NULL, "out of range objectives");

    /* the buttons of the mapping fill the tokens */
    Settings s; settings_defaults(&s);
    char out[160];
    settings_expand_buttons(&s, tutorial_tip(93, 1), out, sizeof(out));
    CHECK(strchr(out, '{') == NULL && strstr(out, "Hold X, flick") != NULL, "default mapping: '%s'", out);
    settings_apply_preset(&s, PRESET_SWAP_POPS);
    settings_expand_buttons(&s, tutorial_tip(99, 0), out, sizeof(out));
    CHECK(strstr(out, "Hold X, flick") != NULL, "swapped pops: the advanced pop is on X: '%s'", out);
    settings_expand_buttons(&s, tutorial_tip(93, 1), out, sizeof(out));
    CHECK(strstr(out, "Hold Triangle, flick") != NULL, "swapped pops: the pop is on Triangle: '%s'", out);
    settings_assign(&s, ACT_GRAB_L, PB_SQUARE);
    settings_expand_buttons(&s, tutorial_tip(95, 1), out, sizeof(out));
    CHECK(strstr(out, "Square") != NULL, "custom grab button: '%s'", out);
    { char tiny[8]; settings_expand_buttons(&s, "{POP} {ADV} {PUSH} {BRAKE}", tiny, sizeof(tiny)); CHECK(strlen(tiny) <= 7, "a short buffer is never overrun"); }
    { char one[2] = { 'x', 'y' }; settings_expand_buttons(&s, "abc", one, 1); CHECK(one[0] == 0 && one[1] == 'y', "a one byte buffer holds just the terminator"); settings_expand_buttons(&s, "abc", one, 0); CHECK(one[1] == 'y', "a zero byte buffer is left alone"); }

    /* the objective the tip is for */
    { int d[4] = { 1, 1, 0, 0 }; CHECK(tutorial_current_objective(d, 4) == 2, "first undone objective"); }
    { int d[3] = { 1, 1, 1 }; CHECK(tutorial_current_objective(d, 3) == -1, "all done"); }
    { int d[3] = { 0, 1, 1 }; CHECK(tutorial_current_objective(d, 3) == 0, "any order: the first one that is not done"); }

    printf(fails ? "%d FAILURES\n" : "all tutorial tests passed\n", fails);
    return fails != 0;
}
