/* My Skate (M9): the menu (Select Profile, Customize, Log Book, Game Settings, Credits), the profile slots, and the Log Book (statistics, personal bests, the trick book, the unlockables).
 * Part of the front end (frontend.h): same state machine and look; no PSP dependencies. The files are the caller's: the profile screen only sets requests (fe->req_slot / req_delete). */
#include "frontend.h"
#include "hud.h"
#include "stats.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define COL_DIM UI_RGBA(200, 205, 205, 255)
#define COL_GREEN UI_RGBA(120, 255, 120, 255)
#define TABLE_ROWS 10

enum { MS_PROFILE = 0, MS_CUSTOMIZE, MS_LOGBOOK, MS_SETTINGS, MS_CREDITS, MS_ROWS };
static const char *const MS_NAME[MS_ROWS] = { "select profile", "customize", "log book", "game settings", "credits" };
static const unsigned char MS_ENABLED[MS_ROWS] = { 1, 1, 1, 1, 1 };
enum { LB_STATS = 0, LB_BESTS, LB_TRICKS, LB_UNLOCKS, LB_ROWS };
static const char *const LB_NAME[LB_ROWS] = { "Statistics", "Personal Bests", "Trick Book", "Unlockables" };

static void enter(Frontend *fe, int state)
{
    fe->state = state;
    fe->t = 0.0f;
    fe->rep_dir = 0;
}

static int wrap(int v, int n) { return n <= 0 ? 0 : (v % n + n) % n; }

static void cust_fix(Frontend *fe);

void fe_book_open(Frontend *fe, int state)
{
    if (state == FE_STATE_PROFILES) { fe->need_slots = 1; fe->pf_sel = fe->cur_slot; fe->pf_confirm = 0; }
    if (state == FE_STATE_LOGPAGE) fe->lb_scroll = 0;
    if (state == FE_STATE_CUSTOM) {                                                  /* start from the skater made before, or from the first choice of everything */
        if (fe->prof && fe->prof->has_custom) fe->cust = fe->prof->custom;
        else memset(&fe->cust, 0, sizeof(fe->cust));
        fe->cust_sel = 0; fe->cust_dirty = 1;
        if (fe->cpak) cust_fix(fe);
    }
    enter(fe, state);
}

/* ---- the trick book's list: flips, grabs, grinds, manuals, moves ----------------------------------------------------------------------------------- */
static int tb_bit(int k)
{
    static const int ORDER[5] = { TRICK_FLIP, TRICK_GRAB, TRICK_GRIND, TRICK_MANUAL, TRICK_MOVE };
    static const int COUNT[5] = { NFLIP_TYPES, NGRAB_TYPES, NGRIND_TYPES, NMANUAL_TYPES, NMOVE_TYPES };
    for (int c = 0; c < 5; c++) {
        if (k < COUNT[c]) return stats_landed_bit(ORDER[c], k);
        k -= COUNT[c];
    }
    return -1;
}

static void tb_name(int k, char *out, size_t cap)
{
    int cls, type;
    stats_bit_trick(tb_bit(k), &cls, &type);
    hud_title_case(trick_def(cls, type)->name, out, cap);
}

/* ---- numbers as text ------------------------------------------------------------------------------------------------------------------------------- */
static void fmt_time(unsigned s, char *out, size_t cap)
{
    snprintf(out, cap, "%u:%02u:%02u", s / 3600u, (s / 60u) % 60u, s % 60u);
}

static void fmt_dist(const Frontend *fe, unsigned cm, char *out, size_t cap)
{
    Settings d;
    const Settings *s = fe->set;
    if (!s) { settings_defaults(&d); s = &d; }
    settings_format_distance(s, (float)cm / 100.0f, out, cap);
}

typedef struct { char label[32]; char value[24]; } Row;

