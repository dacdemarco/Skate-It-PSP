/* Grindable rails (no PSP dependencies - host-tested in tests/test_rails.c).
 * A rail is a polyline over the TOP of a tube or box (tools/rails.py extracts them from the level's collision flag 256 objects). Positions along a rail are arc
 * lengths t in [0, len]. */
#ifndef RAILS_H
#define RAILS_H

#include <stdint.h>

/* what a grindable line is: a rail is a tube / box you slide over the top of; a ledge is the convex top edge of a block (tools/ledges.py) - its polyline is ordered so that
 * the TOP surface lies to the LEFT of the direction of increasing t (left = up x dir), and only a rider on that side can lock on to it */
enum { RAIL_KIND_RAIL = 0, RAIL_KIND_LEDGE = 1 };

typedef struct {
    const float *pts;       /* npts * 3 floats (inside the level file) */
    float *cum;             /* npts cumulative arc lengths, cum[0] = 0 */
    uint32_t npts;
    uint32_t kind;          /* RAIL_KIND_* */
    uint32_t mat;           /* what it is made of: 0 concrete, 1 metal, 2 wood (tools/materials.py) - picks the grinding sound and the sparks */
    float len;
    float lo[3], hi[3];     /* bounding box of the points */
} Rail;

typedef struct Rails {
    Rail *rail;
    uint32_t n;
} Rails;

/* table: n * {first point, npoints | kind << 16 | material << 24} (uint32 pairs; level files from before ledges have kind 0 = rail everywhere), pts: all points (3 floats each).
 * Returns 0 on allocation failure. */
int rails_build(Rails *r, const uint32_t *table, uint32_t n, const float *pts);
void rails_free(Rails *r);

/* point and unit direction (in increasing t) at arc length t (clamped) */
void rails_point(const Rails *r, uint32_t rail, float t, float p[3], float dir[3]);

/* the rail point nearest to p, if one is within horiz metres in XZ and p.y lies from `below` under to `above` over the rail's top there.
 * Returns 1 and the rail index, arc length t, point and direction. */
int rails_find(const Rails *r, const float p[3], float horiz, float below, float above, uint32_t *rail, float *t, float pt[3], float dir[3]);

/* the same without the ledge-side rule; side = how far the point is to the LEFT of a ledge's direction (negative = over the drop), 0 for rails. For diagnostics. */
int rails_find_any_side(const Rails *r, const float p[3], float horiz, float below, float above, uint32_t *rail, float *t, float pt[3], float dir[3], float *side);

#endif
