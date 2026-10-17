#include "frontend.h"
#include "tutorial.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define COL_HINT 0xFFE0E0E0u
#define LIST_ROWS 8                     /* rows of a list on screen */
#define LIST_PITCH 21
/* the main menu of the DS: only the entries marked 1 exist here yet */
static const char *const MAIN_ITEMS[FE_NMAIN] = { "start career", "my skate", "multiplayer", "tutorial", "freeskate", "my spot", "options" };
static const unsigned char MAIN_ENABLED[FE_NMAIN] = { 1, 1, 0, 1, 1, 0, 1 };
enum { MAIN_CAREER = 0, MAIN_MYSKATE = 1, MAIN_TUTORIAL = 3, MAIN_OPTIONS = FE_NMAIN - 1 };

static void copy_name(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src);
}


void fe_init(Frontend *fe, Ui *ui, const Career *c, const unsigned char *skater_ok, const unsigned char *level_ok)
{
    memset(fe, 0, sizeof(*fe));
    fe->ui = ui;
    fe->career = c;
    const UiPak *p = ui->pak;
    fe->f_xl = uipak_font(p, "fe_extralarge"); fe->f_l = uipak_font(p, "fe_large"); fe->f_m = uipak_font(p, "fe_medium"); fe->f_s = uipak_font(p, "fe_small"); fe->f_tiny = uipak_font(p, "fetiny");
    fe->i_title = uipak_image(p, "title"); fe->i_dim = uipak_image(p, "bg_dim"); fe->i_locbox = uipak_image(p, "locbox"); fe->i_arrow = uipak_image(p, "spr_arrow_left");
    for (uint32_t i = 0; i < c->nskaters && i < CAREER_NRIDERS && fe->nskater < FE_MAX_ITEMS; i++) {
        if (!skater_ok || !skater_ok[i])
            continue;
        FeItem *it = &fe->skater[fe->nskater++];
        memset(it, 0, sizeof(*it));
        it->index = (int)i; it->ok = 1;
        copy_name(it->name, sizeof(it->name), career_str(c, c->skater[i].name_off));
        copy_name(it->key, sizeof(it->key), career_str(c, c->skater[i].key_off));
        if (strcmp(it->key, "aaron") == 0)
            fe->skater_sel = fe->nskater - 1;
    }
    for (uint32_t i = 0; i < c->nlevels && fe->nloc < FE_MAX_ITEMS; i++) {
        const char *zip = career_str(c, c->level[i].zip_off);
        if (strcasecmp(zip, "SpotSculptWarehouse") == 0 || !level_ok || !level_ok[c->level[i].id])
            continue;                                              /* (the My Spot base warehouse is not a place to pick) */
        FeItem *it = &fe->loc[fe->nloc++];
        memset(it, 0, sizeof(*it));
        it->index = (int)i; it->ok = 1; it->level_id = c->level[i].id; it->area = c->level[i].area;
        copy_name(it->name, sizeof(it->name), career_str(c, c->level[i].name_off));
        copy_name(it->key, sizeof(it->key), zip);
        char pic[40];
        snprintf(pic, sizeof(pic), "loc_%s", zip);
        for (char *q = pic; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
        it->pic = uipak_image(p, pic);
        int tmp[32];
        it->chals = career_challenges_at(c, it->level_id, tmp, 32);
    }
    for (int i = 1; i < fe->nloc; i++) {                           /* grouped by area, in the original's order inside an area (insertion sort: stable) */
        FeItem t = fe->loc[i];
        int j = i - 1;
        while (j >= 0 && fe->loc[j].area > t.area) { fe->loc[j + 1] = fe->loc[j]; j--; }
        fe->loc[j + 1] = t;
    }
    for (int i = 0; i < fe->nloc; i++)
        if (strcasecmp(fe->loc[i].key, "Amphitheatre") == 0) {
            fe->loc_sel = i;
            fe->area_sel = fe->loc[i].area;
        }
    memcpy(fe->skater_all, fe->skater, sizeof(fe->skater));
    fe->nskater_all = fe->nskater;
    fe->launch_chal = -1;
    fe->story_launch = -1;
    fe->req_slot = fe->req_delete = -1;
    fe->opt_return = FE_STATE_MAIN;
    fe->ntut = tutorial_list(c, fe->tut, (int)(sizeof(fe->tut) / sizeof(fe->tut[0])));
    fe->state = FE_STATE_TITLE;
    fe->preview[0] = COL_L + 3; fe->preview[1] = LEFT_Y + 3; fe->preview[2] = LEFT_W - 6; fe->preview[3] = LEFT_H - 6;        /* (its frame's outer edge is at COL_L, LEFT_W wide, like the pictures) */
}

/* one step of a list: -1 / +1 from a press or the auto-repeat of a held up / down (left / right too when allow_lr) */
int fe_nav_step(Frontend *fe, const FeInput *in, float dt, int allow_lr)
{
    unsigned dirs = FE_UP | FE_DOWN | (allow_lr ? FE_LEFT | FE_RIGHT : 0);
    unsigned p = in->pressed & dirs;
    if (p) {
        fe->rep_dir = p; fe->rep_t = 0.38f;
        return (p & (FE_UP | FE_LEFT)) ? -1 : 1;
    }
    unsigned h = in->held & dirs;
    if (h && h == fe->rep_dir) {
        fe->rep_t -= dt;
        if (fe->rep_t <= 0.0f) { fe->rep_t = 0.09f; return (h & (FE_UP | FE_LEFT)) ? -1 : 1; }
    } else {
        fe->rep_dir = 0;
    }
    return 0;
}

static int wrap(int v, int n) { return n <= 0 ? 0 : (v % n + n) % n; }

static void enter(Frontend *fe, int state)
{
    fe->state = state;
    fe->t = 0.0f;
    fe->rep_dir = 0;
}

/* the first place of the next / previous area (the list is grouped by area) */
static void change_area(Frontend *fe, int dir)
{
    int cur = fe->loc[fe->loc_sel].area, target = -1;
    if (dir > 0) {
        for (int i = 0; i < fe->nloc; i++) if (fe->loc[i].area > cur) { target = i; break; }
        if (target < 0) target = 0;
    } else {
        int prev = -1;
        for (int i = 0; i < fe->nloc; i++) if (fe->loc[i].area < cur) prev = fe->loc[i].area;
        if (prev < 0) prev = fe->loc[fe->nloc - 1].area;
        for (int i = 0; i < fe->nloc; i++) if (fe->loc[i].area == prev) { target = i; break; }
    }
    if (target >= 0 && target != fe->loc_sel) {
        fe->loc_sel = target;
        fe->area_sel = fe->loc[target].area;
        fe->ev_move = 1;
    }
}


/* ---- the career: skaters that can be chosen, and the map (area -> place -> events) --------------------------------------------------------------- */
void fe_set_profile(Frontend *fe, Profile *prof)
{
    fe->prof = prof;
    fe->done = prof ? prof->done : NULL;
}

int fe_event_status(const Frontend *fe, int chal)
{
    if (!fe->prof || chal < 0) return FE_EV_LOCKED;
    if (progress_chal_done(fe->prof, chal)) return FE_EV_DONE;
    if (progress_chal_open(fe->prof, fe->career, chal)) return FE_EV_OPEN;
    return progress_chal_unlocked(fe->prof, chal) ? FE_EV_CLOSED : FE_EV_LOCKED;
}

/* fe->skater = every rider (freeskate) or the unlocked ones (the career); the chosen one stays if it is still there, else the profile's skater, else the first */
static void rebuild_skaters(Frontend *fe, int career_only)
{
    int keep = fe->nskater > 0 ? fe->skater[fe->skater_sel].index : -1;
    if (career_only && fe->prof) keep = fe->prof->skater;
    fe->nskater = 0;
    for (int i = 0; i < fe->nskater_all; i++) {
        const FeItem *it = &fe->skater_all[i];
        if (career_only && fe->prof && !progress_unlocked(fe->prof, UNLOCK_SKATER, it->index)) continue;
        fe->skater[fe->nskater++] = *it;
    }
    if (fe->prof && fe->prof->has_custom && fe->nskater < FE_MAX_ITEMS) {                    /* the player's own skater (My Skate > Customize) */
        FeItem *it = &fe->skater[fe->nskater++];
        memset(it, 0, sizeof(*it));
        it->index = PROFILE_CUSTOM_SKATER; it->ok = 1;
        copy_name(it->name, sizeof(it->name), "Custom skater");
        copy_name(it->key, sizeof(it->key), "custom");
    }
    fe->skater_sel = 0;
    for (int i = 0; i < fe->nskater; i++)
        if (fe->skater[i].index == keep) fe->skater_sel = i;
}

/* the events of a place that belong to a pool (the extra tutorials are played from the tutorial menu): how many are open / done / in all */
static void loc_counts(const Frontend *fe, int level_id, int *open, int *done, int *total)
{
    int tmp[32], n = career_challenges_at(fe->career, level_id, tmp, 32);
    *open = *done = *total = 0;
    for (int i = 0; i < n; i++) {
        if (fe->career->chal[tmp[i]].pool == 0xFF) continue;
        int st = fe_event_status(fe, tmp[i]);
        (*total)++;
        *open += st == FE_EV_OPEN;
        *done += st == FE_EV_DONE;
    }
}

static void build_areas(Frontend *fe)
{
    fe->ncm_areas = 0;
    for (int a = 0; a < (int)fe->career->nareas && fe->ncm_areas < 8; a++)
        for (int i = 0; i < fe->nloc; i++)
            if (fe->loc[i].area == a) { fe->cm_areas[fe->ncm_areas++] = a; break; }
    if (fe->cm_area_sel > fe->ncm_areas) fe->cm_area_sel = 0;           /* (the row after the areas is Sponsors) */
}

static void build_locs(Frontend *fe)
{
    fe->ncm_locs = 0;
    for (int i = 0; i < fe->nloc; i++)
        if (fe->loc[i].area == fe->cm_area && fe->ncm_locs < FE_MAX_ITEMS) fe->cm_locs[fe->ncm_locs++] = i;
    if (fe->cm_loc_sel >= fe->ncm_locs) fe->cm_loc_sel = 0;
}

static void build_chals(Frontend *fe)
{
    int tmp[32], n = fe->ncm_locs > 0 ? career_challenges_at(fe->career, fe->loc[fe->cm_locs[fe->cm_loc_sel]].level_id, tmp, 32) : 0;
    fe->ncm_chals = 0;
    for (int i = 0; i < n && fe->ncm_chals < 16; i++)
        if (fe->career->chal[tmp[i]].pool != 0xFF) fe->cm_chals[fe->ncm_chals++] = tmp[i];
    if (fe->cm_chal_sel >= fe->ncm_chals) fe->cm_chal_sel = 0;
}

static void enter_map(Frontend *fe)
{
    build_areas(fe);
    if (!fe_story_begin(fe, FE_STATE_CMAP_AREA))               /* the story that is due comes first */
        enter(fe, FE_STATE_CMAP_AREA);
}

int fe_launch_career_event(Frontend *fe, int chal)
{
    fe->loc_sel = fe->cm_locs[fe->cm_loc_sel]; fe->area_sel = fe->loc[fe->loc_sel].area;
    fe->mode = FE_MODE_CAREER; fe->launch_chal = chal;
    fe->ev_ok = 1; fe->flash = 0.12f;
    enter(fe, FE_STATE_LAUNCH);
    return 1;
}

/* the first open event of the list, for the cursor to start on */
static int first_open(const Frontend *fe)
{
    for (int i = 0; i < fe->ncm_chals; i++)
        if (fe_event_status(fe, fe->cm_chals[i]) == FE_EV_OPEN) return i;
    return 0;
}

int fe_update(Frontend *fe, const FeInput *in, float dt)
{
    fe->t += dt;
    fe->spin += dt * 1.1f;
    if (fe->flash > 0.0f) fe->flash -= dt;
    switch (fe->state) {
    case FE_STATE_TITLE:
        if ((in->pressed & (FE_START | FE_OK)) && fe->t > 0.4f) { enter(fe, FE_STATE_MAIN); fe->ev_ok = 1; }
        break;
    case FE_STATE_MAIN: {
        int d = fe_nav_step(fe, in, dt, 0);
        if (d) {                                                   /* only the entries that exist can be chosen */
            int s = fe->main_sel;
            for (int k = 0; k < FE_NMAIN; k++) {
                s = wrap(s + (d > 0 ? 1 : -1), FE_NMAIN);
                if (MAIN_ENABLED[s]) break;
            }
            if (s != fe->main_sel) { fe->main_sel = s; fe->ev_move = 1; }
        }
        if ((in->pressed & (FE_OK | FE_START)) && MAIN_ENABLED[fe->main_sel] && fe->main_sel == MAIN_OPTIONS) {
            fe->ev_ok = 1;
            fe_options_open(fe);
            fe->opt_return = FE_STATE_MAIN;
            enter(fe, FE_STATE_OPTIONS);
        } else if ((in->pressed & (FE_OK | FE_START)) && MAIN_ENABLED[fe->main_sel] && fe->main_sel == MAIN_MYSKATE) {
            fe->ev_ok = 1;
            enter(fe, FE_STATE_MYSKATE);
        } else if ((in->pressed & (FE_OK | FE_START)) && MAIN_ENABLED[fe->main_sel] && fe->main_sel == MAIN_TUTORIAL) {
            if (fe->ntut > 0) { fe->ev_ok = 1; enter(fe, FE_STATE_TUTORIAL); }
        } else if ((in->pressed & (FE_OK | FE_START)) && MAIN_ENABLED[fe->main_sel]) {
            fe->mode = fe->main_sel == MAIN_CAREER ? FE_MODE_CAREER : FE_MODE_FREESKATE;
            fe->flash = 0.12f; fe->ev_ok = 1;
            fe->launch_chal = -1;
            if (fe->mode == FE_MODE_CAREER && fe->prof) {              /* a new career starts with the choice of the skater; after that it goes straight to the map */
                if (progress_total_done(fe->prof) == 0 && fe->nskater_all > 0) {
                    rebuild_skaters(fe, 1);
                    fe->skater_back = FE_STATE_MAIN;
                    enter(fe, FE_STATE_SKATER);
                } else {
                    enter_map(fe);
                }
            } else {
                rebuild_skaters(fe, 0);
                fe->skater_back = FE_STATE_MAIN;
                enter(fe, fe->nskater > 0 ? FE_STATE_SKATER : FE_STATE_LOCATION);
            }
        } else if (in->pressed & FE_BACK) {
            enter(fe, FE_STATE_TITLE); fe->ev_back = 1;
        }
        break;
    }
    case FE_STATE_OPTIONS: {
        int r = fe_options_update(fe, in, dt);
        if (r == 2) fe_credits_open(fe, FE_STATE_OPTIONS);
        else if (r) { enter(fe, fe->opt_return); fe->opt_return = FE_STATE_MAIN; }
        break;
    }
    case FE_STATE_STORY:
        return fe_story_update(fe, in, dt);
    case FE_STATE_CREDITS:
        return fe_credits_update(fe, in, dt);
    case FE_STATE_SPONSORS:
        return fe_sponsors_update(fe, in, dt);
    case FE_STATE_MYSKATE: case FE_STATE_PROFILES: case FE_STATE_LOGBOOK: case FE_STATE_LOGPAGE: case FE_STATE_CUSTOM:
        return fe_book_update(fe, in, dt);
    case FE_STATE_TUTORIAL: {
        int d = fe_nav_step(fe, in, dt, 0);
        if (d && fe->ntut > 0) { fe->tut_sel = wrap(fe->tut_sel + d, fe->ntut); fe->ev_move = 1; }
        if ((in->pressed & (FE_OK | FE_START)) && fe->ntut > 0) {
            int place = -1;                                        /* the tutorials are played where the original has them: the level of the challenge */
            for (int i = 0; i < fe->nloc; i++)
                if (fe->loc[i].level_id == fe->career->chal[fe->tut[fe->tut_sel]].level) place = i;
            if (place >= 0) {
                fe->loc_sel = place; fe->area_sel = fe->loc[place].area;
                fe->mode = FE_MODE_TUTORIAL;
                fe->ev_ok = 1; fe->flash = 0.12f;
                enter(fe, FE_STATE_LAUNCH);
                return 1;
            }
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1; enter(fe, FE_STATE_MAIN);
        }
        break;
    }
    case FE_STATE_SKATER: {
        int d = fe_nav_step(fe, in, dt, 1);
        if (in->pressed & FE_L) d = -5;
        if (in->pressed & FE_R) d = 5;
        if (d) {
            int s = fe->skater_sel + d;
            fe->skater_sel = (d == 5 || d == -5) ? (s < 0 ? 0 : (s >= fe->nskater ? fe->nskater - 1 : s)) : wrap(s, fe->nskater);
            fe->ev_move = 1;
        }
        if ((in->pressed & (FE_OK | FE_START)) && fe->nskater > 0) {
            fe->ev_ok = 1;
            if (fe->mode == FE_MODE_CAREER && fe->prof) {
                fe->prof->skater = (uint8_t)fe->skater[fe->skater_sel].index;       /* the career's rider */
                fe->ev_profile = 1;
                enter_map(fe);
            } else {
                enter(fe, FE_STATE_LOCATION);
            }
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1;
            if (fe->skater_back == FE_STATE_CMAP_AREA) enter_map(fe); else enter(fe, FE_STATE_MAIN);
        }
        break;
    }
    case FE_STATE_CMAP_AREA: {
        int d = fe_nav_step(fe, in, dt, 0);
        if (d && fe->ncm_areas > 0) { fe->cm_area_sel = wrap(fe->cm_area_sel + d, fe->ncm_areas + 1); fe->ev_move = 1; }      /* the areas, then Sponsors */
        if ((in->pressed & (FE_OK | FE_START)) && fe->ncm_areas > 0 && fe->cm_area_sel >= fe->ncm_areas) {
            fe->ev_ok = 1; fe_sponsors_open(fe);
        } else if ((in->pressed & (FE_OK | FE_START)) && fe->ncm_areas > 0) {
            fe->cm_area = fe->cm_areas[fe->cm_area_sel];
            fe->cm_loc_sel = 0;
            build_locs(fe);
            fe->ev_ok = 1; enter(fe, FE_STATE_CMAP_LOC);
        } else if ((in->pressed & FE_SQUARE) && fe->nskater_all > 0) {          /* another rider (the unlocked ones) */
            rebuild_skaters(fe, 1);
            fe->skater_back = FE_STATE_CMAP_AREA;
            fe->ev_ok = 1; enter(fe, FE_STATE_SKATER);
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1; enter(fe, FE_STATE_MAIN);
        }
        break;
    }
    case FE_STATE_CMAP_LOC: {
        int d = fe_nav_step(fe, in, dt, 0);
        if (d && fe->ncm_locs > 0) { fe->cm_loc_sel = wrap(fe->cm_loc_sel + d, fe->ncm_locs); fe->ev_move = 1; }
        if ((in->pressed & (FE_OK | FE_START)) && fe->ncm_locs > 0) {
            build_chals(fe);
            if (fe->ncm_chals > 0) { fe->cm_chal_sel = first_open(fe); fe->ev_ok = 1; enter(fe, FE_STATE_CMAP_CHAL); }
            else fe->ev_back = 1;                                  /* no events here (free skate only) */
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1; enter_map(fe);
        }
        break;
    }
    case FE_STATE_CMAP_CHAL: {
        int d = fe_nav_step(fe, in, dt, 0);
        if (d && fe->ncm_chals > 0) { fe->cm_chal_sel = wrap(fe->cm_chal_sel + d, fe->ncm_chals); fe->ev_move = 1; }
        int chal = fe->ncm_chals > 0 ? fe->cm_chals[fe->cm_chal_sel] : -1;
        int st = fe_event_status(fe, chal), runnable = chal >= 0 && career_chal_runnable(fe->career, chal);
        if ((in->pressed & (FE_OK | FE_START)) && st == FE_EV_OPEN && runnable) {            /* Go To Event */
            if (fe_story_intro(fe, chal)) { fe->ev_ok = 1; return 0; }          /* the scene before the event first (once) */
            return fe_launch_career_event(fe, chal);
        } else if (in->pressed & (FE_OK | FE_START)) {
            fe->ev_back = 1;                                       /* locked, done, closed or not built yet */
        } else if (in->pressed & FE_BACK) {
            fe->ev_back = 1; enter(fe, FE_STATE_CMAP_LOC);
        }
        break;
    }
    case FE_STATE_LOCATION: {
        int d = fe_nav_step(fe, in, dt, 0);
        if (d && fe->nloc > 0) { fe->loc_sel = wrap(fe->loc_sel + d, fe->nloc); fe->area_sel = fe->loc[fe->loc_sel].area; fe->ev_move = 1; }
        if (fe->nloc > 0 && (in->pressed & (FE_LEFT | FE_L))) change_area(fe, -1);       /* L / R / left / right jump to the previous / next area */
        if (fe->nloc > 0 && (in->pressed & (FE_RIGHT | FE_R))) change_area(fe, 1);
        if ((in->pressed & (FE_OK | FE_START)) && fe->nloc > 0) { fe->ev_ok = 1; fe->flash = 0.12f; enter(fe, FE_STATE_LAUNCH); return 1; }
        else if (in->pressed & FE_BACK) { fe->ev_back = 1; enter(fe, fe->nskater > 0 ? FE_STATE_SKATER : FE_STATE_MAIN); }
        break;
    }
    default:
        break;
    }
    return 0;
}

void fe_back_to_menu(Frontend *fe)
{
    fe->launch_chal = -1;
    if (fe->mode == FE_MODE_CAREER && fe->prof && fe->ncm_locs > 0) {          /* a career event: back to the list of the place's events, as they are now */
        build_chals(fe);
        fe->cm_chal_sel = first_open(fe);
        if (!fe_story_begin(fe, FE_STATE_CMAP_CHAL))               /* what the win changed in the story comes first */
            enter(fe, FE_STATE_CMAP_CHAL);
        return;
    }
    enter(fe, fe->mode == FE_MODE_TUTORIAL ? FE_STATE_TUTORIAL : FE_STATE_LOCATION);
}

int fe_tutorial_chal(const Frontend *fe)
{
    return fe->ntut > 0 ? fe->tut[fe->tut_sel] : -1;
}

static int tut_done(const Frontend *fe, int s)
{
    int chal = fe->tut[s];
    return fe->done && chal >= 0 && chal < FE_DONE_SIZE && fe->done[chal];
}

void fe_back_to_tutorials(Frontend *fe, int advance)
{
    if (advance && fe->ntut > 0) {                                 /* the next tutorial that is not done yet (just the following one if all are) */
        int pick = wrap(fe->tut_sel + 1, fe->ntut);
        for (int k = 1; k <= fe->ntut; k++) {
            int s = wrap(fe->tut_sel + k, fe->ntut);
            if (!tut_done(fe, s)) { pick = s; break; }
        }
        fe->tut_sel = pick;
    }
    fe->mode = FE_MODE_TUTORIAL;
    enter(fe, FE_STATE_TUTORIAL);
}

/* ---- drawing: one picture behind everything (the dimmed skyline), text lists with the big arrow at the chosen entry ---------------- */
void fe_hint(Frontend *fe, const char *text)
{
    ui_text_center(fe->ui, fe->f_s, UI_W / 2.0f, UI_H - 23, 1.0f, text, COL_HINT);
}

static void fade_in(Frontend *fe, float secs)
{
    if (fe->t < secs)
        ui_rect(fe->ui, 0, 0, UI_W, UI_H, UI_RGBA(0, 0, 0, (int)(255.0f * (1.0f - fe->t / secs))));
}

void fe_title(Frontend *fe, const char *text)
{
    ui_text_shadow(fe->ui, fe->f_xl, 18, 8, 1.0f, text, COL_YELLOW);
}

/* the big arrow that marks the chosen entry of a text menu, just after its text */
static void arrow_after(Frontend *fe, float x_end, float y, float row_h, float size)
{
    ui_image(fe->ui, fe->i_arrow, x_end + 6, y + (row_h - size) / 2.0f, size, size, UI_WHITE);
}

/* a scrolling list of names: LIST_ROWS rows, the chosen one in the middle when the list is long, the big arrow after it; a thin scroll bar when the list does not fit */
void fe_list(Frontend *fe, const char *const *names, int n, int sel, float x, float y0, float bar_x)
{
    Ui *ui = fe->ui;
    int first = sel - LIST_ROWS / 2;
    if (first > n - LIST_ROWS) first = n - LIST_ROWS;
    if (first < 0) first = 0;
    for (int r = 0; r < LIST_ROWS && first + r < n; r++) {
        float y = y0 + (float)(r * LIST_PITCH);
        int chosen = first + r == sel;
        ui_text_shadow(ui, fe->f_m, x, y, 1.0f, names[first + r], chosen ? UI_WHITE : UI_RGBA(255, 255, 255, 185));
        if (fe->mark && fe->mark[first + r]) {                     /* a small box in front of the name: green = done, orange = something to do */
            ui_rect(ui, x - 15.0f, y + 5.0f, 9.0f, 9.0f, UI_WHITE);
            ui_rect(ui, x - 14.0f, y + 6.0f, 7.0f, 7.0f, fe->mark[first + r] == FE_MARK_OPEN ? UI_RGBA(255, 150, 30, 255) : UI_RGBA(60, 200, 70, 255));
        }
        if (chosen)
            arrow_after(fe, x + ui_text_width(ui, fe->f_m, 1.0f, names[first + r]), y, 19.0f, 24.0f);
    }
    fe->mark = NULL;
    if (n > LIST_ROWS) {
        float th = (float)(LIST_ROWS * LIST_PITCH - 4), vis = (float)LIST_ROWS / (float)n, pos = (float)first / (float)(n - LIST_ROWS);
        ui_rect(ui, bar_x, y0, 3, th, UI_RGBA(0, 0, 0, 110));
        ui_rect(ui, bar_x, y0 + (th - th * vis) * pos, 3, th * vis, UI_RGBA(255, 255, 255, 220));
    }
}

/* a white frame around a dark panel (the 3D skater is drawn inside by the caller) */
void fe_frame(Frontend *fe, int x, int y, int w, int h)
{
    Ui *ui = fe->ui;
    ui_rect(ui, (float)x, (float)y, (float)w, (float)h, UI_RGBA(10, 22, 26, 150));
    ui_rect(ui, (float)x - 3, (float)y - 3, (float)w + 6, 2, UI_WHITE);
    ui_rect(ui, (float)x - 3, (float)y + (float)h + 1, (float)w + 6, 2, UI_WHITE);
    ui_rect(ui, (float)x - 3, (float)y - 3, 2, (float)h + 6, UI_WHITE);
    ui_rect(ui, (float)x + (float)w + 1, (float)y - 3, 2, (float)h + 6, UI_WHITE);
}


/* ---- the career map screens ----------------------------------------------------------------------------------------------------------------------- */
/* the place's picture in its frame at the left, the area name and a line under it (the location select and the career map share it) */
static void draw_loc_picture(Frontend *fe, const FeItem *it, const char *area, const char *line, uint32_t line_col)
{
    Ui *ui = fe->ui;
    ui_image(ui, fe->i_locbox, COL_L, LEFT_Y, LEFT_W, (float)LEFT_W * 120.0f / 224.0f, UI_WHITE);                    /* the picture box (224 x 120) scaled to the column's width */
    ui_image(ui, it->pic, COL_L + 7.0f * LEFT_W / 224.0f, LEFT_Y + 7.0f * LEFT_W / 224.0f, 210.0f * LEFT_W / 224.0f, 106.0f * LEFT_W / 224.0f, UI_WHITE);
    ui_text_shadow(ui, fe->f_m, COL_L + 2, LEFT_Y + 118, 1.0f, area, COL_YELLOW);
    ui_text_shadow(ui, fe->f_s, COL_L + 2, LEFT_Y + 142, 1.0f, line, line_col);
}

/* "Beat %s Points" with the score the goal system will ask for */
static void event_map_text(const Frontend *fe, const CareerChal *ch, char *out, size_t cap)
{
    const char *m = career_str(fe->career, ch->map_off), *p = strstr(m, "%s");
    long target = 0;
    const CareerObj *o = career_objs(fe->career, ch);
    for (int i = 0; i < ch->nobj; i++)
        if (o[i].type == OBJ_OWN_THE_SPOT) target = career_own_target(&o[i]);
    if (!p) { snprintf(out, cap, "%s", m); return; }
    snprintf(out, cap, "%.*s%ld%s", (int)(p - m), m, target, p + 2);
}

/* the left panel of an event: its name, what it asks for, the story text and the state */
static void draw_event_panel(Frontend *fe, int chal)
{
    Ui *ui = fe->ui;
    const CareerChal *ch = &fe->career->chal[chal];
    int st = fe_event_status(fe, chal), runnable = career_chal_runnable(fe->career, chal);
    const float x0 = COL_L + 10.0f, maxw = LEFT_W - 24.0f;
    char goal[160], la[3][96], lb[9][96];
    const char *name = st == FE_EV_LOCKED ? "Locked" : career_str(fe->career, ch->name_off);
    fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
    ui_text_shadow(ui, ui_text_width(ui, fe->f_m, 1.0f, name) > maxw ? fe->f_s : fe->f_m, x0, LEFT_Y + 7.0f, 1.0f, name, COL_YELLOW);
    int n1 = 0, n2 = 0, room = 6;
    if (st == FE_EV_LOCKED) {
        n2 = fe_wrap_text(fe, "Finish more events to open this one.", maxw, lb, 8);
    } else {
        event_map_text(fe, ch, goal, sizeof(goal));
        n1 = fe_wrap_text(fe, goal, maxw, la, 2);
        n2 = fe_wrap_text(fe, career_str(fe->career, ch->desc_off), maxw, lb, 9);
    }
    float y = LEFT_Y + 29.0f;
    for (int i = 0; i < n1; i++, y += 14.0f, room--)
        ui_text_shadow(ui, fe->f_s, x0, y, 1.0f, la[i], UI_WHITE);
    if (n2 > room) {                                           /* too long for the panel: the last line ends in dots */
        n2 = room;
        size_t l = strlen(lb[n2 - 1]);
        if (l > 90) l = 90;
        lb[n2 - 1][l] = 0;
        strcat(lb[n2 - 1], "...");
    }
    for (int i = 0; i < n2; i++, y += 14.0f)
        ui_text_shadow(ui, fe->f_s, x0, y, 1.0f, lb[i], UI_RGBA(200, 205, 205, 255));
    char line[64];
    uint32_t col = UI_WHITE;
    if (st == FE_EV_DONE) { snprintf(line, sizeof(line), "%s - done", career_mode_name(ch->mode)); col = UI_RGBA(120, 255, 120, 255); }
    else if (st == FE_EV_OPEN && runnable) { snprintf(line, sizeof(line), "%s - open", career_mode_name(ch->mode)); col = COL_YELLOW; }
    else if (st == FE_EV_OPEN) { snprintf(line, sizeof(line), "Not in the game yet"); col = UI_RGBA(255, 150, 30, 255); }
    else if (st == FE_EV_CLOSED) { snprintf(line, sizeof(line), "%s - not available now", career_mode_name(ch->mode)); col = UI_RGBA(200, 200, 200, 255); }
    else { snprintf(line, sizeof(line), "Locked"); col = UI_RGBA(200, 200, 200, 255); }
    ui_text_shadow(ui, fe->f_s, x0, LEFT_Y + LEFT_H - 24.0f, 1.0f, line, col);
}

void fe_draw_back(Frontend *fe)
{
    Ui *ui = fe->ui;
    ui_reset_view(ui);
    if (!(fe->state == FE_STATE_STORY && fe->scene3d))
        ui_image_cover(ui, fe->i_dim, UI_WHITE);
    if (fe->state == FE_STATE_SKATER || fe->state == FE_STATE_CUSTOM)                              /* the skater stands in this frame (the caller draws him between the two calls) */
        fe_frame(fe, fe->preview[0], fe->preview[1], fe->preview[2], fe->preview[3]);
}

void fe_draw_front(Frontend *fe)
{
    Ui *ui = fe->ui;
    ui_reset_view(ui);
    switch (fe->state) {
    case FE_STATE_TITLE:
        ui_image_fit_height(ui, fe->i_title, UI_WHITE);
        if (fmodf(fe->t, 1.2f) < 0.8f)
            ui_text_center(ui, fe->f_l, UI_W / 2.0f, 214, 1.0f, "PRESS START", COL_YELLOW);
        ui_text_center(ui, fe->f_s, UI_W / 2.0f, UI_H - 19, 1.0f, "(C) 2008 ELECTRONIC ARTS INC.", UI_WHITE);
        fade_in(fe, 0.7f);
        break;
    case FE_STATE_MAIN:
        ui_image(ui, fe->i_title, COL_L, LEFT_Y, LEFT_W, LEFT_H, UI_WHITE);     /* the game's logo picture in its frame, on the left (the skater viewer and the location picture have the same size) */
        for (int i = 0; i < FE_NMAIN; i++) {
            float y = (float)LEFT_Y + 3.0f + (float)i * 23.0f;
            uint32_t col = MAIN_ENABLED[i] ? UI_WHITE : UI_RGBA(255, 255, 255, 95);
            ui_text_shadow(ui, fe->f_m, COL_R, y, 1.0f, MAIN_ITEMS[i], col);
            if (i == fe->main_sel)
                arrow_after(fe, COL_R + ui_text_width(ui, fe->f_m, 1.0f, MAIN_ITEMS[i]), y, 19.0f, 24.0f);
        }
        fe_hint(fe, "UP / DOWN  CHOOSE       X  OK       O  BACK");
        fade_in(fe, 0.3f);
        break;
    case FE_STATE_SKATER:
        fe_title(fe, "Select Skater");
        if (fe->nskater > 0)
        {
            const char *nm[FE_MAX_ITEMS];
            for (int i = 0; i < fe->nskater; i++) nm[i] = fe->skater[i].name;
            fe_list(fe, nm, fe->nskater, fe->skater_sel, COL_R, LEFT_Y - 6, BAR_X);
        }
        fe_hint(fe, "UP / DOWN  SKATER       L / R  PAGE       X  OK       O  BACK");
        break;
    case FE_STATE_LOCATION: {
        fe_title(fe, "Select Location");
        if (fe->nloc > 0) {
            const FeItem *it = &fe->loc[fe->loc_sel];
            const char *nm[FE_MAX_ITEMS];
            for (int i = 0; i < fe->nloc; i++) nm[i] = fe->loc[i].name;
            fe_list(fe, nm, fe->nloc, fe->loc_sel, COL_R, LEFT_Y - 6, BAR_X);
            const char *area = it->area < (int)fe->career->nareas ? career_str(fe->career, fe->career->area_name[it->area]) : "";
            char line[48];
            if (it->chals > 0) snprintf(line, sizeof(line), it->chals == 1 ? "%d challenge" : "%d challenges", it->chals); else snprintf(line, sizeof(line), "free skate only");
            draw_loc_picture(fe, it, area, line, UI_WHITE);
        }
        fe_hint(fe, "UP / DOWN  LOCATION     L / R  AREA     X  GO     O  BACK");
        break;
    }
    case FE_STATE_OPTIONS:
        fe_options_draw(fe, 0);
        break;
    case FE_STATE_CMAP_AREA: {
        fe_title(fe, "Career");
        const char *nm[9];
        unsigned char mk[9];
        char text[96];
        int sel_open = 0, sel_done = 0, sel_total = 0;
        for (int i = 0; i < fe->ncm_areas; i++) {
            int open = 0, done = 0, total = 0;
            for (int k = 0; k < fe->nloc; k++)
                if (fe->loc[k].area == fe->cm_areas[i]) {
                    int o, d, t;
                    loc_counts(fe, fe->loc[k].level_id, &o, &d, &t);
                    open += o; done += d; total += t;
                }
            nm[i] = career_str(fe->career, fe->career->area_name[fe->cm_areas[i]]);
            mk[i] = (unsigned char)(open > 0 ? FE_MARK_OPEN : (total > 0 && done == total ? FE_MARK_DONE : 0));
            if (i == fe->cm_area_sel) { sel_open = open; sel_done = done; sel_total = total; }
        }
        if (fe->ncm_areas > 0) {
            int on_sponsors = fe->cm_area_sel >= fe->ncm_areas;
            nm[fe->ncm_areas] = "Sponsors"; mk[fe->ncm_areas] = 0;
            fe->mark = mk;
            fe_list(fe, nm, fe->ncm_areas + 1, fe->cm_area_sel, COL_R, LEFT_Y - 6, BAR_X);
            fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
            if (on_sponsors) {
                int signed_n = 0;
                for (int k = 0; k < PROFILE_NSPONSOR; k++) signed_n += fe->prof && fe->prof->sponsor[k] != 0;
                snprintf(text, sizeof(text), "%d of %d sponsors signed.", signed_n, PROFILE_NSPONSOR);
                fe_panel_text(fe, "Sponsors", text);
            } else {
                snprintf(text, sizeof(text), sel_open == 1 ? "%d event is open here." : "%d events are open here.", sel_open);
                fe_panel_text(fe, nm[fe->cm_area_sel], text);
                snprintf(text, sizeof(text), "%d of %d events done", sel_done, sel_total);
                ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + LEFT_H - 42.0f, 1.0f, text, UI_WHITE);
            }
            if (fe->prof && fe->prof->skater == PROFILE_CUSTOM_SKATER) {
                ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + LEFT_H - 24.0f, 1.0f, "Skater: Custom skater", COL_YELLOW);
            } else if (fe->prof && fe->career->nskaters > fe->prof->skater) {
                snprintf(text, sizeof(text), "Skater: %s", career_str(fe->career, fe->career->skater[fe->prof->skater].name_off));
                ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + LEFT_H - 24.0f, 1.0f, text, COL_YELLOW);
            }
        }
        fe_hint(fe, "UP / DOWN  CHOOSE       X  OK       SQUARE  SKATER       O  BACK");
        break;
    }
    case FE_STATE_CMAP_LOC: {
        fe_title(fe, career_str(fe->career, fe->career->area_name[fe->cm_area]));
        if (fe->ncm_locs > 0) {
            const FeItem *it = &fe->loc[fe->cm_locs[fe->cm_loc_sel]];
            const char *nm[FE_MAX_ITEMS];
            unsigned char mk[FE_MAX_ITEMS];
            char line[64];
            int sel_open = 0, sel_done = 0, sel_total = 0;
            for (int i = 0; i < fe->ncm_locs; i++) {
                int o, d, t;
                loc_counts(fe, fe->loc[fe->cm_locs[i]].level_id, &o, &d, &t);
                nm[i] = fe->loc[fe->cm_locs[i]].name;
                mk[i] = (unsigned char)(o > 0 ? FE_MARK_OPEN : (t > 0 && d == t ? FE_MARK_DONE : 0));
                if (i == fe->cm_loc_sel) { sel_open = o; sel_done = d; sel_total = t; }
            }
            fe->mark = mk;
            fe_list(fe, nm, fe->ncm_locs, fe->cm_loc_sel, COL_R, LEFT_Y - 6, BAR_X);
            if (sel_total == 0) snprintf(line, sizeof(line), "free skate only");
            else if (sel_open > 0) snprintf(line, sizeof(line), "%d open, %d of %d done", sel_open, sel_done, sel_total);
            else if (sel_total > 0) snprintf(line, sizeof(line), "none open, %d of %d done", sel_done, sel_total);
            draw_loc_picture(fe, it, career_str(fe->career, fe->career->area_name[it->area]), line, sel_open > 0 ? UI_WHITE : UI_RGBA(200, 200, 200, 255));
        }
        fe_hint(fe, "UP / DOWN  LOCATION       X  EVENTS       O  BACK");
        break;
    }
    case FE_STATE_CMAP_CHAL: {
        fe_title(fe, fe->ncm_locs > 0 ? fe->loc[fe->cm_locs[fe->cm_loc_sel]].name : "Events");
        if (fe->ncm_chals > 0) {
            const char *nm[16];
            unsigned char mk[16];
            for (int i = 0; i < fe->ncm_chals; i++) {
                int st = fe_event_status(fe, fe->cm_chals[i]);
                nm[i] = st == FE_EV_LOCKED ? "???" : career_str(fe->career, fe->career->chal[fe->cm_chals[i]].name_off);
                mk[i] = (unsigned char)(st == FE_EV_DONE ? FE_MARK_DONE : (st == FE_EV_OPEN ? FE_MARK_OPEN : 0));
            }
            fe->mark = mk;
            fe_list(fe, nm, fe->ncm_chals, fe->cm_chal_sel, COL_R, LEFT_Y - 6, BAR_X);
            int chal = fe->cm_chals[fe->cm_chal_sel];
            draw_event_panel(fe, chal);
            int st = fe_event_status(fe, chal);
            if (st == FE_EV_OPEN && career_chal_runnable(fe->career, chal)) fe_hint(fe, "UP / DOWN  CHOOSE       X  GO TO EVENT       O  BACK");
            else fe_hint(fe, "UP / DOWN  CHOOSE       O  BACK");
        }
        break;
    }
    case FE_STATE_STORY:
        fe_story_draw(fe);
        break;
    case FE_STATE_CREDITS:
        fe_credits_draw(fe);
        break;
    case FE_STATE_SPONSORS:
        fe_sponsors_draw(fe);
        break;
    case FE_STATE_MYSKATE: case FE_STATE_PROFILES: case FE_STATE_LOGBOOK: case FE_STATE_LOGPAGE: case FE_STATE_CUSTOM:
        fe_book_draw(fe);
        break;
    case FE_STATE_TUTORIAL: {
        fe_title(fe, "Tutorial");
        if (fe->ntut > 0) {
            const char *nm[FE_MAX_ITEMS];
            unsigned char mk[FE_MAX_ITEMS];
            for (int i = 0; i < fe->ntut && i < FE_MAX_ITEMS; i++) { nm[i] = career_str(fe->career, fe->career->chal[fe->tut[i]].name_off); mk[i] = (unsigned char)(tut_done(fe, i) ? FE_MARK_DONE : 0); }
            fe->mark = mk;
            fe_list(fe, nm, fe->ntut, fe->tut_sel, COL_R, LEFT_Y - 6, BAR_X);
            const CareerChal *ch = &fe->career->chal[fe->tut[fe->tut_sel]];
            fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
            fe_panel_text(fe, career_str(fe->career, ch->name_off), tutorial_summary(fe->tut[fe->tut_sel]));
            char line[40];
            int nd = 0;
            for (int i = 0; i < fe->ntut; i++) nd += tut_done(fe, i);
            snprintf(line, sizeof(line), tut_done(fe, fe->tut_sel) ? "Done  (%d of %d)" : "Not done yet  (%d of %d)", nd, fe->ntut);
            ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + LEFT_H - 24.0f, 1.0f, line, tut_done(fe, fe->tut_sel) ? UI_RGBA(120, 255, 120, 255) : COL_YELLOW);
        }
        fe_hint(fe, "UP / DOWN  CHOOSE       X  START       O  BACK");
        break;
    }
    default:
        break;
    }
}

void fe_draw_menu(Frontend *fe, const char *title, const char *const *items, const unsigned char *enabled, int n, int sel)
{
    Ui *ui = fe->ui;
    ui_reset_view(ui);
    ui_rect(ui, 0, 0, UI_W, UI_H, UI_RGBA(0, 0, 0, 150));
    float y0 = 136.0f - (float)n * 11.5f;
    ui_text_center(ui, fe->f_xl, UI_W / 2.0f, y0 - 40, 1.0f, title, COL_YELLOW);
    for (int i = 0; i < n; i++) {
        float y = y0 + (float)i * 23.0f, w = ui_text_width(ui, fe->f_l, 1.0f, items[i]);
        ui_text_center(ui, fe->f_l, UI_W / 2.0f, y, 1.0f, items[i], (!enabled || enabled[i]) ? UI_WHITE : UI_RGBA(255, 255, 255, 95));
        if (i == sel)
            arrow_after(fe, UI_W / 2.0f + w / 2.0f, y, 22.0f, 32.0f);
    }
}
