/* Evidence for the grind / grab events of the recordings: how often a rider is on a rail around each event kind. usage: ghost_probe2 ai.pak level.lvl... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/ghost.h"
#include "../src/coll.h"
#include "../src/rails.h"

typedef struct {
    char magic[4]; uint32_t version, ntex, nbatch, nverts, ncv, ncp, off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6], start[3];
    uint32_t nrails, nrailpts, off_rails, off_polymat, off_railids, off_objs, off_paths, pad;
} Hdr;
static unsigned char *slurp(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); unsigned char *b = malloc((size_t)*n); if (fread(b, 1, (size_t)*n, f) != (size_t)*n) return NULL; fclose(f); return b; }
#define NOFF 13   /* -0.5 .. +2.5 s, 0.25 steps */
static long rail_at[64][NOFF], tot_at[64][NOFF], base_rail, base_tot;
static long arg_rail[64][256], arg_tot[64][256];

int main(int argc, char **argv)
{
    long n;
    unsigned char *pb = slurp(argv[1], &n);
    GhostPak pak;
    if (!pb || !ghost_pak_load(&pak, pb, (size_t)n)) return 2;
    for (int a = 2; a < argc; a++) {
        long ln;
        unsigned char *b = slurp(argv[a], &ln);
        const Hdr *h = (const Hdr *)b;
        Rails rl;
        rails_build(&rl, (const uint32_t *)(b + h->off_rails), h->nrails, (const float *)(b + h->off_rails + 8 * h->nrails));
        char stem[64]; const char *sl = strrchr(argv[a], '/'); snprintf(stem, sizeof stem, "%s", sl ? sl + 1 : argv[a]); *strrchr(stem, '.') = 0;
        for (uint32_t i = 0; i < pak.count; i++) {
            GhostTrack t; char lv[24];
            ghost_entry(&pak, i, &t, lv);
            if (strcmp(lv, stem) != 0) continue;
            for (int k = 0; k < t.n; k++) {
                uint32_t r; float tt, pt[3], dir[3];
                int on = rails_find(&rl, t.p + k * 3, 0.7f, 0.9f, 0.9f, &r, &tt, pt, dir);
                base_tot++; base_rail += on;
            }
            for (int e = 0; e < t.nev; e++) {
                int kind = t.ev[e].kind; if (kind >= 64) continue;
                float te = (float)t.ev[e].frame / 60.0f;
                int onat0 = 0;
                for (int o = 0; o < NOFF; o++) {
                    float tm = te - 0.5f + 0.25f * (float)o, pos[3];
                    if (tm < 0 || tm > t.dur) continue;
                    ghost_pos_at(&t, tm, pos);
                    uint32_t r; float tt, pt[3], dir[3];
                    int on = rails_find(&rl, pos, 0.7f, 0.9f, 0.9f, &r, &tt, pt, dir);
                    tot_at[kind][o]++; rail_at[kind][o] += on;
                    if (o == 3) onat0 = on;
                }
                arg_tot[kind][t.ev[e].arg[0]]++; arg_rail[kind][t.ev[e].arg[0]] += onat0;
            }
        }
        rails_free(&rl);
        free(b);
    }
    printf("baseline near a rail: %ld%%\n", 100 * base_rail / base_tot);
    for (int k = 0; k < 64; k++) {
        if (!tot_at[k][3]) continue;
        printf("kind %2d n=%5ld rail%% at -0.5..+2.5 s:", k, tot_at[k][3]);
        for (int o = 0; o < NOFF; o++) printf(" %3d", tot_at[k][o] ? (int)(100 * rail_at[k][o] / tot_at[k][o]) : -1);
        printf("\n");
    }
    return 0;
}
