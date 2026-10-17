/* Host tests for manuals (src/skate.c, src/score.c) on a flat floor. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/skate.h"
#include "../src/coll.h"
#include "../src/rails.h"
#include "../src/trick.h"
#include "../src/score.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static Coll flat;
static float flatv[12] = { -300, 0, -300, 300, 0, -300, 300, 0, 300, -300, 0, 300 };
static CollPoly flatp[1] = { { { 0, 1, 2, 3 }, 0 } };
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&flat, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&flat, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&flat, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }
static SkateWorld w = { NULL, g_ground, g_wall, g_surface, NULL };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
#define DT (1.0f / 60.0f)

static void rolling(Skate *s, float speed) { skate_init(s, 0, 0, 0, 0.0f); s->speed = speed; }

/* run up to `secs` seconds with a controller; returns frames the manual lasted; flags: bail seen */
typedef struct { int frames, bailed, ended, started; float pitch_max; } MRun;
static MRun run_manual(int type, int mode, float speed, float secs)       /* mode 0: no input, 1: perfect control, 2: reacts 0.2 s late */
{
    Skate s; rolling(&s, speed);
    SkateInput in; memset(&in, 0, sizeof(in));
    float hist[32] = { 0 }; int hi = 0;
    MRun r = { 0, 0, 0, 0, 0 };
    in.manual = type;
    skate_update(&s, &in, &w, DT);
    in.manual = 0;
    r.started = s.manual != 0;
    for (int i = 0; i < (int)(secs * 60.0f) && !r.bailed; i++) {
        float sgn = type == 1 ? 1.0f : -1.0f;
        float cmd = clampf(sgn * (2.5f * s.lean + 0.8f * s.lean_v), -1.0f, 1.0f);     /* stick forwards (+) reduces the lean of a manual */
        hist[hi & 31] = cmd; hi++;
        float late = hi > 12 ? hist[(hi - 13) & 31] : 0.0f;
        in.tilt = mode == 1 ? cmd : mode == 2 ? late : 0.0f;
        skate_update(&s, &in, &w, DT);
        if (s.manual) { r.frames++; if (fabsf(s.pitch) > r.pitch_max) r.pitch_max = fabsf(s.pitch); }
        if (s.ev_bail) r.bailed = 1;
        if (!s.manual && !r.bailed) { r.ended = 1; break; }
    }
    return r;
}

