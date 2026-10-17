#include "control.h"
#include "trick.h"
#include <math.h>
#include <string.h>

#define FLICK_WIN     0.12f    /* a flick is a snap of the stick: it travels FLICK_DIST within this many seconds ... */
#define FLICK_DIST    0.50f
#define FLICK_RIM     0.60f    /* ... and ends at least this far from the centre (the recoil of the nub back to the centre must not count) */
#define FLICK_SETTLE  0.04f    /* the direction is read this long after the snap was seen, once the stick has arrived */
#define FLICK_BEFORE  0.25f    /* a flick this long before the pop button went down still counts for that pop */
#define GROUND_LOCK   0.08f    /* crouching on the ground, the stick steers again this long after a flick was read (in the air it waits until the stick is back near the centre) */
#define DIAG_MAG      0.80f    /* a diagonal held this far out at the pop counts as a flick */
#define MICRO_STEER   0.4f     /* the steer of L / R while a pop button is held (the nub's full steer is 1) */
#define DIAG_LO       32.0f    /* ... if its angle from the horizontal is within DIAG_LO .. DIAG_HI degrees: steering is horizontal and must not become a trick */
#define DIAG_HI       58.0f
#define NOLLIE_HOLD   0.10f    /* seconds the nub is held forward while crouching to shift onto the nose (the nollie stance) */
#define NOLLIE_Y      0.65f    /* ... this far forward, and not more than NOLLIE_X sideways */
#define NOLLIE_X      0.6f
#define AIR_FLIP_HOLD 0.12f    /* an air flick is kept this long for the physics, which starts flips only a moment after take-off */
#define MAN_HOLD      0.22f    /* seconds the stick is pulled down / up to start a manual / nose manual */
#define MAN_MIN_SPEED 1.5f     /* m/s: the physics starts one only from this speed */
#define GRAB_SETTLE   0.07f    /* the stick direction of a grab is read this long after the button */
#define SPIN_STICK    0.5f     /* the nub this far left / right with the push button held (pressed in the air) spins */
#define REVERT_SPEED  1.5f
#define SLIDE_STICK   0.8f     /* a powerslide: the nub held this far over ... */
#define SLIDE_HOLD    0.25f    /* ... for this long ... */
#define SLIDE_SPEED   3.0f     /* ... at this speed, with no flick in the last SLIDE_QUIET seconds */
#define SLIDE_QUIET   0.6f

int ctl_dir8(float ax, float ay)
{
    if (ax * ax + ay * ay < 0.25f) return -1;
    float ang = atan2f(ax, -ay) * 57.29578f;                 /* 0 = up, positive towards the right */
    if (ang < 0.0f) ang += 360.0f;
    return (int)((ang + 22.5f) / 45.0f) % 8;
}

int ctl_dir5(float ax, float ay)
{
    if (fabsf(ay) >= fabsf(ax)) { if (ay < -0.5f) return 1; if (ay > 0.5f) return 2; }
    else { if (ax < -0.5f) return 3; if (ax > 0.5f) return 4; }
    return 0;
}

int ctl_ring_slot(int ring, int dir8)
{
    switch (ring) {
    case RING_ADV:
        switch (dir8) {
        case DIR_UPLEFT:    return FLIP_VARIAL_KICK;
        case DIR_UPRIGHT:   return FLIP_VARIAL_HEEL;
        case DIR_LEFT:      return FLIP_HARDFLIP;
        case DIR_RIGHT:     return FLIP_INWARD_HEEL;
        case DIR_DOWNLEFT:  return FLIP_360_FLIP;
        case DIR_DOWNRIGHT: return FLIP_LASER;
        default:            return -1;
        }
    case RING_BODY:
        switch (dir8) {
        case DIR_UPLEFT:    return FLIP_360_HARDFLIP;
        case DIR_UPRIGHT:   return FLIP_360_INWARD;
        case DIR_UP:        return FLIP_FRONTFLIP;           /* the stick forwards = forwards */
        case DIR_DOWN:      return FLIP_BACKFLIP;
        default:            return -1;
        }
    default:
        switch (dir8) {
        case DIR_UPLEFT:    return FLIP_KICKFLIP;
        case DIR_UPRIGHT:   return FLIP_HEELFLIP;
        case DIR_LEFT:      return FLIP_POP_SHUVIT;
        case DIR_RIGHT:     return FLIP_FS_POP_SHUVIT;
        case DIR_DOWNLEFT:  return FLIP_360_SHUVIT;
        case DIR_DOWNRIGHT: return FLIP_FS_360_SHUVIT;
        default:            return -1;                       /* up = the nollie stance, down = back to the tail: no flip */
        }
    }
}

