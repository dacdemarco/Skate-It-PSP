/* Software clipping for level triangles close to the camera (no PSP dependencies, host-tested in tests/test_nearclip.c).
 *
 * The PSP GE clips against the near plane but then REJECTS any triangle that has a vertex further than 2048 from the viewport
 * centre in screen space. Big triangles under or beside the camera therefore vanish (the sky colour shows through). The static level
 * draw can't avoid that, so every frame the triangles near the camera that would be rejected are clipped here, to a frustum
 * that is a few times wider than the screen, and drawn again in view space. (Model verified against the photo: tools/guard_check.py.)
 */
#ifndef NEARCLIP_H
#define NEARCLIP_H

#include <stdint.h>

typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} NcVertex;                         /* same layout as the level vertices (24 bytes) */

typedef struct {
    float c[3], r;                  /* bounding sphere of the triangle */
    uint32_t t;                     /* triangle index */
} NcItem;                           /* 20 bytes, stored per grid cell so the early-out reads memory sequentially */

typedef struct {
    const NcVertex *verts;          /* level triangle list */
    uint32_t ntris;
    float minx, minz, cell;
    int gw, gh;
    uint32_t *cell_start;           /* gw*gh + 1 */
    NcItem *items;                  /* per cell: the triangles overlapping it */
    uint16_t *tex;                  /* texture id per triangle (0xFFFF = none) */
    uint32_t *stamp;                /* per-triangle frame stamp (dedupe across cells) */
    uint32_t frame;
} NcGrid;

typedef struct {
    NcVertex v[3];                  /* view space (camera looks down -z) */
    uint16_t tex;
    uint32_t src;                   /* source triangle index (tests) */
} NcTri;

/* skip[i] != 0 excludes triangle i (e.g. the far backdrop layer). tex_of_tri[i] is copied. Returns 0 on failure. */
int nc_build(NcGrid *g, const NcVertex *verts, uint32_t nverts, const uint16_t *tex_of_tri, const uint8_t *skip, float cell);
void nc_free(NcGrid *g);

/* view = column-major 4x4 world->view matrix (gluLookAt layout); eye = camera position in the world.
 * Writes up to max triangles into out (view space, ready to draw with identity view/model matrices), sorted by texture.
 * near = the projection's near plane, focal = pixels per unit of x/depth (136 / tan(fov/2)). Returns the number written. */
int nc_gather(NcGrid *g, const float view[16], const float eye[3], float radius, float near_plane, float focal, NcTri *out, int max);

/* gluLookAt-style view matrix (column-major) */
void nc_lookat(float m[16], const float eye[3], const float center[3], const float up[3]);

#endif
