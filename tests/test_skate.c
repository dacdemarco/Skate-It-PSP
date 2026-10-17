/* Host tests for src/skate.c on synthetic collision meshes (the same coll.c queries the PSP uses): flat floor, wall, gentle slope, ledge,
 * steep ramps and flanks, a lip launch, a landing on a slope, and a crack between floor polygons. */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../src/skate.h"
#include "../src/coll.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- tiny mesh builder ------------------------------------------------------------------------ */
typedef struct { float v[3 * 256]; CollPoly p[128]; int nv, np; Coll c; } Mesh;

static void quad(Mesh *m, float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3)
{
    float pts[12] = { x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3 };
    int base = m->nv;
    memcpy(m->v + 3 * m->nv, pts, sizeof(pts));
    m->nv += 4;
    m->p[m->np].v[0] = (uint16_t)base; m->p[m->np].v[1] = (uint16_t)(base + 1); m->p[m->np].v[2] = (uint16_t)(base + 2); m->p[m->np].v[3] = (uint16_t)(base + 3);
    m->p[m->np].flags = 0;
    m->np++;
}
/* horizontal strip in z: y(z0) -> y(z1), x in [-20, 20] */
static void strip(Mesh *m, float z0, float y0, float z1, float y1) { quad(m, -20, y0, z0, 20, y0, z0, 20, y1, z1, -20, y1, z1); }
/* vertical wall across the strip at z, from y0 to y1 */
static void wall_at(Mesh *m, float z, float y0, float y1) { quad(m, -20, y0, z, 20, y0, z, 20, y1, z, -20, y1, z); }
static void finish(Mesh *m) { if (!coll_build(&m->c, m->v, (uint32_t)m->nv, m->p, (uint32_t)m->np, 4.0f)) { printf("mesh build failed\n"); fails++; } }

static Coll *cur;
static int g_ground(void *c, float x, float z, float ymax, float *y) { (void)c; return coll_ground(cur, x, z, ymax, y); }
static int g_wall(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_wall(cur, a, b, h, n); }
static int g_surface(void *c, const float a[3], const float b[3], float h[3], float n[3]) { (void)c; return coll_ray(cur, a, b, COLL_RIDE_NY, 2.0f, 0.06f, h, n); }
static const SkateWorld W = { NULL, g_ground, g_wall, g_surface, NULL };

static void run(Skate *s, SkateInput in, float secs)
{
    for (int i = 0; i < (int)(secs * 60.0f); i++) skate_update(s, &in, &W, 1.0f / 60.0f);
}

/* ride with a 90 Hz fixed step, tracking extremes */
typedef struct { float miny, maxy, minz, maxz; int air_frames, max_air_run; } Track;
static void ride(Skate *s, float secs, Track *t)
{
    SkateInput idle = { 0, 0, 0, 0, 0, 0, 0 , 0, 0, 0};
    int run_air = 0;
    t->miny = 1e9f; t->maxy = -1e9f; t->minz = 1e9f; t->maxz = -1e9f; t->air_frames = 0; t->max_air_run = 0;
    for (int i = 0; i < (int)(secs * 90.0f); i++) {
        skate_update(s, &idle, &W, 1.0f / 90.0f);
        if (s->pos[1] < t->miny) t->miny = s->pos[1];
        if (s->pos[1] > t->maxy) t->maxy = s->pos[1];
        if (s->pos[2] < t->minz) t->minz = s->pos[2];
        if (s->pos[2] > t->maxz) t->maxz = s->pos[2];
        if (!s->grounded) { t->air_frames++; run_air++; if (run_air > t->max_air_run) t->max_air_run = run_air; } else run_air = 0;
    }
}

/* flat y=0 for z < 10, then a ramp of the given angle up to height h; optionally a plateau at height h behind it */
static void ramp_mesh(Mesh *m, float deg, float h, int plateau)
{
    memset(m, 0, sizeof(*m));
    float run = h / tanf(deg * 0.0174533f);
    strip(m, -60, 0, 10, 0);
    strip(m, 10, 0, 10 + run, h);
    if (plateau) strip(m, 10 + run, h, 10 + run + 30, h);
    finish(m);
}

