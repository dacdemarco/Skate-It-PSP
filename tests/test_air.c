/* Host tests for the air tricks (flips and grabs): src/skate.c, src/score.c, src/trick.c on a flat floor with one rail. */
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
#define DT (1.0f / 60.0f)

/* an ollie: charge for `charge_frames`, release; returns with the rider just off the ground */
static void pop(Skate *s, int charge_frames)
{
    SkateInput hold; memset(&hold, 0, sizeof(hold)); hold.ollie_held = 1;
    SkateInput idle; memset(&idle, 0, sizeof(idle));
    for (int i = 0; i < charge_frames; i++) skate_update(s, &hold, &w, DT);
    skate_update(s, &idle, &w, DT);
}

typedef struct { int air_frames, flip_done, bail, grab_end; float flip_p_at_land; } Jump;

/* a jump with a flip requested `flip_at` frames after take-off (-1 = none) and a grab held from `grab_from` to `grab_to` (-1 = none) */
static Jump jump(int type, int flip_at, int grab_type, int grab_from, int grab_to, Skate *out)
{
    Skate s; skate_init(&s, 0, 0, 0, 0.0f); s.speed = 4.0f;
    pop(&s, 12);
    Jump j = { 0, 0, 0, 0, 0 };
    SkateInput in; memset(&in, 0, sizeof(in));
    int i = 0;
    while (!s.grounded && i < 200) {
        in.flip = (flip_at == i) ? type + 1 : 0;
        in.grab = (grab_from >= 0 && i >= grab_from && i < grab_to) ? grab_type + 1 : 0;
        float p = s.flip ? s.flip_t / s.flip_dur : 0.0f;
        skate_update(&s, &in, &w, DT);
        if (s.grounded) j.flip_p_at_land = p;
        i++;
    }
    for (int k = 0; k < 3; k++) skate_update(&s, &in, &w, DT);
    j.air_frames = i; j.flip_done = s.ev_flip_done; j.bail = s.ev_bail; j.grab_end = s.ev_grab_end;
    if (out) *out = s;
    return j;
}

