#include "contest.h"
#include "goals.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned rnd(Contest *k) { k->rng = k->rng * 1664525u + 1013904223u; return k->rng >> 8; }
static float frand(Contest *k) { return (float)(rnd(k) & 0xFFFFu) / 65536.0f; }

static int in_box(const Contest *k, int z, const float p[3])
{
    return p[0] >= k->zlo[z][0] - 3.0f && p[0] <= k->zhi[z][0] + 3.0f && p[2] >= k->zlo[z][2] - 3.0f && p[2] <= k->zhi[z][2] + 3.0f && p[1] >= k->zlo[z][1] - 1.0f && p[1] <= k->zhi[z][1] + 4.0f;
}

/* with no zone in the level everything counts; with zones, what happens inside one of them */
static int in_zone(const Contest *k, const float p[3])
{
    if (k->nzones == 0) return 1;
    for (int z = 0; z < k->nzones; z++)
        if (in_box(k, z, p)) return 1;
    return 0;
}

long contest_line_points(const Contest *k, const Score *sc)
{
    if (sc->bank_n <= 0) return 0;
    int ok = k->nzones == 0;
    for (int i = 0; i < sc->bank_n && !ok; i++)
        ok = in_zone(k, sc->bank_log[i].pos);
    if (!ok) return 0;
    long p = sc->bank_pts;
    return p > CONTEST_SCORE_CAP ? CONTEST_SCORE_CAP : (p < 0 ? 0 : p);
}

int contest_setup(Contest *k, const Career *c, int chal, const LevelObjs *o)
{
    memset(k, 0, sizeof(*k));
    if (chal < 0 || (uint32_t)chal >= c->nchal) return 0;
    const CareerChal *ch = &c->chal[chal];
    if (ch->mode != MODE_JAM && ch->mode != MODE_BESTTRICK && ch->mode != MODE_SKATE) return 0;
    k->mode = ch->mode; k->chal = chal; k->career = c;
    for (int j = 0; j < CONTEST_MAX_OPP; j++)
        if (ch->opp[j] != 0xFE) k->opp_skater[k->nopp++] = ch->opp[j];
    if (k->nopp == 0) return 0;
    for (int z = 1; z <= CONTEST_MAX_ZONES; z++) {
        char sa[24], sb[24];
        float a[3], b[3];
        snprintf(sa, sizeof(sa), "_zone_%d_a", z); snprintf(sb, sizeof(sb), "_zone_%d_b", z);
        if (levelobj_find_num(o, "chal", 3, ch->slot, sa, a) && levelobj_find_num(o, "chal", 3, ch->slot, sb, b)) {
            for (int i = 0; i < 3; i++) { k->zlo[k->nzones][i] = a[i] < b[i] ? a[i] : b[i]; k->zhi[k->nzones][i] = a[i] < b[i] ? b[i] : a[i]; }
            k->nzones++;
        }
    }
    k->limit = ch->time > 0 ? (float)ch->time : 90.0f;
    k->rounds = k->mode == MODE_BESTTRICK ? (ch->nobj >= 1 && ch->nobj <= CONTEST_MAX_ROUNDS ? ch->nobj : 3) : 1;
    k->rng = (unsigned)chal * 7919u + 12345u;
    k->last_ok = -1;
    k->target = -1;
    if (k->mode == MODE_SKATE) {
        int n = 0;
        const uint16_t *e = career_ai_events(c, chal, 0, 0, 1, &n);
        for (int i = 0; e && i < n && k->nset < SKATE_MAX_SET; i++) { k->set_ai[k->nset] = e[i * 2]; k->set_ids[k->nset++] = e[i * 2 + 1]; }
        if (k->nset == 0) { k->set_ids[0] = 0; k->set_ids[1] = 13; k->set_ids[2] = 15; k->nset = 3; k->set_ai[0] = k->set_ai[1] = k->set_ai[2] = -1; }       /* (no recording: the basics) */
    }
    return 1;
}

void contest_start(Contest *k)
{
    k->t = 0.0f;
    k->round = 0;
    k->result = CT_RUNNING;
    k->brk = 0.0f;
    if (k->mode == MODE_SKATE) {
        k->state = SK_OPP_SETS;
        k->turn = 0;
        k->target = k->set_ids[0];
        k->msg = CT_MSG_OPP_SETS;
    }
}

/* ---- the opponents' recorded lines ---------------------------------------------------------------------------------------------------------------- */
long contest_opp_score_at(const Contest *k, int opp, float t)
{
    int n = 0;
    const uint16_t *e = career_ai_events(k->career, k->chal, 0, opp, 0, &n);
    long s = 0;
    for (int i = 0; e && i < n; i++)
        if ((float)e[i * 2] / 60.0f <= t) s += e[i * 2 + 1];
    return s;
}

/* an opponent's best line in a Best Trick round: recorded, or made up (a pseudo-random value in the range the recordings show, lower for the 20 s rounds) */
static long opp_best_line(Contest *k, int round, int opp)
{
    int n = 0;
    const uint16_t *e = career_ai_events(k->career, k->chal, round, opp, 0, &n);
    if (e) {
        long best = 0;
        for (int i = 0; i < n; i++) if (e[i * 2 + 1] > best) best = e[i * 2 + 1];
        return best;
    }
    unsigned h = (unsigned)(k->chal * 131 + round * 17 + opp * 7 + 5) * 2654435761u;
    h ^= h >> 15;
    return k->limit >= 60.0f ? 1400 + (long)(h % 1600u) : 700 + (long)(h % 900u);
}

void contest_letters(int n, char out[12])
{
    static const char L[SKATE_LETTERS] = { 'S', 'K', 'A', 'T', 'E' };
    int o = 0;
    for (int i = 0; i < SKATE_LETTERS; i++) { out[o++] = i < n ? L[i] : '_'; if (i + 1 < SKATE_LETTERS) out[o++] = ' '; }
    out[o] = 0;
}

