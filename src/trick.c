#include "trick.h"
#include <stdio.h>
#include <string.h>

TrickTable tricks;

static void set_def(GrindType *g, const char *name, int pose, int base, int rate, float diff, int board)
{
    strncpy(g->name, name, sizeof(g->name) - 1);
    g->name[sizeof(g->name) - 1] = 0;
    g->pose = pose; g->base = base; g->rate = rate; g->diff = diff; g->board = board; g->alt = -1; g->tp = g->tr = g->ty = 0.0f;
}

static void set_grind(int slot, const char *name, int pose, int base, int rate, float diff, int board, float tp, float tr, float ty)
{
    set_def(&tricks.grind[slot], name, pose, base, rate, diff, board);
    tricks.grind[slot].tp = tp; tricks.grind[slot].tr = tr; tricks.grind[slot].ty = ty;
}

static void set_grab(int slot, const char *name, int pose, int base, int rate)
{
    set_def(&tricks.grab[slot], name, pose, base, rate, 1.0f, 0);
}

static void set_flip(int slot, const char *name, int pose, int alt, int base, int ms)
{
    set_def(&tricks.flip[slot], name, pose, base, ms, 1.0f, 0);
    tricks.flip[slot].alt = alt;
}

const GrindType *trick_def(int cls, int type)
{
    switch (cls) {
    case TRICK_MANUAL: return &tricks.manual[type >= 0 && type < NMANUAL_TYPES ? type : 0];
    case TRICK_FLIP:   return &tricks.flip[type >= 0 && type < NFLIP_TYPES ? type : 0];
    case TRICK_GRAB:   return &tricks.grab[type >= 0 && type < NGRAB_TYPES ? type : 0];
    case TRICK_MOVE:   return &tricks.move[type >= 0 && type < NMOVE_TYPES ? type : 0];
    default:           return &tricks.grind[type >= 0 && type < NGRIND_TYPES ? type : 0];
    }
}

