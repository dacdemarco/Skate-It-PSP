#include "nearclip.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GUARD     1900.0f       /* GE rejects beyond 2048 from the centre; keep a margin */
#define KSIDE     3.0f          /* clip to |x|,|y| <= KSIDE * depth: well outside the screen, well inside the guard band */
#define KSIDE_NORM 3.1622777f   /* sqrt(1 + KSIDE^2): sphere-to-plane distance scale */
#define MAXPOLY   10

typedef struct { float x, y, d, u, v; float r, g, b, a; } CV;       /* d = depth = -z_view */

static void tri_bbox_xz(const NcVertex *t, float *x0, float *x1, float *z0, float *z1)
{
    *x0 = fminf(t[0].x, fminf(t[1].x, t[2].x)); *x1 = fmaxf(t[0].x, fmaxf(t[1].x, t[2].x));
    *z0 = fminf(t[0].z, fminf(t[1].z, t[2].z)); *z1 = fmaxf(t[0].z, fmaxf(t[1].z, t[2].z));
}

static int cell_range(const NcGrid *g, float lo, float hi, float origin, int n, int *i0, int *i1)
{
    int a = (int)floorf((lo - origin) / g->cell), b = (int)floorf((hi - origin) / g->cell);
    if (b < 0 || a >= n) return 0;
    *i0 = a < 0 ? 0 : a; *i1 = b >= n ? n - 1 : b;
    return 1;
}

int nc_build(NcGrid *g, const NcVertex *verts, uint32_t nverts, const uint16_t *tex_of_tri, const uint8_t *skip, float cell)
{
    memset(g, 0, sizeof(*g));
    g->verts = verts; g->ntris = nverts / 3; g->cell = cell;
    if (g->ntris == 0) return 0;
    float minx = 1e30f, minz = 1e30f, maxx = -1e30f, maxz = -1e30f;
    for (uint32_t t = 0; t < g->ntris; t++) {
        if (skip && skip[t]) continue;
        float x0, x1, z0, z1;
        tri_bbox_xz(verts + t * 3, &x0, &x1, &z0, &z1);
        if (x0 < minx) minx = x0;
        if (x1 > maxx) maxx = x1;
        if (z0 < minz) minz = z0;
        if (z1 > maxz) maxz = z1;
    }
    if (minx > maxx) return 0;
    g->minx = minx; g->minz = minz;
    g->gw = (int)((maxx - minx) / cell) + 1; g->gh = (int)((maxz - minz) / cell) + 1;
    size_t ncell = (size_t)g->gw * (size_t)g->gh;
    g->cell_start = (uint32_t *)calloc(ncell + 1, sizeof(uint32_t));
    g->tex = (uint16_t *)malloc(sizeof(uint16_t) * g->ntris);
    g->stamp = (uint32_t *)calloc(g->ntris, sizeof(uint32_t));
    if (!g->cell_start || !g->tex || !g->stamp) { nc_free(g); return 0; }
    memcpy(g->tex, tex_of_tri, sizeof(uint16_t) * g->ntris);
    uint32_t *fill = NULL;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            for (size_t i = 0; i < ncell; i++) g->cell_start[i + 1] += g->cell_start[i];
            g->items = (NcItem *)malloc(sizeof(NcItem) * (g->cell_start[ncell] ? g->cell_start[ncell] : 1));
            fill = (uint32_t *)calloc(ncell, sizeof(uint32_t));
            if (!g->items || !fill) { free(fill); nc_free(g); return 0; }
        }
        for (uint32_t t = 0; t < g->ntris; t++) {
            if (skip && skip[t]) continue;
            float x0, x1, z0, z1; int i0, i1, j0, j1;
            tri_bbox_xz(verts + t * 3, &x0, &x1, &z0, &z1);
            if (!cell_range(g, x0, x1, g->minx, g->gw, &i0, &i1) || !cell_range(g, z0, z1, g->minz, g->gh, &j0, &j1)) continue;
            for (int j = j0; j <= j1; j++)
                for (int i = i0; i <= i1; i++) {
                    size_t ci = (size_t)j * (size_t)g->gw + (size_t)i;
                    if (pass == 0) g->cell_start[ci + 1]++;
                    else {
                        NcItem *it = &g->items[g->cell_start[ci] + fill[ci]++];
                        const NcVertex *v = verts + t * 3;
                        float cx = (v[0].x + v[1].x + v[2].x) / 3.0f, cy = (v[0].y + v[1].y + v[2].y) / 3.0f, cz = (v[0].z + v[1].z + v[2].z) / 3.0f, r2 = 0.0f;
                        for (int q = 0; q < 3; q++) {
                            float dx = v[q].x - cx, dy = v[q].y - cy, dz = v[q].z - cz, d2 = dx * dx + dy * dy + dz * dz;
                            if (d2 > r2) r2 = d2;
                        }
                        it->c[0] = cx; it->c[1] = cy; it->c[2] = cz; it->r = sqrtf(r2) * 1.001f + 1e-4f; it->t = t;
                    }
                }
        }
    }
    free(fill);
    return 1;
}

