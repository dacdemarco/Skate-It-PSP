#include "coll.h"
#ifdef COLL_STATS
unsigned long coll_stat_tris, coll_stat_queries;      /* triangle records examined / queries made (host profiling only) */
#define STAT_TRI() (coll_stat_tris++)
#define STAT_QUERY() (coll_stat_queries++)
#else
#define STAT_TRI() ((void)0)
#define STAT_QUERY() ((void)0)
#endif
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- triangle records ----------------------------------------------------------------------------- */
/* the two triangles of polygon poly: t = poly*2 + (0: v0,v1,v2 | 1: v0,v2,v3). Returns 0 for a missing or degenerate triangle. */
static int make_ctri(const Coll *c, uint32_t t, CTri *o)
{
    const CollPoly *q = &c->poly[t >> 1];
    const float *a, *b, *d;
    if (t & 1) {
        if (q->v[3] == 0xFFFF) return 0;
        a = c->vert + q->v[0] * 3; b = c->vert + q->v[2] * 3; d = c->vert + q->v[3] * 3;
    } else {
        a = c->vert + q->v[0] * 3; b = c->vert + q->v[1] * 3; d = c->vert + q->v[2] * 3;
    }
    for (int k = 0; k < 3; k++) {
        o->v0[k] = a[k]; o->e1[k] = b[k] - a[k]; o->e2[k] = d[k] - a[k];
        float lo = a[k], hi = a[k];
        if (b[k] < lo) lo = b[k];
        if (b[k] > hi) hi = b[k];
        if (d[k] < lo) lo = d[k];
        if (d[k] > hi) hi = d[k];
        o->lo[k] = lo; o->hi[k] = hi;
    }
    float nx = o->e1[1] * o->e2[2] - o->e1[2] * o->e2[1], ny = o->e1[2] * o->e2[0] - o->e1[0] * o->e2[2], nz = o->e1[0] * o->e2[1] - o->e1[1] * o->e2[0];
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len < 1e-9f) return 0;
    o->ny = fabsf(ny / len);
    return 1;
}

/* unit normal of a record */
static void ctri_normal(const CTri *t, float n[3])
{
    n[0] = t->e1[1] * t->e2[2] - t->e1[2] * t->e2[1]; n[1] = t->e1[2] * t->e2[0] - t->e1[0] * t->e2[2]; n[2] = t->e1[0] * t->e2[1] - t->e1[1] * t->e2[0];
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    n[0] /= len; n[1] /= len; n[2] /= len;
}

/* The level's floor polygons do not always meet: cracks of a few centimetres between neighbours (measured: a 5 cm sliver between a flat floor and
 * the slope beside it) make the rider airborne for one frame, and under a rising slope that means falling through it. A point up to FLOOR_EDGE_TOL
 * (metres, measured in XZ) outside a polygon edge still counts as on it; the height is extrapolated from the polygon's plane. */
#define FLOOR_EDGE_TOL 0.08f

static int ctri_floor_height(const CTri *t, float x, float z, float *y_out)
{
    if (t->ny < COLL_FLOOR_NY) return 0;
    /* the tolerance region (3 offset edge lines) pokes far out of acute corners: keep it inside the bounding box + tolerance (also what the grid indexes) */
    if (x < t->lo[0] - FLOOR_EDGE_TOL || x > t->hi[0] + FLOOR_EDGE_TOL || z < t->lo[2] - FLOOR_EDGE_TOL || z > t->hi[2] + FLOOR_EDGE_TOL) return 0;
    float e1x = t->e1[0], e1z = t->e1[2], e2x = t->e2[0], e2z = t->e2[2];
    float d = e1x * e2z - e1z * e2x;                      /* 2 * signed XZ area */
    if (fabsf(d) < 1e-9f) return 0;
    float px = x - t->v0[0], pz = z - t->v0[2];
    float u = (px * e2z - pz * e2x) / d, v = (e1x * pz - e1z * px) / d;     /* point = v0 + u e1 + v e2 */
    float l0 = 1.0f - u - v;                              /* barycentric of vertex 0; u, v belong to vertices 1 and 2 */
    /* barycentric l_i = (distance to the edge opposite vertex i) / (that vertex's height over the edge) */
    float f = FLOOR_EDGE_TOL / fabsf(d);
    float tu = f * sqrtf(e2x * e2x + e2z * e2z);          /* edge v0-v2 is opposite vertex 1 */
    float tv = f * sqrtf(e1x * e1x + e1z * e1z);          /* edge v0-v1 is opposite vertex 2 */
    float dx = e2x - e1x, dz = e2z - e1z;
    float t0 = f * sqrtf(dx * dx + dz * dz);              /* edge v1-v2 is opposite vertex 0 */
    if (u < -tu || v < -tv || l0 < -t0) return 0;
    *y_out = t->v0[1] + u * t->e1[1] + v * t->e2[1];
    return 1;
}

