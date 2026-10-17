/* The options screens (no PSP dependencies; part of the front end, see frontend.h): Controls (a preset, or every button set by hand), the Trick guide (every trick by category, with how
 * to do it on the buttons of the mapping in use), Gravity (the physics tuning) and Information (which lines show while skating).
 * Used by the main menu and, over the paused game, by the pause menu. They change fe->set and raise fe->opt.changed; the caller saves the settings file. */
#include "frontend.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MAXROWS 34

static const char *const PAGE_TITLE[OPT_NPAGES] = { "Options", "Controls", "Custom Controls", "Trick Guide", "Trick Guide", "Gravity", "Information", "Game Settings" };
static const char *const SHORT_ACTION[ACT_COUNT] = { "Pop", "Push", "Brake", "Adv. pop", "Grab L", "Grab R" };
enum { ROOT_CONTROLS = 0, ROOT_GUIDE, ROOT_GRAVITY, ROOT_INFO, ROOT_GAME, ROOT_CREDITS, ROOT_BACK, ROOT_ROWS };
static const char *const ROOT_NAME[ROOT_ROWS] = { "Controls", "Trick guide", "Gravity", "Information", "Game settings", "Credits", "Back" };
/* the rows of the Game settings page */
enum { GAME_STANCE = 0, GAME_UNITS, GAME_SFX, GAME_MUSIC, GAME_ROWS };
/* the entry of row `sel` of the root page (Credits is left out over the pause menu) */
static int root_id(const FeOptions *o, int sel)
{
    return o->no_credits && sel >= ROOT_CREDITS ? sel + 1 : sel;
}

