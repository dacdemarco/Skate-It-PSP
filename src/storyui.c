/* The story screens of the front end (M7): the dialogue scenes (text pages), the sponsor signing, the "Skater Of The Year" ending, the credits and the sponsors page. They belong to
 * src/frontend.c (same state machine, same look); see frontend.h and src/story.h. No PSP dependencies. */
#include "frontend.h"
#include <stdio.h>
#include <string.h>

#define COL_DIM UI_RGBA(200, 205, 205, 255)
#define PANEL_X 18
#define PANEL_W 444
#define SLOT_NAMES_N 4
static const char *const SLOT_NAME[SLOT_NAMES_N] = { "Board", "Trucks", "Wheels", "Shoes" };

static void enter(Frontend *fe, int state)
{
    fe->state = state;
    fe->t = 0.0f;
    fe->rep_dir = 0;
}

/* the name the story calls the player: the career skater's */
static const char *player_name(const Frontend *fe)
{
    if (fe->prof && fe->prof->skater == PROFILE_CUSTOM_SKATER)
        return "Your skater";
    if (fe->prof && fe->prof->skater < fe->career->nskaters)
        return career_str(fe->career, fe->career->skater[fe->prof->skater].name_off);
    return "You";
}

/* ---- the story items ---------------------------------------------------------------------------------------------------------------------------------- */
static void begin_item(Frontend *fe, const StoryItem *it)
{
    fe->story = *it;
    fe->page = 0;
    fe->npages = 1;
    if (it->kind == STORY_SCENE)
        fe->npages = story_pages(fe->career, it->scene, fe->pages, STORY_MAX_PAGES);
    else if (it->kind == STORY_ENDING)
        fe->npages = 2;                                            /* the cover, then the end-of-game text */
    if (fe->npages < 1) fe->npages = 1;
    if (it->kind == STORY_CREDITS) {
        fe_credits_open(fe, fe->story_return);
        return;
    }
    enter(fe, FE_STATE_STORY);
}

int fe_story_begin(Frontend *fe, int return_state)
{
    StoryItem it;
    fe->story_return = return_state;
    fe->story_launch = -1;
    if (!fe->prof || !story_next(fe->prof, fe->career, &it))
        return 0;
    begin_item(fe, &it);
    return 1;
}

int fe_story_intro(Frontend *fe, int chal)
{
    int sc = fe->prof ? story_intro(fe->prof, fe->career, chal) : 0;
    if (!sc)
        return 0;
    StoryItem it = { STORY_SCENE, sc, -1, chal, sc };
    fe->story_return = fe->state;
    fe->story_launch = chal;
    begin_item(fe, &it);
    return 1;
}

/* the item on screen has been read: mark it, then the launch it was the intro of, the next item, or back where we came from. Returns 1 for a launch. */
static int finish_item(Frontend *fe)
{
    StoryItem it;
    if (fe->prof) {
        story_done(fe->prof, &fe->story);
        fe->ev_profile = 1;
    }
    fe->story.kind = STORY_NONE;
    if (fe->story_launch >= 0) {
        int chal = fe->story_launch;
        fe->story_launch = -1;
        return fe_launch_career_event(fe, chal);
    }
    if (fe->prof && story_next(fe->prof, fe->career, &it)) {
        begin_item(fe, &it);
        return 0;
    }
    enter(fe, fe->story_return);
    return 0;
}

int fe_story_update(Frontend *fe, const FeInput *in, float dt)
{
    (void)dt;
    if ((in->pressed & FE_SQUARE) && fe->scene3d && fe->story.kind == STORY_SCENE) {         /* the guest turns round (a test switch: which way the original has him facing is not certain) */
        fe->scene_flip = !fe->scene_flip;
        fe->ev_move = 1;
    }
    if (in->pressed & (FE_OK | FE_START)) {
        fe->ev_ok = 1;
        if (fe->page + 1 < fe->npages) { fe->page++; return 0; }
        return finish_item(fe);
    }
    if (in->pressed & FE_BACK) {                                   /* skip the rest of it */
        fe->ev_back = 1;
        return finish_item(fe);
    }
    return 0;
}

/* a big sponsor picture (the 42 x 42 icon of the original scaled up) */
static void sponsor_icon(Frontend *fe, const char *key, float x, float y, float size)
{
    char name[40];
    snprintf(name, sizeof(name), "sp_%s", key);
    ui_image(fe->ui, uipak_image(fe->ui->pak, name), x, y, size, size, UI_WHITE);
}