void tricks_defaults(void)
{
    /* Grinds: the clips do not say which truck is on the rail (the board sits at a fixed origin, only its yaw / a little pitch and roll move), so every type is one of the clip FAMILIES found by
     * tools/grind_classify.py plus a board tilt: ride stance (0) = 50-50; slides across the rail: 62 / 67 (the two turn senses) and their crouch versions 72 / 77; nose-weighted (the skater's
     * weight over the front truck, b low byte 1): 94 / 97 / 100; tail-weighted: 103 / 106 / 109. Which name goes with which pose beyond the family is a convention (the table is data, the picker
     * and tricks.cfg change it). Columns: name, pose, points, per second, balance difficulty, sound on the board (slides) / trucks, tilt (pitch, roll, yaw degrees). */
    set_grind(GRIND_5050,       "50-50",        0,  50, 100, 1.00f, 0,   0,   0,   0);
    set_grind(GRIND_50,         "5-0",        103,  75, 125, 1.10f, 0,  10,   0,   0);
    set_grind(GRIND_NOSEGRIND,  "NOSEGRIND",   94,  75, 125, 1.15f, 0, -10,   0,   0);
    set_grind(GRIND_BOARDSLIDE, "BOARDSLIDE",  62,  90, 125, 1.20f, 1,   0,   0,   0);
    set_grind(GRIND_LIPSLIDE,   "LIPSLIDE",    67,  90, 125, 1.20f, 1,   0,   0,   0);
    set_grind(GRIND_NOSESLIDE,  "NOSESLIDE",  100, 100, 140, 1.30f, 1, -22,   0,   0);
    set_grind(GRIND_TAILSLIDE,  "TAILSLIDE",  106, 100, 140, 1.30f, 1,  22,   0,   0);
    set_grind(GRIND_SMITH,      "SMITH",      109, 110, 140, 1.35f, 0,   6,  14,   0);
    set_grind(GRIND_FEEBLE,     "FEEBLE",     103, 110, 140, 1.35f, 0,   6, -14,   0);
    set_grind(GRIND_CROOKED,    "CROOKED",     97, 120, 150, 1.40f, 0,  -8,  14,   0);
    set_grind(GRIND_OVERCROOKS, "OVERCROOKS", 100, 130, 150, 1.45f, 0,  -8, -14,  20);
    set_grind(GRIND_WILLY,      "WILLY",       94, 120, 150, 1.40f, 0,  -6,   0, -14);
    set_grind(GRIND_OVERWILLY,  "OVERWILLY",   97, 130, 150, 1.45f, 0,  -6,  10, -22);
    set_grind(GRIND_SALAD,      "SALAD",      106, 120, 150, 1.40f, 0,   6,   0,  14);
    set_grind(GRIND_BLUNT,      "BLUNT",       72, 130, 150, 1.40f, 1,  16,   0,   0);
    set_grind(GRIND_NOSEBLUNT,  "NOSEBLUNT",   77, 130, 150, 1.40f, 1, -16,   0,   0);
    set_def(&tricks.manual[0], "MANUAL",      -1, 25,  80, 1.00f, 0);
    set_def(&tricks.manual[1], "NOSE MANUAL", -1, 40, 100, 1.20f, 0);
    /* Flips = clips 315-362 of the pose library. How they were identified (tools/flip_measure.py, tools/foot_frames.py, animdb.adb): the database labels every clip with a trick TYPE;
     * group 33 holds the 17 types of the tricks done from the ground, group 34 the same types as NOLLIES (the board turns the other way about the vertical), group 35 six types in two
     * start poses each (crouched = from the ground, upright = LATE, done in the air). The board's measured turn - the heading of its nose about the vertical (AZ) and its roll about its
     * own long axis (ROLL) - is a clean multiple of 180 / 360 degrees, which gives each type its name by definition:
     *   ROLL +360: both feet swing out to the board's +x edge = the TOE side (the riding pose's toes point to +x) = a KICKFLIP; ROLL -360 = HEELFLIP;
     *   AZ +180 / +360 = backside (for the goofy-footed skater a backside spin turns counter-clockwise seen from above) = Pop Shuvit; AZ -180 / -360 = frontside.
     * group 33 types: 3 = AZ +177 pop shuvit, 4 = -178 FS pop shuvit, 5 / 6 = +-360 pop shuvits, 7 = kick + BS180 = Varial Kickflip, 8 = heel + BS180 = Inward Heel, 9 = kick + FS180 = Hardflip,
     * 10 = heel + FS180 = Varial Heelflip, 11 = kick + BS360 = 360 Flip, 12 = heel + BS360 = 360 Inward Heel, 13 = kick + FS360 = 360 Hardflip, 14 = heel + FS360 = Laser Flip.
     * Types 1 / 2 (clips 325 / 322, roll +-1440: the board rolls four times) and 0 / 15 / 16 (no turn) are left unused; the plain kickflip / heelflip use the clean single-roll group 35 clips.
     * The two global signs (kick = +, backside = +) are the only conventions assumed; the picker / tricks.cfg can swap any pair. rate = duration in ms. */
    set_flip(FLIP_KICKFLIP,        "KICKFLIP",           357, 353, 150, 480);
    set_flip(FLIP_HEELFLIP,        "HEELFLIP",           355, 351, 150, 480);
    set_flip(FLIP_POP_SHUVIT,      "POP SHUVIT",         359, 348, 100, 450);
    set_flip(FLIP_FS_POP_SHUVIT,   "FS POP SHUVIT",      360, 340, 100, 450);
    set_flip(FLIP_360_SHUVIT,      "360 POP SHUVIT",     318, 338, 200, 600);
    set_flip(FLIP_FS_360_SHUVIT,   "FS 360 POP SHUVIT",  319, 339, 200, 600);
    set_flip(FLIP_VARIAL_KICK,     "VARIAL KICKFLIP",    333, 350, 250, 600);
    set_flip(FLIP_VARIAL_HEEL,     "VARIAL HEELFLIP",    332, 349, 250, 600);
    set_flip(FLIP_HARDFLIP,        "HARDFLIP",           321, 341, 250, 600);
    set_flip(FLIP_INWARD_HEEL,     "INWARD HEEL",        324, 344, 250, 600);
    set_flip(FLIP_LASER,           "LASER FLIP",         327, 347, 400, 700);
    set_flip(FLIP_360_FLIP,        "360 FLIP",           315, 335, 400, 700);
    set_flip(FLIP_360_HARDFLIP,    "360 HARDFLIP",       316, 336, 400, 700);
    set_flip(FLIP_360_INWARD,      "360 INWARD HEEL",    317, 337, 400, 700);
    set_flip(FLIP_BACKFLIP,        "BACKFLIP",           -1,  -1, 300, 800);        /* no clip turns the skater himself: the whole rider is rolled */
    set_flip(FLIP_FRONTFLIP,       "FRONTFLIP",          -1,  -1, 300, 800);
    set_flip(FLIP_LATE_KICK,       "LATE KICKFLIP",      358,  -1, 120, 450);
    set_flip(FLIP_LATE_HEEL,       "LATE HEELFLIP",      356,  -1, 120, 450);
    set_flip(FLIP_LATE_SHUVIT,     "LATE POP SHUVIT",    361,  -1,  80, 420);
    set_flip(FLIP_LATE_FS_SHUVIT,  "LATE FS POP SHUVIT", 362,  -1,  80, 420);
    set_flip(FLIP_LATE_BF_KICK,    "LATE BACKFOOT KICKFLIP", 354, -1, 140, 450);
    set_flip(FLIP_LATE_BF_HEEL,    "LATE BACKFOOT HEELFLIP", 352, -1, 140, 450);
    /* Grabs (tools/grab_classify.py): the animation database makes one GROUP of clips per grab family - which hand is on which edge: g15 front hand on the heel edge = Melon, g16 back hand on the
     * toe edge = Indy ("BS Grab" / "FS Grab" in the book), g17 back hand heel edge = Stale, g18 front hand toe edge = Mute, g19 front hand on the nose = Nose Grab, g20 back hand on the tail = Tail
     * Grab, g21 both hands = Double Grab, g14 131-135 = Coffin - and the field `a` is the variant inside it: 1 / 2 / 3 = the legs (front leg straight + nose down = Nosebone, back leg straight + nose
     * up = Tailbone, front knee tucked = Tuck Knee; in the nose / tail families the same numbers are the BS / FS shifty twists), 4 / 0x104 = the board tweaked. The base grabs and the leg variants
     * are measured (high / medium); which tweaked clip is Method / Christ Air / Cross Bone / No-foot Air / Stiffy / BS Tweak, and which shifty is BS, is not in the data (low): the clips are the
     * 'enter' (target) poses, the picker changes them. */
    set_grab(GRAB_BS_GRAB,        "BS GRAB",             156,  40, 150);
    set_grab(GRAB_FS_GRAB,        "FS GRAB",             156,  40, 150);
    set_grab(GRAB_BS_TWEAK,       "BS TWEAK",            161,  90, 160);
    set_grab(GRAB_NOSEBONE,       "NOSEBONE",            159,  60, 155);
    set_grab(GRAB_TAILBONE,       "TAILBONE",            162,  60, 155);
    set_grab(GRAB_TUCK_KNEE,      "TUCK KNEE",           163,  60, 155);
    set_grab(GRAB_MELON,          "MELON",               145,  50, 150);
    set_grab(GRAB_METHOD,         "METHOD",              148, 100, 165);
    set_grab(GRAB_STALE,          "STALE",               165,  50, 150);
    set_grab(GRAB_STALE_NOSEBONE, "STALE NOSEBONE",      164,  70, 160);
    set_grab(GRAB_STALE_TAILBONE, "STALE TAILBONE",      168,  70, 160);
    set_grab(GRAB_STALE_TUCK,     "STALE TUCK KNEE",     169,  70, 160);
    set_grab(GRAB_MUTE,           "MUTE",                171,  50, 150);
    set_grab(GRAB_MUTE_NOSEBONE,  "MUTE NOSEBONE",       174,  70, 160);
    set_grab(GRAB_MUTE_TAILBONE,  "MUTE TAILBONE",       175,  70, 160);
    set_grab(GRAB_MUTE_TUCK,      "MUTE TUCK KNEE",      170,  70, 160);
    set_grab(GRAB_NOSE,           "NOSE GRAB",           176,  55, 150);
    set_grab(GRAB_BS_SHIFTY_NOSE, "BS SHIFTY NOSE GRAB", 185,  80, 160);
    set_grab(GRAB_FS_SHIFTY_NOSE, "FS SHIFTY NOSE GRAB", 182,  80, 160);
    set_grab(GRAB_ROCKET,         "ROCKET AIR",          179,  90, 165);
    set_grab(GRAB_TAIL,           "TAIL GRAB",           188,  55, 150);
    set_grab(GRAB_BS_SHIFTY_TAIL, "BS SHIFTY TAIL GRAB", 194,  80, 160);
    set_grab(GRAB_FS_SHIFTY_TAIL, "FS SHIFTY TAIL GRAB", 191,  80, 160);
    set_grab(GRAB_DOUBLE,         "DOUBLE GRAB",         197,  80, 160);
    set_grab(GRAB_COFFIN,         "COFFIN",              131, 120, 170);
    set_grab(GRAB_CHRIST,         "CHRIST AIR",          151, 100, 165);
    set_grab(GRAB_CROSSBONE,      "CROSS BONE",          134, 110, 165);
    set_grab(GRAB_NOFOOT,         "NO-FOOT AIR",         160, 100, 165);
    set_grab(GRAB_STIFFY,         "STIFFY",              135, 110, 165);
    /* moves: spins (points per 180 degrees, scored when they land), reverts, powerslides and wallrides (per second) */
    set_def(&tricks.move[MOVE_BS_SPIN],       "BS SPIN",       -1, 100,   0, 1.0f, 0);
    set_def(&tricks.move[MOVE_FS_SPIN],       "FS SPIN",       -1, 100,   0, 1.0f, 0);
    set_def(&tricks.move[MOVE_BS_HALFCAB],    "BS HALF-CAB",   -1, 150,   0, 1.0f, 0);
    set_def(&tricks.move[MOVE_FS_HALFCAB],    "FS HALF-CAB",   -1, 150,   0, 1.0f, 0);
    set_def(&tricks.move[MOVE_BS_CAB],        "BS CAB",        -1, 300,   0, 1.0f, 0);
    set_def(&tricks.move[MOVE_FS_CAB],        "FS CAB",        -1, 300,   0, 1.0f, 0);
    set_def(&tricks.move[MOVE_BS_REVERT],     "BS REVERT",     -1, 100,   0, 1.0f, 0);
    set_def(&tricks.move[MOVE_FS_REVERT],     "FS REVERT",     -1, 100,   0, 1.0f, 0);
    set_def(&tricks.move[MOVE_BS_POWERSLIDE], "BS POWERSLIDE", -1,  25,  60, 1.0f, 0);
    set_def(&tricks.move[MOVE_FS_POWERSLIDE], "FS POWERSLIDE", -1,  25,  60, 1.0f, 0);
    set_def(&tricks.move[MOVE_BS_WALLRIDE],   "BS WALLRIDE",   -1, 150,  80, 1.0f, 0);
    set_def(&tricks.move[MOVE_FS_WALLRIDE],   "FS WALLRIDE",   -1, 150,  80, 1.0f, 0);
    tricks.bail_air[0] = 363; tricks.bail_air[1] = 364;
    tricks.bail_ground = 369;
}

