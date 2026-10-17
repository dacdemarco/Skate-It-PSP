/* Host test: the custom skater (src/custom.c) on the real data/custom.pak and a real pro skater file as the base: every choice builds a valid .skt (offsets, vertex bones, relocated animation table,
 * atlas colours) that the game's loader would accept.
 * usage: test_custom <custom.pak> <skater_aaron.skt> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/custom.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz); if (fread(b, 1, (size_t)sz, f) != (size_t)sz) exit(2); fclose(f);
    *n = (size_t)sz;
    return b;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

static unsigned rng_state = 7;
static unsigned rng(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

/* what the game's loader (src/main.c load_skater_file) and the renderer rely on */
static int check_skt(const uint8_t *f, size_t size, const uint8_t *base, size_t base_size, const char *what, int *nverts_out)
{
    int ok = 1;
#define REQ(c, ...) do { if (!(c)) { printf("FAIL: %s: ", what); printf(__VA_ARGS__); printf("\n"); fails++; ok = 0; } } while (0)
    REQ(size > 100000 && memcmp(f, "SKS2", 4) == 0 && rd32(f + 4) == 2 && rd32(f + 12) == 22 && rd32(f + 16) == 376, "header");
    if (!ok) return 0;
    uint32_t nv = rd32(f + 8), off_tex = rd32(f + 20), off_v = rd32(f + 24), off_vb = rd32(f + 28), off_skel = rd32(f + 32), off_an = rd32(f + 36);
    *nverts_out = (int)nv;
    REQ(nv >= 900 && nv <= 3000 && nv % 3 == 0, "%u vertices", nv);
    REQ(off_tex == 96 && off_v % 16 == 0 && off_vb % 16 == 0 && off_skel % 16 == 0 && off_v + nv * 24 <= off_vb && off_vb + nv <= off_skel && off_skel + 22 * 16 <= off_an && off_an + 376 * 16 <= size, "section offsets");
    if (!ok) return 0;
    const uint8_t *te = f + off_tex;
    uint32_t clut_off = rd32(te + 8), pix_off = rd32(te + 12);
    REQ(te[0] == 0 && te[1] == 1 && te[2] == 128 && te[3] == 0 && te[4] == 8 && rd32(te + 16) == 256, "the texture is 256 x 128 T8 with 256 colours");
    REQ(clut_off + 512 <= pix_off && pix_off + 32768 <= off_v, "texture data inside the file");
    for (uint32_t i = 0; i < nv; i++) {
        const uint8_t *v = f + off_v + i * 24;
        float u = rdf(v), w = rdf(v + 4), x = rdf(v + 12), y = rdf(v + 16), z = rdf(v + 20);
        if (!(isfinite(u) && isfinite(w) && isfinite(x) && isfinite(y) && isfinite(z) && u >= -0.01f && u <= 1.01f && w >= -1.01f && w <= 0.01f && fabsf(x) < 0.2f && fabsf(y) < 0.2f && fabsf(z) < 0.2f)) { REQ(0, "vertex %u is out of range (%.3f %.3f | %.3f %.3f %.3f)", i, u, w, x, y, z); break; }
        if (f[off_vb + i] >= 22) { REQ(0, "vertex %u has bone %d", i, f[off_vb + i]); break; }
    }
    /* the skeleton and every animation are the base skater's, found through the relocated table */
    uint32_t bskel = rd32(base + 32), ban = rd32(base + 36);
    REQ(memcmp(f + off_skel, base + bskel, 22 * 16) == 0, "the skeleton is the base's");
    int same = 1;
    for (uint32_t a = 0; a < 376 && same; a++) {
        const uint8_t *e = f + off_an + a * 16, *be = base + ban + a * 16;
        uint32_t nf = rd32(e), fo = rd32(e + 4), bnf = rd32(e + 8), bo = rd32(e + 12);
        uint32_t onf = rd32(be), ofo = rd32(be + 4), obnf = rd32(be + 8), obo = rd32(be + 12);
        if (nf != onf || bnf != obnf || (uint64_t)fo + (uint64_t)nf * 364 > size || (uint64_t)bo + (uint64_t)bnf * 28 > size || (uint64_t)ofo + (uint64_t)onf * 364 > base_size) { same = 0; break; }
        if (memcmp(f + fo, base + ofo, (size_t)nf * 364) != 0 || memcmp(f + bo, base + obo, (size_t)bnf * 28) != 0) same = 0;
    }
    REQ(same, "the animations (frames and board frames) are the base's");
    return ok;
}

