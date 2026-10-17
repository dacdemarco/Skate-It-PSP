/* Skeleton / pose maths (no PSP dependencies, so it can be unit-tested on the host).
 * Data layout matches tools/convert_skater.py (SKS2). Model: G_i = G_parent * T(offset_i) * inverse(R(q_i)), see docs/TECHNICAL.md. */
#ifndef SKEL_H
#define SKEL_H

#include <stdint.h>

#define SKEL_NBONES 22

typedef struct {
    int32_t parent;
    float off[3];
} SkelBone;                                  /* 16 bytes, as in the file */

typedef struct {
    float root[3];
    float q[SKEL_NBONES][4];                 /* x, y, z, w */
} SkelFrame;                                 /* 364 bytes, as in the file */

typedef struct {
    float pos[3];
    float q[4];                              /* x, y, z, w */
} BoardFrame;                                /* 28 bytes, as in the file */

typedef struct {
    uint32_t nframes, frame_off, board_nframes, board_off;
} SkelAnim;                                  /* 16 bytes, as in the file */

/* out = a*(1-t) + b*t, quaternions nlerp'd on the short arc */
void skelframe_blend(SkelFrame *out, const SkelFrame *a, const SkelFrame *b, float t);
void boardframe_blend(BoardFrame *out, const BoardFrame *a, const BoardFrame *b, float t);

/* Forward kinematics. out[i] = 3x4 row-major matrix mapping bone-local model coordinates to the viewer frame
 * (y up, board nose = +z): viewer = scale * (-x, z, y)_data. */
void skel_matrices(const SkelBone *bones, const SkelFrame *f, float scale, float out[SKEL_NBONES][12]);
void board_matrix(const BoardFrame *b, float scale, float out[12]);

#endif