static int rows_stats(const Frontend *fe, Row *r)
{
    const Profile *p = fe->prof;
    int n = 0;
    if (!p) return 0;
    snprintf(r[n].label, 32, "Total playtime"); fmt_time(p->stat[STAT_PLAY_TIME], r[n++].value, 24);
    snprintf(r[n].label, 32, "Events completed"); snprintf(r[n++].value, 24, "%d", progress_total_done(p));
    snprintf(r[n].label, 32, "Tricks landed"); snprintf(r[n++].value, 24, "%u", p->stat[STAT_TRICKS]);
    snprintf(r[n].label, 32, "Different tricks"); snprintf(r[n++].value, 24, "%d of %d", stats_landed_count(p), STATS_NTRICKS);
    snprintf(r[n].label, 32, "Wipeouts"); snprintf(r[n++].value, 24, "%u", p->stat[STAT_BAILS]);
    snprintf(r[n].label, 32, "Pushes"); snprintf(r[n++].value, 24, "%u", p->stat[STAT_PUSHES]);
    snprintf(r[n].label, 32, "Distance travelled"); fmt_dist(fe, p->stat[STAT_DIST], r[n++].value, 24);
    snprintf(r[n].label, 32, "Distance grinded"); fmt_dist(fe, p->stat[STAT_DIST_GRIND], r[n++].value, 24);
    snprintf(r[n].label, 32, "Manual distance"); fmt_dist(fe, p->stat[STAT_DIST_MANUAL], r[n++].value, 24);
    snprintf(r[n].label, 32, "Powerslide distance"); fmt_dist(fe, p->stat[STAT_DIST_SLIDE], r[n++].value, 24);
    snprintf(r[n].label, 32, "Total airgrab time"); snprintf(r[n++].value, 24, "%u sec", p->stat[STAT_AIRGRAB_TIME]);
    return n;
}

static int rows_bests(const Frontend *fe, Row *r)
{
    const Profile *p = fe->prof;
    int n = 0;
    if (!p) return 0;
    snprintf(r[n].label, 32, "Best airtime"); snprintf(r[n++].value, 24, "%u.%02u sec", p->stat[STAT_BEST_AIR] / 1000u, (p->stat[STAT_BEST_AIR] / 10u) % 100u);
    snprintf(r[n].label, 32, "Longest grind"); fmt_dist(fe, p->stat[STAT_BEST_GRIND], r[n++].value, 24);
    snprintf(r[n].label, 32, "Longest manual"); fmt_dist(fe, p->stat[STAT_BEST_MANUAL], r[n++].value, 24);
    snprintf(r[n].label, 32, "Longest powerslide"); fmt_dist(fe, p->stat[STAT_BEST_SLIDE], r[n++].value, 24);
    snprintf(r[n].label, 32, "Best scoring sequence"); snprintf(r[n++].value, 24, "%u pts", p->stat[STAT_BEST_LINE]);
    return n;
}

/* the gear the career has unlocked: decks, shoes, trucks, wheels (a count of the unlock bits) */
static int count_unlocked(const Profile *p, int kind)
{
    int n = 0;
    for (int i = 0; i < CAREER_MAX_INDEX; i++) n += progress_unlocked(p, kind, i);
    return n;
}

static int unlocked_skaters(const Frontend *fe)
{
    int n = 0;
    for (int i = 0; i < CAREER_NRIDERS && i < (int)fe->career->nskaters; i++) n += fe->prof && progress_unlocked(fe->prof, UNLOCK_SKATER, i);
    return n;
}

/* ---- update ------------------------------------------------------------------------------------------------------------------------------------------ */
static int list_len(const Frontend *fe)
{
    switch (fe->state) {
    case FE_STATE_MYSKATE: return MS_ROWS;
    case FE_STATE_PROFILES: return PROFILE_SLOTS;
    case FE_STATE_LOGBOOK: return LB_ROWS;
    default: return 0;
    }
}

/* ---- Customize: the rows ---- */
enum { CR_GENDER = 0, CR_BUILD, CR_SKIN, CR_FACE, CR_HAIR, CR_HAIR_COL, CR_BEARD, CR_BEARD_COL, CR_HAT, CR_HAT_TEX, CR_TOP, CR_LOGO, CR_PANTS, CR_SHOES, CR_DECK, CR_WHEELS, CR_DONE, CR_ROWS };

/* the rows that mean something for the choice (the colour of no hair, the design of no hat ...) are the ones the cursor stops on */
static int cust_row_active(const Frontend *fe, int r)
{
    const CustomSel *c = &fe->cust;
    if (r == CR_HAIR_COL) return c->hair > 0;
    if (r == CR_BEARD_COL) return c->beard > 0;
    if (r == CR_HAT_TEX) return c->hat > 0;
    if (r == CR_DECK) return fe->cpak && custom_count(fe->cpak, CUST_DECK) > 0;           /* (a custom.pak of the first version has no board art) */
    if (r == CR_WHEELS) return fe->cpak && custom_count(fe->cpak, CUST_WHEELS) > 0;
    return 1;
}

