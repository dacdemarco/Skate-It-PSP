#include "score.h"
#include "trick.h"
#include <stdio.h>

void score_init(Score *sc)
{
    sc->total = 0;
    score_cancel(sc);
    sc->msg = SCORE_MSG_NONE; sc->msg_t = 0.0f; sc->msg_pts = 0; sc->msg_n = 0; sc->msg_mult = 1;
    sc->ev_bank = 0; sc->ev_bank_pts = 0; sc->ev_bank_mult = 1;
    sc->cur_type = 0; sc->cur_kind = 0; sc->cur_cls = TRICK_GRIND; sc->cur_nollie = 0; sc->cur_deg = 0; sc->cur_bs = -1;
    sc->bank_n = 0; sc->bank_pts = 0; sc->bank_mult = 1; sc->bank_serial = 0; sc->flight = 0; sc->prev_air = 0; sc->pos[0] = sc->pos[1] = sc->pos[2] = 0.0f;
}

void score_cancel(Score *sc)
{
    sc->combo_active = 0; sc->combo_n = 0; sc->combo_base = 0; sc->live = 0.0f; sc->in_grind = 0; sc->roll_t = 0.0f; sc->log_n = 0; sc->cur_rec = -1;
}

int score_combo_mult(const Score *sc)
{
    int m = sc->combo_n < 1 ? 1 : sc->combo_n;
    return m > SCORE_MAX_MULT ? SCORE_MAX_MULT : m;
}

long score_combo_points(const Score *sc)
{
    return (sc->combo_base + (long)sc->live) * score_combo_mult(sc);
}

const char *score_trick_name(const Score *sc)
{
    static char buf[48];
    const char *n = trick_def(sc->cur_cls, sc->cur_type)->name;
    if (sc->cur_cls == TRICK_GRIND && sc->cur_bs >= 0) { snprintf(buf, sizeof(buf), "%s %s", sc->cur_bs ? "BS" : "FS", n); return buf; }
    if (sc->cur_cls == TRICK_FLIP && sc->cur_nollie) { snprintf(buf, sizeof(buf), "NOLLIE %s", n); return buf; }
    if (sc->cur_cls == TRICK_MOVE && sc->cur_deg > 180) { snprintf(buf, sizeof(buf), "%s %d", n, sc->cur_deg); return buf; }
    return n;
}

/* append the trick to the combo's log; returns its index (or -1 when the log is full) */
static int log_add(Score *sc, int cls, int type, int bs, int nollie, int deg, unsigned rail, long pts)
{
    if (sc->log_n >= SCORE_LOG_MAX) return -1;
    TrickRec *r = &sc->log[sc->log_n];
    r->cls = (short)cls; r->type = (short)type; r->bs = (signed char)bs; r->nollie = (unsigned char)nollie; r->deg = (short)deg; r->rail = (unsigned short)rail;
    r->flight = sc->flight; r->pos[0] = sc->pos[0]; r->pos[1] = sc->pos[1]; r->pos[2] = sc->pos[2]; r->dur = 0.0f; r->pts = pts;
    return sc->log_n++;
}

static void combo_open(Score *sc)
{
    if (!sc->combo_active) { sc->combo_active = 1; sc->combo_n = 0; sc->combo_base = 0; }
    else if (sc->in_grind) { if (sc->cur_rec >= 0 && sc->cur_rec < sc->log_n) sc->log[sc->cur_rec].pts = (long)sc->live; sc->cur_rec = -1; sc->combo_base += (long)sc->live; sc->live = 0.0f; sc->in_grind = 0; }     /* a trick that was never reported as over: bank what it had */
}

/* a trick that lasts (a grind, a manual, a grab) starts: it opens a combo or joins the one in progress */
static void trick_started(Score *sc, int cls, int type, int kind)
{
    combo_open(sc);
    sc->combo_n++;
    sc->in_grind = 1;
    sc->cur_cls = cls; sc->cur_type = type; sc->cur_kind = kind; sc->cur_bs = -1; sc->cur_nollie = 0; sc->cur_deg = 0;
    sc->live = (float)trick_def(cls, type)->base;
    sc->roll_t = 0.0f;
    sc->cur_rec = log_add(sc, cls, type, -1, 0, 0, 0, (long)sc->live);
}

