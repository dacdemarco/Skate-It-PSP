/* Host test of the settings (src/settings.c): the presets, swapping buttons, the mapping of buttons to control bits, the file. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/settings.h"
#include "../src/control.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int is_perm(const Settings *s)
{
    unsigned seen = 0;
    for (int a = 0; a < ACT_COUNT; a++) { if (s->map[a] >= PB_COUNT) return 0; seen |= PBM(s->map[a]); }
    return seen == (1u << PB_COUNT) - 1;
}

int main(void)
{
    Settings s;
    settings_defaults(&s);
    CHECK(s.preset == PRESET_DEFAULT && is_perm(&s), "defaults");
    /* the default mapping is the game's scheme as it always was: Cross = CTL_CROSS etc. */
    CHECK(settings_ctl_bits(&s, PBM(PB_CROSS)) == CTL_CROSS && settings_ctl_bits(&s, PBM(PB_SQUARE)) == CTL_SQUARE && settings_ctl_bits(&s, PBM(PB_CIRCLE)) == CTL_CIRCLE &&
          settings_ctl_bits(&s, PBM(PB_TRIANGLE)) == CTL_TRIANGLE && settings_ctl_bits(&s, PBM(PB_L)) == CTL_L && settings_ctl_bits(&s, PBM(PB_R)) == CTL_R, "default = the original scheme");
    CHECK(settings_ctl_bits(&s, PBM(PB_CROSS) | PBM(PB_L)) == (CTL_CROSS | CTL_L) && settings_ctl_bits(&s, 0) == 0, "several buttons at once");
    for (int p = 0; p < PRESET_CUSTOM; p++) {
        settings_apply_preset(&s, p);
        CHECK(is_perm(&s) && s.preset == p && settings_match_preset(&s) == p, "preset %d is a permutation and matches itself", p);
        CHECK(settings_preset_map(p) && memcmp(settings_preset_map(p), s.map, ACT_COUNT) == 0, "preset %d map accessor", p);
        unsigned all = 0;
        for (int b = 0; b < PB_COUNT; b++) all |= settings_ctl_bits(&s, PBM(b));
        CHECK(all == 63, "preset %d: every action is on some button (%u)", p, all);
    }
    settings_apply_preset(&s, PRESET_CIRCLE_POP);
    CHECK(settings_ctl_bits(&s, PBM(PB_CIRCLE)) == CTL_CROSS, "circle pop: Circle pops");
    settings_apply_preset(&s, PRESET_DEFAULT);
    settings_assign(&s, ACT_POP, PB_TRIANGLE);
    CHECK(s.map[ACT_POP] == PB_TRIANGLE && s.map[ACT_POP_ADV] == PB_CROSS && is_perm(&s) && s.preset == PRESET_CUSTOM, "assigning a taken button swaps");
    settings_assign(&s, ACT_POP, PB_TRIANGLE);
    CHECK(is_perm(&s), "assigning the same button again changes nothing");
    settings_assign(&s, 99, 0); settings_assign(&s, 0, 99);
    CHECK(is_perm(&s), "bad arguments are ignored");
    settings_assign(&s, ACT_POP, PB_CROSS);
    CHECK(settings_match_preset(&s) == PRESET_DEFAULT && s.preset == PRESET_CUSTOM, "a custom mapping equal to a preset is still reported as the player's own, but matches");
    settings_apply_preset(&s, PRESET_CUSTOM);
    CHECK(s.preset == PRESET_CUSTOM && is_perm(&s), "custom keeps the mapping");
    for (int i = 0; i < INFO_COUNT; i++) CHECK(settings_info_name(i)[0] && settings_info_help(i)[0], "info %d has a name and a help text", i);
    for (int a = 0; a < ACT_COUNT; a++) CHECK(settings_action_name(a)[0] && settings_action_help(a)[0], "action %d has texts", a);
    for (int b = 0; b < PB_COUNT; b++) CHECK(settings_button_name(b)[0], "button %d has a name", b);
    settings_apply_preset(&s, PRESET_SWAP_POPS);
    CHECK(settings_ctl_bits(&s, PBM(PB_TRIANGLE)) == CTL_POP && settings_ctl_bits(&s, PBM(PB_CROSS)) == CTL_POP_ADV, "swapped pops: Triangle pops the basic tricks, X the advanced ones");

    /* the tuning: percent steps within the range, 100 = a scale of 1 */
    settings_defaults(&s);
    for (int t = 0; t < TUNE_COUNT; t++) CHECK(s.tune[t] == 100 && settings_tune_scale(&s, t) == 1.0f && settings_tune_name(t)[0] && settings_tune_help(t)[0], "tune %d: 100 percent by default, named", t);
    settings_tune_step(&s, TUNE_GRAVITY, -1);
    CHECK(s.tune[TUNE_GRAVITY] == 90 && fabsf(settings_tune_scale(&s, TUNE_GRAVITY) - 0.9f) < 1e-6f, "one step down: 90 percent");
    for (int i = 0; i < 20; i++) settings_tune_step(&s, TUNE_OLLIE, 1);
    CHECK(s.tune[TUNE_OLLIE] == TUNE_MAX, "it stops at the top (%d)", s.tune[TUNE_OLLIE]);
    for (int i = 0; i < 20; i++) settings_tune_step(&s, TUNE_PUSH, -1);
    CHECK(s.tune[TUNE_PUSH] == TUNE_MIN, "and at the bottom (%d)", s.tune[TUNE_PUSH]);
    settings_tune_step(&s, 99, 1);

    /* the file */
    Settings t, u;
    settings_defaults(&t);
    settings_apply_preset(&t, PRESET_SHOULDER_BRAKE);
    t.info[INFO_HEAP] = 1; t.info[INFO_RAIL] = 1;
    t.tune[TUNE_GRAVITY] = 80; t.tune[TUNE_PUSH] = 130;
    uint8_t buf[64];
    size_t n = settings_save(&t, buf, sizeof(buf));
    CHECK(n > 0 && settings_save(&t, buf, 4) == 0, "save size");
    settings_defaults(&u);
    CHECK(settings_load(&u, buf, n) && memcmp(&t, &u, sizeof(t)) == 0, "save / load round trip");
    {   /* a version 1 file (no tuning) still loads, with the tuning at 100 */
        uint8_t v1[64];
        size_t n1 = 7 + ACT_COUNT + INFO_COUNT;
        memcpy(v1, buf, n1); v1[4] = 1;
        settings_defaults(&u); u.tune[TUNE_OLLIE] = 60;
        CHECK(settings_load(&u, v1, n1) && u.preset == t.preset && memcmp(u.map, t.map, ACT_COUNT) == 0 && u.info[INFO_RAIL] == 1 && u.tune[TUNE_OLLIE] == 100 && u.tune[TUNE_GRAVITY] == 100, "a version 1 file loads with the tuning at 100");
        memcpy(v1, buf, n); v1[n - 1] = 55;
        CHECK(!settings_load(&u, v1, n), "a tuning value off the steps is refused");
        memcpy(v1, buf, n); v1[n - 2] = 200;
        CHECK(!settings_load(&u, v1, n), "a tuning value out of range is refused");
    }
    uint8_t bad[64];
    memcpy(bad, buf, n);
    bad[6] = bad[7];                                       /* two actions on one button */
    CHECK(!settings_load(&u, bad, n), "a mapping that is not a permutation is refused");
    memcpy(bad, buf, n); bad[0] = 'X';
    CHECK(!settings_load(&u, bad, n), "wrong magic");
    CHECK(!settings_load(&u, buf, 5), "truncated");
    memcpy(bad, buf, n); bad[5] = 99;
    CHECK(!settings_load(&u, bad, n), "bad preset");
    for (size_t k = 0; k < n; k++) { Settings v; settings_load(&v, buf, k); }      /* no crash on any truncation */

    /* ---- game settings: stance, units, volumes ---- */
    {
        Settings g;
        settings_defaults(&g);
        CHECK(g.stance == STANCE_OWN && g.units == UNITS_METERS && g.sfx_vol == VOL_MAX && g.music_vol == VOL_MAX && g.profile_slot == 0, "defaults: the skater's own stance, metres, full volume, profile 1");
        CHECK(settings_riding_regular(&g, 0) == 0 && settings_riding_regular(&g, 1) == 1, "own stance follows the skater");
        g.stance = STANCE_REGULAR; CHECK(settings_riding_regular(&g, 0) == 1 && settings_riding_regular(&g, 1) == 1, "regular is regular for everyone");
        g.stance = STANCE_GOOFY; CHECK(settings_riding_regular(&g, 0) == 0 && settings_riding_regular(&g, 1) == 0, "goofy is goofy for everyone");
        for (int i = 0; i < STANCE_COUNT; i++) CHECK(settings_stance_name(i)[0], "stance %d has a name", i);
        for (int i = 0; i < UNITS_COUNT; i++) CHECK(settings_units_name(i)[0], "units %d have a name", i);
        CHECK(settings_stance_help()[0] && settings_stance_name(9)[0] == 0, "texts");
        CHECK(settings_volume_scale(0) == 0.0f && settings_volume_scale(VOL_MAX) == 1.0f && fabsf(settings_volume_scale(5) - 0.5f) < 1e-6f && settings_volume_scale(99) == 1.0f && settings_volume_scale(-3) == 0.0f, "volume scale");
        char tx[32];
        settings_format_speed(&g, 10.0f, tx, sizeof(t)); CHECK(strcmp(tx, "36 km/h") == 0, "speed in metres: %s", tx);
        g.units = UNITS_FEET;
        settings_format_speed(&g, 10.0f, tx, sizeof(t)); CHECK(strcmp(tx, "22 mph") == 0, "speed in feet: %s", tx);
        settings_format_distance(&g, 12.0f, tx, sizeof(t)); CHECK(strcmp(tx, "39 ft") == 0, "distance in feet: %s", tx);
        g.units = UNITS_METERS;
        settings_format_distance(&g, 12.4f, tx, sizeof(t)); CHECK(strcmp(tx, "12 m") == 0, "distance in metres: %s", tx);
        settings_format_speed(&g, 1.0f, tx, 4); CHECK(strlen(tx) == 3, "a small buffer is respected");
        g.stance = STANCE_GOOFY; g.units = UNITS_FEET; g.sfx_vol = 4; g.music_vol = 7; g.profile_slot = 2;
        uint8_t b[64];
        size_t gn = settings_save(&g, b, sizeof(b));
        Settings h;
        settings_defaults(&h);
        CHECK(gn > 0 && settings_load(&h, b, gn) && memcmp(&g, &h, sizeof(g)) == 0 && h.stance == STANCE_GOOFY && h.units == UNITS_FEET && h.sfx_vol == 4 && h.music_vol == 7 && h.profile_slot == 2, "the game settings survive the file");
        uint8_t v2[64];
        size_t n2 = gn - 1 - 5;                                                   /* a version 2 file ends after the tuning */
        memcpy(v2, b, n2); v2[4] = 2;
        settings_defaults(&h); h.stance = STANCE_REGULAR;
        CHECK(settings_load(&h, v2, n2) && h.stance == STANCE_OWN && h.units == UNITS_METERS && h.sfx_vol == VOL_MAX, "a version 2 file loads with the default game settings");
        uint8_t gbad[64];
        memcpy(gbad, b, gn); gbad[gn - 5] = 9; CHECK(!settings_load(&h, gbad, gn), "a stance out of range is refused");
        memcpy(gbad, b, gn); gbad[gn - 4] = 9; CHECK(!settings_load(&h, gbad, gn), "units out of range are refused");
        memcpy(gbad, b, gn); gbad[gn - 3] = VOL_MAX + 1; CHECK(!settings_load(&h, gbad, gn), "a volume out of range is refused");
        memcpy(gbad, b, gn); gbad[gn - 2] = VOL_MAX + 1; CHECK(!settings_load(&h, gbad, gn), "the music volume too");
        memcpy(gbad, b, gn); gbad[gn - 1] = 3; CHECK(!settings_load(&h, gbad, gn), "a profile slot out of range is refused");
        {   /* a file of the first version-3 build (four game bytes, no profile slot) still loads */
            uint8_t old[64];
            memcpy(old, b, gn - 1); old[gn - 6] = 4;
            settings_defaults(&h); h.profile_slot = 2;
            CHECK(settings_load(&h, old, gn - 1) && h.stance == STANCE_GOOFY && h.sfx_vol == 4 && h.profile_slot == 0, "four game bytes: the profile slot is the default");
        }
        for (size_t k = 0; k < gn; k++) { Settings v; settings_load(&v, b, k); }
    }

    printf(fails ? "%d FAILURES\n" : "all settings tests passed\n", fails);
    return fails != 0;
}