/* ---- the trick guide: every trick the game has, by category, with how it is done. {POP} {ADV} {PUSH} {BRAKE} {GL} {GR} are the buttons of the mapping in use (src/control.h is the scheme). ---- */
enum { CAT_BASICS = 0, CAT_FLIPS, CAT_ADV_FLIPS, CAT_LATE, CAT_SPINS, CAT_GRABS, CAT_GRINDS, CAT_MOVES, CAT_COUNT };
static const char *const CAT_NAME[CAT_COUNT] = { "Basics", "Flip tricks", "Advanced flips", "Late flips", "Spins", "Grabs", "Grinds", "Ground moves" };
static const char *const CAT_HELP[CAT_COUNT] = {
    "Pushing, braking, the ollie and the nollie, manuals.",
    "The six flips of a flick with {POP} held. Hold the nub forward first for the nollie version.",
    "The flicks with {ADV} held, and the body flips with both pops held.",
    "Flips started in the air with a flick of the nub, after the pop.",
    "Turning in the air with {PUSH} and the nub.",
    "The 29 grabs: {GL} / {GR} in the air with the nub, a flick while held for the variants.",
    "The 16 grinds: the board locks on by itself; the nub picks the grind.",
    "Reverts, powerslides and wallrides.",
};
typedef struct { unsigned char cat; const char *name; const char *how; } GuideEntry;
static const GuideEntry GUIDE[] = {
    { CAT_BASICS, "Push",            "Tap {PUSH} for a quick push. Hold it for a stronger, slower push." },
    { CAT_BASICS, "Brake",           "Hold {BRAKE}." },
    { CAT_BASICS, "Ollie",           "Hold {POP} to crouch, let go to pop. The longer the crouch, the higher the pop." },
    { CAT_BASICS, "Nollie",          "Crouch with {POP} and hold the nub forward until NOLLIE shows, then let go. A flick straight down goes back to the tail." },
    { CAT_BASICS, "Manual",          "Rolling, pull the nub back and hold it a moment. Balance with the nub forward and back; {POP} pops out." },
    { CAT_BASICS, "Nose manual",     "Rolling, push the nub forward and hold it a moment. Balance with the nub; {POP} pops out." },
    { CAT_BASICS, "Air steering",    "The nub turns a little in the air. Near a rail or ledge it picks the grind instead." },
    { CAT_FLIPS, "Kickflip",         "Hold {POP}, flick the nub UP-LEFT, let go." },
    { CAT_FLIPS, "Heelflip",         "Hold {POP}, flick the nub UP-RIGHT, let go." },
    { CAT_FLIPS, "Pop shuvit",       "Hold {POP}, flick the nub LEFT, let go." },
    { CAT_FLIPS, "FS pop shuvit",    "Hold {POP}, flick the nub RIGHT, let go." },
    { CAT_FLIPS, "360 pop shuvit",   "Hold {POP}, flick the nub DOWN-LEFT, let go." },
    { CAT_FLIPS, "FS 360 pop shuvit", "Hold {POP}, flick the nub DOWN-RIGHT, let go." },
    { CAT_FLIPS, "Nollie flips",     "Hold {POP} and the nub forward until NOLLIE shows, then flick as for the flip: its nollie version. Every flip has one." },
    { CAT_ADV_FLIPS, "Varial kickflip", "Hold {ADV}, flick the nub UP-LEFT, let go." },
    { CAT_ADV_FLIPS, "Varial heelflip", "Hold {ADV}, flick the nub UP-RIGHT, let go." },
    { CAT_ADV_FLIPS, "Hardflip",     "Hold {ADV}, flick the nub LEFT, let go." },
    { CAT_ADV_FLIPS, "Inward heel",  "Hold {ADV}, flick the nub RIGHT, let go." },
    { CAT_ADV_FLIPS, "360 flip",     "Hold {ADV}, flick the nub DOWN-LEFT, let go." },
    { CAT_ADV_FLIPS, "Laser flip",   "Hold {ADV}, flick the nub DOWN-RIGHT, let go." },
    { CAT_ADV_FLIPS, "360 hardflip", "Hold {POP} and {ADV} together, flick the nub UP-LEFT, let go." },
    { CAT_ADV_FLIPS, "360 inward heel", "Hold {POP} and {ADV} together, flick the nub UP-RIGHT, let go." },
    { CAT_ADV_FLIPS, "Frontflip",    "Hold {POP} and {ADV} together, flick the nub UP, let go. Or in the air: press {PUSH} and flick UP." },
    { CAT_ADV_FLIPS, "Backflip",     "Hold {POP} and {ADV} together, flick the nub DOWN, let go. Or in the air: press {PUSH} and flick DOWN." },
    { CAT_LATE, "Late kickflip",     "In the air, flick the nub UP-LEFT." },
    { CAT_LATE, "Late heelflip",     "In the air, flick the nub UP-RIGHT." },
    { CAT_LATE, "Late pop shuvit",   "In the air, flick the nub LEFT." },
    { CAT_LATE, "Late FS pop shuvit", "In the air, flick the nub RIGHT." },
    { CAT_LATE, "Late backfoot kickflip", "In the air, flick the nub DOWN-LEFT." },
    { CAT_LATE, "Late backfoot heelflip", "In the air, flick the nub DOWN-RIGHT." },
    { CAT_SPINS, "BS spin",          "In the air, press and hold {PUSH} and hold the nub LEFT. He turns in 180s and finishes the half turn he is in." },
    { CAT_SPINS, "FS spin",          "In the air, press and hold {PUSH} and hold the nub RIGHT." },
    { CAT_SPINS, "Half-cab / cab",   "A spin taken off rolling backwards (fakie): 180 is a half-cab, 360 a cab." },
    { CAT_SPINS, "Revert after it",  "Land a half turn and you roll fakie; hold {BRAKE} and flick sideways to revert." },
    { CAT_GRABS, "BS grab",          "In the air hold {GL} (the nub neutral or left)." },
    { CAT_GRABS, "FS grab",          "In the air hold {GL} with the nub RIGHT." },
    { CAT_GRABS, "Tail grab",        "In the air hold {GL} with the nub UP." },
    { CAT_GRABS, "Stale",            "In the air hold {GL} with the nub DOWN." },
    { CAT_GRABS, "Melon",            "In the air hold {GR} (the nub neutral)." },
    { CAT_GRABS, "Nose grab",        "In the air hold {GR} with the nub UP." },
    { CAT_GRABS, "Mute",             "In the air hold {GR} with the nub DOWN." },
    { CAT_GRABS, "Double grab",      "In the air hold {GL} and {GR} together." },
    { CAT_GRABS, "Nosebone",         "Holding a BS or FS grab, flick the nub UP." },
    { CAT_GRABS, "Tailbone",         "Holding a BS or FS grab, flick the nub DOWN." },
    { CAT_GRABS, "Tuck knee",        "Holding a BS or FS grab, flick the nub LEFT or RIGHT." },
    { CAT_GRABS, "BS tweak",         "Holding a BS or FS grab, flick the nub diagonally UP." },
    { CAT_GRABS, "Method",           "Holding a melon, flick the nub diagonally UP." },
    { CAT_GRABS, "Stale nosebone",   "Holding a stale, flick the nub UP." },
    { CAT_GRABS, "Stale tailbone",   "Holding a stale, flick the nub DOWN." },
    { CAT_GRABS, "Stale tuck knee",  "Holding a stale, flick the nub LEFT or RIGHT." },
    { CAT_GRABS, "Mute nosebone",    "Holding a mute, flick the nub UP." },
    { CAT_GRABS, "Mute tailbone",    "Holding a mute, flick the nub DOWN." },
    { CAT_GRABS, "Mute tuck knee",   "Holding a mute, flick the nub LEFT or RIGHT." },
    { CAT_GRABS, "BS shifty nose grab", "Holding a nose grab, flick the nub UP." },
    { CAT_GRABS, "FS shifty nose grab", "Holding a nose grab, flick the nub DOWN." },
    { CAT_GRABS, "Rocket air",       "Holding a nose grab, flick the nub LEFT or RIGHT." },
    { CAT_GRABS, "BS shifty tail grab", "Holding a tail grab, flick the nub UP." },
    { CAT_GRABS, "FS shifty tail grab", "Holding a tail grab, flick the nub DOWN." },
    { CAT_GRABS, "Christ air",       "Holding a double grab, flick the nub UP." },
    { CAT_GRABS, "Coffin",           "Holding a double grab, flick the nub DOWN." },
    { CAT_GRABS, "Cross bone",       "Holding a double grab, flick the nub LEFT." },
    { CAT_GRABS, "Stiffy",           "Holding a double grab, flick the nub RIGHT." },
    { CAT_GRABS, "No-foot air",      "Holding a double grab, flick the nub UP-LEFT." },
    { CAT_GRABS, "Back to the grab", "Holding any variant, flick the nub diagonally DOWN for the plain grab again." },
    { CAT_GRINDS, "How grinds work", "Land on a rail or ledge: the board locks on by itself. The nub at that moment picks the grind; BS / FS follows the side of the rail." },
    { CAT_GRINDS, "Switching and off", "No balancing: the board rides the rail to its end. A flick while grinding switches to that grind. Tap {POP} to hop; hold it and let go to jump off (towards the nub)." },
    { CAT_GRINDS, "50-50",           "Land on the rail with the nub neutral." },
    { CAT_GRINDS, "Nosegrind",       "Land on the rail with the nub UP." },
    { CAT_GRINDS, "5-0",             "Land on the rail with the nub DOWN." },
    { CAT_GRINDS, "Boardslide",      "Land on the rail with the nub LEFT." },
    { CAT_GRINDS, "Lipslide",        "Land on the rail with the nub RIGHT." },
    { CAT_GRINDS, "Noseslide",       "Land on the rail with the nub UP-LEFT." },
    { CAT_GRINDS, "Tailslide",       "Land on the rail with the nub DOWN-LEFT." },
    { CAT_GRINDS, "Noseblunt",       "Land on the rail with the nub UP-RIGHT." },
    { CAT_GRINDS, "Blunt",           "Land on the rail with the nub DOWN-RIGHT." },
    { CAT_GRINDS, "Smith",           "Hold {ADV} in the air, land with the nub neutral." },
    { CAT_GRINDS, "Crooked",         "Hold {ADV} in the air, land with the nub UP." },
    { CAT_GRINDS, "Feeble",          "Hold {ADV} in the air, land with the nub DOWN." },
    { CAT_GRINDS, "Overcrooks",      "Hold {ADV} in the air, land with the nub LEFT." },
    { CAT_GRINDS, "Willy",           "Hold {ADV} in the air, land with the nub RIGHT." },
    { CAT_GRINDS, "Overwilly",       "Hold {ADV} in the air, land with the nub UP-LEFT." },
    { CAT_GRINDS, "Salad",           "Hold {ADV} in the air, land with the nub DOWN-LEFT." },
    { CAT_MOVES, "Revert",           "Rolling, hold {BRAKE} and flick the nub LEFT (BS) or RIGHT (FS): a half turn on the ground, then fakie." },
    { CAT_MOVES, "Powerslide",       "At speed, hold {BRAKE} and push the nub hard LEFT or RIGHT and keep it there." },
    { CAT_MOVES, "Wallride",         "Jump along a tall wall at an angle, at speed: the board rides it by itself. Let go of {POP} (held) to kick off it." },
};
#define NGUIDE ((int)(sizeof(GUIDE) / sizeof(GUIDE[0])))

