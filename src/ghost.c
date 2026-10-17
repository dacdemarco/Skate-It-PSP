#include "ghost.h"
#include "trick.h"
#include "skate.h"
#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- the pak ------------------------------------------------------------------------------------------------------------------------------------ */
#define ENTRY_SIZE 44
#define HEAD_SIZE 8
typedef struct { char level[24]; uint16_t chal; uint8_t mode, a, b, pad[3]; uint32_t n, nev, off; } PakEntry;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void entry_at(const GhostPak *g, uint32_t i, PakEntry *e)
{
    const uint8_t *p = g->buf + HEAD_SIZE + (size_t)i * ENTRY_SIZE;
    memcpy(e->level, p, 24);
    e->chal = rd16(p + 24); e->mode = p[26]; e->a = p[27]; e->b = p[28];
    e->n = rd32(p + 32); e->nev = rd32(p + 36); e->off = rd32(p + 40);
}

int ghost_pak_load(GhostPak *g, const uint8_t *buf, size_t size)
{
    memset(g, 0, sizeof(*g));
    if (size < HEAD_SIZE || memcmp(buf, "AIP1", 4) != 0) return 0;
    uint32_t count = rd32(buf + 4);
    if (count > 100000u || HEAD_SIZE + (size_t)count * ENTRY_SIZE > size) return 0;
    g->buf = buf; g->size = size; g->count = count;
    for (uint32_t i = 0; i < count; i++) {                                     /* every entry's data lies inside the file and is 4-byte aligned (the floats are read in place) */
        PakEntry e;
        entry_at(g, i, &e);
        size_t flags = (size_t)e.n * 12 + e.n;
        flags = (flags + 3u) & ~(size_t)3u;
        if (e.off % 4 != 0 || (size_t)e.off + flags + (size_t)e.nev * 8 > size || e.n < 2) { memset(g, 0, sizeof(*g)); return 0; }
    }
    return 1;
}

static void track_of(const GhostPak *g, const PakEntry *e, GhostTrack *t)
{
    memset(t, 0, sizeof(*t));
    t->chal = e->chal; t->mode = e->mode; t->a = e->a; t->b = e->b;
    t->n = (int)e->n;
    t->p = (const float *)(const void *)(g->buf + e->off);
    t->flag = g->buf + e->off + (size_t)e->n * 12;
    size_t fl = ((size_t)e->n * 13 + 3u) & ~(size_t)3u;
    t->nev = (int)e->nev;
    t->ev = (const GhostEvent *)(const void *)(g->buf + e->off + fl);
    t->dur = (float)(t->n - 1) / (float)GHOST_HZ;
    for (int i = 1; i < t->n; i++)
        if (!(t->flag[i] & 1)) t->len += sqrtf((t->p[i * 3] - t->p[i * 3 - 3]) * (t->p[i * 3] - t->p[i * 3 - 3]) + (t->p[i * 3 + 1] - t->p[i * 3 - 2]) * (t->p[i * 3 + 1] - t->p[i * 3 - 2]) +
                                               (t->p[i * 3 + 2] - t->p[i * 3 - 1]) * (t->p[i * 3 + 2] - t->p[i * 3 - 1]));
}

