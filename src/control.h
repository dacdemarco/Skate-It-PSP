/* Controls: turns the raw pad (the analog nub + buttons) and what the rider is doing into a SkateInput. No PSP dependencies - host-tested with scripted stick movements
 * (tests/test_control.c). The six action bits below come from the physical buttons through the player's mapping (src/settings.c); the names are the default buttons.
 *
 * The scheme (a FlickIt-like version of the original DS stylus controls, on one nub):
 *   nub             steers on the ground (and a little in the air - not while a rail or ledge is within reach: the nub then picks the grind)
 *   POP (Cross)     hold = crouch, release = ollie.  A FLICK of the nub (a quick snap to the rim) while it is held - or just before it is pressed - picks a flip trick that
 *                   starts in the same motion as the pop: the BASIC ring (the table below).
 *   ADVANCED POP (Triangle)  the same, with the ADVANCED ring; both pop buttons held = the body ring (360 hardflip / 360 inward heel / frontflip / backflip).
 *   NOLLIE          hold the nub FORWARD (up) while crouching: the rider shifts onto the nose (the HUD says NOLLIE); then flick for the nollie version of the trick, or just let go for a nollie.
 *                   A flick straight down goes back to the tail (a plain ollie). The stance lasts until the pop.
 *   nub flick       in the air: the six LATE flips; with PUSH held (pressed in the air): nub left / right = spin (held; he finishes the half turn he is in), flick up / down = front / back flip
 *   nub held        down (or up) for a moment while rolling = manual (nose manual); the nub then balances it (forwards / back), a pop gets out of it
 *   BRAKE (Circle)  brake; with it held a flick left / right = REVERT (a half turn on the ground), the nub held hard over at speed = POWERSLIDE
 *   GRAB L / R      in the air: grab. L = the back hand (BS grab / FS grab with the nub left / right, tail grab up, stale down), R = the front hand (melon, nose grab up, mute down), both = double grab.
 *                   A flick while it is held picks the variant: up / down / left-right = nosebone / tailbone / tuck knee (shifty nose / tail, rocket), a diagonal = the tweaked ones
 *   GRINDS          the board locks on to any rail or ledge it comes down on - no button. The nub picks the grind at that moment (neutral 50-50, up nosegrind, down 5-0, left boardslide,
 *                   right lipslide, diagonals noseslide / tailslide / blunts); with the ADVANCED POP held the other seven (smith, feeble, crooked, overcrooks, willy, overwilly, salad).
 *                   Backside / frontside follows the side of the rail. No balancing on a rail: a flick while grinding switches to that grind; a tap of a pop button hops up (and back on to the
 *                   rail), a pop button held and let go jumps off towards the nub. In the air along a tall wall at speed: WALLRIDE (also by itself).
 *   L / R on the ground while a pop button is held: a small steer left / right (micro adjustments while crouching; the nub still steers too). Held through the pop they are not a grab.
 *   PUSH (Square)   tap = a light, quick push; held = a stronger, slower one. Pressed in the air: the modifier of spins and body flips
 *   D-pad           left / right = camera orbit, up / down = camera distance
 *
 * Rings (the flick direction):   POP held (basic)        ADVANCED POP held       both held (body)
 *                                up-left   kickflip      varial kickflip         360 hardflip
 *                                up-right  heelflip      varial heelflip         360 inward heel
 *                                left      pop shuvit    hardflip                (up = frontflip, down = backflip)
 *                                right     FS shuvit     inward heel
 *                                down-left 360 shuvit    360 flip
 *                                down-right FS 360       laser flip */
#ifndef CONTROL_H
#define CONTROL_H

#include "skate.h"

/* the action bits (in the order of the ACT_* actions of src/settings.h), named after their default buttons ... */
enum { CTL_CROSS = 1, CTL_SQUARE = 2, CTL_CIRCLE = 4, CTL_TRIANGLE = 8, CTL_L = 16, CTL_R = 32 };
/* ... and by what they do */
enum { CTL_POP = CTL_CROSS, CTL_PUSH = CTL_SQUARE, CTL_BRAKE = CTL_CIRCLE, CTL_POP_ADV = CTL_TRIANGLE, CTL_GRAB_L = CTL_L, CTL_GRAB_R = CTL_R };