int ctl_flip_slot(int dir8) { return ctl_ring_slot(RING_BASIC, dir8); }

int ctl_air_slot(int dir8)
{
    switch (dir8) {
    case DIR_UPLEFT:    return FLIP_LATE_KICK;
    case DIR_UPRIGHT:   return FLIP_LATE_HEEL;
    case DIR_LEFT:      return FLIP_LATE_SHUVIT;
    case DIR_RIGHT:     return FLIP_LATE_FS_SHUVIT;
    case DIR_DOWNLEFT:  return FLIP_LATE_BF_KICK;
    case DIR_DOWNRIGHT: return FLIP_LATE_BF_HEEL;
    default:            return -1;
    }
}

int ctl_grind_slot(int adv, int dir8)
{
    if (!adv) {
        switch (dir8) {
        case DIR_UP:        return GRIND_NOSEGRIND;
        case DIR_DOWN:      return GRIND_50;
        case DIR_LEFT:      return GRIND_BOARDSLIDE;
        case DIR_RIGHT:     return GRIND_LIPSLIDE;
        case DIR_UPLEFT:    return GRIND_NOSESLIDE;
        case DIR_DOWNLEFT:  return GRIND_TAILSLIDE;
        case DIR_UPRIGHT:   return GRIND_NOSEBLUNT;
        case DIR_DOWNRIGHT: return GRIND_BLUNT;
        default:            return GRIND_5050;
        }
    }
    switch (dir8) {
    case DIR_UP:        return GRIND_CROOKED;
    case DIR_DOWN:      return GRIND_FEEBLE;
    case DIR_LEFT:      return GRIND_OVERCROOKS;
    case DIR_RIGHT:     return GRIND_WILLY;
    case DIR_UPLEFT:    return GRIND_OVERWILLY;
    case DIR_DOWNLEFT:  return GRIND_SALAD;
    default:            return GRIND_SMITH;
    }
}

int ctl_grab_base(unsigned buttons, int dir5)
{
    int l = (buttons & CTL_L) != 0, r = (buttons & CTL_R) != 0;
    if (l && r) return GRAB_DOUBLE;
    if (l) {                                              /* the back hand: indy (the book's BS / FS grab), tail grab, stale */
        switch (dir5) { case 1: return GRAB_TAIL; case 2: return GRAB_STALE; case 4: return GRAB_FS_GRAB; default: return GRAB_BS_GRAB; }
    }
    switch (dir5) { case 1: return GRAB_NOSE; case 2: return GRAB_MUTE; default: return GRAB_MELON; }          /* the front hand: melon, nose grab, mute */
}

