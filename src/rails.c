#include "rails.h"
#include <math.h>
#include <stdlib.h>

#define LEDGE_OUTSIDE 0.05f     /* a rider this far past the edge (over the drop) can still lock on */

int rails_build(Rails *r, const uint32_t *table, uint32_t n, const float *pts)
{
    r->n = 0;
    r->rail = n ? (Rail *)calloc(n, sizeof(Rail)) : NULL;
    if (n && !r->rail) return 0;
    for (uint32_t i = 0; i < n; i++) {
        Rail *a = &r->rail[i];
        a->pts = pts + (size_t)table[2 * i] * 3;
        a->npts = table[2 * i + 1] & 0xFFFFu;
        a->kind = (table[2 * i + 1] >> 16) & 0xFFu;
        a->mat = (table[2 * i + 1] >> 24) & 0xFFu;
        a->cum = (float *)malloc(sizeof(float) * (a->npts ? a->npts : 1));
        if (!a->cum) { r->n = i; rails_free(r); return 0; }
        a->cum[0] = 0.0f;
        for (int k = 0; k < 3; k++) a->lo[k] = a->hi[k] = a->pts[k];
        for (uint32_t j = 1; j < a->npts; j++) {
            const float *p = a->pts + j * 3, *q = p - 3;
            float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
            a->cum[j] = a->cum[j - 1] + sqrtf(dx * dx + dy * dy + dz * dz);
            for (int k = 0; k < 3; k++) { if (p[k] < a->lo[k]) a->lo[k] = p[k]; if (p[k] > a->hi[k]) a->hi[k] = p[k]; }
        }
        a->len = a->cum[a->npts - 1];
    }
    r->n = n;
    return 1;
}

void rails_free(Rails *r)
{
    if (r->rail)
        for (uint32_t i = 0; i < r->n; i++) free(r->rail[i].cum);
    free(r->rail);
    r->rail = NULL; r->n = 0;
}

/* segment index containing arc length t */
static uint32_t seg_at(const Rail *a, float t)
{
    uint32_t j = 1;
    while (j < a->npts - 1 && a->cum[j] < t) j++;
    return j - 1;
}

void rails_point(const Rails *r, uint32_t rail, float t, float p[3], float dir[3])
{
    const Rail *a = &r->rail[rail];
    if (t < 0.0f) t = 0.0f;
    if (t > a->len) t = a->len;
    uint32_t j = seg_at(a, t);
    const float *p0 = a->pts + j * 3, *p1 = p0 + 3;
    float sl = a->cum[j + 1] - a->cum[j];
    float f = sl > 1e-6f ? (t - a->cum[j]) / sl : 0.0f;
    for (int k = 0; k < 3; k++) p[k] = p0[k] + (p1[k] - p0[k]) * f;
    if (dir) {
        for (int k = 0; k < 3; k++) dir[k] = sl > 1e-6f ? (p1[k] - p0[k]) / sl : 0.0f;
    }
}

static int find_impl(const Rails *r, const float p[3], float horiz, float below, float above, int check_side, uint32_t *rail, float *t_out, float pt[3], float dir[3], float *side_out)
{
    float best = 1e30f;
    int found = 0;
    for (uint32_t i = 0; i < r->n; i++) {
        const Rail *a = &r->rail[i];
        if (p[0] < a->lo[0] - horiz || p[0] > a->hi[0] + horiz || p[2] < a->lo[2] - horiz || p[2] > a->hi[2] + horiz ||
            p[1] < a->lo[1] - below || p[1] > a->hi[1] + above) continue;                 /* bounding box reject */
        for (uint32_t j = 0; j + 1 < a->npts; j++) {
            const float *p0 = a->pts + j * 3, *p1 = p0 + 3;
            float ex = p1[0] - p0[0], ez = p1[2] - p0[2], l2 = ex * ex + ez * ez;
            float u = l2 > 1e-8f ? ((p[0] - p0[0]) * ex + (p[2] - p0[2]) * ez) / l2 : 0.0f;
            if (u < 0.0f) u = 0.0f;
            if (u > 1.0f) u = 1.0f;
            float qx = p0[0] + ex * u, qz = p0[2] + ez * u, qy = p0[1] + (p1[1] - p0[1]) * u;
            float dx = p[0] - qx, dz = p[2] - qz, d2 = dx * dx + dz * dz;
            float dy = p[1] - qy;
            if (d2 > horiz * horiz || dy < -below || dy > above || d2 >= best) continue;
            float side = 0.0f;
            if (a->kind == RAIL_KIND_LEDGE && l2 > 1e-8f) {        /* a ledge is only grabbed from its top: its left side (left = (dir.z, -dir.x)) */
                side = (dx * ez - dz * ex) / sqrtf(l2);
                if (check_side && side < -LEDGE_OUTSIDE) continue;
            }
            if (side_out) *side_out = side;
            best = d2; found = 1;
            *rail = i;
            float sl = a->cum[j + 1] - a->cum[j];
            *t_out = a->cum[j] + sl * u;
        }
    }
    if (found) rails_point(r, *rail, *t_out, pt, dir);
    return found;
}

int rails_find(const Rails *r, const float p[3], float horiz, float below, float above, uint32_t *rail, float *t_out, float pt[3], float dir[3])
{
    return find_impl(r, p, horiz, below, above, 1, rail, t_out, pt, dir, NULL);
}

int rails_find_any_side(const Rails *r, const float p[3], float horiz, float below, float above, uint32_t *rail, float *t_out, float pt[3], float dir[3], float *side)
{
    return find_impl(r, p, horiz, below, above, 0, rail, t_out, pt, dir, side);
}