static int guide_count(int cat)
{
    int n = 0;
    for (int i = 0; i < NGUIDE; i++) n += GUIDE[i].cat == cat;
    return n;
}

/* the k-th entry of a category */
static const GuideEntry *guide_entry(int cat, int k)
{
    for (int i = 0; i < NGUIDE; i++)
        if (GUIDE[i].cat == cat && k-- == 0) return &GUIDE[i];
    return &GUIDE[0];
}

/* riding regular, a turn to the left is a frontside one: the two spin entries trade their names and the revert text its sides */
static const char *guide_name(const Frontend *fe, const GuideEntry *g)
{
    if (fe->regular) {
        if (strcmp(g->name, "BS spin") == 0) return "FS spin";
        if (strcmp(g->name, "FS spin") == 0) return "BS spin";
    }
    return g->name;
}

static const char *guide_how(const Frontend *fe, const GuideEntry *g)
{
    if (fe->regular && strcmp(g->name, "Revert") == 0)
        return "Rolling, hold {BRAKE} and flick the nub LEFT (FS) or RIGHT (BS): a half turn on the ground, then fakie.";
    return g->how;
}

/* the text with the {POP} .. {GR} tokens replaced by the names of the buttons of the mapping */
static void guide_text(const Settings *s, const char *tmpl, char *out, size_t cap)
{
    settings_expand_buttons(s, tmpl, out, cap);
}

