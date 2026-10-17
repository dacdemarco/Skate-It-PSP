/* The player's statistics and the trick book (M9; no PSP dependencies, host-tested in tests/test_stats.c): while the player skates, stats_frame adds to the profile's counters (play time,
 * distances, pushes, wipeouts, the personal bests) and, when a combo is banked, marks its tricks as landed (the trick book of the Log Book). The profile keeps them (Profile.stat, Profile.landed).
 * The numbers are the ones the original's Log Book shows (TXT_LOCSTATS_* / TXT_STATS_*): total playtime, distance travelled / grinded / manualed / powerslid, pushes, wipeouts, airgrab time,
 * best airtime, longest grind / manual / powerslide, best scoring sequence. */
#ifndef STATS_H
#define STATS_H

#include "progress.h"
#include "score.h"

typedef struct {
    float acc[8];                       /* fractions that have not made a whole unit yet (seconds, centimetres, milliseconds) */
    float run_grind, run_manual, run_slide;   /* the metres of the grind / manual / powerslide in progress */
    float prev_push_t, prev_bail_t;
    unsigned bank_serial;               /* the Score.bank_serial last looked at */
} StatState;

/* a new stretch of play (a session, or a new Score): remembers where the Score is */
void stats_begin(StatState *st, const Score *sc);
/* once per frame of play (not while paused): the rider's state and what the scorer has banked since the last call */
void stats_frame(Profile *p, StatState *st, const Skate *s, const Score *sc, float dt);

/* the trick book: every trick of the trick table has a bit (the order of the tables: grinds, manuals, flips, grabs, moves) */
#define STATS_NTRICKS (NGRIND_TYPES + NMANUAL_TYPES + NFLIP_TYPES + NGRAB_TYPES + NMOVE_TYPES)
int stats_landed_bit(int cls, int type);                 /* -1 if there is no such trick */
void stats_bit_trick(int bit, int *cls, int *type);
int stats_landed(const Profile *p, int cls, int type);   /* has it been landed */
int stats_landed_count(const Profile *p);                /* how many of the STATS_NTRICKS */

#endif
