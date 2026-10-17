/* Challenge goals (no PSP dependencies - host-tested in tests/test_goals.c): runs one challenge of the original's career (src/career.h) against what the rider does.
 * The objectives come straight from the ROM's tables; how the types are read is decided here (the table says WHAT, the rules below say HOW - the ones marked [guess] are not in the data):
 *   SCORE        banked points since the start (a single line when the challenge text says "single line")
 *   LINE_SCORE   one line worth that much;   LINE  a line of at least two tricks
 *   TRICK        a trick id, or a class mask (flip / advanced flip / grind / manual / grab / advanced grab / front / back trucks / advanced grind / any), a count, or a spin of that many degrees
 *                with it (mod 2); a number in rail[] is a grind line number for grinds and a zone number (chalNNN_zone_k) otherwise - the level decides: where the challenge has zones the numbers are zones
 *                (all 7 such challenges fit, docs/CAMPAIGN_FORMAT.md); mod bit 1 = the trick is done in the air over the zone, bit 8 = the jump lands in it
 *   AIR_TIME     seconds in the air, added up [guess];   SPIN  degrees of landed spins added up
 *   OWN_THE_SPOT a line worth more than a target taken half way into the original's range [guess], with a trick on the listed grind line / in the listed zone / near the beam
 * A challenge with the LINE flag (Photo mode, "..into" chains) has its objectives done by ONE line, tricks in the listed order [guess for Photo]; the others add up over the time limit.
 * Tricks and lines count when the combo is banked (landed), see Score.bank_log. */
#ifndef GOALS_H
#define GOALS_H

#include "career.h"
#include "levelobj.h"
#include "score.h"
#include "race.h"
#include "contest.h"

#define GOAL_MAX_OBJ 8
#define GOAL_MAX_ZONES 8
#define GOAL_MAX_FLIGHTS 16
#define GOAL_COUNTDOWN_TIME 3.0f

typedef struct { float lo[3], hi[3]; } GoalBox;

enum { GOAL_IDLE = 0, GOAL_COUNTDOWN_PHASE, GOAL_ACTIVE, GOAL_WON, GOAL_LOST };

/* what the goals look at every frame */
typedef struct {
    float dt;
    int grounded, grinding;
    float air_t;                    /* Skate.air_t */
    float pos[3];
    int pushes;                     /* pushes started since the last call */
    const Score *score;
    int bails;                      /* bails since the last call (a race takes seconds off the clock for each) */
    RaceGroundFn ground;            /* the level's ground, for the opponents of a race (NULL = they follow the course heights) */
    void *ground_ctx;
} GoalIn;

typedef struct {
    int flight;
    unsigned passed;                /* bit k: the rider was in the air over zone k */
    unsigned landed;                /* bit k: the jump ended in zone k */
    int ended;
} GoalFlight;

typedef struct {
    int phase;
    const Career *career;
    const LevelObjs *objs;
    const uint16_t *rail_ids;       /* the original's grind line number of each rail of the level (0x8000 = none), or NULL */
    uint32_t nrails;
    const CareerChal *ch;
    const CareerObj *obj;
    int nobj, chal;
    /* where the challenge is in the level (chalNNN_* objects) */
    int have_beam, have_start, have_area;
    float beam[3], start_pos[3], start_heading;
    GoalBox area;
    int nzones;                     /* highest zone number present */
    unsigned zone_mask;             /* bit k: zone k exists */
    GoalBox zone[GOAL_MAX_ZONES + 1];
    /* progress */
    float countdown, t, time_left, grace;
    int timed;                      /* the challenge has a time limit */
    int done[GOAL_MAX_OBJ];
    int prog[GOAL_MAX_OBJ];         /* counts / points / degrees so far, for the HUD */
    int need[GOAL_MAX_OBJ];         /* what the objective asks for in the same unit */
    long base_total, total, best_line;
    long own_target;
    int spin_total;
    float air_total, air_peak;
    int pushes_total;
    unsigned seen_bank;
    long live_line;                 /* the line in progress (HUD) */
    GoalFlight fl[GOAL_MAX_FLIGHTS];
    int nfl, cur_flight, prev_air;
    float result_t;                 /* seconds since the challenge was won / lost */
    int is_contest;                 /* a Jam / Best Trick / S.K.A.T.E. challenge: `contest` holds the rules and the opponents' scores, nobj is 0 */
    Contest contest;
    int is_race;                    /* a Checkpoint / Deathrace challenge: `race` holds the gates and the opponents, nobj is 0 (the gates are not listed as objectives) */
    Race race;
    const GhostPak *ai;             /* the recorded runs of the original's opponents (data/ai.pak) and the level they are looked up under: set by goals_set_recordings, kept over goals_begin */
    char level[32];
} Goals;

void goals_init(Goals *g, const Career *c, const LevelObjs *objs, const uint16_t *rail_ids, uint32_t nrails);
/* is this challenge one the game can run (modes Photo, Film, Own The Spot, Tutorial, Checkpoint, Deathrace)? The level's beam is the caller's business. */
int goals_supported(const Goals *g, int chal);
/* the opponents of the races play these recordings instead of following the course (NULL / "" = the generic line); level = the level's stem ("sanfran") */
void goals_set_recordings(Goals *g, const GhostPak *ai, const char *level);
int goals_begin(Goals *g, int chal, const Score *sc);          /* the countdown starts; 0 if the challenge cannot be run */
void goals_abort(Goals *g);                                    /* back to idle without a result */
void goals_update(Goals *g, const GoalIn *in);
int goals_active(const Goals *g);                              /* countdown or running */

/* the original's trick id of a logged trick (-1 = none: spins, body flips, powerslides ...) and the class bits it belongs to (OBJ_CLASS_* masks of docs/CAMPAIGN_FORMAT.md section 6) */
int goals_trick_id(const TrickRec *r);
unsigned goals_trick_classes(const TrickRec *r);

const char *goals_obj_text(const Goals *g, int i);

#endif