/* "<key> <slot> <pose> <base> <rate> <diff> <name>" */
static int parse_def(const char *line, const char *key, GrindType *arr, int n, int min_pose)
{
    char fmt[48];
    int slot, pose, base, rate, used = 0;
    float diff;
    snprintf(fmt, sizeof(fmt), "%s %%d %%d %%d %%d %%f %%n", key);
    if (sscanf(line, fmt, &slot, &pose, &base, &rate, &diff, &used) < 5 || slot < 0 || slot >= n || pose < min_pose || pose >= 376 || diff <= 0.05f) return 0;
    GrindType *g = &arr[slot];
    g->pose = pose; g->base = base; g->rate = rate; g->diff = diff;
    if (used > 0) {
        strncpy(g->name, line + used, sizeof(g->name) - 1);
        g->name[sizeof(g->name) - 1] = 0;
        for (char *c = g->name; *c; c++) if (*c == '\n' || *c == '\r') { *c = 0; break; }
    }
    return 1;
}

#define CFG_VERSION 3        /* 3: the grind (16), flip (22) and grab (29) tables have new slots and meanings (older files held 5 / 6 / 5) and the table of moves exists: those lines of older files are ignored. */

int tricks_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[160];
    int version = 1;
    while (fgets(line, sizeof(line), f)) {                     /* first pass: the version */
        int v;
        if (sscanf(line, "version %d", &v) == 1) version = v;
    }
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        int slot, pose;
        if (version >= 3 && parse_def(line, "grind", tricks.grind, NGRIND_TYPES, 0)) continue;
        if (parse_def(line, "manual", tricks.manual, NMANUAL_TYPES, -1)) continue;
        if (version >= 3 && parse_def(line, "flip", tricks.flip, NFLIP_TYPES, -1)) continue;
        if (version >= 3 && parse_def(line, "grab", tricks.grab, NGRAB_TYPES, 0)) continue;
        if (version >= 3 && parse_def(line, "move", tricks.move, NMOVE_TYPES, -1)) continue;
        float tp, tr, ty;
        if (version >= 3 && sscanf(line, "grindtilt %d %f %f %f", &slot, &tp, &tr, &ty) == 4 && slot >= 0 && slot < NGRIND_TYPES) {
            tricks.grind[slot].tp = tp; tricks.grind[slot].tr = tr; tricks.grind[slot].ty = ty;
        } else if (version >= 3 && sscanf(line, "flipalt %d %d", &slot, &pose) == 2 && slot >= 0 && slot < NFLIP_TYPES && pose >= -1 && pose < 376) {
            tricks.flip[slot].alt = pose;
        } else if (sscanf(line, "grindsound %d %d", &slot, &pose) == 2 && slot >= 0 && slot < NGRIND_TYPES) {
            tricks.grind[slot].board = pose != 0;
        } else if (sscanf(line, "bail_air %d %d", &slot, &pose) == 2 && slot >= 0 && slot < 2 && pose >= 0 && pose < 376) {
            tricks.bail_air[slot] = pose;
        } else if (sscanf(line, "bail_ground %d", &pose) == 1 && pose >= 0 && pose < 376) {
            tricks.bail_ground = pose;
        }
    }
    fclose(f);
    return 1;
}