static int page_rows(const Frontend *fe)
{
    const FeOptions *o = &fe->opt;
    switch (o->page) {
    case OPT_ROOT: return o->no_credits ? ROOT_ROWS - 1 : ROOT_ROWS;
    case OPT_CONTROLS: return PRESET_COUNT;
    case OPT_MAP: return ACT_COUNT + 1;
    case OPT_GUIDE: return CAT_COUNT;
    case OPT_GUIDE_LIST: return guide_count(o->cat);
    case OPT_GRAVITY: return TUNE_COUNT + 1;
    case OPT_GAME: return fe->have_music ? GAME_ROWS : GAME_ROWS - 1;
    default: return INFO_COUNT + 2;
    }
}

/* the texts of the rows of the current page */
static int build_rows(const Frontend *fe, char buf[][48], const char **names)
{
    const FeOptions *o = &fe->opt;
    const Settings *s = fe->set;
    int n = page_rows(fe);
    if (n > MAXROWS) n = MAXROWS;
    for (int i = 0; i < n; i++) {
        buf[i][0] = 0;
        switch (o->page) {
        case OPT_ROOT:
            snprintf(buf[i], 48, "%s", ROOT_NAME[root_id(o, i)]);
            break;
        case OPT_CONTROLS:
            if (i == PRESET_CUSTOM) snprintf(buf[i], 48, "Custom...%s", s->preset == i ? "  (on)" : "");
            else snprintf(buf[i], 48, "%s%s", settings_preset_name(i), s->preset == i ? "  (on)" : "");
            break;
        case OPT_MAP:
            if (i == ACT_COUNT) snprintf(buf[i], 48, "Reset to default");
            else if (o->capture == i) snprintf(buf[i], 48, "%s:  press a button", SHORT_ACTION[i]);
            else snprintf(buf[i], 48, "%s:  %s", SHORT_ACTION[i], settings_button_name(s->map[i]));
            break;
        case OPT_GUIDE:
            snprintf(buf[i], 48, "%s", CAT_NAME[i]);
            break;
        case OPT_GUIDE_LIST:
            snprintf(buf[i], 48, "%s", guide_name(fe, guide_entry(o->cat, i)));
            break;
        case OPT_GAME:
            if (i == GAME_STANCE) snprintf(buf[i], 48, "Stance:  %s", settings_stance_name(s->stance));
            else if (i == GAME_UNITS) snprintf(buf[i], 48, "Units:  %s", settings_units_name(s->units));
            else if (i == GAME_SFX) snprintf(buf[i], 48, "Sound effects:  %d%%", s->sfx_vol * 100 / VOL_MAX);
            else snprintf(buf[i], 48, "Music:  %d%%", s->music_vol * 100 / VOL_MAX);
            break;
        case OPT_GRAVITY:
            if (i == TUNE_COUNT) snprintf(buf[i], 48, "Reset to 100");
            else snprintf(buf[i], 48, "%s  %d%%", settings_tune_name(i), s->tune[i]);
            break;
        default:
            if (i < INFO_COUNT) snprintf(buf[i], 48, "%s  %s", settings_info_name(i), s->info[i] ? "ON" : "OFF");
            else snprintf(buf[i], 48, "%s", i == INFO_COUNT ? "Show all" : "Hide all");
            break;
        }
        names[i] = buf[i];
    }
    return n;
}