/* ---- play ------------------------------------------------------------------------------------------------------------------------------------- */
static int decided(Contest *k, long mine, const long *theirs)
{
    int place = 1;
    for (int j = 0; j < k->nopp; j++) if (theirs[j] >= mine) place++;               /* a tie goes to the opponent */
    k->place = place;
    k->result = place == 1 ? CT_WON : CT_LOST;
    return k->result;
}

static int matches(const Contest *k, const Score *sc, int target)
{
    for (int i = 0; i < sc->bank_n; i++)
        if (goals_trick_id(&sc->bank_log[i]) == target && in_zone(k, sc->bank_log[i].pos)) return 1;
    return 0;
}

static int first_trick(const Contest *k, const Score *sc)
{
    for (int i = 0; i < sc->bank_n; i++)
        if (goals_trick_classes(&sc->bank_log[i]) && goals_trick_id(&sc->bank_log[i]) >= 0 && in_zone(k, sc->bank_log[i].pos)) return goals_trick_id(&sc->bank_log[i]);
    return -1;
}

static void skate_turn(Contest *k, const ContestIn *in, int new_bank)
{
    const Score *sc = in->score;
    k->t += in->dt;
    int timeout = k->t >= k->limit && (!sc->combo_active || k->t >= k->limit + 3.0f);
    if (k->state == SK_OPP_SETS) {                                     /* the opponent has set a trick: match it */
        int ok = -1;
        if (in->bails > 0) ok = 0;
        else if (new_bank) ok = matches(k, sc, k->target) ? 1 : 0;
        else if (timeout) ok = 0;
        if (ok < 0) return;
        k->last_ok = ok;
        if (!ok) { k->letters_player++; k->msg = CT_MSG_MISSED; } else k->msg = CT_MSG_MATCHED;
        k->state = SK_PLAYER_SETS;
        k->brk = CONTEST_BREAK_TIME;
        k->t = 0.0f;
        if (k->letters_player >= SKATE_LETTERS) { k->result = CT_LOST; k->place = k->nopp + 1; }
    } else if (k->state == SK_PLAYER_SETS) {                           /* the player's turn to set one: the first trick he lands */
        int id = new_bank ? first_trick(k, sc) : -1;
        if (id >= 0) {
            k->target = id;
            float pts = k->target < (int)k->career->ntricks ? (float)k->career->trick[k->target].points : 100.0f;
            float skill = 0.45f + 0.12f * (float)k->career->chal[k->chal].skate_set;
            k->p_copy = skill - 0.0015f * pts;
            if (k->p_copy < 0.10f) k->p_copy = 0.10f;
            if (k->p_copy > 0.90f) k->p_copy = 0.90f;
            if (frand(k) < k->p_copy) k->msg = CT_MSG_OPP_MATCHED;
            else { k->letters_opp++; k->msg = CT_MSG_OPP_MISSED; }
            if (k->letters_opp >= SKATE_LETTERS) { k->result = CT_WON; k->place = 1; }
        } else if (in->bails > 0 || timeout || (new_bank && id < 0)) {
            k->msg = CT_MSG_SET_FAILED;                                 /* no letter for missing your own set: the lead passes */
        } else {
            return;
        }
        k->state = SK_OPP_SETS;
        k->brk = CONTEST_BREAK_TIME;
        k->t = 0.0f;
        k->turn++;
        k->target = k->set_ids[k->turn % k->nset];
    }
}

int contest_update(Contest *k, const ContestIn *in)
{
    if (k->result != CT_RUNNING) return k->result;
    const Score *sc = in->score;
    int new_bank = sc->bank_serial != k->seen_bank;
    k->seen_bank = sc->bank_serial;
    if (k->brk > 0.0f) {                                               /* between rounds / turns: nothing counts */
        k->brk -= in->dt;
        if (k->brk <= 0.0f) {
            k->brk = 0.0f;
            if (k->mode == MODE_SKATE && k->state == SK_OPP_SETS) k->msg = CT_MSG_OPP_SETS;
            else if (k->mode == MODE_SKATE) k->msg = CT_MSG_PLAYER_SETS;
        }
        return k->result;
    }
    if (k->mode == MODE_JAM) {
        k->t += in->dt;
        if (new_bank) k->player_score += contest_line_points(k, sc);
        for (int j = 0; j < k->nopp; j++) k->opp_score[j] = contest_opp_score_at(k, j, k->t);
        if (k->t >= k->limit && !sc->combo_active) return decided(k, k->player_score, k->opp_score);
        if (k->t >= k->limit + 3.0f) return decided(k, k->player_score, k->opp_score);          /* a line in progress gets 3 s to land */
    } else if (k->mode == MODE_BESTTRICK) {
        k->t += in->dt;
        if (new_bank) {
            long p = contest_line_points(k, sc);
            if (p > k->player_round) k->player_round = p;
        }
        if ((k->t >= k->limit && !sc->combo_active) || k->t >= k->limit + 3.0f) {
            k->round_score[k->round] = k->player_round;
            k->player_score += k->player_round;
            for (int j = 0; j < k->nopp; j++) { k->opp_round[j] = opp_best_line(k, k->round, j); k->opp_score[j] += k->opp_round[j]; }
            k->round++;
            k->t = 0.0f;
            if (k->round >= k->rounds) return decided(k, k->player_score, k->opp_score);
            k->player_round = 0;
            k->brk = CONTEST_BREAK_TIME;
            k->msg = CT_MSG_ROUND_END;
        }
    } else {
        skate_turn(k, in, new_bank);
    }
    return k->result;
}