/* two-sided Moller-Trumbore of the segment p + t*d (d = q - p), t in [0,1], against a record. tol (metres, roughly) lets the segment hit just outside
 * the triangle's edges: the level's polygons have cracks of a few cm, and a rider crossing one must not fall through it.
 * qlo/qhi: bounding box of the segment, already inflated for the tolerance. */
static int ctri_ray(const CTri *t, const float p[3], const float d[3], const float qlo[3], const float qhi[3], float ny_lo, float ny_hi, float tol, float *best_t, float normal[3])
{
    STAT_TRI();
    if (t->ny < ny_lo || t->ny >= ny_hi) return 0;
    if (t->hi[0] < qlo[0] || t->lo[0] > qhi[0] || t->hi[1] < qlo[1] || t->lo[1] > qhi[1] || t->hi[2] < qlo[2] || t->lo[2] > qhi[2]) return 0;
    const float *e1 = t->e1, *e2 = t->e2;
    float h[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
    float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    if (fabsf(det) < 1e-9f) return 0;
    float inv = 1.0f / det;
    /* u, v are the barycentric coordinates (of vertices 1 and 2) of the point where the line meets the triangle's plane. The tolerance is a DISTANCE (tol metres) from each edge line,
     * which in barycentric units is tol * |edge| / (2 * area): a long thin sliver (the fans of the bowl's flanks are 0.2 m wide and 13 m long) forgives as much as a fat triangle does. */
    float tu = 0.0f, tv = 0.0f, t0 = 0.0f;
    if (tol > 0.0f) {
        float cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2], cz = e1[0] * e2[1] - e1[1] * e2[0];
        float area2 = sqrtf(cx * cx + cy * cy + cz * cz);
        if (area2 > 1e-9f) {
            float l1 = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]), l2 = sqrtf(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
            float dx = e2[0] - e1[0], dy = e2[1] - e1[1], dz = e2[2] - e1[2], l3 = sqrtf(dx * dx + dy * dy + dz * dz);
            tu = tol * l2 / area2;                            /* edge v0-v2 is opposite vertex 1 */
            tv = tol * l1 / area2;                            /* edge v0-v1 is opposite vertex 2 */
            t0 = tol * l3 / area2;                            /* edge v1-v2 is opposite vertex 0 */
        }
    }
    float s[3] = { p[0] - t->v0[0], p[1] - t->v0[1], p[2] - t->v0[2] };
    float u = (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]) * inv;
    if (u < -tu) return 0;
    float qv[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
    float v = (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]) * inv;
    if (v < -tv || 1.0f - u - v < -t0) return 0;
    float tt = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) * inv;
    if (tt < 0.0f || tt > 1.0f || tt >= *best_t) return 0;
    *best_t = tt;
    float n[3];
    ctri_normal(t, n);
    float side = (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) > 0.0f ? -1.0f : 1.0f;     /* face the incoming side */
    normal[0] = n[0] * side; normal[1] = n[1] * side; normal[2] = n[2] * side;
    return 1;
}

static void seg_box(const float a[3], const float b[3], float tol, float lo[3], float hi[3])
{
    float m = tol * 2.1f;                                 /* a tolerant hit can lie ~2 tol outside the triangle's box */
    for (int k = 0; k < 3; k++) {
        lo[k] = (a[k] < b[k] ? a[k] : b[k]) - m;
        hi[k] = (a[k] > b[k] ? a[k] : b[k]) + m;
    }
}