void fe_options_open_page(Frontend *fe, int page)
{
    fe_options_open(fe);
    fe->opt.page = page;
    fe->opt.entry_page = page;
}

const char *fe_trick_how(const char *name)
{
    for (int i = 0; i < NGUIDE; i++)
        if (strcasecmp(GUIDE[i].name, name) == 0) return GUIDE[i].how;
    return NULL;
}

void fe_options_open(Frontend *fe)
{
    memset(&fe->opt, 0, sizeof(fe->opt));
    fe->opt.capture = -1;
    if (fe->set)
        fe->opt.sel[OPT_CONTROLS] = fe->set->preset;       /* the list starts on the preset in use */
}

int fe_wrap_text(Frontend *fe, const char *text, float maxw, char lines[][96], int max_lines)
{
    int n = 0;
    char cur[96] = "";
    const char *p = text;
    while (*p && n < max_lines) {
        char word[40];
        int wl = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && wl < 39) word[wl++] = *p++;
        word[wl] = 0;
        if (!wl) break;
        char trial[160];
        snprintf(trial, sizeof(trial), "%s%s%s", cur, cur[0] ? " " : "", word);
        if (cur[0] && ui_text_width(fe->ui, fe->f_s, 1.0f, trial) > maxw) {
            snprintf(lines[n++], 96, "%s", cur);
            snprintf(cur, sizeof(cur), "%s", word);
        } else {
            snprintf(cur, sizeof(cur), "%.95s", trial);
        }
    }
    if (cur[0] && n < max_lines)
        snprintf(lines[n++], 96, "%s", cur);
    return n;
}

/* the left panel: a mapping table (a preset's, or the current one) */
static void panel_table(Frontend *fe, const char *title, const uint8_t *map, int hilite)
{
    Ui *ui = fe->ui;
    float x0 = COL_L + 10.0f, x1 = COL_L + LEFT_W - 10.0f, y0 = LEFT_Y + 7.0f;
    ui_text_shadow(ui, fe->f_m, x0, y0, 1.0f, title, COL_YELLOW);
    for (int a = 0; a < ACT_COUNT; a++) {
        float y = y0 + 25.0f + (float)a * 17.0f;
        if (a == hilite)
            ui_rect(ui, COL_L + 6.0f, y - 1.0f, LEFT_W - 12.0f, 16.0f, UI_RGBA(255, 255, 255, 45));
        ui_text_shadow(ui, fe->f_s, x0, y, 1.0f, settings_action_name(a), UI_WHITE);
        ui_text_right(ui, fe->f_s, x1, y, 1.0f, settings_button_name(map[a]), COL_YELLOW);
    }
}

