#include "scene.h"
#include "rider.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define HDR_SIZE 64
#define SCENE_HEAD 16

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

int scenepak_load(ScenePak *p, const uint8_t *buf, size_t size)
{
    memset(p, 0, sizeof(*p));
    if (size < HDR_SIZE || memcmp(buf, "SCN1", 4) != 0 || rd32(buf + 4) != 1 || ((uintptr_t)buf & 3)) return 0;
    uint32_t nclips = rd32(buf + 8), nscenes = rd32(buf + 12), oc = rd32(buf + 16), oi = rd32(buf + 20), ob = rd32(buf + 24), os = rd32(buf + 28);
    if (nclips == 0 || nclips > 64 || nscenes == 0 || nscenes > 200) return 0;
    if (oc + nclips * 16 > size || oi + nclips * 8 > size || ob + SKEL_NBONES * 12 > size || os + nscenes * 12 > size) return 0;
    if ((oc | oi | ob | os) & 3) return 0;
    for (uint32_t i = 0; i < nclips; i++) {                                         /* every clip's frames lie inside the file */
        const SceneClipTab *c = (const SceneClipTab *)(buf + oc) + i;
        if (c->nframes == 0 || (uint64_t)c->frame_off + (uint64_t)c->nframes * sizeof(SkelFrame) > size) return 0;
        if ((uint64_t)c->board_off + (uint64_t)c->board_nframes * sizeof(BoardFrame) > size || c->board_nframes == 0 || ((c->frame_off | c->board_off) & 3)) return 0;
    }
    for (uint32_t i = 0; i < nscenes; i++) {
        const uint8_t *e = buf + os + 12 * i;
        uint32_t off = rd32(e + 4), len = rd32(e + 8);
        if ((uint64_t)off + len > size || len < SCENE_HEAD || (off & 3)) return 0;
    }
    p->buf = buf; p->size = size; p->nclips = nclips; p->nscenes = nscenes;
    p->clips = (const SkelAnim *)(buf + oc);
    p->info = (const SceneClipInfo *)(buf + oi);
    p->bone_off = (const float (*)[3])(buf + ob);
    return 1;
}

static const uint8_t *scene_entry(const ScenePak *p, int scene)
{
    if (!p->buf || scene < 1 || (uint32_t)scene > p->nscenes) return NULL;
    uint32_t os = rd32(p->buf + 28);
    return p->buf + os + 12 * (scene - 1);
}

int scenepak_level(const ScenePak *p, int scene)
{
    const uint8_t *e = scene_entry(p, scene);
    if (!e) return -1;
    int32_t v; memcpy(&v, e, 4);
    return v;
}

int scenepak_script(const ScenePak *p, int scene, SceneScript *out)
{
    const uint8_t *e = scene_entry(p, scene);
    memset(out, 0, sizeof(*out));
    if (!e) return 0;
    uint32_t off = rd32(e + 4), len = rd32(e + 8);
    const uint8_t *s = p->buf + off;
    out->dist = rd32(s); out->length = rd32(s + 4);
    uint16_t n[3];
    memcpy(n, s + 8, 6);
    out->nspawn = n[0]; out->ntalk = n[1]; out->nsub = n[2];
    if (SCENE_HEAD + 4u * n[0] + 8u * n[1] + 4u * n[2] > len) { memset(out, 0, sizeof(*out)); return 0; }
    out->spawn = (const SceneSpawn *)(s + SCENE_HEAD);
    out->talk = (const SceneTalk *)(s + SCENE_HEAD + 4 * n[0]);
    out->sub = (const SceneSub *)(s + SCENE_HEAD + 4 * n[0] + 8 * n[1]);
    return 1;
}

void scenepak_bones(const ScenePak *p, const SkelBone *skater_bones, SkelBone out[SKEL_NBONES])
{
    for (int i = 0; i < SKEL_NBONES; i++) {
        out[i].parent = skater_bones[i].parent;
        for (int k = 0; k < 3; k++) out[i].off[k] = p->bone_off[i][k];
    }
}

