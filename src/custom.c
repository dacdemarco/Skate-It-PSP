#include "custom.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ATLAS_W 256
#define ATLAS_H 128
#define MESH_ENT 32
#define TEX_ENT 64
#define SKT_HDR 96
#define NBONES 22
enum { K_ARMS = 0, K_TOP, K_SHOES, K_LEGS, K_PANTS, K_FACE, K_HAIR, K_BEARD, K_LOGO, K_HAT, K_DECK, K_WHEELS, K_NKINDS };
enum { M_TOP = 0, M_PANTS, M_HEAD, M_HAIR, M_HAT };

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static size_t align16(size_t v) { return (v + 15) & ~(size_t)15; }

/* a texture entry: name[32], kind, sub, gender, skin, w, h, pad16, off_pix, off_pal */
typedef struct { const char *name; int kind, sub, gender, skin, w, h, rom; const uint8_t *pix, *pal; } Tex;

static int get_tex(const CustomPak *p, int i, Tex *t)
{
    if (i < 0 || (uint32_t)i >= p->ntex) return 0;
    const uint8_t *e = p->tex + (size_t)i * TEX_ENT;
    t->name = (const char *)e;
    t->kind = e[32]; t->sub = e[33]; t->gender = e[34]; t->skin = e[35]; t->w = e[36]; t->h = e[37]; t->rom = e[38] | (e[39] << 8);
    t->pix = p->file + rd32(e + 40); t->pal = p->file + rd32(e + 44);
    return 1;
}

static int list_of_kind(int kind)
{
    switch (kind) {
    case K_TOP: return CUST_TOP;
    case K_PANTS: return CUST_PANTS;
    case K_SHOES: return CUST_SHOES;
    case K_LOGO: return CUST_LOGO;
    case K_HAT: return CUST_HAT;
    case K_DECK: return CUST_DECK;
    case K_WHEELS: return CUST_WHEELS;
    default: return -1;
    }
}

int custom_pak_load(CustomPak *p, const uint8_t *buf, size_t size)
{
    memset(p, 0, sizeof(*p));
    if (size < 64 || memcmp(buf, "SKCP", 4) != 0 || (rd32(buf + 4) != 2 && rd32(buf + 4) != 3)) return 0;
    uint32_t nmesh = rd32(buf + 8), off_mesh = rd32(buf + 12), ntex = rd32(buf + 16), off_tex = rd32(buf + 20), total = rd32(buf + 24);
    if (total > size || nmesh > 4096 || ntex > 4096 || (uint64_t)off_mesh + (uint64_t)nmesh * MESH_ENT > size || (uint64_t)off_tex + (uint64_t)ntex * TEX_ENT > size) return 0;
    p->file = buf; p->size = size; p->nmesh = nmesh; p->ntex = ntex; p->mesh = buf + off_mesh; p->tex = buf + off_tex;
    for (uint32_t i = 0; i < nmesh; i++) {                       /* every mesh must lie inside the file */
        const uint8_t *e = p->mesh + (size_t)i * MESH_ENT;
        uint32_t nv = rd32(e + 4), ov = rd32(e + 8), ob = rd32(e + 12);
        if (e[0] > M_HAT || (uint64_t)ov + (uint64_t)nv * 24 > size || (uint64_t)ob + nv > size || nv % 3 || nv > 4000) return 0;
    }
    for (uint32_t i = 0; i < ntex; i++) {
        Tex t;
        get_tex(p, (int)i, &t);
        if (t.kind >= K_NKINDS || !t.w || !t.h || (uint64_t)(t.pix - buf) + (uint64_t)t.w * t.h > size || (uint64_t)(t.pal - buf) + 1024 > size) return 0;
        int l = list_of_kind(t.kind);
        if (l >= 0) {
            if (p->nlist[l] >= CUSTOM_MAXLIST) return 0;
            p->list[l][p->nlist[l]++] = (int)i;
        }
    }
    return p->nlist[CUST_TOP] > 0 && p->nlist[CUST_PANTS] > 0 && p->nlist[CUST_SHOES] > 0 && p->nlist[CUST_LOGO] > 0 && p->nlist[CUST_HAT] > 0;
}

