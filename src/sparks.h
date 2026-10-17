/* Sparks thrown off the board while it grinds (no PSP dependencies - host-tested in tests/test_sparks.c; src/main.c draws them as short streaks).
 * Metal throws plenty of hot yellow-orange sparks, concrete fewer, paler ones, wood none. They leave from the contact point against the direction of travel and fly up and
 * sideways, then fall under gravity. */
#ifndef SPARKS_H
#define SPARKS_H

#include <stdint.h>
#include "skate.h"

#define MAX_SPARKS 96

typedef struct {
    float p[3], v[3];
    float life, max_life;       /* seconds left / total */
    int metal;
} Spark;

typedef struct {
    Spark s[MAX_SPARKS];
    int n;                      /* live sparks are s[0..n-1] */
    uint32_t rng;
    float emit_acc;             /* fractional sparks carried over */
} Sparks;

void sparks_init(Sparks *sp);
/* emit while the skater grinds something made of `mat` (0 concrete, 1 metal, 2 wood: none), age and move everything; call once per frame */
void sparks_update(Sparks *sp, const Skate *s, int mat, float dt);
/* colour of a spark as 0xAABBGGRR (the PSP's ABGR): hot white-yellow fading to orange-red and transparent */
uint32_t spark_color(const Spark *k);

#endif
