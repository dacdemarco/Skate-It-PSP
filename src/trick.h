/* The trick table: which tricks exist, how each one looks (a clip of the 376-pose library), what it scores and how hard it is to balance - plus the bail poses.
 * No PSP dependencies (host-tested). The table is data: tricks_defaults() fills it, data/tricks.cfg (a text file, see tricks_save) overrides it (the in-game pose pickers are gone: edit the file). A grind type is chosen by the player's trick input at the moment the board locks on to a rail or ledge (see SkateInput.grind), never by the rail. */
#ifndef TRICK_H
#define TRICK_H

#define NGRIND_TYPES 16         /* grind types, see the GRIND_* slots; backside / frontside is decided by the side of the rail or ledge (SkateInput.grind = 1 + type) */
#define NMANUAL_TYPES 2         /* manual (rear wheels) and nose manual (front wheels) */
#define NFLIP_TYPES 22          /* flip tricks, see the FLIP_* slots: 0-5 basic, 6-13 advanced, 14-15 body flips, 16-21 late flips (done in the air) */
#define NGRAB_TYPES 29          /* grabs, see the GRAB_* slots: L / R held in the air + the nub picks the family, a flick while it is held the variant (src/control.c) */
#define NMOVE_TYPES 12          /* spins, reverts, powerslides and wallrides (class TRICK_MOVE), see the MOVE_* slots */

/* flip slots (the trick book's names; every one but the body flips has a nollie version, see GrindType.alt) */
enum { FLIP_KICKFLIP = 0, FLIP_HEELFLIP, FLIP_POP_SHUVIT, FLIP_FS_POP_SHUVIT, FLIP_360_SHUVIT, FLIP_FS_360_SHUVIT,
       FLIP_VARIAL_KICK, FLIP_VARIAL_HEEL, FLIP_HARDFLIP, FLIP_INWARD_HEEL, FLIP_LASER, FLIP_360_FLIP, FLIP_360_HARDFLIP, FLIP_360_INWARD,
       FLIP_BACKFLIP, FLIP_FRONTFLIP,
       FLIP_LATE_KICK, FLIP_LATE_HEEL, FLIP_LATE_SHUVIT, FLIP_LATE_FS_SHUVIT, FLIP_LATE_BF_KICK, FLIP_LATE_BF_HEEL };
/* grind slots (the trick book's 16 grinds; each is BS or FS by the side of the rail / ledge). The clips do not say which truck is on the rail (the board sits at a fixed origin in every clip), so
 * each type is one of three pose families (tools/grind_classify.py: slides across the rail, nose-weighted, tail-weighted) plus a board tilt (GrindType.tp / tr / ty). */
enum { GRIND_5050 = 0, GRIND_50, GRIND_NOSEGRIND, GRIND_BOARDSLIDE, GRIND_LIPSLIDE, GRIND_NOSESLIDE, GRIND_TAILSLIDE, GRIND_SMITH, GRIND_FEEBLE, GRIND_CROOKED,
       GRIND_OVERCROOKS, GRIND_WILLY, GRIND_OVERWILLY, GRIND_SALAD, GRIND_BLUNT, GRIND_NOSEBLUNT };
/* grab slots */
enum { GRAB_BS_GRAB = 0, GRAB_FS_GRAB, GRAB_BS_TWEAK, GRAB_NOSEBONE, GRAB_TAILBONE, GRAB_TUCK_KNEE, GRAB_MELON, GRAB_METHOD,
       GRAB_STALE, GRAB_STALE_NOSEBONE, GRAB_STALE_TAILBONE, GRAB_STALE_TUCK, GRAB_MUTE, GRAB_MUTE_NOSEBONE, GRAB_MUTE_TAILBONE, GRAB_MUTE_TUCK,
       GRAB_NOSE, GRAB_BS_SHIFTY_NOSE, GRAB_FS_SHIFTY_NOSE, GRAB_ROCKET, GRAB_TAIL, GRAB_BS_SHIFTY_TAIL, GRAB_FS_SHIFTY_TAIL,
       GRAB_DOUBLE, GRAB_COFFIN, GRAB_CHRIST, GRAB_CROSSBONE, GRAB_NOFOOT, GRAB_STIFFY };
/* move slots */
enum { MOVE_BS_SPIN = 0, MOVE_FS_SPIN, MOVE_BS_HALFCAB, MOVE_FS_HALFCAB, MOVE_BS_CAB, MOVE_FS_CAB, MOVE_BS_REVERT, MOVE_FS_REVERT,
       MOVE_BS_POWERSLIDE, MOVE_FS_POWERSLIDE, MOVE_BS_WALLRIDE, MOVE_FS_WALLRIDE };

/* trick classes for trick_def() and the scoring */
enum { TRICK_GRIND = 0, TRICK_MANUAL = 1, TRICK_FLIP = 2, TRICK_GRAB = 3, TRICK_MOVE = 4 };

typedef struct {
    char name[24];
    int pose;                   /* clip of the pose library shown while grinding (its board orientation too: a boardslide clip turns the board across the rail) */
    int base;                   /* points for landing it */
    int rate;                   /* points per second while it lasts */
    float diff;                 /* balance difficulty: 1 = easy (manuals; grinds keep the value in the file but have no balancing since the tenth round) */
    int board;                  /* sound: 0 = grinding on the trucks (the original's *_2T effects), 1 = on the board (*_BRD) */
    int alt;                    /* flips: the clip of the NOLLIE version (-1 = none: the same clip is used) */
    float tp, tr, ty;           /* grinds: the board is tilted from the clip's frame by this much (degrees: pitch = nose up, roll about its long axis, yaw about the deck normal) */
} GrindType;

typedef struct {
    GrindType grind[NGRIND_TYPES];
    GrindType flip[NFLIP_TYPES];       /* pose = a flip clip (skeleton AND board animation; -1 = a body flip, turned procedurally), base = points when it is completed, rate = its duration in MILLISECONDS */
    GrindType grab[NGRAB_TYPES];       /* pose = a grab clip (static), base = points on grabbing, rate = points per second while it is held */
    GrindType manual[NMANUAL_TYPES];   /* same fields: pose -1 = no clip, the stance of normal riding tilted procedurally; `board` is unused */
    GrindType move[NMOVE_TYPES];       /* spins / reverts: base = points (instant); powerslides / wallrides: base = points on starting, rate = points per second; pose unused */
    int bail_air[2];            /* clips while falling off (the rider tips towards the left / right) */
    int bail_ground;            /* clip while lying on the ground */
} TrickTable;

extern TrickTable tricks;

/* the table entry of a trick (class TRICK_*, type = its slot) */
const GrindType *trick_def(int cls, int type);

void tricks_defaults(void);
/* returns 1 if the file was read (unknown lines are ignored, lines it does not mention keep their value) */
int tricks_load(const char *path);
int tricks_save(const char *path);

#endif
