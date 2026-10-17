/* Evidence for src/ghost.h: where the recorded tracks lie against the level's ground and when the events happen. usage: ghost_probe ai.pak level.lvl... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/ghost.h"
#include "../src/coll.h"

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
} Hdr;
static Coll C;
static unsigned char *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); unsigned char *b = malloc((size_t)*n); if (fread(b, 1, (size_t)*n, f) != (size_t)*n) return NULL; fclose(f); return b; }
static int gnd(void *c, float x, float z, float ym, float *y) { (void)c; return coll_ground(&C, x, z, ym, y); }

#define NOFF 17      /* offsets -2 .. +2 s in steps of 0.25 */
static long air_at[64][NOFF], tot_at[64][NOFF];

int main(int argc, char **argv)
{
    long n;
    unsigned char *pb = slurp(argv[1], &n);
    GhostPak pak;
    if (!pb || !ghost_pak_load(&pak, pb, (size_t)n)) { printf("bad pak\n"); return 2; }
    long hist[24] = {0}, nohit = 0, npts = 0;
    for (int a = 2; a < argc; a++) {
        long ln;
        unsigned char *b = slurp(argv[a], &ln);
        const Hdr *h = (const Hdr *)b;
        coll_build(&C, (const float *)(b + h->off_collv), h->ncv, (const CollPoly *)(b + h->off_collp), h->ncp, 8.0f);
        char stem[64]; const char *sl = strrchr(argv[a], '/'); snprintf(stem, sizeof stem, "%s", sl ? sl + 1 : argv[a]); *strrchr(stem, '.') = 0;
        for (uint32_t i = 0; i < pak.count; i++) {
            /* iterate via find over all keys: use route + known chal range is clumsy; scan chal 0..110, modes 0..10, a 0..10, b 0..22 */
        }
        for (int chal = 0; chal < 110; chal++) for (int mode = 0; mode <= 10; mode++) for (int aa = 0; aa <= 10; aa++) for (int bb = 0; bb <= 22; bb++) {
            GhostTrack t;
            if (!ghost_find(&pak, stem, chal, mode, aa, bb, &t)) continue;
            for (int k = 0; k < t.n; k++) {
                if (k && (t.flag[k] & 1)) continue;
                float gy;
                npts++;
                if (!gnd(NULL, t.p[k * 3], t.p[k * 3 + 2], t.p[k * 3 + 1] + 1.0f, &gy)) { nohit++; continue; }
                float d = t.p[k * 3 + 1] - gy;
                int bin = (int)floorf((d + 0.5f) / 0.25f);
                if (bin < 0) bin = 0; if (bin > 23) bin = 23;
                hist[bin]++;
            }
            for (int e = 0; e < t.nev; e++) {
                int kind = t.ev[e].kind; if (kind >= 64) continue;
                float te = (float)t.ev[e].frame / 60.0f;
                for (int o = 0; o < NOFF; o++) {
                    float tm = te + (-2.0f + 0.25f * (float)o), pos[3], gy;
                    if (tm < 0 || tm > t.dur) continue;
                    ghost_pos_at(&t, tm, pos);
                    if (!gnd(NULL, pos[0], pos[2], pos[1] + 1.0f, &gy)) continue;
                    tot_at[kind][o]++;
                    if (pos[1] - gy > 0.45f) air_at[kind][o]++;
                }
            }
        }
        coll_free(&C);
        free(b);
    }
    printf("track y - ground (m), bins of 0.25 from -0.5: no ground %ld of %ld\n", nohit, npts);
    for (int i = 0; i < 24; i++) printf("  %5.2f  %ld\n", -0.5 + 0.25 * i, hist[i]);
    int kinds[] = {27, 31, 6, 7, 56, 57, 58, 54, 9, 10, 49};
    printf("percent airborne at event time -2.0 .. +2.0 s (0.25 steps)\n");
    for (unsigned q = 0; q < sizeof(kinds) / sizeof(kinds[0]); q++) {
        int k = kinds[q];
        printf("kind %2d:", k);
        for (int o = 0; o < NOFF; o++) printf(" %3d", tot_at[k][o] ? (int)(100 * air_at[k][o] / tot_at[k][o]) : -1);
        printf("   (n=%ld)\n", tot_at[k][8]);
    }
    return 0;
}
