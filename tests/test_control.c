/* Host tests for src/control.c: scripted stick movements -> the SkateInput, then the same scripts through the real physics on a flat floor. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/control.h"
#include "../src/skate.h"
#include "../src/coll.h"
#include "../src/trick.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define DT (1.0f / 60.0f)
#define MICRO_STEER_T 0.4f

typedef struct { Control c; unsigned held_prev; CtlFrame f; SkateInput in; } T;

static void t_init(T *t)
{
    memset(t, 0, sizeof(*t));
    control_init(&t->c);
    t->f.grounded = 1; t->f.speed = 4.0f;
}

static SkateInput tick(T *t, float ax, float ay, unsigned held)
{
    t->f.ax = ax; t->f.ay = ay; t->f.held = held; t->f.pressed = held & ~t->held_prev; t->held_prev = held;
    control_update(&t->c, &t->f, DT, &t->in);
    return t->in;
}

/* a flick: the stick snaps from where it is to (x, y) in `frames` frames and stays there for `hold` frames */
static void flick(T *t, float x0, float y0, float x, float y, int frames, int hold, unsigned held)
{
    for (int i = 1; i <= frames; i++) tick(t, x0 + (x - x0) * i / frames, y0 + (y - y0) * i / frames, held);
    for (int i = 0; i < hold; i++) tick(t, x, y, held);
}

static void settle(T *t, int n, unsigned held) { for (int i = 0; i < n; i++) tick(t, 0.0f, 0.0f, held); }


/* ---- the physics on a flat floor ---- */
static Coll flat;
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&flat, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&flat, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&flat, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }

typedef struct { Skate s; Control c; unsigned held_prev; const SkateWorld *w; } Sim;

static void sim_init(Sim *m, const SkateWorld *w, float speed)
{
    memset(m, 0, sizeof(*m));
    skate_init(&m->s, 0, 0, 0, 0);
    m->s.speed = speed;
    control_init(&m->c);
    m->w = w;
}

/* n frames with the stick at (ax, ay) and the buttons held */
static void sim_run(Sim *m, int n, float ax, float ay, unsigned held)
{
    for (int i = 0; i < n; i++) {
        CtlFrame f;
        memset(&f, 0, sizeof(f));
        f.ax = ax; f.ay = ay; f.held = held; f.pressed = held & ~m->held_prev; m->held_prev = held;
        f.grounded = m->s.grounded; f.grinding = m->s.grinding; f.bail = m->s.bail_t > 0.0f;
        f.manual = m->s.manual; f.flip = m->s.flip; f.grab = m->s.grab; f.speed = fabsf(m->s.speed);
        SkateInput in;
        control_update(&m->c, &f, DT, &in);
        skate_update(&m->s, &in, m->w, DT);
    }
}