static void panel_lines(Frontend *fe, const char *text, float x, float y, float maxw, int max_lines, uint32_t col)
{
    char lines[8][96];
    int n = fe_wrap_text(fe, text, maxw, lines, max_lines < 8 ? max_lines : 8);
    for (int i = 0; i < n; i++)
        ui_text_shadow(fe->ui, fe->f_s, x, y + (float)i * 15.0f, 1.0f, lines[i], col);
}

void fe_story_draw(Frontend *fe)
{
    Ui *ui = fe->ui;
    const Career *c = fe->career;
    char buf[200];
    switch (fe->story.kind) {
    case STORY_SCENE: {
        const StoryPage *pg = &fe->pages[fe->page];
        int chal = fe->story.chal;
        const char *sp = chal >= 0 ? career_sponsor_icon(c, chal) : NULL;
        fe_title(fe, pg->speaker[0] ? pg->speaker : "Story");
        if (chal >= 0) {                                           /* a scene that belongs to an event: its name (and its sponsor's picture) above the text */
            float x = PANEL_X;
            if (sp) { sponsor_icon(fe, sp, x, 56.0f, 56.0f); x += 70.0f; }
            ui_text_shadow(ui, fe->f_l, x, sp ? 60.0f : 56.0f, 1.0f, career_str(c, c->chal[chal].name_off), UI_WHITE);
            if (sp) ui_text_shadow(ui, fe->f_s, x, 88.0f, 1.0f, career_sponsor_name(c, chal), COL_YELLOW);
        }
        int py = fe->scene3d ? 150 : 126, ph = fe->scene3d ? 82 : 100;                            /* (with the guest on screen the panel sits low) */
        fe_frame(fe, PANEL_X + 3, py, PANEL_W - 6, ph);
        float y = (float)py + 8.0f;
        for (int i = 0; i < pg->n; i++) {
            char who[STORY_NAME_MAX];
            const char *t = story_line_text(career_scene_line(c, fe->story.scene, pg->first + i), who, sizeof(who));
            char lines[4][96];
            int nl = fe_wrap_text(fe, t, PANEL_W - 36.0f, lines, 4);
            for (int k = 0; k < nl; k++, y += 15.0f)
                ui_text_shadow(ui, fe->f_s, PANEL_X + 14.0f, y, 1.0f, lines[k], UI_WHITE);
            y += 3.0f;
        }
        snprintf(buf, sizeof(buf), "%d / %d", fe->page + 1, fe->npages);
        ui_text_right(ui, fe->f_s, PANEL_X + PANEL_W - 14.0f, (float)(py + ph - 16), 1.0f, buf, COL_DIM);
        if (fe->scene3d)
            fe_hint(fe, fe->page + 1 < fe->npages ? "X  NEXT       O  SKIP       SQUARE  TURN GUEST" : (fe->story_launch >= 0 ? "X  GO       O  SKIP       SQUARE  TURN GUEST" : "X  OK       O  SKIP       SQUARE  TURN GUEST"));
        else
            fe_hint(fe, fe->page + 1 < fe->npages ? "X  NEXT       O  SKIP" : (fe->story_launch >= 0 ? "X  GO       O  SKIP" : "X  OK       O  SKIP"));
        break;
    }
    case STORY_SIGNED: {
        int chal = fe->story.chal;
        fe_title(fe, "Signed!");
        fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
        const char *key = career_sponsor_icon(c, chal);
        if (key) sponsor_icon(fe, key, COL_L + (LEFT_W - 96) / 2.0f, LEFT_Y + 20.0f, 96.0f);
        ui_text_center(ui, fe->f_m, COL_L + LEFT_W / 2.0f, LEFT_Y + 118.0f, 1.0f, career_sponsor_name(c, chal), COL_YELLOW);
        snprintf(buf, sizeof(buf), career_str(c, c->sign_str), player_name(fe));
        panel_lines(fe, buf, COL_R, LEFT_Y + 4.0f, 200.0f, 3, UI_WHITE);
        ui_text_shadow(ui, fe->f_l, COL_R, LEFT_Y + 60.0f, 1.0f, career_sponsor_name(c, chal), COL_YELLOW);
        snprintf(buf, sizeof(buf), "%s sponsor", fe->story.slot >= 0 && fe->story.slot < SLOT_NAMES_N ? SLOT_NAME[fe->story.slot] : "A");
        for (char *q = buf + 1; *q && *q != ' '; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
        ui_text_shadow(ui, fe->f_s, COL_R, LEFT_Y + 86.0f, 1.0f, buf, UI_WHITE);
        { int signed_n = 0; for (int s = 0; s < PROFILE_NSPONSOR; s++) signed_n += fe->prof && (s == fe->story.slot || progress_seen(fe->prof, SEEN_SIGNED + s));       /* (this one and the ones shown before) */
          snprintf(buf, sizeof(buf), "%d of %d sponsors", signed_n, PROFILE_NSPONSOR);
          ui_text_shadow(ui, fe->f_s, COL_R, LEFT_Y + 106.0f, 1.0f, buf, COL_DIM); }
        fe_hint(fe, "X  OK");
        break;
    }
    case STORY_ENDING: {
        int img = uipak_image(ui->pak, "thrasher_sm");
        fe_title(fe, fe->page == 0 ? "Skater Of The Year" : "Career Complete");
        if (fe->page == 0) {
            ui_image(ui, img, COL_L + 10.0f, 44.0f, 208.0f, 176.0f, UI_WHITE);           /* the cover; the skater's name goes on it */
            ui_text_center(ui, fe->f_l, COL_L + 10.0f + 104.0f, 150.0f, 1.0f, player_name(fe), UI_RGBA(190, 20, 20, 255));
            snprintf(buf, sizeof(buf), career_str(c, c->soty_str), player_name(fe));
            panel_lines(fe, buf, 250.0f, 70.0f, 205.0f, 6, UI_WHITE);
        } else {
            fe_frame(fe, PANEL_X + 3, 60, PANEL_W - 6, 140);
            panel_lines(fe, career_str(c, c->end_str), PANEL_X + 16.0f, 72.0f, PANEL_W - 40.0f, 8, UI_WHITE);
        }
        fe_hint(fe, "X  NEXT");
        break;
    }
    default:
        break;
    }
}

/* ---- the credits ------------------------------------------------------------------------------------------------------------------------------------- */
#define CREDIT_W 420.0f
#define CREDIT_SPEED 30.0f

static int is_heading(const char *s)
{
    int letters = 0;
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') return 0;
        letters += *s >= 'A' && *s <= 'Z';
    }
    return letters >= 3;
}