/* the left panel: a few lines of explanation */
void fe_panel_text(Frontend *fe, const char *title, const char *text)
{
    Ui *ui = fe->ui;
    char lines[8][96];
    ui_text_shadow(ui, fe->f_m, COL_L + 10.0f, LEFT_Y + 7.0f, 1.0f, title, COL_YELLOW);
    int n = fe_wrap_text(fe, text, LEFT_W - 24.0f, lines, 7);
    for (int i = 0; i < n; i++)
        ui_text_shadow(ui, fe->f_s, COL_L + 10.0f, LEFT_Y + 33.0f + (float)i * 16.0f, 1.0f, lines[i], UI_WHITE);
}

/* the left panel: a guide text, with the buttons of the mapping filled in */
static void panel_guide(Frontend *fe, const char *title, const char *tmpl)
{
    char text[256];
    guide_text(fe->set, tmpl, text, sizeof(text));
    fe_panel_text(fe, title, text);
}

void fe_options_draw(Frontend *fe, int overlay)
{
    if (!fe->set)
        return;
    Ui *ui = fe->ui;
    const FeOptions *o = &fe->opt;
    const Settings *s = fe->set;
    char buf[MAXROWS][48];
    const char *names[MAXROWS];
    ui_reset_view(ui);
    if (overlay)
        ui_rect(ui, 0, 0, UI_W, UI_H, UI_RGBA(0, 0, 0, 185));
    fe_title(fe, PAGE_TITLE[o->page]);
    int n = build_rows(fe, buf, names);
    int sel = o->sel[o->page];
    fe_list(fe, names, n, sel, COL_R, LEFT_Y - 6, BAR_X);
    fe_frame(fe, COL_L + 3, LEFT_Y + 3, LEFT_W - 6, LEFT_H - 6);
    switch (o->page) {
    case OPT_ROOT:
        { int id = root_id(o, sel);
        if (id == ROOT_CONTROLS) fe_panel_text(fe, "Controls", "Choose a control preset, or set every button yourself.");
        else if (id == ROOT_CREDITS) fe_panel_text(fe, "Credits", "Who made the game.");
        else if (id == ROOT_GUIDE) fe_panel_text(fe, "Trick guide", "Every trick in the game and how to do it, on the buttons you have chosen.");
        else if (id == ROOT_GRAVITY) fe_panel_text(fe, "Gravity", "Tune the physics: gravity, the power of the ollie and the speed of a push.");
        else if (id == ROOT_INFO) fe_panel_text(fe, "Information", "Choose which information lines show while you skate: FPS, speed, timings, memory and more.");
        else if (id == ROOT_GAME) fe_panel_text(fe, "Game settings", "Your stance, the units of speeds and distances, the sound volumes.");
        else fe_panel_text(fe, "Back", "Back to the menu. Your changes are saved."); }
        fe_hint(fe, "UP / DOWN  CHOOSE       X  OK       O  BACK");
        break;
    case OPT_CONTROLS:
        if (sel < PRESET_CUSTOM) panel_table(fe, settings_preset_name(sel), settings_preset_map(sel), -1);
        else panel_table(fe, "Custom", s->map, -1);
        fe_hint(fe, sel < PRESET_CUSTOM ? "UP / DOWN  CHOOSE       X  USE THIS PRESET       O  BACK" : "UP / DOWN  CHOOSE       X  SET THE BUTTONS       O  BACK");
        break;
    case OPT_MAP:
        if (o->capture >= 0) {
            char t[160];
            snprintf(t, sizeof(t), "Press the button for %s. Another action that has it gets this action's old button.", settings_action_name(o->capture));
            fe_panel_text(fe, "Custom", t);
            fe_hint(fe, "PRESS A BUTTON  (X  O  SQUARE  TRIANGLE  L  R)       START  CANCEL");
        } else if (sel < ACT_COUNT) {
            panel_table(fe, "Custom", s->map, sel);
            fe_hint(fe, "UP / DOWN  CHOOSE       X  CHANGE THE BUTTON       O  BACK");
        } else {
            panel_table(fe, "Custom", s->map, -1);
            fe_hint(fe, "UP / DOWN  CHOOSE       X  RESET TO DEFAULT       O  BACK");
        }
        break;
    case OPT_GUIDE:
        panel_guide(fe, CAT_NAME[sel], CAT_HELP[sel]);
        fe_hint(fe, "UP / DOWN  CHOOSE       X  OPEN       O  BACK");
        break;
    case OPT_GUIDE_LIST: {
        const GuideEntry *g = guide_entry(o->cat, sel);
        panel_guide(fe, guide_name(fe, g), guide_how(fe, g));
        fe_hint(fe, "UP / DOWN  NEXT TRICK       O  BACK");
        break;
    }
    case OPT_GAME:
        if (sel == GAME_STANCE) fe_panel_text(fe, "Stance", settings_stance_help());
        else if (sel == GAME_UNITS) fe_panel_text(fe, "Units", "Speeds and distances in metres or in feet (km/h or mph).");
        else if (sel == GAME_SFX) fe_panel_text(fe, "Sound effects", "How loud the board, the grinds, the landings and the menu sounds are.");
        else fe_panel_text(fe, "Music", "How loud the music is.");
        fe_hint(fe, "UP / DOWN  CHOOSE       LEFT / RIGHT  CHANGE       O  BACK");
        break;
    case OPT_GRAVITY:
        if (sel < TUNE_COUNT) fe_panel_text(fe, settings_tune_name(sel), settings_tune_help(sel));
        else fe_panel_text(fe, "Reset to 100", "Everything back to the designed physics.");
        fe_hint(fe, sel < TUNE_COUNT ? "UP / DOWN  CHOOSE       LEFT / RIGHT  CHANGE       O  BACK" : "UP / DOWN  CHOOSE       X  RESET       O  BACK");
        break;
    default:
        if (sel < INFO_COUNT) fe_panel_text(fe, settings_info_name(sel), settings_info_help(sel));
        else fe_panel_text(fe, sel == INFO_COUNT ? "Show all" : "Hide all", sel == INFO_COUNT ? "Turn every information line on." : "Turn every information line off.");
        fe_hint(fe, "UP / DOWN  CHOOSE       X  ON / OFF       O  BACK");
        break;
    }
}

