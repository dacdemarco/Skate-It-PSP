#include "goals.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- our tricks <-> the original's trick table (docs/CAMPAIGN_FORMAT.md section 7; the names match one for one, tests/test_goals.c checks them against the table) ---------------------- */
static const signed char FLIP_ID[NFLIP_TYPES] = { 13, 14, 1, 2, 3, 4, 5, 6, 8, 7, 10, 9, 12, 11, -1, -1, 31, 30, 34, 35, 33, 32 };
static const signed char GRIND_ID[NGRIND_TYPES] = { 36, 48, 38, 58, 60, 64, 62, 54, 56, 40, 42, 44, 46, 50, 66, 68 };      /* the BS id; FS is one more */
static const signed char GRAB_ID[NGRAB_TYPES] = { 70, 76, 71, 77, 80, 79, 72, 73, 89, 90, 92, 91, 93, 94, 96, 95, 85, 87, 86, 88, 82, 84, 83, 97, 98, 75, 74, 81, 78 };

enum { CLS_FLIP = 0x1, CLS_ADV_FLIP = 0x4, CLS_GRIND = 0x8, CLS_MANUAL = 0x10, CLS_GRAB = 0x20, CLS_FRONT = 0x80, CLS_BACK = 0x100, CLS_ADV_GRAB = 0x200, CLS_ADV_GRIND = 0x400, CLS_ANY = 0x800 };
#define GRIND_CLASSES (CLS_GRIND | CLS_FRONT | CLS_BACK | CLS_ADV_GRIND)

int goals_trick_id(const TrickRec *r)
{
    switch (r->cls) {
    case TRICK_FLIP:
        if (r->type == TREC_OLLIE) return r->nollie ? 15 : 0;
        if (r->type < 0 || r->type >= NFLIP_TYPES || FLIP_ID[r->type] < 0) return -1;
        return FLIP_ID[r->type] + (r->nollie && FLIP_ID[r->type] <= 14 ? 15 : 0);
    case TRICK_GRIND:
        if (r->type < 0 || r->type >= NGRIND_TYPES) return -1;
        return GRIND_ID[r->type] + (r->bs == 0 ? 1 : 0);
    case TRICK_GRAB:
        return r->type >= 0 && r->type < NGRAB_TYPES ? GRAB_ID[r->type] : -1;
    case TRICK_MANUAL:
        return r->type == 1 ? 99 : 100;
    default:
        return -1;
    }
}

unsigned goals_trick_classes(const TrickRec *r)
{
    unsigned c = 0;
    switch (r->cls) {
    case TRICK_FLIP:
        if (r->type == TREC_OLLIE) return 0;                      /* a plain ollie is not a flip trick (it is only matched by its id) */
        c = CLS_FLIP;
        if (r->type >= FLIP_VARIAL_KICK && r->type <= FLIP_360_INWARD) c |= CLS_ADV_FLIP;
        break;
    case TRICK_GRIND:
        c = CLS_GRIND;
        if (r->type == GRIND_5050 || r->type == GRIND_BOARDSLIDE || r->type == GRIND_LIPSLIDE) break;       /* the basic grinds */
        c |= CLS_ADV_GRIND;
        switch (r->type) {                                         /* which truck carries the weight: the pose families of tools/grind_classify.py */
        case GRIND_NOSEGRIND: case GRIND_CROOKED: case GRIND_OVERCROOKS: case GRIND_WILLY: case GRIND_OVERWILLY: case GRIND_NOSESLIDE: case GRIND_NOSEBLUNT: c |= CLS_FRONT; break;
        default: c |= CLS_BACK; break;
        }
        break;
    case TRICK_GRAB:
        c = CLS_GRAB;
        switch (r->type) {
        case GRAB_BS_GRAB: case GRAB_FS_GRAB: case GRAB_MELON: case GRAB_STALE: case GRAB_MUTE: case GRAB_NOSE: case GRAB_TAIL: case GRAB_DOUBLE: break;
        default: c |= CLS_ADV_GRAB; break;
        }
        break;
    case TRICK_MANUAL:
        c = CLS_MANUAL;
        break;
    default:
        return 0;                                                  /* spins, reverts, powerslides, wallrides are not tricks of the table */
    }
    return c | CLS_ANY;
}

/* ---- helpers ------------------------------------------------------------------------------------ */
static int is_spin(const TrickRec *r) { return r->cls == TRICK_MOVE && r->type >= MOVE_BS_SPIN && r->type <= MOVE_FS_CAB; }