int scene_guest_place(const LevelObjs *o, int chal_slot, float pos[3])
{
    float q[4][3], h;
    int n = 0;
    if (chal_slot >= 0) {
        char nm[24];
        for (int k = 1; k <= 4; k++) {
            snprintf(nm, sizeof(nm), "_start_pos_%d", k);
            if (!levelobj_find_num(o, "chal", 3, chal_slot, nm, q[n])) break;
            n++;
        }
    }
    if (n == 0)
        for (int k = 1; k <= 4; k++) {
            if (!levelobj_start(o, k, q[n], &h)) break;
            n++;
        }
    if (n == 0) return 0;
    memcpy(pos, q[n > 1 ? 1 : 0], sizeof(float) * 3);
    return 1;
}

/* ---- the player -------------------------------------------------------------------------------------------------------------------------------------- */
int scenepak_type_count(const ScenePak *p, int type)
{
    int n = 0;
    for (uint32_t i = 0; i < p->nclips; i++) n += p->info[i].type == type;
    return n;
}

int scenepak_type_clip(const ScenePak *p, int type, int n)
{
    for (uint32_t i = 0; i < p->nclips; i++)
        if (p->info[i].type == type && n-- == 0) return (int)i;
    return -1;
}

static uint32_t rnd(ScenePlayer *sp)
{
    uint32_t x = sp->rng ? sp->rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    sp->rng = x;
    return x;
}

static AnimSet aset(const ScenePak *p)
{
    AnimSet a = { p->buf, p->clips, p->nclips };
    return a;
}

/* the heading offset that turns clip c (which faces info.face in the data frame) to angle `target` (from +z towards +x) in the viewer frame (viewer = (-x, z, y) of the data) */
static float clip_turn(const ScenePak *p, int c, float target)
{
    float th = p->info[c].face * (float)M_PI / 180.0f;
    return target - atan2f(-cosf(th), sinf(th));
}

static float angle_lerp(float a, float b, float t)
{
    float d = b - a;
    while (d > (float)M_PI) d -= 2.0f * (float)M_PI;
    while (d < -(float)M_PI) d += 2.0f * (float)M_PI;
    return a + d * t;
}

static float target_angle(const ScenePlayer *sp) { return sp->flip ? (float)M_PI : 0.0f; }

void sceneplayer_init(ScenePlayer *sp, const ScenePak *p, int scene, uint32_t seed)
{
    memset(sp, 0, sizeof(*sp));
    sp->pak = p;
    sp->scene = scene;
    sp->clip = sp->from_clip = -1;
    sp->rng = seed ? seed : 1;
    sp->link = 1;
    sp->fade = 1.0f;
    scenepak_script(p, scene, &sp->sc);
    if (sp->sc.nspawn > 0) sp->guest = sp->sc.spawn[0].skater;
}

static void pick_clip(ScenePlayer *sp)
{
    const ScenePak *p = sp->pak;
    int type = sp->link, n = scenepak_type_count(p, type);
    if (n == 0) { type = 1; n = scenepak_type_count(p, 1); }
    if (n == 0) { sp->clip = -1; return; }
    int pick = (int)(rnd(sp) % (uint32_t)n);
    if (n > 1 && scenepak_type_clip(p, type, pick) == sp->clip) pick = (pick + 1) % n;
    SkelFrame fr; BoardFrame bf;
    int had = sp->clip >= 0;
    if (had) {
        sceneplayer_pose(sp, &fr, &bf);
        sp->from = fr; sp->bfrom = bf;
    }
    sp->from_clip = sp->clip;
    sp->clip = scenepak_type_clip(p, type, pick);
    sp->t = 0.0f;
    sp->fade = had ? 0.0f : 1.0f;
    sp->fresh = had;
}

int sceneplayer_active(const ScenePlayer *sp) { return sp->sc.nspawn > 0; }