void nc_free(NcGrid *g)
{
    free(g->cell_start); free(g->items); free(g->tex); free(g->stamp);
    memset(g, 0, sizeof(*g));
}

void nc_lookat(float m[16], const float eye[3], const float center[3], const float up[3])
{
    float f[3] = { center[0] - eye[0], center[1] - eye[1], center[2] - eye[2] };
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] /= fl; f[1] /= fl; f[2] /= fl;
    float s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    m[0] = s[0]; m[4] = s[1]; m[8]  = s[2];  m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[1] = u[0]; m[5] = u[1]; m[9]  = u[2];  m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2]; m[14] = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    m[3] = m[7] = m[11] = 0.0f; m[15] = 1.0f;
}

static void to_view(const float view[16], const NcVertex *p, CV *o)
{
    float x = view[0] * p->x + view[4] * p->y + view[8]  * p->z + view[12];
    float y = view[1] * p->x + view[5] * p->y + view[9]  * p->z + view[13];
    float z = view[2] * p->x + view[6] * p->y + view[10] * p->z + view[14];
    o->x = x; o->y = y; o->d = -z; o->u = p->u; o->v = p->v;
    o->r = (float)(p->color & 255); o->g = (float)((p->color >> 8) & 255);
    o->b = (float)((p->color >> 16) & 255); o->a = (float)(p->color >> 24);
}

static CV lerp_cv(const CV *a, const CV *b, float t)
{
    CV r;
    r.x = a->x + (b->x - a->x) * t; r.y = a->y + (b->y - a->y) * t; r.d = a->d + (b->d - a->d) * t;
    r.u = a->u + (b->u - a->u) * t; r.v = a->v + (b->v - a->v) * t;
    r.r = a->r + (b->r - a->r) * t; r.g = a->g + (b->g - a->g) * t; r.b = a->b + (b->b - a->b) * t; r.a = a->a + (b->a - a->a) * t;
    return r;
}

/* clip polygon against  pa*x + pb*y + pd*d + pc >= 0 */
static int clip_plane(const CV *in, int n, CV *out, float pa, float pb, float pd, float pc)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const CV *a = &in[i], *b = &in[(i + 1) % n];
        float da = pa * a->x + pb * a->y + pd * a->d + pc, db = pa * b->x + pb * b->y + pd * b->d + pc;
        if (da >= 0.0f && m < MAXPOLY) out[m++] = *a;
        if ((da >= 0.0f) != (db >= 0.0f) && m < MAXPOLY)
            out[m++] = lerp_cv(a, b, da / (da - db));
    }
    return m;
}

static int cmp_tri(const void *a, const void *b)
{
    return (int)((const NcTri *)a)->tex - (int)((const NcTri *)b)->tex;
}

