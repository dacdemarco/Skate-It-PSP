/* The race modes: Checkpoint challenges and Death Races (no PSP dependencies - host-tested in tests/test_race.c). Driven by src/goals.c, which gives a race challenge the same countdown / clock / result as every
 * other challenge.
 *
 * What the ROM says (docs/CAMPAIGN_FORMAT.md, the objective records and the strings of the two modes):
 *   CHECKPOINT: the objective records are the gates IN ORDER (record: gate number, bonus seconds; a bonus of 0xFFFFFFFF marks the FINISH gate); the clock starts at the challenge's time and every gate adds its bonus.
 *     "Get to the bottom of a course before your opponents, while smoothly navigating through a series of gates. Bailing will cost you valuable time."
 *   DEATHRACE: one record, the finish gate; the course is the level's progress_spline. "Be the fastest skater to the bottom of a course ... eating concrete will cost you valuable time."
 *   Gate NNN = the two end points checkpoint_NNN_a / _b of the level (src/levelobj.h). The opponents start on the challenge's start grid slots 2..4.
 *   The opponents' runs are recorded in DATA/AI: 4 samples per second, so an opponent finishes in samples / 4 s (career_opponent_samples). That is checked against the time limits in tests/test_race.c.
 * Ours [guess]: a win is first place; a bail costs RACE_BAIL_PENALTY seconds; gates must be passed in order; the opponents are ghosts that follow the course (gate to gate, or along the spline) at a speed that
 * brings each of them to the finish at its recorded time (they do not touch the player). */
#ifndef RACE_H
#define RACE_H

#include "career.h"
#include "levelobj.h"
#include "ghost.h"

#define RACE_MAX_GATES 16
#define RACE_MAX_OPP 3
#define RACE_MAX_PATH 160
#define RACE_BAIL_PENALTY 3.0f      /* seconds lost per bail [guess] */
#define RACE_AI_FACTOR 1.0f         /* the opponents' finish time = recorded time * this (1 = as recorded) */
#define RACE_WIN_PLACE 1            /* the place that wins [guess: the strings say "before your opponents"] */
#define RACE_ACCEL_TIME 2.0f        /* seconds an opponent takes to reach cruising speed */
#define RACE_AI_MAX_SPEED 9.5f      /* m/s: an opponent that would have to cruise faster than this (the original's downhill races: San Francisco needs 14 m/s over the whole course) is slowed down to it - the port's physics tops out
                                     * at 12 m/s on a slope and the bot of tests/race_bot.c averages 9.4 m/s down the San Francisco hill [guess: tune on hardware] */

typedef struct { float a[3], b[3]; float bonus; int finish; int number; } RaceGate;
typedef struct { float p[RACE_MAX_PATH][3]; float cum[RACE_MAX_PATH]; int n; float len; } RacePath;

typedef struct {
    int skater;                     /* the original's skater index */
    int ai;                         /* his recording: DATA/AI <chal>_<mode>_0_<ai> */
    RacePath path;                  /* start slot -> finish (the generic line: used when there is no recording) */
    float finish_time;              /* seconds from GO to the finish gate */
    float cruise;                   /* m/s */
    int has_rec;                    /* race_use_recordings found his recorded run: he plays it (rec, rate) instead of following `path` */
    GhostTrack rec;
    float rate;                     /* the recording's speed: 1, or less when the original's pace is more than the port's physics allows (RACE_AI_MAX_SPEED) */
    float travelled;                /* metres along the recording so far */
    /* state, valid after race_update */
    float s;                        /* progress in metres of the PLAYER'S course (the fraction of his run done x the course length) */
    float pos[3], heading, speed, steer, turn;
    int finished;
    float up[3], fwd[3];            /* the body frame: the ground's slope under him */
    GhostPlayer view;               /* what the drawing code reads: position, frame, air / ground, flips, pushes, bails (always filled, with or without a recording) */
} RaceOpp;

/* ground height under (x, z) at or below y_hint + a little: returns 1 and sets *y */
typedef int (*RaceGroundFn)(void *ctx, float x, float z, float y_hint, float *y);

enum { RACE_EV_GATE = 1, RACE_EV_FINISH = 2 };

typedef struct {
    int mode;                       /* MODE_CHECKPOINT / MODE_DEATHRACE */
    int ngates, next;               /* the gates in order; the one to pass next */
    RaceGate gate[RACE_MAX_GATES];
    int nopp;
    RaceOpp opp[RACE_MAX_OPP];
    RacePath course;                /* the player's line: start -> the gates / the spline -> finish (for progress and the arrow) */
    float t;                        /* seconds since GO */
    float penalty;                  /* seconds added by bails */
    int have_prev;
    float prev[3];
    float seg_hint;                 /* the course segment the player was nearest to */
    float prog;                     /* arc length of the player's nearest point on the course */
    int player_done, place;         /* finished, and the place (1 = first) */
    float player_time;              /* t + penalty at the finish */
    float last_bonus;               /* the bonus seconds of the gate passed by the last update (the caller adds them to the clock) */
} Race;

/* build the race of a challenge from the level's objects / splines; 0 if the level lacks what it needs (gates, start grid, spline) */
int race_setup(Race *r, const Career *c, int chal, const LevelObjs *o);
/* play the opponents' recorded runs (DATA/AI, data/ai.pak) instead of the generic line along the course: each of them that has a recording follows the path the original's skater took, at the
 * recorded pace (slowed down by the same cap as before). level = the level's stem ("sanfran"). Returns how many opponents play a recording. */
int race_use_recordings(Race *r, const Career *c, int chal, const GhostPak *pak, const char *level);
/* advance the clock by dt and the opponents; pos = the player's position. Returns RACE_EV_* bits. */
int race_update(Race *r, float dt, const float pos[3], RaceGroundFn gfn, void *gctx);
/* the opponents on their grid slots (before the GO): positions, headings and the slope under them */
void race_preview(Race *r, RaceGroundFn gfn, void *gctx);
void race_bail(Race *r);                            /* a bail: the time penalty (the caller also takes it off the clock) */
/* the player's place now: 1 + the opponents that are further along the course (after the finish: the final place) */
int race_place(const Race *r);
/* the opponent's place by time when the player finishes at `time` (their recorded times) */
int race_place_at(const Race *r, float time);
/* the course position at distance s along a path (clamped); also the unit tangent */
void race_path_at(const RacePath *p, float s, float pos[3], float tan[3]);

#endif