static uint8_t *cust_field(CustomSel *c, int r)
{
    uint8_t *f[CR_ROWS] = { &c->gender, &c->build, &c->skin, &c->face, &c->hair, &c->hair_col, &c->beard, &c->beard_col, &c->hat, &c->hat_tex, &c->top, &c->logo, &c->pants, &c->shoes, &c->deck, &c->wheels, NULL };
    return f[r];
}

static int cust_values(const CustomPak *p, int r)
{
    const int n[CR_ROWS] = { 2, CUSTOM_NBUILD, CUSTOM_NSKIN, CUSTOM_NFACE, CUSTOM_NHAIR, CUSTOM_NCOL, CUSTOM_NBEARD, CUSTOM_NCOL, CUSTOM_NHAT, custom_count(p, CUST_HAT), custom_count(p, CUST_TOP),
                             custom_count(p, CUST_LOGO) + 1, custom_count(p, CUST_PANTS), custom_count(p, CUST_SHOES), custom_count(p, CUST_DECK) + 1, custom_count(p, CUST_WHEELS) + 1, 1 };
    return n[r];
}

/* the career's unlocks decide which gear can be chosen (shoes, hat designs, decks, wheels: the item numbers of the original's tables, src/career.h); the rest of the editor is open */
static int cust_gear_ok(const Frontend *fe, int list, int i)
{
    int kind = custom_unlock_kind(list), rom = custom_rom(fe->cpak, list, i);
    if (kind < 0) return 1;
    if (!fe->prof) return rom >= 0;
    return rom >= 0 && progress_unlocked(fe->prof, kind, rom);
}

static int cust_hat_has_design(const Frontend *fe, int hat)
{
    for (int i = 0; i < custom_count(fe->cpak, CUST_HAT); i++)
        if (custom_hat_design_ok(fe->cpak, hat, i) && cust_gear_ok(fe, CUST_HAT, i)) return 1;
    return 0;
}

/* can row r take value v? */
static int cust_allowed(const Frontend *fe, int r, int v)
{
    const CustomSel *c = &fe->cust;
    switch (r) {
    case CR_HAT: return v == 0 || (custom_hat_exists(c->gender, v) && cust_hat_has_design(fe, v));
    case CR_HAT_TEX: return custom_hat_design_ok(fe->cpak, c->hat, v) && cust_gear_ok(fe, CUST_HAT, v);
    case CR_SHOES: return cust_gear_ok(fe, CUST_SHOES, v);
    case CR_DECK: return v == 0 || cust_gear_ok(fe, CUST_DECK, v - 1);
    case CR_WHEELS: return v == 0 || cust_gear_ok(fe, CUST_WHEELS, v - 1);
    default: return 1;
    }
}

/* whatever is chosen but not available (a hat design for another hat, a shoe the career has not given yet) becomes the first value that is */
static void cust_fix(Frontend *fe)
{
    CustomSel *c = &fe->cust;
    if (!custom_hat_exists(c->gender, c->hat) || !cust_allowed(fe, CR_HAT, c->hat)) c->hat = 0;          /* a woman's cap for long hair is gone when the skater becomes a man */
    static const int GATED[3] = { CR_HAT_TEX, CR_SHOES, CR_DECK };
    for (int k = 0; k < 4; k++) {
        int r = k < 3 ? GATED[k] : CR_WHEELS;
        if (r == CR_HAT_TEX && c->hat == 0) continue;
        uint8_t *f = cust_field(c, r);
        int n = cust_values(fe->cpak, r);
        if (cust_allowed(fe, r, *f)) continue;
        for (int v = 0; v < n; v++) if (cust_allowed(fe, r, v)) { *f = (uint8_t)v; break; }
    }
}

/* one step of a row: a small list wraps round, with L / R (step 5) a long one stops at its ends; the hats a man has are the ones that exist for him */
static void cust_step(Frontend *fe, int r, int step)
{
    CustomSel *c = &fe->cust;
    uint8_t *f = cust_field(c, r);
    int n = cust_values(fe->cpak, r);
    if (!f || n < 2) return;
    int v = (int)*f;
    if (step > 1 || step < -1) {                                    /* a jump of five: to the allowed value nearest to it, in that direction (not past the ends) */
        int dir = step > 0 ? 1 : -1, target = v + step;
        target = target < 0 ? 0 : (target >= n ? n - 1 : target);
        int best = v;
        for (int w = v + dir; w >= 0 && w < n && (dir > 0 ? w <= target : w >= target); w += dir)
            if (cust_allowed(fe, r, w)) best = w;
        v = best;
    } else {
        for (int k = 0; k < n; k++) {
            v = wrap(v + step, n);
            if (cust_allowed(fe, r, v)) break;
        }
    }
    *f = (uint8_t)v;
    cust_fix(fe);
    fe->cust_dirty = 1; fe->ev_move = 1;
}

