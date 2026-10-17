#include "stage.h"
#include "career.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static unsigned rnd(Stage *s) { s->rng = s->rng * 1664525u + 1013904223u; return s->rng >> 8; }

void stage_init(Stage *s, const GhostPak *pak, const char *level)
{
    memset(s, 0, sizeof(*s));
    s->pak = pak;
    if (level) snprintf(s->level, sizeof(s->level), "%s", level);
    for (int i = 0; i < STAGE_MAX; i++) { s->r[i].skater = -1; s->r[i].key = -1; }
}

void stage_clear(Stage *s)
{
    const GhostPak *pak = s->pak;
    char level[32];
    memcpy(level, s->level, sizeof(level));
    stage_init(s, pak, level);
}

float stage_last_bank_time(const GhostTrack *t)
{
    float last = 0.0f;
    for (int i = 0; i < t->nev; i++)
        if (t->ev[i].kind == GEV_BANK) last = (float)t->ev[i].frame / 60.0f;
    return last;
}

/* the moment a recording's trick happens: its last banked line, else its last flip, else the end */
static float anchor_time(const GhostTrack *t)
{
    float a = stage_last_bank_time(t);
    if (a > 0.0f) return a;
    for (int i = t->nev - 1; i >= 0; i--)
        if (t->ev[i].kind == GEV_FLIP) return (float)t->ev[i].frame / 60.0f;
    return t->dur;
}

static void place_standing(StageRider *r, GhostGroundFn gfn, void *gctx)
{
    ghost_stand(&r->gp, r->stand_pos, r->stand_heading, gfn, gctx);
    r->has_trk = 0;
}

static void play_from(StageRider *r, float time, GhostGroundFn gfn, void *gctx)
{
    ghost_init(&r->gp, &r->trk, 1.0f, time);
    ghost_update(&r->gp, 0.0f, gfn, gctx);
    r->has_trk = 1;
}

/* follow the contest clock: the recording is where it should be at `target` seconds (small differences are walked, big ones jump) */
static void sync_to(StageRider *r, float target, GhostGroundFn gfn, void *gctx)
{
    if (!r->has_trk) return;
    float d = target - r->gp.time;
    if (d > 0.0f && d < 0.5f) ghost_update(&r->gp, d, gfn, gctx);
    else if (d < -0.2f || d >= 0.5f) play_from(r, clampf(target, 0.0f, r->trk.dur), gfn, gctx);
}

/* ---- contests ------------------------------------------------------------------------------------------------------------------------------------ */
void stage_contest_begin(Stage *s, const Contest *k, const LevelObjs *o, GhostGroundFn gfn, void *gctx)
{
    stage_clear(s);
    s->kind = 1;
    s->n = k->nopp < STAGE_MAX ? k->nopp : STAGE_MAX;
    int slot = k->career->chal[k->chal].slot;
    for (int j = 0; j < s->n; j++) {
        StageRider *r = &s->r[j];
        r->skater = k->opp_skater[j];
        float p[3] = { 0, 0, 0 }, d[3];
        char nm[24];
        snprintf(nm, sizeof(nm), "_start_pos_%d", j + 2);
        if (!levelobj_find_num(o, "chal", 3, slot, nm, p) && !levelobj_find_num(o, "chal", 3, slot, "_start_pos_1", p)) memset(p, 0, sizeof(p));
        memcpy(r->stand_pos, p, 12);
        snprintf(nm, sizeof(nm), "_start_dir_%d", j + 2);
        if (levelobj_find_num(o, "chal", 3, slot, nm, d)) r->stand_heading = atan2f(d[0] - p[0], d[2] - p[2]);
        if ((k->mode == MODE_JAM && ghost_find(s->pak, s->level, k->chal, 3, 0, j, &r->trk)) ||
            (k->mode == MODE_BESTTRICK && ghost_find(s->pak, s->level, k->chal, 5, 0, j, &r->trk))) {
            r->key = 0;
            play_from(r, 0.0f, gfn, gctx);
        } else {
            place_standing(r, gfn, gctx);
        }
    }
}

static int find_skate_track(Stage *s, const Contest *k, int a, int b, GhostTrack *t) { return ghost_find(s->pak, s->level, k->chal, 4, a, b, t); }

static void skate_staging(Stage *s, const Contest *k, StageRider *r, GhostGroundFn gfn, void *gctx)
{
    int want = -1;
    if (k->state == SK_OPP_SETS) {
        if (k->brk <= 0.0f) want = k->turn * 4 + 1;                                  /* his turn to set: he does the trick */
        else if (k->msg == CT_MSG_OPP_MATCHED) want = k->turn * 4 + 2;               /* he copies the player's trick: a clean one */
        else if (k->msg == CT_MSG_OPP_MISSED) want = k->turn * 4 + 3;                /* ... and misses it */
    }
    if (want < 0 || want == r->key) return;
    r->key = want;
    GhostTrack t;
    int ok = 0, code = want & 3;
    if (code == 1) ok = k->set_ai[k->turn % k->nset] >= 0 && find_skate_track(s, k, k->set_ai[k->turn % k->nset], 0, &t);
    else if (code == 2) ok = k->set_ai[(k->turn + 1) % k->nset] >= 0 && find_skate_track(s, k, k->set_ai[(k->turn + 1) % k->nset], 0, &t);
    else for (int q = 0; q < 4 && !ok; q++) ok = find_skate_track(s, k, 10, (k->turn + q) % 4, &t);        /* the failed attempts: recordings a = 10 */
    if (!ok) return;
    r->trk = t;
    play_from(r, clampf(anchor_time(&t) - STAGE_SKATE_LEAD, 0.0f, t.dur), gfn, gctx);
}