static void box_from(GoalBox *b, const float a[3], const float c[3])
{
    for (int i = 0; i < 3; i++) { b->lo[i] = a[i] < c[i] ? a[i] : c[i]; b->hi[i] = a[i] < c[i] ? c[i] : a[i]; }
}

/* a point is "in" a zone when it is within 1 m of it sideways and between 1 m under and 2.5 m over it (zones are thin boxes drawn round a gap or a spot) */
static int in_zone(const GoalBox *b, const float p[3], float side)
{
    return p[0] >= b->lo[0] - side && p[0] <= b->hi[0] + side && p[2] >= b->lo[2] - side && p[2] <= b->hi[2] + side && p[1] >= b->lo[1] - 1.0f && p[1] <= b->hi[1] + 2.5f;
}

static GoalFlight *flight_get(Goals *g, int flight, int create)
{
    for (int i = 0; i < GOAL_MAX_FLIGHTS; i++)
        if (g->fl[i].flight == flight && flight != 0) return &g->fl[i];
    if (!create) return NULL;
    GoalFlight *f = &g->fl[g->nfl % GOAL_MAX_FLIGHTS];
    g->nfl++;
    memset(f, 0, sizeof(*f));
    f->flight = flight;
    return f;
}

static int rail_line(const Goals *g, const TrickRec *r)
{
    if (r->cls != TRICK_GRIND || r->rail == 0 || !g->rail_ids || r->rail > g->nrails) return 0;
    return g->rail_ids[r->rail - 1] == 0x8000 ? 0 : g->rail_ids[r->rail - 1];
}

/* does the objective ask for grinds only (then rail[] numbers are grind lines) */
static int grind_only(const CareerObj *o)
{
    if (o->a0 & OBJ_CLASS_MASK) {
        unsigned m = o->a0 & ~OBJ_CLASS_MASK;
        return m != 0 && (m & ~GRIND_CLASSES) == 0;
    }
    return o->a0 >= 36 && o->a0 <= 69;
}

static int zone_ok(const Goals *g, int z) { return z > 0 && z <= GOAL_MAX_ZONES && (g->zone_mask >> z & 1u); }

/* modifier bits that look at the neighbours in the line. 0x10: the trick leads INTO a manual (the next trick of the line is a manual); 0x80: it comes OUT of one (the trick before it is a manual).
 * Evidence: the tutorial "Manuals" reads "Fliptrick into Manual" (mod 0x10) and "..and then flip trick out" (mod 0x80, its NIS text "to fliptrick out of manual"); the Film challenges
 * "Flippin' Mannies" / "Tapas!" (3 advanced flips, mod 0x10) and Lakeside's "manual, flip, nose manual" flip (0x10) say the same. The other bits (0x20, 0x40) are not read. */
#define MOD_INTO_MANUAL 0x10u
#define MOD_OUT_OF_MANUAL 0x80u

static unsigned mod_of(const CareerObj *o) { return o->mod == 0xFFFFu ? 0u : o->mod; }       /* 0xFFFF = no modifier */

static int manual_ctx_ok(unsigned mod, const TrickRec *L, int k, int n)
{
    if (mod & MOD_INTO_MANUAL) {
        int j = k + 1;
        while (j < n && L[j].cls == TRICK_MOVE) j++;                  /* (spins, reverts ... are not tricks of the line) */
        if (j >= n || L[j].cls != TRICK_MANUAL) return 0;
    }
    if (mod & MOD_OUT_OF_MANUAL) {
        int j = k - 1;
        while (j >= 0 && L[j].cls == TRICK_MOVE) j--;
        if (j < 0 || L[j].cls != TRICK_MANUAL) return 0;
    }
    return 1;
}

