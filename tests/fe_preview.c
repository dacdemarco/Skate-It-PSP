/* Host tool + test: renders every front-end screen with the software renderer (tests/ui_sw.h) to PPM files, and walks the menu with scripted button presses to test the flow.
 * usage: fe_preview <ui.pak> <career.dat> <out_dir>   (files: title.ppm main.ppm skater.ppm location_<n>.ppm ...) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_sw.h"
#include "../src/hud.h"
#include "../src/frontend.h"
#include "../src/tutorial.h"
#include "../src/story.h"
#include "../src/stats.h"
#include "../src/trick.h"
#include "../src/custom.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = aligned_alloc(16, ((size_t)sz + 15) & ~15ul); if (fread(b, 1, sz, f) != (size_t)sz) exit(2); fclose(f);
    *n = (size_t)sz;
    return b;
}

static SwCanvas canvas;
static Frontend fe;
static Settings set;
static const char *outdir;

static void shot(const char *name)
{
    char path[512];
    sw_clear(&canvas, 0, 0, 0);
    fe_draw_back(&fe);
    fe_draw_front(&fe);
    snprintf(path, sizeof(path), "%s/%s.ppm", outdir, name);
    sw_save_ppm(&canvas, path);
}

static int press(unsigned b)
{
    FeInput in = { b, b };
    int r = fe_update(&fe, &in, 1.0f / 60);
    in.pressed = 0; in.held = 0;
    for (int i = 0; i < 3; i++) fe_update(&fe, &in, 1.0f / 60);
    return r;
}

static void idle(float secs) { FeInput in = { 0, 0 }; for (float t = 0; t < secs; t += 1.0f / 60) fe_update(&fe, &in, 1.0f / 60); }

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    tricks_defaults();                                            /* the trick book lists the trick table */
    static CustomPak cpak; int cpak_ok = 0;
    if (argc >= 5) { size_t cn; unsigned char *cbuf = slurp(argv[4], &cn); cpak_ok = custom_pak_load(&cpak, cbuf, cn); CHECK(cpak_ok, "custom.pak loads"); }
    outdir = argv[3];
    size_t n1, n2;
    unsigned char *ub = slurp(argv[1], &n1), *cb = slurp(argv[2], &n2);
    static UiPak pak; static Career career;
    CHECK(uipak_load(&pak, ub, n1), "ui.pak loads");
    CHECK(career_load(&career, cb, n2), "career.dat loads");
    CHECK(pak.nimg >= 30 && pak.nfont == 7, "%d images, %d fonts", pak.nimg, pak.nfont);
    CHECK(uipak_image(&pak, "title") >= 0 && uipak_image(&pak, "nope") == -1 && uipak_font(&pak, "fe_large") >= 0, "lookups");
    { unsigned char bad[64] = { 0 }; UiPak p2; CHECK(!uipak_load(&p2, bad, sizeof(bad)), "garbage is refused"); }

    Ui ui; ui_init(&ui, &pak, sw_backend(&canvas));
    unsigned char sk_ok[32] = { 0 }, lv_ok[32];
    memset(lv_ok, 1, sizeof(lv_ok));
    for (int i = 0; i < 19; i++) sk_ok[i] = 1;                    /* all the riders have a file in this preview */
    fe_init(&fe, &ui, &career, sk_ok, lv_ok);
    settings_defaults(&set);
    static Profile prof;
    profile_init(&prof, &career, "TEST");
    fe_set_profile(&fe, &prof);
    fe.set = &set;
    CHECK(fe.nskater == 19 && fe.nloc == 20, "%d skaters, %d locations (expected 19 / 20: the My Spot warehouse is left out)", fe.nskater, fe.nloc);
    CHECK(fe.state == FE_STATE_TITLE && strcmp(fe.skater[fe.skater_sel].key, "aaron") == 0 && strcmp(fe.loc[fe.loc_sel].key, "Amphitheatre") == 0, "defaults: aaron, the library");
    for (int i = 0; i < fe.nloc; i++) CHECK(fe.loc[i].pic >= 0, "location %s has a picture", fe.loc[i].name);

    idle(2.0f); shot("title");
    CHECK(press(FE_START) == 0 && fe.state == FE_STATE_MAIN, "START leaves the title");
    idle(0.6f); shot("main");
    CHECK(fe.main_sel == 0, "start career is the first entry");
    press(FE_DOWN); CHECK(fe.main_sel == 1 && fe.ev_move, "down: my skate (%d)", fe.main_sel);
    {   /* My Skate: the menu, the profile slots, the Log Book */
        static Profile mp;
        profile_init(&mp, &career, "BOOK");
        mp.stat[STAT_PLAY_TIME] = 3725; mp.stat[STAT_TRICKS] = 41; mp.stat[STAT_BAILS] = 17; mp.stat[STAT_PUSHES] = 380; mp.stat[STAT_DIST] = 523400; mp.stat[STAT_DIST_GRIND] = 18200; mp.stat[STAT_DIST_MANUAL] = 9000;
        mp.stat[STAT_DIST_SLIDE] = 2100; mp.stat[STAT_AIRGRAB_TIME] = 64; mp.stat[STAT_BEST_AIR] = 1730; mp.stat[STAT_BEST_GRIND] = 2200; mp.stat[STAT_BEST_MANUAL] = 1450; mp.stat[STAT_BEST_SLIDE] = 640; mp.stat[STAT_BEST_LINE] = 6840;
        for (int i = 93; i <= 97; i++) progress_complete(&mp, &career, i, NULL);
        mp.landed[0] = 0xFF; mp.landed[3] = 0x5A; mp.landed[7] = 0x11;
        fe_set_profile(&fe, &mp);
        press(FE_OK); CHECK(fe.state == FE_STATE_MYSKATE && fe.ms_sel == 0, "OK on my skate opens its menu (state %d)", fe.state);
        idle(0.3f); shot("myskate");
        press(FE_DOWN); CHECK(fe.ms_sel == 1, "customize can be chosen (%d)", fe.ms_sel);
        press(FE_DOWN); press(FE_DOWN); press(FE_DOWN); CHECK(fe.ms_sel == 4, "credits (%d)", fe.ms_sel);
        press(FE_DOWN); CHECK(fe.ms_sel == 0, "the menu wraps");
        press(FE_UP); CHECK(fe.ms_sel == 4, "and back");
        /* Customize: the parts come from custom.pak (the caller loads it while the screen is open) */
        fe.ms_sel = 1; press(FE_OK);
        CHECK(fe.state == FE_STATE_CUSTOM && fe.cust_dirty == 1 && fe.cust_sel == 0, "Customize opens (state %d)", fe.state);
        fe.cust_dirty = 0;
        idle(0.2f); shot("customize_nopak");
        press(FE_RIGHT); CHECK(fe.cust.gender == 0 && !fe.cust_dirty, "without the parts nothing changes");
        press(FE_BACK); CHECK(fe.state == FE_STATE_MYSKATE, "O leaves it");
        if (cpak_ok) {
            fe.cpak = &cpak;
            fe.ms_sel = 1; press(FE_OK);
            CHECK(fe.state == FE_STATE_CUSTOM && fe.cust.top == 0 && fe.cust.gender == 0 && fe.cust.hair == 0, "Customize opens on the first choice");
            fe.cust_dirty = 0;
            press(FE_RIGHT); CHECK(fe.cust.gender == 1 && fe.cust_dirty == 1 && fe.ev_move, "right: female (the viewer is to be rebuilt)");
            press(FE_RIGHT); CHECK(fe.cust.gender == 0, "and round");
            press(FE_DOWN); press(FE_LEFT); CHECK(fe.cust_sel == 1 && fe.cust.build == 2, "the build wraps to skinny (%d)", fe.cust.build);
            press(FE_DOWN); press(FE_OK); CHECK(fe.cust.skin == 1, "OK steps on: skin tone 2");
            press(FE_DOWN); press(FE_RIGHT); press(FE_RIGHT); press(FE_RIGHT); CHECK(fe.cust_sel == 3 && fe.cust.face == 3, "face 4 (%d)", fe.cust.face);
            /* the hair: the colour row exists only with hair */
            press(FE_DOWN); CHECK(fe.cust_sel == 4, "hair");
            press(FE_DOWN); CHECK(fe.cust_sel == 6, "no hair: its colour row is skipped, the beard is next (%d)", fe.cust_sel);
            press(FE_UP); press(FE_RIGHT); CHECK(fe.cust.hair == 1, "hair: afro");
            press(FE_DOWN); CHECK(fe.cust_sel == 5, "with hair the colour row is there");
            press(FE_RIGHT); press(FE_RIGHT); CHECK(fe.cust.hair_col == 2, "brown");
            press(FE_DOWN); press(FE_RIGHT); CHECK(fe.cust_sel == 6 && fe.cust.beard == 1, "a beard");
            press(FE_DOWN); CHECK(fe.cust_sel == 7, "its colour");
            press(FE_RIGHT); CHECK(fe.cust.beard_col == 1, "blonde");
            /* the hat: a man's choices skip the cap for long hair */
            press(FE_DOWN); CHECK(fe.cust_sel == 8, "hat");
            press(FE_RIGHT); CHECK(fe.cust.hat == 4, "a new profile has no cap design yet and a man has no cap for long hair: the first hat is the fedora (%d)", fe.cust.hat);
            press(FE_DOWN); CHECK(fe.cust_sel == 9, "the design");
            CHECK(custom_hat_family(&cpak, fe.cust.hat_tex) == 1 && custom_rom(&cpak, CUST_HAT, fe.cust.hat_tex) >= 16, "a fedora wears a diver design the profile has (%d)", fe.cust.hat_tex);
            { int first = fe.cust.hat_tex; press(FE_RIGHT); int second = fe.cust.hat_tex; press(FE_RIGHT);
              CHECK(second != first && fe.cust.hat_tex == first && custom_hat_family(&cpak, second) == 1, "two diver designs, no others (%d %d %d)", first, second, fe.cust.hat_tex); }
            press(FE_R); CHECK(custom_hat_family(&cpak, fe.cust.hat_tex) == 1, "R stays among the designs there are");
            /* a woman's cap for long hair goes when he becomes a man */
            fe.cust.gender = 1; fe.cust.hat = 3; fe.cust_sel = 0; press(FE_RIGHT);
            CHECK(fe.cust.gender == 0 && fe.cust.hat == 0, "a man cannot keep the cap for long hair (gender %d hat %d)", fe.cust.gender, fe.cust.hat);
            fe.cust.hat = 4; fe.cust_sel = 9;
            press(FE_DOWN); press(FE_R); CHECK(fe.cust_sel == 10 && fe.cust.top == 5, "top: five on (%d)", fe.cust.top);
            for (int i = 0; i < 8; i++) press(FE_R);
            CHECK(fe.cust.top == 34, "the pages stop at the last top (%d)", fe.cust.top);
            press(FE_RIGHT); CHECK(fe.cust.top == 0, "one step wraps round");
            press(FE_L); CHECK(fe.cust.top == 0, "L at the start stays");
            press(FE_DOWN); press(FE_R); press(FE_R); CHECK(fe.cust_sel == 11 && fe.cust.logo == 10, "logo 10 (none is 0)");
            press(FE_LEFT); press(FE_LEFT); press(FE_LEFT); CHECK(fe.cust.logo == 7, "steps back (%d)", fe.cust.logo);
            for (int i = 0; i < 20; i++) press(FE_R);
            CHECK(fe.cust.logo == 70, "the pages stop at the last logo (%d)", fe.cust.logo);
            press(FE_RIGHT); CHECK(fe.cust.logo == 0, "and wrap to none");
            press(FE_R); press(FE_DOWN); press(FE_R); press(FE_DOWN); press(FE_R); press(FE_R);
            CHECK(fe.cust_sel == 13 && fe.cust.logo == 5 && fe.cust.pants == 5 && custom_rom(&cpak, CUST_SHOES, fe.cust.shoes) >= 0 && custom_rom(&cpak, CUST_SHOES, fe.cust.shoes) <= 3, "logo, bottom and a pair of the four generic shoes (%d %d %d)", fe.cust.logo, fe.cust.pants, fe.cust.shoes);
            idle(0.3f); shot("customize");
            fe.cust_sel = 16; fe.ev_profile = 0;                      /* (the Done row: after shoes 13, deck 14, wheels 15) */
            CustomSel made = fe.cust;
            CHECK(custom_valid(&cpak, &made), "the choice made is valid");
            press(FE_OK);
            CHECK(fe.state == FE_STATE_MYSKATE && mp.has_custom && mp.skater == PROFILE_CUSTOM_SKATER && memcmp(&mp.custom, &made, sizeof(made)) == 0 && fe.ev_profile == 1, "Done keeps it and makes it the rider");
            fe.ev_profile = 0;
            fe.ms_sel = 1; press(FE_OK);
            CHECK(fe.cust.shoes == made.shoes && fe.cust.hair == 1 && fe.cust.beard == 1 && fe.cust.hat == 4, "Customize opens on the skater that was made");
            press(FE_RIGHT); fe.cust_sel = 16; press(FE_BACK);
            CHECK(fe.state == FE_STATE_MYSKATE && mp.custom.gender == made.gender, "O drops what was changed since");
            {   /* the gear the career has given: a new profile has the generic decks, wheels and shoes only; signing with Almost adds its decks, wheels and caps */
                static Profile np;
                profile_init(&np, &career, "NEW");
                fe_set_profile(&fe, &np);
                fe.ms_sel = 1; press(FE_OK);
                int row_values[3] = { 0, 0, 0 };
                static const int ROWS[3] = { 13, 14, 15 };                          /* shoes, deck, wheels */
                for (int k = 0; k < 3; k++) {
                    fe.cust_sel = ROWS[k];
                    int start = 0, n = 0;
                    uint8_t *f = k == 0 ? &fe.cust.shoes : (k == 1 ? &fe.cust.deck : &fe.cust.wheels);
                    start = *f;
                    do { press(FE_RIGHT); n++; } while (*f != start && n < 200);
                    row_values[k] = n;
                }
                CHECK(row_values[0] == 4, "a new profile can pick between 4 pairs of shoes (%d)", row_values[0]);
                CHECK(row_values[1] == 7, "... 7 decks: the generic default and 6 listed (Toon again, Bunny, Hume, Lighting, White, White Dirt) (%d)", row_values[1]);
                CHECK(row_values[2] == 5, "... 5 wheel designs: the default and the 4 generic ones (%d)", row_values[2]);
                for (int i = 4; i <= 9; i++) np.unlocked[UNLOCK_DECK][i / 8] |= (uint8_t)(1u << (i % 8));        /* Almost's six decks, three wheels, two caps */
                for (int i = 4; i <= 6; i++) np.unlocked[UNLOCK_WHEELS][i / 8] |= (uint8_t)(1u << (i % 8));
                for (int i = 1; i <= 2; i++) np.unlocked[UNLOCK_HAT][i / 8] |= (uint8_t)(1u << (i % 8));
                fe.cust.hat = 0; fe.cust_sel = 8;
                press(FE_RIGHT);
                CHECK(fe.cust.hat == 1 && custom_hat_family(&cpak, fe.cust.hat_tex) == 0 && custom_rom(&cpak, CUST_HAT, fe.cust.hat_tex) >= 1 && custom_rom(&cpak, CUST_HAT, fe.cust.hat_tex) <= 2, "with Almost's caps the cap is the first hat (%d)", fe.cust.hat);
                int n = 0, start = fe.cust.deck;
                fe.cust_sel = 14;
                do { press(FE_RIGHT); n++; } while (fe.cust.deck != start && n < 200);
                CHECK(n == 13, "13 decks with Almost's six (%d)", n);
                fe_set_profile(&fe, &mp);
                fe.cust_sel = 16; press(FE_BACK);
            }
            fe.cpak = NULL;
        }
        /* game settings: the options on that page, back to the menu */
        fe.ms_sel = 3; press(FE_OK);
        CHECK(fe.state == FE_STATE_OPTIONS && fe.opt.page == OPT_GAME && fe.opt_return == FE_STATE_MYSKATE, "Game Settings opens the options on their page (state %d, page %d)", fe.state, fe.opt.page);
        press(FE_RIGHT); CHECK(set.stance == STANCE_REGULAR, "it works"); set.stance = STANCE_OWN;
        press(FE_BACK); CHECK(fe.state == FE_STATE_MYSKATE && fe.opt_return == FE_STATE_MAIN, "O leaves it for My Skate");
        fe.ms_sel = 4; press(FE_OK); CHECK(fe.state == FE_STATE_CREDITS && fe.credits_return == FE_STATE_MYSKATE, "credits");
        press(FE_BACK); CHECK(fe.state == FE_STATE_MYSKATE, "O: back to My Skate");
        /* the profile slots (the caller fills them) */
        fe.ms_sel = 0; fe.cur_slot = 0; press(FE_OK);
        CHECK(fe.state == FE_STATE_PROFILES && fe.need_slots == 1, "Select Profile asks for the slot list");
        fe.need_slots = 0;
        fe.slot[0] = (FeSlot){ 1, 12, 7, 1, 3725 }; fe.slot[1] = (FeSlot){ 0 }; fe.slot[2] = (FeSlot){ 1, 6, 31, 3, 40000 };
        idle(0.3f); shot("profiles");
        press(FE_OK); CHECK(fe.state == FE_STATE_MYSKATE && fe.req_slot == -1, "OK on the profile in use changes nothing");
        press(FE_OK); fe.need_slots = 0;
        press(FE_DOWN); press(FE_OK); CHECK(fe.req_slot == 1 && fe.state == FE_STATE_PROFILES, "OK on another slot asks for it (%d)", fe.req_slot);
        fe.req_slot = -1; fe.cur_slot = 1; fe.slot[1] = (FeSlot){ 1, 12, 0, 0, 0 };
        press(FE_SQUARE); CHECK(fe.pf_confirm == 1, "Square asks before clearing");
        idle(0.2f); shot("profiles_confirm");
        press(FE_BACK); CHECK(fe.pf_confirm == 0 && fe.req_delete == -1 && fe.state == FE_STATE_PROFILES, "O: no");
        press(FE_SQUARE); press(FE_OK); CHECK(fe.req_delete == 1 && fe.pf_confirm == 0, "X: clear it (%d)", fe.req_delete);
        fe.req_delete = -1;
        fe.slot[1].used = 0; press(FE_SQUARE); CHECK(fe.pf_confirm == 0, "an empty slot cannot be cleared");
        fe.slot[1].used = 1;
        press(FE_BACK); CHECK(fe.state == FE_STATE_MYSKATE, "back");
        /* the Log Book */
        fe.ms_sel = 2; press(FE_OK); CHECK(fe.state == FE_STATE_LOGBOOK && fe.lb_sel == 0, "the Log Book menu");
        idle(0.3f); shot("logbook");
        press(FE_OK); CHECK(fe.state == FE_STATE_LOGPAGE && fe.lb_page == 0, "statistics");
        idle(0.2f); shot("logbook_stats");
        press(FE_DOWN); CHECK(fe.lb_scroll == 1, "the table scrolls by a row: 11 rows, 10 fit (%d)", fe.lb_scroll);
        for (int i = 0; i < 20; i++) press(FE_DOWN);
        CHECK(fe.lb_scroll == 1, "and stops at its end (%d)", fe.lb_scroll);
        press(FE_UP); CHECK(fe.lb_scroll == 0, "up again");
        press(FE_DOWN);
        shot("logbook_stats_end");
        press(FE_BACK); press(FE_DOWN); press(FE_OK); CHECK(fe.lb_page == 1, "personal bests");
        idle(0.2f); shot("logbook_bests");
        press(FE_BACK); press(FE_DOWN); press(FE_OK); CHECK(fe.lb_page == 2 && fe.tb_sel == 0, "the trick book");
        idle(0.2f); shot("logbook_tricks");
        for (int i = 0; i < 26; i++) press(FE_DOWN);
        shot("logbook_tricks_grabs");
        press(FE_R); press(FE_R); CHECK(fe.tb_sel == 42, "L / R page the list (%d)", fe.tb_sel);
        press(FE_L); CHECK(fe.tb_sel == 34, "back a page (%d)", fe.tb_sel);
        for (int i = 0; i < 200; i++) press(FE_DOWN);
        CHECK(fe.tb_sel >= 0 && fe.tb_sel < STATS_NTRICKS, "the list wraps (%d)", fe.tb_sel);
        press(FE_BACK); press(FE_DOWN); press(FE_OK); CHECK(fe.lb_page == 3, "unlockables");
        idle(0.2f); shot("logbook_unlocks");
        press(FE_BACK); press(FE_BACK); CHECK(fe.state == FE_STATE_MYSKATE, "back to My Skate");
        press(FE_BACK); CHECK(fe.state == FE_STATE_MAIN && fe.main_sel == 1, "and to the main menu on its entry");
        fe_set_profile(&fe, &prof);
    }
    press(FE_DOWN); CHECK(fe.main_sel == 3 && fe.ev_move, "down skips the entries that do not exist yet and lands on the tutorial (%d)", fe.main_sel);
    {   /* the tutorial list: ten entries, ticks for the done ones, a launch at the Community Center, the next one that is not done after a win */
        static unsigned char done[FE_DONE_SIZE];
        fe.done = done;
        press(FE_OK); CHECK(fe.state == FE_STATE_TUTORIAL && fe.ntut == 10 && fe.tut_sel == 0, "OK on tutorial opens the list (state %d, %d tutorials)", fe.state, fe.ntut);
        idle(0.3f); shot("tutorial");
        press(FE_DOWN); press(FE_DOWN); CHECK(fe.tut_sel == 2, "down moves in the list");
        for (int i = 0; i < 7; i++) press(FE_DOWN);
        CHECK(fe.tut_sel == 9, "the last tutorial");
        press(FE_DOWN); CHECK(fe.tut_sel == 0, "the list wraps");
        done[93] = 1; done[94] = 1; done[97] = 1;
        press(FE_DOWN); press(FE_DOWN); press(FE_DOWN); shot("tutorial_done");
        press(FE_BACK); CHECK(fe.state == FE_STATE_MAIN && fe.main_sel == 3, "O goes back to the main menu on the tutorial entry");
        press(FE_OK); fe.tut_sel = 0;
        int go = press(FE_OK);
        CHECK(go == 1 && fe.state == FE_STATE_LAUNCH && fe.mode == FE_MODE_TUTORIAL, "OK on a tutorial launches it (go %d, state %d, mode %d)", go, fe.state, fe.mode);
        CHECK(fe_tutorial_chal(&fe) == 93 && strcmp(fe.loc[fe.loc_sel].key, "CommCentre") == 0, "tutorial 93 at the Community Center (%d, %s)", fe_tutorial_chal(&fe), fe.loc[fe.loc_sel].key);
        fe_back_to_tutorials(&fe, 1);
        CHECK(fe.state == FE_STATE_TUTORIAL && fe.tut_sel == 2, "after a win: the next one that is not done (%d)", fe.tut_sel);
        fe_back_to_tutorials(&fe, 0);
        CHECK(fe.tut_sel == 2, "after a loss: the same one");
        for (int i = 93; i <= 102; i++) done[i] = 1;
        fe_back_to_tutorials(&fe, 1);
        CHECK(fe.tut_sel == 3, "all done: just the following one (%d)", fe.tut_sel);
        fe.done = NULL;
        fe_set_profile(&fe, &prof);
        shot("tutorial_nodone");
        fe_back_to_menu(&fe); CHECK(fe.state == FE_STATE_TUTORIAL, "back from a tutorial game: the tutorial list");
        press(FE_BACK); CHECK(fe.state == FE_STATE_MAIN, "back to the menu");
        fe.mode = FE_MODE_FREESKATE;
    }
    press(FE_DOWN); CHECK(fe.main_sel == 4, "freeskate");
    press(FE_DOWN); CHECK(fe.main_sel == FE_NMAIN - 1, "then options (%d)", fe.main_sel);
    {   /* the options: information lines, a controls preset, a custom button */
        press(FE_OK); CHECK(fe.state == FE_STATE_OPTIONS && fe.opt.page == OPT_ROOT, "OK on options opens them");
        idle(0.3f); shot("options_root");
        /* the trick guide: categories, then the tricks of one with the buttons of the mapping in use */
        press(FE_DOWN); press(FE_OK); CHECK(fe.opt.page == OPT_GUIDE, "trick guide page");
        shot("options_guide");
        for (int i = 0; i < 6; i++) press(FE_DOWN);
        press(FE_OK); CHECK(fe.opt.page == OPT_GUIDE_LIST && fe.opt.cat == 6 && fe.opt.sel[OPT_GUIDE_LIST] == 0, "the grinds category opens its list (page %d, cat %d)", fe.opt.page, fe.opt.cat);
        for (int i = 0; i < 11; i++) press(FE_DOWN);
        shot("options_guide_grinds");
        press(FE_BACK); CHECK(fe.opt.page == OPT_GUIDE, "back to the categories");
        press(FE_UP);
        press(FE_OK); CHECK(fe.opt.cat == 5, "the grabs (%d)", fe.opt.cat);
        for (int i = 0; i < 20; i++) press(FE_DOWN);
        shot("options_guide_grabs");
        press(FE_BACK); press(FE_BACK); CHECK(fe.opt.page == OPT_ROOT, "back to the root");
        /* the gravity page: left / right step the percentages, reset puts them back */
        press(FE_DOWN); press(FE_OK); CHECK(fe.opt.page == OPT_GRAVITY, "gravity page (%d)", fe.opt.page);
        press(FE_RIGHT); CHECK(set.tune[TUNE_GRAVITY] == 110 && fe.opt.changed, "right: gravity 110 (%d)", set.tune[TUNE_GRAVITY]);
        fe.opt.changed = 0;
        press(FE_LEFT); press(FE_LEFT); CHECK(set.tune[TUNE_GRAVITY] == 90 && fe.opt.changed, "left twice: 90");
        press(FE_DOWN); press(FE_OK); CHECK(set.tune[TUNE_OLLIE] == 110, "OK on a value steps it up");
        shot("options_gravity");
        fe.opt.sel[OPT_GRAVITY] = TUNE_COUNT; press(FE_OK);
        CHECK(set.tune[TUNE_GRAVITY] == 100 && set.tune[TUNE_OLLIE] == 100, "reset to 100");
        press(FE_BACK); CHECK(fe.opt.page == OPT_ROOT, "back from gravity");
        press(FE_DOWN); press(FE_OK); CHECK(fe.opt.page == OPT_INFO, "information page");
        press(FE_OK); CHECK(set.info[INFO_FPS] == 1 && fe.opt.changed, "OK turns the first line on");
        fe.opt.changed = 0;
        press(FE_DOWN); press(FE_RIGHT); CHECK(set.info[INFO_SPEED] == 1 && set.info[INFO_FPS] == 1 && fe.opt.changed, "right turns the next one on, the first stays on");
        press(FE_LEFT); CHECK(set.info[INFO_SPEED] == 0, "left turns it off again");
        for (int i = 0; i < 5; i++) press(FE_DOWN);
        shot("options_info");
        for (int i = 0; i < 6; i++) press(FE_DOWN);
        CHECK(fe.opt.sel[OPT_INFO] == INFO_COUNT + 1 || fe.opt.sel[OPT_INFO] == 0, "the list wraps (%d)", fe.opt.sel[OPT_INFO]);
        fe.opt.sel[OPT_INFO] = INFO_COUNT; press(FE_OK);
        { int all = 1; for (int i = 0; i < INFO_COUNT; i++) all &= set.info[i]; CHECK(all, "show all"); }
        fe.opt.sel[OPT_INFO] = INFO_COUNT + 1; press(FE_OK);
        { int none = 1; for (int i = 0; i < INFO_COUNT; i++) none &= !set.info[i]; CHECK(none, "hide all"); }
        press(FE_BACK); CHECK(fe.opt.page == OPT_ROOT, "back to the root page");
        {   /* the game settings: stance, units, volumes (the music row only when the game has music) */
            fe.opt.sel[OPT_ROOT] = 4; press(FE_OK); CHECK(fe.opt.page == OPT_GAME, "the Game settings page (%d)", fe.opt.page);
            CHECK(set.stance == STANCE_OWN && set.units == UNITS_METERS && set.sfx_vol == VOL_MAX, "defaults");
            idle(0.3f); shot("options_game");
            fe.opt.changed = 0;
            press(FE_RIGHT); CHECK(set.stance == STANCE_REGULAR && fe.opt.changed, "right: regular");
            press(FE_OK); CHECK(set.stance == STANCE_GOOFY, "OK steps on: goofy");
            press(FE_LEFT); press(FE_LEFT); CHECK(set.stance == STANCE_OWN, "left twice: back to the skater's own");
            press(FE_LEFT); CHECK(set.stance == STANCE_GOOFY, "and round");
            press(FE_DOWN); press(FE_RIGHT); CHECK(set.units == UNITS_FEET, "units: feet");
            press(FE_DOWN);
            for (int i = 0; i < 3; i++) press(FE_LEFT);
            CHECK(set.sfx_vol == VOL_MAX - 3, "sound effects 70%% (%d)", set.sfx_vol);
            for (int i = 0; i < 12; i++) press(FE_LEFT);
            CHECK(set.sfx_vol == 0, "the arrows stop at silence (%d)", set.sfx_vol);
            set.sfx_vol = VOL_MAX; press(FE_RIGHT); CHECK(set.sfx_vol == VOL_MAX, "and at full");
            press(FE_OK); CHECK(set.sfx_vol == 0, "OK wraps round from full to silent");
            set.sfx_vol = VOL_MAX;
            press(FE_DOWN); CHECK(fe.opt.sel[OPT_GAME] == 0, "no music row: the list wraps after three");
            fe.have_music = 1;
            press(FE_UP); press(FE_LEFT); CHECK(set.music_vol == VOL_MAX - 1 && fe.opt.sel[OPT_GAME] == 3, "with music there is a fourth row (%d)", set.music_vol);
            idle(0.3f); shot("options_game_music");
            fe.have_music = 0;
            set.stance = STANCE_OWN; set.units = UNITS_METERS; set.sfx_vol = set.music_vol = VOL_MAX;
            press(FE_BACK); CHECK(fe.opt.page == OPT_ROOT, "back to the root");
            fe.regular = 1; fe.opt.sel[OPT_ROOT] = 1; press(FE_OK); fe.opt.sel[OPT_GUIDE] = 4; press(FE_OK);
            CHECK(fe.opt.page == OPT_GUIDE_LIST && fe.opt.cat == 4, "the spins category");
            idle(0.2f); shot("options_guide_spins_regular");
            fe.regular = 0; press(FE_BACK); press(FE_BACK);
        }
        fe.opt.sel[OPT_ROOT] = 0; press(FE_OK); CHECK(fe.opt.page == OPT_CONTROLS && fe.opt.sel[OPT_CONTROLS] == PRESET_DEFAULT, "controls page starts on the preset in use");
        press(FE_DOWN); press(FE_OK); CHECK(set.preset == PRESET_SWAP_POPS && settings_ctl_bits(&set, PBM(PB_TRIANGLE)) == 1, "swapped pops: Triangle pops");
        press(FE_UP);
        press(FE_DOWN); press(FE_DOWN);
        shot("options_controls");
        press(FE_OK); CHECK(set.preset == PRESET_CIRCLE_POP && settings_ctl_bits(&set, PBM(PB_CIRCLE)) == 1, "the circle pop preset puts the ollie on Circle");
        press(FE_DOWN); press(FE_DOWN); press(FE_OK); CHECK(fe.opt.page == OPT_MAP, "custom opens the button page");
        shot("options_map");
        press(FE_OK); CHECK(fe.opt.capture == ACT_POP, "OK waits for a button");
        shot("options_capture");
        press(FE_START); CHECK(fe.opt.capture == -1 && set.map[ACT_POP] == PB_CIRCLE, "start cancels");
        press(FE_OK); press(FE_TRIANGLE);
        CHECK(fe.opt.capture == -1 && set.map[ACT_POP] == PB_TRIANGLE && set.preset == PRESET_CUSTOM, "a pressed button is given to the action");
        { unsigned seen = 0; for (int a = 0; a < ACT_COUNT; a++) seen |= PBM(set.map[a]); CHECK(seen == (1u << PB_COUNT) - 1, "still one action per button (swap)"); }
        press(FE_BACK); CHECK(fe.opt.page == OPT_CONTROLS, "back from the buttons");
        press(FE_BACK); press(FE_BACK); CHECK(fe.state == FE_STATE_MAIN, "back to the main menu");
        CHECK(fe.main_sel == FE_NMAIN - 1, "the menu stays on options");
    }
    press(FE_DOWN); CHECK(fe.main_sel == 0, "the menu wraps");
    press(FE_BACK); CHECK(fe.state == FE_STATE_TITLE, "O goes back to the title");
    idle(0.5f); press(FE_START); idle(0.5f);
    /* ---- the career: a new profile chooses a skater (the unlocked ones), then the map: area -> place -> events ---- */
    press(FE_OK); CHECK(fe.state == FE_STATE_SKATER && fe.mode == FE_MODE_CAREER, "start career -> skater select (state %d)", fe.state);
    CHECK(fe.nskater == 3 && fe.skater[fe.skater_sel].index == 12, "a new career can choose between the three starting skaters, Aaron first (%d skaters, index %d)", fe.nskater, fe.skater[fe.skater_sel].index);
    idle(0.5f); shot("skater_career");
    press(FE_DOWN);
    { int want = fe.skater[fe.skater_sel].index; press(FE_OK);
      CHECK(fe.state == FE_STATE_STORY && prof.skater == want && fe.ev_profile == 1, "the choice goes to the profile (skater %d, state %d), then the first scene", prof.skater, fe.state); fe.ev_profile = 0; }
    CHECK(fe.story.scene == 1 && fe.npages == 2, "the story of a new career opens with Reda's introduction");
    press(FE_OK); press(FE_OK); CHECK(fe.state == FE_STATE_CMAP_AREA && progress_seen(&prof, 1) && fe.ev_profile == 1, "read: the map; the scene is marked (state %d)", fe.state); fe.ev_profile = 0;
    CHECK(fe.ncm_areas == 5, "five areas on the map (%d)", fe.ncm_areas);
    idle(0.4f); shot("map_areas");
    while (fe.cm_areas[fe.cm_area_sel] != 1) press(FE_DOWN);                    /* Parks: the Community Center is there */
    press(FE_OK); CHECK(fe.state == FE_STATE_CMAP_LOC && fe.ncm_locs >= 3, "the area opens its places (%d)", fe.ncm_locs);
    shot("map_places");
    for (int i = 0; i < fe.ncm_locs && strcmp(fe.loc[fe.cm_locs[fe.cm_loc_sel]].key, "CommCentre") != 0; i++) press(FE_DOWN);
    CHECK(strcmp(fe.loc[fe.cm_locs[fe.cm_loc_sel]].key, "CommCentre") == 0, "the Community Center is in Parks");
    press(FE_OK); CHECK(fe.state == FE_STATE_CMAP_CHAL && fe.ncm_chals == 14, "its events: 5 tutorials + 9 others, no extra tutorials (%d)", fe.ncm_chals);
    CHECK(fe.cm_chals[fe.cm_chal_sel] == 93, "the cursor starts on the first open event (%d)", fe.cm_chals[fe.cm_chal_sel]);
    CHECK(fe_event_status(&fe, 93) == FE_EV_OPEN && fe_event_status(&fe, 3) == FE_EV_LOCKED && fe_event_status(&fe, 98) == FE_EV_LOCKED, "a new career: the tutorials are open, the rest is locked");
    idle(0.3f); shot("map_events");
    { int go = press(FE_OK);
      CHECK(go == 1 && fe.state == FE_STATE_LAUNCH && fe.launch_chal == 93 && fe.mode == FE_MODE_CAREER && strcmp(fe.loc[fe.loc_sel].key, "CommCentre") == 0, "Go To Event launches the event at its place (go %d, chal %d)", go, fe.launch_chal); }
    for (int i = 93; i <= 97; i++) progress_complete(&prof, &career, i, NULL);
    fe_back_to_menu(&fe);
    CHECK(fe.state == FE_STATE_CMAP_CHAL && fe.launch_chal == -1, "back from an event: its place's event list");
    CHECK(fe_event_status(&fe, 93) == FE_EV_DONE && fe_event_status(&fe, 3) == FE_EV_OPEN && fe_event_status(&fe, 4) == FE_EV_LOCKED && fe_event_status(&fe, 9) == FE_EV_LOCKED, "pool 1 is open: Ride The Rail yes, Filming 101 not yet");
    CHECK(fe.cm_chals[fe.cm_chal_sel] == 3, "the cursor moved to the open event (%d)", fe.cm_chals[fe.cm_chal_sel]);
    idle(0.3f); shot("map_events_done");
    press(FE_DOWN); { int cur = fe.cm_chals[fe.cm_chal_sel]; CHECK(fe_event_status(&fe, cur) != FE_EV_OPEN, "the next row (%d) is not open", cur); CHECK(press(FE_OK) == 0 && fe.state == FE_STATE_CMAP_CHAL, "OK on an event that is not open does nothing"); shot("map_locked"); }
    press(FE_BACK); CHECK(fe.state == FE_STATE_CMAP_LOC, "O goes back to the places");
    for (int i = 0; i < fe.ncm_locs && strcmp(fe.loc[fe.cm_locs[fe.cm_loc_sel]].key, "ElementarySchool") != 0; i++) press(FE_DOWN);
    press(FE_OK);
    while (fe.cm_chals[fe.cm_chal_sel] != 5) press(FE_DOWN);                    /* EduSKATE: a S.K.A.T.E. game against Sammy */
    CHECK(fe_event_status(&fe, 5) == FE_EV_OPEN && career_chal_runnable(&career, 5), "EduSKATE is open and runnable");
    shot("map_skate");
    CHECK(press(FE_SQUARE) == 0 && !prof.done[5], "Square does nothing any more (there is nothing to skip: every event can be played)");
    { int go = press(FE_OK); CHECK(go == 1 && fe.launch_chal == 5 && strcmp(fe.loc[fe.loc_sel].key, "ElementarySchool") == 0, "OK launches EduSKATE (go %d, chal %d)", go, fe.launch_chal); }
    progress_complete(&prof, &career, 5, NULL);
    fe_back_to_menu(&fe);
    CHECK(fe.state == FE_STATE_CMAP_CHAL && fe_event_status(&fe, 5) == FE_EV_DONE, "back from the event: done");
    press(FE_BACK); press(FE_BACK); CHECK(fe.state == FE_STATE_CMAP_AREA, "back to the areas");
    press(FE_SQUARE); CHECK(fe.state == FE_STATE_SKATER && fe.skater_back == FE_STATE_CMAP_AREA && fe.nskater == 3, "Square on the map: choose another skater");
    press(FE_BACK); CHECK(fe.state == FE_STATE_CMAP_AREA, "O goes back to the map");
    progress_complete(&prof, &career, 20, NULL);                                /* (a skater unlock arrives with the progress) */
    press(FE_BACK); CHECK(fe.state == FE_STATE_MAIN, "O leaves the map");
    press(FE_OK); CHECK(fe.state == FE_STATE_CMAP_AREA, "a career in progress goes straight to the map");
    press(FE_BACK);
    fe.done = prof.done;
    /* ---- freeskate: every skater, every place ---- */
    press(FE_DOWN); press(FE_DOWN); press(FE_DOWN); CHECK(fe.main_sel == 4, "freeskate (%d)", fe.main_sel);
    press(FE_OK); CHECK(fe.state == FE_STATE_SKATER && fe.mode == FE_MODE_FREESKATE && fe.nskater == 19, "freeskate -> skater select with all 19 (%d)", fe.nskater);
    idle(0.5f); shot("skater");
    press(FE_RIGHT); CHECK(fe.skater_sel >= 0, "right moves");
    for (int i = 0; i < 12; i++) press(FE_DOWN);
    shot("skater_scrolled");
    press(FE_OK); CHECK(fe.state == FE_STATE_LOCATION, "skater -> location");
    CHECK(fe.area_sel == fe.loc[fe.loc_sel].area, "area_sel follows the chosen place");
    for (int i = 1; i < fe.nloc; i++) CHECK(fe.loc[i - 1].area <= fe.loc[i].area, "the list is grouped by area (%d)", i);
    shot("location_0");
    int a0 = fe.area_sel;
    press(FE_RIGHT); CHECK(fe.area_sel != a0 && fe.loc[fe.loc_sel].area == fe.area_sel, "right jumps to the next area (%d -> %d) and picks its first place", a0, fe.area_sel);
    shot("location_area2");
    for (int i = 0; i < 5; i++) press(FE_RIGHT);
    for (int i = 0; i < 4; i++) { int before = fe.area_sel; press(FE_LEFT); CHECK(fe.loc[fe.loc_sel].area == fe.area_sel && fe.area_sel != before, "left: previous area (%d -> %d)", before, fe.area_sel); }
    press(FE_DOWN); press(FE_DOWN);
    shot("location_9");
    press(FE_BACK); CHECK(fe.state == FE_STATE_SKATER, "O goes back to the skater");
    press(FE_OK); int launched = press(FE_OK);
    CHECK(launched == 1 && fe.state == FE_STATE_LAUNCH, "OK on a location launches");
    CHECK(fe.mode == FE_MODE_FREESKATE && fe.skater_sel >= 0 && fe.loc_sel >= 0, "the choices are readable");
    fe_back_to_menu(&fe); CHECK(fe.state == FE_STATE_LOCATION, "back from a game: the location list");

    {   /* ---- the story: a profile in the middle of Paris (the scenes that are due, the intro of the event), then a whole career's worth with the signing, the ending and the credits ---- */
        static Profile q;
        profile_init(&q, &career, "STORY");
        for (uint32_t i = 0; i < 3; i++) for (int k = 0; k < career.pool[i].nchal; k++) q.done[career.pool[i].chal[k]] = 1;
        progress_complete(&q, &career, 99, NULL);
        fe_set_profile(&fe, &q);
        fe.state = FE_STATE_MAIN; fe.main_sel = 0; fe.mode = FE_MODE_FREESKATE;
        press(FE_OK);
        CHECK(fe.state == FE_STATE_STORY && fe.story.kind == STORY_SCENE && fe.story.scene == 1 && fe.npages == 2, "the map first shows the scene that is due: Reda's introduction (state %d, scene %d, %d pages)", fe.state, fe.story.scene, fe.npages);
        idle(0.3f); shot("story_scene");
        { int f0 = fe.scene_flip; press(FE_SQUARE); CHECK(fe.scene_flip == f0, "SQUARE does nothing without the 3D view");
          fe.scene3d = 1; idle(0.2f); shot("story_scene3d");
          press(FE_SQUARE); CHECK(fe.scene_flip == !f0 && fe.page == 0 && fe.state == FE_STATE_STORY, "SQUARE turns the guest round, not the page");
          press(FE_SQUARE); CHECK(fe.scene_flip == f0, "and back");
          fe.scene3d = 0; }
        press(FE_OK); CHECK(fe.state == FE_STATE_STORY && fe.page == 1 && !progress_seen(&q, 1), "X turns the page; the scene is not marked until it is read");
        press(FE_OK); CHECK(fe.state == FE_STATE_STORY && fe.story.scene == 2 && progress_seen(&q, 1) && fe.ev_profile == 1, "then scene 2 (the trip to Paris): scene 1 is marked and the profile is to be saved");
        fe.ev_profile = 0;
        idle(0.3f); press(FE_BACK); CHECK(fe.state == FE_STATE_CMAP_AREA && progress_seen(&q, 2), "O skips the rest of a scene; nothing more is due: the map");
        CHECK(fe.ncm_areas == 5, "the map");
        while (fe.cm_areas[fe.cm_area_sel] != 4) press(FE_DOWN);                  /* Real World: Paris */
        press(FE_OK);
        for (int i = 0; i < fe.ncm_locs && strcmp(fe.loc[fe.cm_locs[fe.cm_loc_sel]].key, "Paris") != 0; i++) press(FE_DOWN);
        press(FE_OK);
        CHECK(fe.state == FE_STATE_CMAP_CHAL && fe.cm_chals[fe.cm_chal_sel] == 10, "Paris Connection is the open event (%d)", fe.cm_chals[fe.cm_chal_sel]);
        { int go = press(FE_OK);
          CHECK(go == 0 && fe.state == FE_STATE_STORY && fe.story.scene == 3 && fe.story_launch == 10 && fe.launch_chal == -1, "Go To Event shows the scene before it first (go %d, scene %d)", go, fe.story.scene); }
        idle(0.3f); shot("story_intro");
        press(FE_OK);
        { int go = press(FE_OK);
          CHECK(go == 1 && fe.state == FE_STATE_LAUNCH && fe.launch_chal == 10 && progress_seen(&q, 3), "when it is read the event starts (go %d, chal %d)", go, fe.launch_chal); }
        fe_back_to_menu(&fe);
        CHECK(fe.state == FE_STATE_CMAP_CHAL, "back from the event without a win: nothing new, the list");
        { int go = press(FE_OK); CHECK(go == 1 && fe.launch_chal == 10, "the intro scene is shown once: the second time it goes straight in (%d)", go); }
        progress_complete(&q, &career, 10, NULL);
        fe_back_to_menu(&fe);
        CHECK(fe.state == FE_STATE_STORY && fe.story.scene == 4, "after the win: scene 4");
        press(FE_BACK); CHECK(fe.state == FE_STATE_CMAP_CHAL, "and back to the list");
        press(FE_BACK); press(FE_BACK);

        /* the sponsors page: the last row of the areas */
        CHECK(fe.state == FE_STATE_CMAP_AREA, "the areas again");
        fe.cm_area_sel = 0; press(FE_UP); CHECK(fe.cm_area_sel == fe.ncm_areas, "up from the first area: the Sponsors row (%d)", fe.cm_area_sel);
        idle(0.3f); shot("map_sponsors_row");
        press(FE_OK); CHECK(fe.state == FE_STATE_SPONSORS, "OK opens the sponsors page");
        idle(0.3f); shot("sponsors_none");
        press(FE_BACK); CHECK(fe.state == FE_STATE_CMAP_AREA && fe.cm_area_sel == fe.ncm_areas, "O goes back to the map on the same row");
        press(FE_BACK);

        /* a whole career's worth of story: the profile has finished everything and has seen none of it */
        static Profile z;
        profile_init(&z, &career, "ALL");
        for (int guard = 0; guard < 200 && !progress_finished(&z, &career); guard++) {
            int open[64], k = progress_open_list(&z, &career, open, 64);
            if (!k) break;
            int pick = open[k - 1];                                              /* the last sponsor of each pool, for a change */
            progress_complete(&z, &career, pick, NULL);
        }
        CHECK(progress_finished(&z, &career), "the whole career is done");
        fe_set_profile(&fe, &z);
        fe.state = FE_STATE_MAIN; fe.main_sel = 0;
        press(FE_OK);
        int items = 0, shots = 0, signed_seen = 0, covers = 0, credits_seen = 0;
        for (int guard = 0; guard < 120 && fe.state != FE_STATE_CMAP_AREA; guard++) {
            items++;
            if (fe.state == FE_STATE_STORY && fe.story.kind == STORY_SIGNED) { signed_seen++; idle(0.3f); if (signed_seen == 1) shot("story_signed"); }
            if (fe.state == FE_STATE_STORY && fe.story.kind == STORY_ENDING) {
                covers++; idle(0.3f); shot("story_cover");
                press(FE_OK); idle(0.3f); shot("story_end_text");
            }
            if (fe.state == FE_STATE_STORY && fe.story.kind == STORY_SCENE && fe.story.chal >= 0 && career_sponsor_name(&career, fe.story.chal) && shots++ == 0) { idle(0.3f); shot("story_sponsor_scene"); }
            if (fe.state == FE_STATE_CREDITS) {
                credits_seen++;
                idle(2.0f); shot("credits_start");
                idle(50.0f); shot("credits_middle");
                idle(40.0f); shot("credits_late");
                CHECK(fe.state == FE_STATE_CREDITS && fe.credits_y > 1000.0f && fe.cr_h > 1500.0f, "the credits scroll (%.0f of %.0f)", fe.credits_y, fe.cr_h);
                if (fe.state == FE_STATE_CREDITS) press(FE_BACK);
                continue;
            }
            press(FE_OK);
        }
        CHECK(fe.state == FE_STATE_CMAP_AREA, "all of it read: the map (state %d after %d screens)", fe.state, items);
        CHECK(signed_seen == 4 && covers == 1 && credits_seen == 1, "four signings, one cover, the credits (%d %d %d)", signed_seen, covers, credits_seen);
        { StoryItem it; CHECK(!story_next(&z, &career, &it), "nothing is due any more"); }
        fe_set_profile(&fe, &z);
        fe.cm_area_sel = 0; press(FE_UP); press(FE_OK); CHECK(fe.state == FE_STATE_SPONSORS, "the sponsors page of a finished career");
        shot("sponsors_all");
        press(FE_DOWN); press(FE_DOWN); shot("sponsors_all_wheels");
        press(FE_BACK); press(FE_BACK);
        CHECK(fe.state == FE_STATE_MAIN, "back at the main menu");
        fe_set_profile(&fe, &prof);
    }
    {   /* the credits from the options */
        fe.state = FE_STATE_MAIN; fe.main_sel = FE_NMAIN - 1;
        press(FE_OK); CHECK(fe.state == FE_STATE_OPTIONS, "options");
        for (int i = 0; i < 5; i++) press(FE_DOWN);
        idle(0.3f); shot("options_credits_row");
        press(FE_OK); CHECK(fe.state == FE_STATE_CREDITS && fe.credits_return == FE_STATE_OPTIONS, "the Credits entry opens the credits (state %d)", fe.state);
        idle(1.0f); shot("credits_options");
        press(FE_BACK); CHECK(fe.state == FE_STATE_OPTIONS && fe.opt.page == OPT_ROOT, "O goes back to the options");
        press(FE_BACK); CHECK(fe.state == FE_STATE_MAIN, "and to the menu");
        fe_options_open(&fe); fe.opt.no_credits = 1;
        fe.state = FE_STATE_OPTIONS;
        for (int i = 0; i < 9; i++) press(FE_DOWN);
        CHECK(fe.opt.sel[OPT_ROOT] == 9 % 6 && fe.state == FE_STATE_OPTIONS, "without credits the root page has six rows (row %d)", fe.opt.sel[OPT_ROOT]);
        fe.opt.sel[OPT_ROOT] = 5; press(FE_OK); CHECK(fe.state == FE_STATE_MAIN, "the last row is Back there");
        fe.mode = FE_MODE_FREESKATE; fe.state = FE_STATE_LOCATION;
    }

    {   /* the pause menu over a picture */
        const char *items[5] = { "RESUME", "START CHALLENGE", "RESPAWN", "QUIT TO MENU", "" };
        sw_clear(&canvas, 0, 0, 0);
        fe_draw_back(&fe);
        fe_draw_menu(&fe, "PAUSED", items, NULL, 4, 1);
        char path[512]; snprintf(path, sizeof(path), "%s/pause.ppm", outdir); sw_save_ppm(&canvas, path);
    }

    {   /* the in-game HUD over a picture: a combo in progress at each multiplier level, and the challenge texts */
        Hud hud; hud_init(&hud, &pak);
        CHECK(hud.i_strip >= 0 && hud.i_fill >= 0 && hud.i_x15 >= 0 && hud.i_digit[2] >= 0 && hud.f_s >= 0, "the HUD sprites and fonts are in ui.pak");
        char tc[32]; hud_title_case("NOLLIE KICKFLIP", tc, sizeof(tc)); CHECK(strcmp(tc, "Nollie Kickflip") == 0, "title case: %s", tc);
        hud_title_case("BS 50-50", tc, sizeof(tc)); CHECK(strcmp(tc, "BS 50-50") == 0, "title case keeps short words: %s", tc);
        for (int lvl = 0; lvl <= 3; lvl++) {
            sw_clear(&canvas, 0, 0, 0);
            ui_reset_view(&ui);
            ui_image_cover(&ui, uipak_image(&pak, "loc_amphitheatre"), UI_WHITE);
            hud_text_right(&hud, &ui, hud.f_m, UI_W - 8, 6, "Score 1250", UI_WHITE);
            hud_combo(&hud, &ui, "SWITCH OLLIE", lvl == 3 ? "Line: " : "", 40L * (lvl + 1), lvl);
            hud_text_right(&hud, &ui, hud.f_s, UI_W - 8, 26, "Spillway Photo", UI_RGBA(255, 214, 40, 255));
            hud_text_right(&hud, &ui, hud.f_s, UI_W - 8, 42, "Kickflip  2/3", UI_WHITE);
            hud_text_right(&hud, &ui, hud.f_s, UI_W - 8, 57, "Grind the rail", UI_RGBA(120, 255, 120, 255));
            hud_text_center(&hud, &ui, hud.f_l, UI_W / 2.0f, 6, 1.0f, "1:05", UI_WHITE);
            hud_text_center(&hud, &ui, hud.f_l, UI_W / 2.0f, 78, 1.0f, "+240 pts", UI_RGBA(120, 255, 120, 255));
            if (lvl == 1) hud_digit(&hud, &ui, 3);
            if (lvl == 2) hud_text_center(&hud, &ui, hud.f_xl, UI_W / 2.0f, 82, 1.5f, "GO!", UI_RGBA(120, 255, 120, 255));
            char path[512]; snprintf(path, sizeof(path), "%s/hud_%d.ppm", outdir, lvl); sw_save_ppm(&canvas, path);
        }
    }

    {   /* a tutorial's tip on the HUD: top left, with the challenge's objectives on the right */
        Hud hud; hud_init(&hud, &pak);
        for (int pick = 0; pick < 2; pick++) {
            int chal = pick ? 100 : 93;
            const CareerChal *ch = &career.chal[chal];
            sw_clear(&canvas, 0, 0, 0);
            ui_reset_view(&ui);
            ui_image_cover(&ui, uipak_image(&pak, "loc_commcentre"), UI_WHITE);
            hud_text_right(&hud, &ui, hud.f_m, UI_W - 8, 6, "Score 0", UI_WHITE);
            hud_text_right(&hud, &ui, hud.f_s, UI_W - 8, 26, career_str(&career, ch->name_off), UI_RGBA(255, 214, 40, 255));
            const CareerObj *ob = career_objs(&career, ch);
            for (int i = 0; i < ch->nobj; i++)
                hud_text_right(&hud, &ui, hud.f_s, UI_W - 8, 42.0f + 15.0f * (float)i, career_str(&career, ob[i].text_off)[0] ? career_str(&career, ob[i].text_off) : "(no text)", i == 0 ? UI_RGBA(120, 255, 120, 255) : UI_WHITE);
            const char *tip = tutorial_tip(chal, pick ? 1 : 3);
            char ex[160], *ln = ex; float ty = 6.0f;
            settings_expand_buttons(&set, tip, ex, sizeof(ex));
            for (;;) { char *nl = strchr(ln, 0x0a); if (nl) *nl = 0; hud_text(&hud, &ui, hud.f_s, 8, ty, ln, UI_WHITE); ty += 15.0f; if (!nl) break; ln = nl + 1; }
            char path[512]; snprintf(path, sizeof(path), "%s/hud_tutorial_%d.ppm", outdir, chal); sw_save_ppm(&canvas, path);
        }
    }

    /* held up / down repeats */
    int sel0 = fe.loc_sel;
    { FeInput in = { FE_DOWN, FE_DOWN }; fe_update(&fe, &in, 1.0f / 60); in.pressed = 0; for (int i = 0; i < 90; i++) fe_update(&fe, &in, 1.0f / 60); }
    CHECK(fe.loc[fe.loc_sel].area == fe.area_sel && fe.loc_sel != sel0, "holding down keeps scrolling");

    /* with no skater files the skater screen is skipped */
    { Frontend f2; unsigned char none[32] = { 0 }; fe_init(&f2, &ui, &career, none, lv_ok); f2.state = FE_STATE_MAIN; FeInput in = { FE_OK, FE_OK }; fe_update(&f2, &in, 0.016f);
      CHECK(f2.state == FE_STATE_LOCATION, "no skaters: straight to the locations"); }

    printf(fails ? "%d FAILURES\n" : "all front-end tests passed\n", fails);
    return fails != 0;
}
