/* The front end: title screen, main menu, skater select, location select. No PSP dependencies (host-tested; tests/fe_preview.c renders every screen to a picture): it draws through src/ui.h
 * and is driven by button presses. The caller (src/main.c) draws the 3D skater preview between fe_draw_back() and fe_draw_front(), starts the game when fe_update() reports a launch,
 * and plays the menu sounds from the ev_* flags.
 * Layout (480 x 272): one picture (the dimmed skyline) behind every screen; text lists with the big arrow at the chosen entry; the main menu has the logo picture in its frame at the left,
 * the location list has the place's picture at the right, the skater list has the 3D skater in a small frame at the left (fe->preview). */
#ifndef FRONTEND_H
#define FRONTEND_H

#include "career.h"
#include "progress.h"
#include "settings.h"
#include "story.h"
#include "ui.h"

enum { FE_UP = 1, FE_DOWN = 2, FE_LEFT = 4, FE_RIGHT = 8, FE_OK = 16, FE_BACK = 32, FE_START = 64, FE_L = 128, FE_R = 256, FE_SQUARE = 512, FE_TRIANGLE = 1024 };

typedef struct {
    unsigned pressed;               /* FE_* bits that went down this frame */
    unsigned held;                  /* FE_* bits that are down (up / down repeat while held) */
} FeInput;

enum { FE_STATE_TITLE = 0, FE_STATE_MAIN, FE_STATE_SKATER, FE_STATE_LOCATION, FE_STATE_LAUNCH, FE_STATE_OPTIONS, FE_STATE_TUTORIAL, FE_STATE_CMAP_AREA, FE_STATE_CMAP_LOC, FE_STATE_CMAP_CHAL, FE_STATE_STORY, FE_STATE_CREDITS, FE_STATE_SPONSORS, FE_STATE_MYSKATE, FE_STATE_PROFILES, FE_STATE_LOGBOOK, FE_STATE_LOGPAGE, FE_STATE_CUSTOM };
enum { FE_MODE_FREESKATE = 0, FE_MODE_CAREER = 1, FE_MODE_TUTORIAL = 2 };

#define FE_MAX_ITEMS 24
#define FE_MAX_CREDITS 256
#define FE_DONE_SIZE 128                /* the career's done flags: one byte per challenge of the original's table (data/career.sav) */
#define FE_NMAIN 7
/* the layout (480 x 272): the left column (picture, skater viewer, an options panel: COL_L, LEFT_W x LEFT_H from LEFT_Y), the right column (the text lists: COL_R, scroll bar at BAR_X) */
#define COL_L 18
#define LEFT_W 192
#define LEFT_H 144
#define LEFT_Y 62
#define COL_R 250
#define BAR_X 462                       /* the scroll bar of a long list */

#define COL_YELLOW UI_RGBA(255, 214, 40, 255)

/* the options screens (src/options.c): used by the main menu and, over the paused game, by the pause menu. Root: Controls (presets / the custom mapping), Trick guide (the categories, then
 * the tricks of one with how to do each on the current mapping), Gravity (the physics tuning), Information (the HUD lines), Game settings (stance, units, sound volumes). */
enum { OPT_ROOT = 0, OPT_CONTROLS, OPT_MAP, OPT_GUIDE, OPT_GUIDE_LIST, OPT_GRAVITY, OPT_INFO, OPT_GAME, OPT_NPAGES };
typedef struct {
    int page;
    int sel[OPT_NPAGES];
    int capture;                    /* the action whose button is being chosen (OPT_MAP page), -1 none */
    int cat;                        /* the trick guide category open on the OPT_GUIDE_LIST page */
    int changed;                    /* a setting was changed: the caller saves the settings file and clears this */
    int entry_page;                 /* the page the options were opened on (fe_options_open_page): Circle there leaves them */
    int no_credits;                 /* the root page has no Credits entry (the options over the pause menu: the caller sets this after fe_options_open) */
} FeOptions;

/* a saved profile slot as the Select Profile screen shows it (the caller fills them: fe->need_slots) */
typedef struct { int used, skater, done, sponsors; unsigned play; } FeSlot;

typedef struct { int index; char name[32]; char key[24]; int ok; int chals; int pic; int level_id; int area; } FeItem;       /* a skater or a location in a list */

