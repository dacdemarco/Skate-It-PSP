/* Recorded runs played back: the original's DATA/AI recordings (tools/campaign_air.py, converted by tools/convert_ai.py into data/ai.pak) turned into riders you can see - the opponents of
 * the races and of the score contests, and the skaters that roam the free-skate places. No PSP dependencies (host-tested in tests/test_ghost.c).
 *
 * A recording is a track: the rider's position 4 times a second (point i is where he was i / 4 s after the start) and a list of events with their frame (60 per second). What the events
 * mean was worked out from the data (tests/test_ghost.c checks it against the levels): 27 = a flip trick (arg 0 = the original's trick id), 6 = a push, 56 / 57 = a bail begins,
 * 54 = the respawn that follows it (the track jumps to a new place there). Everything else about the pose comes from the track itself: the heading and speed from the motion, "in the air" from
 * the ground under him. The 7 bytes of pose data of each sample are not used. */
#ifndef GHOST_H
#define GHOST_H

#include <stddef.h>
#include <stdint.h>
#include "skate.h"

#define GHOST_HZ 4
#define GHOST_MAX_ROUTES 32

typedef struct { uint16_t frame; uint8_t kind, arg[4], pad; } GhostEvent;

enum { GEV_FLIP = 27, GEV_PUSH = 6, GEV_BAIL_A = 56, GEV_BAIL_B = 57, GEV_RESPAWN = 54, GEV_BANK = 49,
       GEV_GRIND = 51, GEV_GRIND_END_A = 38, GEV_GRIND_END_B = 39, GEV_GRIND_END_C = 35, GEV_GRAB = 43, GEV_GRAB_END = 44 };

typedef struct {
    int chal, mode, a, b;
    int n;                          /* points */
    const float *p;                 /* n * 3: x, y, z in metres (the level's frame) */
    const uint8_t *flag;            /* n: bit 0 = the track jumps to this point (a respawn) */
    int nev;
    const GhostEvent *ev;
    float dur;                      /* seconds from the first point to the last */
    float len;                      /* metres travelled, jumps not counted */
} GhostTrack;

typedef struct { const uint8_t *buf; size_t size; uint32_t count; } GhostPak;

/* buf stays owned by the caller; returns 0 when it is not an ai.pak or runs past its size */
int ghost_pak_load(GhostPak *g, const uint8_t *buf, size_t size);
/* the recording <chal>_<mode>_<a>_<b> of a level (stem "amphitheatre"); 0 when there is none */
int ghost_find(const GhostPak *g, const char *level, int chal, int mode, int a, int b, GhostTrack *t);
/* the i-th recording of the pak (0 .. count - 1), with the level stem it belongs to (tests, tools) */
int ghost_entry(const GhostPak *g, uint32_t i, GhostTrack *t, char level[24]);
/* the free-skate routes of a level (chal 0, mode 0): how many, and the k-th */
int ghost_route_count(const GhostPak *g, const char *level);
int ghost_route(const GhostPak *g, const char *level, int k, GhostTrack *t);

/* the original's trick id (event 27, arg 0) as one of our flips: 1 + the flip slot of src/trick.h, 0 for none (a plain ollie, an id that is not a flip); *nollie is set for the nollie versions */
int ghost_flip_of_id(int id, int *nollie);

/* the original's trick id of a grind (event 51, arg 0) as one of our grind slots: 1 + the slot, 0 if it is not a grind; *bs = 1 for the backside id, 0 for the frontside one (id + 1) */
int ghost_grind_of_id(int id, int *bs);
/* ... and of a grab (event 43, arg 0): 1 + the grab slot, 0 for none */
int ghost_grab_of_id(int id);

/* ground height under (x, z) at or below y_hint (+ a little): returns 1 and sets *y */
typedef int (*GhostGroundFn)(void *ctx, float x, float z, float y_hint, float *y);

/* a rider playing a track. The fields after `time` are the state the drawing code wants (the same ones the physics gives the player, see Skate in src/skate.h). */
typedef struct {
    const GhostTrack *t;
    float rate;                     /* playback speed: 1 = as recorded, < 1 = slower */
    float time;                     /* seconds into the recording */
    int done;                       /* the track is over (he stands at its last point) */
    int jumped;                     /* a respawn happened in the last update */
    float pos[3], heading, speed, steer;
    float fwd[3], up[3];            /* the body frame: the ground's slope under him */
    int grounded;
    float air_t;
    int flip, flip_nollie;          /* 1 + flip slot while a flip trick plays, else 0 */
    float flip_t, flip_dur;
    int grind, grind_bs;            /* 1 + grind slot while he grinds (event 51 until the end event), the side; 0 = not grinding */
    float grind_t;
    int grab;                       /* 1 + grab slot while he holds a grab (events 43 / 44) */
    float grab_t;
    float push_t;                   /* seconds since the last push began, < 0 = none */
    float bail_t;                   /* seconds since a bail began, 0 = not bailing */
    int next_ev;                    /* events before this index have been seen */
} GhostPlayer;

/* time = where in the recording to start (seconds) */
void ghost_init(GhostPlayer *g, const GhostTrack *t, float rate, float time);
/* advance by dt of play time (the recording moves by dt * rate); gfn may be NULL (then he is always on the ground and level) */
void ghost_update(GhostPlayer *g, float dt, GhostGroundFn gfn, void *gctx);
/* a rider who just stands there (no track): position, heading and the slope under him */
void ghost_stand(GhostPlayer *g, const float pos[3], float heading, GhostGroundFn gfn, void *gctx);
/* the physics state the pose logic (src/rider.c) and the body matrix read, filled in from a ghost: a rider the recording moves is drawn exactly like the player */
void ghost_fill_skate(const GhostPlayer *g, Skate *s);
/* the track's position at `time` seconds (clamped to its ends; a respawn is a step) */
void ghost_pos_at(const GhostTrack *t, float time, float pos[3]);

#endif
