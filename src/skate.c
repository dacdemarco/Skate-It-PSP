#include "skate.h"
#include "rails.h"
#include "trick.h"
#include <math.h>
#include <string.h>

#define STEP_UP      0.28f     /* surfaces up to this much higher are climbed (stair risers are ~0.17) */
#define STEP_DOWN    0.30f     /* ground this much lower is still followed; beyond that the rider leaves the surface */
#define ROLL_DRAG    0.55f     /* m/s^2 */
#define BRAKE_DECEL  7.0f
#define PUSH_GAIN    2.4f      /* m/s per push (a tap), applied over the push animation */
#define PUSH_GAIN_HOLD 4.2f    /* the strong push: the button still held when the foot plants (PUSH_PLANT of the tap's duration) */
#define PUSH_PLANT   0.25f     /* the push is delivered from this fraction of its duration (the foot is on the ground) ... */
#define PUSH_LIFT    0.60f     /* ... to this one */
#define GRAV         (SKATE_GRAVITY * skate_tune.gravity)
#define WALL_MARGIN  0.35f
#define FEET_LIFT    0.05f     /* the sweep that finds rideable surfaces ahead runs this far above the one we roll on */
#define AIR_LIFT     (STEP_UP * 0.5f)
#define PI_F         3.14159265f
#define SPIN_RATE    9.0f      /* rad/s of an air spin (515 degrees a second: a half turn in 0.35 s) */
#define SPIN_LAND_TOL 0.75f    /* rad (43 degrees): landing a spin further than this from a half turn is a sketchy landing */
#define REVERT_MIN_SPEED 1.5f
#define SLIDE_MIN_SPEED  3.0f  /* m/s to start a powerslide, it ends below SLIDE_END_SPEED */
#define SLIDE_END_SPEED  1.2f
#define SLIDE_DECEL      4.5f  /* m/s^2 on top of the rolling drag while sliding */
#define WALL_MIN_SPEED   3.0f  /* wallride: horizontal speed to start, and along the wall */
#define WALL_MIN_ALONG   2.5f
#define WALL_MAX_TIME    1.3f  /* s */
#define WALL_GAP         0.06f /* the board rides this far off the wall */
#define MIN_HORIZ    0.25f     /* heading follows fwd only while fwd is this far from vertical */
/* grinding: lock on to a rail when the board's contact point comes within GRIND_HORIZ of it sideways and from GRIND_BELOW under to GRIND_ABOVE over its top
 * (an ollie of 0.45-0.74 m therefore reaches rails up to ~1.1 m above the take-off), moving along it fast enough and not across it */
#define GRIND_HORIZ    0.35f
#define GRIND_BELOW    0.35f
#define GRIND_ABOVE    0.45f
#define GRIND_MIN_SPEED 1.2f   /* m/s along the rail (horizontal) to lock on */
#define GRIND_ALIGN    0.45f   /* |cos| of the angle between the horizontal velocity and the rail */
#define GRIND_FRICTION 0.9f    /* m/s^2, a bit more than rolling */
#define GRIND_MIN_EXIT 0.5f    /* below this speed the rider drops off the rail */
#define GRIND_COOLDOWN 0.30f   /* s without re-locking after leaving a rail */
#define GRIND_HOP_HOLD 0.25f   /* the pop button held this long on a rail before the release = a jump OFF the rail (shorter = a hop straight up that comes back down on it) */
#define GRIND_HOP_SIDE 1.6f    /* m/s sideways of a jump off (towards the nub, else away from a ledge's drop / to the left of a rail) */
#define GRIND_HOP_COOL 0.9f    /* s without re-locking after a jump off */
/* balance: lean'' = TIP*diff*lean + WOBBLE*diff*ramp*wobble(t) + INPUT*stick - DAMP*lean'; |lean| >= 1 = bail. Without any input the rider stays up for about 3 s on the easiest
 * grind; the stick (tilted AWAY from the lean) always wins. The wobble builds up over GRACE seconds after locking on. */
#define LEAN_TIP       1.5f
#define LEAN_WOBBLE    1.1f
#define LEAN_INPUT     6.0f
#define LEAN_DAMP      1.4f
#define LEAN_GRACE     0.6f
#define LEAN_ENTRY     0.4f    /* lean at lock-on when the board lands a full GRIND_HORIZ off the line */
/* manuals: roll on the rear wheels (or the front ones) while balancing, see manual_prepare */
#define MANUAL_MIN_SPEED  1.5f    /* m/s to start one */
#define MANUAL_END_SPEED  0.8f    /* below this the manual ends */
#define MANUAL_DRAG       0.3f    /* m/s^2 on top of the rolling drag */
#define MANUAL_TURN       0.6f    /* steering is this much of normal */
#define MANUAL_PITCH      0.30f   /* board tilt (rad) at the balance point; + MANUAL_PITCH_VAR per unit of lean */
#define MANUAL_PITCH_VAR  0.14f
#define MANUAL_PIVOT      0.40f   /* m from the board's centre to the wheels it stands on */
#define MANUAL_ENTRY_LEAN 0.20f   /* the nose pops up on entry: it starts a bit past the balance point and moving */
#define MANUAL_ENTRY_PUSH 0.40f
#define BAIL_TIME      1.6f    /* s without control after falling */