void sceneplayer_line(ScenePlayer *sp, int line, int nlines)
{
    const SceneScript *s = &sp->sc;
    if (s->nspawn == 0) return;
    int f;
    if (s->nsub == nlines && line >= 0 && line < s->nsub) f = s->sub[line].start;
    else f = nlines > 0 ? (int)((int64_t)line * (int64_t)s->length / nlines) : 0;
    sp->frame = f;
    int guest = s->nspawn > 0 ? s->spawn[0].skater : 0;
    for (int i = 0; i < s->nspawn; i++) if (s->spawn[i].frame <= f) guest = s->spawn[i].skater;
    int link = 0;
    for (int i = 0; i < s->ntalk; i++) {                         /* the span the frame is in, else the latest one that began */
        int open = s->talk[i].end == 0xFFFF || f < s->talk[i].end;
        if (s->talk[i].start <= f && open) { link = s->talk[i].link; break; }
        if (s->talk[i].start <= f) link = s->talk[i].link;
    }
    if (link <= 0) link = s->ntalk > 0 ? s->talk[0].link : 1;
    int changed = guest != sp->guest || link != sp->link || sp->clip < 0;
    sp->guest = guest;
    sp->link = link;
    if (changed) pick_clip(sp);
}

void sceneplayer_update(ScenePlayer *sp, float dt)
{
    if (sp->clip < 0) return;
    sp->t += dt;
    if (sp->fade < 1.0f) { sp->fade += dt / SCENE_CLIP_FADE; if (sp->fade > 1.0f) sp->fade = 1.0f; }
    float len = (float)(sp->pak->clips[sp->clip].nframes - 1) / SCENE_FPS;
    if (sp->t >= len) pick_clip(sp);                             /* the next clip of the same kind, faded in from the last pose */
}

void sceneplayer_pose(const ScenePlayer *sp, SkelFrame *out, BoardFrame *board)
{
    const ScenePak *p = sp->pak;
    if (sp->clip < 0) {
        memset(out, 0, sizeof(*out));
        for (int i = 0; i < SKEL_NBONES; i++) out->q[i][3] = 1.0f;
        memset(board, 0, sizeof(*board));
        board->q[3] = 1.0f;
        return;
    }
    AnimSet a = aset(p);
    const SkelAnim *an = &p->clips[sp->clip];
    float f = sp->t * SCENE_FPS;
    SkelFrame cur; BoardFrame bc;
    animset_sample(&a, sp->clip, f, &cur);
    float bfr = an->nframes > 1 ? (f > (float)(an->nframes - 1) ? (float)(an->nframes - 1) : f) * (float)(an->board_nframes - 1) / (float)(an->nframes - 1) : 0.0f;
    animset_board(&a, sp->clip, bfr, &bc);
    if (sp->fade < 1.0f) {
        float k = sp->fade * sp->fade * (3.0f - 2.0f * sp->fade);
        skelframe_blend(out, &sp->from, &cur, k);
        boardframe_blend(board, &sp->bfrom, &bc, k);
    } else {
        *out = cur; *board = bc;
    }
}

void sceneplayer_body(const ScenePlayer *sp, const float pos[3], float m[16])
{
    float psi = 0.0f;
    if (sp->clip >= 0) {
        psi = clip_turn(sp->pak, sp->clip, target_angle(sp));
        if (sp->fade < 1.0f && sp->from_clip >= 0) psi = angle_lerp(clip_turn(sp->pak, sp->from_clip, target_angle(sp)), psi, sp->fade * sp->fade * (3.0f - 2.0f * sp->fade));
    }
    float c = cosf(psi), s = sinf(psi);
    float r[16] = { c, 0, -s, 0,  0, 1, 0, 0,  s, 0, c, 0,  pos[0], pos[1], pos[2], 1 };
    memcpy(m, r, sizeof(r));
}

void sceneplayer_camera(const ScenePlayer *sp, const float pos[3], float eye[3], float look[3])
{
    float d = (float)sp->sc.dist / 4096.0f;
    if (d <= 0.0f) d = 2.0f;
    look[0] = pos[0]; look[1] = pos[1] + 1.0f; look[2] = pos[2];
    eye[0] = look[0]; eye[1] = look[1] + 1.0f; eye[2] = look[2] + d;
}
