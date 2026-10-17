#include "sparks.h"
#include "rails.h"
#include <math.h>

#define SPARK_GRAVITY   9.81f
#define METAL_RATE      150.0f      /* sparks per second at 3 m/s on a rail */
#define CONC_RATE        55.0f

void sparks_init(Sparks *sp)
{
    sp->n = 0; sp->rng = 0x1234567u; sp->emit_acc = 0.0f;
}

static float rnd(Sparks *sp)                 /* 0..1 */
{
    sp->rng = sp->rng * 1664525u + 1013904223u;
    return (float)(sp->rng >> 8) * (1.0f / 16777216.0f);
}

static void emit(Sparks *sp, const Skate *s, int metal)
{
    if (sp->n >= MAX_SPARKS) return;
    Spark *k = &sp->s[sp->n++];
    float side[3] = { s->fwd[2], 0.0f, -s->fwd[0] };           /* the rider's left, horizontal */
    float along = (rnd(sp) - 0.5f) * 0.5f;                     /* from the trucks / the board's length */
    float lat = (rnd(sp) - 0.5f) * 2.0f * (metal ? 1.3f : 0.9f);
    float up = (metal ? 1.2f : 0.8f) + rnd(sp) * (metal ? 2.6f : 1.6f);
    float back = 0.15f + rnd(sp) * 0.3f;
    for (int i = 0; i < 3; i++) {
        k->p[i] = s->pos[i] + s->fwd[i] * along + s->cn[i] * 0.04f;
        k->v[i] = -s->fwd[i] * s->speed * back + side[i] * lat + s->cn[i] * up * 0.6f;
    }
    k->v[1] += up * 0.7f;                                      /* mostly up in the world, whatever the rail's slope */
    k->max_life = k->life = (metal ? 0.30f : 0.22f) + rnd(sp) * (metal ? 0.30f : 0.18f);
    k->metal = metal;
}

void sparks_update(Sparks *sp, const Skate *s, int mat, float dt)
{
    /* age and move */
    for (int i = 0; i < sp->n; ) {
        Spark *k = &sp->s[i];
        k->life -= dt;
        if (k->life <= 0.0f) { *k = sp->s[--sp->n]; continue; }
        k->v[1] -= SPARK_GRAVITY * dt;
        for (int a = 0; a < 3; a++) k->p[a] += k->v[a] * dt;
        i++;
    }
    /* emit */
    if (s->grinding && mat != 2) {
        int metal = mat == 1;
        float sp_f = fabsf(s->speed) / 3.0f;
        if (sp_f > 1.6f) sp_f = 1.6f;
        if (sp_f < 0.3f) sp_f = 0.3f;
        sp->emit_acc += (metal ? METAL_RATE : CONC_RATE) * sp_f * dt;
        while (sp->emit_acc >= 1.0f) { emit(sp, s, metal); sp->emit_acc -= 1.0f; }
    } else {
        sp->emit_acc = 0.0f;
    }
}

uint32_t spark_color(const Spark *k)
{
    float t = k->life / k->max_life;                         /* 1 fresh .. 0 dying */
    float a = t > 0.5f ? 1.0f : t * 2.0f;
    int r, g, b;
    if (k->metal) { r = 255; g = (int)(110.0f + 140.0f * t); b = (int)(160.0f * t * t); }
    else          { r = (int)(200.0f + 55.0f * t); g = r - 10; b = r - 25; }
    return ((uint32_t)(a * 255.0f) << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}