/* a trick that is over the moment it is done (a flip, a spin, a revert): its points go straight into the combo */
static void trick_instant(Score *sc, int cls, int type, long pts, int nollie, int deg)
{
    combo_open(sc);
    sc->combo_n++;
    sc->combo_base += pts;
    sc->cur_cls = cls; sc->cur_type = type; sc->cur_nollie = nollie; sc->cur_deg = deg; sc->cur_bs = -1;
    sc->roll_t = 0.0f;
    log_add(sc, cls, type, -1, nollie, deg, 0, pts);
}

/* a plain ollie: part of the line (the goals look for it) but no points and no multiplier */
static void trick_ollie(Score *sc, int nollie)
{
    combo_open(sc);
    sc->roll_t = 0.0f;
    log_add(sc, TRICK_FLIP, TREC_OLLIE, -1, nollie, 0, 0, 0);
}

static void trick_ended(Score *sc)
{
    if (!sc->in_grind) return;
    if (sc->cur_rec >= 0 && sc->cur_rec < sc->log_n) sc->log[sc->cur_rec].pts = (long)sc->live;
    sc->cur_rec = -1;
    sc->combo_base += (long)sc->live;
    sc->live = 0.0f;
    sc->in_grind = 0;
    sc->roll_t = 0.0f;
}

