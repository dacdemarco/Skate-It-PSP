/* Skating physics (no PSP dependencies - unit-tested on the host, see tests/test_skate.c).
 * World frame = the level's: y up, units ~ metres. The rider's local frame: +z forward (board nose), +y up, +x to the rider's left.
 *
 * The rider rolls ON a surface: a contact point with a normal (cn) and a facing direction tangent to it (fwd); speed is along fwd (negative =
 * rolling backwards). Gravity acts along the surface, so flats, stairs, ramps and the steep flanks of a bowl (up to ~80 degrees) all work with
 * the same code. Leaving the surface (a lip, a crest, an ollie) turns the rider into a ballistic body (vel) that lands on whatever surface it
 * meets. heading is the yaw of fwd (kept for the camera and the poses): forward = (sin h, 0, cos h); increasing heading turns left. */
#ifndef SKATE_H
#define SKATE_H

typedef struct {
    float steer;          /* -1 (right) .. +1 (left) */
    int push;             /* pressed this frame */
    int ollie_held;       /* ollie button is down */
    int brake;            /* brake button is down */
    int grind;            /* 1 + type (src/trick.h) = the grind the board does when it meets a rail or ledge (the nub picks it, src/control.c). The board locks on by itself - no button; 0 = a 50-50 */
    int manual;           /* a press this frame: 1 = start a manual, 2 = start a nose manual (either one ends the manual in progress) */
    float tilt;           /* the stick forwards (+1) / back (-1): shifts the rider's weight while balancing a manual */
    int flip;             /* a request this frame (one update only): 1 + flip type = start that flip in the air */
    int grab;             /* held: 0 = nothing, 1 + grab type = hold that grab (the type is taken when the grab starts) */
    int flip_pre;         /* on the ground: 1 + flip type while the rider holds a flip trick ready (Cross held after a flick of the nub, src/control.c). It takes the place of a push, charges the pop (ollie_held is set
                           * by the caller as well) and when the pop happens - ollie_held let go, flip_pre still set for that frame - the flip starts at once, in the same motion. */
    int nollie;           /* with flip / flip_pre: the NOLLIE version (the nollie clip of the trick, scored "NOLLIE ...") */
    int spin;             /* in the air, held: +1 turns the rider to the left (backside for the goofy default skater), -1 to the right (frontside); let go, it finishes the half turn it is in */
    int revert;           /* a request this frame (one update), on the ground: +1 = a half turn to the left (BS revert), -1 = to the right (FS): ends up rolling fakie */
    int slide;            /* held, on the ground: +1 / -1 = powerslide to the left / right (the board turned sideways, scrubbing speed), 0 = none */
    int push_held;        /* the push button is still down: a push whose button is held when the foot plants becomes the strong, slower push (a tap is the light, quick one) */
    int grind_switch;     /* a request this frame (one update), while grinding: 1 + type = change to that grind on the same rail (a flick of the nub) */
} SkateInput;

struct Rails;

typedef struct {
    /* queries into the level, supplied by the caller */
    void *ctx;
    /* highest walkable surface at or below y_max under (x, z); returns 1 and sets *y if found. Used only as a fallback across cracks in the floor. */
    int (*ground)(void *ctx, float x, float z, float y_max, float *y);
    /* does the segment a->b hit a wall (a surface too steep to roll on)? returns 1 and sets hit point + unit normal (pointing towards a) */
    int (*wall)(void *ctx, const float a[3], const float b[3], float hit[3], float normal[3]);
    /* nearest rideable surface (any slope up to ~80 degrees, either winding) crossed by the segment a->b: hit point + unit normal facing a */
    int (*surface)(void *ctx, const float a[3], const float b[3], float hit[3], float normal[3]);
    /* the grindable rails of the level (src/rails.h), or NULL */
    const struct Rails *rails;
} SkateWorld;