typedef struct {
    Ui *ui;
    const Career *career;
    int state;
    float t;                        /* seconds in the state */
    int mode;                       /* FE_MODE_* picked in the main menu */
    int main_sel, skater_sel, loc_sel;         /* main_sel: 0 start career .. 5 my spot; skater_sel / loc_sel: indices into the lists below */
    int area_sel;                   /* the area of the chosen location (CareerLevel.area); the location list is grouped by area */
    FeItem skater[FE_MAX_ITEMS]; int nskater;  /* the skaters whose model file exists */
    FeItem loc[FE_MAX_ITEMS]; int nloc;        /* the locations whose level file exists */
    float rep_t; unsigned rep_dir;  /* auto-repeat of up / down */
    float spin;                     /* the skater preview turns */
    float flash;
    int ev_move, ev_ok, ev_back;    /* set by fe_update for the sound effects; the caller clears them */
    int preview[4];                 /* x, y, w, h on the screen where the caller draws the 3D skater */
    /* fonts and pictures */
    int f_xl, f_l, f_m, f_s, f_tiny;
    int i_title, i_dim, i_locbox, i_arrow;
    Settings *set;                  /* the player's settings (set by the caller; the options screens change them) */
    FeOptions opt;
    const unsigned char *done;      /* the career's done flags (FE_DONE_SIZE bytes, 1 = done), set by the caller; NULL = none */
    int tut[12], ntut, tut_sel;     /* the tutorials (career challenge indices, in order) and the one chosen; at a launch fe->tut_sel names the tutorial and fe->loc_sel its place */
    const unsigned char *mark;      /* fe_list: one byte per row: FE_MARK_DONE (green box) / FE_MARK_OPEN (orange box); the caller sets it before fe_list and fe_list clears it */
    /* the career: the profile (set with fe_set_profile; the front end changes its skater and, for the temporary skip, completes events), and the map */
    Profile *prof;
    FeItem skater_all[FE_MAX_ITEMS]; int nskater_all;          /* every rider with a file; fe->skater is the part of it that can be chosen now (the unlocked ones in the career) */
    int skater_back;                /* the state the skater screen returns to */
    int cm_area;                    /* the area chosen on the map (CareerLevel.area) */
    int cm_areas[8], ncm_areas, cm_area_sel;                   /* the areas that have a place with a level file, and the chosen one in that list */
    int cm_locs[FE_MAX_ITEMS], ncm_locs, cm_loc_sel;           /* the places of the area (indices into loc[]) and the chosen one */
    int cm_chals[16], ncm_chals, cm_chal_sel;                  /* the career events of the place (challenge indices, slot order) and the chosen one */
    int launch_chal;                /* a career launch: the challenge to begin at once (-1 = none) */
    int ev_profile;                 /* the profile was changed (skater chosen, a story item shown): the caller saves it and clears this */
    /* the story screens (src/storyui.c) */
    StoryItem story;                /* the item on screen (kind STORY_NONE = none) */
    StoryPage pages[STORY_MAX_PAGES]; int npages, page;       /* a scene's pages; the ending has two */
    int story_return;               /* the state to go back to when nothing more is due */
    int story_launch;               /* the event whose intro scene is on screen: launched when it is read (-1 = none) */
    int scene3d;                    /* the caller draws the scene's 3D view (the guest in the level) behind the pages: the dim picture is not drawn and the text panel moves down (set by the caller) */
    int scene_flip;                 /* SQUARE turns the guest round (the way he faces the camera is not certain, see src/scene.h); the caller hands it to the scene player */
    float credits_y; int credits_return;                       /* the scroll position and where to go back to */
    float cr_y[FE_MAX_CREDITS], cr_h; unsigned char cr_rows[FE_MAX_CREDITS];    /* the credits' layout */
    int sp_sel;                     /* the sponsors page */
    /* My Skate (src/logbook.c) */
    const CustomPak *cpak;          /* the custom skater's parts while the Customize screen is open (the caller loads and frees them: NULL = not available) */
    CustomSel cust;                 /* the choice being made */
    int cust_sel, cust_dirty;       /* the row; the choice changed: the caller rebuilds the skater in the viewer and clears this */
    int ms_sel, lb_sel, lb_page, lb_scroll, tb_sel;           /* the menu row, the Log Book row, the page open, its scroll position, the chosen trick / skater */
    FeSlot slot[PROFILE_SLOTS]; int cur_slot, pf_sel, pf_confirm;     /* the profile slots; the one in use; the one chosen; "clear it?" */
    int need_slots;                 /* the profile screen was opened: the caller refreshes slot[] and clears this */
    int req_slot, req_delete;       /* requests for the caller: use (or start) this slot / clear this slot (-1 = none; the caller sets cur_slot, refreshes slot[] and clears them) */
    int opt_return;                 /* the state to go back to when the options are left (the main menu, or My Skate for its Game settings) */
    int regular;                    /* the rider rides regular now (the caller keeps it up to date: the trick guide's backside / frontside follow it) */
    int have_music;                 /* the music is in the game: the Game settings page has a music volume (the caller sets it) */
} Frontend;
#define FE_MARK_DONE 1
#define FE_MARK_OPEN 2