static int rec_matches(const Goals *g, const CareerObj *o, const TrickRec *L, int k, int n, int combo_spin)
{
    const TrickRec *r = &L[k];
    int id = goals_trick_id(r);
    unsigned mod = mod_of(o);
    if (o->a0 & OBJ_CLASS_MASK) {
        if (!(goals_trick_classes(r) & (o->a0 & ~OBJ_CLASS_MASK))) return 0;
    } else if (id != (int)o->a0) {
        return 0;
    }
    if (o->mod == OBJ_MOD_SPIN && combo_spin < (int)o->a1) return 0;
    if (o->mod != OBJ_MOD_SPIN && !manual_ctx_ok(mod, L, k, n)) return 0;
    if (o->rail[0] || o->rail[1]) {
        if (grind_only(o)) {                                       /* on one of the listed grind lines */
            int l = rail_line(g, r);
            if (l == 0 || (l != o->rail[0] && l != o->rail[1])) return 0;
        } else if (g->zone_mask) {                                 /* over / landing in one of the listed zones */
            GoalFlight *f = flight_get((Goals *)g, r->flight, 0);
            if (!f) return 0;
            unsigned bit0 = zone_ok(g, o->rail[0]) ? 1u << o->rail[0] : 0, bit1 = zone_ok(g, o->rail[1]) ? 1u << o->rail[1] : 0;
            unsigned want = bit0 | bit1;
            if (want && (mod & 8) && !(f->landed & want)) return 0;
            if (want && !(mod & 8) && (mod & 1) && !(f->passed & want)) return 0;
        }
    } else if ((mod & 1) && g->zone_mask) {                      /* "over the picnic table": the one zone of the challenge */
        GoalFlight *f = flight_get((Goals *)g, r->flight, 0);
        int z = 1;
        while (z <= GOAL_MAX_ZONES && !zone_ok(g, z)) z++;
        if (!f || z > GOAL_MAX_ZONES || !(f->passed >> z & 1u)) return 0;
    }
    return 1;
}

static int same_spec(const CareerObj *a, const CareerObj *b)
{
    if ((mod_of(a) | mod_of(b)) & (MOD_INTO_MANUAL | MOD_OUT_OF_MANUAL)) return 0;      /* "into a manual" and "out of a manual" are two tricks */
    return a->type == OBJ_TRICK && b->type == OBJ_TRICK && a->a0 == b->a0 && (a->mod == OBJ_MOD_SPIN) == (b->mod == OBJ_MOD_SPIN);
}

static int need_of(const CareerObj *o)
{
    switch (o->type) {
    case OBJ_TRICK: return o->mod == OBJ_MOD_SPIN ? 1 : (o->a1 > 0 ? (int)o->a1 : 1);
    case OBJ_LINE: case OBJ_OWN_THE_SPOT: return 1;
    default: return (int)o->a0;
    }
}

/* ---- setup -------------------------------------------------------------------------------------- */
void goals_init(Goals *g, const Career *c, const LevelObjs *objs, const uint16_t *rail_ids, uint32_t nrails)
{
    memset(g, 0, sizeof(*g));
    g->career = c; g->objs = objs; g->rail_ids = rail_ids; g->nrails = nrails;
    g->phase = GOAL_IDLE;
    g->chal = -1;
}

static int find_obj(const Goals *g, const CareerChal *ch, const char *suffix, float p[3])
{
    char name[48];
    snprintf(name, sizeof(name), "chal%03d%s", ch->slot, suffix);
    return levelobj_find(g->objs, name, p);
}

int goals_supported(const Goals *g, int chal)
{
    if (!g->career || !career_chal_runnable(g->career, chal)) return 0;
    int mode = g->career->chal[chal].mode;
    if (mode == MODE_CHECKPOINT || mode == MODE_DEATHRACE) return g->career->chal[chal].nobj <= RACE_MAX_GATES;
    if (mode == MODE_JAM || mode == MODE_SKATE || mode == MODE_BESTTRICK) return g->career->chal[chal].nobj <= CONTEST_MAX_ROUNDS + 3;
    return g->career->chal[chal].nobj <= GOAL_MAX_OBJ;              /* (the beam is the caller's business: tutorials have none) */
}

void goals_set_recordings(Goals *g, const GhostPak *ai, const char *level)
{
    g->ai = ai;
    snprintf(g->level, sizeof(g->level), "%s", level ? level : "");
}