/* the physical button of a pressed FE_* bit, -1 for none */
static int pressed_button(unsigned pressed)
{
    if (pressed & FE_OK) return PB_CROSS;
    if (pressed & FE_BACK) return PB_CIRCLE;
    if (pressed & FE_SQUARE) return PB_SQUARE;
    if (pressed & FE_TRIANGLE) return PB_TRIANGLE;
    if (pressed & FE_L) return PB_L;
    if (pressed & FE_R) return PB_R;
    return -1;
}

/* one step of a Game settings row: the stance and the units cycle, a volume moves a tenth (a press of OK wraps round from full to silent, the arrows stop at the ends) */
static void game_step(Settings *s, int row, int dir, int wrap)
{
    switch (row) {
    case GAME_STANCE: s->stance = (uint8_t)((s->stance + STANCE_COUNT + dir) % STANCE_COUNT); break;
    case GAME_UNITS: s->units = (uint8_t)((s->units + UNITS_COUNT + dir) % UNITS_COUNT); break;
    default: {
        uint8_t *v = row == GAME_SFX ? &s->sfx_vol : &s->music_vol;
        int n = (int)*v + dir;
        if (n > VOL_MAX) n = wrap ? 0 : VOL_MAX;
        if (n < 0) n = wrap ? VOL_MAX : 0;
        *v = (uint8_t)n;
        break;
    }
    }
}