int main(void)
{
    if (!coll_build(&flat, flatv, 4, flatp, 1, 4.0f)) return 1;
    tricks_defaults();
    SkateInput in; memset(&in, 0, sizeof(in));
    Skate s;

    /* starting: a press while rolling fast enough on a flat floor, not too slow, not while charging an ollie */
    rolling(&s, 4.0f); in.manual = 1; skate_update(&s, &in, &w, DT);
    CHECK(s.manual == 1 && s.ev_manual_start == 1, "a press while rolling starts a manual");
    rolling(&s, 4.0f); in.manual = 2; skate_update(&s, &in, &w, DT);
    CHECK(s.manual == 2, "type 2 = nose manual");
    rolling(&s, 0.8f); in.manual = 1; skate_update(&s, &in, &w, DT);
    CHECK(s.manual == 0, "too slow: no manual");
    rolling(&s, 4.0f); in.manual = 1; in.ollie_held = 1; skate_update(&s, &in, &w, DT); in.ollie_held = 0;
    CHECK(s.manual == 0, "not while the ollie button is down");
    s.grounded = 0; s.vel[1] = 1.0f; in.manual = 1; skate_update(&s, &in, &w, DT);
    CHECK(s.manual == 0, "not in the air");

    /* the board tilts: nose up for a manual, tail up for a nose manual, about the wheels it stands on */
    rolling(&s, 4.0f); in.manual = 1; skate_update(&s, &in, &w, DT); in.manual = 0;
    for (int i = 0; i < 30; i++) skate_update(&s, &in, &w, DT);
    CHECK(s.pitch > 0.2f && s.pitch < 0.5f, "manual pitch %.2f (nose up)", s.pitch);
    float m[16]; skate_body_matrix(&s, m);
    CHECK(m[9] > 0.15f, "the body's forward axis points up (%.2f)", m[9]);
    float rear_before[3] = { s.pos[0] - s.fwd[0] * 0.4f, s.pos[1] - s.fwd[1] * 0.4f, s.pos[2] - s.fwd[2] * 0.4f };
    float rear_after[3] = { m[12] - m[8] * 0.4f, m[13] - m[9] * 0.4f, m[14] - m[10] * 0.4f };
    CHECK(fabsf(rear_before[0] - rear_after[0]) + fabsf(rear_before[1] - rear_after[1]) + fabsf(rear_before[2] - rear_after[2]) < 0.01f, "the rear wheels stay where they were");
    rolling(&s, 4.0f); in.manual = 2; skate_update(&s, &in, &w, DT); in.manual = 0;
    for (int i = 0; i < 30; i++) skate_update(&s, &in, &w, DT);
    skate_body_matrix(&s, m);
    CHECK(s.pitch < -0.2f && m[9] < -0.15f, "nose manual: tail up (pitch %.2f)", s.pitch);

    /* balance: no input ends in a bail, a player who keeps it centred never bails */
    for (int type = 1; type <= 2; type++) {
        MRun r0 = run_manual(type, 0, 7.0f, 12.0f), r1 = run_manual(type, 1, 7.0f, 12.0f), r2 = run_manual(type, 2, 7.0f, 12.0f);
        printf("  manual type %d: no input bails after %.1f s; perfect lasted %.1f s (bail %d); late lasted %.1f s (bail %d)\n", type, r0.frames / 60.0f, r1.frames / 60.0f, r1.bailed, r2.frames / 60.0f, r2.bailed);
        CHECK(r0.started && r0.bailed && r0.frames > 30 && r0.frames < 480, "type %d with no input: bail after %.1f s", type, r0.frames / 60.0f);
        CHECK(!r1.bailed && r1.ended && r1.frames > 300, "type %d with perfect balancing: no bail, ends when the speed runs out (%.1f s, bailed %d)", type, r1.frames / 60.0f, r1.bailed);
        CHECK(!r2.bailed && r2.frames > 200, "type %d reacting 0.2 s late: no bail (%.1f s)", type, r2.frames / 60.0f);
    }

    /* the manual is over when pressed again, when the rider pops out of it, and when the speed is gone */
    rolling(&s, 5.0f); in.manual = 1; skate_update(&s, &in, &w, DT); in.manual = 0;
    for (int i = 0; i < 20; i++) skate_update(&s, &in, &w, DT);
    in.manual = 1; skate_update(&s, &in, &w, DT); in.manual = 0;
    CHECK(s.manual == 0 && s.ev_manual_end >= 1, "a second press ends it");
    rolling(&s, 5.0f); in.manual = 1; skate_update(&s, &in, &w, DT); in.manual = 0; s.ev_manual_end = 0;
    for (int i = 0; i < 20; i++) skate_update(&s, &in, &w, DT);
    SkateInput hold = { 0, 0, 1, 0, 0, 0, 0 , 0, 0, 0};
    for (int i = 0; i < 25; i++) skate_update(&s, &hold, &w, DT);                 /* charge */
    for (int i = 0; i < 3; i++) skate_update(&s, &in, &w, DT);                    /* release */
    CHECK(s.manual == 0 && !s.grounded && s.ev_manual_end == 1 && s.ev_ollie == 1, "an ollie pops out of the manual (manual %d, grounded %d, ended %d)", s.manual, s.grounded, s.ev_manual_end);
    rolling(&s, 4.0f); in.manual = 1; skate_update(&s, &in, &w, DT); in.manual = 0;
    SkateInput brake = { 0, 0, 0, 1, 0, 0, 0.8f , 0, 0, 0};
    for (int i = 0; i < 90 && s.manual; i++) skate_update(&s, &brake, &w, DT);
    CHECK(s.manual == 0 && s.ev_bail == 0, "braking to a stop ends it without a bail (lean %.2f)", s.lean);

    /* a bail: thrown off, no control, lands and gets up */
    {
        MRun r = run_manual(1, 0, 6.0f, 12.0f); (void)r;
        rolling(&s, 6.0f); in.manual = 1; skate_update(&s, &in, &w, DT); in.manual = 0;
        int bf = -1;
        for (int i = 0; i < 900 && bf < 0; i++) { skate_update(&s, &in, &w, DT); if (s.ev_bail) bf = i; }
        CHECK(bf >= 0 && s.bail_t > 1.0f && s.manual == 0 && !s.grounded, "the manual bail throws the rider off (bail_t %.2f)", s.bail_t);
        SkateInput steer = { 1.0f, 1, 1, 0, 0, 0, 0 , 0, 0, 0};
        float h0 = s.heading;
        for (int i = 0; i < 60; i++) skate_update(&s, &steer, &w, DT);
        CHECK(s.bail_t > 0.0f && fabsf(s.heading - h0) < 0.05f, "no steering while fallen (heading change %.2f, bail_t %.2f)", s.heading - h0, s.bail_t);
        for (int i = 0; i < 400; i++) skate_update(&s, &in, &w, DT);
        CHECK(s.grounded && s.bail_t == 0.0f && fabsf(s.speed) < 0.3f, "then it lands, stops and is released (speed %.2f)", s.speed);
    }

    /* the stick that was held at the start does not count until it has been centred */
    rolling(&s, 4.0f); in.manual = 2; in.tilt = 1.0f; skate_update(&s, &in, &w, DT); in.manual = 0;
    CHECK(s.manual == 2 && !s.lean_armed, "nose manual started with the stick up: not armed");
    in.tilt = 0.0f; skate_update(&s, &in, &w, DT);
    CHECK(s.lean_armed, "armed once centred");

    /* scoring: a manual is a trick of its own, chains with other tricks, banks after rolling */
    {
        Score sc; score_init(&sc);
        rolling(&s, 7.0f);
        float t = 0.0f; in.tilt = 0;
        in.manual = 1; skate_update(&s, &in, &w, DT); score_update(&sc, &s, DT); in.manual = 0;
        CHECK(sc.combo_active && sc.in_grind && sc.cur_cls == TRICK_MANUAL && strcmp(score_trick_name(&sc), "MANUAL") == 0, "a manual opens a combo");
        for (int i = 0; i < 120; i++) {
            in.tilt = clampf(2.5f * s.lean + 0.8f * s.lean_v, -1.0f, 1.0f);
            skate_update(&s, &in, &w, DT); score_update(&sc, &s, DT); t += DT;
        }
        long expect = tricks.manual[0].base + (long)(tricks.manual[0].rate * 2.0f);
        CHECK(labs(score_combo_points(&sc) - expect) < 20, "2 s of manual: %ld (expected about %ld)", score_combo_points(&sc), expect);
        in.manual = 1; skate_update(&s, &in, &w, DT); score_update(&sc, &s, DT); in.manual = 0;          /* press again: over */
        CHECK(!sc.in_grind && sc.combo_active, "ended: the combo waits to be banked");
        for (int i = 0; i < 70 && sc.total == 0; i++) { skate_update(&s, &in, &w, DT); score_update(&sc, &s, DT); }
        CHECK(sc.total > 150 && !sc.combo_active, "banked after rolling: %ld", sc.total);
        /* a bail loses it */
        rolling(&s, 6.0f); in.manual = 1; skate_update(&s, &in, &w, DT); score_update(&sc, &s, DT); in.manual = 0;
        long before = sc.total;
        in.tilt = 0.0f;
        for (int i = 0; i < 600 && sc.msg != SCORE_MSG_BAIL; i++) { skate_update(&s, &in, &w, DT); score_update(&sc, &s, DT); }
        CHECK(sc.msg == SCORE_MSG_BAIL && sc.total == before && !sc.combo_active, "a manual bail loses the combo");
    }

    printf(fails ? "%d FAILURES\n" : "all manual tests passed\n", fails);
    return fails != 0;
}