typedef struct {
    float pos[3];         /* contact point: on the surface under the board's centre */
    float heading;        /* radians, yaw of fwd */
    float speed;          /* m/s along fwd in the surface plane (negative = rolling backwards) */
    float vy;             /* vertical velocity while airborne (= vel[1]); 0 on the ground */
    int grounded;
    float steer;          /* smoothed steering, -1..1 */
    float charge;         /* 0..1 ollie charge while the button is held */
    float absorb;         /* 0..1 landing absorb, decays on its own */
    float crouch;         /* output for the pose logic = max(charge, absorb) */
    float push_t;         /* seconds since the push started; < 0 = not pushing */
    float push_boost;     /* m/s still to be added by the current push */
    float push_dur;       /* seconds the current push lasts (SKATE_PUSH_TIME for a tap, SKATE_PUSH_TIME_HOLD once the button is held through the foot plant); the pose follows push_t / push_dur */
    int push_strong;      /* the current push is the strong one */
    float air_t;          /* seconds airborne */
    float pop_hold_t;     /* seconds the pop button has been held (on a rail: a tap hops straight up and the board comes back down on the rail, a long press jumps OFF it, see GRIND_HOP_HOLD) */
    float up[3];          /* smoothed surface normal the rider stands on (unit) - for the body orientation */
    int landed;           /* set for one update when touching down (for effects) */
    /* physics state */
    float fwd[3];         /* unit facing: tangent to the surface on the ground, horizontal in the air */
    float cn[3];          /* unit normal of the surface in contact (valid while grounded) */
    float vel[3];         /* velocity while airborne */
    /* grinding: the rider slides along rail `rail` (arc length rail_t), moving in direction rail_sign (+1 = increasing t) at `speed`; grounded is 0 meanwhile */
    int grinding;
    unsigned int rail;
    float rail_t, rail_sign;
    float grind_cool;     /* seconds before the rider can lock on to a rail again (after leaving one) */
    float grind_time;     /* seconds on the current rail */
    int grind_type;       /* the grind type (index into the trick table) of the current / last grind */
    int grind_bs;         /* 1 = backside (the rail / ledge edge is on the rider's right, behind his back for the goofy default), 0 = frontside: decided when the board locks on */
    /* manual: 0 = none, 1 = manual (on the rear wheels), 2 = nose manual (on the front wheels); balanced with `lean` / `lean_v` like a grind, see skate.c. pitch = the board's tilt
     * for the renderer (radians, + = nose up, about the wheels it stands on). */
    /* air tricks: a flip plays its clip over flip_dur seconds (flip = 1 + type while it runs); a grab is held while the button is down. Landing before FLIP_LAND_OK of a flip is a bail. */
    int flip;
    int flip_nollie;                              /* the flip in progress is the nollie version */
    float flip_roll;                              /* body flips (no clip): how far the rider has rolled about the board's long axis (radians, + = head towards the left) */
    float flip_t, flip_dur;
    int grab;
    float grab_time;
    int ev_flip_done, ev_flip_type, ev_flip_nollie;       /* flips completed (and the type / nollie-ness of the last), counted for the scoring */
    /* spins: turning about the vertical in the air; fwd turns with it, so a half turn lands fakie (speed < 0) */
    float spin_ang;                               /* radians turned since take-off (+ = left) */
    float spin_dir;                               /* the direction it is going (+1 / -1) */
    int fakie_takeoff;                            /* took off rolling backwards */
    int ev_spin, ev_spin_deg, ev_spin_dir, ev_spin_fakie;       /* a landed spin: degrees (a multiple of 180), direction, started fakie */
    /* ground moves: a revert is a half turn on the ground; a powerslide has the board sideways while it scrubs speed. vis_yaw is a visual turn of the rider on top of fwd. */
    float vis_yaw, vis_target;
    int ev_revert;                                /* +1 / -1 = a revert to the left / right happened */
    int sliding; float slide_dir, slide_t;
    int ev_slide_start, ev_slide_end;             /* powerslide events (direction in slide_dir) */
    /* wallride: riding along a wall in the air (the trick button held while the board meets a wall at an angle, at speed) */
    int wallriding; float wall_n[3], wall_dir[3], wall_t, wall_side;       /* wall_side: +1 = the wall is on the rider's left (frontside for the goofy default) */
    int wall_held;                                /* ollie_held last update (a release jumps off the wall) */
    int ev_wall_start, ev_wall_end;
    int ev_grab_start, ev_grab_end, ev_grab_type, ev_grab_switch;       /* a grab changed to another variant while it was held (ev_grab_type = the new one) */
    int manual;
    float manual_time, pitch;
    int ev_manual_start, ev_manual_end;
    /* balance while grinding: lean -1..1 (+ = tipping to the rider's left); at +-1 the rider bails. The stick counters it: tilt it AWAY from the lean.
     * In a manual the same fields mean: lean > 0 = tipping too far (back for a manual, forward onto the nose for a nose manual): at +1 the rider bails; at -1 the board comes
     * down and the manual just ends. Shift your weight the other way: the stick forwards for a manual, back for a nose manual. */
    float lean, lean_v;
    int lean_armed;       /* the stick counts for balancing only once it has been near the centre since the lock-on (the direction that chose the grind must not unbalance it) */
    float roll;           /* smoothed body tilt for the renderer (radians, + = towards the left) */
    /* bail: the rider has fallen; no control for bail_t seconds, bail_dir = side fallen to (+1 left, -1 right) */
    float bail_t;
    float bail_dir;
    /* events for the scoring (src/score.c clears them): counted since it last looked */
    int ev_grind_start, ev_grind_end, ev_bail;
    int ev_grind_switch;  /* the grind changed to another type on the same rail (grind_type = the new one): the first one is over, the new one counts */
    int ev_grind_kind;    /* RAIL_KIND_* of the last grind that started (kept until the next one) */
    int ev_grind_mat;     /* the material (0 concrete / 1 metal / 2 wood) of the rail or ledge of the last grind */
    /* events for the sound effects (src/sfx.c clears them) */
    int ev_ollie;         /* ollies popped */
    int ev_ollie_plain, ev_ollie_nollie;   /* for the scoring (src/score.c clears them): plain ollies popped (no flip trick with the pop) and whether the last one was a nollie */
    int ev_scrape;        /* wall scrapes (sliding along a wall at speed) and the side of the wall: +1 left, -1 right */
    float scrape_side;
    float ollie_pop;      /* speed of the last pop (m/s) */
    int ev_land;          /* touchdowns (on a surface, not a rail) */
    float land_impact;    /* speed into the surface of the last touchdown (m/s) */
} Skate;