int main(void)
{
    if (!coll_build(&flat, flatv, 4, flatp, 1, 4.0f)) return 1;
    tricks_defaults();
    Skate s;

    Jump base = jump(0, -1, 0, -1, -1, NULL);
    printf("  a normal jump: %d frames (%.2f s) in the air; flip %d ms\n", base.air_frames, base.air_frames / 60.0f, tricks.flip[0].rate);
    CHECK(base.air_frames > 30 && base.flip_done == 0 && base.bail == 0, "plain jump: no tricks, no bail");

    /* a flip started right after take-off is finished before landing: it counts, no bail */
    Jump a = jump(0, 3, 0, -1, -1, &s);
    CHECK(a.flip_done == 1 && a.bail == 0 && s.ev_flip_type == 0, "an early flip completes (done %d, bail %d)", a.flip_done, a.bail);
    /* one started so late that less than 60 % is done at landing is a bail */
    int late = base.air_frames - 12;
    Jump b = jump(0, late, 0, -1, -1, &s);
    CHECK(b.bail == 1 && b.flip_done == 0 && s.bail_t > 0.0f, "a flip started %d frames before landing is a bail (progress %.2f at landing)", 12, b.flip_p_at_land);
    /* started so that about 80 % is done at landing: counts */
    int mid = base.air_frames - (int)(0.8f * tricks.flip[0].rate * 0.06f);
    Jump c = jump(0, mid, 0, -1, -1, NULL);
    CHECK(c.flip_done == 1 && c.bail == 0, "a flip landed at %.0f %% counts (done %d, bail %d)", 100.0f * c.flip_p_at_land, c.flip_done, c.bail);
    /* every flip type works and reports its type */
    for (int t = 0; t < NFLIP_TYPES; t++) {
        Jump j = jump(t, 3, 0, -1, -1, &s);
        CHECK(j.flip_done == 1 && s.ev_flip_type == t, "flip type %d completes and reports itself (type %d)", t, s.ev_flip_type);
    }
    /* a flip cannot start on the ground or when none is requested */
    skate_init(&s, 0, 0, 0, 0.0f); s.speed = 4.0f;
    SkateInput in; memset(&in, 0, sizeof(in)); in.flip = 1;
    skate_update(&s, &in, &w, DT);
    CHECK(s.flip == 0, "no flip on the ground");

    /* grabs: held from the apex, released before landing */
    Jump g1 = jump(0, -1, 2, 10, base.air_frames - 8, &s);
    CHECK(g1.grab_end == 1 && g1.bail == 0, "a grab held and released: one start, one end (ends %d)", g1.grab_end);
    {
        Skate q; skate_init(&q, 0, 0, 0, 0.0f); q.speed = 4.0f; pop(&q, 12);
        SkateInput gi; memset(&gi, 0, sizeof(gi));
        gi.grab = 3;
        for (int i = 0; i < 6; i++) skate_update(&q, &gi, &w, DT);
        CHECK(q.grab == 3 && q.ev_grab_start == 1 && q.ev_grab_type == 2, "grab type 2 started (grab %d, type %d)", q.grab, q.ev_grab_type);
        skate_update(&q, &gi, &w, DT);
        CHECK(q.grab == 3 && q.ev_grab_switch == 0, "the same request keeps the grab (no switch event)");
        gi.grab = 5;                                       /* a flick while held asks for another variant: the grab changes and says so */
        skate_update(&q, &gi, &w, DT);
        CHECK(q.grab == 5 && q.ev_grab_switch == 1 && q.ev_grab_type == 4, "a variant change while held (grab %d, switch events %d, type %d)", q.grab, q.ev_grab_switch, q.ev_grab_type);
        gi.grab = 3;                                       /* (back to the first one for the rest of this test) */
        skate_update(&q, &gi, &w, DT);
        float t0 = q.grab_time;
        for (int i = 0; i < 10; i++) skate_update(&q, &gi, &w, DT);
        CHECK(q.grab_time > t0 + 0.1f, "grab time runs");
        gi.flip = 1; skate_update(&q, &gi, &w, DT);
        CHECK(q.flip == 0, "no flip while grabbing");
        gi.flip = 0;
        for (int i = 0; i < 200 && !q.grounded; i++) skate_update(&q, &gi, &w, DT);          /* land with the grab still held */
        CHECK(q.grounded && q.grab == 0 && q.ev_grab_end == 1 && q.ev_bail == 0, "landing with a grab held: it ends, no bail (grab %d, ends %d)", q.grab, q.ev_grab_end);
    }
    {
        Skate q; skate_init(&q, 0, 0, 0, 0.0f); q.speed = 4.0f; pop(&q, 12);
        SkateInput fi; memset(&fi, 0, sizeof(fi));
        for (int i = 0; i < 5; i++) skate_update(&q, &fi, &w, DT);
        fi.flip = 2; skate_update(&q, &fi, &w, DT); fi.flip = 0;
        fi.grab = 1; skate_update(&q, &fi, &w, DT);
        CHECK(q.flip == 2 && q.grab == 0, "no grab while flipping");
    }

    /* a flip from the ground: hold it ready (it cancels the push, charges the pop), let go = pop and flip in one motion */
    {
        Skate q; skate_init(&q, 0, 0, 0, 0.0f); q.speed = 4.0f;
        SkateInput gi; memset(&gi, 0, sizeof(gi));
        gi.push = 1; skate_update(&q, &gi, &w, DT); gi.push = 0;
        for (int i = 0; i < 4; i++) skate_update(&q, &gi, &w, DT);
        CHECK(q.push_t >= 0.0f, "a push is under way");
        gi.flip_pre = 3; gi.ollie_held = 1;                  /* held ready: flip type 2 */
        skate_update(&q, &gi, &w, DT);
        CHECK(q.push_t < 0.0f && q.push_boost == 0.0f, "holding a flip ready cancels the push");
        float v0 = q.speed;
        for (int i = 0; i < 12; i++) skate_update(&q, &gi, &w, DT);
        CHECK(q.speed <= v0 + 0.01f && q.grounded && q.flip == 0, "held: no speed gained, still on the ground, no flip yet (speed %.2f -> %.2f)", v0, q.speed);
        gi.ollie_held = 0;                                   /* let go: flip_pre is still set for this frame */
        skate_update(&q, &gi, &w, DT);
        CHECK(!q.grounded && q.ev_ollie == 1 && q.flip == 3 && q.vel[1] > 2.5f, "letting go pops and flips together (grounded %d, flip %d, vy %.2f)", q.grounded, q.flip, q.vel[1]);
        gi.flip_pre = 0;
        for (int i = 0; i < 200 && !q.grounded; i++) skate_update(&q, &gi, &w, DT);
        for (int i = 0; i < 3; i++) skate_update(&q, &gi, &w, DT);
        CHECK(q.grounded && q.ev_flip_done == 1 && q.ev_flip_type == 2 && q.ev_bail == 0, "and the flip completes in the one jump (done %d, type %d, bail %d)", q.ev_flip_done, q.ev_flip_type, q.ev_bail);
        /* a very short hold still pops (the held trick has a minimum charge) */
        skate_init(&q, 0, 0, 0, 0.0f); q.speed = 4.0f;
        gi.flip_pre = 1; gi.ollie_held = 1; skate_update(&q, &gi, &w, DT);
        gi.ollie_held = 0; skate_update(&q, &gi, &w, DT);
        CHECK(!q.grounded && q.flip == 1, "a short hold pops too");
        /* no flip_pre: an ordinary ollie */
        skate_init(&q, 0, 0, 0, 0.0f); q.speed = 4.0f;
        gi.flip_pre = 0; gi.ollie_held = 1;
        for (int i = 0; i < 12; i++) skate_update(&q, &gi, &w, DT);
        gi.ollie_held = 0; skate_update(&q, &gi, &w, DT);
        CHECK(!q.grounded && q.flip == 0, "without a flip held ready it is a plain ollie");
        /* a tap on the push (no flip_pre) still pushes: the light, quick push; the button held through the foot plant: the strong, slower one */
        skate_init(&q, 0, 0, 0, 0.0f); q.speed = 0.5f;
        gi.ollie_held = 0; gi.push = 1; skate_update(&q, &gi, &w, DT); gi.push = 0;
        for (int i = 0; i < 70; i++) skate_update(&q, &gi, &w, DT);
        float tap = q.speed;
        Skate h; skate_init(&h, 0, 0, 0, 0.0f); h.speed = 0.5f;
        gi.push = 1; gi.push_held = 1; skate_update(&h, &gi, &w, DT); gi.push = 0;
        int strong = 0; float dur = 0.0f;
        for (int i = 0; i < 70; i++) { skate_update(&h, &gi, &w, DT); if (i == 20) { gi.push_held = 0; strong = h.push_strong; dur = h.push_dur; } }
        printf("  a push from 0.5 m/s: tap %.2f m/s, held %.2f m/s after 1.2 s", tap, h.speed); printf("%c", 10);
        CHECK(tap > 1.9f && !q.push_strong, "a tap gives speed (%.2f)", tap);
        CHECK(strong && dur > SKATE_PUSH_TIME + 0.1f && h.speed > tap + 1.0f, "the button held when the foot plants: the strong, slower push (strong %d, %.2f s, %.2f m/s)", strong, dur, h.speed);
        CHECK(h.push_t < 0.0f || h.push_t > 0.9f, "the strong push lasts longer (push_t %.2f after 1.2 s)", h.push_t);
    }

    /* a rail grabbed in the middle of a flip: the flip is cancelled without a bail (or counts when it has gone far enough) */
    {
        uint32_t tab[2] = { 0, 2 };
        float pts[6] = { 0, 1.0f, -3, 0, 1.0f, 30 };
        Rails rl;
        if (!rails_build(&rl, tab, 1, pts)) return 1;
        SkateWorld wr = { NULL, g_ground, g_wall, g_surface, &rl };
        for (int late_flip = 0; late_flip < 2; late_flip++) {
            Skate q; skate_init(&q, 0.1f, 1.7f, -2.0f, 0.0f); q.grounded = 0; q.vel[2] = 5.0f; q.vel[1] = 0.0f; q.air_t = 0.2f;     /* (1.7: still above the lock-on reach of the rail at 1.0) */
            SkateInput ri; memset(&ri, 0, sizeof(ri));
            ri.flip = 1;
            skate_update(&q, &ri, &wr, DT); ri.flip = 0;
            CHECK(q.flip == 1, "flip started in the air above the rail");
            if (late_flip) for (int i = 0; i < 20; i++) { skate_update(&q, &ri, &wr, DT); if (q.grinding) break; }
            int bailed = 0;
            for (int i = 0; i < 30 && !q.grinding; i++) { skate_update(&q, &ri, &wr, DT); bailed |= q.ev_bail; }
            CHECK(q.grinding && !bailed && q.flip == 0, "locking on to a rail mid-flip: no bail, the flip is over (grinding %d, flip %d)", q.grinding, q.flip);
        }
        rails_free(&rl);
    }

    /* scoring: flips and grabs are tricks of a combo */
    {
        Score sc; score_init(&sc);
        Skate q; skate_init(&q, 0, 0, 0, 0.0f); q.speed = 4.0f; pop(&q, 12);
        score_update(&sc, &q, DT);
        SkateInput ti; memset(&ti, 0, sizeof(ti));
        for (int i = 0; i < 3; i++) { skate_update(&q, &ti, &w, DT); score_update(&sc, &q, DT); }
        ti.flip = 3;                                       /* type 2 */
        skate_update(&q, &ti, &w, DT); score_update(&sc, &q, DT); ti.flip = 0;
        for (int i = 0; i < 28; i++) { skate_update(&q, &ti, &w, DT); score_update(&sc, &q, DT); }
        CHECK(sc.combo_active && sc.combo_n == 1 && sc.cur_cls == TRICK_FLIP && score_combo_points(&sc) == tricks.flip[2].base, "a completed flip: %ld points (%s)", score_combo_points(&sc), score_trick_name(&sc));
        ti.grab = 1;
        for (int i = 0; i < 5; i++) { skate_update(&q, &ti, &w, DT); score_update(&sc, &q, DT); }
        CHECK(sc.combo_n == 2 && sc.cur_cls == TRICK_GRAB && score_combo_mult(&sc) == 2, "then a grab joins the combo (x%d)", score_combo_mult(&sc));
        float t_grab = 0.0f;
        while (!q.grounded && t_grab < 2.0f) { skate_update(&q, &ti, &w, DT); score_update(&sc, &q, DT); t_grab += DT; }
        ti.grab = 0;
        for (int i = 0; i < 60 && sc.total == 0; i++) { skate_update(&q, &ti, &w, DT); score_update(&sc, &q, DT); }
        long min_expect = (tricks.flip[2].base + tricks.grab[0].base) * 2;
        CHECK(sc.total >= min_expect && sc.msg == SCORE_MSG_BANK && sc.msg_n == 2, "banked after landing: %ld (at least %ld)", sc.total, min_expect);
        /* a flip that ends in a bail loses the combo */
        long before = sc.total;
        skate_init(&q, 0, 0, 0, 0.0f); q.speed = 4.0f; pop(&q, 12);
        SkateInput bi; memset(&bi, 0, sizeof(bi));
        int n = 0;
        while (!q.grounded && n < 200) { bi.flip = (n == 3) ? 1 : 0; skate_update(&q, &bi, &w, DT); score_update(&sc, &q, DT); n++; }
        /* an early flip completes; make the second one a late one */
        skate_init(&q, 0, 0, 0, 0.0f); q.speed = 4.0f; pop(&q, 12);
        n = 0;
        while (!q.grounded && n < 200) { bi.flip = (n == base.air_frames - 10) ? 1 : 0; skate_update(&q, &bi, &w, DT); score_update(&sc, &q, DT); n++; }
        skate_update(&q, &bi, &w, DT); score_update(&sc, &q, DT);
        CHECK(sc.msg == SCORE_MSG_BAIL && !sc.combo_active, "a flip landed too early loses the combo");
        CHECK(sc.total >= before, "banked points stay");
    }

    printf(fails ? "%d FAILURES\n" : "all air trick tests passed\n", fails);
    return fails != 0;
}