int main(void)
{
    tricks_defaults();

    /* ---- directions ---- */
    CHECK(ctl_dir8(0, -1) == DIR_UP && ctl_dir8(1, -1) == DIR_UPRIGHT && ctl_dir8(1, 0) == DIR_RIGHT && ctl_dir8(1, 1) == DIR_DOWNRIGHT &&
          ctl_dir8(0, 1) == DIR_DOWN && ctl_dir8(-1, 1) == DIR_DOWNLEFT && ctl_dir8(-1, 0) == DIR_LEFT && ctl_dir8(-1, -1) == DIR_UPLEFT, "the eight directions");
    CHECK(ctl_dir8(0.2f, 0.1f) == -1, "the dead radius");
    CHECK(ctl_dir5(0, 0) == 0 && ctl_dir5(0, -0.9f) == 1 && ctl_dir5(0, 0.9f) == 2 && ctl_dir5(-0.9f, 0) == 3 && ctl_dir5(0.9f, 0) == 4, "the five slots");
    CHECK(ctl_flip_slot(DIR_UP) == -1 && ctl_flip_slot(DIR_DOWN) == -1 && ctl_flip_slot(DIR_UPLEFT) == 0 && ctl_flip_slot(DIR_DOWNRIGHT) == 5, "flip slots of the directions");
    CHECK(5 < NFLIP_TYPES, "the table has a slot for every flick");

    /* ---- steering, push, brake, grind ---- */
    {
        T t; t_init(&t);
        SkateInput in = tick(&t, 0.5f, 0.0f, 0);
        CHECK(fabsf(in.steer + 0.5f) < 1e-5f && !in.push && !in.brake && in.flip == 0 && in.flip_pre == 0, "plain steering");
        in = tick(&t, 0, 0, CTL_SQUARE);
        CHECK(in.push == 1, "a Square press is a push");
        in = tick(&t, 0, 0, CTL_SQUARE);
        CHECK(in.push == 0, "a held Square is not another push");
        in = tick(&t, 0, 0, CTL_CIRCLE);
        CHECK(in.brake == 1, "Circle brakes");
        in = tick(&t, 0, 0, 0);
        CHECK(in.grind == 1 + GRIND_5050, "nothing held, the stick neutral: the grind the board would do is a 50-50 (no button needed)");
        in = tick(&t, -0.9f, 0, 0);
        CHECK(in.grind == 1 + GRIND_BOARDSLIDE, "the stick left: a boardslide");
        in = tick(&t, -0.9f, 0, CTL_TRIANGLE);
        CHECK(in.grind == 1 + GRIND_OVERCROOKS && in.ollie_held, "the advanced pop held + left: an overcrooks (and the advanced pop crouches)");
        in = tick(&t, 0, 0, CTL_SQUARE);
        CHECK(in.push == 1 && in.push_held == 1, "a Square press is a push, and it is held");
        in = tick(&t, 0, 0, CTL_SQUARE);
        CHECK(in.push == 0 && in.push_held == 1, "still held: no new push, the hold goes on (the strong push)");
        in = tick(&t, 0, 0, 0);
        CHECK(in.push_held == 0, "let go");
    }

    /* ---- a flick while Cross is held sets a flip up; letting go pops it ---- */
    {
        T t; t_init(&t);
        settle(&t, 12, CTL_CROSS);
        flick(&t, 0, 0, -0.9f, -0.9f, 3, 6, CTL_CROSS);
        CHECK(t.c.ready == 0, "a snap up-left while Cross is held: kickflip ready (ready %d, flick %d)", t.c.ready, t.c.flick_dir);
        CHECK(t.in.flip_pre == 1 && t.in.ollie_held, "ready is passed on to the physics (flip_pre %d)", t.in.flip_pre);
        CHECK(t.in.steer == 0.0f, "the stick does not steer during the flick (steer %.2f)", t.in.steer);
        for (int i = 0; i < 6; i++) tick(&t, -0.9f, -0.9f, CTL_CROSS);
        CHECK(fabsf(t.in.steer - 0.9f) < 1e-4f && t.c.ready == 0, "kept over after the flick was read: it steers again while crouching, the trick stays ready (steer %.2f, ready %d)", t.in.steer, t.c.ready);
        SkateInput in = tick(&t, -0.9f, -0.9f, 0);
        CHECK(in.flip_pre == 1 && !in.ollie_held, "the release frame carries the flip (flip_pre %d)", in.flip_pre);
        in = tick(&t, -0.9f, -0.9f, 0);
        CHECK(in.flip_pre == 0, "and only that frame");
    }
    /* flicks to all six directions */
    {
        static const float dx[6] = { -0.9f, 0.9f, -1.0f, 1.0f, -0.9f, 0.9f }, dy[6] = { -0.9f, -0.9f, 0.0f, 0.0f, 0.9f, 0.9f };
        for (int k = 0; k < 6; k++) {
            T t; t_init(&t);
            settle(&t, 8, CTL_CROSS);
            flick(&t, 0, 0, dx[k], dy[k], 3, 4, CTL_CROSS);
            SkateInput in = tick(&t, 0, 0, 0);
            CHECK(in.flip_pre == 1 + k, "flick %d: flip slot %d expected, got flip_pre %d", k, k, in.flip_pre);
        }
    }
    /* a flick up after the trick: the nollie version of it; a flick down: a plain ollie again */
    {
        T t; t_init(&t);
        settle(&t, 8, CTL_CROSS);
        flick(&t, 0, 0, -0.9f, -0.9f, 3, 3, CTL_CROSS);
        flick(&t, -0.9f, -0.9f, 0, -1.0f, 3, 3, CTL_CROSS);
        CHECK(t.c.ready == 0 && t.c.nollie == 1, "a flick up keeps the flip that was ready and makes it a nollie (ready %d, nollie %d)", t.c.ready, t.c.nollie);
        flick(&t, 0, -1.0f, 0, 1.0f, 3, 3, CTL_CROSS);
        CHECK(t.c.ready == -1 && t.c.nollie == 0, "a flick down cancels it (ready %d)", t.c.ready);
        SkateInput in = tick(&t, 0, 0, 0);
        CHECK(in.flip_pre == 0 && !in.nollie, "released after the cancel: a plain ollie");
    }
    /* slow movements are not flicks */
    {
        T t; t_init(&t);
        settle(&t, 8, CTL_CROSS);
        flick(&t, 0, 0, -0.95f, 0.0f, 30, 10, CTL_CROSS);                    /* steering: half a second to full left */
        CHECK(t.c.ready == -1, "slow steering is not a flick (ready %d)", t.c.ready);
        SkateInput in = tick(&t, -0.95f, 0.0f, 0);
        CHECK(in.flip_pre == 0, "released while steering left: a plain ollie (flip_pre %d)", in.flip_pre);
        T u; t_init(&u);
        settle(&u, 8, CTL_CROSS);
        flick(&u, 0, 0, -0.9f, -0.9f, 30, 10, CTL_CROSS);                    /* a slow push to the up-left corner ... */
        in = tick(&u, -0.9f, -0.9f, 0);
        CHECK(in.flip_pre == 1, "... still counts when it is held out at the release (flip_pre %d)", in.flip_pre);
        T w; t_init(&w);
        settle(&w, 8, CTL_CROSS);
        flick(&w, 0, 0, -0.95f, -0.3f, 30, 10, CTL_CROSS);                   /* steering hard left with a little up: 17 degrees, not a diagonal */
        in = tick(&w, -0.95f, -0.3f, 0);
        CHECK(in.flip_pre == 0, "a nearly horizontal stick at the release is steering (flip_pre %d)", in.flip_pre);
    }
    /* a flick just before Cross counts, an old one does not; none counts without Cross at all */
    {
        T t; t_init(&t);
        settle(&t, 10, 0);
        flick(&t, 0, 0, 0.9f, -0.9f, 3, 3, 0);
        settle(&t, 4, 0);
        tick(&t, 0, 0, CTL_CROSS);
        CHECK(t.c.ready == 1, "a flick 0.12 s before the Cross press counts (ready %d)", t.c.ready);
        T u; t_init(&u);
        settle(&u, 10, 0);
        flick(&u, 0, 0, 0.9f, -0.9f, 3, 3, 0);
        settle(&u, 30, 0);
        tick(&u, 0, 0, CTL_CROSS);
        CHECK(u.c.ready == -1, "one 0.5 s before does not (ready %d)", u.c.ready);
        T w; t_init(&w);
        settle(&w, 10, 0);
        flick(&w, 0, 0, 0.9f, -0.9f, 3, 3, 0);
        CHECK(w.in.flip_pre == 0 && w.in.flip == 0, "a flick on the ground with no Cross does nothing");
    }
    /* the recoil of the nub is not a second flick */
    {
        T t; t_init(&t);
        settle(&t, 8, CTL_CROSS);
        int events = 0, last = -1;
        for (int i = 1; i <= 3; i++) { tick(&t, 0, -0.35f * i, CTL_CROSS); if (t.c.flick_new) { events++; last = t.c.flick_dir; } }
        for (int i = 0; i < 6; i++) { tick(&t, 0, -1.05f, CTL_CROSS); if (t.c.flick_new) { events++; last = t.c.flick_dir; } }
        for (int i = 0; i < 6; i++) { tick(&t, 0, 0, CTL_CROSS); if (t.c.flick_new) { events++; last = t.c.flick_dir; } }
        CHECK(events == 1 && last == DIR_UP, "snap up and spring back: one flick (%d events, last %d)", events, last);
    }
    /* a quick push of the nub to the side while crouching (a flick too) steers after a moment: no being stuck going straight with a pop button held */
    {
        T t; t_init(&t);
        settle(&t, 8, CTL_CROSS);
        flick(&t, 0, 0, -1.0f, 0, 2, 2, CTL_CROSS);
        int frames_locked = 0;
        for (int i = 0; i < 20; i++) { tick(&t, -1.0f, 0, CTL_CROSS); frames_locked += t.in.steer == 0.0f; }
        CHECK(frames_locked <= 8 && fabsf(t.in.steer - 1.0f) < 1e-4f, "the nub pushed left and held while crouching steers left (%d frames locked, steer %.2f)", frames_locked, t.in.steer);
    }
    /* steering is locked while a flick is held, free again at the centre */
    {
        T t; t_init(&t);
        t.f.grounded = 0;                                                   /* in the air */
        settle(&t, 8, 0);
        tick(&t, -0.5f, 0, 0);
        CHECK(fabsf(t.in.steer - 0.5f) < 1e-4f, "air steering with a slow stick");
        settle(&t, 8, 0);
        flick(&t, 0, 0, -1.0f, 0, 3, 8, 0);
        CHECK(t.in.steer == 0.0f, "no steering while the flick is held (%.2f)", t.in.steer);
        tick(&t, -0.2f, 0, 0);
        CHECK(fabsf(t.in.steer - 0.2f) < 1e-4f, "steering again once the stick is back near the centre (%.2f)", t.in.steer);
    }

    /* ---- the air: a flick starts a flip ---- */
    {
        T t; t_init(&t);
        t.f.grounded = 0;
        settle(&t, 8, 0);
        int frames_with_flip = 0, slot = -1;
        for (int i = 1; i <= 3; i++) tick(&t, 0.9f * i / 3, -0.9f * i / 3, 0);
        for (int i = 0; i < 20; i++) { tick(&t, 0.9f, -0.9f, 0); if (t.in.flip) { frames_with_flip++; slot = t.in.flip - 1; } }
        CHECK(slot == FLIP_LATE_HEEL && frames_with_flip >= 1 && frames_with_flip <= 9, "an air flick up-right asks for the late heelflip for a moment (slot %d, %d frames)", slot, frames_with_flip);
        t.f.flip = 2;                                                        /* the physics has taken it */
        settle(&t, 4, 0);
        flick(&t, 0, 0, -1.0f, 0, 3, 3, 0);
        CHECK(t.in.flip == 0, "no new flip while one is going on");
        T u; t_init(&u);
        u.f.grounded = 0;
        settle(&u, 8, CTL_TRIANGLE);
        flick(&u, 0, 0, -1.0f, 0, 3, 6, CTL_TRIANGLE);
        CHECK(u.in.flip == 0 && u.in.grind == 1 + GRIND_OVERCROOKS, "a stick movement with the advanced pop held in the air is the (advanced) grind choice, not a flip");
        T w; t_init(&w);
        w.f.grounded = 0;
        settle(&w, 8, 0);
        flick(&w, 0, 0, 0, -1.0f, 3, 6, 0);
        CHECK(w.in.flip == 0, "an air flick straight up is no flip");
    }

    /* ---- rings, nollie, spins, reverts, powerslides ---- */
    {
        /* the advanced pop (Triangle) held instead of the basic one: the advanced ring; both held: the body ring. The pop is the release of the last pop button. */
        static const struct { unsigned held; float dx, dy; int slot; } R[] = {
            { CTL_TRIANGLE, -0.9f, -0.9f, FLIP_VARIAL_KICK }, { CTL_TRIANGLE, 0.9f, -0.9f, FLIP_VARIAL_HEEL }, { CTL_TRIANGLE, -1.0f, 0.0f, FLIP_HARDFLIP },
            { CTL_TRIANGLE, 1.0f, 0.0f, FLIP_INWARD_HEEL }, { CTL_TRIANGLE, -0.9f, 0.9f, FLIP_360_FLIP }, { CTL_TRIANGLE, 0.9f, 0.9f, FLIP_LASER },
            { CTL_CROSS | CTL_TRIANGLE, -0.9f, -0.9f, FLIP_360_HARDFLIP }, { CTL_CROSS | CTL_TRIANGLE, 0.9f, -0.9f, FLIP_360_INWARD }, { CTL_CROSS | CTL_TRIANGLE, 0.0f, -1.0f, FLIP_FRONTFLIP },
            { CTL_CROSS | CTL_TRIANGLE, 0.0f, 1.0f, FLIP_BACKFLIP } };
        for (unsigned k = 0; k < sizeof(R) / sizeof(R[0]); k++) {
            T t; t_init(&t);
            settle(&t, 8, R[k].held);
            CHECK(t.in.ollie_held, "ring %u: the pop button(s) crouch", R[k].held);
            flick(&t, 0, 0, R[k].dx, R[k].dy, 3, 4, R[k].held);
            SkateInput in = tick(&t, 0, 0, 0);
            CHECK(in.flip_pre == 1 + R[k].slot && !in.nollie, "ring %u flick %u: slot %d expected, got %d", R[k].held, k, R[k].slot, in.flip_pre - 1);
        }
        /* a diagonal held out at the release counts for the ring that was held */
        {
            T t; t_init(&t);
            settle(&t, 8, CTL_TRIANGLE);
            flick(&t, 0, 0, 0.9f, 0.9f, 30, 10, CTL_TRIANGLE);
            SkateInput in = tick(&t, 0.9f, 0.9f, 0);
            CHECK(in.flip_pre == 1 + FLIP_LASER, "a slow diagonal held at the release of the advanced pop: the advanced ring (flip_pre %d)", in.flip_pre);
        }
        /* the nollie stance: the nub held forward while crouching, then the trick flicked from there (or just the pop) is the nollie version */
        T t; t_init(&t);
        settle(&t, 8, CTL_CROSS);
        for (int i = 1; i <= 24; i++) tick(&t, 0, -0.7f * i / 24, CTL_CROSS);       /* pushed forward slowly (no flick): past 0.65 for the last 2 frames */
        for (int i = 0; i < 2; i++) tick(&t, 0, -0.7f, CTL_CROSS);
        CHECK(t.c.nollie == 0, "the nub forward for 0.07 s: not yet");
        for (int i = 0; i < 3; i++) tick(&t, 0, -0.7f, CTL_CROSS);
        CHECK(t.c.nollie == 1 && t.c.ready == -1, "the nub held forward while crouching: the nollie stance (nollie %d)", t.c.nollie);
        flick(&t, 0, -0.9f, -0.9f, -0.9f, 3, 4, CTL_CROSS);
        CHECK(t.c.ready == FLIP_KICKFLIP && t.c.nollie == 1 && t.in.flip_pre == 1 + FLIP_KICKFLIP && t.in.nollie, "a flick to the up-left from there: a nollie kickflip ready (ready %d, nollie %d)", t.c.ready, t.c.nollie);
        SkateInput in = tick(&t, 0, 0, 0);
        CHECK(in.flip_pre == 1 + FLIP_KICKFLIP && in.nollie == 1, "popped: a nollie kickflip (flip_pre %d, nollie %d)", in.flip_pre, in.nollie);
        in = tick(&t, 0, 0, 0);
        CHECK(!in.nollie && t.c.nollie == 0, "and the nollie state is gone");
        /* a plain nollie: the stance, then the pop with no flick; the stance lasts until the pop even if the nub comes back */
        T u; t_init(&u);
        settle(&u, 8, CTL_CROSS);
        for (int i = 0; i < 10; i++) tick(&u, 0, -0.9f, CTL_CROSS);
        settle(&u, 10, CTL_CROSS);
        CHECK(u.c.nollie == 1, "the stance stays while the pop is held (nollie %d)", u.c.nollie);
        in = tick(&u, 0, 0, 0);
        CHECK(in.flip_pre == 0 && in.nollie == 1, "a nollie without a trick");
        /* a snap forwards is the stance at once; a snap straight down takes it back */
        T v; t_init(&v);
        settle(&v, 8, CTL_CROSS);
        flick(&v, 0, 0, 0, -1.0f, 3, 4, CTL_CROSS);
        CHECK(v.c.nollie == 1, "a flick up: the nollie stance at once");
        flick(&v, 0, -1.0f, 0, 1.0f, 3, 4, CTL_CROSS);
        CHECK(v.c.nollie == 0 && v.c.ready == -1, "a flick down: back to the tail");
        in = tick(&v, 0, 0, 0);
        CHECK(in.flip_pre == 0 && !in.nollie, "popped: a plain ollie");
        /* the nub forward with no pop button is a nose manual, not a stance */
        T w; t_init(&w);
        w.f.speed = 3.0f;
        for (int i = 0; i < 10; i++) tick(&w, 0, -0.9f, 0);
        CHECK(w.c.nollie == 0, "no stance without the pop");
        /* the push is not a modifier any more: no push while crouching, but no advanced ring from it either */
        T x; t_init(&x);
        settle(&x, 4, CTL_CROSS);
        in = tick(&x, 0, 0, CTL_CROSS | CTL_SQUARE);
        CHECK(in.push == 0, "no push with a pop button held");
        flick(&x, 0, 0, -0.9f, -0.9f, 3, 4, CTL_CROSS | CTL_SQUARE);
        in = tick(&x, 0, 0, CTL_SQUARE);
        CHECK(in.flip_pre == 1 + FLIP_KICKFLIP, "Square held with the basic pop: still the basic ring (flip_pre %d)", in.flip_pre);
    }
    {
        /* the air: Square held + the nub = spin; a flick up / down = front / back flip */
        T t; t_init(&t);
        t.f.grounded = 0;
        settle(&t, 8, CTL_SQUARE);
        SkateInput in = tick(&t, -0.9f, 0, CTL_SQUARE);
        CHECK(in.spin == 1 && in.steer == 0.0f, "Square + nub left in the air: spin left (BS), no steering (spin %d)", in.spin);
        in = tick(&t, 0.9f, 0, CTL_SQUARE);
        CHECK(in.spin == -1, "nub right: spin right (FS)");
        in = tick(&t, 0.3f, 0, CTL_SQUARE);
        CHECK(in.spin == 0, "a small deflection does not spin");
        T u; t_init(&u);
        u.f.grounded = 0;
        settle(&u, 8, CTL_SQUARE);
        int slot = -1;
        for (int i = 1; i <= 3; i++) tick(&u, 0, -0.35f * i, CTL_SQUARE);
        for (int i = 0; i < 6; i++) { tick(&u, 0, -1.05f, CTL_SQUARE); if (u.in.flip) slot = u.in.flip - 1; }
        CHECK(slot == FLIP_FRONTFLIP, "Square + flick up in the air: frontflip (slot %d)", slot);
        T w; t_init(&w);
        w.f.grounded = 0;
        settle(&w, 8, 0);
        tick(&w, -0.9f, 0, 0);
        CHECK(w.in.spin == 0, "no spin without Square");
        /* Square held from the ground (a strong push) into the air is not the modifier: no spin, the nub steers; pressed again in the air it is */
        T x; t_init(&x);
        settle(&x, 8, CTL_SQUARE);
        x.f.grounded = 0;
        settle(&x, 4, CTL_SQUARE);
        for (int i = 1; i <= 20; i++) in = tick(&x, -0.9f * i / 20, 0, CTL_SQUARE);        /* (slowly: a snap would be a flick, which locks the steering) */
        CHECK(in.spin == 0 && fabsf(in.steer - 0.9f) < 1e-4f, "Square held from the ground: no spin in the air, the nub steers (spin %d, steer %.2f)", in.spin, in.steer);
        tick(&x, -0.9f, 0, 0);
        in = tick(&x, -0.9f, 0, CTL_SQUARE);
        CHECK(in.spin == 1, "pressed again in the air: a spin");
    }
    {
        /* near a rail or ledge the nub is the grind choice, not a turn */
        T t; t_init(&t);
        t.f.grounded = 0;
        settle(&t, 4, 0);
        SkateInput in;
        for (int i = 1; i <= 20; i++) in = tick(&t, -0.6f * i / 20, 0, 0);
        CHECK(fabsf(in.steer - 0.6f) < 1e-4f && in.grind == 1 + GRIND_BOARDSLIDE, "in the open air the nub steers, and would pick a boardslide (steer %.2f, grind %d)", in.steer, in.grind);
        t.f.rail_near = 1;
        in = tick(&t, -0.6f, 0, 0);
        CHECK(in.steer == 0.0f && in.grind == 1 + GRIND_BOARDSLIDE, "with a rail within reach it only picks the grind (steer %.2f)", in.steer);
        /* a flick while grinding switches the grind; the stick does not push the balance during the flick */
        T u; t_init(&u);
        u.f.grounded = 0; u.f.grinding = 1;
        settle(&u, 4, 0);
        int sw = 0, steer_during = 0;
        for (int i = 1; i <= 3; i++) { tick(&u, -0.35f * i, 0, 0); if (u.in.grind_switch) sw = u.in.grind_switch; }
        for (int i = 0; i < 6; i++) { tick(&u, -1.05f, 0, 0); if (u.in.grind_switch) sw = u.in.grind_switch; if (u.in.steer != 0.0f) steer_during = 1; }
        CHECK(sw == 1 + GRIND_BOARDSLIDE && !steer_during, "a flick left while grinding: switch to a boardslide, no balance push meanwhile (switch %d, steer %d)", sw, steer_during);
        settle(&u, 6, CTL_TRIANGLE);
        sw = 0;
        for (int i = 1; i <= 3; i++) { tick(&u, 0, -0.35f * i, CTL_TRIANGLE); if (u.in.grind_switch) sw = u.in.grind_switch; }
        for (int i = 0; i < 6; i++) { tick(&u, 0, -1.05f, CTL_TRIANGLE); if (u.in.grind_switch) sw = u.in.grind_switch; }
        CHECK(sw == 1 + GRIND_CROOKED, "with the advanced pop held, a flick up: a crooked (switch %d)", sw);
        in = tick(&u, 0, -0.6f, 0);
        CHECK(in.grind_switch == 0 && fabsf(in.steer) < 1e-4f, "the switch is one update only; the stick back near the centre: the balance input again");
    }
    {
        /* Circle on the ground: a flick sideways = revert, the nub held hard over = powerslide */
        T t; t_init(&t);
        t.f.speed = 5.0f;
        settle(&t, 8, CTL_CIRCLE);
        int rev = 0;
        for (int i = 1; i <= 3; i++) { tick(&t, -0.35f * i, 0, CTL_CIRCLE); if (t.in.revert) rev = t.in.revert; }
        for (int i = 0; i < 6; i++) { tick(&t, -1.05f, 0, CTL_CIRCLE); if (t.in.revert) rev = t.in.revert; }
        CHECK(rev == 1, "Circle + a flick left: a revert to the left (BS) (%d)", rev);
        CHECK(t.in.slide == 0, "the flick itself is not a powerslide");
        T u; t_init(&u);
        u.f.speed = 5.0f;
        settle(&u, 8, CTL_CIRCLE);
        int slid = 0, rv = 0;
        for (int i = 1; i <= 40; i++) { tick(&u, 0.95f * i / 40, 0, CTL_CIRCLE); if (u.in.slide) slid = u.in.slide; if (u.in.revert) rv = 1; }
        for (int i = 0; i < 20; i++) { tick(&u, 0.95f, 0, CTL_CIRCLE); if (u.in.slide) slid = u.in.slide; }
        CHECK(slid == -1 && !rv, "Circle + the nub slowly held hard right: powerslide to the right (slide %d, revert %d)", slid, rv);
        tick(&u, 0.95f, 0, 0);
        CHECK(u.in.slide == 0, "Circle let go: the powerslide ends");
        T w; t_init(&w);
        w.f.speed = 1.0f;
        settle(&w, 8, CTL_CIRCLE);
        for (int i = 1; i <= 3; i++) tick(&w, -0.35f * i, 0, CTL_CIRCLE);
        for (int i = 0; i < 6; i++) tick(&w, -1.05f, 0, CTL_CIRCLE);
        CHECK(w.in.revert == 0, "too slow to revert");
        T x; t_init(&x);
        x.f.speed = 5.0f;
        settle(&x, 8, 0);
        for (int i = 1; i <= 3; i++) tick(&x, -0.35f * i, 0, 0);
        for (int i = 0; i < 6; i++) { tick(&x, -1.05f, 0, 0); CHECK(x.in.revert == 0 && x.in.slide == 0, "no revert / slide without Circle"); }
    }

    /* ---- manuals ---- */
    {
        T t; t_init(&t);
        t.f.speed = 3.0f;
        int starts = 0, kind = 0;
        for (int i = 0; i < 30; i++) { tick(&t, 0, 0.9f, 0); if (t.in.manual) { starts++; kind = t.in.manual; } }
        CHECK(starts == 1 && kind == 1, "the stick pulled down for 0.5 s: one manual start (%d, kind %d)", starts, kind);
        t.f.manual = 1;
        settle(&t, 10, 0);
        t.f.manual = 0;
        starts = 0; kind = 0;
        for (int i = 0; i < 30; i++) { tick(&t, 0, -0.9f, 0); if (t.in.manual) { starts++; kind = t.in.manual; } }
        CHECK(starts == 1 && kind == 2, "pushed up: a nose manual (%d, kind %d)", starts, kind);
        T u; t_init(&u);
        u.f.speed = 3.0f;
        int n = 0;
        for (int i = 0; i < 10; i++) { tick(&u, 0, 0.9f, 0); n += u.in.manual != 0; }
        for (int i = 0; i < 10; i++) { tick(&u, 0, 0.0f, 0); n += u.in.manual != 0; }
        CHECK(n == 0, "a short pull is not a manual");
        T w; t_init(&w);
        w.f.speed = 1.0f;
        n = 0;
        for (int i = 0; i < 30; i++) { tick(&w, 0, 0.9f, 0); n += w.in.manual != 0; }
        CHECK(n == 0, "too slow for a manual");
        T x; t_init(&x);
        x.f.speed = 3.0f;
        n = 0;
        for (int i = 0; i < 30; i++) { tick(&x, 0.8f, 0.9f, 0); n += x.in.manual != 0; }
        CHECK(n == 0, "a diagonal pull while steering is not a manual");
        T y; t_init(&y);
        y.f.speed = 3.0f;
        n = 0;
        for (int i = 0; i < 30; i++) { tick(&y, 0, 0.9f, CTL_CROSS); n += y.in.manual != 0; }
        CHECK(n == 0, "no manual while Cross is held");
    }

    /* ---- grabs ---- */
    {
        T t; t_init(&t);
        t.f.grounded = 0;
        settle(&t, 3, 0);
        int first = -1, type = 0;
        for (int i = 0; i < 12; i++) {
            tick(&t, -0.9f, 0, i >= 1 ? CTL_L : 0);
            if (t.in.grab && first < 0) { first = i; type = t.in.grab; }
        }
        CHECK(first >= 4 && first <= 8 && type == 1 + GRAB_BS_GRAB, "L in the air with the stick left: the back-hand BS grab after ~0.07 s (frame %d, type %d)", first, type);
        tick(&t, 0, 0, 0);
        CHECK(t.in.grab == 0, "let go: the grab ends");
        T u; t_init(&u);
        settle(&u, 3, CTL_R);                                               /* R held on the ground ... */
        u.f.grounded = 0;                                                    /* ... and the board leaves it */
        int got = 0;
        for (int i = 0; i < 10; i++) { tick(&u, 0, 0, CTL_R); got |= u.in.grab != 0; }
        CHECK(got, "R held through the take-off grabs");
        T w; t_init(&w);
        settle(&w, 3, 0);
        tick(&w, 0, 0, CTL_L); tick(&w, 0, 0, CTL_L); tick(&w, 0, 0, CTL_L); tick(&w, 0, 0, CTL_L); tick(&w, 0, 0, CTL_L); tick(&w, 0, 0, CTL_L); tick(&w, 0, 0, CTL_L);
        CHECK(w.in.grab == 0, "no grab on the ground");
    }

    /* ---- grab families and variants, grind rings ---- */
    {
        CHECK(ctl_grab_base(CTL_R, 0) == GRAB_MELON && ctl_grab_base(CTL_R, 1) == GRAB_NOSE && ctl_grab_base(CTL_R, 2) == GRAB_MUTE, "the front hand: melon / nose grab / mute");
        CHECK(ctl_grab_base(CTL_L, 0) == GRAB_BS_GRAB && ctl_grab_base(CTL_L, 4) == GRAB_FS_GRAB && ctl_grab_base(CTL_L, 1) == GRAB_TAIL && ctl_grab_base(CTL_L, 2) == GRAB_STALE, "the back hand: indy / FS grab / tail grab / stale");
        CHECK(ctl_grab_base(CTL_L | CTL_R, 0) == GRAB_DOUBLE, "both hands: double grab");
        CHECK(ctl_grab_variant(GRAB_MUTE, DIR_UP) == GRAB_MUTE_NOSEBONE && ctl_grab_variant(GRAB_MUTE_NOSEBONE, DIR_DOWN) == GRAB_MUTE_TAILBONE && ctl_grab_variant(GRAB_MUTE, DIR_LEFT) == GRAB_MUTE_TUCK, "mute variants");
        CHECK(ctl_grab_variant(GRAB_NOSE, DIR_UP) == GRAB_BS_SHIFTY_NOSE && ctl_grab_variant(GRAB_NOSE, DIR_DOWN) == GRAB_FS_SHIFTY_NOSE && ctl_grab_variant(GRAB_NOSE, DIR_RIGHT) == GRAB_ROCKET, "nose grab variants");
        CHECK(ctl_grab_variant(GRAB_MELON, DIR_UPLEFT) == GRAB_METHOD && ctl_grab_variant(GRAB_MELON, DIR_UP) == -1, "melon + tweak = method");
        CHECK(ctl_grab_variant(GRAB_STALE_TUCK, DIR_DOWNLEFT) == GRAB_STALE, "a diagonal down goes back to the plain grab");
        T t; t_init(&t);
        t.f.grounded = 0;
        settle(&t, 3, 0);
        int slot = -1;
        for (int i = 0; i < 12; i++) { tick(&t, 0, 0.9f, CTL_R); if (t.in.grab) slot = t.in.grab - 1; }
        CHECK(slot == GRAB_MUTE, "R + the nub down: mute (slot %d)", slot);
        t.f.grab = 1 + GRAB_MUTE;                                            /* the physics has it */
        for (int i = 1; i <= 3; i++) tick(&t, 0, 0.9f - 1.9f * i / 3, CTL_R);
        for (int i = 0; i < 4; i++) tick(&t, 0, -1.0f, CTL_R);
        CHECK(t.in.grab == 1 + GRAB_MUTE_NOSEBONE, "then a flick up while it is held: mute nosebone (slot %d)", t.in.grab - 1);
    }
    {
        static const struct { int sq, dir, slot; } G[] = {
            { 0, -1, GRIND_5050 }, { 0, DIR_UP, GRIND_NOSEGRIND }, { 0, DIR_DOWN, GRIND_50 }, { 0, DIR_LEFT, GRIND_BOARDSLIDE }, { 0, DIR_RIGHT, GRIND_LIPSLIDE },
            { 0, DIR_UPLEFT, GRIND_NOSESLIDE }, { 0, DIR_DOWNLEFT, GRIND_TAILSLIDE }, { 0, DIR_UPRIGHT, GRIND_NOSEBLUNT }, { 0, DIR_DOWNRIGHT, GRIND_BLUNT },
            { 1, -1, GRIND_SMITH }, { 1, DIR_UP, GRIND_CROOKED }, { 1, DIR_DOWN, GRIND_FEEBLE }, { 1, DIR_LEFT, GRIND_OVERCROOKS }, { 1, DIR_RIGHT, GRIND_WILLY },
            { 1, DIR_UPLEFT, GRIND_OVERWILLY }, { 1, DIR_DOWNLEFT, GRIND_SALAD } };
        int seen[NGRIND_TYPES]; memset(seen, 0, sizeof(seen));
        for (unsigned k = 0; k < sizeof(G) / sizeof(G[0]); k++) { CHECK(ctl_grind_slot(G[k].sq, G[k].dir) == G[k].slot, "grind ring %d dir %d", G[k].sq, G[k].dir); seen[G[k].slot]++; }
        int all = 1; for (int i = 0; i < NGRIND_TYPES; i++) all &= seen[i] > 0;
        CHECK(all, "every one of the 16 grinds is reachable");
        T t; t_init(&t);
        t.f.grounded = 0;
        settle(&t, 3, CTL_TRIANGLE | CTL_SQUARE);
        tick(&t, 0, -0.9f, CTL_TRIANGLE | CTL_SQUARE);
        CHECK(t.in.grind == 1 + GRIND_CROOKED && t.in.spin == 0, "the advanced pop + Square + up in the air: a crooked grind, not a spin (grind %d spin %d)", t.in.grind, t.in.spin);
    }

    /* ---- through the real physics, on a flat floor ---- */
    {
        static float fv[12] = { -200, 0, -200, 200, 0, -200, 200, 0, 200, -200, 0, 200 };
        static CollPoly fp[1] = { { { 0, 1, 2, 3 }, 0 } };
        if (!coll_build(&flat, fv, 4, fp, 1, 8.0f)) return 1;
        SkateWorld w = { NULL, g_ground, g_wall, g_surface, NULL };
        const float DX = -0.9f, DY = -0.9f;

        /* pop with a flick: the flip starts in the same motion */
        {
            Sim m; sim_init(&m, &w, 4.0f);
            sim_run(&m, 10, 0, 0, 0);
            sim_run(&m, 3, 0, 0, CTL_CROSS);
            for (int i = 1; i <= 3; i++) sim_run(&m, 1, DX * i / 3, DY * i / 3, CTL_CROSS);
            sim_run(&m, 8, DX, DY, CTL_CROSS);
            int flipped = 0, popped = 0;
            for (int i = 0; i < 6; i++) { sim_run(&m, 1, DX, DY, 0); flipped |= m.s.flip != 0; popped |= !m.s.grounded; }
            CHECK(popped && flipped && m.s.flip == 1, "ollie + kickflip in one motion (popped %d, flip %d)", popped, m.s.flip);
            CHECK(m.s.ev_ollie == 1, "one pop");
        }
        /* no flick: a plain ollie */
        {
            Sim m; sim_init(&m, &w, 4.0f);
            sim_run(&m, 10, 0, 0, 0);
            sim_run(&m, 20, 0, 0, CTL_CROSS);
            int popped = 0;
            for (int i = 0; i < 8; i++) { sim_run(&m, 1, 0, 0, 0); popped |= !m.s.grounded; }
            CHECK(popped && m.s.flip == 0, "a plain ollie has no flip (flip %d)", m.s.flip);
        }
        /* steering left through the pop is not a shuvit */
        {
            Sim m; sim_init(&m, &w, 4.0f);
            sim_run(&m, 10, 0, 0, 0);
            for (int i = 1; i <= 20; i++) sim_run(&m, 1, -0.8f * i / 20, 0, CTL_CROSS);
            sim_run(&m, 5, -0.8f, 0, CTL_CROSS);
            for (int i = 0; i < 8; i++) sim_run(&m, 1, -0.8f, 0, 0);
            CHECK(!m.s.grounded && m.s.flip == 0, "an ollie out of a left turn is just an ollie (flip %d)", m.s.flip);
        }
        /* a late flick in the air */
        {
            Sim m; sim_init(&m, &w, 4.0f);
            sim_run(&m, 10, 0, 0, 0);
            sim_run(&m, 20, 0, 0, CTL_CROSS);
            sim_run(&m, 6, 0, 0, 0);
            int air = !m.s.grounded;
            for (int i = 1; i <= 3; i++) sim_run(&m, 1, 0.9f * i / 3, -0.9f * i / 3, 0);
            int flipped = 0;
            for (int i = 0; i < 8; i++) { sim_run(&m, 1, 0.9f, -0.9f, 0); flipped |= m.s.flip == 1 + FLIP_LATE_HEEL; }
            CHECK(air && flipped, "a flick in the air starts the late heelflip (air %d, flip %d)", air, m.s.flip);
        }
        /* a grab with L */
        {
            Sim m; sim_init(&m, &w, 4.0f);
            sim_run(&m, 10, 0, 0, 0);
            sim_run(&m, 20, 0, 0, CTL_CROSS);
            sim_run(&m, 6, 0, 0, 0);
            sim_run(&m, 8, 0, 0, CTL_L);
            CHECK(!m.s.grounded && m.s.grab == 1, "L in the air grabs (grab %d)", m.s.grab);
        }
        /* a manual from the stick, balanced, and a flip trick out of it */
        {
            Sim m; sim_init(&m, &w, 4.0f);
            sim_run(&m, 5, 0, 0, 0);
            sim_run(&m, 20, 0, 0.9f, 0);
            CHECK(m.s.manual == 1, "the stick held down starts a manual (manual %d)", m.s.manual);
            sim_run(&m, 20, 0, 0, 0);
            CHECK(m.s.manual == 1 && m.s.bail_t <= 0.0f, "and it goes on with the stick back in the centre");
            sim_run(&m, 3, 0, 0, CTL_CROSS);
            for (int i = 1; i <= 3; i++) sim_run(&m, 1, DX * i / 3, DY * i / 3, CTL_CROSS);
            sim_run(&m, 5, DX, DY, CTL_CROSS);
            int flipped = 0;
            for (int i = 0; i < 6; i++) { sim_run(&m, 1, DX, DY, 0); flipped |= m.s.flip != 0; }
            CHECK(flipped && m.s.manual == 0, "a flip trick out of the manual (flip %d, manual %d)", m.s.flip, m.s.manual);
        }
    }
    {   /* the push through the physics: a tap is the light push, the button held through the foot plant the strong one */
        static float fv2[12] = { -200, 0, -200, 200, 0, -200, 200, 0, 200, -200, 0, 200 };
        static CollPoly fp2[1] = { { { 0, 1, 2, 3 }, 0 } };
        if (!coll_build(&flat, fv2, 4, fp2, 1, 8.0f)) return 1;
        SkateWorld w = { NULL, g_ground, g_wall, g_surface, NULL };
        Sim a; sim_init(&a, &w, 1.0f);
        sim_run(&a, 1, 0, 0, CTL_SQUARE); sim_run(&a, 2, 0, 0, 0);
        sim_run(&a, 75, 0, 0, 0);
        Sim b; sim_init(&b, &w, 1.0f);
        sim_run(&b, 25, 0, 0, CTL_SQUARE);
        int strong = b.s.push_strong;
        sim_run(&b, 51, 0, 0, 0);
        CHECK(!a.s.push_strong && a.s.speed > 2.0f, "a tap: the light push (%.2f m/s)", a.s.speed);
        CHECK(strong && b.s.speed > a.s.speed + 1.0f, "held: the strong push (strong %d, %.2f m/s)", strong, b.s.speed);
    }
    {   /* L / R while crouching: micro steering, and no grab out of it */
        T t; t_init(&t);
        tick(&t, 0, 0, CTL_CROSS);
        SkateInput in = tick(&t, 0, 0, CTL_CROSS | CTL_L);
        CHECK(fabsf(in.steer - MICRO_STEER_T) < 1e-5f && in.ollie_held, "L while the pop is held steers a little left (%.2f)", in.steer);
        in = tick(&t, 0, 0, CTL_CROSS | CTL_R);
        CHECK(fabsf(in.steer + MICRO_STEER_T) < 1e-5f, "R steers a little right (%.2f)", in.steer);
        in = tick(&t, 0, 0, CTL_CROSS | CTL_L | CTL_R);
        CHECK(fabsf(in.steer) < 1e-5f, "both: none");
        in = tick(&t, -0.5f, 0, CTL_CROSS | CTL_L);
        CHECK(fabsf(in.steer - 0.9f) < 1e-4f, "it adds to the nub (%.2f)", in.steer);
        in = tick(&t, -0.5f, 0, CTL_CROSS | CTL_R);
        CHECK(fabsf(in.steer - 0.1f) < 1e-4f, "and R takes from a left nub (%.2f)", in.steer);
        tick(&t, 0, 0, 0);
        in = tick(&t, 0, 0, CTL_L);
        CHECK(in.steer == 0.0f, "L alone on the ground does not steer (%.2f)", in.steer);
        /* held through the pop: no grab at take-off; let go and press again in the air: a grab */
        T u; t_init(&u);
        tick(&u, 0, 0, CTL_CROSS); tick(&u, 0, 0, CTL_CROSS | CTL_L);
        tick(&u, 0, 0, CTL_L);
        u.f.grounded = 0;
        int grabbed = 0;
        for (int i = 0; i < 40; i++) grabbed |= tick(&u, 0, 0, CTL_L).grab != 0;
        CHECK(!grabbed, "a L that steered up to the pop is not a grab in the air");
        tick(&u, 0, 0, 0);
        for (int i = 0; i < 40; i++) grabbed |= tick(&u, 0, 0, CTL_L).grab != 0;
        CHECK(grabbed, "let go and pressed in the air: a grab");
        T v; t_init(&v);
        v.f.grounded = 0;
        int g2 = 0;
        for (int i = 0; i < 40; i++) g2 |= tick(&v, 0, 0, CTL_L).grab != 0;
        CHECK(g2, "a plain air grab still works");
    }
    printf(fails ? "%d FAILURES\n" : "all control tests passed\n", fails);
    return fails != 0;
}