SkateTune skate_tune = { 1.0f, 1.0f, 1.0f };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
/* The PSP has a single-precision FPU only: libm's expf/sinf/cosf/hypotf (double inside) are slow there, and the physics calls them every step. */
static float hyp2(float x, float z) { return sqrtf(x * x + z * z); }
static float decay(float x) { return 1.0f / (1.0f + x + 0.5f * x * x + x * x * x * (1.0f / 6.0f)); }     /* exp(-x), x in [0, ~0.5] */
/* atan2 within 1e-4 rad: plenty for a heading that only steers the camera and the pose */
static float fast_atan2(float y, float x)
{
    float ax = fabsf(x), ay = fabsf(y), mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx < 1e-12f) return 0.0f;
    float r = mn / mx, r2 = r * r;
    float a = r * (0.9998660f + r2 * (-0.3302995f + r2 * (0.1801410f + r2 * (-0.0851330f + r2 * 0.0208351f))));   /* atan(r), r in [0,1], error < 1e-4 */
    if (ay > ax) a = 1.5707963f - a;
    if (x < 0.0f) a = 3.1415927f - a;
    return y < 0.0f ? -a : a;
}
static void rot_cs(float th, float *c, float *s)         /* cos/sin of a small turn per step */
{
    if (th > 0.5f || th < -0.5f) { *c = cosf(th); *s = sinf(th); return; }
    float t2 = th * th;
    *c = 1.0f - 0.5f * t2 + t2 * t2 * (1.0f / 24.0f);
    *s = th * (1.0f - t2 * (1.0f / 6.0f) + t2 * t2 * (1.0f / 120.0f));
}
static float dotv(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float lenv(const float a[3]) { return sqrtf(dotv(a, a)); }

static void normv(float v[3])
{
    float l = lenv(v);
    if (l > 1e-8f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void crossv(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}

/* fwd := fwd projected onto the plane with normal n (unit). If fwd is (nearly) along n, face up the slope instead. */
static void project_fwd(float fwd[3], const float n[3])
{
    float d = dotv(fwd, n);
    float f[3] = { fwd[0] - n[0] * d, fwd[1] - n[1] * d, fwd[2] - n[2] * d };
    if (lenv(f) < 1e-3f) {
        float up[3] = { 0.0f, 1.0f, 0.0f };
        float du = n[1];
        f[0] = up[0] - n[0] * du; f[1] = up[1] - n[1] * du; f[2] = up[2] - n[2] * du;
    }
    normv(f);
    fwd[0] = f[0]; fwd[1] = f[1]; fwd[2] = f[2];
}

static void sync_heading(Skate *s)
{
    if (hyp2(s->fwd[0], s->fwd[2]) > MIN_HORIZ)
        s->heading = fast_atan2(s->fwd[0], s->fwd[2]);
}

void skate_init(Skate *s, float x, float y, float z, float heading)
{
    s->pos[0] = x; s->pos[1] = y; s->pos[2] = z;
    s->heading = heading;
    s->speed = 0.0f; s->vy = 0.0f; s->grounded = 1;
    s->steer = 0.0f; s->charge = 0.0f; s->absorb = 0.0f; s->crouch = 0.0f; s->push_t = -1.0f; s->push_boost = 0.0f; s->push_dur = SKATE_PUSH_TIME; s->push_strong = 0; s->air_t = 0.0f; s->pop_hold_t = 0.0f;
    s->up[0] = 0.0f; s->up[1] = 1.0f; s->up[2] = 0.0f;
    s->cn[0] = 0.0f; s->cn[1] = 1.0f; s->cn[2] = 0.0f;
    s->fwd[0] = sinf(heading); s->fwd[1] = 0.0f; s->fwd[2] = cosf(heading);
    s->vel[0] = s->vel[1] = s->vel[2] = 0.0f;
    s->grinding = 0; s->rail = 0; s->rail_t = 0.0f; s->rail_sign = 1.0f; s->grind_cool = 0.0f; s->grind_time = 0.0f;
    s->grind_type = 0; s->lean = s->lean_v = s->roll = 0.0f; s->lean_armed = 1;
    s->bail_t = 0.0f; s->bail_dir = 1.0f;
    s->manual = 0; s->manual_time = 0.0f; s->pitch = 0.0f; s->ev_manual_start = s->ev_manual_end = 0;
    s->flip = 0; s->flip_t = 0.0f; s->flip_dur = 0.5f; s->grab = 0; s->grab_time = 0.0f;
    s->ev_flip_done = s->ev_flip_type = s->ev_grab_start = s->ev_grab_end = s->ev_grab_type = s->ev_grab_switch = 0; s->grind_bs = 0;
    s->ev_grind_start = s->ev_grind_end = s->ev_bail = 0; s->ev_grind_kind = 0; s->ev_grind_switch = 0;
    s->ev_ollie = s->ev_ollie_plain = s->ev_ollie_nollie = s->ev_land = s->ev_scrape = 0; s->scrape_side = 1.0f; s->ev_grind_mat = 0; s->ollie_pop = s->land_impact = 0.0f;
    s->landed = 0;
    s->flip_nollie = 0; s->flip_roll = 0.0f; s->ev_flip_nollie = 0;
    s->spin_ang = 0.0f; s->spin_dir = 0.0f; s->fakie_takeoff = 0; s->ev_spin = s->ev_spin_deg = s->ev_spin_dir = s->ev_spin_fakie = 0;
    s->vis_yaw = s->vis_target = 0.0f; s->ev_revert = 0; s->sliding = 0; s->slide_dir = 0.0f; s->slide_t = 0.0f; s->ev_slide_start = s->ev_slide_end = 0;
    s->wallriding = 0; s->wall_t = 0.0f; s->wall_side = 1.0f; s->wall_held = 0; s->ev_wall_start = s->ev_wall_end = 0;
}

/* ---- rolling on a surface ---------------------------------------------------------------------- */
static void leave_surface(Skate *s)
{
    s->fakie_takeoff = s->speed < 0.0f; s->spin_ang = 0.0f; s->spin_dir = 0.0f;
    s->grounded = 0; s->air_t = 0.0f;
    for (int i = 0; i < 3; i++) s->vel[i] = s->fwd[i] * s->speed;       /* keeps its momentum, including the vertical part off a crest */
    float h = hyp2(s->fwd[0], s->fwd[2]);                              /* the facing stays level in the air */
    if (h > 1e-3f) { s->fwd[0] /= h; s->fwd[2] /= h; s->fwd[1] = 0.0f; }
    else { s->fwd[0] = sinf(s->heading); s->fwd[1] = 0.0f; s->fwd[2] = cosf(s->heading); }
}

/* Walls in the way of a move of length d (plus margin) along dir (tangent to the surface, unit). Two probes at 0.3 m and 0.9 m above the surface along
 * the tangent, and - on slopes - two more along the horizontal part of the motion from 0.3 m / 0.9 m above the feet: on a steep ramp the tangent
 * runs parallel to a wall at its top and would never see it. Returns 1 and the wall's normal (facing the rider) if one is hit. */
static int probe_walls(const Skate *s, const SkateWorld *w, const float pos[3], const float cn[3], const float dir[3], float reach, float bn_out[3])
{
    float hit[3], n[3], best = 1e30f;
    int blocked = 0;
    if (!w->wall)
        return 0;
    for (int k = 0; k < 2; k++) {
        float off = k ? 0.9f : 0.3f;
        float a[3] = { pos[0] + cn[0] * off, pos[1] + cn[1] * off, pos[2] + cn[2] * off };
        float b[3] = { a[0] + dir[0] * reach, a[1] + dir[1] * reach, a[2] + dir[2] * reach };
        if (w->wall(w->ctx, a, b, hit, n)) {
            float t = (hit[0] - a[0]) * (hit[0] - a[0]) + (hit[1] - a[1]) * (hit[1] - a[1]) + (hit[2] - a[2]) * (hit[2] - a[2]);
            if (t < best) { best = t; bn_out[0] = n[0]; bn_out[1] = n[1]; bn_out[2] = n[2]; blocked = 1; }
        }
    }
    float hl = hyp2(dir[0], dir[2]);
    if (cn[1] < 0.98f && hl > 0.05f) {
        float hx = dir[0] / hl, hz = dir[2] / hl;
        for (int k = 0; k < 2; k++) {
            float off = k ? 0.9f : 0.3f;
            float a[3] = { pos[0], pos[1] + off, pos[2] };
            float b[3] = { a[0] + hx * reach * hl, a[1], a[2] + hz * reach * hl };
            if (reach * hl > 0.02f && w->wall(w->ctx, a, b, hit, n)) {
                float t = (hit[0] - a[0]) * (hit[0] - a[0]) + (hit[1] - a[1]) * (hit[1] - a[1]) + (hit[2] - a[2]) * (hit[2] - a[2]);
                if (t < best) { best = t; bn_out[0] = n[0]; bn_out[1] = n[1]; bn_out[2] = n[2]; blocked = 1; }
            }
        }
    }
    (void)s;
    return blocked;
}

/* stay on the surface: look for it from just above (stair treads up to STEP_UP higher) to STEP_DOWN below along the normal. Returns 0 if there is nothing to stand on. */
static int snap_to_surface(Skate *s, const SkateWorld *w)
{
    float a[3] = { s->pos[0] + s->cn[0] * STEP_UP, s->pos[1] + s->cn[1] * STEP_UP, s->pos[2] + s->cn[2] * STEP_UP };
    float b[3] = { s->pos[0] - s->cn[0] * STEP_DOWN, s->pos[1] - s->cn[1] * STEP_DOWN, s->pos[2] - s->cn[2] * STEP_DOWN };
    float hit[3], n[3], gy;
    if (w->surface && w->surface(w->ctx, a, b, hit, n) && n[1] > 0.0f) {
        for (int i = 0; i < 3; i++) { s->pos[i] = hit[i]; s->cn[i] = n[i]; }
        project_fwd(s->fwd, s->cn);
        return 1;
    }
    if (s->cn[1] > 0.7f && w->ground && w->ground(w->ctx, s->pos[0], s->pos[2], s->pos[1] + STEP_UP, &gy) && gy > s->pos[1] - STEP_DOWN) {
        s->pos[1] = gy;                                  /* a crack between polygons: the height query bridges it */
        return 1;
    }
    return 0;
}

static void ground_move(Skate *s, const SkateWorld *w, float dt)
{
    float old_pos[3] = { s->pos[0], s->pos[1], s->pos[2] }, old_cn[3] = { s->cn[0], s->cn[1], s->cn[2] }, old_fwd[3] = { s->fwd[0], s->fwd[1], s->fwd[2] };
    int blocked = 0;
    float sgn = s->speed >= 0.0f ? 1.0f : -1.0f;
    float d = fabsf(s->speed) * dt;
    if (d > 1e-7f) {
        float dir[3] = { s->fwd[0] * sgn, s->fwd[1] * sgn, s->fwd[2] * sgn };
        float hit[3], n[3], bn[3] = { 0, 0, 0 };
        /* walls in the way (a margin beyond the step keeps the rider off them) */
        blocked = probe_walls(s, w, s->pos, s->cn, dir, d + WALL_MARGIN, bn);
        if (blocked) {
            /* slide along the wall: remove the motion component into it, keep a little forward progress */
            float mv[3] = { dir[0] * d, dir[1] * d, dir[2] * d };
            float into = dotv(mv, bn);
            float sl[3] = { mv[0] - bn[0] * into, mv[1] - bn[1] * into, mv[2] - bn[2] * into };
            float nd = dotv(sl, s->cn);
            sl[0] -= s->cn[0] * nd; sl[1] -= s->cn[1] * nd; sl[2] -= s->cn[2] * nd;
            float slen = lenv(sl);
            if (slen > d) { sl[0] *= d / slen; sl[1] *= d / slen; sl[2] *= d / slen; }
            for (int i = 0; i < 3; i++) s->pos[i] += sl[i] * 0.5f;
            /* lose speed in proportion to how head-on the hit was */
            float head_on = fabsf(dotv(s->fwd, bn));
            if (fabsf(s->speed) > 1.5f) {                              /* scraping along a wall: for the sound (the side the wall is on) */
                s->ev_scrape++;
                s->scrape_side = (bn[0] * s->fwd[2] - bn[2] * s->fwd[0]) < 0.0f ? 1.0f : -1.0f;
            }
            s->speed *= 1.0f - 0.85f * head_on;
        } else {
            /* a rideable surface ahead (the foot of a ramp or bowl flank): roll onto it */
            float a[3] = { s->pos[0] + s->cn[0] * FEET_LIFT, s->pos[1] + s->cn[1] * FEET_LIFT, s->pos[2] + s->cn[2] * FEET_LIFT };
            float b[3] = { a[0] + dir[0] * d, a[1] + dir[1] * d, a[2] + dir[2] * d };
            if (w->surface && w->surface(w->ctx, a, b, hit, n) && n[1] > 0.0f) {
                for (int i = 0; i < 3; i++) { s->pos[i] = hit[i]; s->cn[i] = n[i]; }
                project_fwd(s->fwd, s->cn);
            } else {
                for (int i = 0; i < 3; i++) s->pos[i] += dir[i] * d;
            }
        }
    }
    if (snap_to_surface(s, w))
        return;
    if (!blocked && d > 1e-7f) {
        /* the surface ended: is that a lip to fly off, or a wall just ahead (a steep ramp meeting a wall)? look further than the usual probes */
        float dir[3] = { old_fwd[0] * sgn, old_fwd[1] * sgn, old_fwd[2] * sgn }, bn[3];
        blocked = probe_walls(s, w, old_pos, old_cn, dir, d + 0.8f, bn);
        if (!blocked && w->surface) {
            /* a surface right above the new position means the rider walked into the body of a solid object (its sides are low or thin), not off a lip */
            float a[3] = { s->pos[0], s->pos[1] + 0.1f, s->pos[2] }, b[3] = { s->pos[0], s->pos[1] + 1.6f, s->pos[2] }, h2[3], n2[3];
            if (w->surface(w->ctx, a, b, h2, n2)) blocked = 1;
        }
    }
    if (blocked) {
        /* the wall slide carried the rider off the edge of the surface (a steep ramp meeting a wall): undo it and stay put */
        for (int i = 0; i < 3; i++) { s->pos[i] = old_pos[i]; s->cn[i] = old_cn[i]; s->fwd[i] = old_fwd[i]; }
        s->speed *= 0.5f;
        if (snap_to_surface(s, w))
            return;
    }
    leave_surface(s);
}

/* ---- grinding ---------------------------------------------------------------------------------- */
/* the facing and the contact normal while on a rail: fwd along the rail in the direction of travel, normal = the world up made perpendicular to it */
static void grind_orient(Skate *s, const float dir[3])
{
    float d = s->rail_sign;
    for (int i = 0; i < 3; i++) s->fwd[i] = dir[i] * d;
    float up[3] = { 0.0f, 1.0f, 0.0f };
    float k = dotv(up, s->fwd);
    for (int i = 0; i < 3; i++) s->cn[i] = up[i] - s->fwd[i] * k;
    normv(s->cn);
}

static void leave_rail(Skate *s)
{
    s->grinding = 0;
    s->grind_cool = GRIND_COOLDOWN;
    s->ev_grind_end++;
    leave_surface(s);                                   /* continues with fwd * speed (the rail's direction of travel) as a ballistic body */
}

static void air_tricks_land(Skate *s, int to_rail);
static int spin_landing(Skate *s, int to_rail);

/* the grind type of a SkateInput.grind / grind_switch value (0 = a 50-50) */
static int grind_type_of(int g)
{
    return g > 0 && g - 1 < NGRIND_TYPES ? g - 1 : (g > 0 ? NGRIND_TYPES - 1 : GRIND_5050);
}

/* airborne: does the board's contact point meet a rail or ledge? It locks on by itself (no button); the grind is the one the player holds the nub for (in->grind) */
static int try_grind(Skate *s, const SkateInput *in, const SkateWorld *w)
{
    const Rails *rl = (const Rails *)w->rails;
    uint32_t rail; float t, pt[3], dir[3];
    if (!rl || s->grind_cool > 0.0f || s->bail_t > 0.0f) return 0;
    if (!rails_find(rl, s->pos, GRIND_HORIZ, GRIND_BELOW, GRIND_ABOVE, &rail, &t, pt, dir)) return 0;
    float dh = hyp2(dir[0], dir[2]), vh = hyp2(s->vel[0], s->vel[2]);
    if (dh < 0.05f || vh < 1e-3f) return 0;
    float along_h = (s->vel[0] * dir[0] + s->vel[2] * dir[2]) / dh;       /* horizontal velocity along the rail's horizontal direction */
    if (fabsf(along_h) < GRIND_MIN_SPEED || fabsf(along_h) / vh < GRIND_ALIGN) return 0;
    float sign = along_h >= 0.0f ? 1.0f : -1.0f;
    float sp = (s->vel[0] * dir[0] + s->vel[1] * dir[1] + s->vel[2] * dir[2]) * sign;    /* speed along the rail, vertical part included */
    /* off-centre landings start leaning: the side of the rider's travel the board is on (+ = left) */
    float lat = ((s->pos[0] - pt[0]) * dir[2] - (s->pos[2] - pt[2]) * dir[0]) / dh * sign;
    s->grinding = 1; s->rail = rail; s->rail_t = t; s->rail_sign = sign; s->grind_time = 0.0f;
    s->grind_type = grind_type_of(in->grind);
    /* backside = the edge (the line of the rail, the drop of a ledge) is on the rider's right: a ledge has its top on the left of its direction, so the drop is on the right of the travel when moving with it */
    s->grind_bs = rl->rail[rail].kind == RAIL_KIND_LEDGE ? (sign > 0.0f) : (lat > 0.03f);
    s->lean = clampf(lat / GRIND_HORIZ, -1.0f, 1.0f) * LEAN_ENTRY; s->lean_v = 0.0f;          /* an off-centre landing tips the body a little (a look only: no balancing on rails) */
    s->lean_armed = fabsf(in->steer) < 0.3f;
    spin_landing(s, 1);
    air_tricks_land(s, 1);
    s->ev_grind_start++; s->ev_grind_kind = (int)rl->rail[rail].kind; s->ev_grind_mat = (int)rl->rail[rail].mat;
    s->speed = sp > GRIND_MIN_SPEED ? sp : GRIND_MIN_SPEED;
    s->grounded = 0; s->vel[0] = s->vel[1] = s->vel[2] = 0.0f;
    for (int i = 0; i < 3; i++) s->pos[i] = pt[i];
    grind_orient(s, dir);
    s->absorb = 0.35f;                                  /* a little crouch as the board lands on the rail */
    return 1;
}

int skate_grind_probe(const Skate *s, const SkateInput *in, const SkateWorld *w, GrindProbe *p)
{
    const Rails *rl = (const Rails *)w->rails;
    uint32_t rail; float t, pt[3], dir[3], side = 0.0f;
    (void)in;                                                            /* (the board locks on without a trick input now; the parameter stays for the callers) */
    memset(p, 0, sizeof(*p));
    if (!rl || !rails_find_any_side(rl, s->pos, 2.0f, 3.0f, 3.0f, &rail, &t, pt, dir, &side)) return 0;
    p->found = 1; p->rail = rail; p->kind = (int)rl->rail[rail].kind; p->side = side;
    p->dist = hyp2(s->pos[0] - pt[0], s->pos[2] - pt[2]);
    p->dy = s->pos[1] - pt[1];
    float dh = hyp2(dir[0], dir[2]), vh = hyp2(s->vel[0], s->vel[2]);
    if (s->grounded) { vh = hyp2(s->fwd[0], s->fwd[2]) * fabsf(s->speed); p->speed_along = dh > 0.05f ? fabsf((s->fwd[0] * dir[0] + s->fwd[2] * dir[2]) / dh) * fabsf(s->speed) : 0.0f; }
    else p->speed_along = dh > 0.05f ? fabsf((s->vel[0] * dir[0] + s->vel[2] * dir[2]) / dh) : 0.0f;
    p->align = vh > 1e-3f ? p->speed_along / vh : 0.0f;
    if (s->grounded) p->reason = GP_GROUND;
    else if (s->bail_t > 0.0f) p->reason = GP_BAIL;
    else if (s->grind_cool > 0.0f) p->reason = GP_COOLDOWN;
    else if (p->dist > GRIND_HORIZ) p->reason = GP_FAR;
    else if (p->dy < -GRIND_BELOW) p->reason = GP_LOW;
    else if (p->dy > GRIND_ABOVE) p->reason = GP_HIGH;
    else if (p->kind == RAIL_KIND_LEDGE && side < -0.05f) p->reason = GP_WRONG_SIDE;
    else if (p->speed_along < GRIND_MIN_SPEED || p->align < GRIND_ALIGN) p->reason = vh < GRIND_MIN_SPEED ? GP_SLOW : GP_ACROSS;      /* slow overall, or fast but aimed across the line */
    else p->reason = GP_OK;
    return 1;
}

/* the deterministic part of the balance disturbance: two incommensurate sines with a phase of their own */
static float wobble(float t, float ph)
{
    return 0.9f * sinf(2.1f * t + ph) + 0.5f * sinf(4.9f * t + 2.0f * ph);
}

/* one step of the balance model shared by grinds and manuals: `input` is the stick pushing the lean (+) or against it (-), t the seconds since it began */
static void balance_step(Skate *s, float diff, float input, float t, float phase, float dt)
{
    if (fabsf(input) < 0.3f) s->lean_armed = 1;
    float ramp = clampf(t * (1.0f / LEAN_GRACE), 0.0f, 1.0f);
    float acc = LEAN_TIP * diff * s->lean + LEAN_WOBBLE * diff * ramp * wobble(t, phase) + (s->lean_armed ? LEAN_INPUT * input : 0.0f) - LEAN_DAMP * s->lean_v;
    s->lean_v += acc * dt;
    s->lean += s->lean_v * dt;
}

static void grind_move(Skate *s, const SkateInput *in, const SkateWorld *w, float dt)
{
    const Rails *rl = (const Rails *)w->rails;
    float p[3], dir[3];
    rails_point(rl, s->rail, s->rail_t, p, dir);
    if (in->grind_switch > 0 && grind_type_of(in->grind_switch) != s->grind_type) {       /* a flick of the nub: another grind on the same rail (the original's "swipe again"); the stick must come back to the centre before it balances again */
        s->grind_type = grind_type_of(in->grind_switch);
        s->ev_grind_switch++;
        s->lean_armed = 0;
        s->absorb = 0.25f;
    }
    /* no balancing on a rail (the user's call after the play-testers: the board snaps on by itself and the nub changes the grind, so the stick is not a balance input any more).
     * lean just settles to 0 for the renderer; the balance model stays for manuals. */
    s->lean += (0.0f - s->lean) * (1.0f - decay(8.0f * dt)); s->lean_v = 0.0f;
    /* friction, and gravity along the rail (sliding downhill speeds up, uphill slows) */
    s->speed -= GRIND_FRICTION * dt;
    s->speed -= GRAV * dir[1] * s->rail_sign * dt;
    if (s->speed > SKATE_MAX_SPEED * 1.5f) s->speed = SKATE_MAX_SPEED * 1.5f;
    if (s->speed < GRIND_MIN_EXIT) { grind_orient(s, dir); leave_rail(s); return; }
    /* a ledge that runs into a wall (the front edge of a stair step ends at the curb beside the stairs): the board stops there - leaving the ledge at its end point would put the rider
     * into the wall's plane (inside the block, where nothing holds him up). Handrails are not probed: their own tubes are "walls" to the collision. */
    if (rl->rail[s->rail].kind == RAIL_KIND_LEDGE && w->wall) {
        float step = s->speed * dt, hit[3], n[3];
        for (int k = 0; k < 2; k++) {
            float a[3] = { s->pos[0], s->pos[1] + (k ? 0.9f : 0.3f), s->pos[2] };
            float b[3] = { a[0] + dir[0] * s->rail_sign * (step + WALL_MARGIN), a[1] + dir[1] * s->rail_sign * (step + WALL_MARGIN), a[2] + dir[2] * s->rail_sign * (step + WALL_MARGIN) };
            if (w->wall(w->ctx, a, b, hit, n)) {
                s->speed *= 0.3f;                                          /* it hits the wall: slides off the end of the ledge with what is left */
                grind_orient(s, dir);
                leave_rail(s);
                return;
            }
        }
    }
    s->rail_t += s->rail_sign * s->speed * dt;
    s->grind_time += dt;
    float len = rl->rail[s->rail].len;
    if (s->rail_t <= 0.0f || s->rail_t >= len) {                          /* off the end of the rail */
        s->rail_t = s->rail_t <= 0.0f ? 0.0f : len;
        rails_point(rl, s->rail, s->rail_t, p, dir);
        for (int i = 0; i < 3; i++) s->pos[i] = p[i];
        grind_orient(s, dir);
        leave_rail(s);
        return;
    }
    rails_point(rl, s->rail, s->rail_t, p, dir);
    for (int i = 0; i < 3; i++) s->pos[i] = p[i];
    grind_orient(s, dir);
}

/* ---- manuals ----------------------------------------------------------------------------------- */
static void manual_end(Skate *s)
{
    s->manual = 0;
    s->ev_manual_end++;
}

/* the rider slips off the back (or the front) wheels: thrown into the air a little, no control for BAIL_TIME */
static void manual_bail(Skate *s)
{
    s->manual = 0;
    s->ev_manual_end++; s->ev_bail++;
    leave_surface(s);
    for (int i = 0; i < 3; i++) s->vel[i] *= 0.6f;
    s->vel[1] += 1.5f;
    s->bail_t = BAIL_TIME; s->bail_dir = 1.0f;
    s->grind_cool = BAIL_TIME;
    s->charge = 0.0f; s->push_t = -1.0f; s->push_boost = 0.0f;
}

/* Called while rolling on a surface, before the move: starts, balances and ends a manual. Returns 1 if the rider is still on the surface and should roll on. */
static int manual_prepare(Skate *s, const SkateInput *in, float dt)
{
    if (!s->manual) {
        if (in->manual > 0 && !s->grinding && s->bail_t <= 0.0f && !in->ollie_held && fabsf(s->speed) >= MANUAL_MIN_SPEED && s->cn[1] > 0.9f) {
            s->manual = in->manual == 2 ? 2 : 1;
            s->manual_time = 0.0f;
            s->lean = MANUAL_ENTRY_LEAN; s->lean_v = MANUAL_ENTRY_PUSH;
            s->lean_armed = fabsf(in->tilt) < 0.3f;
            s->ev_manual_start++;
            s->push_t = -1.0f; s->push_boost = 0.0f;
        }
        return 1;
    }
    if (in->manual > 0 || fabsf(s->speed) < MANUAL_END_SPEED || s->cn[1] < 0.8f) { manual_end(s); return 1; }
    s->manual_time += dt;
    float sgn = s->manual == 1 ? 1.0f : -1.0f;
    const GrindType *g = &tricks.manual[s->manual - 1];
    balance_step(s, g->diff > 0.05f ? g->diff : 1.0f, -sgn * clampf(in->tilt, -1.0f, 1.0f), s->manual_time, 2.1f + 0.9f * (float)s->manual, dt);
    if (s->lean >= 1.0f) { s->lean = 1.0f; manual_bail(s); return 0; }
    if (s->lean <= -1.0f) { s->lean = -1.0f; manual_end(s); }          /* the board comes down: a clean end */
    return 1;
}

/* ---- air tricks -------------------------------------------------------------------------------- */
/* a grab ends, a flip is over (completed = counts) - when the rider lands, locks on to a rail, falls, or lets go */
static void end_grab(Skate *s)
{
    if (!s->grab) return;
    s->grab = 0;
    s->ev_grab_end++;
}

static void end_flip(Skate *s, int completed)
{
    if (!s->flip) return;
    if (completed) { s->ev_flip_done++; s->ev_flip_type = s->flip - 1; s->ev_flip_nollie = s->flip_nollie; }
    s->flip = 0; s->flip_nollie = 0; s->flip_roll = 0.0f;
}

static void start_flip(Skate *s, int t, int nollie)
{
    if (t < 0) t = 0;
    if (t >= NFLIP_TYPES) t = NFLIP_TYPES - 1;
    s->flip = t + 1; s->flip_t = 0.0f; s->flip_roll = 0.0f;
    s->flip_nollie = nollie && tricks.flip[t].pose >= 0;                    /* a body flip has no nollie version */
    float ms = (float)tricks.flip[t].rate;
    s->flip_dur = ms > 100.0f ? ms * 0.001f : 0.5f;
}

/* in the air: start / run / end the tricks */
static void air_tricks(Skate *s, const SkateInput *in, float dt)
{
    if (s->bail_t > 0.0f) { end_grab(s); end_flip(s, 0); return; }
    if (s->flip) {
        s->flip_t += dt;
        if (s->flip_t >= s->flip_dur) end_flip(s, 1);
        else if (tricks.flip[s->flip - 1].pose < 0)                          /* a body flip: the whole rider rolls about the board's long axis (frontflip = head towards the chest, to the left) */
            s->flip_roll = (s->flip - 1 == FLIP_BACKFLIP ? -1.0f : 1.0f) * 2.0f * PI_F * (s->flip_t / s->flip_dur);
    } else if (in->flip > 0 && !s->grab && s->air_t > 0.03f) {
        start_flip(s, in->flip - 1, in->nollie);
    }
    if (s->grab) {
        s->grab_time += dt;
        if (in->grab == 0) end_grab(s);
        else if (in->grab != s->grab && in->grab - 1 < NGRAB_TYPES) {                 /* a flick while it is held: the variant of the grab (nosebone, shifty, tweak ...) */
            s->grab = in->grab; s->ev_grab_switch++; s->ev_grab_type = in->grab - 1; s->grab_time = 0.0f;
        }
    } else if (in->grab > 0 && !s->flip && s->air_t > 0.03f) {
        int t = in->grab - 1;
        if (t >= NGRAB_TYPES) t = NGRAB_TYPES - 1;
        s->grab = t + 1; s->grab_time = 0.0f;
        s->ev_grab_start++; s->ev_grab_type = t;
    }
}

/* touching down or locking on to a rail: a grab is let go; a flip that has gone far enough counts, one that has not is a sketchy landing */
/* landed in the middle of a flip, or turned sideways after a spin: down you go */
static void sketchy_landing(Skate *s)
{
    s->bail_t = BAIL_TIME; s->bail_dir = 1.0f;
    s->ev_bail++;
    s->speed *= 0.4f; s->absorb = 0.0f;
    s->grind_cool = BAIL_TIME;
    s->charge = 0.0f; s->push_t = -1.0f; s->push_boost = 0.0f;
}

static void air_tricks_land(Skate *s, int to_rail)
{
    end_grab(s);
    if (!s->flip) return;
    float p = s->flip_t / s->flip_dur;
    if (p >= FLIP_LAND_OK) { end_flip(s, 1); return; }
    end_flip(s, 0);
    if (!to_rail) sketchy_landing(s);
}

/* touching down (or locking on to a rail) after a spin: the turn is rounded to the half turn it is closest to and the board lands along (or against) the travel - fakie after an odd
 * number of half turns. Returns 1 when the board is sideways to the travel (more than SPIN_LAND_TOL away from a half turn): a bad landing unless it is a rail. */
static int spin_landing(Skate *s, int to_rail)
{
    if (s->spin_ang == 0.0f) return 0;
    float k = roundf(s->spin_ang / PI_F), err = fabsf(s->spin_ang - k * PI_F), corr = k * PI_F - s->spin_ang;
    float dir = s->spin_dir >= 0.0f ? 1.0f : -1.0f;
    s->spin_ang = 0.0f; s->spin_dir = 0.0f;
    if (err > SPIN_LAND_TOL && !to_rail) return 1;
    if (!to_rail) {                                      /* line the facing up with the half turn (a rail sets the facing itself) */
        float c, sn;
        rot_cs(corr, &c, &sn);
        float fx = s->fwd[0] * c + s->fwd[2] * sn, fz = -s->fwd[0] * sn + s->fwd[2] * c;
        s->fwd[0] = fx; s->fwd[2] = fz;
    }
    if (k != 0.0f) { s->ev_spin++; s->ev_spin_deg = (int)(fabsf(k) * 180.0f + 0.5f); s->ev_spin_dir = dir > 0.0f ? 1 : -1; s->ev_spin_fakie = s->fakie_takeoff; }
    return 0;
}

/* ---- flying ------------------------------------------------------------------------------------ */
static void land(Skate *s, const float hit[3], const float n[3])
{
    int sideways = spin_landing(s, 0);
    float fp[3] = { s->fwd[0], s->fwd[1], s->fwd[2] };
    project_fwd(fp, n);
    float d = dotv(s->vel, n);
    s->ev_land++; s->land_impact = d < 0.0f ? -d : 0.0f;
    float vt[3] = { s->vel[0] - n[0] * d, s->vel[1] - n[1] * d, s->vel[2] - n[2] * d };
    s->speed = dotv(vt, fp);                             /* the board grips: sideways velocity is lost */
    for (int i = 0; i < 3; i++) { s->pos[i] = hit[i]; s->cn[i] = n[i]; s->fwd[i] = fp[i]; s->vel[i] = 0.0f; }
    s->grounded = 1; s->landed = 1;
    s->absorb = s->air_t > 0.25f ? 0.8f : 0.4f;          /* absorb the landing */
    if (s->bail_t > 0.0f) { s->speed *= 0.35f; s->absorb = 0.0f; }     /* a fallen rider does not roll away */
    air_tricks_land(s, 0);
    if (sideways && s->bail_t <= 0.0f) sketchy_landing(s);
}

/* in the air: the spin input turns the rider (and the board with him) about the vertical; let go and he finishes the half turn he is in. fwd turns, the velocity does not: after a half turn
 * the board lands pointing backwards - fakie. */
static void air_spin(Skate *s, const SkateInput *in, float dt)
{
    float step = 0.0f;
    if (in->spin != 0 && s->bail_t <= 0.0f) {
        s->spin_dir = in->spin > 0 ? 1.0f : -1.0f;
        step = s->spin_dir * SPIN_RATE * dt;
    } else if (s->spin_ang != 0.0f) {
        float dir = s->spin_dir >= 0.0f ? 1.0f : -1.0f;
        float target = dir > 0.0f ? ceilf(s->spin_ang / PI_F - 0.02f) * PI_F : floorf(s->spin_ang / PI_F + 0.02f) * PI_F;
        float rem = target - s->spin_ang, mx = SPIN_RATE * dt;
        step = rem > mx ? mx : (rem < -mx ? -mx : rem);
    }
    if (step == 0.0f) return;
    float c, sn;
    rot_cs(step, &c, &sn);
    float fx = s->fwd[0] * c + s->fwd[2] * sn, fz = -s->fwd[0] * sn + s->fwd[2] * c;
    s->fwd[0] = fx; s->fwd[2] = fz;
    s->spin_ang += step;
}

/* ---- wallride ---- */
static void wall_end(Skate *s, int jump)
{
    s->wallriding = 0; s->ev_wall_end++;
    for (int i = 0; i < 3; i += 2) s->vel[i] = s->wall_dir[i] * s->speed + s->wall_n[i] * (jump ? 3.0f : 0.8f);
    if (jump) s->vel[1] += 2.4f;
    s->grind_cool = 0.35f;                               /* no lock-on straight away (the same cool-down as a rail) */
    float h = hyp2(s->wall_dir[0], s->wall_dir[2]);
    s->fwd[0] = s->wall_dir[0] / (h > 1e-3f ? h : 1.0f); s->fwd[1] = 0.0f; s->fwd[2] = s->wall_dir[2] / (h > 1e-3f ? h : 1.0f);
    s->cn[0] = 0.0f; s->cn[1] = 1.0f; s->cn[2] = 0.0f;
    s->air_t = 0.1f;
}

/* The board has met a wall in the air at an angle, at speed: it rides along the wall by itself (no button), feet on it, nearly level, for up to WALL_MAX_TIME. It ends when the speed
 * is gone, the time is up, the wall ends or something is in the way, the ground is reached - or with a jump (the ollie button let go) that kicks off the wall. */
static int wall_try(Skate *s, const SkateInput *in, const SkateWorld *w, const float bn[3], const float hit[3])
{
    if (s->grind_cool > 0.0f || s->bail_t > 0.0f || s->wallriding || !w->wall) return 0;
    float nh = hyp2(bn[0], bn[2]);
    if (nh < 0.99f || fabsf(bn[1]) > 0.1f) return 0;     /* a vertical wall, not a slope, a ceiling or one of the leaning slivers of the bowl's banks */
    {                                                     /* ... and a tall one: it must still be there 1.2 m above the board (a curb is not a wall to ride) */
        float a[3] = { s->pos[0], s->pos[1] + 1.2f, s->pos[2] }, b[3] = { a[0] - bn[0] * 1.0f, a[1], a[2] - bn[2] * 1.0f }, h2[3], n2[3];
        if (!w->wall(w->ctx, a, b, h2, n2)) return 0;
    }
    float nx = bn[0] / nh, nz = bn[2] / nh;
    float vh = hyp2(s->vel[0], s->vel[2]);
    if (vh < WALL_MIN_SPEED) return 0;
    if (w->ground) {                                      /* there must be a floor to come down on beside the wall, here and a little way along it (not the outside of the level's edge) */
        float gy;
        if (!w->ground(w->ctx, s->pos[0], s->pos[2], s->pos[1] + 0.3f, &gy) || s->pos[1] - gy > 3.0f) return 0;
        float ahead = 2.0f / vh;
        if (!w->ground(w->ctx, s->pos[0] + s->vel[0] * ahead, s->pos[2] + s->vel[2] * ahead, s->pos[1] + 0.3f, &gy) || s->pos[1] - gy > 3.0f) return 0;
    }
    float vn = s->vel[0] * nx + s->vel[2] * nz;           /* < 0: into the wall */
    float ax = s->vel[0] - nx * vn, az = s->vel[2] - nz * vn, al = hyp2(ax, az);
    if (al < WALL_MIN_ALONG || -vn > 0.8f * vh) return 0;     /* head-on is a bump, not a wallride */
    s->wallriding = 1; s->wall_t = 0.0f; s->wall_held = in->ollie_held;
    s->wall_n[0] = nx; s->wall_n[1] = 0.0f; s->wall_n[2] = nz;
    s->wall_dir[0] = ax / al; s->wall_dir[1] = 0.0f; s->wall_dir[2] = az / al;
    s->speed = al * 0.95f;
    float left0 = s->wall_dir[2], left2 = -s->wall_dir[0];             /* the rider's left, for the direction of travel */
    s->wall_side = (-nx * left0 - nz * left2) >= 0.0f ? 1.0f : -1.0f;    /* the wall is at -n: on the left (+1) or the right */
    s->pos[0] = hit[0] + nx * WALL_GAP; s->pos[2] = hit[2] + nz * WALL_GAP;
    if (s->vel[1] > 3.0f) s->vel[1] = 3.0f;
    s->cn[0] = nx; s->cn[1] = 0.0f; s->cn[2] = nz;
    s->fwd[0] = s->wall_dir[0]; s->fwd[1] = 0.0f; s->fwd[2] = s->wall_dir[2];
    spin_landing(s, 1);
    air_tricks_land(s, 1);
    s->ev_wall_start++;
    return 1;
}

static void wall_move(Skate *s, const SkateInput *in, const SkateWorld *w, float dt)
{
    s->wall_t += dt;
    int jumped = s->wall_held && !in->ollie_held;
    s->wall_held = in->ollie_held;
    if (jumped) { wall_end(s, 1); return; }
    s->speed -= 1.6f * dt;
    s->vel[1] += -GRAV * 0.12f * dt;
    s->vel[1] -= s->vel[1] * 3.0f * dt;
    if (s->speed < 2.0f || s->wall_t > WALL_MAX_TIME) { wall_end(s, 0); return; }
    float step = s->speed * dt, np[3] = { s->pos[0] + s->wall_dir[0] * step, s->pos[1] + s->vel[1] * dt, s->pos[2] + s->wall_dir[2] * step };
    float hit[3], n[3];
    if (w->wall) {
        float a[3] = { s->pos[0], s->pos[1] + 0.4f, s->pos[2] }, b[3] = { a[0] + s->wall_dir[0] * (step + 0.3f), a[1], a[2] + s->wall_dir[2] * (step + 0.3f) };
        if (w->wall(w->ctx, a, b, hit, n)) { wall_end(s, 0); return; }                          /* a corner / something in the way */
        float a2[3] = { np[0] + s->wall_n[0] * 0.25f, np[1] + 0.4f, np[2] + s->wall_n[2] * 0.25f }, b2[3] = { np[0] - s->wall_n[0] * 0.4f, np[1] + 0.4f, np[2] - s->wall_n[2] * 0.4f };
        if (!w->wall(w->ctx, a2, b2, hit, n)) { wall_end(s, 0); return; }                       /* the wall has ended */
        np[0] = hit[0] + s->wall_n[0] * WALL_GAP; np[2] = hit[2] + s->wall_n[2] * WALL_GAP;
    }
    float gy;
    if (w->ground && w->ground(w->ctx, np[0], np[2], np[1] + 0.2f, &gy) && np[1] <= gy + 0.05f) { wall_end(s, 0); return; }     /* down to the ground */
    for (int i = 0; i < 3; i++) s->pos[i] = np[i];
}

static void air_move(Skate *s, const SkateInput *in, const SkateWorld *w, float dt)
{
    air_tricks(s, in, dt);
    air_spin(s, in, dt);
    if (try_grind(s, in, w))
        return;
    /* limited air control: turn the whole horizontal velocity and the facing */
    float th = s->steer * 1.0f * dt, c, sn;
    rot_cs(th, &c, &sn);
    float vx = s->vel[0] * c + s->vel[2] * sn, vz = -s->vel[0] * sn + s->vel[2] * c;
    s->vel[0] = vx; s->vel[2] = vz;
    float fx = s->fwd[0] * c + s->fwd[2] * sn, fz = -s->fwd[0] * sn + s->fwd[2] * c;
    s->fwd[0] = fx; s->fwd[2] = fz;
    s->vel[1] -= GRAV * dt;

    float v[3] = { s->vel[0] * dt, s->vel[1] * dt, s->vel[2] * dt };
    float hit[3], n[3];
    /* walls: probes at 0.12 m, 0.45 m and 0.9 m above the feet along the motion (a wall whose top is a little above the feet - a 0.77 m curb the board is 0.3 m below the top of - must not
     * slip between two probes: the rider would end up inside it, where nothing holds him up). The first wall hit takes away the velocity component into it. */
    float hlen = hyp2(v[0], v[2]);
    float mx = hlen > 1e-6f ? v[0] / hlen * WALL_MARGIN : 0.0f, mz = hlen > 1e-6f ? v[2] / hlen * WALL_MARGIN : 0.0f;
    float best = 1e30f, bn[3] = { 0, 0, 0 }, hit_best[3] = { 0, 0, 0 };
    int blocked = 0;
    for (int k = 0; k < 3; k++) {
        float off = k == 0 ? 0.12f : (k == 1 ? 0.45f : 0.9f);                  /* (0.12: a little under AIR_LIFT, which steps up onto a surface that low instead: no gap between the two) */
        float a[3] = { s->pos[0], s->pos[1] + off, s->pos[2] };
        float b[3] = { s->pos[0] + v[0] + mx, s->pos[1] + off + v[1], s->pos[2] + v[2] + mz };
        if (w->wall && w->wall(w->ctx, a, b, hit, n)) {
            float t = (hit[0] - a[0]) * (hit[0] - a[0]) + (hit[1] - a[1]) * (hit[1] - a[1]) + (hit[2] - a[2]) * (hit[2] - a[2]);
            if (t < best) { best = t; bn[0] = n[0]; bn[1] = n[1]; bn[2] = n[2]; blocked = 1; hit_best[0] = hit[0]; hit_best[1] = hit[1]; hit_best[2] = hit[2]; }
        }
    }
    if (blocked && best < 0.06f * 0.06f && dotv(bn, s->cn) < -0.5f) {
        /* the wall is right on the rider and faces AWAY from the surface he has just left: he stands a few millimetres inside it (the crease between a slope and a steeper wall - the crack
         * tolerance of the surface query lets the board settle past it). The open side is the one the surface is on: step out to it instead of being held by the wall. */
        float d = sqrtf(best) + 0.01f;
        for (int i = 0; i < 3; i++) s->pos[i] -= bn[i] * d;
        blocked = 0;
    }
    if (blocked && wall_try(s, in, w, bn, hit_best)) return;
    if (blocked) {
        float into = dotv(s->vel, bn);
        if (into < 0.0f) {
            float vl = lenv(s->vel);
            float head_on = vl > 1e-6f ? fabsf(into) / vl : 0.0f;
            for (int i = 0; i < 3; i++) s->vel[i] -= bn[i] * into;
            s->vel[0] *= 1.0f - 0.85f * head_on; s->vel[2] *= 1.0f - 0.85f * head_on;
            for (int i = 0; i < 3; i++) v[i] = s->vel[i] * dt;
        }
    }
    /* land on any rideable surface crossed between a bit above the feet and where they will be */
    float a[3] = { s->pos[0], s->pos[1] + AIR_LIFT, s->pos[2] };
    float b[3] = { s->pos[0] + v[0], s->pos[1] + v[1], s->pos[2] + v[2] };
    if (w->surface && w->surface(w->ctx, a, b, hit, n) && n[1] > 0.0f && dotv(s->vel, n) < 0.0f) {
        land(s, hit, n);
        return;
    }
    float gy;
    float ny = s->pos[1] + v[1];
    if (s->vel[1] <= 0.0f && w->ground && w->ground(w->ctx, s->pos[0] + v[0], s->pos[2] + v[2], s->pos[1] + AIR_LIFT, &gy) && ny <= gy) {
        float up[3] = { 0.0f, 1.0f, 0.0f }, h2[3] = { s->pos[0] + v[0], gy, s->pos[2] + v[2] };
        land(s, h2, up);                                 /* a crack between polygons: the height query catches the landing */
        return;
    }
    for (int i = 0; i < 3; i++) s->pos[i] += v[i];
    s->air_t += dt;
}

void skate_update(Skate *s, const SkateInput *in_, const SkateWorld *w, float dt)
{
    SkateInput fallen;                                                     /* a rider who has fallen has no control (and the board drags to a stop) */
    memset(&fallen, 0, sizeof(fallen));
    fallen.brake = 1;
    const SkateInput *in = in_;
    s->landed = 0;
    if (s->bail_t > 0.0f) { s->bail_t -= dt; if (s->bail_t < 0.0f) s->bail_t = 0.0f; in = &fallen; }
    if (s->grind_cool > 0.0f) s->grind_cool -= dt;
    float target_steer = clampf(in->steer, -1.0f, 1.0f);
    s->steer += (target_steer - s->steer) * (1.0f - decay(10.0f * dt));

    /* ---- push timing (one push at a time): a tap is the light, quick push; the button still held when the foot plants makes it the strong, slower one ---- */
    float push_max = SKATE_MAX_SPEED * skate_tune.push;                   /* pushing helps up to this speed */
    if (in->flip_pre > 0 && s->push_t >= 0.0f) { s->push_t = -1.0f; s->push_boost = 0.0f; }      /* holding a flip trick ready: that was not a push (no speed has been given yet) */
    if (in->push && s->grounded && !s->manual && in->flip_pre <= 0 && s->push_t < 0.0f && s->speed < push_max) {
        s->push_t = 0.0f; s->push_dur = SKATE_PUSH_TIME; s->push_strong = 0;
        s->push_boost = PUSH_GAIN * skate_tune.push * (1.0f - clampf(s->speed / push_max, 0.0f, 0.9f));
    }
    if (s->push_t >= 0.0f) {
        float prev = s->push_t;
        s->push_t += dt;
        float a = s->push_dur * PUSH_PLANT, b = s->push_dur * PUSH_LIFT;
        if (!s->push_strong && prev < a && s->push_t >= a && in->push_held) {        /* the foot plants with the button still down: the strong push (the boost grows, the animation slows) */
            s->push_strong = 1;
            s->push_boost *= PUSH_GAIN_HOLD / PUSH_GAIN;
            s->push_t = s->push_t / s->push_dur * SKATE_PUSH_TIME_HOLD;            /* the same point of the animation, on the slower clock */
            s->push_dur = SKATE_PUSH_TIME_HOLD;
            prev = prev / SKATE_PUSH_TIME * SKATE_PUSH_TIME_HOLD;
            a = s->push_dur * PUSH_PLANT; b = s->push_dur * PUSH_LIFT;
        }
        /* the push is delivered between PUSH_PLANT and PUSH_LIFT of the animation (foot on the ground) */
        float f0 = clampf((prev - a) / (b - a), 0.0f, 1.0f), f1 = clampf((s->push_t - a) / (b - a), 0.0f, 1.0f);
        float give = (f1 - f0) * s->push_boost;
        if (f1 >= 1.0f && f0 >= 1.0f) give = 0.0f;
        s->speed += give;
        if (s->push_t >= s->push_dur) { s->push_t = -1.0f; s->push_boost = 0.0f; s->push_strong = 0; }
    }

    if (s->grounded || s->grinding) {
        /* ---- ollie charge ---- */
        if (in->ollie_held) {
            s->charge += dt / 0.28f;
            if (in->flip_pre > 0 && s->charge < 0.4f) s->charge = 0.4f;       /* a flip trick held ready always has enough charge to pop */
            if (s->charge > 1.0f) s->charge = 1.0f;
        } else if (s->charge > 0.0f) {
            if (s->charge > 0.25f) {                       /* released: pop off the surface */
                float pop = (2.7f + 1.1f * s->charge) * skate_tune.ollie;
                float side[3] = { 0.0f, 0.0f, 0.0f };
                if (s->grinding) {
                    /* on a rail: a tap hops straight up (the board comes back down on the rail), a long press jumps OFF it - sideways towards the nub, else away from a ledge's drop
                     * (its top is on the left of its line) / to the left of a rail - with a longer no-relock time so the rail does not catch him again */
                    int off = s->pop_hold_t >= GRIND_HOP_HOLD;
                    const Rails *rl = (const Rails *)w->rails;
                    s->grinding = 0; s->grind_cool = off ? GRIND_HOP_COOL : GRIND_COOLDOWN; s->ev_grind_end++; s->cn[0] = 0.0f; s->cn[1] = 1.0f; s->cn[2] = 0.0f;
                    if (off) {
                        float dir_side = in->steer > 0.3f ? 1.0f : (in->steer < -0.3f ? -1.0f : (rl && rl->rail[s->rail].kind == RAIL_KIND_LEDGE ? s->rail_sign : 1.0f));
                        float h = hyp2(s->fwd[0], s->fwd[2]);
                        if (h > 1e-3f) { side[0] = s->fwd[2] / h * dir_side * GRIND_HOP_SIDE; side[2] = -s->fwd[0] / h * dir_side * GRIND_HOP_SIDE; }     /* left = (fwd.z, -fwd.x) */
                    }
                }
                for (int i = 0; i < 3; i++) s->vel[i] = s->fwd[i] * s->speed + s->cn[i] * pop + side[i];
                float h = hyp2(s->fwd[0], s->fwd[2]);
                if (h > 1e-3f) { s->fwd[0] /= h; s->fwd[2] /= h; s->fwd[1] = 0.0f; }
                else { s->fwd[0] = sinf(s->heading); s->fwd[1] = 0.0f; s->fwd[2] = cosf(s->heading); }
                s->grounded = 0; s->air_t = 0.0f;
                s->push_t = -1.0f; s->push_boost = 0.0f;
                s->ev_ollie++; s->ollie_pop = pop;
                if (in->flip_pre <= 0) { s->ev_ollie_plain++; s->ev_ollie_nollie = in->nollie != 0; }
                s->fakie_takeoff = s->speed < 0.0f; s->spin_ang = 0.0f; s->spin_dir = 0.0f;
                if (in->flip_pre > 0) start_flip(s, in->flip_pre - 1, in->nollie);        /* the pop and the flip are one motion (also out of a manual: a flip trick out of a manual) */
                if (s->manual) manual_end(s);                  /* popping out of a manual */
            }
            s->charge = 0.0f;
        }
    } else {
        s->charge = 0.0f;
    }
    s->pop_hold_t = in->ollie_held ? s->pop_hold_t + dt : 0.0f;       /* (after the pop above, which reads how long the button was held) */

    /* ground moves: a revert is a half turn (BS to the left, FS to the right), after which the rider rolls fakie with the travel unchanged: fwd and speed flip and the rider's drawn turn
     * (vis_yaw) unwinds; a powerslide has the board sideways while it scrubs speed */
    if (s->grounded && !s->grinding && s->bail_t <= 0.0f) {
        if (in->revert && !s->manual && !s->sliding && fabsf(s->speed) >= REVERT_MIN_SPEED) {
            for (int i = 0; i < 3; i++) s->fwd[i] = -s->fwd[i];
            s->speed = -s->speed;
            s->vis_yaw = -(in->revert > 0 ? 1.0f : -1.0f) * PI_F;
            s->ev_revert = in->revert > 0 ? 1 : -1;
        }
        if (in->slide != 0 && !s->sliding && !s->manual && fabsf(s->speed) >= SLIDE_MIN_SPEED) {
            s->sliding = 1; s->slide_dir = in->slide > 0 ? 1.0f : -1.0f; s->slide_t = 0.0f; s->ev_slide_start++;
        }
    }
    if (s->sliding) {
        if (!s->grounded || s->grinding || s->bail_t > 0.0f || s->manual || in->slide == 0 || fabsf(s->speed) < SLIDE_END_SPEED) {
            s->sliding = 0; s->ev_slide_end++; s->vis_target = 0.0f;
        } else {
            s->slide_t += dt;
            s->vis_target = s->slide_dir * PI_F * 0.5f;
            float d = SLIDE_DECEL * dt;
            if (s->speed > 0.0f) { s->speed -= d; if (s->speed < 0.0f) s->speed = 0.0f; } else { s->speed += d; if (s->speed > 0.0f) s->speed = 0.0f; }
        }
    }
    s->vis_yaw += (s->vis_target - s->vis_yaw) * (1.0f - decay(12.0f * dt));

    if (s->wallriding) {
        wall_move(s, in, w, dt);
    } else if (s->grinding) {
        grind_move(s, in, w, dt);
    } else if (s->grounded && manual_prepare(s, in, dt)) {
        /* ---- steering: turn the facing about the surface normal (left = n x fwd) ---- */
        float spd = fabsf(s->speed);
        float turn = 2.5f / (1.0f + 0.14f * spd);
        if (spd < 0.6f) turn *= 0.55f + 0.45f * (spd / 0.6f);     /* standing kick-turns are slow but possible */
        if (s->manual) turn *= MANUAL_TURN;
        float th = s->steer * turn * dt, c, sn;
        rot_cs(th, &c, &sn);
        float left[3];
        crossv(s->cn, s->fwd, left);
        for (int i = 0; i < 3; i++) s->fwd[i] = s->fwd[i] * c + left[i] * sn;
        project_fwd(s->fwd, s->cn);

        /* friction, braking, and gravity along the surface (a facing that points downhill speeds up) */
        float dec = ROLL_DRAG * dt;
        if (s->manual) dec += MANUAL_DRAG * dt;
        if (in->brake) dec += BRAKE_DECEL * dt;
        if (s->speed > 0.0f) { s->speed -= dec; if (s->speed < 0.0f) s->speed = 0.0f; }
        else if (s->speed < 0.0f) { s->speed += dec; if (s->speed > 0.0f) s->speed = 0.0f; }
        s->speed -= GRAV * s->fwd[1] * dt;
        if (s->speed > SKATE_MAX_SPEED * 1.5f) s->speed = SKATE_MAX_SPEED * 1.5f;
        if (s->speed < -SKATE_MAX_SPEED * 1.5f) s->speed = -SKATE_MAX_SPEED * 1.5f;

        ground_move(s, w, dt);
    } else {
        air_move(s, in, w, dt);
    }

    if (s->manual && !s->grounded) manual_end(s);          /* rolled off an edge */

    /* landing absorb decays on its own; the pose logic reads the larger of the two */
    if ((s->grounded || s->grinding) && s->absorb > 0.0f && !s->landed) {
        s->absorb -= dt / 0.30f;
        if (s->absorb < 0.0f) s->absorb = 0.0f;
    }
    s->crouch = s->charge > s->absorb ? s->charge : s->absorb;
    s->vy = (s->grounded || s->grinding) ? 0.0f : s->vel[1];
    sync_heading(s);

    /* smoothed surface normal for the body orientation */
    float tgt[3] = { 0.0f, 1.0f, 0.0f };
    if (s->grounded || s->grinding || s->wallriding) { tgt[0] = s->cn[0]; tgt[1] = s->cn[1]; tgt[2] = s->cn[2]; }
    float k = 1.0f - decay(12.0f * dt);
    for (int i = 0; i < 3; i++) s->up[i] += (tgt[i] - s->up[i]) * k;
    normv(s->up);
    /* the body tilts into the lean while balancing on a rail */
    float rt = s->grinding ? s->lean * 0.45f : 0.0f;
    s->roll += (rt - s->roll) * k;
    /* and the board tilts about the wheels it stands on */
    float pt = s->manual && tricks.manual[s->manual - 1].pose < 0 ? (s->manual == 1 ? 1.0f : -1.0f) * (MANUAL_PITCH + MANUAL_PITCH_VAR * clampf(s->lean, -1.0f, 1.0f)) : 0.0f;
    s->pitch += (pt - s->pitch) * k;
}

void skate_mirror_matrix(float m[16])
{
    m[0] = -m[0]; m[1] = -m[1]; m[2] = -m[2];
}

void skate_body_matrix(const Skate *s, float m[16])
{
    float u[3] = { s->up[0], s->up[1], s->up[2] };
    float f0[3] = { s->fwd[0], s->fwd[1], s->fwd[2] };
    float d = dotv(f0, u);
    float f[3] = { f0[0] - u[0] * d, f0[1] - u[1] * d, f0[2] - u[2] * d };
    if (lenv(f) < 1e-3f) {                                  /* facing along the normal (cannot happen on a surface): fall back to the heading */
        float h[3] = { sinf(s->heading), 0.0f, cosf(s->heading) };
        d = dotv(h, u);
        f[0] = h[0] - u[0] * d; f[1] = h[1] - u[1] * d; f[2] = h[2] - u[2] * d;
    }
    normv(f);
    float l[3];
    crossv(u, f, l);                                        /* u x f = left */
    if (s->vis_yaw > 0.002f || s->vis_yaw < -0.002f) {      /* a revert / powerslide being played out: the rider is drawn turned about the surface normal */
        float c, sn, f2[3];
        rot_cs(s->vis_yaw, &c, &sn);
        for (int i = 0; i < 3; i++) f2[i] = f[i] * c + l[i] * sn;
        f[0] = f2[0]; f[1] = f2[1]; f[2] = f2[2];
        crossv(u, f, l);
    }
    float roll_all = s->roll + s->flip_roll;                /* the lean, and a body flip rolling the whole rider about the board's long axis */
    float u_before[3] = { u[0], u[1], u[2] };
    if (roll_all > 0.002f || roll_all < -0.002f) {            /* lean: tilt the up vector towards the left (roll > 0) or the right */
        float c, sn, u2[3];
        rot_cs(roll_all, &c, &sn);
        for (int i = 0; i < 3; i++) u2[i] = u[i] * c + l[i] * sn;
        crossv(u2, f, l);
        u[0] = u2[0]; u[1] = u2[1]; u[2] = u2[2];
    }
    float pos[3] = { s->pos[0], s->pos[1], s->pos[2] };
    if (s->flip_roll != 0.0f)                               /* a body flip turns about the middle of the rider, not about his feet */
        for (int i = 0; i < 3; i++) pos[i] += 0.8f * (u_before[i] - u[i]);
    if (s->pitch > 0.002f || s->pitch < -0.002f) {          /* a manual: tilt about the left axis, pivoting on the rear (or front) wheels (positive = nose up) */
        float c, sn, f2[3], u2[3];
        rot_cs(s->pitch, &c, &sn);
        for (int i = 0; i < 3; i++) { f2[i] = f[i] * c + u[i] * sn; u2[i] = u[i] * c - f[i] * sn; }
        float pv = s->pitch > 0.0f ? MANUAL_PIVOT : -MANUAL_PIVOT;
        for (int i = 0; i < 3; i++) { pos[i] += pv * (f2[i] - f[i]); f[i] = f2[i]; u[i] = u2[i]; }
    }
    m[0] = l[0];  m[1] = l[1];  m[2] = l[2];  m[3] = 0.0f;
    m[4] = u[0];  m[5] = u[1];  m[6] = u[2];  m[7] = 0.0f;
    m[8] = f[0];  m[9] = f[1];  m[10] = f[2]; m[11] = 0.0f;
    m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2]; m[15] = 1.0f;
}