int goals_begin(Goals *g, int chal, const Score *sc)
{
    if (!goals_supported(g, chal)) return 0;
    const CareerChal *ch = &g->career->chal[chal];
    const Career *career = g->career; const LevelObjs *objs = g->objs; const uint16_t *ids = g->rail_ids; uint32_t nr = g->nrails;
    const GhostPak *ai = g->ai;
    char level[32];
    memcpy(level, g->level, sizeof(level));
    goals_init(g, career, objs, ids, nr);
    g->ai = ai;
    memcpy(g->level, level, sizeof(level));
    g->ch = ch; g->obj = career_objs(career, ch); g->nobj = ch->nobj; g->chal = chal;
    if (ch->mode == MODE_CHECKPOINT || ch->mode == MODE_DEATHRACE) {                    /* a race: the gates, the course and the opponents; no objective list */
        if (!race_setup(&g->race, career, chal, objs)) { g->phase = GOAL_IDLE; return 0; }
        g->is_race = 1;
        g->nobj = 0;
        if (g->ai && g->level[0]) race_use_recordings(&g->race, career, chal, g->ai, g->level);       /* (the recorded paths when the data has them) */
    }
    if (ch->mode == MODE_JAM || ch->mode == MODE_SKATE || ch->mode == MODE_BESTTRICK) {          /* a score contest against recorded opponents: no objective list either */
        if (!contest_setup(&g->contest, career, chal, objs)) { g->phase = GOAL_IDLE; return 0; }
        g->is_contest = 1;
        g->nobj = 0;
    }
    g->have_beam = find_obj(g, ch, "_chalstart", g->beam);
    float d[3], a[3], b[3];
    g->have_start = find_obj(g, ch, "_start_pos_1", g->start_pos);
    if (g->have_start && find_obj(g, ch, "_start_dir_1", d)) g->start_heading = atan2f(d[0] - g->start_pos[0], d[2] - g->start_pos[2]);
    g->have_area = find_obj(g, ch, "_boundinner_a", a) && find_obj(g, ch, "_boundinner_b", b);
    if (g->have_area) box_from(&g->area, a, b);
    for (int z = 1; z <= GOAL_MAX_ZONES; z++) {
        char sa[24], sb[24];
        snprintf(sa, sizeof(sa), "_zone_%d_a", z); snprintf(sb, sizeof(sb), "_zone_%d_b", z);
        if (find_obj(g, ch, sa, a) && find_obj(g, ch, sb, b)) { box_from(&g->zone[z], a, b); g->zone_mask |= 1u << z; g->nzones = z; }
    }
    for (int i = 0; i < g->nobj; i++) {
        g->need[i] = need_of(&g->obj[i]);
        if (g->obj[i].type == OBJ_OWN_THE_SPOT) {                   /* "Beat %s points": the value is somewhere in the original's range [guess: half way, in steps of 25] */
            g->own_target = career_own_target(&g->obj[i]);
            g->need[i] = (int)g->own_target;
        }
    }
    g->phase = GOAL_COUNTDOWN_PHASE;
    g->countdown = GOAL_COUNTDOWN_TIME;
    g->timed = ch->time > 0;
    g->time_left = g->timed ? (float)ch->time : 0.0f;
    g->base_total = sc->total;
    g->seen_bank = sc->bank_serial;
    return 1;
}

void goals_abort(Goals *g) { g->phase = GOAL_IDLE; }
int goals_active(const Goals *g) { return g->phase == GOAL_COUNTDOWN_PHASE || g->phase == GOAL_ACTIVE; }

/* ---- the rules ---------------------------------------------------------------------------------- */
static int ots_ok(const Goals *g, const CareerObj *o, const TrickRec *L, int n)
{
    int z0 = o->rail[0], z1 = o->rail[1];
    for (int i = 0; i < n; i++) {
        const TrickRec *r = &L[i];
        if (r->cls == TRICK_FLIP && r->type == TREC_OLLIE) continue;
        if (g->zone_mask && (z0 || z1)) {
            if ((zone_ok(g, z0) && in_zone(&g->zone[z0], r->pos, 1.0f)) || (zone_ok(g, z1) && in_zone(&g->zone[z1], r->pos, 1.0f))) return 1;
        } else if (z0 || z1) {
            int l = rail_line(g, r);
            if (l && (l == z0 || l == z1)) return 1;
        } else if (g->have_area) {
            if (in_zone(&g->area, r->pos, 3.0f)) return 1;
        } else if (g->have_beam) {
            float dx = r->pos[0] - g->beam[0], dz = r->pos[2] - g->beam[2];
            if (dx * dx + dz * dz < 30.0f * 30.0f) return 1;
        } else {
            return 1;
        }
    }
    return 0;
}

