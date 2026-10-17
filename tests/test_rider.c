/* Host test: rider pose blending on the real .skt */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../src/rider.h"
#include "../src/trick.h"

typedef struct { char magic[4]; uint32_t version, nverts, nbones, nanims, off_tex, off_verts, off_vbone, off_skel, off_anims; float scale, height; uint32_t pad[12]; } Hdr;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static float diff(const SkelFrame *a, const SkelFrame *b)
{
    float m = 0; const float *x = (const float *)a, *y = (const float *)b;
    for (int i = 0; i < 91; i++) { float d = fabsf(x[i] - y[i]); if (d > m) m = d; }
    return m;
}

int main(int argc, char **argv)
{
    (void)argc;
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 2; fclose(f);
    const Hdr *h = (const Hdr *)b;
    AnimSet as = { b, (const SkelAnim *)(b + h->off_anims), h->nanims };
    CHECK(as.nanims == 376, "nanims %u", as.nanims);
    CHECK(as.anims[POSE_PUSH].nframes == 5, "push frames %u", as.anims[POSE_PUSH].nframes);

    Skate s; skate_init(&s, 0, 0, 0, 0);
    Rider r; rider_init(&r);
    SkelFrame out, ref; BoardFrame bf;
    rider_pose(&r, &s, &as, &out, &bf);
    animset_sample(&as, POSE_RIDE, 5.0f, &ref);
    CHECK(diff(&out, &ref) < 1e-6f, "idle must equal the ride pose (%g)", diff(&out, &ref));
    CHECK(fabsf(bf.pos[2] - 0.01f) < 1e-3f, "board height %.4f", bf.pos[2]);

    /* steering left drives lean_l up, right drives lean_r */
    s.speed = 4.0f; s.steer = 1.0f;
    for (int i = 0; i < 60; i++) rider_update(&r, &s, 1.0f / 60);
    CHECK(r.lean_l > 0.9f && r.lean_r < 0.01f, "lean_l %.2f lean_r %.2f", r.lean_l, r.lean_r);
    rider_pose(&r, &s, &as, &out, &bf);
    animset_sample(&as, POSE_LEAN_L, 5.0f, &ref);
    CHECK(diff(&out, &ref) < 0.15f, "full left lean ~ lean pose (%g)", diff(&out, &ref));

    /* airborne -> air weight */
    s.grounded = 0; s.steer = 0;
    for (int i = 0; i < 60; i++) rider_update(&r, &s, 1.0f / 60);
    CHECK(r.air > 0.95f && r.lean_l < 0.01f, "air %.2f", r.air);
    rider_pose(&r, &s, &as, &out, &bf);
    for (int i = 0; i < 91; i++) CHECK(isfinite(((float *)&out)[i]), "non-finite output");
    /* quaternions stay unit length through the blends */
    float worst = 0;
    for (int i = 0; i < SKEL_NBONES; i++) {
        float *q = out.q[i];
        float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (fabsf(l - 1) > worst) worst = fabsf(l - 1);
    }
    CHECK(worst < 1e-4f, "quaternion length error %g", worst);

    /* push: weight rises while pushing and the pose changes along the clip */
    skate_init(&s, 0, 0, 0, 0); rider_init(&r);
    s.push_t = 0.3f;
    for (int i = 0; i < 30; i++) rider_update(&r, &s, 1.0f / 60);
    CHECK(r.push > 0.7f, "push weight %.2f", r.push);
    SkelFrame p1, p2;
    s.push_t = 0.1f; rider_pose(&r, &s, &as, &p1, &bf);
    s.push_t = 0.4f; rider_pose(&r, &s, &as, &p2, &bf);
    CHECK(diff(&p1, &p2) > 0.05f, "push pose must change over time (%g)", diff(&p1, &p2));

    /* flips: the clip's skeleton AND board play over the flip (the board turns over), and everything stays finite */
    tricks_defaults();
    skate_init(&s, 0, 0, 0, 0); rider_init(&r);
    s.grounded = 0; s.air_t = 0.2f;
    for (int i = 0; i < 60; i++) rider_update(&r, &s, 1.0f / 60);
    SkelFrame air; BoardFrame b_air;
    rider_pose(&r, &s, &as, &air, &b_air);
    s.flip = 1; s.flip_dur = 0.5f;
    BoardFrame b_mid, b_end;
    s.flip_t = 0.0f; rider_update(&r, &s, 1.0f / 60);
    for (int i = 0; i < 6; i++) { s.flip_t = 0.25f; rider_update(&r, &s, 1.0f / 60); }
    SkelFrame mid; rider_pose(&r, &s, &as, &mid, &b_mid);
    CHECK(r.flip > 0.9f, "flip weight rises (%.2f)", r.flip);
    CHECK(diff(&mid, &air) > 0.05f, "mid-flip pose differs from the air pose (%g)", diff(&mid, &air));
    float bd = 0; for (int i = 0; i < 4; i++) bd += fabsf(b_mid.q[i] - b_air.q[i]);
    CHECK(bd > 0.3f, "the board has turned over mid-flip (quat difference %.2f)", bd);
    for (int i = 0; i < 91; i++) CHECK(isfinite(((float *)&mid)[i]), "non-finite flip output");
    s.flip_t = 0.5f; for (int i = 0; i < 12; i++) rider_update(&r, &s, 1.0f / 60);
    SkelFrame endp; rider_pose(&r, &s, &as, &endp, &b_end);
    for (int i = 0; i < 91; i++) CHECK(isfinite(((float *)&endp)[i]), "non-finite flip end output");
    s.flip = 0;
    for (int i = 0; i < 120; i++) rider_update(&r, &s, 1.0f / 60);
    CHECK(r.flip < 0.01f, "the flip fades out (%.3f)", r.flip);
    /* every flip clip fades back to the riding board without a turn: the board at the end of the clip and at the end of the fade-out are the same orientation (a 180 degree
     * clip used to spin the board half a turn) */
    {
        int checked = 0, bad = 0;
        for (int clip = 315; clip <= 362; clip++) {
            Skate z; skate_init(&z, 0, 0, 0, 0);
            Rider rr; rider_init(&rr);
            rr.flip_pose = clip; rr.flip_p = 1.0f;
            BoardFrame b1, b0; SkelFrame sf;
            rr.flip = 1.0f;  rider_pose(&rr, &z, &as, &sf, &b1);       /* the clip's last frame, full weight */
            rr.flip = 0.002f; rider_pose(&rr, &z, &as, &sf, &b0);      /* the very end of the fade-out */
            float d = fabsf(b1.q[0] * b0.q[0] + b1.q[1] * b0.q[1] + b1.q[2] * b0.q[2] + b1.q[3] * b0.q[3]);
            checked++;
            if (d < 0.9f) { bad++; printf("  clip %d: board turns on the way out (|dot| %.3f)", clip, d); printf("%c", 10); }
        }
        CHECK(bad == 0, "%d of %d flip clips spin the board when they fade out", bad, checked);
    }
    /* grabs: a hand on the board */
    s.grab = 1;
    for (int i = 0; i < 40; i++) rider_update(&r, &s, 1.0f / 60);
    SkelFrame gr; BoardFrame b_gr;
    rider_pose(&r, &s, &as, &gr, &b_gr);
    CHECK(r.grab > 0.9f && diff(&gr, &air) > 0.05f, "grab pose differs from the air pose (%g)", diff(&gr, &air));
    for (int i = 0; i < 91; i++) CHECK(isfinite(((float *)&gr)[i]), "non-finite grab output");

    /* the whole trick table: every flip (and its nollie clip), grab and grind gives a finite pose with unit quaternions, a body flip needs no clip, the grind tilt keeps the board unit length */
    {
        int bad = 0, n = 0;
        for (int t = 0; t < NFLIP_TYPES; t++)
            for (int nol = 0; nol < 2; nol++) {
                Skate z; skate_init(&z, 0, 0, 0, 0); z.grounded = 0; z.air_t = 0.3f; z.flip = 1 + t; z.flip_nollie = nol; z.flip_dur = 0.5f; z.flip_t = 0.25f;
                Rider rr; rider_init(&rr);
                for (int i = 0; i < 20; i++) rider_update(&rr, &z, 1.0f / 60);
                SkelFrame sf; BoardFrame bb;
                rider_pose(&rr, &z, &as, &sf, &bb);
                int ok = 1;
                for (int i = 0; i < 91; i++) ok &= isfinite(((float *)&sf)[i]) != 0;
                for (int i = 0; i < 4; i++) ok &= isfinite(bb.q[i]) != 0;
                int pose = (nol && tricks.flip[t].alt >= 0) ? tricks.flip[t].alt : tricks.flip[t].pose;
                ok &= pose == -1 || (pose >= 0 && pose < 376);
                n++; bad += !ok;
            }
        for (int t = 0; t < NGRAB_TYPES; t++) {
            Skate z; skate_init(&z, 0, 0, 0, 0); z.grounded = 0; z.air_t = 0.3f; z.grab = 1 + t;
            Rider rr; rider_init(&rr);
            for (int i = 0; i < 30; i++) rider_update(&rr, &z, 1.0f / 60);
            SkelFrame sf; BoardFrame bb;
            rider_pose(&rr, &z, &as, &sf, &bb);
            int ok = tricks.grab[t].pose >= 0 && tricks.grab[t].pose < 376;
            for (int i = 0; i < 91; i++) ok &= isfinite(((float *)&sf)[i]) != 0;
            n++; bad += !ok;
        }
        for (int t = 0; t < NGRIND_TYPES; t++) {
            Skate z; skate_init(&z, 0, 0, 0, 0); z.grounded = 0; z.grinding = 1; z.grind_type = t;
            Rider rr; rider_init(&rr);
            for (int i = 0; i < 30; i++) rider_update(&rr, &z, 1.0f / 60);
            SkelFrame sf; BoardFrame bb;
            rider_pose(&rr, &z, &as, &sf, &bb);
            float l = sqrtf(bb.q[0] * bb.q[0] + bb.q[1] * bb.q[1] + bb.q[2] * bb.q[2] + bb.q[3] * bb.q[3]);
            int ok = tricks.grind[t].pose >= 0 && tricks.grind[t].pose < 376 && fabsf(l - 1.0f) < 1e-3f;
            for (int i = 0; i < 91; i++) ok &= isfinite(((float *)&sf)[i]) != 0;
            n++; bad += !ok;
        }
        CHECK(bad == 0, "%d of %d trick table entries give a bad pose", bad, n);
    }

    printf(fails ? "%d FAILURES\n" : "all rider tests passed\n", fails);
    return fails != 0;
}