static void save_defs(FILE *f, const char *key, const GrindType *arr, int n)
{
    for (int i = 0; i < n; i++)
        fprintf(f, "%s %d %d %d %d %.2f %s\n", key, i, arr[i].pose, arr[i].base, arr[i].rate, arr[i].diff, arr[i].name);
}

int tricks_save(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Skate It PSP trick table. Edit freely; the game rewrites it when you change a pose with the picker.\n");
    fprintf(f, "version %d\n", CFG_VERSION);
    fprintf(f, "# grind <slot> <pose clip 0..375> <points for landing it> <points per second> <balance difficulty, 1 = easy> <name>   (backside / frontside is decided by the side of the rail)\n");
    fprintf(f, "# slots 0-15: 50-50, 5-0, nosegrind, boardslide, lipslide, noseslide, tailslide, smith, feeble, crooked, overcrooks, willy, overwilly, salad, blunt, noseblunt (Triangle + the nub; Square held too for 7-13)\n");
    save_defs(f, "grind", tricks.grind, NGRIND_TYPES);
    fprintf(f, "# grindsound <slot> <0 = grinds on the trucks (50-50 like), 1 = on the board (slides)>: which of the original's grind sound effects it uses\n");
    for (int i = 0; i < NGRIND_TYPES; i++)
        fprintf(f, "grindsound %d %d\n", i, tricks.grind[i].board);
    fprintf(f, "# grindtilt <slot> <pitch> <roll> <yaw>: the board is tilted from the clip frame by this many degrees (nose up, about its long axis, about the deck normal)\n");
    for (int i = 0; i < NGRIND_TYPES; i++)
        fprintf(f, "grindtilt %d %.0f %.0f %.0f\n", i, tricks.grind[i].tp, tricks.grind[i].tr, tricks.grind[i].ty);
    fprintf(f, "# manual <slot 0 = manual, 1 = nose manual> <pose clip or -1 = ordinary stance tilted with the board> <points for starting it> <points per second> <balance difficulty> <name>\n");
    save_defs(f, "manual", tricks.manual, NMANUAL_TYPES);
    fprintf(f, "# flip <slot> <flip clip 315..362: skeleton and board animation, -1 = a body flip> <points when completed> <duration in MILLISECONDS> <unused, 1.00> <name>\n");
    fprintf(f, "# slots 0-5 basic (flick with Cross held: 0 up-left, 1 up-right, 2 left, 3 right, 4 down-left, 5 down-right), 6-13 advanced (Square held: varial kick, varial heel, hardflip, inward heel,\n");
    fprintf(f, "# laser, 360 flip, 360 hardflip, 360 inward heel), 14-15 backflip / frontflip, 16-21 late flips (a flick in the air). Landing before 60 percent of the flip is a bail.\n");
    save_defs(f, "flip", tricks.flip, NFLIP_TYPES);
    fprintf(f, "# flipalt <slot> <clip of the NOLLIE version, -1 = the same clip>\n");
    for (int i = 0; i < NFLIP_TYPES; i++)
        fprintf(f, "flipalt %d %d\n", i, tricks.flip[i].alt);
    fprintf(f, "# grab <slot> <grab clip, a static pose> <points for grabbing> <points per second held> <unused, 1.00> <name>\n");
    fprintf(f, "# slots: see the GRAB_* enum in src/trick.h (L / R + the nub picks the family, a flick while it is held the variant)\n");
    save_defs(f, "grab", tricks.grab, NGRAB_TYPES);
    fprintf(f, "# move <slot> <unused, -1> <points (per 180 degrees for spins)> <points per second> <unused, 1.00> <name>: spins, reverts, powerslides, wallrides\n");
    save_defs(f, "move", tricks.move, NMOVE_TYPES);
    fprintf(f, "# bail: clips while falling (tipping left / right) and while lying on the ground\n");
    fprintf(f, "bail_air 0 %d\nbail_air 1 %d\nbail_ground %d\n", tricks.bail_air[0], tricks.bail_air[1], tricks.bail_ground);
    fclose(f);
    return 1;
}