/* the whole objective list done by this one line? (LINE challenges) */
static int line_done(const Goals *g, const TrickRec *L, int n, long pts, int combo_spin, int ntricks)
{
    int pos = 0, last = -1;
    for (int i = 0; i < g->nobj; i++) {
        const CareerObj *o = &g->obj[i];
        switch (o->type) {
        case OBJ_TRICK:
            if (i > 0 && same_spec(&g->obj[i - 1], o) && last >= 0) {         /* the same trick again with another condition: it is the same trick */
                if (!rec_matches(g, o, L, last, n, combo_spin)) return 0;
            } else {
                int need = need_of(o), cnt = 0, found = 0;
                for (int k = pos; k < n && !found; k++)
                    if (rec_matches(g, o, L, k, n, combo_spin)) { cnt++; last = k; if (cnt >= need) { pos = k + 1; found = 1; } }
                if (!found) return 0;
            }
            break;
        case OBJ_SCORE: if (pts < (long)o->a0) return 0; break;
        case OBJ_LINE_SCORE: if (pts < (long)o->a0 || ntricks < 2) return 0; break;       /* a line is two tricks or more (the multiplier lights up at the second) */
        case OBJ_SPIN: if (combo_spin < (int)o->a0) return 0; break;
        case OBJ_AIR_TIME: if (g->air_total < (float)o->a0) return 0; break;
        case OBJ_LINE: if (ntricks < 2) return 0; break;
        case OBJ_OWN_THE_SPOT: if (pts < g->own_target || !ots_ok(g, o, L, n)) return 0; break;
        default: return 0;
        }
    }
    return 1;
}

static void process_bank(Goals *g, const Score *sc)
{
    const TrickRec *L = sc->bank_log;
    int n = sc->bank_n, combo_spin = 0, ntricks = 0;
    long pts = sc->bank_pts;
    for (int i = 0; i < n; i++) {
        if (is_spin(&L[i])) combo_spin += L[i].deg > 0 ? L[i].deg : 180;
        else if (goals_trick_classes(&L[i])) ntricks++;
    }
    g->total = sc->total - g->base_total;
    if (pts > g->best_line) g->best_line = pts;
    g->spin_total += combo_spin;
    if (g->ch->flags & CHAL_LINE) {
        if (line_done(g, L, n, pts, combo_spin, ntricks))
            for (int i = 0; i < g->nobj; i++) { g->done[i] = 1; g->prog[i] = g->need[i]; }
        return;
    }
    for (int i = 0; i < g->nobj; i++) {
        const CareerObj *o = &g->obj[i];
        if (g->done[i]) continue;
        switch (o->type) {
        case OBJ_TRICK: {
            int cnt = 0;
            if (o->a0 == OBJ_TRICK_PUSH) break;                     /* pushes are counted as they happen */
            for (int k = 0; k < n; k++) cnt += rec_matches(g, o, L, k, n, combo_spin);
            g->prog[i] += cnt;
            break;
        }
        case OBJ_SCORE: g->prog[i] = (o->flags & OBJF_LINE_ONLY) ? (int)g->best_line : (int)g->total; break;
        case OBJ_LINE_SCORE: if (ntricks >= 2 && pts > g->prog[i]) g->prog[i] = (int)pts; break;      /* only lines of two tricks or more: one big trick is no line */
        case OBJ_SPIN: g->prog[i] = g->spin_total; break;
        case OBJ_LINE: if (ntricks >= 2) g->prog[i] = 1; break;
        case OBJ_OWN_THE_SPOT:
            if (ots_ok(g, o, L, n)) {                               /* only lines on the spot count (the HUD shows the best of them, never more than the target until it is beaten) */
                if (pts >= g->own_target) g->prog[i] = (int)g->own_target;
                else if (pts > g->prog[i]) g->prog[i] = (int)pts;
            }
            break;
        default: break;
        }
        if (g->prog[i] >= g->need[i]) g->done[i] = 1;
    }
}

