/* Development tool: draws the guest of a story scene as the PSP would (same pose, body matrix and camera, a software rasteriser), to look at the facing and the framing without the console.
 * usage: scene_preview <scenes.pak> <career.dat> <data dir> <scene> <line> <flip 0|1> <seconds into the clip> <out.ppm> [clip id]
 * Draws the skater of the scene's first guest (textured) and the board, on a grey floor at the guest's feet; no level. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/scene.h"
#include "../src/career.h"
#include "../src/rider.h"

#define W 480
#define H 272
#define FOCAL (136.0f / 0.63704518f)

typedef struct { uint16_t w, h; uint8_t bpp, swz, alpha, pad; uint32_t clut_off, pix_off, n, pad2; } Tex;
typedef struct { float u, v; uint32_t color; float x, y, z; } Vtx;
typedef struct { char magic[4]; uint32_t version, nverts, nbones, nanims, off_tex, off_verts, off_vbone, off_skel, off_anims; float scale, height; uint32_t pad[12]; } Skt;
typedef struct { char magic[4]; uint32_t version, ntex, nverts, off_tex, off_batch, off_verts; float scale; uint32_t pad[8]; } Brd;
typedef struct { uint16_t tex, flags; uint32_t first, count, pad; } Batch;

static uint8_t fb[H][W][3];
static float zb[H][W];

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = aligned_alloc(64, ((size_t)sz + 63) & ~(size_t)63);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) return NULL;
    fclose(f);
    *n = (size_t)sz;
    return b;
}

static uint32_t texel(const uint8_t *base, const Tex *t, int x, int y)
{
    int rb = t->bpp == 4 ? t->w / 2 : t->w, xb = t->bpp == 4 ? x / 2 : x, idx;
    if (t->swz) {
        int bw = rb / 16;
        idx = (y / 8 * bw + xb / 16) * 128 + (y % 8) * 16 + xb % 16;
    } else idx = y * rb + xb;
    uint8_t v = base[t->pix_off + idx];
    int pi = t->bpp == 4 ? ((x & 1) ? v >> 4 : v & 15) : v;
    uint16_t c; memcpy(&c, base + t->clut_off + 2 * pi, 2);
    int r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
    return (c & 0x8000) ? (uint32_t)(r | g << 8 | b << 16 | 0xFF000000u) : 0;
}

static void tri(const float p[3][3], const float uv[3][2], const uint32_t col[3], const uint8_t *base, const Tex *t)
{
    float sx[3], sy[3];
    for (int i = 0; i < 3; i++) {
        if (p[i][2] < 0.3f) return;
        sx[i] = W / 2.0f + p[i][0] / p[i][2] * FOCAL;
        sy[i] = H / 2.0f - p[i][1] / p[i][2] * FOCAL;
    }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (fabsf(area) < 1e-6f) return;
    int x0 = (int)floorf(fminf(sx[0], fminf(sx[1], sx[2]))), x1 = (int)ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2])));
    int y0 = (int)floorf(fminf(sy[0], fminf(sy[1], sy[2]))), y1 = (int)ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2])));
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
            float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * p[0][2] + w1 * p[1][2] + w2 * p[2][2];
            if (z >= zb[y][x]) continue;
            uint32_t c;
            if (t) {
                float u = w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0], v = w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1];
                u -= floorf(u); v -= floorf(v);
                c = texel(base, t, (int)(u * t->w) % t->w, (int)(v * t->h) % t->h);
                if (!(c >> 24)) continue;
                float sh = (float)(col[0] & 255) / 255.0f;
                c = (uint32_t)((c & 255) * sh) | (uint32_t)(((c >> 8) & 255) * sh) << 8 | (uint32_t)(((c >> 16) & 255) * sh) << 16;
            } else c = col[0];
            zb[y][x] = z;
            fb[y][x][0] = (uint8_t)(c & 255); fb[y][x][1] = (uint8_t)((c >> 8) & 255); fb[y][x][2] = (uint8_t)((c >> 16) & 255);
        }
}

static void xf(const float m[16], const float v[3], float o[3])
{
    for (int i = 0; i < 3; i++) o[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2] + m[12 + i];
}

static void draw_mesh(const Vtx *verts, int nv, const uint16_t *batch_tex, const Batch *batches, int nb, const uint8_t *base, const Tex *tex, const float world[16], const float eye[3], const float look[3])
{
    float f[3] = { look[0] - eye[0], look[1] - eye[1], look[2] - eye[2] }, l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (int i = 0; i < 3; i++) f[i] /= l;
    float r[3] = { f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0 };       /* f x up(0,1,0) */
    l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int i = 0; i < 3; i++) r[i] /= l;
    float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    (void)batch_tex;
    for (int b = 0; b < nb; b++) {
        const Tex *t = &tex[batches ? batches[b].tex : 0];
        int first = batches ? (int)batches[b].first : 0, count = batches ? (int)batches[b].count : nv;
        for (int i = first; i + 2 < first + count; i += 3) {
            float p[3][3], uv[3][2];
            uint32_t col[3];
            for (int k = 0; k < 3; k++) {
                const Vtx *v = &verts[i + k];
                float w[3], d[3] = { 0, 0, 0 }, pt[3] = { v->x, v->y, v->z };
                xf(world, pt, w);
                for (int a = 0; a < 3; a++) d[a] = w[a] - eye[a];
                p[k][0] = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
                p[k][1] = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
                p[k][2] = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
                uv[k][0] = v->u; uv[k][1] = v->v; col[k] = v->color;
            }
            tri(p, uv, col, base, t);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 9) { printf("usage: scene_preview scenes.pak career.dat datadir scene line flip seconds out.ppm [clip]\n"); return 2; }
    size_t np, nc, ns, nbd;
    uint8_t *pb = slurp(argv[1], &np), *cb = slurp(argv[2], &nc);
    ScenePak pak; Career c;
    if (!pb || !cb || !scenepak_load(&pak, pb, np) || !career_load(&c, cb, nc)) { printf("data\n"); return 2; }
    int scene = atoi(argv[4]), line = atoi(argv[5]), flip = atoi(argv[6]);
    float secs = (float)atof(argv[7]);
    ScenePlayer sp;
    sceneplayer_init(&sp, &pak, scene, 5);
    sceneplayer_line(&sp, line, career_scene_nlines(&c, scene));
    sp.flip = flip;
    if (argc > 9) { sp.clip = atoi(argv[9]); sp.fade = 1.0f; sp.from_clip = -1; }
    sp.t = secs;
    char path[300];
    snprintf(path, sizeof(path), "%s/skater_%s.skt", argv[3], career_str(&c, c.skater[sp.guest].key_off));
    uint8_t *sb = slurp(path, &ns);
    snprintf(path, sizeof(path), "%s/board.brd", argv[3]);
    uint8_t *bb = slurp(path, &nbd);
    if (!sb || !bb) { printf("skater / board file\n"); return 2; }
    const Skt *sh = (const Skt *)sb;
    const Brd *bh = (const Brd *)bb;
    SkelBone bones[SKEL_NBONES];
    scenepak_bones(&pak, (const SkelBone *)(sb + sh->off_skel), bones);
    SkelFrame fr; BoardFrame bf; float m[SKEL_NBONES][12];
    sceneplayer_pose(&sp, &fr, &bf);
    skel_matrices(bones, &fr, sh->scale, m);
    Vtx *sv = malloc(sh->nverts * sizeof(Vtx));
    const Vtx *src = (const Vtx *)(sb + sh->off_verts);
    const uint8_t *vb = sb + sh->off_vbone;
    for (uint32_t i = 0; i < sh->nverts; i++) {
        sv[i] = src[i];
        const float *a = m[vb[i]];
        sv[i].x = a[0] * src[i].x + a[1] * src[i].y + a[2] * src[i].z + a[3];
        sv[i].y = a[4] * src[i].x + a[5] * src[i].y + a[6] * src[i].z + a[7];
        sv[i].z = a[8] * src[i].x + a[9] * src[i].y + a[10] * src[i].z + a[11];
    }
    float pos[3] = { 0, 0, 0 }, body[16], eye[3], look[3];
    sceneplayer_body(&sp, pos, body);
    sceneplayer_camera(&sp, pos, eye, look);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { zb[y][x] = 1e9f; fb[y][x][0] = 0x70; fb[y][x][1] = 0xB0; fb[y][x][2] = 0xE0; }
    float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    Vtx fl[6];                                                                      /* a floor: 8 x 8 m of grey with a mark at the origin and one 1 m towards +z (towards the camera) */
    float q[4][3] = { { -4, 0, -4 }, { 4, 0, -4 }, { 4, 0, 4 }, { -4, 0, 4 } };
    int ix[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++) { memset(&fl[i], 0, sizeof(fl[i])); fl[i].x = q[ix[i]][0]; fl[i].y = q[ix[i]][1]; fl[i].z = q[ix[i]][2]; fl[i].color = 0xFF707070; }
    Tex none = { 0 };
    {
        float p0[3][3], uv0[3][2] = { { 0 } };
        float f[3] = { look[0] - eye[0], look[1] - eye[1], look[2] - eye[2] };
        (void)p0; (void)f; (void)uv0;
    }
    Batch fb1 = { 0, 0, 0, 6, 0 };
    draw_mesh(fl, 6, NULL, &fb1, 1, NULL, &none, ident, eye, look);
    const Tex *stex = (const Tex *)(sb + sh->off_tex);
    draw_mesh(sv, (int)sh->nverts, NULL, NULL, 1, sb, stex, body, eye, look);
    float bm34[12], bm[16] = { 0 }, bmat[16];
    board_matrix(&bf, sh->scale, bm34);
    bm[0] = bm34[0]; bm[1] = bm34[4]; bm[2] = bm34[8]; bm[4] = bm34[1]; bm[5] = bm34[5]; bm[6] = bm34[9]; bm[8] = bm34[2]; bm[9] = bm34[6]; bm[10] = bm34[10];
    bm[12] = bm34[3]; bm[13] = bm34[7]; bm[14] = bm34[11]; bm[15] = 1.0f;
    for (int cc = 0; cc < 4; cc++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += body[k * 4 + rr] * bm[cc * 4 + k]; bmat[cc * 4 + rr] = s; }
    const Tex *btex = (const Tex *)(bb + bh->off_tex);
    const Batch *bbatch = (const Batch *)(bb + bh->off_batch);
    draw_mesh((const Vtx *)(bb + bh->off_verts), (int)bh->nverts, NULL, bbatch, 3, bb, btex, bmat, eye, look);
    FILE *o = fopen(argv[8], "wb");
    fprintf(o, "P6\n%d %d\n255\n", W, H);
    fwrite(fb, 1, sizeof(fb), o);
    fclose(o);
    printf("scene %d line %d: guest %s, clip %d (type %d, faces %.0f), flip %d, camera (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)\n", scene, line, career_str(&c, c.skater[sp.guest].key_off), sp.clip, pak.info[sp.clip].type, pak.info[sp.clip].face, flip, eye[0], eye[1], eye[2], look[0], look[1], look[2]);
    return 0;
}