int custom_count(const CustomPak *p, int list) { return list >= 0 && list < CUST_NLISTS ? p->nlist[list] : 0; }

const char *custom_name(const CustomPak *p, int list, int i)
{
    Tex t;
    if (list < 0 || list >= CUST_NLISTS || i < 0 || i >= p->nlist[list] || !get_tex(p, p->list[list][i], &t)) return "";
    return t.name;
}

int custom_rom(const CustomPak *p, int list, int i)
{
    Tex t;
    if (list < 0 || list >= CUST_NLISTS || i < 0 || i >= p->nlist[list] || !get_tex(p, p->list[list][i], &t)) return -1;
    return t.rom == 0xFFFF ? -1 : t.rom;
}

int custom_hat_family(const CustomPak *p, int i)
{
    Tex t;
    return i >= 0 && i < p->nlist[CUST_HAT] && get_tex(p, p->list[CUST_HAT][i], &t) ? t.sub : 0;
}

/* the caps (forwards, backwards, for long hair) wear the baseball cap designs, the fedora and the flat cap the diver ones, the helmet the helmets [guess: the original has one item per cap design and
 * one per hat of the others, but the files do not say which design goes on which model] */
int custom_hat_design_ok(const CustomPak *p, int hat, int i)
{
    int fam = custom_hat_family(p, i);
    if (hat == 6) return fam == 2;
    if (hat == 4 || hat == 5) return fam == 1;
    return hat >= 1 && hat <= 3 && fam == 0;
}

int custom_unlock_kind(int list)
{
    switch (list) {
    case CUST_SHOES: return 5;
    case CUST_HAT: return 2;
    case CUST_DECK: return 8;
    case CUST_WHEELS: return 7;
    default: return -1;
    }
}

void custom_default(const CustomPak *p, CustomSel *s)
{
    (void)p;
    memset(s, 0, sizeof(*s));
}

int custom_valid(const CustomPak *p, const CustomSel *s)
{
    return s->gender < 2 && s->build < CUSTOM_NBUILD && s->skin < CUSTOM_NSKIN && s->face < CUSTOM_NFACE && s->hair < CUSTOM_NHAIR && s->hair_col < CUSTOM_NCOL && s->beard < CUSTOM_NBEARD && s->beard_col < CUSTOM_NCOL &&
           s->hat < CUSTOM_NHAT && custom_hat_exists(s->gender, s->hat) && s->hat_tex < p->nlist[CUST_HAT] && s->top < p->nlist[CUST_TOP] && s->logo <= p->nlist[CUST_LOGO] && s->pants < p->nlist[CUST_PANTS] && s->shoes < p->nlist[CUST_SHOES] && s->deck <= p->nlist[CUST_DECK] && s->wheels <= p->nlist[CUST_WHEELS];
}

const char *custom_gender_name(int g) { return g == 1 ? "Female" : "Male"; }
const char *custom_build_name(int b) { return b == 1 ? "Stocky" : (b == 2 ? "Skinny" : "Average"); }
const char *custom_colour_name(int c) { return c == 1 ? "Blonde" : (c == 2 ? "Brown" : "Black"); }

const char *custom_hair_name(int h)
{
    static const char *const n[CUSTOM_NHAIR] = { "None", "Afro", "Curtains", "Emo", "Fauxhawk", "Fringe", "Long", "Long, tied back", "Mohawk", "Short" };
    return h >= 0 && h < CUSTOM_NHAIR ? n[h] : "";
}

const char *custom_hat_name(int h)
{
    static const char *const n[CUSTOM_NHAT] = { "None", "Cap", "Cap, backwards", "Cap, long hair", "Fedora", "Flat cap", "Helmet" };
    return h >= 0 && h < CUSTOM_NHAT ? n[h] : "";
}

/* ---- finding the parts ---------------------------------------------------------------------------------------------------------------------------- */
static int find_mesh(const CustomPak *p, int kind, int gender, int a, int var)
{
    for (uint32_t i = 0; i < p->nmesh; i++) {
        const uint8_t *e = p->mesh + (size_t)i * MESH_ENT;
        if (e[0] == kind && e[1] == gender && e[2] == a && e[3] == var) return (int)i;
    }
    return -1;
}

