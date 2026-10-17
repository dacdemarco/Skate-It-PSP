#include "race.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- small vector helpers ---------------------------------------------------------------------------------------------------------------------- */
static float dist2d(const float a[3], const float b[3]) { return hypotf(a[0] - b[0], a[2] - b[2]); }
static float dist3d(const float a[3], const float b[3]) { return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
static void mid3(const float a[3], const float b[3], float m[3]) { for (int i = 0; i < 3; i++) m[i] = (a[i] + b[i]) * 0.5f; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- paths ------------------------------------------------------------------------------------------------------------------------------------- */
static void path_add(RacePath *p, const float v[3])
{
    if (p->n >= RACE_MAX_PATH) return;
    if (p->n > 0 && dist3d(p->p[p->n - 1], v) < 0.5f) return;      /* (no zero-length pieces) */
    memcpy(p->p[p->n++], v, 12);
}

static void path_close(RacePath *p)
{
    p->cum[0] = 0.0f;
    for (int i = 1; i < p->n; i++) p->cum[i] = p->cum[i - 1] + dist3d(p->p[i - 1], p->p[i]);
    p->len = p->n > 0 ? p->cum[p->n - 1] : 0.0f;
}

void race_path_at(const RacePath *p, float s, float pos[3], float tan[3])
{
    if (p->n == 0) { pos[0] = pos[1] = pos[2] = 0.0f; tan[0] = 0.0f; tan[1] = 0.0f; tan[2] = 1.0f; return; }
    if (p->n == 1) { memcpy(pos, p->p[0], 12); tan[0] = 0.0f; tan[1] = 0.0f; tan[2] = 1.0f; return; }
    s = clampf(s, 0.0f, p->len);
    int i = 0;
    while (i < p->n - 2 && p->cum[i + 1] < s) i++;
    float seg = p->cum[i + 1] - p->cum[i], u = seg > 1e-6f ? (s - p->cum[i]) / seg : 0.0f;
    for (int k = 0; k < 3; k++) {
        pos[k] = p->p[i][k] + (p->p[i + 1][k] - p->p[i][k]) * u;
        tan[k] = seg > 1e-6f ? (p->p[i + 1][k] - p->p[i][k]) / seg : 0.0f;
    }
}

/* ---- the recorded runs: when the opponent is where ------------------------------------------------------------------------------------------------ */
static float opp_distance(const RaceOpp *o, float t)
{
    const float ta = RACE_ACCEL_TIME, v = o->cruise;
    float s = t <= ta ? v * t * t / (2.0f * ta) : v * (t - ta * 0.5f);
    if (t > o->finish_time) {                                           /* past the finish he rolls on and slows down over 3 s */
        float d = t - o->finish_time;
        if (d > 3.0f) d = 3.0f;
        s = o->path.len + v * d - v * d * d / 6.0f;
    }
    return s;
}

static float opp_speed(const RaceOpp *o, float t)
{
    const float ta = RACE_ACCEL_TIME, v = o->cruise;
    if (t > o->finish_time) { float d = t - o->finish_time; return d >= 3.0f ? 0.0f : v * (1.0f - d / 3.0f); }
    return t <= ta ? v * t / ta : v;
}

/* ---- setup ------------------------------------------------------------------------------------------------------------------------------------- */
static int find_slot(const LevelObjs *o, int slot, int k, float p[3])
{
    char name[40];
    snprintf(name, sizeof(name), "_start_pos_%d", k);
    return levelobj_find_num(o, "chal", 3, slot, name, p);
}

/* checkpoint_NNN_a / _b; DIY Park names its gates 9, 10 and 11 "checkpoint_9_a" ... without the zero padding */
static int find_gate_end(const LevelObjs *o, int number, const char *suffix, float p[3])
{
    return levelobj_find_num(o, "checkpoint_", 3, number, suffix, p) || levelobj_find_num(o, "checkpoint_", 1, number, suffix, p);
}

static int find_slot_dir(const LevelObjs *o, int slot, int k, float p[3])
{
    char name[40];
    snprintf(name, sizeof(name), "_start_dir_%d", k);
    return levelobj_find_num(o, "chal", 3, slot, name, p);
}

int race_setup(Race *r, const Career *c, int chal, const LevelObjs *o)
{
    memset(r, 0, sizeof(*r));
    if (chal < 0 || (uint32_t)chal >= c->nchal) return 0;
    const CareerChal *ch = &c->chal[chal];
    if (ch->mode != MODE_CHECKPOINT && ch->mode != MODE_DEATHRACE) return 0;
    if (ch->nobj == 0 || ch->nobj > RACE_MAX_GATES) return 0;
    r->mode = ch->mode;
    const CareerObj *ob = career_objs(c, ch);
    for (int i = 0; i < ch->nobj; i++) {
        if (ob[i].type != OBJ_CHECKPOINT) return 0;
        RaceGate *g = &r->gate[r->ngates];
        if (!find_gate_end(o, (int)ob[i].a0, "_a", g->a) || !find_gate_end(o, (int)ob[i].a0, "_b", g->b)) return 0;
        g->number = (int)ob[i].a0;
        g->finish = ob[i].a1 == 0xFFFFFFFFu;
        g->bonus = g->finish ? 0.0f : (float)ob[i].a1;
        r->ngates++;
    }
    r->gate[r->ngates - 1].finish = 1;                                   /* the last gate is the finish */
    float start[3];
    if (!find_slot(o, ch->slot, 1, start)) return 0;
    path_add(&r->course, start);
    float fmid[3];
    mid3(r->gate[r->ngates - 1].a, r->gate[r->ngates - 1].b, fmid);
    if (ch->mode == MODE_CHECKPOINT) {                                   /* through every gate square on: a point before it, its middle, a point after it */
        for (int i = 0; i < r->ngates; i++) {
            float m[3], nx[3], n[2], w;
            mid3(r->gate[i].a, r->gate[i].b, m);
            if (i + 1 < r->ngates) mid3(r->gate[i + 1].a, r->gate[i + 1].b, nx);
            else for (int k = 0; k < 3; k++) nx[k] = m[k] + (m[k] - r->course.p[r->course.n - 1][k]);
            n[0] = -(r->gate[i].b[2] - r->gate[i].a[2]); n[1] = r->gate[i].b[0] - r->gate[i].a[0];
            w = hypotf(n[0], n[1]);
            if (w < 1e-3f) { path_add(&r->course, m); continue; }
            n[0] /= w; n[1] /= w;
            if (n[0] * (nx[0] - r->course.p[r->course.n - 1][0]) + n[1] * (nx[2] - r->course.p[r->course.n - 1][2]) < 0.0f) { n[0] = -n[0]; n[1] = -n[1]; }   /* the way of travel across it */
            float before[3] = { m[0] - n[0] * 3.0f, m[1], m[2] - n[1] * 3.0f }, after[3] = { m[0] + n[0] * 3.0f, m[1], m[2] + n[1] * 3.0f };
            path_add(&r->course, before); path_add(&r->course, m); path_add(&r->course, after);
        }
    } else {                                                             /* the level's race line between the start and the finish gate */
        static float sp[RACE_MAX_PATH][3];
        int n = levelobj_path(o, "progress_spline", sp, RACE_MAX_PATH);
        if (n < 2 || n > RACE_MAX_PATH) return 0;
        int is = 0, ifin = 0;
        float bs = 1e30f, bf = 1e30f;
        for (int i = 0; i < n; i++) {
            float ds = dist3d(sp[i], start), df = dist3d(sp[i], fmid);
            if (ds < bs) { bs = ds; is = i; }
            if (df < bf) { bf = df; ifin = i; }
        }
        int step = ifin >= is ? 1 : -1;
        float sd[3], dirx = 0.0f, dirz = 0.0f;                           /* the way the rider faces at the start: spline points behind him or right under him are skipped */
        if (find_slot_dir(o, ch->slot, 1, sd)) { dirx = sd[0] - start[0]; dirz = sd[2] - start[2]; float dl = hypotf(dirx, dirz); if (dl > 1e-3f) { dirx /= dl; dirz /= dl; } }
        for (int i = is; i != ifin + step; i += step) {
            if (r->course.n == 1 && i != ifin && (dist2d(sp[i], start) < 6.0f || (sp[i][0] - start[0]) * dirx + (sp[i][2] - start[2]) * dirz < 0.0f)) continue;
            path_add(&r->course, sp[i]);
        }
        path_add(&r->course, fmid);
        {                                                                /* and on past the line, in the direction he came from */
            const float *pv = r->course.p[r->course.n > 1 ? r->course.n - 2 : 0];
            float dx = fmid[0] - pv[0], dz = fmid[2] - pv[2], l = hypotf(dx, dz);
            if (l > 0.1f) { float e[3] = { fmid[0] + dx / l * 4.0f, fmid[1], fmid[2] + dz / l * 4.0f }; path_add(&r->course, e); }
        }
    }
    path_close(&r->course);
    if (r->course.n < 2 || r->course.len < 5.0f) return 0;
    for (int j = 0; j < RACE_MAX_OPP; j++) {
        int samples = career_opponent_samples(c, chal, j);
        if (ch->opp[j] == 0xFE || samples <= 0) continue;
        RaceOpp *op = &r->opp[r->nopp];
        float st[3];
        if (!find_slot(o, ch->slot, j + 2, st)) memcpy(st, r->course.p[0], 12);
        op->skater = ch->opp[j];
        op->ai = j;
        path_add(&op->path, st);
        for (int i = 1; i < r->course.n; i++) path_add(&op->path, r->course.p[i]);
        path_close(&op->path);
        if (op->path.n < 2) continue;
        op->finish_time = (float)samples / (float)CAREER_AI_HZ * RACE_AI_FACTOR;
        float run = op->finish_time - RACE_ACCEL_TIME * 0.5f;
        op->cruise = op->path.len / (run > 1.0f ? run : 1.0f);
        if (op->cruise > RACE_AI_MAX_SPEED) {                           /* too fast for the port's physics: he takes longer than he did in the original */
            op->cruise = RACE_AI_MAX_SPEED;
            op->finish_time = op->path.len / op->cruise + RACE_ACCEL_TIME * 0.5f;
        }
        memcpy(op->pos, st, 12);
        op->fwd[2] = 1.0f; op->up[1] = 1.0f;
        r->nopp++;
    }
    r->next = 0;
    return 1;
}

static int passed_gate(const RaceGate *g, const float p0[3], const float p1[3]);

int race_use_recordings(Race *r, const Career *c, int chal, const GhostPak *pak, const char *level)
{
    if (chal < 0 || (uint32_t)chal >= c->nchal) return 0;
    const CareerChal *ch = &c->chal[chal];
    int n = 0;
    for (int i = 0; i < r->nopp; i++) {
        RaceOpp *op = &r->opp[i];
        GhostTrack t;
        op->has_rec = 0;
        if (!ghost_find(pak, level, chal, ch->mode, 0, op->ai, &t) || t.len < 5.0f || t.dur < 5.0f) continue;
        op->rec = t;
        float t_fin = t.dur;                                                        /* he is at the finish when the track first crosses the finish gate (in the second half of his run); the track goes on a few seconds as he rolls out */
        if (r->ngates > 0) {
            float done = 0.0f;
            for (int k = 1; k < t.n; k++) {
                if (t.flag[k] & 1) continue;
                const float *a = t.p + (k - 1) * 3, *b = t.p + k * 3;
                done += dist3d(a, b);
                if (done >= 0.5f * t.len && passed_gate(&r->gate[r->ngates - 1], a, b)) { t_fin = (float)k / (float)GHOST_HZ; break; }
            }
        }
        float pace = t_fin > 1.0f ? r->course.len / t_fin : 0.0f;                    /* the pace along the player's course: what the player's physics has to match */
        op->rate = pace > RACE_AI_MAX_SPEED ? RACE_AI_MAX_SPEED / pace : 1.0f;      /* too fast for the port's physics: he takes longer than he did in the original */
        op->finish_time = t_fin / op->rate * RACE_AI_FACTOR;
        op->has_rec = 1;
        n++;
    }
    return n;
}

/* ---- play ------------------------------------------------------------------------------------------------------------------------------------- */
static int segs_cross(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz)
{
    float d1x = bx - ax, d1z = bz - az, d2x = dx - cx, d2z = dz - cz;
    float den = d1x * d2z - d1z * d2x;
    if (fabsf(den) < 1e-9f) return 0;
    float t = ((cx - ax) * d2z - (cz - az) * d2x) / den, u = ((cx - ax) * d1z - (cz - az) * d1x) / den;
    return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
}

/* did the player pass the gate between p0 and p1: crossed its line (a little longer than it is, so the posts count) at about its height, or came within 6 m of a gate that is just a point */
static int passed_gate(const RaceGate *g, const float p0[3], const float p1[3])
{
    float w = dist2d(g->a, g->b);
    float lo = g->a[1] < g->b[1] ? g->a[1] : g->b[1], hi = g->a[1] < g->b[1] ? g->b[1] : g->a[1];
    if (p1[1] < lo - 4.0f || p1[1] > hi + 8.0f) return 0;
    if (w < 2.0f) return dist2d(p1, g->a) < 6.0f;
    float ux = (g->b[0] - g->a[0]) / w, uz = (g->b[2] - g->a[2]) / w, ext = 1.0f;
    return segs_cross(p0[0], p0[2], p1[0], p1[2], g->a[0] - ux * ext, g->a[2] - uz * ext, g->b[0] + ux * ext, g->b[2] + uz * ext);
}

static void project_on_course(Race *r, const float pos[3])
{
    const RacePath *c = &r->course;
    float best = 1e30f, bs = r->prog;
    int lo = (int)r->seg_hint - 2, hi = (int)r->seg_hint + 4;
    for (int pass = 0; pass < 2; pass++) {                                 /* the segments near the last one; all of them if the player is far from those */
        if (pass == 1) { if (best < 40.0f) break; lo = 0; hi = c->n - 2; }
        for (int i = lo < 0 ? 0 : lo; i <= hi && i < c->n - 1; i++) {
            const float *a = c->p[i], *b = c->p[i + 1];
            float abx = b[0] - a[0], abz = b[2] - a[2], l2 = abx * abx + abz * abz;
            float u = l2 > 1e-6f ? clampf(((pos[0] - a[0]) * abx + (pos[2] - a[2]) * abz) / l2, 0.0f, 1.0f) : 0.0f;
            float qx = a[0] + abx * u, qz = a[2] + abz * u, d = hypotf(pos[0] - qx, pos[2] - qz);
            if (d < best) { best = d; bs = c->cum[i] + (c->cum[i + 1] - c->cum[i]) * u; r->seg_hint = (float)i; }
        }
    }
    r->prog = bs;
}

/* the generic line's state as the view the drawing code reads: a few pushes off the line, a crouch at speed */
static void view_from_line(RaceOpp *o, float t)
{
    GhostPlayer *v = &o->view;
    memcpy(v->pos, o->pos, 12); memcpy(v->fwd, o->fwd, 12); memcpy(v->up, o->up, 12);
    v->heading = o->heading; v->speed = o->speed; v->steer = o->steer;
    v->grounded = 1; v->flip = 0; v->bail_t = 0.0f; v->air_t = 0.0f;
    v->push_t = (t > 0.0f && t < 5.0f) ? fmodf(t, 0.9f) : -1.0f;       /* (not before GO) */
}

/* an opponent that plays his recording: the ghost rider walks the track, the progress for the placing is the fraction of his run done */
static void rec_update(Race *r, RaceOpp *o, float dt, RaceGroundFn gfn, void *gctx)
{
    GhostPlayer *v = &o->view;
    if (r->t <= dt) { ghost_init(v, &o->rec, o->rate, 0.0f); o->travelled = 0.0f; ghost_update(v, 0.0f, gfn, gctx); }
    else {
        float p0[3];
        memcpy(p0, v->pos, 12);
        ghost_update(v, dt, gfn, gctx);
        if (!v->jumped) o->travelled += dist3d(p0, v->pos);
    }
    memcpy(o->pos, v->pos, 12); memcpy(o->fwd, v->fwd, 12); memcpy(o->up, v->up, 12);
    o->heading = v->heading; o->speed = v->speed; o->steer = v->steer;
    float frac = o->rec.len > 1.0f ? o->travelled / o->rec.len : 0.0f;
    o->s = (frac > 1.0f ? 1.0f : frac) * r->course.len;
    o->finished = r->t >= o->finish_time;
    if (o->finished) o->s = r->course.len + 1.0f;
}

static void opp_update(Race *r, RaceOpp *o, float dt, RaceGroundFn gfn, void *gctx)
{
    if (o->has_rec) { rec_update(r, o, dt, gfn, gctx); return; }
    float s = opp_distance(o, r->t), p[3], tn[3];
    float on = s < o->path.len ? s : o->path.len;
    race_path_at(&o->path, on, p, tn);
    if (s > o->path.len) for (int k = 0; k < 3; k++) p[k] += tn[k] * (s - o->path.len);
    o->s = s;
    o->finished = r->t >= o->finish_time;
    float gy;
    if (gfn && gfn(gctx, p[0], p[2], p[1] + 2.0f, &gy)) p[1] = gy;
    float old_h = o->heading;
    float look[3], lt[3];
    race_path_at(&o->path, on + 3.0f, look, lt);                         /* he faces a little way ahead, not the tangent at his feet */
    float h = atan2f(look[0] - p[0], look[2] - p[2]);
    if (dist2d(look, p) < 0.2f) h = atan2f(tn[0], tn[2]);
    if (r->t <= dt) old_h = h;
    float dh = h - old_h;
    while (dh > 3.14159265f) dh -= 6.2831853f;
    while (dh < -3.14159265f) dh += 6.2831853f;
    o->heading = old_h + dh * clampf(8.0f * dt, 0.0f, 1.0f);
    o->turn = dt > 1e-5f ? dh / dt : 0.0f;
    o->steer += (clampf(o->turn / 1.5f, -1.0f, 1.0f) - o->steer) * clampf(6.0f * dt, 0.0f, 1.0f);
    o->speed = opp_speed(o, r->t);
    memcpy(o->pos, p, 12);
    float fx = sinf(o->heading), fz = cosf(o->heading), pitch = 0.0f;       /* the slope under him from the ground a step ahead and behind */
    float yf, yb;
    if (gfn && gfn(gctx, p[0] + fx * 0.7f, p[2] + fz * 0.7f, p[1] + 1.0f, &yf) && gfn(gctx, p[0] - fx * 0.7f, p[2] - fz * 0.7f, p[1] + 1.0f, &yb)) pitch = clampf((yf - yb) / 1.4f, -1.0f, 1.0f);
    float fl = sqrtf(1.0f + pitch * pitch);
    o->fwd[0] = fx / fl; o->fwd[1] = pitch / fl; o->fwd[2] = fz / fl;
    float lx = o->fwd[2], lz = -o->fwd[0];                                  /* left = up x fwd with up = world up (then up = fwd x left) */
    float ll = hypotf(lx, lz);
    if (ll < 1e-6f) { o->up[0] = 0.0f; o->up[1] = 1.0f; o->up[2] = 0.0f; }
    else {
        lx /= ll; lz /= ll;
        o->up[0] = o->fwd[1] * lz; o->up[1] = o->fwd[2] * lx - o->fwd[0] * lz; o->up[2] = -o->fwd[1] * lx;
    }
    view_from_line(o, r->t);
}

int race_update(Race *r, float dt, const float pos[3], RaceGroundFn gfn, void *gctx)
{
    int ev = 0;
    r->t += dt;
    r->last_bonus = 0.0f;
    for (int i = 0; i < r->nopp; i++) opp_update(r, &r->opp[i], dt, gfn, gctx);
    if (!r->player_done) {
        if (r->have_prev && r->next < r->ngates && passed_gate(&r->gate[r->next], r->prev, pos)) {
            const RaceGate *g = &r->gate[r->next];
            r->next++;
            ev |= RACE_EV_GATE;
            if (g->finish) {
                r->player_done = 1;
                r->player_time = r->t + r->penalty;
                r->place = race_place_at(r, r->player_time);
                ev |= RACE_EV_FINISH;
            } else {
                r->last_bonus = g->bonus;
            }
        }
        project_on_course(r, pos);
    }
    memcpy(r->prev, pos, 12);
    r->have_prev = 1;
    return ev;
}

void race_preview(Race *r, RaceGroundFn gfn, void *gctx)
{
    r->t = 0.0f;
    for (int i = 0; i < r->nopp; i++) opp_update(r, &r->opp[i], 1.0f / 60.0f, gfn, gctx);
}

void race_bail(Race *r) { r->penalty += RACE_BAIL_PENALTY; }

int race_place_at(const Race *r, float time)
{
    int p = 1;
    for (int i = 0; i < r->nopp; i++)
        if (r->opp[i].finish_time < time) p++;
    return p;
}

int race_place(const Race *r)
{
    if (r->player_done) return r->place;
    int p = 1;
    for (int i = 0; i < r->nopp; i++)
        if (r->opp[i].s > r->prog + 1.0f) p++;
    return p;
}
