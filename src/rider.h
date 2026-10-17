/* Pose logic: turns the physics state into a blended pose from the original DS pose library (no PSP dependencies). */
#ifndef RIDER_H
#define RIDER_H

#include "skel.h"
#include "skate.h"

/* indices into the 376-clip pose library of anims.arc (skater "aaron"); identified in docs/TECHNICAL.md (Pose library) */
#define POSE_RIDE      0     /* neutral riding stance */
#define POSE_LEAN_L    1     /* leaning to the left of travel */
#define POSE_LEAN_R    2     /* leaning to the right of travel */
#define POSE_CROUCH    3     /* crouch (ollie charge, landing) */
#define POSE_AIR_A     4     /* crouched, arms out, leaning left */
#define POSE_AIR_B     5     /* crouched, arms out, leaning right (A+B averaged = centred arms-out air pose) */
#define POSE_PUSH      42    /* 5-frame push: foot swings to the ground and back */
#define POSE_GRIND     62    /* only the initial value of Rider.grind_pose: what a grind looks like is decided by its type (trick table) */

typedef struct {
    const uint8_t *file;          /* the loaded .skt */
    const SkelAnim *anims;
    uint32_t nanims;
} AnimSet;

typedef struct {
    float lean_l, lean_r, crouch, air, push, grind, bail, manual, man_arms, flip, grab;   /* smoothed blend weights 0..1 */
    int flip_pose, grab_pose;                         /* the flip clip (played from its first to its last frame over the flip) and the grab clip of the trick in progress / the last one */
    float flip_p;                                     /* progress of that flip 0..1 */
    int man_pose;                                     /* manual clip from the trick table, -1 = none: the stance of normal riding with the arms out more the worse the balance */
    float grind_tilt[3];                              /* the grind type's board tilt in degrees (pitch, roll, yaw; src/trick.h) */
    int grind_pose;                                   /* pose-library clip used while grinding (its board orientation too: a boardslide pose turns the board across the rail);
                                                         taken from the trick table (src/trick.h) by the grind type of the moment */
} Rider;

void rider_init(Rider *r);
void rider_update(Rider *r, const Skate *s, float dt);
/* frame `frame` (float, clamped) of clip `anim`, frames interpolated */
void animset_sample(const AnimSet *a, int anim, float frame, SkelFrame *out);
void animset_board(const AnimSet *a, int anim, float frame, BoardFrame *out);
void rider_pose(const Rider *r, const Skate *s, const AnimSet *a, SkelFrame *out, BoardFrame *board);

#endif
