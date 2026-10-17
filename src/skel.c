#include "skel.h"
#include <math.h>

static void quat_blend(float out[4], const float a[4], const float b[4], float t)
{
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float sb = dot < 0.0f ? -t : t;
    float sa = 1.0f - t;
    float q[4];
    for (int k = 0; k < 4; k++)
        q[k] = sa * a[k] + sb * b[k];
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n < 1e-8f) {
        for (int k = 0; k < 4; k++) out[k] = a[k];
        return;
    }
    for (int k = 0; k < 4; k++)
        out[k] = q[k] / n;
}

void skelframe_blend(SkelFrame *out, const SkelFrame *a, const SkelFrame *b, float t)
{
    for (int k = 0; k < 3; k++)
        out->root[k] = a->root[k] + (b->root[k] - a->root[k]) * t;
    for (int i = 0; i < SKEL_NBONES; i++)
        quat_blend(out->q[i], a->q[i], b->q[i], t);
}

void boardframe_blend(BoardFrame *out, const BoardFrame *a, const BoardFrame *b, float t)
{
    for (int k = 0; k < 3; k++)
        out->pos[k] = a->pos[k] + (b->pos[k] - a->pos[k]) * t;
    quat_blend(out->q, a->q, b->q, t);
}

/* rotation matrix of the INVERSE of quaternion q (x,y,z,w): the conjugate, so the vector part is negated */
static void rot_inv(const float q[4], float R[9])
{
    float x = -q[0], y = -q[1], z = -q[2], w = q[3];
    float n = sqrtf(x * x + y * y + z * z + w * w);
    if (n > 0.0f) { x /= n; y /= n; z /= n; w /= n; }
    R[0] = 1 - 2 * (y * y + z * z); R[1] = 2 * (x * y - z * w);     R[2] = 2 * (x * z + y * w);
    R[3] = 2 * (x * y + z * w);     R[4] = 1 - 2 * (x * x + z * z); R[5] = 2 * (y * z - x * w);
    R[6] = 2 * (x * z - y * w);     R[7] = 2 * (y * z + x * w);     R[8] = 1 - 2 * (x * x + y * y);
}

static void to_viewer(const float R[9], const float t[3], float scale, float out[12])
{
    /* viewer = scale * (-x, z, y) of the data-frame result */
    for (int c = 0; c < 3; c++) {
        out[0 * 4 + c] = -scale * R[0 * 3 + c];
        out[1 * 4 + c] =  scale * R[2 * 3 + c];
        out[2 * 4 + c] =  scale * R[1 * 3 + c];
    }
    out[3]  = -scale * t[0];
    out[7]  =  scale * t[2];
    out[11] =  scale * t[1];
}

void skel_matrices(const SkelBone *bones, const SkelFrame *f, float scale, float out[SKEL_NBONES][12])
{
    float R[SKEL_NBONES][9], T[SKEL_NBONES][3];
    for (int i = 0; i < SKEL_NBONES; i++) {
        float r[9];
        rot_inv(f->q[i], r);
        int p = bones[i].parent;
        if (p < 0) {
            for (int k = 0; k < 9; k++) R[i][k] = r[k];
            for (int k = 0; k < 3; k++) T[i][k] = f->root[k];
        } else {
            const float *pR = R[p];
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                    R[i][a * 3 + b] = pR[a * 3 + 0] * r[0 * 3 + b] + pR[a * 3 + 1] * r[1 * 3 + b] + pR[a * 3 + 2] * r[2 * 3 + b];
            for (int a = 0; a < 3; a++)
                T[i][a] = T[p][a] + pR[a * 3 + 0] * bones[i].off[0] + pR[a * 3 + 1] * bones[i].off[1] + pR[a * 3 + 2] * bones[i].off[2];
        }
        to_viewer(R[i], T[i], scale, out[i]);
    }
}

void board_matrix(const BoardFrame *b, float scale, float out[12])
{
    float R[9];
    rot_inv(b->q, R);
    to_viewer(R, b->pos, scale, out);
}