void score_update(Score *sc, Skate *s, float dt)
{
    int air = !s->grounded && !s->grinding;
    if (air && !sc->prev_air) sc->flight++;
    sc->prev_air = air;
    sc->pos[0] = s->pos[0]; sc->pos[1] = s->pos[1]; sc->pos[2] = s->pos[2];
    if (sc->msg_t > 0.0f) { sc->msg_t -= dt; if (sc->msg_t <= 0.0f) sc->msg = SCORE_MSG_NONE; }
    if (s->ev_bail) {
        if (sc->combo_active) {                          /* the combo is lost, tricks and all */
            sc->msg = SCORE_MSG_BAIL; sc->msg_t = SCORE_MSG_TIME;
            sc->msg_pts = score_combo_points(sc); sc->msg_n = sc->combo_n; sc->msg_mult = score_combo_mult(sc);
        }
        score_cancel(sc);
        s->ev_grind_start = s->ev_grind_end = s->ev_bail = 0; s->ev_grind_switch = 0;
        s->ev_manual_start = s->ev_manual_end = 0;
        s->ev_grab_start = s->ev_grab_end = 0; s->ev_flip_done = 0;
        s->ev_grab_switch = 0; s->ev_spin = s->ev_revert = 0; s->ev_slide_start = s->ev_slide_end = 0; s->ev_wall_start = s->ev_wall_end = 0;
        s->ev_ollie_plain = s->ev_ollie_nollie = 0;
        return;
    }
    /* ends of a trick that began earlier come first, then the starts (a trick that both starts and ends within one call is over) */
    if (s->ev_grind_end && !s->ev_grind_start) trick_ended(sc);
    if (s->ev_manual_end && !s->ev_manual_start) trick_ended(sc);
    if (s->ev_grab_end && !s->ev_grab_start) trick_ended(sc);
    if (s->ev_slide_end && !s->ev_slide_start) trick_ended(sc);
    if (s->ev_wall_end && !s->ev_wall_start) trick_ended(sc);
    if (s->ev_manual_start) {
        trick_started(sc, TRICK_MANUAL, s->manual > 0 ? s->manual - 1 : 0, 0);
        if (s->ev_manual_end) trick_ended(sc);
    }
    if (s->ev_grab_start) {
        trick_started(sc, TRICK_GRAB, s->ev_grab_type, 0);
        if (s->ev_grab_end) trick_ended(sc);
    }
    if (s->ev_grab_switch && !s->ev_grab_start) {        /* the variant of a grab that is held changed: the first one is over, the new one counts */
        trick_ended(sc);
        trick_started(sc, TRICK_GRAB, s->ev_grab_type, 0);
    }
    if (s->ev_ollie_plain) trick_ollie(sc, s->ev_ollie_nollie);
    if (s->ev_flip_done) {
        for (int i = 0; i < s->ev_flip_done; i++) trick_instant(sc, TRICK_FLIP, s->ev_flip_type, (long)trick_def(TRICK_FLIP, s->ev_flip_type)->base, s->ev_flip_nollie, 0);
    }
    if (s->ev_spin) {                                    /* a landed spin: BS is a turn to the left; started fakie a half turn is a half-cab and a full turn a cab */
        int bs = (s->ev_spin_dir > 0) != (sc->regular != 0), deg = s->ev_spin_deg;
        int type = bs ? MOVE_BS_SPIN : MOVE_FS_SPIN;
        if (s->ev_spin_fakie) type = deg <= 180 ? (bs ? MOVE_BS_HALFCAB : MOVE_FS_HALFCAB) : (bs ? MOVE_BS_CAB : MOVE_FS_CAB);
        long pts = (long)trick_def(TRICK_MOVE, type)->base * (deg / 180 > 0 ? deg / 180 : 1);
        trick_instant(sc, TRICK_MOVE, type, pts, 0, deg);
    }
    if (s->ev_revert) {
        int rt = (s->ev_revert > 0) != (sc->regular != 0) ? MOVE_BS_REVERT : MOVE_FS_REVERT;
        trick_instant(sc, TRICK_MOVE, rt, (long)trick_def(TRICK_MOVE, rt)->base, 0, 0);
    }
    if (s->ev_slide_start) {
        trick_started(sc, TRICK_MOVE, (s->slide_dir > 0.0f) != (sc->regular != 0) ? MOVE_BS_POWERSLIDE : MOVE_FS_POWERSLIDE, 0);
        if (s->ev_slide_end) trick_ended(sc);
    }
    if (s->ev_wall_start) {                              /* the wall on the right = back to the wall = backside (for the goofy default) */
        trick_started(sc, TRICK_MOVE, (s->wall_side > 0.0f) != (sc->regular != 0) ? MOVE_FS_WALLRIDE : MOVE_BS_WALLRIDE, 0);
        if (s->ev_wall_end) trick_ended(sc);
    }
    if (s->ev_grind_start || (s->ev_grind_switch && !s->ev_grind_end)) {       /* a grind starts - or changes to another type on the same rail: the first one is over, the new one counts */
        if (s->ev_grind_switch && !s->ev_grind_start) trick_ended(sc);
        trick_started(sc, TRICK_GRIND, s->grind_type, s->ev_grind_kind);
        int gbs = sc->regular ? !s->grind_bs : s->grind_bs;                 /* (the rail is on the same side of the world, the rider's back is not) */
        sc->cur_bs = gbs;
        if (sc->cur_rec >= 0) { sc->log[sc->cur_rec].bs = (signed char)gbs; sc->log[sc->cur_rec].rail = (unsigned short)(s->rail + 1); }
        if (s->ev_grind_end) trick_ended(sc);
    }
    s->ev_grind_start = s->ev_grind_end = 0; s->ev_grind_switch = 0;
    s->ev_manual_start = s->ev_manual_end = 0;
    s->ev_grab_start = s->ev_grab_end = 0;
    s->ev_flip_done = 0;
    s->ev_ollie_plain = s->ev_ollie_nollie = 0;
    s->ev_grab_switch = 0; s->ev_spin = 0; s->ev_revert = 0; s->ev_slide_start = s->ev_slide_end = 0; s->ev_wall_start = s->ev_wall_end = 0;
    if (sc->in_grind && (s->grinding || s->manual || s->grab || s->sliding || s->wallriding)) {
        const GrindType *g = trick_def(sc->cur_cls, sc->cur_type);
        sc->live += (float)g->rate * dt;
        if (sc->cur_rec >= 0 && sc->cur_rec < sc->log_n) sc->log[sc->cur_rec].dur += dt;
    }
    if (sc->combo_active && !sc->in_grind) {
        if (s->grounded) {
            sc->roll_t += dt;
            if (sc->roll_t >= SCORE_BANK_TIME) {         /* landed it: bank */
                long pts = sc->combo_n > 0 ? score_combo_points(sc) : 0;
                for (int i = 0; i < sc->log_n; i++) sc->bank_log[i] = sc->log[i];
                sc->bank_n = sc->log_n; sc->bank_pts = pts; sc->bank_mult = score_combo_mult(sc); sc->bank_serial++;
                if (sc->combo_n > 0) {                   /* (a combo of nothing but ollies counts for the goals only) */
                    sc->total += pts;
                    sc->ev_bank = 1; sc->ev_bank_pts = pts; sc->ev_bank_mult = score_combo_mult(sc);
                    sc->msg = SCORE_MSG_BANK; sc->msg_t = SCORE_MSG_TIME; sc->msg_pts = pts; sc->msg_n = sc->combo_n; sc->msg_mult = score_combo_mult(sc);
                }
                score_cancel(sc);
            }
        } else {
            sc->roll_t = 0.0f;                           /* still in the air: the combo goes on */
        }
    }
}