static int find_tex(const CustomPak *p, int kind, int sub, int gender, int skin)
{
    for (uint32_t i = 0; i < p->ntex; i++) {
        Tex t;
        get_tex(p, (int)i, &t);
        if (t.kind == kind && t.sub == sub && (gender < 0 || t.gender == gender) && (skin < 0 || t.skin == skin)) return (int)i;
    }
    return -1;
}

/* ---- the atlas -------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct { uint16_t c[ATLAS_W * ATLAS_H]; } Atlas;       /* 15-bit RGB, R in the low bits (the PSP's 5551 without the alpha bit) */

static uint16_t rgb15(const uint8_t *pal, int idx)
{
    const uint8_t *q = pal + idx * 4;
    return (uint16_t)((q[0] >> 3) | ((q[1] >> 3) << 5) | ((q[2] >> 3) << 10));
}

static void paste(Atlas *a, const Tex *t, int x0, int row0, int keyed)
{
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++) {
            int idx = t->pix[y * t->w + x];
            if (keyed && idx == 0) continue;                    /* palette entry 0 of a top or pants texture: the skin below shows */
            int ax = x0 + x, ay = row0 + y;
            if (ax >= ATLAS_W || ay >= ATLAS_H) continue;
            a->c[ay * ATLAS_W + ax] = rgb15(t->pal, idx);
        }
}

/* an A3I5 overlay (index = byte & 31, alpha = byte >> 5 of 7) blended over what is there; `flip` mirrors it left to right */
static void blend(Atlas *a, const Tex *t, int x0, int row0, int flip)
{
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++) {
            int v = t->pix[y * t->w + x], al = v >> 5;
            if (!al) continue;
            int ax = x0 + (flip ? t->w - 1 - x : x), ay = row0 + y;
            if (ax >= ATLAS_W || ay >= ATLAS_H) continue;
            uint16_t s = rgb15(t->pal, v & 31), *d = &a->c[ay * ATLAS_W + ax], dv = *d;
            int r = ((dv & 31) * (7 - al) + (s & 31) * al + 3) / 7, g = (((dv >> 5) & 31) * (7 - al) + ((s >> 5) & 31) * al + 3) / 7, b = (((dv >> 10) & 31) * (7 - al) + ((s >> 10) & 31) * al + 3) / 7;
            *d = (uint16_t)(r | (g << 5) | (b << 10));
        }
}

static int cmp_desc(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? 1 : (x > y ? -1 : 0);
}

/* the colours of the atlas as at most 256 palette entries: the 256 most used ones, every other colour mapped to the nearest of them */
static void palettize(const Atlas *a, uint16_t clut[256], uint8_t *pix)
{
    static uint32_t count[32768];
    static int16_t slot[32768];
    memset(count, 0, sizeof(count));
    for (int i = 0; i < ATLAS_W * ATLAS_H; i++) count[a->c[i]]++;
    int ncol = 0;
    for (int c = 0; c < 32768; c++) ncol += count[c] != 0;
    uint16_t pick[256];
    int npick = 0;
    for (int c = 0; c < 32768; c++) slot[c] = -1;
    if (ncol <= 256) {
        for (int c = 0; c < 32768; c++) if (count[c]) { slot[c] = (int16_t)npick; pick[npick++] = (uint16_t)c; }
    } else {
        static uint64_t key[32768];                              /* the colours by use, most used first: count << 15 | colour */
        int nk = 0;
        for (int c = 0; c < 32768; c++) if (count[c]) key[nk++] = ((uint64_t)count[c] << 15) | (uint64_t)c;
        qsort(key, (size_t)nk, sizeof(key[0]), cmp_desc);
        for (int k = 0; k < 256 && k < nk; k++) { int c = (int)(key[k] & 32767); slot[c] = (int16_t)npick; pick[npick++] = (uint16_t)c; }
        for (int c = 0; c < 32768; c++) {
            if (!count[c] || slot[c] >= 0) continue;
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31, bi = 0, bd = 1 << 30;
            for (int k = 0; k < npick; k++) {
                int dr = r - (pick[k] & 31), dg = g - ((pick[k] >> 5) & 31), db = b - ((pick[k] >> 10) & 31), d = dr * dr * 2 + dg * dg * 4 + db * db * 3;
                if (d < bd) { bd = d; bi = k; }
            }
            slot[c] = (int16_t)bi;
        }
    }
    for (int k = 0; k < 256; k++) clut[k] = k < npick ? (uint16_t)(pick[k] | 0x8000) : 0x8000;
    for (int i = 0; i < ATLAS_W * ATLAS_H; i++) pix[i] = (uint8_t)slot[a->c[i]];
}