static int credit_font(const Frontend *fe, const char *s, int first)
{
    return first ? fe->f_xl : (is_heading(s) ? fe->f_m : fe->f_s);
}

/* where every line goes (a long one takes several rows), and the height of it all */
static void credits_layout(Frontend *fe)
{
    float y = 0.0f;
    char lines[8][96];
    for (uint32_t i = 0; i < fe->career->ncredits && i < FE_MAX_CREDITS; i++) {
        const char *s = career_str(fe->career, fe->career->credit[i]);
        int blank = s[0] == 0 || (s[0] == ' ' && s[1] == 0);
        int f = credit_font(fe, s, i == 0);
        float pitch = f == fe->f_xl ? 30.0f : (f == fe->f_m ? 22.0f : 16.0f);
        int rows = 1;
        if (!blank && ui_text_width(fe->ui, f, 1.0f, s) > CREDIT_W) {
            rows = fe_wrap_text(fe, s, CREDIT_W, lines, 8);
            if (rows < 1) rows = 1;
        }
        if (is_heading(s) && i > 0) y += 12.0f;                    /* a gap before a new section */
        fe->cr_y[i] = y;
        fe->cr_rows[i] = (unsigned char)rows;
        y += blank ? 10.0f : pitch * (float)rows;
    }
    fe->cr_h = y;
}

void fe_credits_open(Frontend *fe, int return_state)
{
    fe->credits_return = return_state;
    fe->credits_y = 0.0f;
    credits_layout(fe);
    enter(fe, FE_STATE_CREDITS);
}

static int credits_finish(Frontend *fe)
{
    if (fe->story.kind == STORY_CREDITS)                           /* part of the career's ending: marked, then whatever is next */
        return finish_item(fe);
    enter(fe, fe->credits_return);
    return 0;
}

int fe_credits_update(Frontend *fe, const FeInput *in, float dt)
{
    float speed = CREDIT_SPEED * ((in->held & FE_OK) ? 4.0f : 1.0f);
    fe->credits_y += speed * dt;
    if ((in->held & FE_DOWN) && !(in->held & FE_OK)) fe->credits_y += 90.0f * dt;
    if ((in->held & FE_UP) && fe->credits_y > 0.0f) fe->credits_y -= 120.0f * dt;
    if (fe->credits_y < 0.0f) fe->credits_y = 0.0f;
    if ((in->pressed & (FE_BACK | FE_START)) || fe->credits_y > fe->cr_h + (float)UI_H) {
        fe->ev_back = 1;
        return credits_finish(fe);
    }
    return 0;
}