/* ---- brute force (reference): the same record maths over every triangle, no grid ------------------- */
int coll_ground_brute(const Coll *c, float x, float z, float y_max, float *y_out)
{
    int found = 0; float best = -1e30f;
    for (uint32_t t = 0; t < c->npolys * 2; t++) {
        CTri ct; float y;
        if (make_ctri(c, t, &ct) && ctri_floor_height(&ct, x, z, &y) && y <= y_max && y > best) { best = y; found = 1; }
    }
    if (found) *y_out = best;
    return found;
}

int coll_ray_brute(const Coll *c, const float a[3], const float b[3], float ny_lo, float ny_hi, float tol, float hit[3], float normal[3])
{
    float best = 1e30f, d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, qlo[3], qhi[3];
    seg_box(a, b, tol, qlo, qhi);
    for (uint32_t t = 0; t < c->npolys * 2; t++) {
        CTri ct;
        if (make_ctri(c, t, &ct)) ctri_ray(&ct, a, d, qlo, qhi, ny_lo, ny_hi, tol, &best, normal);
    }
    if (best >= 1e30f) return 0;
    for (int k = 0; k < 3; k++) hit[k] = a[k] + d[k] * best;
    return 1;
}

int coll_wall_brute(const Coll *c, const float a[3], const float b[3], float hit[3], float normal[3])
{
    return coll_ray_brute(c, a, b, 0.0f, COLL_RIDE_NY, 0.0f, hit, normal);
}

/* ---- grid ------------------------------------------------------------------------------------ */
static int cell_index(float v, float origin, float inv)
{
    float f = (v - origin) * inv;
    return f >= 0.0f ? (int)f : (int)floorf(f);
}

static int cell_range(const Coll *c, float lo, float hi, float origin, int n, int *i0, int *i1)
{
    int a = cell_index(lo, origin, c->inv_cell), b = cell_index(hi, origin, c->inv_cell);
    if (b < 0 || a >= n) return 0;
    *i0 = a < 0 ? 0 : a; *i1 = b >= n ? n - 1 : b;
    return 1;
}

int coll_build(Coll *c, const float *vert, uint32_t nverts, const CollPoly *poly, uint32_t npolys, float cell)
{
    memset(c, 0, sizeof(*c));
    c->vert = vert; c->poly = poly; c->nverts = nverts; c->npolys = npolys; c->cell = cell; c->inv_cell = 1.0f / cell;
    if (npolys == 0 || nverts == 0) return 0;
    float minx = 1e30f, minz = 1e30f, maxx = -1e30f, maxz = -1e30f;
    for (uint32_t i = 0; i < npolys; i++) {                  /* the grid covers the polygons, not the whole vertex pool (which has unused vertices far outside the level) */
        for (int k = 0; k < 4; k++) {
            uint32_t vi = poly[i].v[k];
            if (vi == 0xFFFF || vi >= nverts) continue;
            float x = vert[vi * 3], z = vert[vi * 3 + 2];
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (z < minz) minz = z;
            if (z > maxz) maxz = z;
        }
    }
    minx -= FLOOR_EDGE_TOL; minz -= FLOOR_EDGE_TOL; maxx += FLOOR_EDGE_TOL; maxz += FLOOR_EDGE_TOL;     /* triangles reach FLOOR_EDGE_TOL past their vertices */
    c->minx = minx; c->minz = minz;
    c->gw = (int)((maxx - minx) / cell) + 1; c->gh = (int)((maxz - minz) / cell) + 1;
    size_t ncell = (size_t)c->gw * (size_t)c->gh;
    c->cell_start = (uint32_t *)calloc(ncell + 1, sizeof(uint32_t));
    if (!c->cell_start) return 0;
    uint32_t *fill = NULL;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            for (size_t i = 0; i < ncell; i++) c->cell_start[i + 1] += c->cell_start[i];
            size_t total = c->cell_start[ncell] ? c->cell_start[ncell] : 1;
            c->tri_mem = malloc(total * sizeof(CTri) + 64);
            fill = (uint32_t *)calloc(ncell, sizeof(uint32_t));
            if (!c->tri_mem || !fill) { free(fill); coll_free(c); return 0; }
            c->cell_tris = (CTri *)(((uintptr_t)c->tri_mem + 63) & ~(uintptr_t)63);     /* one record per cache line */
            c->cell_poly = (uint32_t *)malloc(total * sizeof(uint32_t));
            if (!c->cell_poly) { free(fill); coll_free(c); return 0; }
        }
        for (uint32_t t = 0; t < npolys * 2; t++) {
            CTri ct;
            if (!make_ctri(c, t, &ct)) continue;
            int i0, i1, j0, j1;
            if (!cell_range(c, ct.lo[0] - FLOOR_EDGE_TOL, ct.hi[0] + FLOOR_EDGE_TOL, c->minx, c->gw, &i0, &i1) ||
                !cell_range(c, ct.lo[2] - FLOOR_EDGE_TOL, ct.hi[2] + FLOOR_EDGE_TOL, c->minz, c->gh, &j0, &j1)) continue;
            for (int j = j0; j <= j1; j++)
                for (int i = i0; i <= i1; i++) {
                    size_t ci = (size_t)j * (size_t)c->gw + (size_t)i;
                    if (pass == 0) c->cell_start[ci + 1]++;
                    else { c->cell_poly[c->cell_start[ci] + fill[ci]] = t >> 1; c->cell_tris[c->cell_start[ci] + fill[ci]++] = ct; }
                }
        }
    }
    free(fill);
    return 1;
}