/* the hair texture type a style uses: the afro and the short cut the curly one (1), the others the smooth one (2) */
static int hair_tex_type(int style) { return style == 0 || style == 8 ? 1 : 2; }

/* ---- hats ----------------------------------------------------------------------------------------------------------------------------------------------- */
/* The hat meshes dip 4 - 12 % into the head here and there, and hair (afro, mohawk ...) comes out through them: with a hat on, every hat vertex is moved out to just clear the head (seen from the
 * centre of the head, in its own direction) and every hair vertex under the hat is pulled in below the hat's surface. All of them are on the head bone. */
#define FIT_MAX 1024
static float *vtx(uint8_t *verts, uint32_t i) { return (float *)(void *)(verts + (size_t)i * 24 + 12); }

typedef struct { float d[FIT_MAX][3], r[FIT_MAX]; int n; } Shell;

static void shell_of(Shell *s, uint8_t *verts, const uint8_t *vbone, uint32_t first, uint32_t count, const float c[3])
{
    s->n = 0;
    for (uint32_t i = first; i < first + count && s->n < FIT_MAX; i++) {
        if (vbone[i] != 18) continue;
        const float *p = vtx(verts, i);
        float d[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] }, r = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (r < 1e-6f) continue;
        for (int k = 0; k < 3; k++) s->d[s->n][k] = d[k] / r;
        s->r[s->n++] = r;
    }
}

/* the radius of the shell in the direction d: the largest (max = 1) or the mean of the vertices within about 15 degrees; 0 if there are none */
static float shell_radius(const Shell *s, const float d[3], int max)
{
    float acc = 0.0f;
    int cnt = 0;
    for (int j = 0; j < s->n; j++) {
        if (s->d[j][0] * d[0] + s->d[j][1] * d[1] + s->d[j][2] * d[2] < 0.97f) continue;
        if (max) { if (s->r[j] > acc) acc = s->r[j]; cnt = 1; }
        else { acc += s->r[j]; cnt++; }
    }
    return cnt ? (max ? acc : acc / (float)cnt) : 0.0f;
}

static void fit_hat(uint8_t *verts, const uint8_t *vbone, uint32_t h0, uint32_t hn, uint32_t r0, uint32_t rn, uint32_t t0, uint32_t tn)
{
    static Shell head, hat;
    float c[3] = { 0, 0, 0 };
    int n = 0;
    for (uint32_t i = h0; i < h0 + hn; i++) if (vbone[i] == 18) { const float *p = vtx(verts, i); c[0] += p[0]; c[1] += p[1]; c[2] += p[2]; n++; }
    if (!n) return;
    for (int k = 0; k < 3; k++) c[k] /= (float)n;
    shell_of(&head, verts, vbone, h0, hn, c);
    float xlo = 1e9f, xhi = -1e9f;                                     /* the head's long axis is the bone's x: up. Hats sit a little higher (4 % of the head's height: hardware reports, 2 % was not enough) */
    for (uint32_t i = h0; i < h0 + hn; i++) if (vbone[i] == 18) { float x = vtx(verts, i)[0]; if (x < xlo) xlo = x; if (x > xhi) xhi = x; }
    for (uint32_t i = t0; i < t0 + tn; i++) if (vbone[i] == 18) vtx(verts, i)[0] += 0.04f * (xhi - xlo);
    for (uint32_t i = t0; i < t0 + tn; i++) {                         /* the hat clears the head by 2 % */
        if (vbone[i] != 18) continue;
        float *p = vtx(verts, i);
        float d[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] }, r = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (r < 1e-6f) continue;
        for (int k = 0; k < 3; k++) d[k] /= r;
        float hr = shell_radius(&head, d, 1) * 1.02f;
        if (hr > 0.0f && r < hr) for (int k = 0; k < 3; k++) p[k] = c[k] + d[k] * hr;
    }
    shell_of(&hat, verts, vbone, t0, tn, c);
    for (uint32_t i = r0; i < r0 + rn; i++) {                          /* the hair under it comes in below its surface */
        if (vbone[i] != 18) continue;
        float *p = vtx(verts, i);
        float d[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] }, r = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (r < 1e-6f) continue;
        for (int k = 0; k < 3; k++) d[k] /= r;
        float tr = shell_radius(&hat, d, 0) * 0.96f;
        if (tr > 0.0f && r > tr) for (int k = 0; k < 3; k++) p[k] = c[k] + d[k] * tr;
    }
}