int fe_book_update(Frontend *fe, const FeInput *in, float dt)
{
    int d = fe_nav_step(fe, in, dt, 0);
    switch (fe->state) {
    case FE_STATE_MYSKATE: {
        if (d) {                                                   /* only the entries that exist can be chosen */
            int s = fe->ms_sel;
            for (int k = 0; k < MS_ROWS; k++) {
                s = wrap(s + (d > 0 ? 1 : -1), MS_ROWS);
                if (MS_ENABLED[s]) break;
            }
            if (s != fe->ms_sel) { fe->ms_sel = s; fe->ev_move = 1; }
        }
        if (in->pressed & (FE_OK | FE_START)) {
            fe->ev_ok = 1;
            if (fe->ms_sel == MS_PROFILE) fe_book_open(fe, FE_STATE_PROFILES);
            else if (fe->ms_sel == MS_CUSTOMIZE) fe_book_open(fe, FE_STATE_CUSTOM);
            else if (fe->ms_sel == MS_LOGBOOK) fe_book_open(fe, FE_STATE_LOGBOOK);
            else if (fe->ms_sel == MS_SETTINGS) { fe_options_open_page(fe, OPT_GAME); fe->opt_return = FE_STATE_MYSKATE; enter(fe, FE_STATE_OPTIONS); }
            else if (fe->ms_sel == MS_CREDITS) fe_credits_open(fe, FE_STATE_MYSKATE);
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1; enter(fe, FE_STATE_MAIN);
        }
        break;
    }
    case FE_STATE_PROFILES: {
        if (fe->pf_confirm) {                                      /* "delete this profile?" */
            if (in->pressed & (FE_OK | FE_START)) { fe->req_delete = fe->pf_sel; fe->pf_confirm = 0; fe->ev_ok = 1; }
            else if (in->pressed & FE_BACK) { fe->pf_confirm = 0; fe->ev_back = 1; }
            break;
        }
        if (d) { fe->pf_sel = wrap(fe->pf_sel + d, PROFILE_SLOTS); fe->ev_move = 1; }
        if (in->pressed & (FE_OK | FE_START)) {
            fe->ev_ok = 1;
            if (fe->pf_sel != fe->cur_slot) fe->req_slot = fe->pf_sel;                  /* the caller loads it (or starts a new one) */
            else { fe->ev_ok = 0; fe->ev_back = 1; enter(fe, FE_STATE_MYSKATE); }          /* it is the profile in use: nothing to do */
        } else if ((in->pressed & FE_SQUARE) && fe->slot[fe->pf_sel].used) {
            fe->pf_confirm = 1; fe->ev_ok = 1;
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1; enter(fe, FE_STATE_MYSKATE);
        }
        break;
    }
    case FE_STATE_CUSTOM: {
        if (!fe->cpak) {                                                           /* the parts are not there (custom.pak missing) */
            if (in->pressed & FE_BACK) { fe->ev_back = 1; enter(fe, FE_STATE_MYSKATE); }
            break;
        }
        if (d) {                                                                   /* up / down: the next row that means something */
            int r = fe->cust_sel;
            for (int k = 0; k < CR_ROWS; k++) {
                r = wrap(r + d, CR_ROWS);
                if (cust_row_active(fe, r)) break;
            }
            fe->cust_sel = r; fe->ev_move = 1;
        }
        int step = 0;
        if (in->pressed & FE_RIGHT) step = 1;
        if (in->pressed & FE_LEFT) step = -1;
        if (in->pressed & FE_R) step = 5;
        if (in->pressed & FE_L) step = -5;
        if ((in->pressed & (FE_OK | FE_START)) && fe->cust_sel == CR_DONE) {                  /* keep it: it is the career's rider from now on */
            if (fe->prof) {
                fe->prof->custom = fe->cust; fe->prof->has_custom = 1; fe->prof->skater = PROFILE_CUSTOM_SKATER;
                fe->ev_profile = 1;
            }
            fe->ev_ok = 1; enter(fe, FE_STATE_MYSKATE);
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1; enter(fe, FE_STATE_MYSKATE);                                       /* (what was made but not kept is dropped) */
        } else {
            if ((in->pressed & FE_OK) && fe->cust_sel != CR_DONE) step = 1;
            if (step && fe->cust_sel != CR_DONE) cust_step(fe, fe->cust_sel, step);
        }
        break;
    }
    case FE_STATE_LOGBOOK:
        if (d) { fe->lb_sel = wrap(fe->lb_sel + d, LB_ROWS); fe->ev_move = 1; }
        if (in->pressed & (FE_OK | FE_START)) {
            fe->lb_page = fe->lb_sel; fe->tb_sel = 0; fe->ev_ok = 1;
            fe_book_open(fe, FE_STATE_LOGPAGE);
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1; enter(fe, FE_STATE_MYSKATE);
        }
        break;
    case FE_STATE_LOGPAGE: {
        int n = fe->lb_page == LB_TRICKS ? STATS_NTRICKS : (fe->lb_page == LB_UNLOCKS ? CAREER_NRIDERS : 0);
        if (n) {                                                   /* a list: up / down, L / R a page */
            if (d) { fe->tb_sel = wrap(fe->tb_sel + d, n); fe->ev_move = 1; }
            if (in->pressed & (FE_L | FE_LEFT)) { fe->tb_sel = fe->tb_sel - 8 < 0 ? 0 : fe->tb_sel - 8; fe->ev_move = 1; }
            if (in->pressed & (FE_R | FE_RIGHT)) { fe->tb_sel = fe->tb_sel + 8 >= n ? n - 1 : fe->tb_sel + 8; fe->ev_move = 1; }
        } else {                                                   /* a table: it scrolls when it has more rows than fit */
            Row r[16];
            int rows = fe->lb_page == LB_STATS ? rows_stats(fe, r) : rows_bests(fe, r), max = rows > TABLE_ROWS ? rows - TABLE_ROWS : 0;
            if (d) { fe->lb_scroll += d; if (fe->lb_scroll < 0) fe->lb_scroll = 0; if (fe->lb_scroll > max) fe->lb_scroll = max; }
        }
        if (in->pressed & FE_BACK) { fe->ev_back = 1; enter(fe, FE_STATE_LOGBOOK); }
        break;
    }
    default:
        break;
    }
    (void)list_len;
    return 0;
}