int fe_options_update(Frontend *fe, const FeInput *in, float dt)
{
    FeOptions *o = &fe->opt;
    Settings *s = fe->set;
    if (!s)
        return 1;
    if (o->capture >= 0) {                                 /* waiting for the button of an action (the nub does not count: only the six buttons can be given) */
        if (in->pressed & FE_START) {
            o->capture = -1; fe->ev_back = 1;
        } else {
            int b = pressed_button(in->pressed);
            if (b >= 0) {
                settings_assign(s, o->capture, b);
                o->capture = -1; o->changed = 1; fe->ev_ok = 1;
            }
        }
        return 0;
    }
    int n = page_rows(fe);
    int d = fe_nav_step(fe, in, dt, 0);
    if (d) {
        o->sel[o->page] = (o->sel[o->page] + d + n) % n;
        fe->ev_move = 1;
    }
    int sel = o->sel[o->page];
    if (in->pressed & FE_OK) {
        fe->ev_ok = 1;
        switch (o->page) {
        case OPT_ROOT:
            { int id = root_id(o, sel);
            if (id == ROOT_CONTROLS) o->page = OPT_CONTROLS;
            else if (id == ROOT_GUIDE) o->page = OPT_GUIDE;
            else if (id == ROOT_GRAVITY) o->page = OPT_GRAVITY;
            else if (id == ROOT_INFO) o->page = OPT_INFO;
            else if (id == ROOT_GAME) o->page = OPT_GAME;
            else if (id == ROOT_CREDITS) return 2;
            else { fe->ev_ok = 0; fe->ev_back = 1; return 1; } }
            break;
        case OPT_CONTROLS:
            if (sel < PRESET_CUSTOM) { settings_apply_preset(s, sel); o->changed = 1; }
            else o->page = OPT_MAP;
            break;
        case OPT_MAP:
            if (sel < ACT_COUNT) o->capture = sel;
            else { settings_apply_preset(s, PRESET_DEFAULT); o->changed = 1; }
            break;
        case OPT_GUIDE:
            o->cat = sel; o->sel[OPT_GUIDE_LIST] = 0; o->page = OPT_GUIDE_LIST;
            break;
        case OPT_GUIDE_LIST:
            fe->ev_ok = 0;                                 /* (nothing to choose: the panel already shows the trick) */
            break;
        case OPT_GAME:
            game_step(s, sel, 1, 1);
            o->changed = 1;
            break;
        case OPT_GRAVITY:
            if (sel < TUNE_COUNT) settings_tune_step(s, sel, 1);
            else for (int t = 0; t < TUNE_COUNT; t++) s->tune[t] = 100;
            o->changed = 1;
            break;
        default:
            if (sel < INFO_COUNT) s->info[sel] = !s->info[sel];
            else for (int i = 0; i < INFO_COUNT; i++) s->info[i] = sel == INFO_COUNT;
            o->changed = 1;
            break;
        }
    } else if (o->page == OPT_GAME && (in->pressed & (FE_LEFT | FE_RIGHT))) {
        game_step(s, sel, (in->pressed & FE_RIGHT) ? 1 : -1, 0);
        o->changed = 1; fe->ev_move = 1;
    } else if (o->page == OPT_INFO && sel < INFO_COUNT && (in->pressed & (FE_LEFT | FE_RIGHT))) {
        s->info[sel] = !s->info[sel];
        o->changed = 1; fe->ev_move = 1;
    } else if (o->page == OPT_GRAVITY && sel < TUNE_COUNT && (in->pressed & (FE_LEFT | FE_RIGHT))) {
        int before = s->tune[sel];
        settings_tune_step(s, sel, (in->pressed & FE_RIGHT) ? 1 : -1);
        if (s->tune[sel] != before) { o->changed = 1; fe->ev_move = 1; }
    } else if (in->pressed & FE_BACK) {
        fe->ev_back = 1;
        if (o->page == OPT_ROOT || o->page == o->entry_page) return 1;
        o->page = o->page == OPT_MAP ? OPT_CONTROLS : (o->page == OPT_GUIDE_LIST ? OPT_GUIDE : OPT_ROOT);
    }
    return 0;
}