int nc_gather(NcGrid *g, const float view[16], const float eye[3], float radius, float near_plane, float focal, NcTri *out, int max)
{
    int i0, i1, j0, j1, count = 0;
    g->frame++;
    if (!cell_range(g, eye[0] - radius, eye[0] + radius, g->minx, g->gw, &i0, &i1) ||
        !cell_range(g, eye[2] - radius, eye[2] + radius, g->minz, g->gh, &j0, &j1))
        return 0;
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            size_t ci = (size_t)j * (size_t)g->gw + (size_t)i;
            for (uint32_t k = g->cell_start[ci]; k < g->cell_start[ci + 1]; k++) {
                const NcItem *it = &g->items[k];
                /* cheap early-out on the bounding sphere (sequential reads): a triangle can only be rejected if it reaches the near
                 * plane, or has a point whose depth is below 1/KG of its distance from the eye (KG = guard / focal). */
                float rx = it->c[0] - eye[0], ry = it->c[1] - eye[1], rz = it->c[2] - eye[2];
                float dc = -(view[2] * it->c[0] + view[6] * it->c[1] + view[10] * it->c[2] + view[14]);
                float dmin = dc - it->r;
                if (dc + it->r < near_plane) continue;                       /* entirely behind the near plane */
                {   /* entirely outside the clip frustum (|x|,|y| <= KSIDE*depth): nothing of it can be drawn by us */
                    float xc = view[0] * it->c[0] + view[4] * it->c[1] + view[8]  * it->c[2] + view[12];
                    float yc = view[1] * it->c[0] + view[5] * it->c[1] + view[9]  * it->c[2] + view[13];
                    float rr = it->r * KSIDE_NORM;
                    if (xc - KSIDE * dc > rr || -xc - KSIDE * dc > rr || yc - KSIDE * dc > rr || -yc - KSIDE * dc > rr) continue;
                }
                float lim = dmin * (GUARD / focal) - it->r;
                if (dmin >= near_plane && lim > 0.0f && lim * lim >= rx * rx + ry * ry + rz * rz) continue;
                uint32_t t = it->t;
                if (g->stamp[t] == g->frame) continue;
                g->stamp[t] = g->frame;
                /* what the GE would do: clip to the near plane, then reject if anything leaves the guard band (positions only first) */
                {
                    const NcVertex *v = &g->verts[t * 3];
                    float px[3], py[3], pd[3];
                    for (int q = 0; q < 3; q++) {
                        px[q] = view[0] * v[q].x + view[4] * v[q].y + view[8]  * v[q].z + view[12];
                        py[q] = view[1] * v[q].x + view[5] * v[q].y + view[9]  * v[q].z + view[13];
                        pd[q] = -(view[2] * v[q].x + view[6] * v[q].y + view[10] * v[q].z + view[14]);
                    }
                    if (pd[0] < near_plane && pd[1] < near_plane && pd[2] < near_plane) continue;
                    float glim = GUARD / focal;
                    int rej = 0;
                    for (int q = 0; q < 3 && !rej; q++) {
                        int q2 = (q + 1) % 3;
                        int in1 = pd[q] >= near_plane, in2 = pd[q2] >= near_plane;
                        if (in1 && (fabsf(px[q]) > glim * pd[q] || fabsf(py[q]) > glim * pd[q])) rej = 1;
                        if (in1 != in2) {                                    /* vertex created by the near-plane clip */
                            float tt = (near_plane - pd[q]) / (pd[q2] - pd[q]);
                            float cx = px[q] + (px[q2] - px[q]) * tt, cy = py[q] + (py[q2] - py[q]) * tt;
                            if (fabsf(cx) > glim * near_plane || fabsf(cy) > glim * near_plane) rej = 1;
                        }
                    }
                    if (!rej) continue;
                }
                CV poly[MAXPOLY];
                for (int cc = 0; cc < 3; cc++) to_view(view, &g->verts[t * 3 + (uint32_t)cc], &poly[cc]);
                /* we draw it ourselves: clip to near plane and to a frustum a few times wider than the screen */
                int n = 3;
                CV a[MAXPOLY], b[MAXPOLY];
                n = clip_plane(poly, n, a, 0.0f, 0.0f, 1.0f, -near_plane); if (n < 3) continue;
                n = clip_plane(a, n, b, -1.0f, 0.0f, KSIDE, 0.0f);          if (n < 3) continue;
                n = clip_plane(b, n, a,  1.0f, 0.0f, KSIDE, 0.0f);          if (n < 3) continue;
                n = clip_plane(a, n, b, 0.0f, -1.0f, KSIDE, 0.0f);          if (n < 3) continue;
                n = clip_plane(b, n, a, 0.0f,  1.0f, KSIDE, 0.0f);          if (n < 3) continue;
                for (int f = 1; f + 1 < n && count < max; f++) {
                    const CV *tri[3] = { &a[0], &a[f], &a[f + 1] };
                    NcTri *o = &out[count++];
                    o->tex = g->tex[t];
                    o->src = t;
                    for (int c = 0; c < 3; c++) {
                        o->v[c].u = tri[c]->u; o->v[c].v = tri[c]->v;
                        o->v[c].x = tri[c]->x; o->v[c].y = tri[c]->y; o->v[c].z = -tri[c]->d;
                        o->v[c].color = (uint32_t)(tri[c]->r + 0.5f) | ((uint32_t)(tri[c]->g + 0.5f) << 8) |
                                        ((uint32_t)(tri[c]->b + 0.5f) << 16) | ((uint32_t)(tri[c]->a + 0.5f) << 24);
                    }
                }
            }
        }
    qsort(out, (size_t)count, sizeof(NcTri), cmp_tri);
    return count;
}