static int same_level(const PakEntry *e, const char *level)
{
    char a[24];
    memcpy(a, e->level, 24); a[23] = 0;
    for (int i = 0; a[i] || level[i]; i++) {
        char x = a[i], y = level[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

int ghost_find(const GhostPak *g, const char *level, int chal, int mode, int a, int b, GhostTrack *t)
{
    for (uint32_t i = 0; i < g->count; i++) {
        PakEntry e;
        entry_at(g, i, &e);
        if (e.chal == chal && e.mode == mode && e.a == a && e.b == b && same_level(&e, level)) { track_of(g, &e, t); return 1; }
    }
    return 0;
}

int ghost_entry(const GhostPak *g, uint32_t i, GhostTrack *t, char level[24])
{
    if (i >= g->count) return 0;
    PakEntry e;
    entry_at(g, i, &e);
    track_of(g, &e, t);
    memcpy(level, e.level, 24); level[23] = 0;
    return 1;
}

int ghost_route_count(const GhostPak *g, const char *level)
{
    int n = 0;
    for (uint32_t i = 0; i < g->count; i++) {
        PakEntry e;
        entry_at(g, i, &e);
        if (e.chal == 0 && e.mode == 0 && same_level(&e, level)) n++;
    }
    return n;
}

int ghost_route(const GhostPak *g, const char *level, int k, GhostTrack *t)
{
    int n = 0;
    for (uint32_t i = 0; i < g->count; i++) {
        PakEntry e;
        entry_at(g, i, &e);
        if (e.chal == 0 && e.mode == 0 && same_level(&e, level)) {
            if (n++ == k) { track_of(g, &e, t); return 1; }
        }
    }
    return 0;
}

/* ---- tricks ------------------------------------------------------------------------------------------------------------------------------------- */
/* the original's id of each flip slot (the table of src/goals.c, which tests/test_ghost.c compares this one with) */
static const signed char FLIP_ID[NFLIP_TYPES] = { 13, 14, 1, 2, 3, 4, 5, 6, 8, 7, 10, 9, 12, 11, -1, -1, 31, 30, 34, 35, 33, 32 };

/* the ids of src/goals.c (GRIND_ID: the BS id, FS is one more; GRAB_ID), which tests/test_stage.c compares these with */
static const signed char GRIND_ID[NGRIND_TYPES] = { 36, 48, 38, 58, 60, 64, 62, 54, 56, 40, 42, 44, 46, 50, 66, 68 };
static const signed char GRAB_ID[NGRAB_TYPES] = { 70, 76, 71, 77, 80, 79, 72, 73, 89, 90, 92, 91, 93, 94, 96, 95, 85, 87, 86, 88, 82, 84, 83, 97, 98, 75, 74, 81, 78 };

int ghost_grind_of_id(int id, int *bs)
{
    for (int i = 0; i < NGRIND_TYPES; i++) {
        if (GRIND_ID[i] == id) { *bs = 1; return i + 1; }
        if (GRIND_ID[i] + 1 == id) { *bs = 0; return i + 1; }
    }
    *bs = 0;
    return 0;
}

int ghost_grab_of_id(int id)
{
    for (int i = 0; i < NGRAB_TYPES; i++) if (GRAB_ID[i] == id) return i + 1;
    return 0;
}

int ghost_flip_of_id(int id, int *nollie)
{
    *nollie = 0;
    if (id == 0 || id == 15) return 0;                                       /* a plain ollie / nollie: nothing to show */
    int base = id;
    if (id >= 16 && id <= 29) { base = id - 15; *nollie = 1; }
    for (int i = 0; i < NFLIP_TYPES; i++)
        if (FLIP_ID[i] == base && base >= 0) return i + 1;
    *nollie = 0;
    return 0;
}

/* ---- the track ---------------------------------------------------------------------------------------------------------------------------------- */
static int jump_into(const GhostTrack *t, int i) { return i > 0 && i < t->n && (t->flag[i] & 1); }

void ghost_pos_at(const GhostTrack *t, float time, float pos[3])
{
    float u = time * (float)GHOST_HZ;
    if (u <= 0.0f) { memcpy(pos, t->p, 12); return; }
    int i = (int)u;
    if (i >= t->n - 1) { memcpy(pos, t->p + (t->n - 1) * 3, 12); return; }
    const float *a = t->p + i * 3, *b = t->p + (i + 1) * 3;
    if (jump_into(t, i + 1)) { memcpy(pos, a, 12); return; }                 /* a respawn: he stays where he fell until the new place begins */
    float f = u - (float)i;
    for (int k = 0; k < 3; k++) pos[k] = a[k] + (b[k] - a[k]) * f;
}

/* the horizontal velocity round `time` (m/s): the segments before, at and after it, those across a respawn left out; 0 = the segment count that was used */
static int motion_at(const GhostTrack *t, float time, float v[2])
{
    int i = (int)floorf(clampf(time, 0.0f, t->dur) * (float)GHOST_HZ);
    if (i > t->n - 2) i = t->n - 2;
    if (i < 0) i = 0;
    float sx = 0.0f, sz = 0.0f, sw = 0.0f;
    for (int k = i - 1; k <= i + 1; k++) {
        if (k < 0 || k + 1 >= t->n || jump_into(t, k + 1)) continue;
        float w = k == i ? 2.0f : 1.0f;
        sx += (t->p[(k + 1) * 3] - t->p[k * 3]) * w; sz += (t->p[(k + 1) * 3 + 2] - t->p[k * 3 + 2]) * w; sw += w;
    }
    if (sw <= 0.0f) { v[0] = v[1] = 0.0f; return 0; }
    v[0] = sx / sw * (float)GHOST_HZ; v[1] = sz / sw * (float)GHOST_HZ;
    return 1;
}

static float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

void ghost_init(GhostPlayer *g, const GhostTrack *t, float rate, float time)
{
    memset(g, 0, sizeof(*g));
    g->t = t; g->rate = rate > 0.0f ? rate : 1.0f;
    g->time = clampf(time, 0.0f, t->dur);
    g->push_t = -1.0f;
    g->grounded = 1;
    ghost_pos_at(t, g->time, g->pos);
    float v[2];
    if (motion_at(t, g->time + 0.5f, v) && hypotf(v[0], v[1]) > 0.5f) g->heading = atan2f(v[0], v[1]);
    g->fwd[2] = 1.0f; g->up[1] = 1.0f;
    while (g->next_ev < t->nev && (float)t->ev[g->next_ev].frame / 60.0f < g->time) g->next_ev++;
}

/* the events up to the clock: a flip starts when its trick is logged, a push, a bail and the respawn that ends it */
static void events_until(GhostPlayer *g)
{
    const GhostTrack *t = g->t;
    while (g->next_ev < t->nev && (float)t->ev[g->next_ev].frame / 60.0f <= g->time) {
        const GhostEvent *e = &t->ev[g->next_ev++];
        switch (e->kind) {
        case GEV_FLIP: {
            int nl, f = ghost_flip_of_id(e->arg[0], &nl);
            if (f > 0) { g->flip = f; g->flip_nollie = nl; g->flip_t = 0.0f; g->flip_dur = (float)tricks.flip[f - 1].rate / 1000.0f; if (g->flip_dur < 0.2f) g->flip_dur = 0.6f; }
            break;
        }
        case GEV_GRIND: { int bs, t2 = ghost_grind_of_id(e->arg[0], &bs); if (t2 > 0) { g->grind = t2; g->grind_bs = bs; g->grind_t = 0.0f; g->flip = 0; } break; }
        case GEV_GRIND_END_A: case GEV_GRIND_END_B: case GEV_GRIND_END_C: g->grind = 0; break;
        case GEV_GRAB: { int t2 = ghost_grab_of_id(e->arg[0]); if (t2 > 0) { g->grab = t2; g->grab_t = 0.0f; } break; }
        case GEV_GRAB_END: g->grab = 0; break;
        case GEV_PUSH: g->push_t = 0.0f; break;
        case GEV_BAIL_A: case GEV_BAIL_B: if (g->bail_t <= 0.0f) g->bail_t = 0.001f; break;
        case GEV_RESPAWN: g->bail_t = 0.0f; g->flip = 0; g->grind = 0; g->grab = 0; break;
        default: break;
        }
    }
}

void ghost_update(GhostPlayer *g, float dt, GhostGroundFn gfn, void *gctx)
{
    const GhostTrack *t = g->t;
    float old_time = g->time;
    g->time += dt * g->rate;
    if (g->time >= t->dur) { g->time = t->dur; g->done = 1; }
    int i0 = (int)floorf(old_time * (float)GHOST_HZ), i1 = (int)floorf(g->time * (float)GHOST_HZ);
    g->jumped = 0;
    for (int k = i0 + 1; k <= i1 && k < t->n; k++) if (jump_into(t, k)) g->jumped = 1;
    events_until(g);
    if (g->flip) { g->flip_t += dt; if (g->flip_t >= g->flip_dur) g->flip = 0; }
    if (g->grind) { g->grind_t += dt; if (g->grind_t > 4.0f) g->grind = 0; }
    if (g->grab) { g->grab_t += dt; if (g->grab_t > 3.0f) g->grab = 0; }
    if (g->push_t >= 0.0f) { g->push_t += dt; if (g->push_t > SKATE_PUSH_TIME) g->push_t = -1.0f; }
    if (g->bail_t > 0.0f) { g->bail_t += dt; if (g->bail_t > 4.0f) g->bail_t = 0.0f; }
    float p[3];
    ghost_pos_at(t, g->time, p);
    float v[2];
    int have = motion_at(t, g->time, v);
    float spd = have && !g->done ? hypotf(v[0], v[1]) : 0.0f;          /* (at the end of the track he stands) */
    float old_h = g->heading;
    if (spd > 0.8f) {
        float h = atan2f(v[0], v[1]), dh = wrap_pi(h - old_h);
        if (g->jumped || old_time <= 0.0f) g->heading = h;
        else g->heading = old_h + dh * clampf(8.0f * dt, 0.0f, 1.0f);
        float turn = dt > 1e-5f && !g->jumped ? dh / dt : 0.0f;
        g->steer += (clampf(turn / 1.5f, -1.0f, 1.0f) - g->steer) * clampf(6.0f * dt, 0.0f, 1.0f);
    } else {
        g->steer += (0.0f - g->steer) * clampf(6.0f * dt, 0.0f, 1.0f);
    }
    g->speed = spd * g->rate;
    float gy;
    g->grounded = 1;
    if (gfn) {
        if (gfn(gctx, p[0], p[2], p[1] + 1.0f, &gy)) {
            if (p[1] - gy > 0.45f || g->grind) g->grounded = 0;               /* (on a rail he stays where the track puts him, on the rail) */
            else p[1] = gy;
        }
    }
    if (g->grounded) g->air_t = 0.0f; else g->air_t += dt;
    memcpy(g->pos, p, 12);
    float fx = sinf(g->heading), fz = cosf(g->heading), pitch = 0.0f;       /* the slope under him: the ground a step ahead and behind */
    if (gfn && g->grounded) {
        float yf, yb;
        if (gfn(gctx, p[0] + fx * 0.7f, p[2] + fz * 0.7f, p[1] + 1.0f, &yf) && gfn(gctx, p[0] - fx * 0.7f, p[2] - fz * 0.7f, p[1] + 1.0f, &yb)) pitch = clampf((yf - yb) / 1.4f, -1.0f, 1.0f);
    }
    float fl = sqrtf(1.0f + pitch * pitch);
    g->fwd[0] = fx / fl; g->fwd[1] = pitch / fl; g->fwd[2] = fz / fl;
    float lx = g->fwd[2], lz = -g->fwd[0], ll = hypotf(lx, lz);              /* left = up x fwd with up = world up, then up = fwd x left */
    if (ll < 1e-6f) { g->up[0] = 0.0f; g->up[1] = 1.0f; g->up[2] = 0.0f; return; }
    lx /= ll; lz /= ll;
    g->up[0] = g->fwd[1] * lz; g->up[1] = g->fwd[2] * lx - g->fwd[0] * lz; g->up[2] = -g->fwd[1] * lx;
}

/* ---- standing ----------------------------------------------------------------------------------------------------------------------------------- */
void ghost_stand(GhostPlayer *g, const float pos[3], float heading, GhostGroundFn gfn, void *gctx)
{
    memset(g, 0, sizeof(*g));
    g->rate = 1.0f; g->push_t = -1.0f; g->grounded = 1; g->done = 1;
    memcpy(g->pos, pos, 12);
    g->heading = heading;
    float gy, fx = sinf(heading), fz = cosf(heading), pitch = 0.0f;
    if (gfn && gfn(gctx, pos[0], pos[2], pos[1] + 1.0f, &gy)) {
        g->pos[1] = gy;
        float yf, yb;
        if (gfn(gctx, pos[0] + fx * 0.7f, pos[2] + fz * 0.7f, gy + 1.0f, &yf) && gfn(gctx, pos[0] - fx * 0.7f, pos[2] - fz * 0.7f, gy + 1.0f, &yb)) pitch = clampf((yf - yb) / 1.4f, -1.0f, 1.0f);
    }
    float fl = sqrtf(1.0f + pitch * pitch);
    g->fwd[0] = fx / fl; g->fwd[1] = pitch / fl; g->fwd[2] = fz / fl;
    float lx = g->fwd[2], lz = -g->fwd[0], ll = hypotf(lx, lz);
    if (ll < 1e-6f) { g->up[1] = 1.0f; return; }
    lx /= ll; lz /= ll;
    g->up[0] = g->fwd[1] * lz; g->up[1] = g->fwd[2] * lx - g->fwd[0] * lz; g->up[2] = -g->fwd[1] * lx;
}

void ghost_fill_skate(const GhostPlayer *g, Skate *o)
{
    memcpy(o->pos, g->pos, sizeof(o->pos)); memcpy(o->fwd, g->fwd, sizeof(o->fwd)); memcpy(o->up, g->up, sizeof(o->up)); memcpy(o->cn, g->up, sizeof(o->cn));
    o->heading = g->heading; o->speed = g->speed; o->steer = g->steer; o->grounded = g->grounded; o->vis_yaw = 0.0f; o->manual = 0;
    o->grinding = g->grind > 0; o->grind_type = g->grind > 0 ? g->grind - 1 : 0; o->grind_bs = g->grind_bs; o->grab = g->grab;
    o->vy = 0.0f;
    o->bail_t = g->bail_t; o->bail_dir = 1.0f;
    o->flip = g->flip; o->flip_nollie = g->flip_nollie; o->flip_t = g->flip_t; o->flip_dur = g->flip_dur > 0.01f ? g->flip_dur : 0.6f;
    o->air_t = g->air_t;
    o->roll = g->steer * 0.12f;
    o->crouch = g->speed > 9.0f ? 0.35f : 0.0f;
    o->push_t = g->push_t; o->push_dur = SKATE_PUSH_TIME;
}
