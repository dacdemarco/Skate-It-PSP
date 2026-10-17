/* Host tests for the moves added to src/skate.c: air spins (fakie landings), reverts, powerslides, body flips, nollie flips and wallrides - on a flat floor and a synthetic wall. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/skate.h"
#include "../src/coll.h"
#include "../src/trick.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define DT (1.0f / 60.0f)

static Coll world;
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&world, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&world, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&world, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }

static SkateWorld W = { NULL, g_ground, g_wall, g_surface, NULL };

/* an ollie from the ground: crouch 0.3 s, release. Returns the frame count at which the rider left the ground. */
static void step(Skate *s, SkateInput in, int n) { for (int i = 0; i < n; i++) skate_update(s, &in, &W, DT); }

static SkateInput blank(void) { SkateInput in; memset(&in, 0, sizeof(in)); return in; }

static void ollie(Skate *s, float charge_s, SkateInput extra_on_release)
{
    SkateInput in = blank();
    in.ollie_held = 1;
    step(s, in, (int)(charge_s * 60.0f));
    SkateInput rel = extra_on_release;
    rel.ollie_held = 0;
    skate_update(s, &rel, &W, DT);
}

int main(void)
{
    tricks_defaults();
    static float fv[24] = { -300, 0, -300, 300, 0, -300, 300, 0, 300, -300, 0, 300,         /* the floor */
                            10, 0, -100, 10, 0, 100, 10, 12, 100, 10, 12, -100 };             /* a wall at x = 10 */
    static CollPoly fp[2] = { { { 0, 1, 2, 3 }, 0 }, { { 4, 5, 6, 7 }, 0 } };
    if (!coll_build(&world, fv, 8, fp, 2, 8.0f)) return 1;

    /* ---- a half turn spin lands fakie ---- */
    {
        Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 5.0f;
        ollie(&s, 0.3f, blank());
        CHECK(!s.grounded, "popped");
        SkateInput in = blank(); in.spin = 1;
        step(&s, in, 12);                                           /* 0.2 s: 1.8 rad ... */
        in.spin = 0;
        step(&s, in, 90);                                           /* ... the rest of the half turn, and the landing */
        CHECK(s.grounded && s.bail_t <= 0.0f, "landed without a bail (grounded %d, bail %.2f)", s.grounded, s.bail_t);
        CHECK(s.speed < -3.0f, "a half turn lands fakie: speed %.2f", s.speed);
        CHECK(s.ev_spin == 1 && s.ev_spin_deg == 180 && s.ev_spin_dir == 1 && !s.ev_spin_fakie, "a BS 180 event (n %d deg %d dir %d)", s.ev_spin, s.ev_spin_deg, s.ev_spin_dir);
        CHECK(s.fwd[2] < -0.9f, "the board faces backwards (fwd z %.2f)", s.fwd[2]);
        CHECK(s.pos[2] > 2.0f, "and went on the way it was going (z %.2f)", s.pos[2]);
    }
    /* a frontside half turn, and from fakie */
    {
        Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 5.0f;
        ollie(&s, 0.3f, blank());
        SkateInput in = blank(); in.spin = -1;
        step(&s, in, 12); in.spin = 0; step(&s, in, 90);
        CHECK(s.grounded && s.ev_spin_dir == -1 && s.ev_spin_deg == 180, "an FS 180 (dir %d deg %d)", s.ev_spin_dir, s.ev_spin_deg);
        s.ev_spin = 0;
        ollie(&s, 0.3f, blank());                                    /* now rolling backwards: another half turn is a half-cab */
        in.spin = 1; step(&s, in, 12); in.spin = 0; step(&s, in, 90);
        CHECK(s.grounded && s.ev_spin == 1 && s.ev_spin_fakie && s.speed > 2.0f, "a spin from fakie lands forwards (fakie flag %d, speed %.2f)", s.ev_spin_fakie, s.speed);
    }
    /* a full turn */
    {
        Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 5.0f;
        ollie(&s, 0.28f, blank());
        SkateInput in = blank(); in.spin = 1;
        step(&s, in, 42);                                           /* 0.7 s of spinning = 360 degrees */
        in.spin = 0;
        step(&s, in, 60);
        CHECK(s.grounded && s.ev_spin_deg == 360 && s.bail_t <= 0.0f && s.speed > 3.0f, "a 360 spin (deg %d, bail %.2f, speed %.2f)", s.ev_spin_deg, s.bail_t, s.speed);
    }
    /* landing sideways is a bail */
    {
        Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 5.0f;
        ollie(&s, 0.3f, blank());
        step(&s, blank(), 15);                                       /* 0.25 s into the jump he starts spinning and is still at it when he lands */
        SkateInput in = blank(); in.spin = 1;
        int landed = 0;
        for (int i = 0; i < 120 && !landed; i++) { skate_update(&s, &in, &W, DT); landed = s.grounded; }
        CHECK(landed && s.bail_t > 0.0f && s.ev_spin == 0, "still spinning (about 1.5 half turns) when it lands: a bail (bail %.2f, spin events %d, angle %.2f)", s.bail_t, s.ev_spin, s.spin_ang);
    }

    /* ---- reverts ---- */
    {
        Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 5.0f;
        step(&s, blank(), 5);
        float z0 = s.pos[2];
        SkateInput in = blank(); in.revert = 1;
        skate_update(&s, &in, &W, DT);
        CHECK(s.ev_revert == 1 && s.speed < -4.0f && s.fwd[2] < -0.9f, "a revert to the left: fakie (speed %.2f, fwd z %.2f)", s.speed, s.fwd[2]);
        CHECK(s.vis_yaw < -2.0f && s.vis_yaw > -3.2f, "the drawn turn starts at a half turn the other way (vis_yaw %.2f)", s.vis_yaw);
        step(&s, blank(), 40);
        CHECK(fabsf(s.vis_yaw) < 0.1f, "and unwinds (vis_yaw %.2f)", s.vis_yaw);
        CHECK(s.pos[2] > z0 + 2.0f, "the travel went on the same way (z %.2f -> %.2f)", z0, s.pos[2]);
        Skate t; skate_init(&t, 0, 0, 0, 0); t.speed = 0.5f;
        in = blank(); in.revert = -1;
        skate_update(&t, &in, &W, DT);
        CHECK(t.ev_revert == 0, "too slow to revert");
    }

    /* ---- powerslide ---- */
    {
        Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 7.0f;
        Skate r; skate_init(&r, 0, 0, 0, 0); r.speed = 7.0f;
        SkateInput in = blank(); in.slide = 1;
        step(&s, in, 30); step(&r, blank(), 30);
        CHECK(s.sliding && s.ev_slide_start == 1 && s.vis_target > 1.5f, "sliding to the left (sliding %d, target %.2f)", s.sliding, s.vis_target);
        CHECK(s.speed < r.speed - 1.5f, "it scrubs speed (%.2f against %.2f rolling)", s.speed, r.speed);
        CHECK(s.vis_yaw > 1.0f, "the board is drawn sideways (vis_yaw %.2f)", s.vis_yaw);
        step(&s, blank(), 40);
        CHECK(!s.sliding && s.ev_slide_end == 1 && fabsf(s.vis_yaw) < 0.2f, "it ends when the input is let go (end events %d, vis_yaw %.2f)", s.ev_slide_end, s.vis_yaw);
        Skate t; skate_init(&t, 0, 0, 0, 0); t.speed = 2.0f;
        in = blank(); in.slide = -1;
        step(&t, in, 10);
        CHECK(!t.sliding, "too slow to start one");
    }

    /* ---- body flips and nollie flips ---- */
    {
        Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 4.0f;
        ollie(&s, 0.3f, blank());
        step(&s, blank(), 3);
        SkateInput in = blank(); in.flip = 1 + FLIP_BACKFLIP;
        skate_update(&s, &in, &W, DT);
        CHECK(s.flip == 1 + FLIP_BACKFLIP, "a back flip starts");
        float prev = 0.0f; int monotone = 1, steps = 0;
        in = blank();
        while (s.flip && steps < 200) { skate_update(&s, &in, &W, DT); if (s.flip && s.flip_roll > prev + 1e-4f) monotone = 0; if (s.flip) prev = s.flip_roll; steps++; }
        CHECK(monotone && prev < -1.0f, "the rider rolls backwards (roll %.2f)", prev);
        step(&s, in, 60);
        CHECK(s.grounded && s.ev_flip_done == 1 && s.ev_flip_type == FLIP_BACKFLIP && s.flip_roll == 0.0f, "completed (done %d type %d, roll %.2f)", s.ev_flip_done, s.ev_flip_type, s.flip_roll);
        Skate f; skate_init(&f, 0, 0, 0, 0); f.speed = 4.0f;
        ollie(&f, 0.3f, blank()); step(&f, blank(), 3);
        in = blank(); in.flip = 1 + FLIP_FRONTFLIP; skate_update(&f, &in, &W, DT);
        step(&f, blank(), 20);
        CHECK(f.flip_roll > 0.5f, "a frontflip rolls the other way (%.2f)", f.flip_roll);
    }
    {
        Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 4.0f;
        SkateInput rel = blank(); rel.flip_pre = 1 + FLIP_HARDFLIP; rel.nollie = 1;
        ollie(&s, 0.3f, rel);
        CHECK(s.flip == 1 + FLIP_HARDFLIP && s.flip_nollie == 1, "a nollie hardflip with the pop (flip %d nollie %d)", s.flip, s.flip_nollie);
        step(&s, blank(), 100);
        CHECK(s.grounded && s.ev_flip_done == 1 && s.ev_flip_nollie == 1, "completed as a nollie (nollie event %d)", s.ev_flip_nollie);
        Skate b; skate_init(&b, 0, 0, 0, 0); b.speed = 4.0f;
        rel = blank(); rel.flip_pre = 1 + FLIP_BACKFLIP; rel.nollie = 1;
        ollie(&b, 0.3f, rel);
        CHECK(b.flip == 1 + FLIP_BACKFLIP && b.flip_nollie == 0, "a body flip has no nollie version");
    }

    /* ---- wallride ---- */
    {
        Skate s; skate_init(&s, 0, 0, 0, 0);
        s.grounded = 0; s.pos[0] = 9.2f; s.pos[1] = 1.5f; s.pos[2] = 0.0f; s.vel[0] = 2.5f; s.vel[1] = 1.0f; s.vel[2] = 7.0f;
        s.fwd[0] = 0.0f; s.fwd[1] = 0.0f; s.fwd[2] = 1.0f; s.speed = 7.0f;
        SkateInput in = blank();                                     /* no button: the wall is ridden by itself, like a rail */
        int got = 0, steps = 0; float ymin = 9e9f, ymax = -9e9f, xmax = -9e9f;
        for (; steps < 160; steps++) {
            skate_update(&s, &in, &W, DT);
            if (s.wallriding) { got = 1; if (s.pos[1] < ymin) ymin = s.pos[1]; if (s.pos[1] > ymax) ymax = s.pos[1]; if (s.pos[0] > xmax) xmax = s.pos[0]; }
            if (got && !s.wallriding) break;
        }
        CHECK(got && s.ev_wall_start == 1, "a glancing wall at speed: a wallride, no button needed (started %d)", s.ev_wall_start);
        CHECK(xmax < 10.0f && xmax > 9.7f, "it stays on the near side of the wall, close to it (x max %.2f)", xmax);
        CHECK(ymax - ymin < 1.3f && ymin > 0.3f, "and sinks only slowly (y %.2f .. %.2f)", ymin, ymax);
        CHECK(s.wall_side > 0.0f || s.wall_side < 0.0f, "side known");
        CHECK(!s.wallriding && s.ev_wall_end == 1, "and ends (end events %d)", s.ev_wall_end);
        CHECK(s.pos[2] > 3.0f, "after travelling along the wall (z %.2f)", s.pos[2]);
        /* too slow along the wall: just a bump */
        Skate t; skate_init(&t, 0, 0, 0, 0);
        t.grounded = 0; t.pos[0] = 9.2f; t.pos[1] = 1.5f; t.pos[2] = 0.0f; t.vel[0] = 0.8f; t.vel[1] = 1.0f; t.vel[2] = 2.0f; t.fwd[2] = 1.0f;
        step(&t, blank(), 60);
        CHECK(!t.wallriding && t.ev_wall_start == 0, "too slow: no wallride");
        /* head on is not one either */
        Skate u; skate_init(&u, 0, 0, 0, 0);
        u.grounded = 0; u.pos[0] = 9.2f; u.pos[1] = 1.5f; u.pos[2] = 0.0f; u.vel[0] = 7.0f; u.vel[1] = 1.0f; u.vel[2] = 0.5f; u.fwd[0] = 1.0f;
        in = blank();
        step(&u, in, 60);
        CHECK(u.ev_wall_start == 0, "head on: no wallride");
        /* a jump off the wall */
        Skate v; skate_init(&v, 0, 0, 0, 0);
        v.grounded = 0; v.pos[0] = 9.2f; v.pos[1] = 1.5f; v.pos[2] = 0.0f; v.vel[0] = 2.5f; v.vel[1] = 1.0f; v.vel[2] = 7.0f; v.fwd[2] = 1.0f; v.speed = 7.0f;
        in = blank(); in.ollie_held = 1;
        int on = 0;
        for (int i = 0; i < 60 && !on; i++) { skate_update(&v, &in, &W, DT); on = v.wallriding; }
        in.ollie_held = 0;
        skate_update(&v, &in, &W, DT);
        CHECK(on && !v.wallriding && v.vel[0] < -1.0f && v.vel[1] > 0.3f, "letting go of the ollie button kicks off the wall (on %d, vx %.2f, vy %.2f)", on, v.vel[0], v.vel[1]);
    }

    /* ---- the tuning (Options > Gravity): ollie power, gravity and push speed scale the defaults ---- */
    {
        float apex[3], air[3];
        for (int k = 0; k < 3; k++) {
            skate_tune.gravity = k == 1 ? 0.6f : 1.0f; skate_tune.ollie = k == 2 ? 1.3f : 1.0f;
            Skate s; skate_init(&s, 0, 0, 0, 0); s.speed = 4.0f;
            ollie(&s, 0.3f, blank());
            apex[k] = 0.0f; air[k] = 0.0f;
            SkateInput none = blank();
            for (int i = 0; i < 300 && !s.grounded; i++) { skate_update(&s, &none, &W, DT); if (s.pos[1] > apex[k]) apex[k] = s.pos[1]; air[k] += DT; }
        }
        skate_tune.gravity = skate_tune.ollie = 1.0f;
        CHECK(apex[1] > apex[0] * 1.4f && air[1] > air[0] * 1.2f, "less gravity: higher and longer (%.2f / %.2f m, %.2f / %.2f s)", apex[0], apex[1], air[0], air[1]);
        CHECK(apex[2] > apex[0] * 1.4f, "more ollie power: higher (%.2f -> %.2f m)", apex[0], apex[2]);
        Skate p; skate_init(&p, 0, 0, 0, 0); p.speed = 0.5f;
        SkateInput in = blank(); in.push = 1; skate_update(&p, &in, &W, DT); step(&p, blank(), 70);
        float v1 = p.speed;
        skate_tune.push = 1.5f;
        Skate q; skate_init(&q, 0, 0, 0, 0); q.speed = 0.5f;
        skate_update(&q, &in, &W, DT); step(&q, blank(), 70);
        skate_tune.push = 1.0f;
        CHECK(q.speed > v1 * 1.3f, "push speed 150%%: a faster push (%.2f -> %.2f)", v1, q.speed);
    }

    printf(fails ? "%d FAILURES\n" : "all move tests passed\n", fails);
    return fails != 0;
}
