/* Host tests for src/rails.c and the grinding part of src/skate.c: synthetic rails, then every rail of the real converted level.
 * usage: test_rails [level.lvl] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/skate.h"
#include "../src/coll.h"
#include "../src/rails.h"
#include "../src/trick.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static Coll flat;
static float flatv[12] = { -50, 0, -50, 50, 0, -50, 50, 0, 50, -50, 0, 50 };
static CollPoly flatp[1] = { { { 0, 1, 2, 3 }, 0 } };
static int g_ground(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&flat, x, z, ym, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(&flat, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(&flat, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }

typedef struct { int grinding_frames, attach_frame; float exit_speed, max_y_diff; int left_rail; int total_frames; } Run;
static int g_autobal = 1;
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* an airborne rider at p with velocity v; run until it has left the rail again (or 4 s) */
static Run fly(const SkateWorld *w, const float p[3], const float v[3], float heading, int hop_after_frames)
{
    Skate s; skate_init(&s, p[0], p[1], p[2], heading);
    s.grounded = 0; for (int i = 0; i < 3; i++) s.vel[i] = v[i];
    SkateInput idle = { 0, 0, 0, 0, 1 , 0, 0, 0, 0, 0}, hold = { 0, 0, 1, 0, 1, 0, 0, 0, 0 };      /* the grind trick is held throughout */
    Run r = { 0, -1, 0, 0, 0, 0 };
    int was = 0, first_done = 0;
    for (int i = 0; i < 900; i++) {
        SkateInput in = idle;
        if (hop_after_frames > 0 && s.grinding && s.grind_time > hop_after_frames / 60.0f - 0.35f && s.grind_time < hop_after_frames / 60.0f) in = hold;
        if (g_autobal && s.grinding) in.steer = clampf(-(2.5f * s.lean + 0.8f * s.lean_v), -1.0f, 1.0f);        /* a player who keeps the needle centred */
        skate_update(&s, &in, w, 1.0f / 60.0f);
        if (s.grinding) { r.total_frames++; if (!first_done) r.grinding_frames++; if (r.attach_frame < 0) r.attach_frame = i; }
        if (was && !s.grinding && !first_done) { r.left_rail = 1; first_done = 1; r.exit_speed = hypotf(hypotf(s.vel[0], s.vel[1]), s.vel[2]); }
        was = s.grinding;
        if (first_done && s.grounded) break;
    }
    return r;
}