/* ---- drawing ------------------------------------------------------------------------------------------------------------------------------------------ */
static void draw_items(Frontend *fe, const char *const *names, const unsigned char *enabled, int n, int sel)
{
    Ui *ui = fe->ui;
    for (int i = 0; i < n; i++) {
        float y = (float)LEFT_Y + 3.0f + (float)i * 23.0f;
        ui_text_shadow(ui, fe->f_m, COL_R, y, 1.0f, names[i], enabled[i] ? UI_WHITE : UI_RGBA(255, 255, 255, 95));
        if (i == sel) ui_image(ui, fe->i_arrow, COL_R + ui_text_width(ui, fe->f_m, 1.0f, names[i]) + 6, y + (19.0f - 24.0f) / 2.0f, 24, 24, UI_WHITE);
    }
}

static void draw_table(Frontend *fe, const char *title, const Row *r, int n)
{
    Ui *ui = fe->ui;
    fe_title(fe, title);
    fe_frame(fe, 24, 52, 432, 176);
    for (int i = 0; i < TABLE_ROWS && fe->lb_scroll + i < n; i++) {
        float y = 60.0f + (float)i * 16.5f;
        ui_text_shadow(ui, fe->f_s, 36, y, 1.0f, r[fe->lb_scroll + i].label, UI_WHITE);
        ui_text_right(ui, fe->f_s, 444, y, 1.0f, r[fe->lb_scroll + i].value, COL_YELLOW);
    }
    if (n > TABLE_ROWS) {
        float th = 164.0f, vis = (float)TABLE_ROWS / (float)n, pos = (float)fe->lb_scroll / (float)(n - TABLE_ROWS);
        ui_rect(ui, 450, 58, 3, th, UI_RGBA(0, 0, 0, 110));
        ui_rect(ui, 450, 58 + (th - th * vis) * pos, 3, th * vis, UI_RGBA(255, 255, 255, 220));
    }
    fe_hint(fe, n > TABLE_ROWS ? "UP / DOWN  SCROLL       O  BACK" : "O  BACK");
}

