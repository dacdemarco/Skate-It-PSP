/* Level collision: floor height and wall queries over the converted collision polygons, accelerated with a uniform XZ grid.
 * No PSP dependencies (host-tested in tests/test_coll.c against brute force on the real level). */
#ifndef COLL_H
#define COLL_H

#include <stdint.h>

typedef struct {
    uint16_t v[4];          /* vertex indices, v[3] == 0xFFFF for a triangle */
    uint32_t flags;         /* surface flags (grind / ledge / ... meaning not decoded yet) */
} CollPoly;

/* One collision triangle as the grid stores it (64 bytes = one cache line on the PSP): the grid keeps a copy per cell the triangle touches, so a
 * query reads one contiguous run of records instead of chasing polygon -> vertex indices all over memory. */
typedef struct CTri {
    float v0[3], e1[3], e2[3];   /* vertex 0 and the two edges from it */
    float ny;                    /* |normal.y| of the triangle */
    float lo[3], hi[3];          /* bounding box */
} CTri;

typedef struct {
    const float *vert;      /* 3 floats per vertex */
    const CollPoly *poly;
    uint32_t nverts, npolys;
    float minx, minz, cell, inv_cell;
    int gw, gh;
    uint32_t *cell_start;   /* gw*gh + 1 */
    CTri *cell_tris;        /* the triangles of each cell: cell_start[i] .. cell_start[i+1] (64-byte aligned inside tri_mem) */
    uint32_t *cell_poly;    /* the polygon index of each record of cell_tris (only read after a hit, so it is kept apart from the 64-byte records) */
    void *tri_mem;
} Coll;

/* returns 0 on allocation failure */
int coll_build(Coll *c, const float *vert, uint32_t nverts, const CollPoly *poly, uint32_t npolys, float cell);
void coll_free(Coll *c);

/* Surface classes by |normal.y| (two-sided: the polygon winding of the DS data is not reliable):
 *   >= COLL_FLOOR_NY  floor: coll_ground() height queries (camera, shadow, spawn, the physics' crack fallback)
 *   >= COLL_RIDE_NY   rideable: the skate physics can roll on it (up to ~80 degrees: bowl flanks, ramps)
 *   <  COLL_RIDE_NY   wall: blocks (coll_wall) */
#define COLL_FLOOR_NY 0.5f
#define COLL_RIDE_NY  0.15f

/* highest walkable surface (|normal.y| >= COLL_FLOOR_NY) at or below y_max under (x, z) */
int coll_ground(const Coll *c, float x, float z, float y_max, float *y_out);
/* the same, and the index of the collision polygon that is the surface (for per-polygon data such as the material) */
int coll_ground_poly(const Coll *c, float x, float z, float y_max, float *y_out, uint32_t *poly_out);
/* nearest wall (|normal.y| < COLL_RIDE_NY) crossed by the segment a->b; normal points back towards a */
int coll_wall(const Coll *c, const float a[3], const float b[3], float hit[3], float normal[3]);
/* nearest triangle with ny_lo <= |normal.y| < ny_hi crossed by the segment a->b (normal faces a). tol > 0 forgives hits up to ~tol metres outside the
 * triangle's edges (cracks between polygons). The skate physics uses (COLL_RIDE_NY, 2) for surfaces to roll on. */
int coll_ray(const Coll *c, const float a[3], const float b[3], float ny_lo, float ny_hi, float tol, float hit[3], float normal[3]);

/* reference implementations without the grid (tests) */
int coll_ground_brute(const Coll *c, float x, float z, float y_max, float *y_out);
int coll_wall_brute(const Coll *c, const float a[3], const float b[3], float hit[3], float normal[3]);
int coll_ray_brute(const Coll *c, const float a[3], const float b[3], float ny_lo, float ny_hi, float tol, float hit[3], float normal[3]);

#endif