/* the variant of a grab family a flick picks; base is any slot of the family the grab started in */
int ctl_grab_variant(int base, int dir8)
{
    int fam_first, v[8];
    for (int i = 0; i < 8; i++) v[i] = -1;
    switch (base) {
    case GRAB_BS_GRAB: case GRAB_FS_GRAB: case GRAB_BS_TWEAK: case GRAB_NOSEBONE: case GRAB_TAILBONE: case GRAB_TUCK_KNEE:
        fam_first = base == GRAB_FS_GRAB ? GRAB_FS_GRAB : GRAB_BS_GRAB;
        v[DIR_UP] = GRAB_NOSEBONE; v[DIR_DOWN] = GRAB_TAILBONE; v[DIR_LEFT] = v[DIR_RIGHT] = GRAB_TUCK_KNEE; v[DIR_UPLEFT] = v[DIR_UPRIGHT] = GRAB_BS_TWEAK;
        break;
    case GRAB_MELON: case GRAB_METHOD:
        fam_first = GRAB_MELON; v[DIR_UPLEFT] = v[DIR_UPRIGHT] = GRAB_METHOD;
        break;
    case GRAB_STALE: case GRAB_STALE_NOSEBONE: case GRAB_STALE_TAILBONE: case GRAB_STALE_TUCK:
        fam_first = GRAB_STALE; v[DIR_UP] = GRAB_STALE_NOSEBONE; v[DIR_DOWN] = GRAB_STALE_TAILBONE; v[DIR_LEFT] = v[DIR_RIGHT] = GRAB_STALE_TUCK;
        break;
    case GRAB_MUTE: case GRAB_MUTE_NOSEBONE: case GRAB_MUTE_TAILBONE: case GRAB_MUTE_TUCK:
        fam_first = GRAB_MUTE; v[DIR_UP] = GRAB_MUTE_NOSEBONE; v[DIR_DOWN] = GRAB_MUTE_TAILBONE; v[DIR_LEFT] = v[DIR_RIGHT] = GRAB_MUTE_TUCK;
        break;
    case GRAB_NOSE: case GRAB_BS_SHIFTY_NOSE: case GRAB_FS_SHIFTY_NOSE: case GRAB_ROCKET:
        fam_first = GRAB_NOSE; v[DIR_UP] = GRAB_BS_SHIFTY_NOSE; v[DIR_DOWN] = GRAB_FS_SHIFTY_NOSE; v[DIR_LEFT] = v[DIR_RIGHT] = GRAB_ROCKET;
        break;
    case GRAB_TAIL: case GRAB_BS_SHIFTY_TAIL: case GRAB_FS_SHIFTY_TAIL:
        fam_first = GRAB_TAIL; v[DIR_UP] = GRAB_BS_SHIFTY_TAIL; v[DIR_DOWN] = GRAB_FS_SHIFTY_TAIL;
        break;
    default:                                              /* the double grab and the poses that need both hands / the whole body */
        fam_first = GRAB_DOUBLE; v[DIR_UP] = GRAB_CHRIST; v[DIR_DOWN] = GRAB_COFFIN; v[DIR_LEFT] = GRAB_CROSSBONE; v[DIR_RIGHT] = GRAB_STIFFY; v[DIR_UPLEFT] = GRAB_NOFOOT;
        break;
    }
    if (dir8 == DIR_DOWNLEFT || dir8 == DIR_DOWNRIGHT) return fam_first;               /* the diagonal down: back to the plain grab */
    return dir8 >= 0 ? v[dir8] : -1;
}

void control_init(Control *c)
{
    memset(c, 0, sizeof(*c));
    c->flick_dir = -1; c->ready = -1;
    c->quiet_t = -1.0f; c->flick_t = -100.0f;
    c->man_armed = 1;
}

/* a diagonal held out far enough: its direction, or -1 */
static int held_diagonal(float ax, float ay)
{
    if (ax * ax + ay * ay < DIAG_MAG * DIAG_MAG) return -1;
    float a = atan2f(fabsf(ay), fabsf(ax)) * 57.29578f;
    if (a < DIAG_LO || a > DIAG_HI) return -1;
    return ay < 0.0f ? (ax < 0.0f ? DIR_UPLEFT : DIR_UPRIGHT) : (ax < 0.0f ? DIR_DOWNLEFT : DIR_DOWNRIGHT);
}

/* the ring the pop buttons held pick: both = the body flips, the advanced pop alone = the advanced ring, else the basic one */
static int ring_of(unsigned held)
{
    int basic = (held & CTL_POP) != 0, adv = (held & CTL_POP_ADV) != 0;
    return basic && adv ? RING_BODY : (adv ? RING_ADV : RING_BASIC);
}

