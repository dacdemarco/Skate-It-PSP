/* Host test: the story scene player (src/scene.c) on the real data/scenes.pak, career.dat, the level files and a skater file.
 * usage: test_scene <scenes.pak> <career.dat> <data dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "../src/scene.h"
#include "../src/career.h"
#include "../src/rider.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
    uint32_t nrails, nrailpts, off_rails, off_polymat, off_railids, off_objs, off_paths, pad;
} LHdr;

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = aligned_alloc(64, ((size_t)sz + 63) & ~(size_t)63);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return NULL; }
    fclose(f);
    *n = (size_t)sz;
    return b;
}

static int finite_frame(const SkelFrame *f)
{
    const float *p = (const float *)f;
    for (size_t i = 0; i < sizeof(*f) / 4; i++) if (!isfinite(p[i])) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 4) { printf("usage: test_scene scenes.pak career.dat datadir\n"); return 2; }
    size_t np, nc;
    uint8_t *pb = slurp(argv[1], &np), *cb = slurp(argv[2], &nc);
    if (!pb || !cb) { printf("cannot read the data\n"); return 2; }
    ScenePak pak; Career c;
    CHECK(scenepak_load(&pak, pb, np), "scenes.pak loads");
    CHECK(career_load(&c, cb, nc), "career.dat loads");
    CHECK(pak.nscenes == 57 && pak.nclips == 22, "57 scenes, 22 clips (%u, %u)", pak.nscenes, pak.nclips);
    { ScenePak bad; CHECK(!scenepak_load(&bad, pb, np / 2), "a cut file is refused"); CHECK(!scenepak_load(&bad, pb, 10), "a tiny file is refused");
      uint8_t *tmp = aligned_alloc(64, (np + 63) & ~(size_t)63); memcpy(tmp, pb, np); tmp[0] = 'X'; CHECK(!scenepak_load(&bad, tmp, np), "bad magic"); free(tmp); }

    /* the clip database: kinds 1 and 2 exist (the scripts ask for nothing else) */
    CHECK(scenepak_type_count(&pak, 1) == 11 && scenepak_type_count(&pak, 2) == 7 && scenepak_type_count(&pak, 3) == 4, "types 1 / 2 / 3: %d %d %d", scenepak_type_count(&pak, 1), scenepak_type_count(&pak, 2), scenepak_type_count(&pak, 3));

    /* the scripts */
    int levels_seen[32] = { 0 }, nolevel = 0;
    for (int s = 1; s <= 57; s++) {
        SceneScript sc;
        CHECK(scenepak_script(&pak, s, &sc), "scene %d script", s);
        int nl = career_scene_nlines(&c, s);
        CHECK(sc.nsub == nl || (s == 57 && sc.nsub == 0), "scene %d: %d subtitle records, %d lines", s, sc.nsub, nl);
        CHECK((s == 57 ? sc.nspawn == 0 : sc.spawn[0].frame == 0), "scene %d spawns a guest at frame 0 (57 has none)", s);
        for (int i = 0; i < sc.nspawn; i++) CHECK((uint32_t)sc.spawn[i].skater < c.nskaters, "scene %d guest %d", s, sc.spawn[i].skater);
        for (int i = 0; i < sc.ntalk; i++) CHECK(sc.talk[i].link == 1 || sc.talk[i].link == 2, "scene %d talk link %d", s, sc.talk[i].link);
        for (int i = 0; i + 1 < sc.nsub; i++) CHECK(sc.sub[i].start < sc.sub[i].end && sc.sub[i].end <= sc.sub[i + 1].start + 1, "scene %d subtitles run in order", s);
        CHECK(sc.dist == 0x2000 || sc.dist == 0x1666 || sc.dist == 0x1800 || sc.dist == 0x8000, "scene %d distance %#x", s, sc.dist);
        int lv = scenepak_level(&pak, s);
        if (lv < 0) nolevel++;
        else { int ok = 0; for (uint32_t i = 0; i < c.nlevels; i++) ok |= c.level[i].id == lv; CHECK(ok, "scene %d level %d is in the table", s, lv); if (lv < 32) levels_seen[lv]++; }
    }
    CHECK(nolevel == 12, "12 scenes name no level (%d)", nolevel);
    { SceneScript sc; scenepak_script(&pak, 3, &sc); CHECK(sc.nspawn == 2 && sc.spawn[1].frame == 500 && sc.spawn[1].skater == 19, "scene 3 changes the guest at frame 500 to Reda"); }
    { SceneScript sc; scenepak_script(&pak, 1, &sc); CHECK(sc.length == 688 && sc.nsub == 5 && sc.ntalk == 1 && sc.talk[0].link == 1 && sc.spawn[0].skater == 19, "scene 1: 688 frames, 5 lines, Reda"); }

    /* where the guest stands: the emulated values (challenge 21: start_pos_2 of its grid; 22 / 62: the only start point) */
    {
        static const struct { int level, slot; float p[3]; const char *what; } want[] = {
            { 9, 0, { -70.164f, 11.612f, 38.348f }, "challenge 21 (MegaPark, slot 0)" },
            { 16, 5, { 433.013f, -219.953f, 107.662f }, "challenge 22 (South Bank, slot 5)" },
            { 19, 5, { -71.155f, -117.904f, 138.550f }, "challenge 62 (Shanghai, slot 5)" },
        };
        for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
            char path[256]; const char *zip = NULL;
            for (uint32_t k = 0; k < c.nlevels; k++) if (c.level[k].id == want[i].level) zip = career_str(&c, c.level[k].zip_off);
            snprintf(path, sizeof(path), "%s/%s.lvl", argv[3], zip ? zip : "?");
            for (char *q = path + strlen(argv[3]) + 1; *q; q++) *q = (char)tolower((unsigned char)*q);
            size_t n; uint8_t *lb = slurp(path, &n);
            CHECK(lb != NULL, "%s: cannot read %s", want[i].what, path);
            if (!lb) continue;
            LevelObjs o; float p[3];
            CHECK(levelobj_init(&o, lb, n, ((const LHdr *)lb)->off_objs), "%s objects", want[i].what);
            CHECK(scene_guest_place(&o, want[i].slot, p), "%s has a place", want[i].what);
            CHECK(fabsf(p[0] - want[i].p[0]) < 0.01f && fabsf(p[1] - want[i].p[1]) < 0.01f && fabsf(p[2] - want[i].p[2]) < 0.01f, "%s: %.3f %.3f %.3f", want[i].what, p[0], p[1], p[2]);
            CHECK(scene_guest_place(&o, -1, p), "%s: the level's own grid", want[i].what);
            free(lb);
        }
    }

    /* the pose, the facing and the clips, on Aaron's skeleton */
    char skt[256];
    snprintf(skt, sizeof(skt), "%s/skater_aaron.skt", argv[3]);
    size_t sn; uint8_t *sb = slurp(skt, &sn);
    CHECK(sb != NULL, "skater file");
    if (sb) {
        const SkelBone *sbones = (const SkelBone *)(sb + ((const uint32_t *)sb)[8]);
        float scale; memcpy(&scale, sb + 40, 4);
        SkelBone bones[SKEL_NBONES];
        scenepak_bones(&pak, sbones, bones);
        for (int flip = 0; flip < 2; flip++)
            for (int cl = 0; cl < (int)pak.nclips; cl++) {
                ScenePlayer sp;
                sceneplayer_init(&sp, &pak, 5, 7);
                sp.clip = cl; sp.flip = flip; sp.link = pak.info[cl].type;
                float place[3] = { 10.0f, 3.0f, -4.0f }, body[16];
                sceneplayer_body(&sp, place, body);
                double hx = 0, hz = 0, low = 1e9, high = -1e9;
                int nf = (int)pak.clips[cl].nframes;
                for (int f = 0; f < nf; f += 3) {
                    SkelFrame fr; BoardFrame bf; float m[SKEL_NBONES][12];
                    sp.t = (float)f / SCENE_FPS;
                    sceneplayer_pose(&sp, &fr, &bf);
                    CHECK(finite_frame(&fr), "clip %d frame %d is finite", cl, f);
                    skel_matrices(bones, &fr, scale, m);
                    float j[SKEL_NBONES][3];
                    for (int b = 0; b < SKEL_NBONES; b++) {
                        float x = m[b][3], y = m[b][7], z = m[b][11];
                        j[b][0] = body[0] * x + body[4] * y + body[8] * z + body[12];
                        j[b][1] = body[1] * x + body[5] * y + body[9] * z + body[13];
                        j[b][2] = body[2] * x + body[6] * y + body[10] * z + body[14];
                        if (j[b][1] < low) low = j[b][1];
                        if (j[b][1] > high) high = j[b][1];
                    }
                    for (int leg = 0; leg < 2; leg++) {                       /* the knees bend the way the rider faces */
                        int hip = leg ? 5 : 1, knee = leg ? 6 : 2, ank = leg ? 7 : 3;
                        hx += j[knee][0] - (j[hip][0] + j[ank][0]) / 2; hz += j[knee][2] - (j[hip][2] + j[ank][2]) / 2;
                    }
                }
                double ang = atan2(hx, hz) * 180.0 / M_PI, want_ang = flip ? 180.0 : 0.0, d = fabs(fmod(ang - want_ang + 540.0, 360.0) - 180.0);
                CHECK(d < 30.0, "clip %d flip %d: faces %.0f degrees (want %.0f)", cl, flip, ang, want_ang);
                CHECK(low > 3.0 - 0.15 && low < 3.0 + 0.6, "clip %d: lowest joint %.2f above the ground at 3.0 (feet stand on it)", cl, low);
                CHECK(high - 3.0 > 1.3 && high - 3.0 < 2.1, "clip %d: head %.2f m above the ground", cl, high - 3.0);
            }
    }

    /* playing a scene: the guest changes at frame 500 of scene 3, the clip is of the asked kind, clips follow one another, nothing goes bad */
    {
        ScenePlayer sp;
        sceneplayer_init(&sp, &pak, 3, 99);
        int nl = career_scene_nlines(&c, 3), changes = 0, last_guest = -1, clips_seen = 0, last_clip = -2;
        for (int line = 0; line < nl; line++) {
            sceneplayer_line(&sp, line, nl);
            if (sp.guest != last_guest) { changes++; last_guest = sp.guest; }
            CHECK(sp.clip >= 0 && pak.info[sp.clip].type == sp.link, "scene 3 line %d: clip %d is of kind %d", line, sp.clip, sp.link);
            for (int k = 0; k < 600; k++) {
                sceneplayer_update(&sp, 1.0f / 60.0f);
                SkelFrame fr; BoardFrame bf;
                sceneplayer_pose(&sp, &fr, &bf);
                CHECK(finite_frame(&fr) && sp.fade >= 0.0f && sp.fade <= 1.0f && sp.clip >= 0, "scene 3 line %d step %d", line, k);
                if (sp.clip != last_clip) { clips_seen++; last_clip = sp.clip; }
            }
        }
        CHECK(changes == 2, "scene 3 has two guests (%d)", changes);
        CHECK(clips_seen > nl, "clips follow each other (%d)", clips_seen);
        float eye[3], look[3], p[3] = { 1, 2, 3 };
        sceneplayer_camera(&sp, p, eye, look);
        CHECK(fabsf(look[1] - 3.0f) < 1e-4f && fabsf(eye[1] - 4.0f) < 1e-4f && fabsf(eye[2] - (3.0f + 2.0f)) < 1e-4f && fabsf(eye[0] - 1.0f) < 1e-4f, "camera at guest + (0, 2, 2): %.2f %.2f %.2f", eye[0], eye[1], eye[2]);
        for (int s = 1; s <= 57; s++) {                                                 /* every scene, every line */
            ScenePlayer q;
            sceneplayer_init(&q, &pak, s, (uint32_t)s);
            int n = career_scene_nlines(&c, s);
            for (int line = 0; line < n; line++) {
                sceneplayer_line(&q, line, n);
                CHECK((q.clip >= 0) == sceneplayer_active(&q), "scene %d line %d has a clip", s, line);
                for (int k = 0; k < 120; k++) sceneplayer_update(&q, 1.0f / 30.0f);
            }
        }
    }
    printf(fails ? "SCENE TESTS FAILED (%d)\n" : "scene tests ok\n", fails);
    return fails ? 1 : 0;
}
