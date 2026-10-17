/* Host tests for src/score.c: combos, multiplier, banking and bailing, driven by hand-made skater events. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/score.h"
#include "../src/trick.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static Skate sk;
static Score sc;

static void start_grind(int type, int kind) { sk.grinding = 1; sk.grounded = 0; sk.grind_type = type; sk.ev_grind_start++; sk.ev_grind_kind = kind; score_update(&sc, &sk, 0.0f); }
static void end_grind(void)                 { sk.grinding = 0; sk.ev_grind_end++; score_update(&sc, &sk, 0.0f); }
static void run(float secs)                 { for (float t = 0; t < secs; t += 1.0f / 60.0f) score_update(&sc, &sk, 1.0f / 60.0f); }

int main(void)
{
    tricks_defaults();
    memset(&sk, 0, sizeof(sk));
    score_init(&sc);

    /* one grind of 2 s, then roll: base + rate * time, x1 */
    start_grind(0, 0);
    CHECK(sc.combo_active && sc.combo_n == 1 && score_combo_mult(&sc) == 1, "a grind starts a combo");
    CHECK(score_combo_points(&sc) == tricks.grind[0].base, "points on landing the grind: %ld", score_combo_points(&sc));
    run(2.0f);
    long expect = tricks.grind[0].base + 2 * tricks.grind[0].rate;
    CHECK(labs(score_combo_points(&sc) - expect) <= 4, "after 2 s: %ld (expected about %ld)", score_combo_points(&sc), expect);
    end_grind();
    sk.grounded = 1;                                    /* it rolls on the ground; not banked at once */
    run(0.4f);
    CHECK(sc.total == 0 && sc.combo_active, "not banked before %.1f s of rolling", SCORE_BANK_TIME);
    run(0.5f);
    CHECK(sc.msg == SCORE_MSG_BANK && labs(sc.total - expect) <= 4 && !sc.combo_active, "banked after rolling: total %ld", sc.total);
    long first = sc.total;

    /* a combo of two grinds with a hop between: the multiplier is 2 */
    sk.grounded = 0;
    start_grind(1, 0);
    run(1.0f);
    end_grind();
    run(0.5f);                                           /* in the air: no banking however long */
    CHECK(sc.combo_active && sc.roll_t == 0.0f, "airborne between tricks keeps the combo");
    start_grind(3, 1);
    CHECK(sc.combo_n == 2 && score_combo_mult(&sc) == 2, "second trick: x%d", score_combo_mult(&sc));
    run(1.0f);
    long expect2 = (tricks.grind[1].base + tricks.grind[1].rate + tricks.grind[3].base + tricks.grind[3].rate) * 2;
    CHECK(labs(score_combo_points(&sc) - expect2) <= 8, "combo points %ld (expected about %ld)", score_combo_points(&sc), expect2);
    end_grind();
    sk.grounded = 1;
    run(1.0f);
    CHECK(sc.msg == SCORE_MSG_BANK && sc.msg_n == 2 && labs(sc.total - first - expect2) <= 8, "combo banked: total %ld (expected %ld)", sc.total, first + expect2);
    long banked = sc.total;

    /* a bail loses the whole combo, banked points stay */
    sk.grounded = 0;
    start_grind(0, 0); run(1.0f); end_grind(); start_grind(2, 0); run(0.5f);
    sk.grinding = 0; sk.ev_grind_end++; sk.ev_bail++;
    score_update(&sc, &sk, 0.0f);
    CHECK(sc.msg == SCORE_MSG_BAIL && sc.msg_pts > 0 && sc.msg_n == 2 && !sc.combo_active && sc.total == banked, "bail: combo lost (msg %d, lost %ld, total %ld)", sc.msg, sc.msg_pts, sc.total);
    sk.grounded = 1;
    run(2.5f);
    CHECK(sc.total == banked, "nothing is banked after a bail");
    CHECK(sc.msg == SCORE_MSG_NONE, "the message fades");

    /* the multiplier is capped */
    sk.grounded = 0;
    for (int i = 0; i < 14; i++) { start_grind(0, 0); run(0.1f); end_grind(); }
    CHECK(sc.combo_n == 14 && score_combo_mult(&sc) == SCORE_MAX_MULT, "multiplier cap x%d", score_combo_mult(&sc));

    /* the new moves: nollie flips, spins (half-cab / cab from fakie), reverts, powerslides, wallrides */
    score_init(&sc); memset(&sk, 0, sizeof(sk)); sk.grounded = 0;
    sk.ev_flip_done = 1; sk.ev_flip_type = FLIP_HARDFLIP; sk.ev_flip_nollie = 1; score_update(&sc, &sk, 0.0f);
    CHECK(strcmp(score_trick_name(&sc), "NOLLIE HARDFLIP") == 0 && sc.combo_base == tricks.flip[FLIP_HARDFLIP].base, "a nollie flip: %s %ld", score_trick_name(&sc), sc.combo_base);
    sk.ev_flip_done = 1; sk.ev_flip_type = FLIP_KICKFLIP; sk.ev_flip_nollie = 0; score_update(&sc, &sk, 0.0f);
    CHECK(strcmp(score_trick_name(&sc), "KICKFLIP") == 0, "a plain flip has no prefix");
    sk.ev_spin = 1; sk.ev_spin_deg = 180; sk.ev_spin_dir = 1; sk.ev_spin_fakie = 0; score_update(&sc, &sk, 0.0f);
    CHECK(strcmp(score_trick_name(&sc), "BS SPIN") == 0, "a 180 to the left: %s", score_trick_name(&sc));
    long before = sc.combo_base;
    sk.ev_spin = 1; sk.ev_spin_deg = 540; sk.ev_spin_dir = -1; sk.ev_spin_fakie = 0; score_update(&sc, &sk, 0.0f);
    CHECK(strcmp(score_trick_name(&sc), "FS SPIN 540") == 0 && sc.combo_base - before == 3 * tricks.move[MOVE_FS_SPIN].base, "a 540 scores three half turns: %s +%ld", score_trick_name(&sc), sc.combo_base - before);
    sk.ev_spin = 1; sk.ev_spin_deg = 180; sk.ev_spin_dir = -1; sk.ev_spin_fakie = 1; score_update(&sc, &sk, 0.0f);
    CHECK(strcmp(score_trick_name(&sc), "FS HALF-CAB") == 0, "a half turn from fakie: %s", score_trick_name(&sc));
    sk.ev_spin = 1; sk.ev_spin_deg = 360; sk.ev_spin_dir = 1; sk.ev_spin_fakie = 1; score_update(&sc, &sk, 0.0f);
    CHECK(strcmp(score_trick_name(&sc), "BS CAB 360") == 0, "a full turn from fakie: %s", score_trick_name(&sc));
    sk.ev_revert = -1; score_update(&sc, &sk, 0.0f);
    CHECK(strcmp(score_trick_name(&sc), "FS REVERT") == 0 && sk.ev_revert == 0, "a revert to the right");
    CHECK(sc.combo_n == 7 && score_combo_mult(&sc) == 7, "seven tricks, x%d", score_combo_mult(&sc));
    sk.sliding = 1; sk.slide_dir = 1.0f; sk.ev_slide_start = 1; score_update(&sc, &sk, 0.0f);
    long p0 = score_combo_points(&sc);
    run(1.0f);
    CHECK(strcmp(score_trick_name(&sc), "BS POWERSLIDE") == 0 && score_combo_points(&sc) > p0 + 7 * tricks.move[MOVE_BS_POWERSLIDE].rate / 2, "a powerslide scores while it lasts (%ld -> %ld)", p0, score_combo_points(&sc));
    sk.sliding = 0; sk.ev_slide_end = 1; score_update(&sc, &sk, 0.0f);
    sk.wallriding = 1; sk.wall_side = -1.0f; sk.ev_wall_start = 1; score_update(&sc, &sk, 0.0f);
    CHECK(strcmp(score_trick_name(&sc), "BS WALLRIDE") == 0, "the wall on the right: back to the wall: %s", score_trick_name(&sc));
    run(0.5f);
    sk.wallriding = 0; sk.ev_wall_end = 1; score_update(&sc, &sk, 0.0f);
    CHECK(sk.ev_wall_end == 0 && !sc.in_grind, "the wallride ends");

    /* grinds are BS or FS by the side of the rail; a grab that changes variant while held counts again */
    score_init(&sc); memset(&sk, 0, sizeof(sk)); sk.grounded = 0;
    sk.grind_bs = 1; start_grind(GRIND_SMITH, 0);
    CHECK(strcmp(score_trick_name(&sc), "BS SMITH") == 0, "a backside grind: %s", score_trick_name(&sc));
    end_grind();
    sk.grind_bs = 0; start_grind(GRIND_50, 0);
    CHECK(strcmp(score_trick_name(&sc), "FS 5-0") == 0, "a frontside grind: %s", score_trick_name(&sc));
    end_grind();
    sk.grab = 1 + GRAB_MUTE; sk.ev_grab_start = 1; sk.ev_grab_type = GRAB_MUTE; score_update(&sc, &sk, 0.0f);
    int n0 = sc.combo_n;
    sk.grab = 1 + GRAB_MUTE_TAILBONE; sk.ev_grab_switch = 1; sk.ev_grab_type = GRAB_MUTE_TAILBONE; score_update(&sc, &sk, 0.0f);
    CHECK(sc.combo_n == n0 + 1 && strcmp(score_trick_name(&sc), "MUTE TAILBONE") == 0 && sk.ev_grab_switch == 0, "a variant change while held counts as the next trick: %s", score_trick_name(&sc));
    sk.grab = 0; sk.ev_grab_end = 1; score_update(&sc, &sk, 0.0f);
    CHECK(!sc.in_grind, "the grab ends");
    /* a grind that changes type on the same rail (a flick of the nub): the first one is over, the new one is the next trick of the combo */
    sk.grind_bs = 1; start_grind(GRIND_5050, 0);
    int n1 = sc.combo_n; run(0.5f);
    sk.grind_type = GRIND_NOSEGRIND; sk.ev_grind_switch = 1; score_update(&sc, &sk, 0.0f);
    CHECK(sc.combo_n == n1 + 1 && sc.in_grind && strcmp(score_trick_name(&sc), "BS NOSEGRIND") == 0 && sk.ev_grind_switch == 0, "a grind switch counts as the next trick: %s", score_trick_name(&sc));
    CHECK(sc.log_n >= 2 && sc.log[sc.log_n - 2].type == GRIND_5050 && sc.log[sc.log_n - 2].dur > 0.45f && sc.log[sc.log_n - 1].type == GRIND_NOSEGRIND, "both grinds are in the log with their time");
    end_grind();

    /* events are consumed */
    CHECK(sk.ev_grind_start == 0 && sk.ev_grind_end == 0 && sk.ev_bail == 0, "events cleared by the scorer");

    /* the trick log and flights (for the challenge goals): a plain ollie is part of the line but scores nothing and adds no multiplier */
    score_init(&sc); memset(&sk, 0, sizeof(sk)); sk.grounded = 1;
    run(0.1f);
    sk.grounded = 0; sk.ev_ollie_plain = 1; sk.ev_ollie_nollie = 0; sk.pos[0] = 1.0f; score_update(&sc, &sk, 1.0f / 60.0f);
    CHECK(sc.flight == 1, "a take-off starts flight 1 (%d)", sc.flight);
    CHECK(sc.log_n == 1 && sc.log[0].cls == TRICK_FLIP && sc.log[0].type == TREC_OLLIE && sc.log[0].flight == 1 && sc.log[0].pts == 0, "the ollie is logged");
    CHECK(sc.combo_active && sc.combo_n == 0 && score_combo_points(&sc) == 0 && sk.ev_ollie_plain == 0, "an ollie opens a combo with no points and no multiplier");
    sk.ev_flip_done = 1; sk.ev_flip_type = FLIP_KICKFLIP; sk.ev_flip_nollie = 0; score_update(&sc, &sk, 1.0f / 60.0f);
    CHECK(sc.combo_n == 1 && sc.log_n == 2 && sc.log[1].type == FLIP_KICKFLIP && sc.log[1].flight == 1 && fabsf(sc.log[1].pos[0] - 1.0f) < 1e-6f, "the flip joins the log in flight 1, with its position");
    sk.grounded = 1; run(1.0f);
    CHECK(sc.bank_serial == 1 && sc.bank_n == 2 && sc.bank_log[0].type == TREC_OLLIE && sc.bank_log[1].type == FLIP_KICKFLIP && sc.bank_pts == tricks.flip[FLIP_KICKFLIP].base, "banked: the snapshot keeps both tricks (%d) and the points (%ld)", sc.bank_n, sc.bank_pts);
    CHECK(sc.total == tricks.flip[FLIP_KICKFLIP].base && sc.log_n == 0, "the log is cleared after the bank");
    sc.ev_bank = 0;                                       /* (the main loop clears the jingle flag every frame) */
    sk.ev_ollie_plain = 1; sk.ev_ollie_nollie = 1; sk.grounded = 0; score_update(&sc, &sk, 1.0f / 60.0f);
    CHECK(sc.flight == 2 && sc.log[0].nollie == 1, "the next jump is flight 2, a nollie ollie");
    sk.grounded = 1; run(1.0f);
    CHECK(sc.bank_serial == 2 && sc.total == tricks.flip[FLIP_KICKFLIP].base && !sc.ev_bank, "a combo of only ollies banks silently: no points, no jingle");
    sk.grounded = 0; sk.rail = 6; sk.grind_bs = 1; start_grind(GRIND_SMITH, 0);
    CHECK(sc.log_n == 1 && sc.log[0].cls == TRICK_GRIND && sc.log[0].rail == 7 && sc.log[0].bs == 1, "a grind logs its rail (index + 1) and side");
    run(0.5f); end_grind();
    CHECK(sc.log[0].dur > 0.45f && sc.log[0].pts > tricks.grind[GRIND_SMITH].base, "and how long it lasted and what it earned (%.2f s, %ld)", sc.log[0].dur, sc.log[0].pts);
    sk.ev_bail = 1; score_update(&sc, &sk, 0.0f);
    CHECK(sc.log_n == 0 && sc.bank_serial == 2, "a bail clears the log and banks nothing");

    /* ---- riding regular: backside and frontside swap for the moves that are defined by the side of the world ---- */
    {
        for (int pass = 0; pass < 2; pass++) {
            int reg = pass;
            memset(&sk, 0, sizeof(sk));
            score_init(&sc); sc.regular = reg;
            sk.ev_spin = 1; sk.ev_spin_deg = 180; sk.ev_spin_dir = 1; score_update(&sc, &sk, 0.0f);
            CHECK(strcmp(score_trick_name(&sc), reg ? "FS SPIN" : "BS SPIN") == 0, "a turn to the left (regular %d): %s", reg, score_trick_name(&sc));
            sk.ev_spin = 1; sk.ev_spin_deg = 180; sk.ev_spin_dir = -1; sk.ev_spin_fakie = 1; score_update(&sc, &sk, 0.0f);
            CHECK(strcmp(score_trick_name(&sc), reg ? "BS HALF-CAB" : "FS HALF-CAB") == 0, "a half turn to the right from fakie (regular %d): %s", reg, score_trick_name(&sc));
            sk.ev_revert = 1; score_update(&sc, &sk, 0.0f);
            CHECK(strcmp(score_trick_name(&sc), reg ? "FS REVERT" : "BS REVERT") == 0, "a revert to the left (regular %d): %s", reg, score_trick_name(&sc));
            sk.slide_dir = 1.0f; sk.sliding = 1; sk.ev_slide_start = 1; score_update(&sc, &sk, 0.0f);
            CHECK(strcmp(score_trick_name(&sc), reg ? "FS POWERSLIDE" : "BS POWERSLIDE") == 0, "a powerslide (regular %d): %s", reg, score_trick_name(&sc));
            sk.sliding = 0; sk.ev_slide_end = 1; score_update(&sc, &sk, 0.0f);
            sk.wallriding = 1; sk.wall_side = -1.0f; sk.ev_wall_start = 1; score_update(&sc, &sk, 0.0f);
            CHECK(strcmp(score_trick_name(&sc), reg ? "FS WALLRIDE" : "BS WALLRIDE") == 0, "the wall on the right (regular %d): %s", reg, score_trick_name(&sc));
            sk.wallriding = 0; sk.ev_wall_end = 1; score_update(&sc, &sk, 0.0f);
            sk.grounded = 0; sk.rail = 3; sk.grind_bs = 1; start_grind(GRIND_SMITH, 0);
            CHECK(strcmp(score_trick_name(&sc), reg ? "FS SMITH" : "BS SMITH") == 0 && sc.log[sc.log_n - 1].bs == (reg ? 0 : 1), "a rail on the right (regular %d): %s, logged bs %d", reg, score_trick_name(&sc), sc.log[sc.log_n - 1].bs);
            end_grind();
            sk.ev_bail = 1; score_update(&sc, &sk, 0.0f);
        }
        /* flips and grabs keep their names: a mirrored rider's kickflip is a kickflip */
        memset(&sk, 0, sizeof(sk));
        score_init(&sc); sc.regular = 1;
        sk.ev_flip_done = 1; sk.ev_flip_type = FLIP_KICKFLIP; score_update(&sc, &sk, 0.0f);
        CHECK(strcmp(score_trick_name(&sc), "KICKFLIP") == 0, "a kickflip is a kickflip in both stances");
    }

    printf(fails ? "%d FAILURES\n" : "all score tests passed\n", fails);
    return fails != 0;
}