/* a flick with a pop button held on the ground: sets a flip up, or the nollie stance (up) / back to the tail (down) */
static void ground_flick(Control *c, int dir, int ring)
{
    if (ring != RING_BODY && dir == DIR_UP) { c->nollie = 1; return; }          /* a snap forwards: onto the nose (the trick that was ready stays, as its nollie version) */
    if (ring != RING_BODY && dir == DIR_DOWN) { c->nollie = 0; c->ready = -1; return; }     /* a snap back: a plain ollie again */
    c->ready = ctl_ring_slot(ring, dir);
}

void control_update(Control *c, const CtlFrame *f, float dt, SkateInput *in)
{
    memset(in, 0, sizeof(*in));
    c->t += dt;
    float ax = f->ax, ay = f->ay, mag = sqrtf(ax * ax + ay * ay);
    int adv = (f->held & CTL_POP_ADV) != 0;
    int cross = (f->held & (CTL_POP | CTL_POP_ADV)) != 0;      /* any pop button: crouch */
    int push_b = (f->held & CTL_PUSH) != 0, circle = (f->held & CTL_BRAKE) != 0;
    int ground_ok = f->grounded && !f->grinding && !f->bail;
    /* L / R held while crouching on the ground steer (below): a button that did so is not a grab until it has been let go, so a micro adjustment up to the pop does not start a grab */
    if (cross && ground_ok) c->micro |= f->held & (CTL_L | CTL_R);
    c->micro &= f->held;
    unsigned gheld = f->held & (CTL_L | CTL_R) & ~c->micro;      /* the grab buttons */
    int grabbtn = gheld != 0;
    int airborne = !f->grounded && !f->grinding && !f->bail && !f->wallriding;
    int ring = ring_of(f->held);
    /* the push button is the modifier of spins and body flips only when its hold began in the air (a strong push held from the ground into a jump is not one) */
    if (!push_b) c->push_air = 0;
    else if ((f->pressed & CTL_PUSH) && airborne) c->push_air = 1;
    int sq_air = push_b && c->push_air;

    /* ---- the stick history, and flick detection ---- */
    {
        int i = c->hn % CTL_HIST;
        c->hx[i] = ax; c->hy[i] = ay; c->ht[i] = c->t;
        c->hn++;
    }
    c->flick_new = 0;
    int context = (cross && ground_ok) || airborne || (circle && ground_ok) || f->grinding;       /* a flick matters now (and the stick must not also steer or balance) */
    if (c->pend > 0.0f) {
        c->pend -= dt;
        if (c->pend <= 0.0f) {
            c->pend = 0.0f;
            int d = ctl_dir8(ax, ay);                           /* where the snap ended up; -1 when the stick has already come back */
            if (d >= 0) { c->flick_dir = d; c->flick_t = c->t; c->flick_new = 1; }
            c->quiet_t = c->t;
        }
    } else {
        float t0 = c->t - FLICK_WIN;
        if (t0 < c->quiet_t) t0 = c->quiet_t;
        float px = ax, py = ay;
        int n = c->hn < CTL_HIST ? c->hn : CTL_HIST;
        for (int k = 1; k <= n; k++) {                          /* newest to oldest: the oldest sample inside the window is where the snap began */
            int i = (c->hn - k) % CTL_HIST;
            if (c->ht[i] < t0) break;
            px = c->hx[i]; py = c->hy[i];
        }
        float dx = ax - px, dy = ay - py;
        if (dx * dx + dy * dy >= FLICK_DIST * FLICK_DIST && mag >= FLICK_RIM) {
            c->pend = FLICK_SETTLE;
            if (context) c->steer_lock = 1;
        }
    }
    if (mag < 0.3f) c->steer_lock = 0;
    /* crouching on the ground, the lock only lasts until the flick has been read: a stick then kept over is the player steering (a quick push of the nub to the side is a flick too, and
     * without this he could not turn at all while holding a pop button) */
    if (c->steer_lock && cross && ground_ok && c->pend <= 0.0f && c->t - c->flick_t >= GROUND_LOCK) c->steer_lock = 0;

    /* ---- the ground: a pop button = crouch and pop; a flick before / during the hold sets a flip up that starts with the pop; the nub held forward = the nollie stance ---- */
    if (!ground_ok) { c->ready = -1; c->nollie = 0; c->nollie_hold = 0.0f; }
    if (ground_ok && (f->pressed & (CTL_POP | CTL_POP_ADV)) && c->flick_dir >= 0 && c->t - c->flick_t <= FLICK_BEFORE)
        ground_flick(c, c->flick_dir, ring);
    if (cross && ground_ok && c->flick_new)
        ground_flick(c, c->flick_dir, ring);                    /* a later flick replaces it (up = onto the nose, down = back to the tail) */
    if (cross && ground_ok && ring != RING_BODY && ay < -NOLLIE_Y && fabsf(ax) < NOLLIE_X) {       /* (the body flips have no nollie version: forwards is the frontflip there) */
        c->nollie_hold += dt;
        if (c->nollie_hold >= NOLLIE_HOLD) c->nollie = 1;      /* the weight is on the nose: the pop (and the trick flicked from here) is a nollie, until the pop */
    } else {
        c->nollie_hold = 0.0f;
    }
    in->ollie_held = cross;
    if (cross && ground_ok && c->ready >= 0) { in->flip_pre = 1 + c->ready; in->nollie = c->nollie; }
    if (!cross && c->cross_prev && f->grounded && !f->grinding) {      /* let go: the pop - and the flip with it */
        int slot = c->ready;
        if (slot < 0) { int d = held_diagonal(ax, ay); if (d >= 0) slot = ctl_ring_slot(c->ring_prev, d); }
        if (slot >= 0) { in->flip_pre = 1 + slot; in->nollie = c->nollie; }
        else if (c->nollie) in->nollie = 1;                     /* a nollie without a flip trick: a plain nollie ollie */
        c->ready = -1;
    }
    if (!cross) { c->ready = -1; c->nollie = 0; c->nollie_hold = 0.0f; }

    /* ---- the air: a flick starts a late flip (kept for a moment: the physics waits a few frames after take-off); with the push button (pressed in the air) the nub spins / body-flips ---- */
    if (!airborne) c->pend_flip_t = 0.0f;
    else if (c->flick_new && !adv && !grabbtn && !f->flip && !f->grab) {
        int slot = sq_air ? (c->flick_dir == DIR_UP ? FLIP_FRONTFLIP : (c->flick_dir == DIR_DOWN ? FLIP_BACKFLIP : -1)) : ctl_air_slot(c->flick_dir);
        if (slot >= 0) { c->pend_flip = slot; c->pend_flip_t = AIR_FLIP_HOLD; }
    }
    if (airborne && c->pend_flip_t > 0.0f) {
        if (f->flip) c->pend_flip_t = 0.0f;
        else { in->flip = 1 + c->pend_flip; c->pend_flip_t -= dt; }
    }
    if (airborne && sq_air && !adv && !grabbtn && fabsf(ax) >= SPIN_STICK) in->spin = ax < 0.0f ? 1 : -1;      /* left = turn left (backside for the goofy default) */

    /* ---- steering ---- */
    in->steer = -ax;
    if (c->steer_lock) in->steer = 0.0f;
    if (airborne && (grabbtn || f->flip || sq_air || f->rail_near)) in->steer = 0.0f;       /* (near a rail or ledge the nub is the grind choice, not a turn) */
    if (cross && ground_ok) {                                  /* micro adjustments: L = a little to the left, R = a little to the right (both: none), on top of the nub */
        in->steer += MICRO_STEER * (float)(((f->held & CTL_L) != 0) - ((f->held & CTL_R) != 0));
        if (in->steer > 1.0f) in->steer = 1.0f;
        if (in->steer < -1.0f) in->steer = -1.0f;
    }
    in->tilt = -ay;

    /* ---- push, brake, grinds ---- */
    in->push = (f->pressed & CTL_PUSH) != 0 && !cross && f->grounded;      /* no push while crouching */
    in->push_held = push_b && !cross;
    in->brake = circle;
    in->grind = 1 + ctl_grind_slot(adv, ctl_dir8(ax, ay));    /* the grind the board does if it meets a rail or ledge now (it locks on by itself) */
    if (f->grinding && c->flick_new) in->grind_switch = 1 + ctl_grind_slot(adv, c->flick_dir);     /* a flick while grinding: another grind on the same rail */

    /* ---- the brake on the ground: a flick left / right = revert; the nub held hard over at speed = powerslide ---- */
    if (circle && ground_ok && !cross && !f->manual) {
        if (c->flick_new && f->speed >= REVERT_SPEED && c->flick_dir != DIR_UP && c->flick_dir != DIR_DOWN)
            in->revert = (c->flick_dir == DIR_LEFT || c->flick_dir == DIR_UPLEFT || c->flick_dir == DIR_DOWNLEFT) ? 1 : -1;
        if (!c->slide_on) {
            c->slide_hold = (fabsf(ax) >= SLIDE_STICK && f->speed >= SLIDE_SPEED && c->t - c->flick_t > SLIDE_QUIET) ? c->slide_hold + dt : 0.0f;
            if (c->slide_hold >= SLIDE_HOLD) c->slide_on = 1;
        } else if (fabsf(ax) < 0.5f) {
            c->slide_on = 0; c->slide_hold = 0.0f;
        }
        if (c->slide_on) in->slide = ax < 0.0f ? 1 : -1;
    } else {
        c->slide_on = 0; c->slide_hold = 0.0f;
    }

    /* ---- manual: the stick pulled down (manual) or up (nose manual) for a moment while rolling; it must come back near the centre before another can start ---- */
    if (ground_ok && !f->manual && !cross && !circle && c->ready < 0 && f->speed >= MAN_MIN_SPEED) {
        if (c->man_armed) {
            c->hold_down = (ay > 0.65f && fabsf(ax) < 0.6f) ? c->hold_down + dt : 0.0f;
            c->hold_up = (ay < -0.65f && fabsf(ax) < 0.6f) ? c->hold_up + dt : 0.0f;
            if (c->hold_down >= MAN_HOLD) { in->manual = 1; c->man_armed = 0; c->hold_down = c->hold_up = 0.0f; }
            else if (c->hold_up >= MAN_HOLD) { in->manual = 2; c->man_armed = 0; c->hold_down = c->hold_up = 0.0f; }
        }
    } else {
        c->hold_down = c->hold_up = 0.0f;
    }
    if (fabsf(ay) < 0.35f) c->man_armed = 1;

    /* ---- grabs: L / R in the air (also held through the take-off); the stick direction is read a moment later ---- */
    if (!grabbtn || !airborne) { c->grab_arm = 0.0f; c->grab_ready = 0; }
    if (grabbtn && airborne && ((f->pressed & gheld) || !c->air_prev) && c->grab_arm <= 0.0f && !c->grab_ready) c->grab_arm = GRAB_SETTLE;
    if (c->grab_arm > 0.0f) {
        c->grab_arm -= dt;
        if (c->grab_arm <= 0.0f) { c->grab_arm = 0.0f; c->grab_ready = 1; c->grab_base = c->grab_slot = ctl_grab_base(gheld, ctl_dir5(ax, ay)); }
    }
    if (c->grab_ready && airborne && c->flick_new) {          /* a flick while it is held: its variant */
        int v = ctl_grab_variant(c->grab_base, c->flick_dir);
        if (v >= 0) c->grab_slot = v;
    }
    in->grab = (airborne && c->grab_ready) ? 1 + c->grab_slot : 0;

    c->cross_prev = cross;
    c->air_prev = airborne;
    if (cross) c->ring_prev = ring;
}