int main(int argc, char **argv)
{
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    size_t pn, bn;
    unsigned char *pb = slurp(argv[1], &pn), *bb = slurp(argv[2], &bn);
    CustomPak pak;
    CHECK(custom_pak_load(&pak, pb, pn), "custom.pak loads");
    CHECK(custom_count(&pak, CUST_TOP) == 35 && custom_count(&pak, CUST_PANTS) == 28 && custom_count(&pak, CUST_SHOES) == 24, "35 tops, 28 bottoms, 24 pairs of shoes (%d %d %d)", custom_count(&pak, CUST_TOP), custom_count(&pak, CUST_PANTS), custom_count(&pak, CUST_SHOES));
    CHECK(pak.nmesh == 85 && pak.ntex == 349, "85 meshes, 349 textures (%u %u)", pak.nmesh, pak.ntex);
    CHECK(custom_count(&pak, CUST_DECK) == 61 && custom_count(&pak, CUST_WHEELS) == 53, "61 decks, 53 wheel designs (%d %d)", custom_count(&pak, CUST_DECK), custom_count(&pak, CUST_WHEELS));
    {   /* the original's item numbers: each gear list holds them once (shoes 0 .. 23, hat designs 1 .. 20, decks 0 .. 59, wheels 0 .. 51) and the one file the tables do not name has none */
        static const struct { int list, first, count, none; } G[4] = { { CUST_SHOES, 0, 24, 0 }, { CUST_HAT, 1, 20, 0 }, { CUST_DECK, 0, 60, 1 }, { CUST_WHEELS, 0, 52, 1 } };
        for (int g = 0; g < 4; g++) {
            int seen[80] = { 0 }, nones = 0;
            for (int i = 0; i < custom_count(&pak, G[g].list); i++) {
                int r = custom_rom(&pak, G[g].list, i);
                if (r < 0) { nones++; continue; }
                CHECK(r >= G[g].first && r < G[g].first + G[g].count && !seen[r], "list %d: item %d has the number %d (twice or out of range)", G[g].list, i, r);
                if (r >= 0 && r < 80) seen[r] = 1;
            }
            CHECK(nones == G[g].none, "list %d: %d items without a number (%d expected)", G[g].list, nones, G[g].none);
        }
        int fam[3] = { 0, 0, 0 };
        for (int i = 0; i < custom_count(&pak, CUST_HAT); i++) fam[custom_hat_family(&pak, i)]++;
        CHECK(fam[0] == 15 && fam[1] == 2 && fam[2] == 3, "hat designs: %d caps, %d divers, %d helmets", fam[0], fam[1], fam[2]);
        CHECK(custom_unlock_kind(CUST_SHOES) == 5 && custom_unlock_kind(CUST_HAT) == 2 && custom_unlock_kind(CUST_DECK) == 8 && custom_unlock_kind(CUST_WHEELS) == 7 && custom_unlock_kind(CUST_TOP) < 0, "unlock kinds of the gear lists");
    }
    CHECK(custom_count(&pak, CUST_LOGO) == 70 && custom_count(&pak, CUST_HAT) == 20, "70 logos, 20 hat designs (%d %d)", custom_count(&pak, CUST_LOGO), custom_count(&pak, CUST_HAT));
    { CustomPak q; CHECK(!custom_pak_load(&q, pb, 40) && !custom_pak_load(&q, bb, bn), "a short file and a skater file are refused");
      unsigned char *bad = malloc(pn); memcpy(bad, pb, pn); bad[4] = 4; CHECK(!custom_pak_load(&q, bad, pn), "version 4 is refused");
      memcpy(bad, pb, pn); bad[12] = 0xFF; bad[13] = 0xFF; bad[14] = 0xFF; CHECK(!custom_pak_load(&q, bad, pn), "a mesh table past the end is refused"); free(bad); }
    for (int l = 0; l < CUST_NLISTS; l++)
        for (int i = 0; i < custom_count(&pak, l); i++) CHECK(custom_name(&pak, l, i)[0], "list %d entry %d has a name", l, i);
    CHECK(custom_name(&pak, CUST_TOP, 99)[0] == 0 && custom_name(&pak, 9, 0)[0] == 0, "bad indices give no name");
    printf("some names: top \"%s\", \"%s\"; bottom \"%s\"; shoes \"%s\"\n", custom_name(&pak, CUST_TOP, 0), custom_name(&pak, CUST_TOP, 30), custom_name(&pak, CUST_PANTS, 5), custom_name(&pak, CUST_SHOES, 3));
    CHECK(strcmp(custom_gender_name(0), "Male") == 0 && strcmp(custom_gender_name(1), "Female") == 0 && custom_build_name(0)[0] && custom_build_name(1)[0] && custom_build_name(2)[0], "names");

    if (argc > 3) {   /* the board texture: the default art (Toon + white wheels) must be the picture the converter put into board.brd (compared as colours: the palettes are ordered differently, the PSP layout is swizzled) */
        size_t brn;
        unsigned char *br = slurp(argv[3], &brn);
        uint32_t off_tex = rd32(br + 16);
        const uint8_t *te = br + off_tex;
        uint32_t clut_off = rd32(te + 8), pix_off = rd32(te + 12);
        CHECK(te[0] == 64 && te[1] == 0 && te[2] == 128 && te[3] == 0 && te[4] == 8 && te[5] == 1, "board.brd texture 0 is 64 x 128, T8, swizzled");
        static uint8_t pix[64 * 128];
        static uint16_t clut[256];
        CustomSel s0;
        memset(&s0, 0, sizeof(s0));
        CHECK(custom_board_texture(&pak, &s0, pix, clut), "the default board texture is made");
        int diff = 0, opaque = 0;
        for (int j = 0; j < 128; j++)
            for (int i = 0; i < 64; i++) {
                int bx = i / 16, by = j / 8, x = i % 16, y = j % 8;
                int ref_idx = br[pix_off + (bx + by * 4) * 128 + y * 16 + x];
                uint16_t ref = ref_idx ? (uint16_t)(br[clut_off + ref_idx * 2] | (br[clut_off + ref_idx * 2 + 1] << 8)) : 0;
                uint16_t mine = pix[j * 64 + i] ? clut[pix[j * 64 + i]] : 0;
                if ((ref_idx != 0) != (pix[j * 64 + i] != 0) || ref != mine) diff++;
                if (pix[j * 64 + i]) opaque++;
            }
        CHECK(diff == 0 && opaque > 3000, "the default board texture equals board.brd's: %d pixels differ, %d opaque", diff, opaque);
        CHECK(clut[0] == 0, "palette entry 0 is transparent");
        /* any deck with any wheels: the art lands where the board's UVs expect it, the key stays transparent, at most 255 colours */
        int bad = 0;
        for (int d = 0; d <= custom_count(&pak, CUST_DECK); d += 3)
            for (int w = 0; w <= custom_count(&pak, CUST_WHEELS); w += 5) {
                CustomSel s1;
                memset(&s1, 0, sizeof(s1));
                s1.deck = (uint8_t)d; s1.wheels = (uint8_t)w;
                if (!custom_board_texture(&pak, &s1, pix, clut)) { bad++; continue; }
                for (int k = 0; k < 64 * 128; k++) if (pix[k] > 100) bad++;
                for (int j = 0; j < 128; j++) for (int i = 42; i < 64; i++) if (pix[j * 64 + i]) bad++;        /* (columns 42 .. 63 are unused) */
            }
        CHECK(bad == 0, "%d problems in the board textures of the deck / wheel combinations", bad);
        s0.deck = 200;
        CHECK(!custom_board_texture(&pak, &s0, pix, clut), "a deck that does not exist is refused");
        free(br);
    }

    CustomSel sel;
    custom_default(&pak, &sel);
    CHECK(custom_valid(&pak, &sel), "the default choice is valid");
    { CustomSel b = sel; b.gender = 2; CHECK(!custom_valid(&pak, &b), "gender 2");
      b = sel; b.top = 35; CHECK(!custom_valid(&pak, &b), "top 35"); b = sel; b.hair = 10; CHECK(!custom_valid(&pak, &b), "hair 10"); b = sel; b.beard = 9; CHECK(!custom_valid(&pak, &b), "beard 9"); b = sel; b.hat = 7; CHECK(!custom_valid(&pak, &b), "hat 7"); b = sel; b.logo = 71; CHECK(!custom_valid(&pak, &b), "logo 71"); b = sel; b.face = 8; CHECK(!custom_valid(&pak, &b), "face 8"); b = sel; b.hat_tex = 20; CHECK(!custom_valid(&pak, &b), "hat design 20"); b = sel; b.hat = 3; CHECK(!custom_valid(&pak, &b), "a man cannot wear the cap for long hair"); b.gender = 1; CHECK(custom_valid(&pak, &b), "a woman can"); b = sel; b.hair_col = 3; CHECK(!custom_valid(&pak, &b), "hair colour 3"); b = sel; b.skin = 4; CHECK(!custom_valid(&pak, &b), "skin 4");
      size_t sz; CHECK(custom_build(&pak, &b, bb, bn, &sz) == NULL, "an invalid choice builds nothing"); }

    size_t sz;
    uint8_t *f = custom_build(&pak, &sel, bb, bn, &sz);
    CHECK(f != NULL, "the default skater builds");
    int nv = 0;
    if (f) { check_skt(f, sz, bb, bn, "default", &nv); printf("default: %d vertices, %zu bytes (the base: %zu)\n", nv, sz, bn); }
    { size_t s2; uint8_t *g = custom_build(&pak, &sel, bb, bn, &s2); CHECK(g && s2 == sz && memcmp(f, g, sz) == 0, "building is deterministic"); free(g); }
    CHECK(custom_build(&pak, &sel, pb, pn, &sz) == NULL && custom_build(&pak, &sel, bb, 50, &sz) == NULL, "a base that is not a skater file is refused");

    /* the atlas: a pixel of the top texture shows up where it should, the skin shows through the top's transparent entry */
    if (f) {
        size_t sz2;
        CustomSel c = sel;
        c.top = 0;
        uint8_t *g = custom_build(&pak, &c, bb, bn, &sz2);
        if (!g) return 1;
        const uint8_t *te = g + 96;
        uint32_t clut_off = rd32(te + 8), pix_off = rd32(te + 12);
        int ncol = 0, seen[256] = { 0 };
        for (int i = 0; i < 256 * 128; i++) if (!seen[g[pix_off + (size_t)i]]) { seen[g[pix_off + (size_t)i]] = 1; ncol++; }
        printf("the default atlas uses %d palette entries\n", ncol);
        /* the first top texture: find an opaque pixel in the body part and compare */
        const uint8_t *tx = NULL; int tw = 0, th = 0; const uint8_t *tpal = NULL;
        for (uint32_t i = 0; i < pak.ntex; i++) {
            const uint8_t *e = pak.tex + (size_t)i * 64;
            if (e[32] == 1 && (int)i == pak.list[CUST_TOP][0]) { tx = pak.file + rd32(e + 40); tw = e[36]; th = e[37]; tpal = pak.file + rd32(e + 44); }
        }
        CHECK(tx && tw == 150 && th == 64, "the first top is 150 x 64");
        int checked = 0, bad = 0;
        for (int y = 5; y < 60 && tx; y += 3)
            for (int x = 40; x < 140; x += 7) {
                int idx = tx[y * tw + x];
                if (!idx) continue;
                const uint8_t *q = tpal + idx * 4;
                int want = (q[0] >> 3) | ((q[1] >> 3) << 5) | ((q[2] >> 3) << 10);
                uint32_t pi = g[pix_off + (size_t)y * 256 + (size_t)(42 + x)];
                int got = (g[clut_off + pi * 2] | (g[clut_off + pi * 2 + 1] << 8)) & 0x7FFF;
                int dr = (want & 31) - (got & 31), dg = ((want >> 5) & 31) - ((got >> 5) & 31), db = ((want >> 10) & 31) - ((got >> 10) & 31);
                checked++;
                if (abs(dr) + abs(dg) + abs(db) > 6) bad++;
            }
        CHECK(checked > 100 && bad * 20 < checked, "the top's pixels are in the atlas at (42, 0): %d of %d are off", bad, checked);
        /* a transparent entry (index 0) of a top: the arms skin shows */
        int found = 0;
        for (int y = 0; y < 64 && tx && !found; y++)
            for (int x = 0; x < 22; x++)
                if (tx[y * tw + x] == 0) {
                    const uint8_t *e = NULL;
                    for (uint32_t i = 0; i < pak.ntex; i++) { const uint8_t *t = pak.tex + (size_t)i * 64; if (t[32] == 0 && t[35] == 1) e = t; }
                    int ai = pak.file[rd32(e + 40) + (size_t)y * 22 + (size_t)x];
                    const uint8_t *ap = pak.file + rd32(e + 44) + (size_t)ai * 4;
                    int want = (ap[0] >> 3) | ((ap[1] >> 3) << 5) | ((ap[2] >> 3) << 10);
                    uint32_t pi = g[pix_off + (size_t)y * 256 + (size_t)(42 + x)];
                    int got = (g[clut_off + pi * 2] | (g[clut_off + pi * 2 + 1] << 8)) & 0x7FFF;
                    CHECK(abs((want & 31) - (got & 31)) + abs(((want >> 5) & 31) - ((got >> 5) & 31)) + abs(((want >> 10) & 31) - ((got >> 10) & 31)) <= 6, "the arms skin shows through the transparent entry at (%d, %d)", x, y);
                    found = 1; break;
                }
        printf("transparent entry in the first top: %s\n", found ? "yes (checked)" : "none in the sleeves");
        free(g);
    }

    /* the overlays: a beard changes the face region (124,64) only, a logo the front of the top at (64,0) only, a hat the hat region (224,64) only, hair the face region too */
    {
        size_t s0, s1;
        CustomSel c = sel;
        uint8_t *f0 = custom_build(&pak, &c, bb, bn, &s0);
        uint32_t pix0 = rd32(f0 + 96 + 12);
        const char *what[4] = { "beard", "logo", "hat", "hair" };
        int x0[4] = { 124, 64, 224, 124 }, y0[4] = { 64, 0, 64, 64 }, w[4] = { 100, 64, 32, 100 }, h[4] = { 64, 64, 64, 64 };
        for (int k = 0; k < 4; k++) {
            c = sel;
            if (k == 0) { c.beard = 3; c.beard_col = 0; }
            if (k == 1) { c.logo = 1; }
            if (k == 2) { c.hat = 4; c.hat_tex = 3; }
            if (k == 3) { c.hair = 1; c.hair_col = 1; }
            uint8_t *f1 = custom_build(&pak, &c, bb, bn, &s1);
            CHECK(f1 != NULL, "%s builds", what[k]);
            if (!f1) continue;
            uint32_t pix1 = rd32(f1 + 96 + 12), cl0 = rd32(f0 + 96 + 8), cl1 = rd32(f1 + 96 + 8);
            int changed_in = 0, changed_out = 0;
            for (int y = 0; y < 128; y++)
                for (int x = 0; x < 256; x++) {
                    uint32_t i0 = f0[pix0 + (size_t)y * 256 + (size_t)x], i1 = f1[pix1 + (size_t)y * 256 + (size_t)x];
                    int diff = (f0[cl0 + i0 * 2] != f1[cl1 + i1 * 2]) || (f0[cl0 + i0 * 2 + 1] != f1[cl1 + i1 * 2 + 1]);
                    if (!diff) continue;
                    if (x >= x0[k] && x < x0[k] + w[k] && y >= y0[k] && y < y0[k] + h[k]) changed_in++; else changed_out++;
                }
            CHECK(changed_in > (k == 0 ? 20 : 100) && changed_out == 0, "%s: %d pixels changed inside its region, %d outside", what[k], changed_in, changed_out);
            if (k == 0 || k == 3) CHECK(rd32(f1 + 8) >= rd32(f0 + 8), "%s: no fewer vertices (the hair is a mesh)", what[k]);
            if (k == 3 || k == 2) CHECK(rd32(f1 + 8) > rd32(f0 + 8), "%s adds a mesh (%u -> %u vertices)", what[k], rd32(f0 + 8), rd32(f1 + 8));
            free(f1);
        }
        /* the logo is mirrored: its first column (the left edge of the picture) lands on the right of its square */
        free(f0);
    }

    /* different choices give different skaters; gender and build change the meshes */
    {
        CustomSel a = sel, b = sel;
        b.gender = 1;
        size_t sa, sb; int na, nb;
        uint8_t *fa = custom_build(&pak, &a, bb, bn, &sa), *fb = custom_build(&pak, &b, bb, bn, &sb);
        if (!fa || !fb) { printf("FAIL: the two skaters build\n"); return 1; }
        na = (int)rd32(fa + 8); nb = (int)rd32(fb + 8);
        CHECK(memcmp(fa + rd32(fa + 24), fb + rd32(fb + 24), 1000) != 0 || na != nb, "a woman is not the same mesh as a man");
        b = sel; b.skin = 3; free(fb); fb = custom_build(&pak, &b, bb, bn, &sb);
        CHECK(memcmp(fa + rd32(fa + 20) + 24, fb + rd32(fb + 20) + 24, 512) != 0, "another skin tone changes the palette");
        free(fa); free(fb);
    }

    /* every part and a crowd of random choices */
    {
        int built = 0, minv = 1 << 30, maxv = 0, many = 0;
        for (int g = 0; g < 2; g++) for (int b = 0; b < 3; b++) for (int fc = 0; fc < 8; fc++) {      /* every head, every build, with one top and one pair of pants of each model type */
            for (int t = 0; t < 35; t += 9) for (int pp = 0; pp < 28; pp += 7) {
                CustomSel c;
                memset(&c, 0, sizeof(c));
                c.gender = (uint8_t)g; c.build = (uint8_t)b; c.skin = (uint8_t)((g + b + fc) % 4); c.face = (uint8_t)fc; c.top = (uint8_t)t; c.pants = (uint8_t)pp; c.shoes = (uint8_t)((t + pp) % 24);
                size_t s2; int n2; char what[64];
                uint8_t *x = custom_build(&pak, &c, bb, bn, &s2);
                snprintf(what, sizeof(what), "g%d b%d f%d t%d p%d", g, b, fc, t, pp);
                CHECK(x != NULL, "%s builds", what);
                if (x && check_skt(x, s2, bb, bn, what, &n2)) { built++; if (n2 < minv) minv = n2; if (n2 > maxv) maxv = n2; }
                free(x);
            }
        }
        for (int g = 0; g < 2; g++) for (int h = 0; h < CUSTOM_NHAIR; h++) for (int hat = 0; hat < CUSTOM_NHAT; hat++) {      /* every hair style with every hat that exists for the gender */
            CustomSel c;
            memset(&c, 0, sizeof(c));
            c.gender = (uint8_t)g; c.hair = (uint8_t)h; c.hat = (uint8_t)hat; c.hair_col = (uint8_t)(h % 3); c.hat_tex = (uint8_t)((h + hat) % 20); c.beard = (uint8_t)(h % 9); c.beard_col = (uint8_t)(hat % 3); c.logo = (uint8_t)(h * 7);
            size_t s2; int n2; char what[64];
            snprintf(what, sizeof(what), "g%d hair%d hat%d", g, h, hat);
            uint8_t *x = custom_build(&pak, &c, bb, bn, &s2);
            if (!custom_hat_exists(g, hat)) { CHECK(x == NULL, "%s: the hat does not exist for him", what); free(x); continue; }
            CHECK(x != NULL, "%s builds", what);
            if (x && check_skt(x, s2, bb, bn, what, &n2)) { built++; if (n2 < minv) minv = n2; if (n2 > maxv) maxv = n2; }
            free(x);
        }
        for (int k = 0; k < 120; k++) {
            CustomSel c;
            memset(&c, 0, sizeof(c));
            c.gender = (uint8_t)(rng() % 2); c.build = (uint8_t)(rng() % 3); c.skin = (uint8_t)(rng() % 4); c.face = (uint8_t)(rng() % 8); c.hair = (uint8_t)(rng() % 10); c.hair_col = (uint8_t)(rng() % 3);
            c.beard = (uint8_t)(rng() % 9); c.beard_col = (uint8_t)(rng() % 3); c.hat = (uint8_t)(rng() % 7); if (!custom_hat_exists(c.gender, c.hat)) c.hat = 0;
            c.hat_tex = (uint8_t)(rng() % 20); c.top = (uint8_t)(rng() % 35); c.logo = (uint8_t)(rng() % 71); c.pants = (uint8_t)(rng() % 28); c.shoes = (uint8_t)(rng() % 24);
            size_t s2; int n2;
            uint8_t *x = custom_build(&pak, &c, bb, bn, &s2);
            CHECK(x != NULL, "random choice %d builds", k);
            if (x && check_skt(x, s2, bb, bn, "random", &n2)) built++;
            free(x);
        }
        many += 0;
        printf("%d choices built and checked; vertices %d .. %d\n", built, minv, maxv);
        CHECK(built >= 400, "enough combinations were checked (%d)", built);
    }
    {   /* hats: with a hat on, no hat vertex is inside the head and no hair vertex comes out of the hat (seen from the head's centre, in 15 degree cones) */
        long combos = 0, hat_in = 0, hair_out = 0, moved_far = 0;
        for (int g = 0; g < 2; g++)
            for (int face = 0; face < 8; face += 2)
                for (int hair = 0; hair < 10; hair++)
                    for (int hat = 1; hat < 7; hat++) {
                        if (!custom_hat_exists(g, hat)) continue;
                        CustomSel c;
                        memset(&c, 0, sizeof(c));
                        c.gender = (uint8_t)g; c.face = (uint8_t)face; c.hair = (uint8_t)hair; c.hat = (uint8_t)hat; c.hat_tex = 0;
                        uint32_t cnt[5];
                        size_t sz; int nvv;
                        uint8_t *x = custom_build(&pak, &c, bb, bn, &sz);
                        CHECK(x && custom_part_counts(&pak, &c, cnt), "hat %d hair %d builds", hat, hair);
                        if (!x) continue;
                        (void)check_skt(x, sz, bb, bn, "hat", &nvv);
                        uint32_t off_v = rd32(x + 24), off_b = rd32(x + 28);
                        const uint8_t *vb = x + off_b;
                        uint32_t h0 = cnt[0] + cnt[1], r0 = h0 + cnt[2], t0 = r0 + cnt[3];
                        float ctr[3] = { 0, 0, 0 };
                        int nh = 0;
                        for (uint32_t i = h0; i < h0 + cnt[2]; i++) if (vb[i] == 18) { for (int k = 0; k < 3; k++) ctr[k] += rdf(x + off_v + i * 24 + 12 + 4 * k); nh++; }
                        for (int k = 0; k < 3; k++) ctr[k] /= (float)nh;
                        for (uint32_t i = t0; i < t0 + cnt[4]; i++) {
                            if (vb[i] != 18) continue;
                            float d[3], r = 0;
                            for (int k = 0; k < 3; k++) { d[k] = rdf(x + off_v + i * 24 + 12 + 4 * k) - ctr[k]; r += d[k] * d[k]; }
                            r = sqrtf(r);
                            float hr = 0;
                            for (uint32_t j = h0; j < h0 + cnt[2]; j++) {
                                if (vb[j] != 18) continue;
                                float e[3], rj = 0, dot = 0;
                                for (int k = 0; k < 3; k++) { e[k] = rdf(x + off_v + j * 24 + 12 + 4 * k) - ctr[k]; rj += e[k] * e[k]; }
                                rj = sqrtf(rj);
                                for (int k = 0; k < 3; k++) dot += d[k] * e[k];
                                if (dot / (r * rj) > 0.97f && rj > hr) hr = rj;
                            }
                            if (hr > 0 && r < hr * 1.0f) hat_in++;
                            if (r > 0.1f) moved_far++;
                        }
                        for (uint32_t i = r0; i < r0 + cnt[3]; i++) {
                            if (vb[i] != 18) continue;
                            float d[3], r = 0;
                            for (int k = 0; k < 3; k++) { d[k] = rdf(x + off_v + i * 24 + 12 + 4 * k) - ctr[k]; r += d[k] * d[k]; }
                            r = sqrtf(r);
                            float sum = 0; int n = 0;
                            for (uint32_t j = t0; j < t0 + cnt[4]; j++) {
                                if (vb[j] != 18) continue;
                                float e[3], rj = 0, dot = 0;
                                for (int k = 0; k < 3; k++) { e[k] = rdf(x + off_v + j * 24 + 12 + 4 * k) - ctr[k]; rj += e[k] * e[k]; }
                                rj = sqrtf(rj);
                                for (int k = 0; k < 3; k++) dot += d[k] * e[k];
                                if (dot / (r * rj) > 0.97f) { sum += rj; n++; }
                            }
                            if (n && r > sum / (float)n * 0.97f) hair_out++;
                        }
                        combos++;
                        free(x);
                    }
        CHECK(hat_in == 0 && hair_out == 0 && moved_far == 0, "%ld hat vertices inside the head, %ld hair vertices outside the hat, %ld far away (over %ld combinations)", hat_in, hair_out, moved_far, combos);
        printf("hats fitted: %ld combinations of gender, face, hair and hat style\n", combos);
    }
    free(f); free(pb); free(bb);
    printf(fails ? "%d FAILURES\n" : "all custom skater tests passed\n", fails);
    return fails != 0;
}
