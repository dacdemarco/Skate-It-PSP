/* Host test: the level markers of a version 4 level file (src/levelobj.c) on the real converted Amphitheatre, plus its rail id table.
 * Expected values come from the original's track1.pvs (tools/campaign_pvs.py Amphitheatre): start grid, bounds, challenge beams, 187 grind lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/levelobj.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
    uint32_t nrails, nrailpts, off_rails, off_polymat, off_railids, off_objs, off_paths, pad;
} Hdr;

int main(int argc, char **argv)
{
    (void)argc;
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 2; fclose(f);
    const Hdr *h = (const Hdr *)b;
    CHECK(h->version == 4, "version %u", h->version);

    LevelObjs o;
    CHECK(levelobj_init(&o, b, (size_t)n, h->off_objs), "objects section");
    CHECK(o.n == 65, "65 objects expected, got %u", o.n);
    CHECK(!levelobj_init(&o, b, (size_t)n, (uint32_t)n - 2), "an offset past the end must be refused");
    CHECK(!levelobj_init(&o, b, (size_t)n, 0), "no section = refused");
    levelobj_init(&o, b, (size_t)n, h->off_objs);

    float p[3], hd;
    CHECK(levelobj_find(&o, "start_pos_1", p) && fabsf(p[0] + 7.4f) < 0.1f && fabsf(p[1] + 64.1f) < 0.1f && fabsf(p[2] + 9.7f) < 0.1f, "start_pos_1 (-7.4 -64.1 -9.7), got %.2f %.2f %.2f", p[0], p[1], p[2]);
    CHECK(!levelobj_find(&o, "no_such_object", p), "missing name");
    CHECK(levelobj_start(&o, 1, p, &hd) && fabsf(hd) < 0.1f, "slot 1 faces +z (heading %.3f)", hd);
    CHECK(levelobj_start(&o, 2, p, &hd) && fabsf(hd - atan2f(-0.9f, 4.8f)) < 0.05f, "slot 2 heading %.3f", hd);
    CHECK(levelobj_start(&o, 4, p, &hd), "slot 4");
    CHECK(!levelobj_start(&o, 5, p, &hd), "no slot 5");
    CHECK(levelobj_find_num(&o, "chal", 3, 2, "_chalstart", p) && fabsf(p[0] + 3.3f) < 0.1f && fabsf(p[2] - 41.0f) < 0.1f, "chal002_chalstart (-3.3 .. 41.0), got %.2f %.2f", p[0], p[2]);
    CHECK(levelobj_find_num(&o, "chal", 3, 6, "_start_pos_4", p), "chal006_start_pos_4");
    CHECK(!levelobj_find_num(&o, "chal", 3, 7, "_chalstart", p), "no chal007");

    float lo[3], hi[3];
    CHECK(levelobj_bounds_outer(&o, lo, hi), "outer bounds");
    CHECK(fabsf(lo[0] + 83.4f) < 0.1f && fabsf(lo[2] + 133.5f) < 0.1f && fabsf(hi[0] - 58.0f) < 0.1f && fabsf(hi[2] - 73.8f) < 0.1f, "bounds x %.1f..%.1f z %.1f..%.1f", lo[0], hi[0], lo[2], hi[2]);

    /* rail ids: the original's 187 grind lines are 1..187, each once; everything else is a heuristic extra (0x8000) */
    const uint16_t *id = (const uint16_t *)(b + h->off_railids);
    int seen[188] = { 0 }, orig = 0, extra = 0, bad = 0;
    for (uint32_t i = 0; i < h->nrails; i++) {
        if (id[i] == 0x8000) extra++;
        else if (id[i] >= 1 && id[i] <= 187 && !seen[id[i]]) { seen[id[i]] = 1; orig++; }
        else bad++;
    }
    CHECK(bad == 0, "%d rails with an invalid or repeated id", bad);
    CHECK(orig == 187, "%d original grind lines, expected 187", orig);
    CHECK(orig + extra == (int)h->nrails, "ids cover every rail");

    printf(fails ? "%d FAILURES\n" : "all level object tests passed (%d original grind lines, %d extras)\n", fails ? fails : orig, extra);
    free(b);
    return fails != 0;
}