/* eight stick directions, clockwise from up */
enum { DIR_UP = 0, DIR_UPRIGHT, DIR_RIGHT, DIR_DOWNRIGHT, DIR_DOWN, DIR_DOWNLEFT, DIR_LEFT, DIR_UPLEFT };

/* which ring of flip tricks a flick picks from */
enum { RING_BASIC = 0, RING_ADV = 1, RING_BODY = 2 };

#define CTL_HIST 24

typedef struct {
    float ax, ay;                   /* the nub: ax +1 = right, ay +1 = down (the dead zone is applied by the caller) */
    unsigned held, pressed;         /* CTL_* bits: down now / went down this frame */
    /* what the rider is doing (copies of the Skate fields) */
    int grounded, grinding, bail;   /* bail: bail_t > 0 */
    int manual, flip, grab;
    int wallriding;
    int rail_near;                  /* a rail or ledge is within reach of the board (skate_grind_probe): the nub picks the grind and does not steer */
    float speed;
} CtlFrame;

typedef struct {
    float t;                        /* the clock */
    float hx[CTL_HIST], hy[CTL_HIST], ht[CTL_HIST];     /* the stick of the last frames (ring) */
    int hn;
    float pend;                     /* > 0: a snap of the stick was seen; its direction is read when this runs out */
    float quiet_t;                  /* the time of the last recognised flick: the next one is looked for after it */
    int flick_dir;                  /* the last flick (DIR_*), -1 none */
    float flick_t;                  /* when it happened */
    int flick_new;                  /* a flick was recognised on this very update */
    int steer_lock;                 /* a flick is going on: the stick does not steer until it is back near the centre */
    int ready;                      /* the flip slot (src/trick.h) that will start at the pop, or -1 */
    int nollie;                     /* the nollie stance: the trick that follows is the nollie version */
    float nollie_hold;              /* seconds the nub has been held forward while crouching */
    int ring_prev;                  /* the ring of the pop buttons held on the last update (the release frame has none held) */
    unsigned micro;                 /* the grab buttons (CTL_L / CTL_R) that were held while crouching on the ground: they steered, so they are no grab until let go */
    int push_air;                   /* the push button's hold began in the air: it is the spin / body-flip modifier (a push held from the ground is not) */
    int pend_flip; float pend_flip_t;   /* an air flick waiting for the physics to take it */
    int cross_prev, air_prev;
    float hold_down, hold_up;       /* seconds the stick has been held down / up (manual) */
    int man_armed;
    float grab_arm; int grab_ready;
    int grab_base, grab_slot;       /* the grab the buttons started with, and the variant a flick has changed it to */
    float slide_hold; int slide_on; /* a powerslide: how long the stick has been held hard over; latched while it goes on */
} Control;

void control_init(Control *c);
/* one update: the SkateInput for this frame (its push / flip / manual / revert / grind_switch fields are one-shot: the caller clears them after the physics steps of a long frame) */
void control_update(Control *c, const CtlFrame *f, float dt, SkateInput *in);

int ctl_dir8(float ax, float ay);   /* -1 when the stick is inside the dead radius, else DIR_* */
int ctl_dir5(float ax, float ay);   /* 0 neutral, 1 up, 2 down, 3 left, 4 right: the slots of grinds and grabs */
int ctl_flip_slot(int dir8);        /* the flip table slot of a flick direction in the basic ring, -1 for none (up / down) */
int ctl_ring_slot(int ring, int dir8);  /* the same for any ring */
int ctl_air_slot(int dir8);         /* the late flip of a flick in the air */
int ctl_grind_slot(int advanced, int dir8);   /* the grind of the nub (dir8 -1 = neutral), with the advanced pop held or not */
int ctl_grab_base(unsigned buttons, int dir5);   /* the grab slot a L / R press starts with */
int ctl_grab_variant(int base, int dir8);        /* the variant of that grab a flick picks, or -1 */

#endif