static void slot_line(const FeSlot *s, int i, char *out, size_t cap)
{
    if (!s->used) snprintf(out, cap, "Profile %d  (new)", i + 1);
    else snprintf(out, cap, "Profile %d", i + 1);
}

void fe_book_draw(Frontend *fe)
{
    Ui *ui = fe->ui;
    const Career *c = fe->career;
    char buf[160];
    switch (fe->state) {
    case FE_STATE_MYSKATE: {
        fe_title(fe, "My Skate");
        draw_items(fe, MS_NAME, MS_ENABLED, MS_ROWS, fe->ms_sel);
        fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
        static const char *const HELP[MS_ROWS] = {
            "Choose one of the three saved careers, start a new one, or clear one.",
            "Make your own skater: body, face, hair, beard, hat, top, logo, bottom and shoes.",
            "Your statistics, personal bests, the trick book and what you have unlocked.",
            "Your stance, the units, the sound volumes.",
            "Who made the game.",
        };
        static const char *const TITLE[MS_ROWS] = { "Select Profile", "Customize", "Log Book", "Game Settings", "Credits" };
        fe_panel_text(fe, TITLE[fe->ms_sel], HELP[fe->ms_sel]);
        fe_hint(fe, "UP / DOWN  CHOOSE       X  OK       O  BACK");
        break;
    }
    case FE_STATE_PROFILES: {
        fe_title(fe, "Select Profile");
        const char *nm[PROFILE_SLOTS];
        char names[PROFILE_SLOTS][32];
        unsigned char mk[PROFILE_SLOTS];
        for (int i = 0; i < PROFILE_SLOTS; i++) { slot_line(&fe->slot[i], i, names[i], sizeof(names[i])); nm[i] = names[i]; mk[i] = (unsigned char)(i == fe->cur_slot ? FE_MARK_DONE : 0); }
        fe->mark = mk;
        fe_list(fe, nm, PROFILE_SLOTS, fe->pf_sel, COL_R, LEFT_Y - 6, BAR_X);
        fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
        const FeSlot *s = &fe->slot[fe->pf_sel];
        if (fe->pf_confirm) {
            snprintf(buf, sizeof(buf), "Clear profile %d? All of its progress is lost.", fe->pf_sel + 1);
            fe_panel_text(fe, "Clear profile", buf);
            fe_hint(fe, "X  CLEAR IT       O  NO");
            break;
        }
        if (!s->used) {
            fe_panel_text(fe, "New profile", fe->pf_sel == fe->cur_slot ? "This one is in use and has nothing saved yet." : "Empty. Choose it to start a new career here.");
        } else {
            ui_text_shadow(ui, fe->f_m, COL_L + 10.0f, LEFT_Y + 7.0f, 1.0f, s->skater == PROFILE_CUSTOM_SKATER ? "Custom skater" : (c->nskaters > (uint32_t)s->skater ? career_str(c, c->skater[s->skater].name_off) : "Skater"), COL_YELLOW);
            snprintf(buf, sizeof(buf), "%d of 49 events done", s->done); ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 36.0f, 1.0f, buf, UI_WHITE);
            snprintf(buf, sizeof(buf), "%d of %d sponsors", s->sponsors, PROFILE_NSPONSOR); ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 54.0f, 1.0f, buf, UI_WHITE);
            char t[24]; fmt_time(s->play, t, sizeof(t));
            snprintf(buf, sizeof(buf), "Playtime %s", t); ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 72.0f, 1.0f, buf, UI_WHITE);
        }
        if (fe->pf_sel == fe->cur_slot) ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + LEFT_H - 24.0f, 1.0f, "In use", COL_GREEN);
        fe_hint(fe, s->used ? "UP / DOWN  CHOOSE       X  USE IT       SQUARE  CLEAR       O  BACK" : "UP / DOWN  CHOOSE       X  USE IT       O  BACK");
        break;
    }
    case FE_STATE_CUSTOM: {
        fe_title(fe, "Customize");
        if (!fe->cpak) {
            fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
            fe_panel_text(fe, "Not available", "The parts of the custom skater (data/custom.pak) were not found.");
            fe_hint(fe, "O  BACK");
            break;
        }
        const CustomSel *cs = &fe->cust;
        const CustomPak *cp = fe->cpak;
        char names[CR_ROWS][40];
        const char *nm[CR_ROWS];
        int rows[CR_ROWS], nr = 0, sel = 0;                                        /* only the rows that mean something are listed */
        for (int r = 0; r < CR_ROWS; r++) {
            if (!cust_row_active(fe, r)) continue;
            char *d = names[nr];
            const char *label = "", *val = "";
            char num[16];
            switch (r) {
            case CR_GENDER: label = "Gender"; val = custom_gender_name(cs->gender); break;
            case CR_BUILD: label = "Build"; val = custom_build_name(cs->build); break;
            case CR_SKIN: label = "Skin tone"; snprintf(num, sizeof(num), "%d", cs->skin + 1); val = num; break;
            case CR_FACE: label = "Face"; snprintf(num, sizeof(num), "%d", cs->face + 1); val = num; break;
            case CR_HAIR: label = "Hair"; val = custom_hair_name(cs->hair); break;
            case CR_HAIR_COL: label = "Hair colour"; val = custom_colour_name(cs->hair_col); break;
            case CR_BEARD: label = "Beard"; if (cs->beard) { snprintf(num, sizeof(num), "%d", cs->beard); val = num; } else val = "None"; break;
            case CR_BEARD_COL: label = "Beard colour"; val = custom_colour_name(cs->beard_col); break;
            case CR_HAT: label = "Hat"; val = custom_hat_name(cs->hat); break;
            case CR_HAT_TEX: label = "Hat design"; val = custom_name(cp, CUST_HAT, cs->hat_tex); break;
            case CR_TOP: label = "Top"; val = custom_name(cp, CUST_TOP, cs->top); break;
            case CR_LOGO: label = "Logo"; val = cs->logo ? custom_name(cp, CUST_LOGO, cs->logo - 1) : "None"; break;
            case CR_PANTS: label = "Bottom"; val = custom_name(cp, CUST_PANTS, cs->pants); break;
            case CR_SHOES: label = "Shoes"; val = custom_name(cp, CUST_SHOES, cs->shoes); break;
            case CR_DECK: label = "Deck"; val = cs->deck ? custom_name(cp, CUST_DECK, cs->deck - 1) : "Generic Toon"; break;
            case CR_WHEELS: label = "Wheels"; val = cs->wheels ? custom_name(cp, CUST_WHEELS, cs->wheels - 1) : "Generic White"; break;
            default: label = "Done"; break;
            }
            if (r == CR_DONE) snprintf(d, sizeof(names[0]), "Done"); else snprintf(d, sizeof(names[0]), "%s  %s", label, val);
            size_t dl = strlen(d);                                                  /* the long names are cut to the width of the list */
            if (ui_text_width(ui, fe->f_m, 1.0f, d) > 196.0f) { while (dl > 5 && ui_text_width(ui, fe->f_m, 1.0f, d) > 196.0f) d[--dl] = 0; d[dl - 1] = '.'; d[dl - 2] = '.'; }
            nm[nr] = d;
            if (r == fe->cust_sel) sel = nr;
            rows[nr++] = r;
        }
        (void)rows;
        fe_list(fe, nm, nr, sel, COL_R, LEFT_Y - 6, BAR_X);
        fe_hint(fe, "UP / DOWN  ROW    LEFT / RIGHT  CHANGE    L / R  X5    O  CANCEL");
        break;
    }
    case FE_STATE_LOGBOOK: {
        fe_title(fe, "Log Book");
        unsigned char en[LB_ROWS] = { 1, 1, 1, 1 };
        const char *nm[LB_ROWS];
        for (int i = 0; i < LB_ROWS; i++) nm[i] = LB_NAME[i];
        draw_items(fe, nm, en, LB_ROWS, fe->lb_sel);
        fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
        if (fe->lb_sel == LB_TRICKS) snprintf(buf, sizeof(buf), "%d of %d tricks landed. How to do each one.", fe->prof ? stats_landed_count(fe->prof) : 0, STATS_NTRICKS);
        else if (fe->lb_sel == LB_UNLOCKS) snprintf(buf, sizeof(buf), "%d of %d skaters unlocked, and the gear you have won.", unlocked_skaters(fe), CAREER_NRIDERS);
        else if (fe->lb_sel == LB_BESTS) snprintf(buf, sizeof(buf), "Your best air, longest grind, manual and powerslide, and your best line.");
        else snprintf(buf, sizeof(buf), "Playtime, distances, pushes and wipeouts.");
        fe_panel_text(fe, LB_NAME[fe->lb_sel], buf);
        fe_hint(fe, "UP / DOWN  CHOOSE       X  OPEN       O  BACK");
        break;
    }
    case FE_STATE_LOGPAGE: {
        Row r[16];
        if (fe->lb_page == LB_STATS) { int n = rows_stats(fe, r); draw_table(fe, "Statistics", r, n); break; }
        if (fe->lb_page == LB_BESTS) { int n = rows_bests(fe, r); draw_table(fe, "Personal Bests", r, n); break; }
        if (fe->lb_page == LB_TRICKS) {
            fe_title(fe, "Trick Book");
            static char names[STATS_NTRICKS][32];
            static const char *nm[STATS_NTRICKS];
            static unsigned char mk[STATS_NTRICKS];
            for (int k = 0; k < STATS_NTRICKS; k++) {
                int cls, type;
                tb_name(k, names[k], sizeof(names[k]));
                nm[k] = names[k];
                stats_bit_trick(tb_bit(k), &cls, &type);
                mk[k] = (unsigned char)(fe->prof && stats_landed(fe->prof, cls, type) ? FE_MARK_DONE : 0);
            }
            fe->mark = mk;
            fe_list(fe, nm, STATS_NTRICKS, fe->tb_sel, COL_R, LEFT_Y - 6, BAR_X);
            fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
            const char *how = fe_trick_how(nm[fe->tb_sel]);
            char ex[256];
            if (how && fe->set) settings_expand_buttons(fe->set, how, ex, sizeof(ex));
            else snprintf(ex, sizeof(ex), "%s", how ? how : "");
            fe_panel_text(fe, nm[fe->tb_sel], ex[0] ? ex : "A trick of the original's trick book.");
            ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + LEFT_H - 24.0f, 1.0f, mk[fe->tb_sel] ? "Landed" : "Not landed yet", mk[fe->tb_sel] ? COL_GREEN : COL_DIM);
            fe_hint(fe, "UP / DOWN  CHOOSE       L / R  PAGE       O  BACK");
            break;
        }
        fe_title(fe, "Unlockables");
        const char *nm[CAREER_NRIDERS];
        unsigned char mk[CAREER_NRIDERS];
        for (int k = 0; k < CAREER_NRIDERS; k++) {
            int un = fe->prof && progress_unlocked(fe->prof, UNLOCK_SKATER, k);
            nm[k] = un && (uint32_t)k < c->nskaters ? career_str(c, c->skater[k].name_off) : "???";
            mk[k] = (unsigned char)(un ? FE_MARK_DONE : 0);
        }
        fe->mark = mk;
        fe_list(fe, nm, CAREER_NRIDERS, fe->tb_sel, COL_R, LEFT_Y - 6, BAR_X);
        fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
        ui_text_shadow(ui, fe->f_m, COL_L + 10.0f, LEFT_Y + 7.0f, 1.0f, "Unlocked", COL_YELLOW);
        snprintf(buf, sizeof(buf), "Skaters  %d of %d", unlocked_skaters(fe), CAREER_NRIDERS); ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 34.0f, 1.0f, buf, UI_WHITE);
        if (fe->prof) {
            int sp = 0; for (int k = 0; k < PROFILE_NSPONSOR; k++) sp += fe->prof->sponsor[k] != 0;
            snprintf(buf, sizeof(buf), "Sponsors  %d of %d", sp, PROFILE_NSPONSOR); ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 52.0f, 1.0f, buf, UI_WHITE);
            snprintf(buf, sizeof(buf), "Decks  %d", count_unlocked(fe->prof, UNLOCK_DECK)); ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 70.0f, 1.0f, buf, UI_WHITE);
            snprintf(buf, sizeof(buf), "Trucks  %d", count_unlocked(fe->prof, UNLOCK_TRUCKS)); ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 88.0f, 1.0f, buf, UI_WHITE);
            snprintf(buf, sizeof(buf), "Wheels  %d     Shoes  %d", count_unlocked(fe->prof, UNLOCK_WHEELS), count_unlocked(fe->prof, UNLOCK_SHOES)); ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 106.0f, 1.0f, buf, UI_WHITE);
        }
        fe_hint(fe, "UP / DOWN  CHOOSE       L / R  PAGE       O  BACK");
        break;
    }
    default:
        break;
    }
}
