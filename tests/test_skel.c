/* Host test: run src/skel.c on the real .skt and print matrices for comparison with tools/test_skel_ref.py */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/skel.h"

typedef struct {
    char magic[4]; uint32_t version, nverts, nbones, nanims, off_tex, off_verts, off_vbone, off_skel, off_anims;
    float scale, height; uint32_t pad[12];
} Hdr;

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); fread(b, 1, n, f); fclose(f);
    const Hdr *h = (const Hdr *)b;
    const SkelBone *bones = (const SkelBone *)(b + h->off_skel);
    const SkelAnim *an = (const SkelAnim *)(b + h->off_anims);
    int cases[][2] = {{0, 0}, {0, 5}, {1, 3}, {42, 2}, {244, 4}, {375, 0}};
    float M[SKEL_NBONES][12];
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        const SkelFrame *fr = (const SkelFrame *)(b + an[cases[c][0]].frame_off) + cases[c][1];
        skel_matrices(bones, fr, h->scale, M);
        printf("pose %d %d", cases[c][0], cases[c][1]);
        for (int i = 0; i < SKEL_NBONES; i++) for (int k = 0; k < 12; k++) printf(" %.6f", M[i][k]);
        printf("\n");
        const BoardFrame *bf = (const BoardFrame *)(b + an[cases[c][0]].board_off) + (cases[c][1] < (int)an[cases[c][0]].board_nframes ? cases[c][1] : 0);
        float B[12]; board_matrix(bf, h->scale, B);
        printf("board %d %d", cases[c][0], cases[c][1]);
        for (int k = 0; k < 12; k++) printf(" %.6f", B[k]);
        printf("\n");
    }
    /* blend pose 0 frame 5 with pose 3 frame 5 at 0.35 */
    SkelFrame o;
    skelframe_blend(&o, (const SkelFrame *)(b + an[0].frame_off) + 5, (const SkelFrame *)(b + an[3].frame_off) + 5, 0.35f);
    skel_matrices(bones, &o, h->scale, M);
    printf("blend 0 3 0.35");
    for (int i = 0; i < SKEL_NBONES; i++) for (int k = 0; k < 12; k++) printf(" %.6f", M[i][k]);
    printf("\n");
    return 0;
}