void fe_credits_draw(Frontend *fe)
{
    Ui *ui = fe->ui;
    ui_rect(ui, 0, 0, UI_W, UI_H, UI_RGBA(0, 0, 0, 120));
    float base = (float)UI_H - fe->credits_y;                    /* the first line starts below the screen and moves up */
    char lines[8][96];
    for (uint32_t i = 0; i < fe->career->ncredits && i < FE_MAX_CREDITS; i++) {
        float y = base + fe->cr_y[i];
        const char *s = career_str(fe->career, fe->career->credit[i]);
        int f = credit_font(fe, s, i == 0);
        float pitch = f == fe->f_xl ? 30.0f : (f == fe->f_m ? 22.0f : 16.0f);
        if (y > (float)UI_H || y + pitch * (float)fe->cr_rows[i] < 0.0f) continue;
        uint32_t col = i == 0 ? COL_YELLOW : (is_heading(s) ? COL_YELLOW : UI_WHITE);
        if (fe->cr_rows[i] > 1) {
            int n = fe_wrap_text(fe, s, CREDIT_W, lines, 8);
            for (int k = 0; k < n; k++)
                ui_text_center(ui, f, UI_W / 2.0f, y + pitch * (float)k, 1.0f, lines[k], col);
        } else {
            ui_text_center(ui, f, UI_W / 2.0f, y, 1.0f, s, col);
        }
    }
    ui_rect(ui, 0, UI_H - 34.0f, UI_W, 34.0f, UI_RGBA(0, 0, 0, 190));                                    /* the hint is not drawn over the credits */
    fe_hint(fe, "X  FASTER       UP / DOWN  SCROLL       O  LEAVE");
}

/* ---- the sponsors page ---------------------------------------------------------------------------------------------------------------------------------- */
void fe_sponsors_open(Frontend *fe)
{
    fe->sp_sel = 0;
    enter(fe, FE_STATE_SPONSORS);
}

int fe_sponsors_update(Frontend *fe, const FeInput *in, float dt)
{
    int d = fe_nav_step(fe, in, dt, 0);
    if (d) { fe->sp_sel = (fe->sp_sel + d + SLOT_NAMES_N) % SLOT_NAMES_N; fe->ev_move = 1; }
    if (in->pressed & (FE_BACK | FE_OK | FE_START)) {
        fe->ev_back = 1;
        enter(fe, FE_STATE_CMAP_AREA);
    }
    return 0;
}

void fe_sponsors_draw(Frontend *fe)
{
    Ui *ui = fe->ui;
    const Career *c = fe->career;
    char rows[SLOT_NAMES_N][48];
    const char *nm[SLOT_NAMES_N];
    fe_title(fe, "Sponsors");
    for (int s = 0; s < SLOT_NAMES_N; s++) {
        int ch = fe->prof && fe->prof->sponsor[s] ? fe->prof->sponsor[s] - 1 : -1;
        snprintf(rows[s], sizeof(rows[s]), "%s: %s", SLOT_NAME[s], ch >= 0 ? career_sponsor_name(c, ch) : "-");
        nm[s] = rows[s];
    }
    fe_list(fe, nm, SLOT_NAMES_N, fe->sp_sel, COL_R, LEFT_Y - 6, BAR_X);
    fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
    int sel = fe->sp_sel, ch = fe->prof && fe->prof->sponsor[sel] ? fe->prof->sponsor[sel] - 1 : -1;
    if (ch >= 0) {
        const char *key = career_sponsor_icon(c, ch);
        if (key) sponsor_icon(fe, key, COL_L + (LEFT_W - 84) / 2.0f, LEFT_Y + 14.0f, 84.0f);
        ui_text_center(ui, fe->f_m, COL_L + LEFT_W / 2.0f, LEFT_Y + 104.0f, 1.0f, career_sponsor_name(c, ch), COL_YELLOW);
        ui_text_center(ui, fe->f_s, COL_L + LEFT_W / 2.0f, LEFT_Y + 124.0f, 1.0f, "Signed", UI_RGBA(120, 255, 120, 255));
    } else {
        fe_panel_text(fe, SLOT_NAME[sel], "No sponsor yet. Win one of the sponsor events to be signed.");
    }
    fe_hint(fe, "UP / DOWN  CHOOSE       O  BACK");
}