void coll_free(Coll *c)
{
    free(c->cell_start); free(c->tri_mem); free(c->cell_poly);
    c->cell_start = NULL; c->tri_mem = NULL; c->cell_tris = NULL; c->cell_poly = NULL;
}

int coll_ground(const Coll *c, float x, float z, float y_max, float *y_out)
{
    int i = cell_index(x, c->minx, c->inv_cell), j = cell_index(z, c->minz, c->inv_cell);
    if (i < 0 || j < 0 || i >= c->gw || j >= c->gh) return 0;
    size_t ci = (size_t)j * (size_t)c->gw + (size_t)i;
    int found = 0; float best = -1e30f;
    STAT_QUERY();
    for (uint32_t k = c->cell_start[ci]; k < c->cell_start[ci + 1]; k++) {
        float y;
        STAT_TRI();
        if (ctri_floor_height(&c->cell_tris[k], x, z, &y) && y <= y_max && y > best) { best = y; found = 1; }
    }
    if (found) *y_out = best;
    return found;
}

int coll_ground_poly(const Coll *c, float x, float z, float y_max, float *y_out, uint32_t *poly_out)
{
    int i = cell_index(x, c->minx, c->inv_cell), j = cell_index(z, c->minz, c->inv_cell);
    if (i < 0 || j < 0 || i >= c->gw || j >= c->gh) return 0;
    size_t ci = (size_t)j * (size_t)c->gw + (size_t)i;
    int found = 0; float best = -1e30f; uint32_t bk = 0;
    for (uint32_t k = c->cell_start[ci]; k < c->cell_start[ci + 1]; k++) {
        float y;
        if (ctri_floor_height(&c->cell_tris[k], x, z, &y) && y <= y_max && y > best) { best = y; found = 1; bk = k; }
    }
    if (found) { *y_out = best; *poly_out = c->cell_poly[bk]; }
    return found;
}

int coll_ray(const Coll *c, const float a[3], const float b[3], float ny_lo, float ny_hi, float tol, float hit[3], float normal[3])
{
    int i0, i1, j0, j1;
    STAT_QUERY();
    float qlo[3], qhi[3], d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    seg_box(a, b, tol, qlo, qhi);
    if (!cell_range(c, qlo[0], qhi[0], c->minx, c->gw, &i0, &i1) || !cell_range(c, qlo[2], qhi[2], c->minz, c->gh, &j0, &j1)) return 0;
    float best = 1e30f;
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            size_t ci = (size_t)j * (size_t)c->gw + (size_t)i;
            for (uint32_t k = c->cell_start[ci]; k < c->cell_start[ci + 1]; k++)
                ctri_ray(&c->cell_tris[k], a, d, qlo, qhi, ny_lo, ny_hi, tol, &best, normal);
        }
    if (best >= 1e30f) return 0;
    for (int k = 0; k < 3; k++) hit[k] = a[k] + d[k] * best;
    return 1;
}

int coll_wall(const Coll *c, const float a[3], const float b[3], float hit[3], float normal[3])
{
    return coll_ray(c, a, b, 0.0f, COLL_RIDE_NY, 0.0f, hit, normal);
}