void goals_update(Goals *g, const GoalIn *in)
{
    if (g->phase == GOAL_WON || g->phase == GOAL_LOST) { g->result_t += in->dt; return; }
    if (g->phase == GOAL_COUNTDOWN_PHASE) {
        g->countdown -= in->dt;
        if (g->is_race) race_preview(&g->race, in->ground, in->ground_ctx);        /* the opponents stand on the grid while 3 - 2 - 1 runs */
        if (g->countdown <= 0.0f) {
            if (g->is_contest) { g->contest.seen_bank = in->score->bank_serial; contest_start(&g->contest); g->time_left = g->contest.limit; }
            g->phase = GOAL_ACTIVE;
            g->countdown = 0.0f;
            g->seen_bank = in->score->bank_serial;                  /* nothing done before the GO counts */
            g->base_total = in->score->total;
        }
        return;
    }
    if (g->phase != GOAL_ACTIVE) return;
    const Score *sc = in->score;
    if (g->is_contest) {                                                   /* a contest: the rules are in contest.c, the clock shows what is left of the round / turn / Jam */
        ContestIn ci;
        memset(&ci, 0, sizeof(ci));
        ci.dt = in->dt; ci.bails = in->bails; ci.score = sc; memcpy(ci.pos, in->pos, sizeof(ci.pos));
        g->t += in->dt;
        int r = contest_update(&g->contest, &ci);
        g->time_left = g->contest.limit - g->contest.t;
        if (g->time_left < 0.0f) g->time_left = 0.0f;
        if (r != CT_RUNNING) { g->phase = r == CT_WON ? GOAL_WON : GOAL_LOST; g->result_t = 0.0f; }
        return;
    }
    if (g->is_race) {                                                      /* a race: the clock counts down, bails and gates move it, the finish line decides */
        g->t += in->dt;
        g->time_left -= in->dt;
        for (int b = 0; b < in->bails; b++) { race_bail(&g->race); g->time_left -= RACE_BAIL_PENALTY; }
        int ev = race_update(&g->race, in->dt, in->pos, in->ground, in->ground_ctx);
        g->time_left += g->race.last_bonus;
        g->prog[0] = g->race.next;
        if (ev & RACE_EV_FINISH) {
            g->phase = (g->time_left > 0.0f && g->race.place <= RACE_WIN_PLACE) ? GOAL_WON : GOAL_LOST;
            g->result_t = 0.0f;
        } else if (g->time_left <= 0.0f) {
            g->phase = GOAL_LOST;
            g->result_t = 0.0f;
        }
        return;
    }
    g->t += in->dt;
    if (g->timed && g->time_left > 0.0f) g->time_left -= in->dt;

    /* the jumps: which zones the rider flew over, where each one landed, and how long they lasted */
    int air = !in->grounded && !in->grinding;
    if (air && !g->prev_air) { GoalFlight *f = flight_get(g, sc->flight, 1); (void)f; g->air_peak = 0.0f; }
    if (air) {
        GoalFlight *f = flight_get(g, sc->flight, 1);
        for (int z = 1; z <= GOAL_MAX_ZONES; z++)
            if (zone_ok(g, z) && in_zone(&g->zone[z], in->pos, 1.0f)) f->passed |= 1u << z;
        if (in->air_t > g->air_peak) g->air_peak = in->air_t;
    } else if (g->prev_air) {
        GoalFlight *f = flight_get(g, sc->flight, 1);
        for (int z = 1; z <= GOAL_MAX_ZONES; z++)
            if (zone_ok(g, z) && in_zone(&g->zone[z], in->pos, 1.0f)) f->landed |= 1u << z;
        f->ended = 1;
        g->air_total += g->air_peak;
        g->air_peak = 0.0f;
    }
    g->prev_air = air;
    g->pushes_total += in->pushes;
    g->live_line = sc->combo_active ? score_combo_points(sc) : 0;

    if (sc->bank_serial != g->seen_bank) { g->seen_bank = sc->bank_serial; process_bank(g, sc); }

    /* what is checked every frame rather than at a bank: air time and pushes */
    for (int i = 0; i < g->nobj; i++) {
        const CareerObj *o = &g->obj[i];
        if (g->done[i]) continue;
        if (o->type == OBJ_AIR_TIME && !(g->ch->flags & CHAL_LINE)) { g->prog[i] = (int)(g->air_total + (air ? g->air_peak : 0.0f)); if (g->prog[i] >= g->need[i]) g->done[i] = 1; }
        if (o->type == OBJ_TRICK && o->a0 == OBJ_TRICK_PUSH) { g->prog[i] = g->pushes_total; if (g->prog[i] >= g->need[i]) g->done[i] = 1; }
    }
    int all = 1;
    for (int i = 0; i < g->nobj; i++) all &= g->done[i] != 0;
    if (all) { g->phase = GOAL_WON; g->result_t = 0.0f; return; }
    if (g->timed && g->time_left <= 0.0f) {                        /* time is up; a line in progress may still finish (3 s) */
        g->grace += in->dt;
        if (!sc->combo_active || g->grace > 3.0f) { g->phase = GOAL_LOST; g->result_t = 0.0f; }
    }
}

const char *goals_obj_text(const Goals *g, int i)
{
    static char buf[96];
    if (i < 0 || i >= g->nobj) return "";
    const CareerObj *o = &g->obj[i];
    if (o->type == OBJ_OWN_THE_SPOT) { snprintf(buf, sizeof(buf), "Beat %ld points", g->own_target); return buf; }
    return career_str(g->career, o->text_off);
}
