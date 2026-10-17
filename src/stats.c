#include "stats.h"
#include <math.h>
#include <string.h>

enum { A_TIME = 0, A_DIST, A_GRIND, A_MANUAL, A_SLIDE, A_AIRGRAB };

static const int CLASS_FIRST[5] = { 0, NGRIND_TYPES, NGRIND_TYPES + NMANUAL_TYPES, NGRIND_TYPES + NMANUAL_TYPES + NFLIP_TYPES, NGRIND_TYPES + NMANUAL_TYPES + NFLIP_TYPES + NGRAB_TYPES };
static const int CLASS_COUNT[5] = { NGRIND_TYPES, NMANUAL_TYPES, NFLIP_TYPES, NGRAB_TYPES, NMOVE_TYPES };
/* the order of the bits: grinds, manuals, flips, grabs, moves (the class numbers of src/trick.h are 0 grind, 1 manual, 2 flip, 3 grab, 4 move) */

int stats_landed_bit(int cls, int type)
{
    if (cls < 0 || cls > 4 || type < 0 || type >= CLASS_COUNT[cls]) return -1;
    return CLASS_FIRST[cls] + type;
}

void stats_bit_trick(int bit, int *cls, int *type)
{
    for (int c = 4; c >= 0; c--)
        if (bit >= CLASS_FIRST[c]) { *cls = c; *type = bit - CLASS_FIRST[c]; return; }
    *cls = 0; *type = 0;
}

int stats_landed(const Profile *p, int cls, int type)
{
    int b = stats_landed_bit(cls, type);
    return b >= 0 && ((p->landed[b >> 3] >> (b & 7)) & 1);
}

int stats_landed_count(const Profile *p)
{
    int n = 0;
    for (int b = 0; b < STATS_NTRICKS; b++) n += (p->landed[b >> 3] >> (b & 7)) & 1;
    return n;
}

void stats_begin(StatState *st, const Score *sc)
{
    memset(st, 0, sizeof(*st));
    st->prev_push_t = -1.0f;
    st->bank_serial = sc->bank_serial;
}

/* adds `amount` (a float in the unit of the counter) to a counter, keeping the fraction in acc[slot] */
static void add(Profile *p, StatState *st, int slot, int stat, float amount)
{
    st->acc[slot] += amount;
    if (st->acc[slot] >= 1.0f) {
        uint32_t whole = (uint32_t)st->acc[slot];
        p->stat[stat] += whole;
        st->acc[slot] -= (float)whole;
    }
}

static void best(Profile *p, int stat, float value)
{
    uint32_t v = value > 0.0f ? (uint32_t)value : 0;
    if (v > p->stat[stat]) p->stat[stat] = v;
}

void stats_frame(Profile *p, StatState *st, const Skate *s, const Score *sc, float dt)
{
    if (dt <= 0.0f) return;
    add(p, st, A_TIME, STAT_PLAY_TIME, dt);
    int air = !s->grounded && !s->grinding;
    float v = air ? sqrtf(s->vel[0] * s->vel[0] + s->vel[2] * s->vel[2]) : fabsf(s->speed);
    float d = v * dt;
    add(p, st, A_DIST, STAT_DIST, d * 100.0f);
    if (s->grinding) { add(p, st, A_GRIND, STAT_DIST_GRIND, d * 100.0f); st->run_grind += d; best(p, STAT_BEST_GRIND, st->run_grind * 100.0f); } else st->run_grind = 0.0f;
    if (s->manual && s->grounded && !s->grinding) { add(p, st, A_MANUAL, STAT_DIST_MANUAL, d * 100.0f); st->run_manual += d; best(p, STAT_BEST_MANUAL, st->run_manual * 100.0f); } else st->run_manual = 0.0f;
    if (s->sliding && s->grounded) { add(p, st, A_SLIDE, STAT_DIST_SLIDE, d * 100.0f); st->run_slide += d; best(p, STAT_BEST_SLIDE, st->run_slide * 100.0f); } else st->run_slide = 0.0f;
    if (air) {
        best(p, STAT_BEST_AIR, s->air_t * 1000.0f);
        if (s->grab) add(p, st, A_AIRGRAB, STAT_AIRGRAB_TIME, dt);
    }
    if (s->push_t >= 0.0f && st->prev_push_t < 0.0f) p->stat[STAT_PUSHES]++;
    st->prev_push_t = s->push_t;
    if (s->bail_t > 0.0f && st->prev_bail_t <= 0.0f) p->stat[STAT_BAILS]++;
    st->prev_bail_t = s->bail_t;
    if (sc->bank_serial != st->bank_serial) {                      /* a combo was banked: its tricks are landed, its points are a candidate for the best sequence */
        st->bank_serial = sc->bank_serial;
        for (int i = 0; i < sc->bank_n && i < SCORE_LOG_MAX; i++) {
            const TrickRec *r = &sc->bank_log[i];
            if (r->type == TREC_OLLIE) continue;                   /* (a plain ollie is not in the book) */
            int b = stats_landed_bit(r->cls, r->type);
            if (b < 0) continue;
            p->stat[STAT_TRICKS]++;
            p->landed[b >> 3] |= (uint8_t)(1u << (b & 7));
        }
        if (sc->bank_pts > 0) best(p, STAT_BEST_LINE, (float)sc->bank_pts);
    }
}