/* skater_ok[i] / level_ok[id]: whether the file of that skater (career skater index) / level (level table id) exists. Items are built from the career data. */
void fe_init(Frontend *fe, Ui *ui, const Career *c, const unsigned char *skater_ok, const unsigned char *level_ok);
/* returns 1 when a game is to be started: fe->mode, fe->skater[fe->skater_sel], fe->loc[fe->loc_sel] */
int fe_update(Frontend *fe, const FeInput *in, float dt);
void fe_draw_back(Frontend *fe);      /* backgrounds */
void fe_draw_front(Frontend *fe);     /* everything that goes over the 3D preview */
void fe_back_to_menu(Frontend *fe);   /* after a game: the location list again */
/* building blocks of the screens (also used by src/options.c) */
int fe_nav_step(Frontend *fe, const FeInput *in, float dt, int allow_lr);          /* -1 / +1 from a press or the auto-repeat of a held up / down (left / right too when allow_lr), else 0 */
void fe_list(Frontend *fe, const char *const *names, int n, int sel, float x, float y0, float bar_x);   /* the arrow list of the right column */
void fe_frame(Frontend *fe, int x, int y, int w, int h);                           /* a dark panel with a white frame around the inner rectangle */
void fe_hint(Frontend *fe, const char *text);                                      /* the button hints at the bottom */
int fe_wrap_text(Frontend *fe, const char *text, float maxw, char lines[][96], int max_lines);   /* word-wrap in the small font; returns the number of lines */
void fe_panel_text(Frontend *fe, const char *title, const char *text);             /* the left panel: a yellow title and the wrapped text */
/* the profile the career uses (done flags for the tutorial ticks, unlocks, progress); must be set before the career is entered */
void fe_set_profile(Frontend *fe, Profile *prof);
/* an event of the career map as the list shows it */
enum { FE_EV_LOCKED = 0, FE_EV_OPEN, FE_EV_DONE, FE_EV_CLOSED };
int fe_event_status(const Frontend *fe, int chal);                 /* FE_EV_*: not unlocked yet, open, done, or unlocked but its pool has passed */
/* the story (src/storyui.c): after the profile changed (entering the map, back from an event) fe_story_begin shows what is due and returns 1 if it started something (the state is then
 * FE_STATE_STORY / FE_STATE_CREDITS; when it has all been read the front end goes on to return_state); fe_story_intro does the same for the scene before an event and, once it is read,
 * launches the event (fe_story_update then returns 1) */
int fe_story_begin(Frontend *fe, int return_state);
int fe_story_intro(Frontend *fe, int chal);
int fe_story_update(Frontend *fe, const FeInput *in, float dt);
void fe_story_draw(Frontend *fe);
void fe_credits_open(Frontend *fe, int return_state);
int fe_credits_update(Frontend *fe, const FeInput *in, float dt);
void fe_credits_draw(Frontend *fe);
void fe_sponsors_open(Frontend *fe);
int fe_sponsors_update(Frontend *fe, const FeInput *in, float dt);
void fe_sponsors_draw(Frontend *fe);
/* the career event is started (the state becomes FE_STATE_LAUNCH): returns 1 */
int fe_launch_career_event(Frontend *fe, int chal);
/* the chosen tutorial's challenge index (-1 if there are none) */
int fe_tutorial_chal(const Frontend *fe);
/* after a tutorial: back to the tutorial list, on the next one that is not done when `advance` */
void fe_back_to_tutorials(Frontend *fe, int advance);
void fe_title(Frontend *fe, const char *text);                                     /* the yellow title at the top left */
/* My Skate (src/logbook.c): the menu, the profile slots, the Log Book */
void fe_book_open(Frontend *fe, int state);
int fe_book_update(Frontend *fe, const FeInput *in, float dt);
void fe_book_draw(Frontend *fe);
/* the options opened on one page (Circle there leaves them) */
void fe_options_open_page(Frontend *fe, int page);
/* the how-to text of a trick of the trick guide by its name ("Kickflip", case does not matter), NULL if there is none; with {POP} .. {GR} buttons in it */
const char *fe_trick_how(const char *name);
/* the options: open at the root page; update returns 1 when the player left the options (Circle on the root page), 2 when the Credits entry was chosen; draw over the background (overlay 0) or dimming what is on screen (overlay 1) */
void fe_options_open(Frontend *fe);
int fe_options_update(Frontend *fe, const FeInput *in, float dt);
void fe_options_draw(Frontend *fe, int overlay);
/* a text menu over whatever was drawn before (the pause menu): a dimmed screen, the items in the main menu's style with the arrow at the selected one (enabled[i] == 0 greys it; NULL = all on) */
void fe_draw_menu(Frontend *fe, const char *title, const char *const *items, const unsigned char *enabled, int n, int sel);

#endif
