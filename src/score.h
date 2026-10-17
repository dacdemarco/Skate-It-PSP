/* Scoring (no PSP dependencies - host-tested in tests/test_score.c).
 * Every grind, manual, grab and flip is a trick worth GrindType.base points when it starts plus GrindType.rate points per second while it lasts. Tricks chained without the rider
 * rolling on the ground for BANK_TIME form a COMBO: its points are multiplied by the number of tricks in it. The combo banks into the total once the rider has rolled for BANK_TIME after
 * the last trick; a bail anywhere in it loses all of it. */
#ifndef SCORE_H
#define SCORE_H

#include "skate.h"
#include "trick.h"

#define SCORE_BANK_TIME  0.7f
#define SCORE_MAX_MULT   10
#define SCORE_MSG_TIME   2.2f

enum { SCORE_MSG_NONE = 0, SCORE_MSG_BANK = 1, SCORE_MSG_BAIL = 2 };

#define SCORE_LOG_MAX 48
#define TREC_OLLIE (-1)         /* TrickRec.type of a plain ollie (class TRICK_FLIP; nollie set for a nollie): worth no points and not counted for the multiplier, but it is part of the line */

/* one trick of a combo, for the challenge goals (src/goals.c) */
typedef struct {
    short cls, type;            /* TRICK_*, the slot in its table (TREC_OLLIE for a plain ollie) */
    signed char bs;             /* grinds: 1 = backside, 0 = frontside, -1 = not a grind */
    unsigned char nollie;
    short deg;                  /* spins: degrees */
    unsigned short rail;        /* grinds: 1 + the index of the rail or ledge (0 = none) */
    int flight;                 /* the jump it belongs to (Score.flight when it happened) */
    float pos[3];               /* where the rider was when it happened */
    float dur;                  /* seconds it lasted (grinds, manuals, grabs, slides) */
    long pts;                   /* its points before the multiplier */
} TrickRec;

typedef struct {
    int regular;                /* the rider rides regular (set by the caller each frame): backside and frontside swap for the world-sided moves (spins, reverts, powerslides, wallrides, grinds) */
    long total;                 /* banked points */
    int combo_active;
    int combo_n;                /* tricks in the combo (including the one in progress) */
    long combo_base;            /* points of the finished tricks, before the multiplier */
    float live;                 /* points of the grind in progress */
    int in_grind;
    int cur_type, cur_kind;     /* trick table index and RAIL_KIND_* of the trick in progress / the last one */
    int cur_cls;                /* its class: TRICK_GRIND / MANUAL / FLIP / GRAB / MOVE (trick_def(cur_cls, cur_type)) */
    int cur_nollie;             /* the flip was a nollie one ("NOLLIE <name>") */
    int cur_bs;                 /* a grind: 1 = backside ("BS <name>"), 0 = frontside, -1 = not a grind */
    int cur_deg;                /* a spin: its degrees ("BS SPIN 360") */
    float roll_t;               /* seconds rolling on the ground since the last trick */
    int msg;                    /* SCORE_MSG_* for the last result, shown for msg_t seconds */
    float msg_t;
    long msg_pts;               /* points banked / lost by that result */
    int msg_n;                  /* tricks in it */
    int msg_mult;               /* its multiplier */
    int ev_bank;                /* a combo was banked (the sound effects clear it), with its points and multiplier: */
    long ev_bank_pts;
    int ev_bank_mult;
    /* the combo's tricks in order, and the last combo banked (a combo of nothing but ollies banks no points but is still a line) */
    TrickRec log[SCORE_LOG_MAX];
    int log_n, cur_rec;         /* cur_rec: the log entry of the trick in progress (a grind, manual, grab ...), -1 none */
    TrickRec bank_log[SCORE_LOG_MAX];
    int bank_n;
    long bank_pts;
    int bank_mult;
    unsigned bank_serial;       /* counts the combos banked: a reader that sees it change has a new bank_log */
    int flight;                 /* counts the jumps (take-offs, also off a rail) */
    int prev_air;
    float pos[3];               /* the rider's position at the last update */
} Score;

void score_init(Score *sc);
/* call after every skate_update: reads and clears the skater's events */
void score_update(Score *sc, Skate *s, float dt);
/* the combo as it would bank now (finished tricks + the one in progress) x the multiplier */
long score_combo_points(const Score *sc);
int score_combo_mult(const Score *sc);
/* name of the trick in progress / the last one */
const char *score_trick_name(const Score *sc);
/* forget the combo without a message (respawn) */
void score_cancel(Score *sc);

#endif