#define SKATE_GRAVITY      9.81f
#define SKATE_MAX_SPEED    8.0f
#define SKATE_PUSH_TIME    0.55f      /* a tap of the push button: the light, quick push */
#define SKATE_PUSH_TIME_HOLD 1.00f    /* the button held through the foot plant: the strong, slower push */
#define FLIP_LAND_OK       0.60f      /* a flip landed after this much of it counts; before it, it is a bail */

/* The player's physics tuning (Options > Gravity): scales of the defaults, 1 = as designed. Read by skate_update; the caller sets it from the settings. */
typedef struct {
    float gravity;        /* gravity in the air, on slopes and on rails */
    float ollie;          /* the speed of a pop */
    float push;           /* the speed a push gives, and the speed up to which pushing still helps */
} SkateTune;
extern SkateTune skate_tune;

/* Why the board does (not) lock on to the nearest rail or ledge right now - a diagnostic for the HUD. found = a line within 2 m (any direction); dist = sideways distance (m),
 * dy = the board's height over the line (+ = above), side = which side of a ledge (+ = the top), reason = GP_*. */
enum { GP_OK = 0, GP_GROUND, GP_BAIL, GP_COOLDOWN, GP_FAR, GP_LOW, GP_HIGH, GP_WRONG_SIDE, GP_SLOW, GP_ACROSS };
typedef struct {
    int found;
    unsigned int rail;
    int kind;
    float dist, dy, side, speed_along, align;
    int reason;
} GrindProbe;
int skate_grind_probe(const Skate *s, const SkateInput *in, const SkateWorld *w, GrindProbe *p);

void skate_init(Skate *s, float x, float y, float z, float heading);
void skate_update(Skate *s, const SkateInput *in, const SkateWorld *w, float dt);
/* column-major 4x4 (OpenGL layout, as used by sceGumLoadMatrix): body frame at pos, built from fwd and the smoothed surface up */
/* the body matrix of a rider who rides regular: the same matrix with the left axis reversed (a mirror image; the clips are goofy ones) */
void skate_mirror_matrix(float m[16]);
void skate_body_matrix(const Skate *s, float m[16]);

#endif