int main(int argc, char **argv)
{
    if (!coll_build(&flat, flatv, 4, flatp, 1, 4.0f)) return 1;

    /* ---- rails.c ---- */
    tricks_defaults();
    uint32_t tab[8] = { 0, 2, 2, 3, 5, 2, 7, 2 | (RAIL_KIND_LEDGE << 16) };    /* rail 0: 2 points, rail 1: 3 points, rail 2: 2 points, rail 3: a ledge */
    float pts[27] = { 0, 1, 0,  0, 1, 10,                          /* flat rail along +z at y = 1 */
                      5, 3, 0,  5, 2, 5,  5, 0, 10,                /* sloped rail, bent */
                      -50, 1, 20,  50, 1, 20,                      /* a long flat rail along +x */
                      20, 1, -30,  20, 1, 30 };                    /* a ledge along +z (its top is on the left = +x) */
    Rails rl;
    if (!rails_build(&rl, tab, 4, pts)) return 1;
    CHECK(rl.rail[3].kind == RAIL_KIND_LEDGE && rl.rail[3].npts == 2 && rl.rail[0].kind == RAIL_KIND_RAIL, "rail kinds are read from the table");
    CHECK(fabsf(rl.rail[0].len - 10.0f) < 1e-4f, "rail 0 length %.3f", rl.rail[0].len);
    CHECK(fabsf(rl.rail[1].len - (sqrtf(26.0f) + sqrtf(29.0f))) < 1e-3f, "rail 1 length %.3f", rl.rail[1].len);
    uint32_t id; float t, pt[3], dir[3];
    float q1[3] = { 0.1f, 1.2f, 5.0f };
    CHECK(rails_find(&rl, q1, 0.35f, 0.35f, 0.45f, &id, &t, pt, dir) && id == 0 && fabsf(t - 5.0f) < 1e-3f && fabsf(pt[1] - 1.0f) < 1e-4f && fabsf(dir[2] - 1.0f) < 1e-4f,
          "find on the flat rail: id %u t %.2f", id, t);
    float q2[3] = { 1.0f, 1.2f, 5.0f };
    CHECK(!rails_find(&rl, q2, 0.35f, 0.35f, 0.45f, &id, &t, pt, dir), "too far sideways must not find");
    float q3[3] = { 0.1f, 2.0f, 5.0f };
    CHECK(!rails_find(&rl, q3, 0.35f, 0.35f, 0.45f, &id, &t, pt, dir), "too high above the rail must not find");
    float q4[3] = { 0.1f, 0.5f, 5.0f };
    CHECK(!rails_find(&rl, q4, 0.35f, 0.35f, 0.45f, &id, &t, pt, dir), "too far below the rail must not find");
    float q5[3] = { 5.0f, 2.1f, 5.0f };
    CHECK(rails_find(&rl, q5, 0.35f, 0.35f, 0.45f, &id, &t, pt, dir) && id == 1, "find on the bent rail");
    rails_point(&rl, 1, rl.rail[1].len, pt, dir);
    CHECK(fabsf(pt[2] - 10.0f) < 1e-4f && dir[1] < 0.0f, "end of the sloped rail");

    SkateWorld w = { NULL, g_ground, g_wall, g_surface, &rl };

    /* ---- grinding on synthetic rails ---- */
    float p[3] = { 0.1f, 1.1f, 1.0f }, v[3] = { 0, 0, 5.0f };
    Run r = fly(&w, p, v, 0.0f, 0);
    CHECK(r.attach_frame >= 0 && r.attach_frame < 10, "locks on to the flat rail (frame %d)", r.attach_frame);
    CHECK(r.left_rail && r.grinding_frames > 60 && r.grinding_frames < 140, "slides the length of the rail (%d frames)", r.grinding_frames);
    CHECK(r.exit_speed > 2.5f && r.exit_speed < 5.0f, "exit speed %.2f (friction only)", r.exit_speed);

    float v2[3] = { 5.0f, 0, 0 };                                    /* across the rail: no lock-on */
    r = fly(&w, p, v2, 1.57f, 0);
    CHECK(r.attach_frame < 0, "crossing the rail must not lock on");
    float v3[3] = { 0, 0, 0.5f };                                    /* too slow */
    r = fly(&w, p, v3, 0.0f, 0);
    CHECK(r.attach_frame < 0, "too slow must not lock on");
    float v4[3] = { 0, 0, -5.0f };                                   /* the other way along the rail */
    float p4[3] = { 0.1f, 1.1f, 9.0f };
    r = fly(&w, p4, v4, 3.14159f, 0);
    CHECK(r.attach_frame >= 0 && r.left_rail && r.grinding_frames > 60, "grinds in the negative direction too (%d frames)", r.grinding_frames);

    /* downhill rail (rail 1: 3 m down over 10 m on average): speed grows */
    float p5[3] = { 5.05f, 3.1f, 0.0f }, v5[3] = { 0, -0.5f, 2.5f };
    r = fly(&w, p5, v5, 0.0f, 0);
    CHECK(r.attach_frame >= 0 && r.left_rail && r.exit_speed > 4.5f, "downhill rail speeds the rider up (exit %.2f, attach %d)", r.exit_speed, r.attach_frame);

    /* ollie off the rail: leaves it early, goes up, and does not grab it again at once */
    r = fly(&w, p, v, 0.0f, 40);
    CHECK(r.left_rail && r.grinding_frames < 55 && r.grinding_frames > 20, "hop off mid-rail (%d frames on the first stint)", r.grinding_frames);
    {
        Skate s; skate_init(&s, p[0], p[1], p[2], 0.0f); s.grounded = 0; s.vel[2] = 5.0f;
        SkateInput idle = { 0, 0, 0, 0, 1 , 0, 0, 0, 0, 0}, hold = { 0, 0, 1, 0, 1, 0, 0, 0, 0 };
        float ymax = 0; int left_frame = -1, regrab_frame = -1;
        for (int i = 0; i < 150; i++) {
            SkateInput in = (s.grinding && s.grind_time > 0.1f && s.grind_time < 0.45f) ? hold : idle;
            int was = s.grinding;
            skate_update(&s, &in, &w, 1.0f / 60.0f);
            if (was && !s.grinding && left_frame < 0) left_frame = i;
            if (left_frame >= 0 && s.grinding && regrab_frame < 0) regrab_frame = i;
            if (left_frame >= 0 && s.pos[1] > ymax) ymax = s.pos[1];
        }
        CHECK(left_frame >= 0 && ymax > 1.6f, "after a hop the rider rises (%.2f)", ymax);
        CHECK(regrab_frame < 0 || regrab_frame - left_frame >= 15, "no re-grab inside the cool-down (left at %d, re-grabbed at %d)", left_frame, regrab_frame);
    }
    /* a TAP of the pop on the rail hops straight up and comes back down on it; a LONG press jumps off it (to the side of the nub, else to the left) and does not re-grab */
    for (int mode = 0; mode < 3; mode++) {                      /* 0: a tap, 1: a long press, 2: a long press with the nub right */
        Skate s; skate_init(&s, p[0], p[1], p[2], 0.0f); s.grounded = 0; s.vel[2] = 5.0f;
        SkateInput idle = { 0, 0, 0, 0, 1 , 0, 0, 0, 0, 0}, hold = { 0, 0, 1, 0, 1, 0, 0, 0, 0 };
        float hold_s = mode == 0 ? 0.1f : 0.4f;
        if (mode == 2) hold.steer = -1.0f;                          /* the nub right */
        int left_frame = -1, regrab_frame = -1, landed_ground = 0; float x_end = 0.0f;
        for (int i = 0; i < 240; i++) {
            SkateInput in = (s.grinding && s.grind_time > 0.1f && s.grind_time < 0.1f + hold_s) ? hold : idle;
            if (mode == 2 && s.grinding) in.steer = -1.0f;              /* the nub stays right through the release */
            int was = s.grinding;
            skate_update(&s, &in, &w, 1.0f / 60.0f);
            if (was && !s.grinding && left_frame < 0) left_frame = i;
            if (left_frame >= 0 && s.grinding && regrab_frame < 0) { regrab_frame = i; if (mode == 0) break; }
            if (left_frame >= 0 && s.grounded) { landed_ground = 1; x_end = s.pos[0]; break; }
        }
        if (mode == 0) CHECK(left_frame >= 0 && regrab_frame > left_frame && !landed_ground, "a tap: hops up and lands back on the rail (left %d, re-grabbed %d)", left_frame, regrab_frame);
        else CHECK(left_frame >= 0 && regrab_frame < 0 && landed_ground && (mode == 1 ? x_end > p[0] + 0.4f : x_end < p[0] - 0.4f), "a long press%s: off the rail onto the ground beside it (x %.2f from %.2f, re-grabbed %d)", mode == 2 ? " with the nub right" : "", x_end, p[0], regrab_frame);
    }
    /* ---- the board locks on by itself (no button); the trick input decides which grind it is, none = a 50-50 ---- */
    {
        Skate s; skate_init(&s, 0.1f, 1.1f, 1.0f, 0.0f); s.grounded = 0; s.vel[2] = 5.0f;
        SkateInput none = { 0, 0, 0, 0, 0 , 0, 0, 0, 0, 0};
        int locked = 0;
        for (int i = 0; i < 60; i++) { skate_update(&s, &none, &w, 1.0f / 60.0f); locked |= s.grinding; }
        CHECK(locked && s.grind_type == GRIND_5050, "without a trick input the board still locks on, as a 50-50 (locked %d, type %d)", locked, s.grind_type);
        /* each trick input gives its own grind type */
        for (int ty = 0; ty < NGRIND_TYPES; ty++) {
            Skate q; skate_init(&q, 0.1f, 1.1f, 1.0f, 0.0f); q.grounded = 0; q.vel[2] = 5.0f;
            SkateInput in = { 0, 0, 0, 0, ty + 1 , 0, 0, 0, 0, 0};
            for (int i = 0; i < 20 && !q.grinding; i++) skate_update(&q, &in, &w, 1.0f / 60.0f);
            CHECK(q.grinding && q.grind_type == ty && q.ev_grind_start == 1, "trick input %d starts grind type %d (got %d)", ty + 1, ty, q.grind_type);
        }
        /* a switch request while grinding changes the grind on the same rail (the first one is over for the scoring); the same type again is nothing */
        {
            Skate q; skate_init(&q, 0.1f, 1.1f, 1.0f, 0.0f); q.grounded = 0; q.vel[2] = 5.0f;
            SkateInput in = { 0, 0, 0, 0, 1 + GRIND_5050, 0, 0, 0, 0, 0};
            for (int i = 0; i < 20 && !q.grinding; i++) skate_update(&q, &in, &w, 1.0f / 60.0f);
            float lean0 = q.lean;
            in.grind_switch = 1 + GRIND_50; in.steer = 0.9f; skate_update(&q, &in, &w, 1.0f / 60.0f); in.grind_switch = 0;        /* (the flick that chose it still deflects the stick) */
            CHECK(q.grinding && q.grind_type == GRIND_50 && q.ev_grind_switch == 1 && q.ev_grind_start == 1 && q.ev_grind_end == 0, "a switch to a 5-0 (type %d, switch events %d)", q.grind_type, q.ev_grind_switch);
            CHECK(fabsf(q.lean - lean0) < 0.02f && !q.ev_bail, "the flick that chose it does not tip the rider (lean %.3f -> %.3f)", lean0, q.lean);
            in.steer = 0.0f;
            in.grind_switch = 1 + GRIND_50; skate_update(&q, &in, &w, 1.0f / 60.0f); in.grind_switch = 0;
            CHECK(q.ev_grind_switch == 1, "the same grind again is not a switch");
        }
    }

    /* ---- no balancing on a rail: with no input - or the stick held hard over - every grind rides the rail to its end, nobody bails ---- */
    {
        for (int ty = 0; ty < NGRIND_TYPES; ty++) {
            for (int mode = 0; mode < 2; mode++) {                  /* 0: no input, 1: the stick held hard over the whole time */
                Skate s; skate_init(&s, -45.0f, 1.1f, 20.0f, 1.5708f); s.grounded = 0; s.vel[0] = 7.0f;
                SkateInput in = { 0, 0, 0, 0, ty + 1 , 0, 0, 0, 0, 0};
                in.steer = mode ? 1.0f : 0.0f;
                int bailed = 0, gframes = 0; float lean_max = 0.0f;
                for (int i = 0; i < 1200 && !bailed; i++) {
                    skate_update(&s, &in, &w, 1.0f / 60.0f);
                    if (s.grinding) { gframes++; if (fabsf(s.lean) > lean_max) lean_max = fabsf(s.lean); }
                    if (s.ev_bail) bailed = 1;
                    if (!s.grinding && gframes > 0) break;           /* slid off the end of the rail / too slow */
                }
                CHECK(!bailed && gframes > 60 && lean_max < 0.5f, "grind type %d, %s: rides the rail to the end without a bail (%.1f s, lean max %.2f, bailed %d)", ty, mode ? "stick hard over" : "no input", gframes / 60.0f, lean_max, bailed);
            }
        }
    }
    /* landing off-centre tips the body a little that way (a look only: it settles back) */
    {
        for (int sd = -1; sd <= 1; sd += 2) {
            Skate s; skate_init(&s, 0.3f * (float)sd, 1.1f, 1.0f, 0.0f); s.grounded = 0; s.vel[2] = 5.0f;     /* rail 0 runs along +z: +x is the rider's left */
            SkateInput in = { 0, 0, 0, 0, 1 , 0, 0, 0, 0, 0};
            for (int i = 0; i < 10 && !s.grinding; i++) skate_update(&s, &in, &w, 1.0f / 60.0f);
            CHECK(s.grinding && s.lean * (float)sd > 0.1f, "off-centre landing on the %s leans that way (lean %.2f)", sd > 0 ? "left" : "right", s.lean);
        }
    }
    /* a ledge is grabbed from its top side only */
    {
        float hv[3] = { 0, 0, 5.0f };
        float ontop[3] = { 20.15f, 1.1f, -20.0f }, outside[3] = { 19.8f, 1.1f, -20.0f };
        int a = 0, b = 0;
        for (int side = 0; side < 2; side++) {
            Skate s; const float *pp = side ? outside : ontop;
            skate_init(&s, pp[0], pp[1], pp[2], 0.0f); s.grounded = 0; s.vel[2] = hv[2];
            SkateInput in = { 0, 0, 0, 0, 1 , 0, 0, 0, 0, 0};
            int locked = 0;
            for (int i = 0; i < 30; i++) { skate_update(&s, &in, &w, 1.0f / 60.0f); locked |= s.grinding && s.rail == 3 && s.ev_grind_kind == RAIL_KIND_LEDGE; }
            if (side) b = locked; else a = locked;
        }
        CHECK(a && !b, "ledge: top side locks (%d), the drop side does not (%d)", a, b);
        /* the same line travelled the other way (-z) is on the other side: its top is then to the rider's RIGHT of the rail's direction, so it still counts when the rider is on the + x side */
        Skate s; skate_init(&s, 20.15f, 1.1f, 20.0f, 3.14159f); s.grounded = 0; s.vel[2] = -5.0f;
        SkateInput in = { 0, 0, 0, 0, 1 , 0, 0, 0, 0, 0};
        int locked = 0;
        for (int i = 0; i < 30; i++) { skate_update(&s, &in, &w, 1.0f / 60.0f); locked |= s.grinding; }
        CHECK(locked, "ledge: grabbed from the top side while travelling against its direction too");
    }

    /* the HUD diagnostic says why the board does (not) lock on */
    {
        struct { float x, y, z, vx, vz; int grind; int grounded; float cool; int want; const char *what; } cases[] = {
            { 0.1f, 1.1f, 5.0f, 0, 5.0f, 1, 0, 0, GP_OK, "ready" },
            { 0.1f, 1.1f, 5.0f, 0, 5.0f, 0, 0, 0, GP_OK, "no trick input: still ready (the board locks on by itself)" },
            { 1.0f, 1.1f, 5.0f, 0, 5.0f, 1, 0, 0, GP_FAR, "too far sideways" },
            { 0.1f, 2.0f, 5.0f, 0, 5.0f, 1, 0, 0, GP_HIGH, "too high" },
            { 0.1f, 0.3f, 5.0f, 0, 5.0f, 1, 0, 0, GP_LOW, "too low" },
            { 0.1f, 1.1f, 5.0f, 0, 0.6f, 1, 0, 0, GP_SLOW, "too slow" },
            { 0.1f, 1.1f, 5.0f, 5.0f, 1.0f, 1, 0, 0, GP_ACROSS, "across" },
            { 0.1f, 1.1f, 5.0f, 0, 5.0f, 1, 1, 0, GP_GROUND, "on the ground" },
            { 0.1f, 1.1f, 5.0f, 0, 5.0f, 1, 0, 0.2f, GP_COOLDOWN, "cooling down" },
            { 20.15f, 1.1f, 0.0f, 0, 5.0f, 1, 0, 0, GP_OK, "ledge, from the top" },
            { 19.8f, 1.1f, 0.0f, 0, 5.0f, 1, 0, 0, GP_WRONG_SIDE, "ledge, from the drop side" },
        };
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            Skate q; skate_init(&q, cases[i].x, cases[i].y, cases[i].z, 0.0f);
            q.grounded = cases[i].grounded; q.vel[0] = cases[i].vx; q.vel[2] = cases[i].vz; q.grind_cool = cases[i].cool;
            if (cases[i].grounded) { q.speed = 5.0f; }
            SkateInput pi = { 0, 0, 0, 0, cases[i].grind, 0, 0, 0, 0, 0 };
            GrindProbe gp;
            int found = skate_grind_probe(&q, &pi, &w, &gp);
            CHECK(found && gp.reason == cases[i].want, "probe: %s -> reason %d (wanted %d, found %d)", cases[i].what, gp.reason, cases[i].want, found);
        }
        Skate q; skate_init(&q, 40.0f, 1.1f, 40.0f, 0.0f); SkateInput pi; memset(&pi, 0, sizeof(pi)); GrindProbe gp;
        CHECK(!skate_grind_probe(&q, &pi, &w, &gp), "nothing near: nothing found");
    }

    /* a rider on a flat floor next to a rail is not grinding */
    {
        Skate s; skate_init(&s, 0.1f, 0.0f, 1.0f, 0.0f); s.speed = 4.0f;
        SkateInput idle = { 0, 0, 0, 0, 0 , 0, 0, 0, 0, 0};
        for (int i = 0; i < 120; i++) skate_update(&s, &idle, &w, 1.0f / 60.0f);
        CHECK(!s.grinding && s.grounded, "rolling on the floor under a rail is not grinding");
    }

    /* ---- the real level ---- */
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 2; fclose(f);
        uint32_t *h = (uint32_t *)b;                                 /* header words: version at 1, nrails at 21, npts 22, off_rails 23 */
        CHECK(h[1] == 4, "level format version %u", h[1]);
        uint32_t nrails = h[21], npts = h[22], off = h[23];
        printf("level: %u rails, %u points\n", nrails, npts);
        Rails lr;
        if (!rails_build(&lr, (const uint32_t *)(b + off), nrails, (const float *)(b + off + 8 * nrails))) return 1;
        SkateWorld lw = { NULL, g_ground, g_wall, g_surface, &lr };   /* floors of the flat test mesh are irrelevant: the riders stay on the rails */
        int ok = 0; float minlen = 1e9f;
        for (uint32_t i = 0; i < lr.n; i++) {
            const Rail *a = &lr.rail[i];
            if (a->len < minlen) minlen = a->len;
            float st[3], d[3], en[3], dn[3];
            rails_point(&lr, i, 0.3f, st, d); rails_point(&lr, i, a->len, en, dn);
            float sp0 = sqrtf(2.0f * 0.9f * a->len * 1.3f);              /* fast enough to slide the whole length against the friction */
            if (sp0 < 4.0f) sp0 = 4.0f;
            if (sp0 > 11.0f) sp0 = 11.0f;
            float pp[3] = { st[0] + 0.1f * d[2], st[1] + 0.2f, st[2] - 0.1f * d[0] }, vv[3] = { d[0] * sp0, 0.0f, d[2] * sp0 };
            float hdg = atan2f(d[0], d[2]);
            Run rr = fly(&lw, pp, vv, hdg, 0);
            int good = rr.attach_frame >= 0 && rr.attach_frame < 12 && rr.left_rail && rr.grinding_frames >= 5;
            if (!good) printf("  rail %u (%.1f m, from (%.1f %.2f %.1f)): attach frame %d, left %d, frames on rail %d\n", i, a->len, st[0], st[1], st[2], rr.attach_frame, rr.left_rail, rr.grinding_frames);
            ok += good;
        }
        CHECK(ok == (int)lr.n, "%d of %u level rails: rider locks on and slides off the end", ok, lr.n);
        printf("level rails: %d / %u grind from end to end (shortest %.1f m)\n", ok, lr.n, minlen);
        rails_free(&lr);
        free(b);
    }
    rails_free(&rl);
    printf(fails ? "%d FAILURES\n" : "all rail tests passed\n", fails);
    return fails != 0;
}