int custom_part_counts(const CustomPak *p, const CustomSel *s, uint32_t out[5])
{
    memset(out, 0, 5 * sizeof(uint32_t));
    if (!custom_valid(p, s)) return 0;
    Tex top, pants;
    if (!get_tex(p, p->list[CUST_TOP][s->top], &top) || !get_tex(p, p->list[CUST_PANTS][s->pants], &pants)) return 0;
    int m[5] = { find_mesh(p, M_TOP, s->gender, s->build, top.sub), find_mesh(p, M_PANTS, s->gender, s->build, pants.sub), find_mesh(p, M_HEAD, s->gender, s->face / 2, 0),
                 s->hair ? find_mesh(p, M_HAIR, s->gender, s->hair - 1, 0) : -1, s->hat ? find_mesh(p, M_HAT, s->gender, s->hat - 1, 0) : -1 };
    for (int k = 0; k < 5; k++) {
        if (m[k] < 0) { if (k < 3 || (k == 3 && s->hair) || (k == 4 && s->hat)) return 0; continue; }
        out[k] = rd32(p->mesh + (size_t)m[k] * MESH_ENT + 4);
    }
    return 1;
}

uint8_t *custom_build(const CustomPak *p, const CustomSel *s, const uint8_t *base, size_t base_size, size_t *out_size)
{
    if (!custom_valid(p, s) || base_size < SKT_HDR + 64 || memcmp(base, "SKS2", 4) != 0 || rd32(base + 4) != 2 || rd32(base + 12) != NBONES) return NULL;
    Tex top, pants, shoes, arms, legs, face;
    if (!get_tex(p, p->list[CUST_TOP][s->top], &top) || !get_tex(p, p->list[CUST_PANTS][s->pants], &pants) || !get_tex(p, p->list[CUST_SHOES][s->shoes], &shoes)) return NULL;
    int ia = find_tex(p, K_ARMS, 0, -1, s->skin + 1), il = find_tex(p, K_LEGS, (pants.sub == 1 || pants.sub == 2) ? 1 : 0, -1, s->skin + 1), ifc = find_tex(p, K_FACE, s->face % 2 + 1, s->gender, s->skin + 1);
    int mt = find_mesh(p, M_TOP, s->gender, s->build, top.sub), mp = find_mesh(p, M_PANTS, s->gender, s->build, pants.sub), mh = find_mesh(p, M_HEAD, s->gender, s->face / 2, 0);
    int mhair = s->hair ? find_mesh(p, M_HAIR, s->gender, s->hair - 1, 0) : -1, mhat = s->hat ? find_mesh(p, M_HAT, s->gender, s->hat - 1, 0) : -1;
    if (ia < 0 || il < 0 || ifc < 0 || mt < 0 || mp < 0 || mh < 0 || (s->hair && mhair < 0) || (s->hat && mhat < 0) || !get_tex(p, ia, &arms) || !get_tex(p, il, &legs) || !get_tex(p, ifc, &face)) return NULL;
    Tex logo, beard, hair, hat;
    int has_logo = s->logo > 0, has_beard = s->beard > 0, has_hair = s->hair > 0, has_hat = s->hat > 0;
    if (has_logo && !get_tex(p, p->list[CUST_LOGO][s->logo - 1], &logo)) return NULL;
    if (has_hat && !get_tex(p, p->list[CUST_HAT][s->hat_tex], &hat)) return NULL;
    if (has_beard) { int ib = find_tex(p, K_BEARD, s->beard, -1, s->beard_col + 1); if (ib < 0 || !get_tex(p, ib, &beard)) return NULL; }
    if (has_hair) { int ih = find_tex(p, K_HAIR, hair_tex_type(s->hair - 1), -1, s->hair_col + 1); if (ih < 0 || !get_tex(p, ih, &hair)) return NULL; }

    Atlas *atlas = calloc(1, sizeof(*atlas));
    uint8_t *pix = malloc(ATLAS_W * ATLAS_H);
    if (!atlas || !pix) { free(atlas); free(pix); return NULL; }
    paste(atlas, &arms, 42, 0, 0);
    paste(atlas, &top, 42, 0, 1);
    if (has_logo) blend(atlas, &logo, 64, 0, 1);                 /* on the front of the top (its left island of the shirt texture), mirrored */
    paste(atlas, &shoes, 192, 0, 0);
    paste(atlas, &legs, 42, 64, 0);
    paste(atlas, &pants, 42, 64, 1);
    paste(atlas, &face, 124, 64, 0);
    if (has_beard) blend(atlas, &beard, 124, 64, 0);
    if (has_hair) blend(atlas, &hair, 124, 64, 0);               /* (the hair meshes use its right-hand 32 columns) */
    if (has_hat) paste(atlas, &hat, 224, 64, 0);
    uint16_t clut[256];
    palettize(atlas, clut, pix);
    free(atlas);

    int meshes[5], nm = 0;
    meshes[nm++] = mt; meshes[nm++] = mp; meshes[nm++] = mh;
    if (has_hair) meshes[nm++] = mhair;
    if (has_hat) meshes[nm++] = mhat;
    uint32_t nv = 0;
    for (int k = 0; k < nm; k++) nv += rd32(p->mesh + (size_t)meshes[k] * MESH_ENT + 4);
    size_t off_tex = SKT_HDR, clut_off = align16(off_tex + 24), pix_off = align16(clut_off + 512), off_verts = align16(pix_off + ATLAS_W * ATLAS_H);
    size_t off_vbone = align16(off_verts + (size_t)nv * 24), off_skel = align16(off_vbone + nv);
    uint32_t old_skel = rd32(base + 32), old_anims = rd32(base + 36), nanims = rd32(base + 16);
    if (old_skel > base_size || old_anims < old_skel || (uint64_t)old_anims + (uint64_t)nanims * 16 > base_size) { free(pix); return NULL; }
    size_t tail = base_size - old_skel;
    size_t total = off_skel + tail;
    uint8_t *out = calloc(1, total);
    if (!out) { free(pix); return NULL; }
    memcpy(out, base, SKT_HDR);
    wr32(out + 8, nv);
    wr32(out + 20, (uint32_t)off_tex); wr32(out + 24, (uint32_t)off_verts); wr32(out + 28, (uint32_t)off_vbone); wr32(out + 32, (uint32_t)off_skel); wr32(out + 36, (uint32_t)(off_skel + (old_anims - old_skel)));
    uint8_t *te = out + off_tex;                                /* TexEntry: u16 w, h; u8 bpp, swz, alpha, pad; u32 clut_off, pix_off, n, pad2 */
    te[0] = ATLAS_W & 255; te[1] = ATLAS_W >> 8; te[2] = ATLAS_H & 255; te[3] = ATLAS_H >> 8; te[4] = 8; te[5] = 0; te[6] = 0; te[7] = 0;
    wr32(te + 8, (uint32_t)clut_off); wr32(te + 12, (uint32_t)pix_off); wr32(te + 16, 256); wr32(te + 20, 0);
    for (int k = 0; k < 256; k++) { out[clut_off + (size_t)k * 2] = (uint8_t)clut[k]; out[clut_off + (size_t)k * 2 + 1] = (uint8_t)(clut[k] >> 8); }
    memcpy(out + pix_off, pix, ATLAS_W * ATLAS_H);
    free(pix);
    size_t vo = off_verts, bo = off_vbone;
    uint32_t first[5] = { 0, 0, 0, 0, 0 }, count[5] = { 0, 0, 0, 0, 0 }, at = 0;
    for (int k = 0; k < nm; k++) {
        const uint8_t *e = p->mesh + (size_t)meshes[k] * MESH_ENT;
        uint32_t n = rd32(e + 4);
        first[k] = at; count[k] = n; at += n;
        memcpy(out + vo, p->file + rd32(e + 8), (size_t)n * 24); vo += (size_t)n * 24;
        memcpy(out + bo, p->file + rd32(e + 12), n); bo += n;
    }
    for (size_t i = 0; i < nv; i++) if (out[off_vbone + i] >= NBONES) { free(out); return NULL; }
    if (has_hat) fit_hat(out + off_verts, out + off_vbone, first[2], count[2], has_hair ? first[3] : 0, has_hair ? count[3] : 0, first[nm - 1], count[nm - 1]);
    memcpy(out + off_skel, base + old_skel, tail);              /* the skeleton, the animation table and the frames */
    int64_t delta = (int64_t)off_skel - (int64_t)old_skel;
    uint8_t *tab = out + off_skel + (old_anims - old_skel);
    for (uint32_t i = 0; i < nanims; i++) {                      /* the table holds absolute offsets of the frames */
        uint8_t *e = tab + (size_t)i * 16;
        wr32(e + 4, (uint32_t)((int64_t)rd32(e + 4) + delta));
        wr32(e + 12, (uint32_t)((int64_t)rd32(e + 12) + delta));
    }
    *out_size = total;
    return out;
}

