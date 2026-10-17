/* The riders you see besides the player: who plays which recorded run, and when (no PSP dependencies - host-tested in tests/test_stage.c). The races keep their own opponents (src/race.c); this
 * module stages
 *   the opponents of a Jam / Best Trick / S.K.A.T.E.: they skate the original's recordings of the challenge (Jam: the whole run, in step with the contest clock; Best Trick: the recording of the
 *   round, from the start of every round; S.K.A.T.E.: the recording of the trick he sets, from a few seconds before it, and a copy attempt after the player's trick), and
 *   the skaters of the free-skate places: a few riders who skate the original's ambient routes (DATA/AI/<LEVEL>/000_0_0_N), one after the other, never right on top of the player. */
#ifndef STAGE_H
#define STAGE_H

#include "contest.h"
#include "ghost.h"
#include "levelobj.h"

#define STAGE_MAX 3
#define STAGE_AMBIENT_MAX 3
#define STAGE_SKATE_LEAD 2.6f            /* S.K.A.T.E.: the opponent starts his recording this many seconds before the trick in it */
#define STAGE_AMBIENT_KEEP_OFF 18.0f     /* metres: an ambient skater is not started closer to the player than this */

typedef struct {
    int skater;                          /* the career skater index to draw; -1 = nobody in this slot */
    int has_trk;
    GhostTrack trk;
    GhostPlayer gp;                      /* what the drawing code reads */
    int key;                             /* S.K.A.T.E. / Best Trick: which recording is loaded (-1 none) */
    float stand_pos[3], stand_heading;   /* where he waits when he has no recording playing */
    int route;                           /* ambient: the route being played */
    float retry;                         /* ambient: seconds until he looks for a new route again */
} StageRider;

typedef struct {
    const GhostPak *pak;
    char level[32];                      /* the level stem the recordings are looked up under */
    StageRider r[STAGE_MAX];
    int n;
    int kind;                            /* 0 none, 1 contest, 2 ambient */
    int nroutes;                         /* ambient: routes of this level */
    unsigned rng;
} Stage;

void stage_init(Stage *s, const GhostPak *pak, const char *level);
void stage_clear(Stage *s);

/* a contest has been set up (and is waiting for GO): who stands where, and which recordings exist */
void stage_contest_begin(Stage *s, const Contest *k, const LevelObjs *o, GhostGroundFn gfn, void *gctx);
/* every frame while the contest is on */
void stage_contest_update(Stage *s, const Contest *k, float dt, GhostGroundFn gfn, void *gctx);

/* n ambient skaters (the career skater indices in skaters[]): each starts somewhere on a route, at least STAGE_AMBIENT_KEEP_OFF from the player; 0 if the level has no routes */
int stage_ambient_begin(Stage *s, const int *skaters, int n, unsigned seed, const float player[3], GhostGroundFn gfn, void *gctx);
void stage_ambient_update(Stage *s, float dt, const float player[3], GhostGroundFn gfn, void *gctx);

/* time of the last banked line (event 49) of a recording, seconds; 0 if it has none */
float stage_last_bank_time(const GhostTrack *t);

#endif