void stage_contest_update(Stage *s, const Contest *k, float dt, GhostGroundFn gfn, void *gctx)
{
    for (int j = 0; j < s->n; j++) {
        StageRider *r = &s->r[j];
        if (k->mode == MODE_JAM) {
            sync_to(r, k->t, gfn, gctx);
        } else if (k->mode == MODE_BESTTRICK) {
            int round = k->round < k->rounds ? k->round : k->rounds - 1;
            if (round != r->key) {                                                       /* the next round: the recording of that round, from its start */
                r->key = round;
                GhostTrack t;
                if (ghost_find(s->pak, s->level, k->chal, 5, round, j, &t)) { r->trk = t; play_from(r, 0.0f, gfn, gctx); }
                else place_standing(r, gfn, gctx);
            }
            sync_to(r, k->t, gfn, gctx);
        } else if (k->mode == MODE_SKATE) {
            skate_staging(s, k, r, gfn, gctx);
            if (r->has_trk && r->gp.t) ghost_update(&r->gp, dt, gfn, gctx);
        }
    }
}

/* ---- ambient skaters ----------------------------------------------------------------------------------------------------------------------------- */
static float dist2(const float a[3], const float b[3]) { return hypotf(a[0] - b[0], a[2] - b[2]); }

/* a route to start next: its first point at least KEEP_OFF from the player and from the other riders, and the nearest of those to `from` (he skates on from where he is when there is a route starting close
 * by - no popping out of nothing), else a random one that is far enough. -1 if none */
static int pick_route(Stage *s, int me, int avoid, const float from[3], const float player[3], GhostTrack *out)
{
    int best = -1;
    float best_d = 1e30f;
    unsigned start = rnd(s);
    for (int i = 0; i < s->nroutes; i++) {
        int k = (int)((start + (unsigned)i) % (unsigned)s->nroutes);
        GhostTrack t;
        if (k == avoid || !ghost_route(s->pak, s->level, k, &t) || t.dur < 4.0f) continue;
        if (dist2(t.p, player) < STAGE_AMBIENT_KEEP_OFF) continue;
        int clash = 0;
        for (int j = 0; j < s->n; j++) if (j != me && s->r[j].has_trk && dist2(t.p, s->r[j].gp.pos) < 6.0f) clash = 1;
        if (clash) continue;
        float d = (from ? dist2(t.p, from) : 0.0f) + (float)(rnd(s) & 1023) / 1024.0f * 20.0f;       /* (a little luck, so that the same few routes do not repeat) */
        if (d < best_d) { best_d = d; best = k; }
    }
    if (best < 0) return -1;
    ghost_route(s->pak, s->level, best, out);
    return best;
}

int stage_ambient_begin(Stage *s, const int *skaters, int n, unsigned seed, const float player[3], GhostGroundFn gfn, void *gctx)
{
    stage_clear(s);
    s->rng = seed * 2654435761u + 977u;
    s->nroutes = ghost_route_count(s->pak, s->level);
    if (s->nroutes <= 0) return 0;
    s->kind = 2;
    s->n = n < STAGE_AMBIENT_MAX ? n : STAGE_AMBIENT_MAX;
    int started = 0;
    for (int j = 0; j < s->n; j++) {
        StageRider *r = &s->r[j];
        r->skater = skaters[j];
        r->route = pick_route(s, j, -1, NULL, player, &r->trk);
        if (r->route < 0) { r->skater = -1; continue; }
        float at = (float)(rnd(s) & 1023) / 1024.0f * r->trk.dur * 0.6f;               /* not everybody at the start of a route */
        float p[3];
        ghost_pos_at(&r->trk, at, p);
        if (dist2(p, player) < STAGE_AMBIENT_KEEP_OFF) at = 0.0f;
        play_from(r, at, gfn, gctx);
        started++;
    }
    return started;
}

void stage_ambient_update(Stage *s, float dt, const float player[3], GhostGroundFn gfn, void *gctx)
{
    for (int j = 0; j < s->n; j++) {
        StageRider *r = &s->r[j];
        if (r->skater < 0 || !r->has_trk) continue;
        ghost_update(&r->gp, dt, gfn, gctx);
        if (r->gp.done) {                                                              /* the route is over: the next one, from where he stands (looked for once a second) */
            r->retry -= dt;
            if (r->retry > 0.0f) continue;
            r->retry = 1.0f;
            float here[3];
            memcpy(here, r->gp.pos, 12);
            GhostTrack t;
            int k = pick_route(s, j, r->route, here, player, &t);
            if (k >= 0) { r->route = k; r->trk = t; play_from(r, 0.0f, gfn, gctx); }
        }
    }
}