/* ---- the board ---------------------------------------------------------------------------------------------------------------------------------------- */
static int find_named(const CustomPak *p, int list, const char *name)
{
    for (int i = 0; i < p->nlist[list]; i++)
        if (strcmp(custom_name(p, list, i), name) == 0) return i;
    return 0;
}

int custom_board_texture(const CustomPak *p, const CustomSel *s, uint8_t pix[64 * 128], uint16_t clut[256])
{
    if (p->nlist[CUST_DECK] <= 0 || p->nlist[CUST_WHEELS] <= 0) return 0;
    int di = s->deck ? s->deck - 1 : find_named(p, CUST_DECK, "Generic Toon"), wi = s->wheels ? s->wheels - 1 : find_named(p, CUST_WHEELS, "Generic White");
    Tex deck, wheel;
    if (di >= p->nlist[CUST_DECK] || wi >= p->nlist[CUST_WHEELS] || !get_tex(p, p->list[CUST_DECK][di], &deck) || !get_tex(p, p->list[CUST_WHEELS][wi], &wheel)) return 0;
    if (deck.w != 42 || deck.h != 105 || wheel.w != 42 || wheel.h != 23) return 0;
    uint16_t colour[256];
    int ncol = 0;
    memset(pix, 0, 64 * 128);
    for (int part = 0; part < 2; part++) {                           /* the deck on rows 0 .. 104, the wheels under it on rows 105 .. 127 */
        const Tex *t = part ? &wheel : &deck;
        int row0 = part ? 105 : 0;
        for (int y = 0; y < t->h; y++)
            for (int x = 0; x < t->w; x++) {
                const uint8_t *q = t->pal + t->pix[y * t->w + x] * 4;
                if (q[0] == 255 && q[1] == 0 && q[2] == 255) continue;           /* the colour key: magenta is transparent */
                uint16_t c = rgb15(t->pal, t->pix[y * t->w + x]);
                int k = 0;
                while (k < ncol && colour[k] != c) k++;
                if (k == ncol) {
                    if (ncol >= 255) k = ncol - 1;                      /* (never happens: the art has 33 colours at most) */
                    else colour[ncol++] = c;
                }
                pix[(row0 + y) * 64 + x] = (uint8_t)(k + 1);
            }
    }
    for (int k = 0; k < 256; k++) clut[k] = k >= 1 && k <= ncol ? (uint16_t)(colour[k - 1] | 0x8000) : 0;
    return 1;
}