int main(void)
{
    SkateInput idle = { 0, 0, 0, 0, 0, 0, 0 , 0, 0, 0};
    Skate s;
    static Mesh flat, slope, ledge, ramp;

    /* flat world with a wall at z = 20 */
    memset(&flat, 0, sizeof(flat));
    strip(&flat, -60, 0, 60, 0); wall_at(&flat, 20, -5, 5); finish(&flat);
    /* gentle slope (0.2 per metre, 11 degrees) from z=5 to z=25, then flat; wall at z=20 */
    memset(&slope, 0, sizeof(slope));
    strip(&slope, -60, 0, 5, 0); strip(&slope, 5, 0, 25, -4); strip(&slope, 25, -4, 60, -4); wall_at(&slope, 20, -8, 5); finish(&slope);
    /* ledge: floor at 0 until z=10, floor at -3 behind it (no wall) */
    memset(&ledge, 0, sizeof(ledge));
    strip(&ledge, -60, 0, 10, 0); strip(&ledge, 10, -3, 60, -3); finish(&ledge);

    cur = &flat.c;
    /* 1. push accelerates, friction slows */
    skate_init(&s, 0, 0, 0, 0);
    SkateInput push = { 0, 1, 0, 0, 0, 0, 0 , 0, 0, 0};
    skate_update(&s, &push, &W, 1.0f / 60.0f);
    run(&s, idle, 1.0f);
    CHECK(s.speed > 1.5f && s.speed < SKATE_MAX_SPEED, "push speed %.2f", s.speed);
    float v1 = s.speed;
    run(&s, idle, 2.0f);
    CHECK(s.speed < v1 && s.speed > 0.0f, "friction %.2f -> %.2f", v1, s.speed);
    CHECK(s.pos[2] > 1.0f, "moved forward %.2f", s.pos[2]);
    /* repeated pushes cap at max speed */
    skate_init(&s, 0, 0, -40, 0);
    for (int i = 0; i < 12; i++) { skate_update(&s, &push, &W, 1.0f / 60.0f); run(&s, idle, 0.8f); s.pos[2] = -40; }
    CHECK(s.speed <= SKATE_MAX_SPEED + 0.01f, "max speed %.2f", s.speed);
    /* brake */
    SkateInput brake = { 0, 0, 0, 1, 0, 0, 0 , 0, 0, 0};
    run(&s, brake, 2.0f);
    CHECK(s.speed == 0.0f, "brake to stop %.2f", s.speed);

    /* 2. steering: left (+1) turns heading up and moves towards +x (left of +z) */
    skate_init(&s, 0, 0, 0, 0); s.speed = 4.0f;
    SkateInput left = { 1.0f, 0, 0, 0, 0, 0, 0 , 0, 0, 0};
    run(&s, left, 1.0f);
    CHECK(s.heading > 0.5f, "heading after left %.2f", s.heading);
    CHECK(s.pos[0] > 0.3f, "x after left %.2f", s.pos[0]);
    skate_init(&s, 0, 0, 0, 0); s.speed = 4.0f;
    SkateInput right = { -1.0f, 0, 0, 0, 0, 0, 0 , 0, 0, 0};
    run(&s, right, 1.0f);
    CHECK(s.pos[0] < -0.3f, "x after right %.2f", s.pos[0]);

    /* 3. ollie: hold then release */
    skate_init(&s, 0, 0, 0, 0); s.speed = 3.0f;
    SkateInput hold = { 0, 0, 1, 0, 0, 0, 0 , 0, 0, 0};
    run(&s, hold, 0.35f);
    CHECK(s.grounded && s.charge > 0.9f, "charge %.2f", s.charge);
    float apex = 0.0f; int took_off = 0, landed = 0, pops = 0; int was_grounded = 1;
    for (int i = 0; i < 120; i++) {
        skate_update(&s, &idle, &W, 1.0f / 60.0f);
        if (!s.grounded) took_off = 1;
        if (was_grounded && !s.grounded) pops++;
        was_grounded = s.grounded;
        if (s.pos[1] > apex) apex = s.pos[1];
        if (s.landed) landed++;
    }
    CHECK(took_off && pops == 1, "ollie took off %d pops %d", took_off, pops);
    CHECK(apex > 0.35f && apex < 0.9f, "ollie apex %.2f", apex);
    CHECK(landed == 1 && s.grounded && fabsf(s.pos[1]) < 1e-4f, "landed %d grounded %d y %.3f", landed, s.grounded, s.pos[1]);
    CHECK(s.crouch < 0.01f, "landing absorb decayed %.2f", s.crouch);
    /* short tap does not pop */
    skate_init(&s, 0, 0, 0, 0);
    run(&s, hold, 0.05f); run(&s, idle, 0.5f);
    CHECK(s.grounded, "short tap must not pop");

    /* 4. wall blocks */
    skate_init(&s, 0, 0, 18.0f, 0); s.speed = 6.0f;
    run(&s, idle, 3.0f);
    CHECK(s.pos[2] < 20.0f, "wall: z %.2f", s.pos[2]);
    CHECK(s.speed < 3.0f, "wall: speed after hit %.2f", s.speed);

    /* 5. slope: downhill accelerates, normal tilts */
    cur = &slope.c;
    skate_init(&s, 0, -0.6f, 8.0f, 0); s.speed = 1.0f;
    run(&s, idle, 2.0f);
    CHECK(s.speed > 3.0f, "downhill gain %.2f", s.speed);
    CHECK(s.up[2] > 0.1f && s.up[1] > 0.9f, "slope normal (%.2f %.2f %.2f)", s.up[0], s.up[1], s.up[2]);
    /* uphill: starts fast, slows, never falls through */
    skate_init(&s, 0, -4.0f, 40.0f, 3.14159f); s.speed = 0.0f;   /* facing -z at the bottom of the slope... rolls nowhere */
    run(&s, idle, 1.0f);
    CHECK(s.grounded && fabsf(s.pos[1] + 4.0f) < 0.01f, "stays on the lower flat: y %.3f", s.pos[1]);

    /* 6. ledge: rolling off drops the rider, lands lower */
    cur = &ledge.c;
    skate_init(&s, 0, 0, 8.0f, 0); s.speed = 5.0f;
    int air = 0;
    for (int i = 0; i < 240; i++) { skate_update(&s, &idle, &W, 1.0f / 60.0f); if (!s.grounded) air = 1; }
    CHECK(air && s.grounded && fabsf(s.pos[1] + 3.0f) < 0.01f, "ledge: air %d grounded %d y %.3f", air, s.grounded, s.pos[1]);

    /* 7. body matrix is orthonormal */
    skate_init(&s, 1, 2, 3, 0.7f);
    float m[16]; skate_body_matrix(&s, m);
    float d01 = m[0] * m[4] + m[1] * m[5] + m[2] * m[6], d02 = m[0] * m[8] + m[1] * m[9] + m[2] * m[10];
    CHECK(fabsf(d01) < 1e-5f && fabsf(d02) < 1e-5f, "matrix not orthogonal");
    /* local +z (nose) maps to the heading direction */
    CHECK(fabsf(m[8] - sinf(0.7f)) < 1e-5f && fabsf(m[10] - cosf(0.7f)) < 1e-5f, "nose direction");
    /* local +x (left) = (cos h, 0, -sin h) */
    CHECK(fabsf(m[0] - cosf(0.7f)) < 1e-5f && fabsf(m[2] + sinf(0.7f)) < 1e-5f, "left direction");

    /* the mirror image for a rider who rides regular: the left axis reversed, the nose and the up axis as they were, the position the same */
    {
        float r[16];
        memcpy(r, m, sizeof(r));
        skate_mirror_matrix(r);
        CHECK(fabsf(r[0] + m[0]) < 1e-6f && fabsf(r[1] + m[1]) < 1e-6f && fabsf(r[2] + m[2]) < 1e-6f && fabsf(r[8] - m[8]) < 1e-6f && fabsf(r[5] - m[5]) < 1e-6f && r[12] == m[12] && r[13] == m[13] && r[14] == m[14], "mirror: left reversed, the rest kept");
        float det = r[0] * (r[5] * r[10] - r[6] * r[9]) - r[1] * (r[4] * r[10] - r[6] * r[8]) + r[2] * (r[4] * r[9] - r[5] * r[8]);
        CHECK(det < -0.999f && det > -1.001f, "a mirror image (determinant %.3f)", det);
        skate_mirror_matrix(r);
        CHECK(memcmp(r, m, sizeof(r)) == 0, "mirroring twice gives the original");
    }

    /* ---- slope physics ---- */
    Track t;
    /* 8. a 60 degree ramp, 3 m high with a plateau: 8 m/s has the energy (v^2/2g = 3.3 m of rise), the rider ends up on top */
    ramp_mesh(&ramp, 60.0f, 3.0f, 1); cur = &ramp.c;
    skate_init(&s, 0, 0, 0, 0); s.speed = 11.0f;                 /* ~6 m of rise worth of speed (max speed clamp is 12) */
    ride(&s, 3.0f, &t);
    CHECK(s.grounded && s.pos[1] > 2.99f && s.pos[1] < 3.01f, "ramp: rider on the plateau, y %.2f grounded %d", s.pos[1], s.grounded);
    CHECK(t.miny > -0.05f, "ramp: never below the floor (min y %.3f)", t.miny);
    /* 9. same ramp, too slow: climbs part of the way, stops, rolls back to the bottom facing the ramp, never falls through */
    skate_init(&s, 0, 0, 8.5f, 0); s.speed = 5.0f;              /* 1.5 m run-up; rise capacity v^2/2g = 1.27 m minus drag */
    ride(&s, 6.0f, &t);
    CHECK(t.maxy > 0.95f && t.maxy < 1.4f, "slow ramp: climbs to %.2f (expected ~1.2)", t.maxy);
    CHECK(s.grounded && t.miny > -0.05f, "slow ramp: stays on the surface (min y %.3f grounded %d)", t.miny, s.grounded);
    CHECK(s.speed < 0.0f || s.pos[2] < 10.5f, "slow ramp: rolls back (speed %.2f z %.2f)", s.speed, s.pos[2]);
    /* 10. a 75 degree flank (bowl side): the same, no clipping */
    ramp_mesh(&ramp, 75.0f, 3.0f, 1); cur = &ramp.c;
    skate_init(&s, 0, 0, 0, 0); s.speed = 7.0f;                  /* v^2/2g = 2.5 m: not enough for the top */
    ride(&s, 6.0f, &t);
    CHECK(t.maxy > 1.5f && t.maxy < 3.0f, "flank: climbs to %.2f", t.maxy);
    CHECK(t.miny > -0.05f && s.grounded, "flank: no clipping (min y %.3f grounded %d)", t.miny, s.grounded);
    /* 11. hitting the flank at an angle and while turning stays on it as well */
    skate_init(&s, 3.0f, 0, 0, 0.5f); s.speed = 7.0f;
    ride(&s, 6.0f, &t);
    CHECK(t.miny > -0.05f, "flank at an angle: no clipping (min y %.3f)", t.miny);
    /* 12. lip launch: 45 degree ramp, 2 m high, nothing behind it for 2 m, then a floor at 0 again: fly over the gap and land */
    memset(&ramp, 0, sizeof(ramp));
    strip(&ramp, -60, 0, 10, 0); strip(&ramp, 10, 0, 12, 2); strip(&ramp, 14, 0, 60, 0); finish(&ramp); cur = &ramp.c;
    skate_init(&s, 0, 0, 0, 0); s.speed = 9.0f;
    ride(&s, 4.0f, &t);
    CHECK(t.air_frames > 20, "lip launch: left the ramp (air frames %d)", t.air_frames);
    CHECK(s.grounded && fabsf(s.pos[1]) < 0.01f && s.pos[2] > 14.0f, "lip launch: landed beyond the gap, y %.3f z %.2f", s.pos[1], s.pos[2]);
    CHECK(t.miny > -0.05f, "lip launch: never fell into the gap (min y %.3f)", t.miny);
    /* 13. landing on a steep slope from above: dropped 2 m onto a 70 degree flank, ends on it */
    ramp_mesh(&ramp, 70.0f, 4.0f, 1); cur = &ramp.c;
    skate_init(&s, 0, 0, 0, 0);
    s.pos[0] = 0; s.pos[1] = 4.0f; s.pos[2] = 10.0f + 0.5f / tanf(70.0f * 0.0174533f); s.grounded = 0; s.vel[0] = 0; s.vel[1] = 0; s.vel[2] = 3.0f; s.speed = 3.0f;
    ride(&s, 2.0f, &t);
    CHECK(t.miny > -0.05f && s.grounded, "slope landing: on the surface (min y %.3f grounded %d)", t.miny, s.grounded);
    /* 14. a crack between two floor polygons (5 cm) is not a hole */
    memset(&ramp, 0, sizeof(ramp));
    strip(&ramp, -60, 0, 10, 0); strip(&ramp, 10.05f, 0, 60, 0); finish(&ramp); cur = &ramp.c;
    skate_init(&s, 0, 0, 5.0f, 0); s.speed = 6.0f;
    ride(&s, 1.5f, &t);
    CHECK(s.grounded && t.miny > -0.01f && t.max_air_run <= 2, "crack: crossed (min y %.3f, longest air run %d frames, grounded %d)", t.miny, t.max_air_run, s.grounded);
    /* 15. stairs: 0.17 m risers are climbed, a 0.5 m riser blocks */
    memset(&ramp, 0, sizeof(ramp));
    strip(&ramp, -60, 0, 10, 0);
    for (int i = 0; i < 4; i++) {                                 /* 4 steps of 0.17 m, 0.6 m deep */
        float z = 10 + i * 0.6f, y = (i + 1) * 0.17f;
        quad(&ramp, -20, y, z, 20, y, z, 20, y, z + 0.6f, -20, y, z + 0.6f);
        wall_at(&ramp, z, i * 0.17f, y);
    }
    strip(&ramp, 12.4f, 0.68f, 60, 0.68f); wall_at(&ramp, 12.4f, 0.51f, 0.68f);
    finish(&ramp); cur = &ramp.c;
    skate_init(&s, 0, 0, 5.0f, 0); s.speed = 5.0f;
    ride(&s, 2.0f, &t);
    CHECK(s.pos[1] > 0.6f && s.pos[2] > 12.4f, "stairs: climbed (y %.2f z %.2f)", s.pos[1], s.pos[2]);
    memset(&ramp, 0, sizeof(ramp));
    strip(&ramp, -60, 0, 10, 0); strip(&ramp, 10, 0.5f, 60, 0.5f); wall_at(&ramp, 10, 0, 0.5f); finish(&ramp); cur = &ramp.c;
    skate_init(&s, 0, 0, 5.0f, 0); s.speed = 5.0f;
    ride(&s, 2.0f, &t);
    CHECK(s.pos[1] < 0.01f && s.pos[2] < 10.0f, "0.5 m riser blocks (y %.2f z %.2f)", s.pos[1], s.pos[2]);

    printf(fails ? "%d FAILURES\n" : "all skate tests passed\n", fails);
    return fails != 0;
}
