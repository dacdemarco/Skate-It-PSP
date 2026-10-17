#include "rider.h"
#include "trick.h"
#include <math.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* quaternions as x, y, z, w (the layout of the animation files); o = a * b */
static void qmul4(const float a[4], const float b[4], float o[4])
{
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static float dot4(const float a[4], const float b[4]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }
static const float Y180[4] = { 0.0f, 1.0f, 0.0f, 0.0f };              /* a half turn about the board's own y axis (the deck normal) */

void rider_init(Rider *r)
{
    r->lean_l = r->lean_r = r->crouch = r->air = r->push = r->grind = r->bail = r->manual = r->man_arms = r->flip = r->grab = r->flip_p = 0.0f;
    r->flip_pose = r->grab_pose = 0;
    r->man_pose = -1;
    r->grind_pose = POSE_GRIND;
    r->grind_tilt[0] = r->grind_tilt[1] = r->grind_tilt[2] = 0.0f;
}

static float approach(float v, float target, float rate, float dt)
{
    return v + (target - v) * (1.0f - expf(-rate * dt));
}

void rider_update(Rider *r, const Skate *s, float dt)
{
    float spd = fabsf(s->speed);
    float lean = clampf(0.35f + spd / 3.0f, 0.0f, 1.0f);
    float tl = 0.0f, tr = 0.0f;
    if (s->grounded) {
        if (s->steer > 0.0f) tl = clampf(s->steer, 0.0f, 1.0f) * lean;
        else                 tr = clampf(-s->steer, 0.0f, 1.0f) * lean;
    }
    float tc = (s->grounded || s->grinding) ? s->crouch : 0.0f;
    float ta = (s->grounded || s->grinding) ? 0.0f : 1.0f;
    float tg = s->grinding ? 1.0f : 0.0f;
    float tb = s->bail_t > 0.0f ? 1.0f : 0.0f;
    if (s->grinding && s->grind_type >= 0 && s->grind_type < NGRIND_TYPES) {
        r->grind_pose = tricks.grind[s->grind_type].pose;
        r->grind_tilt[0] = tricks.grind[s->grind_type].tp; r->grind_tilt[1] = tricks.grind[s->grind_type].tr; r->grind_tilt[2] = tricks.grind[s->grind_type].ty;
    }
    r->bail = approach(r->bail, tb, tb > 0.5f ? 22.0f : 8.0f, dt);
    if (s->manual) {
        float al = fabsf(s->lean) > 1.0f ? 1.0f : fabsf(s->lean);
        r->man_pose = tricks.manual[s->manual - 1].pose;
        r->man_arms = approach(r->man_arms, 0.25f + 0.45f * al, 10.0f, dt);
    }
    r->manual = approach(r->manual, s->manual ? 1.0f : 0.0f, s->manual ? 10.0f : 8.0f, dt);
    if (s->flip) {
        const GrindType *ft = &tricks.flip[s->flip - 1];   /* the clip of the trick, or of its nollie version; -1 = a body flip (no clip: the whole rider is rolled) */
        r->flip_pose = (s->flip_nollie && ft->alt >= 0) ? ft->alt : ft->pose;
        r->flip_p = clampf(s->flip_t / s->flip_dur, 0.0f, 1.0f);
    } else if (r->flip > 0.5f) {
        r->flip_p = 1.0f;                                  /* the flip is over: its last frame (the landing stance) fades out */
    }
    r->flip = approach(r->flip, s->flip ? 1.0f : 0.0f, s->flip ? 30.0f : 14.0f, dt);
    if (s->grab) r->grab_pose = tricks.grab[s->grab - 1].pose;
    r->grab = approach(r->grab, s->grab ? 1.0f : 0.0f, s->grab ? 16.0f : 10.0f, dt);
    float tp = (s->grounded && s->push_t >= 0.0f) ? 1.0f : 0.0f;
    r->lean_l = approach(r->lean_l, tl, 12.0f, dt);
    r->lean_r = approach(r->lean_r, tr, 12.0f, dt);
    r->crouch = approach(r->crouch, tc, 16.0f, dt);
    r->air    = approach(r->air,    ta, s->grounded ? 10.0f : 14.0f, dt);
    r->push   = approach(r->push,   tp, tp > 0.5f ? 18.0f : 10.0f, dt);
    r->grind  = approach(r->grind,  tg, tg > 0.5f ? 14.0f : 10.0f, dt);
}

static const SkelFrame *frame_at(const AnimSet *a, int anim, int idx)
{
    const SkelAnim *an = &a->anims[anim];
    if (idx < 0) idx = 0;
    if (idx >= (int)an->nframes) idx = (int)an->nframes - 1;
    return (const SkelFrame *)(a->file + an->frame_off) + idx;
}

void animset_sample(const AnimSet *a, int anim, float frame, SkelFrame *out)
{
    int n = (int)a->anims[anim].nframes;
    frame = clampf(frame, 0.0f, (float)(n - 1));
    int i = (int)frame;
    float t = frame - (float)i;
    if (t < 1e-4f || i + 1 >= n) { *out = *frame_at(a, anim, i); return; }
    skelframe_blend(out, frame_at(a, anim, i), frame_at(a, anim, i + 1), t);
}

void animset_board(const AnimSet *a, int anim, float frame, BoardFrame *out)
{
    const SkelAnim *an = &a->anims[anim];
    int n = (int)an->board_nframes;
    frame = clampf(frame, 0.0f, (float)(n - 1));
    int i = (int)frame;
    float t = frame - (float)i;
    const BoardFrame *b = (const BoardFrame *)(a->file + an->board_off);
    if (t < 1e-4f || i + 1 >= n) { *out = b[i]; return; }
    boardframe_blend(out, &b[i], &b[i + 1], t);
}

/* the static clips end in their final pose; use the last frame */
static void static_pose(const AnimSet *a, int anim, SkelFrame *out)
{
    animset_sample(a, anim, (float)(a->anims[anim].nframes - 1), out);
}

void rider_pose(const Rider *r, const Skate *s, const AnimSet *a, SkelFrame *out, BoardFrame *board)
{
    SkelFrame cur, p;
    static_pose(a, POSE_RIDE, &cur);
    if (r->lean_l > 0.001f) { static_pose(a, POSE_LEAN_L, &p); skelframe_blend(&cur, &cur, &p, clampf(r->lean_l, 0, 1)); }
    if (r->lean_r > 0.001f) { static_pose(a, POSE_LEAN_R, &p); skelframe_blend(&cur, &cur, &p, clampf(r->lean_r, 0, 1)); }
    if (r->crouch > 0.001f) { static_pose(a, POSE_CROUCH, &p); skelframe_blend(&cur, &cur, &p, clampf(r->crouch, 0, 1)); }
    if (r->push > 0.001f) {
        float f = s->push_t < 0.0f ? 0.0f : clampf(s->push_t / (s->push_dur > 0.01f ? s->push_dur : SKATE_PUSH_TIME), 0.0f, 1.0f) * (float)(a->anims[POSE_PUSH].nframes - 1);
        animset_sample(a, POSE_PUSH, f, &p);
        skelframe_blend(&cur, &cur, &p, clampf(r->push, 0, 1));
    }
    if (r->air > 0.001f) {
        SkelFrame pa, pb;
        static_pose(a, POSE_AIR_A, &pa); static_pose(a, POSE_AIR_B, &pb);
        skelframe_blend(&p, &pa, &pb, 0.5f);
        skelframe_blend(&cur, &cur, &p, clampf(r->air, 0, 1));
    }
    if (r->grab > 0.001f) {
        static_pose(a, r->grab_pose, &p);
        skelframe_blend(&cur, &cur, &p, clampf(r->grab, 0, 1));
    }
    if (r->flip > 0.001f && r->flip_pose >= 0) {           /* the flip clip: skeleton and board play together */
        int nf = (int)a->anims[r->flip_pose].nframes;
        animset_sample(a, r->flip_pose, r->flip_p * (float)(nf - 1), &p);
        skelframe_blend(&cur, &cur, &p, clampf(r->flip, 0, 1));
    }
    if (r->grind > 0.001f) {
        static_pose(a, r->grind_pose, &p);
        skelframe_blend(&cur, &cur, &p, clampf(r->grind, 0, 1));
    }
    if (r->manual > 0.001f) {
        if (r->man_pose >= 0) {
            static_pose(a, r->man_pose, &p);
            skelframe_blend(&cur, &cur, &p, clampf(r->manual, 0, 1));
        } else {                                           /* arms out for balance */
            SkelFrame pa, pb;
            static_pose(a, POSE_AIR_A, &pa); static_pose(a, POSE_AIR_B, &pb);
            skelframe_blend(&p, &pa, &pb, 0.5f);
            skelframe_blend(&cur, &cur, &p, clampf(r->manual * r->man_arms, 0, 1));
        }
    }
    int bp = s->grounded ? tricks.bail_ground : tricks.bail_air[s->bail_dir > 0.0f ? 0 : 1];     /* falling: tipping to the left / right; then lying */
    if (r->bail > 0.001f) {
        static_pose(a, bp, &p);
        skelframe_blend(&cur, &cur, &p, clampf(r->bail, 0, 1));
    }
    *out = cur;
    animset_board(a, POSE_RIDE, 0.0f, board);
    if (r->grind > 0.001f) {                               /* the board follows the grind pose (a boardslide pose turns it across the rail) */
        BoardFrame gb;
        int nb = (int)a->anims[r->grind_pose].board_nframes;
        animset_board(a, r->grind_pose, (float)(nb - 1), &gb);
        boardframe_blend(board, board, &gb, clampf(r->grind, 0, 1));
        /* the grind type's own tilt of the board: a rotation about the board's local axes (x = width: pitch, z = long axis: roll, y = deck normal: yaw), faded in with the grind. The frame stores the
         * INVERSE rotation (the matrix is built from the conjugate), so the local turn goes in as conj(turn) * q. */
        float w = clampf(r->grind, 0, 1) * 0.0174533f * 0.5f;
        float qp[4] = { sinf(r->grind_tilt[0] * w), 0.0f, 0.0f, cosf(r->grind_tilt[0] * w) };
        float qr[4] = { 0.0f, 0.0f, sinf(r->grind_tilt[1] * w), cosf(r->grind_tilt[1] * w) };
        float qy[4] = { 0.0f, sinf(r->grind_tilt[2] * w), 0.0f, cosf(r->grind_tilt[2] * w) };
        float t1[4], t2[4], t3[4];
        qmul4(qy, qp, t1); qmul4(t1, qr, t2);
        t2[0] = -t2[0]; t2[1] = -t2[1]; t2[2] = -t2[2];                  /* conjugate */
        qmul4(t2, board->q, t3);
        for (int i = 0; i < 4; i++) board->q[i] = t3[i];
    }
    if (r->grab > 0.001f) {                                /* the board is in the hand */
        BoardFrame gb;
        int nb = (int)a->anims[r->grab_pose].board_nframes;
        animset_board(a, r->grab_pose, (float)(nb - 1), &gb);
        boardframe_blend(board, board, &gb, clampf(r->grab, 0, 1));
    }
    if (r->flip > 0.001f && r->flip_pose >= 0) {
        BoardFrame fb, last, base = *board;
        int nb = (int)a->anims[r->flip_pose].board_nframes;
        animset_board(a, r->flip_pose, r->flip_p * (float)(nb - 1), &fb);
        /* Shove-it like clips end with the board turned 180 degrees about its deck normal. The board is the same both ways round (its mesh matches itself under that turn to
         * 0.8 cm), so the riding frame it fades back to is the one that is nearest to where the clip ends: blending to the unturned frame spun the board half a turn at the end. */
        animset_board(a, r->flip_pose, (float)(nb - 1), &last);
        float alt[4];
        qmul4(Y180, board->q, alt);
        if (fabsf(dot4(last.q, alt)) > fabsf(dot4(last.q, board->q)))
            for (int i = 0; i < 4; i++) base.q[i] = alt[i];
        boardframe_blend(board, &base, &fb, clampf(r->flip, 0, 1));
    }
    if (r->manual > 0.001f && r->man_pose >= 0) {
        BoardFrame mb;
        int nb = (int)a->anims[r->man_pose].board_nframes;
        animset_board(a, r->man_pose, (float)(nb - 1), &mb);
        boardframe_blend(board, board, &mb, clampf(r->manual, 0, 1));
    }
    if (r->bail > 0.001f) {
        BoardFrame bb;
        int nb = (int)a->anims[bp].board_nframes;
        animset_board(a, bp, (float)(nb - 1), &bb);
        boardframe_blend(board, board, &bb, clampf(r->bail, 0, 1));
    }
}
