/* The score contests against recorded opponents: Jam, Best Trick and S.K.A.T.E. (no PSP dependencies - host-tested in tests/test_contest.c). Driven by src/goals.c like the other challenges.
 * The opponents' results come from the original's recordings (DATA/AI, tools/campaign_air.py): the lines an opponent banked in a Jam / Best Trick run (event 49: frame + points,
 * at 60 frames a second) and, for S.K.A.T.E., the tricks he sets (event 27 of the recordings of his successes). The same recordings are what src/stage.c plays on the screen (the opponents skate their
 * recorded runs; this module only keeps the score). What the rules are [guess where not in the data]:
 *   JAM: the whole time limit; the player's score = the points of the lines he banks (only lines with a trick inside the challenge's zone when the level has one); the opponents' scores grow with the clock
 *     as recorded; the highest score at the end wins.
 *   BEST TRICK: one round per objective record, each round `time` seconds; a round's score = the best single LINE banked in it; the highest total wins. Recorded rounds: DATA/AI `<chal>_5_<round>_<opponent>`;
 *     the two 20-second 5-round contests have no recordings: their rounds are made up from the challenge and round numbers (a fixed pseudo-random value in the range the recordings show).
 *   S.K.A.T.E.: turn by turn, the opponent sets a trick (the ones he did in the recordings), the player must land the same trick within the time limit (10 s) or gets a letter; then the player sets one
 *     (the first trick he lands) and the opponent copies it with a chance that depends on the trick and the challenge's difficulty (skate_set); a miss is a letter for him. The first to S-K-A-T-E loses. */
#ifndef CONTEST_H
#define CONTEST_H

#include "career.h"
#include "levelobj.h"
#include "score.h"

#define CONTEST_MAX_OPP 3
#define CONTEST_MAX_ROUNDS 5
#define CONTEST_MAX_ZONES 4
#define CONTEST_BREAK_TIME 3.0f         /* between rounds / turns */
#define CONTEST_SCORE_CAP 4096          /* the recordings' line points saturate here */
#define SKATE_LETTERS 5
#define SKATE_MAX_SET 10

enum { CT_RUNNING = 0, CT_WON, CT_LOST };
enum { SK_OPP_SETS = 0, SK_PLAYER_SETS, SK_OPP_REPLY, SK_RESULT };

typedef struct {
    float dt;
    float pos[3];
    int bails;                          /* bails since the last call */
    const Score *score;
} ContestIn;

typedef struct {
    int mode;                           /* MODE_JAM / MODE_BESTTRICK / MODE_SKATE */
    int chal;
    const Career *career;
    int nopp, opp_skater[CONTEST_MAX_OPP];
    int rounds, round;                  /* Best Trick: the number of rounds and the one being played (Jam: 1, 0) */
    float limit;                        /* seconds of a round / the whole Jam / an attempt */
    float t;                            /* seconds into the round / the Jam / the turn */
    float brk;                          /* > 0: the pause before the next round / turn */
    int nzones;
    float zlo[CONTEST_MAX_ZONES][3], zhi[CONTEST_MAX_ZONES][3];
    unsigned seen_bank;
    int result;                         /* CT_* */
    int place;                          /* the player's place at the end (1 = won) */
    /* Jam / Best Trick */
    long player_score;                  /* Jam: the banked points; Best Trick: the total of the rounds' best lines */
    long player_round;                  /* Best Trick: the best line of this round so far */
    long opp_score[CONTEST_MAX_OPP];    /* Jam: the score now; Best Trick: the total of the finished rounds */
    long opp_round[CONTEST_MAX_OPP];    /* Best Trick: the opponent's best line of this round (known when it ends) */
    long round_score[CONTEST_MAX_ROUNDS];       /* the player's finished rounds */
    /* S.K.A.T.E. */
    int state, turn;
    int set_ids[SKATE_MAX_SET], nset;   /* the tricks the opponent sets, in order */
    int set_ai[SKATE_MAX_SET];          /* the recording (DATA/AI <chal>_4_<n>_0) of each of them, -1 = none */
    int target;                         /* the trick to match / the trick the opponent copies (the original's trick id, -1 none) */
    int letters_player, letters_opp;
    int last_ok;                        /* the last attempt: 1 landed, 0 missed, -1 nothing yet */
    unsigned rng;
    float p_copy;                       /* the chance the opponent copies the player's trick */
    int msg;                            /* what to say: CT_MSG_* */
} Contest;

enum { CT_MSG_NONE = 0, CT_MSG_OPP_SETS, CT_MSG_PLAYER_SETS, CT_MSG_MATCHED, CT_MSG_MISSED, CT_MSG_OPP_MATCHED, CT_MSG_OPP_MISSED, CT_MSG_SET_FAILED, CT_MSG_ROUND_END };

/* 0 if the challenge is not a Jam / Best Trick / S.K.A.T.E. or the level lacks what it needs */
int contest_setup(Contest *k, const Career *c, int chal, const LevelObjs *o);
/* the challenge started (GO): the first round / turn begins */
void contest_start(Contest *k);
/* advance by dt; returns CT_RUNNING until the contest is decided */
int contest_update(Contest *k, const ContestIn *in);
/* the opponent's score now (Jam) / his total so far (Best Trick) */
long contest_opp_score_at(const Contest *k, int opp, float t);
/* the points a line would add: 0 if it does not count (outside the zone) */
long contest_line_points(const Contest *k, const Score *sc);
/* S.K.A.T.E.: letters as text "S K A _ _" */
void contest_letters(int n, char out[12]);

#endif
