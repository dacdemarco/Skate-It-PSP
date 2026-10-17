/* A software renderer behind the UiBackend (host tests only): 480 x 272 RGB canvas, axis-aligned textured quads with nearest / bilinear sampling, colour modulation and alpha blending
 * (src * alpha + dst * (1 - alpha), like the PSP's blend setup), and a PPM writer so a screen can be looked at. */
#ifndef UI_SW_H
#define UI_SW_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ui.h"

typedef struct { uint8_t px[UI_W * UI_H * 3]; } SwCanvas;

static void sw_clear(SwCanvas *c, int r, int g, int b)
{
    for (int i = 0; i < UI_W * UI_H; i++) { c->px[3 * i] = (uint8_t)r; c->px[3 * i + 1] = (uint8_t)g; c->px[3 * i + 2] = (uint8_t)b; }
}

static void sw_blend(SwCanvas *c, int x, int y, int r, int g, int b, int a)
{
    if (x < 0 || y < 0 || x >= UI_W || y >= UI_H || a <= 0) return;
    uint8_t *d = &c->px[3 * (y * UI_W + x)];
    d[0] = (uint8_t)((r * a + d[0] * (255 - a)) / 255);
    d[1] = (uint8_t)((g * a + d[1] * (255 - a)) / 255);
    d[2] = (uint8_t)((b * a + d[2] * (255 - a)) / 255);
}

static void sw_texel(const UiTex *t, int u, int v, int out[4])
{
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (u >= t->tw) u = t->tw - 1;
    if (v >= t->th) v = t->th - 1;
    if (t->fmt == 1) { const uint8_t *p = t->pix + 4 * (v * t->tw + u); for (int i = 0; i < 4; i++) out[i] = p[i]; }
    else { const uint8_t *p = t->clut + 4 * t->pix[v * t->tw + u]; for (int i = 0; i < 4; i++) out[i] = p[i]; }
}

static void sw_quad(void *ctx, const UiTex *t, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, int linear)
{
    SwCanvas *c = (SwCanvas *)ctx;
    int cr = color & 255, cg = (color >> 8) & 255, cb = (color >> 16) & 255, ca = (color >> 24) & 255;
    int ix0 = (int)floorf(x0 + 0.5f), ix1 = (int)floorf(x1 + 0.5f), iy0 = (int)floorf(y0 + 0.5f), iy1 = (int)floorf(y1 + 0.5f);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float fu = u0 + ((float)x + 0.5f - x0) / (x1 - x0) * (u1 - u0), fv = v0 + ((float)y + 0.5f - y0) / (y1 - y0) * (v1 - v0);
            int s[4];
            if (!linear) sw_texel(t, (int)floorf(fu), (int)floorf(fv), s);
            else {
                float gu = fu - 0.5f, gv = fv - 0.5f; int iu = (int)floorf(gu), iv = (int)floorf(gv); float au = gu - (float)iu, av = gv - (float)iv;
                int a[4], b[4], cc[4], d[4];
                sw_texel(t, iu, iv, a); sw_texel(t, iu + 1, iv, b); sw_texel(t, iu, iv + 1, cc); sw_texel(t, iu + 1, iv + 1, d);
                for (int i = 0; i < 4; i++) s[i] = (int)((a[i] * (1 - au) + b[i] * au) * (1 - av) + (cc[i] * (1 - au) + d[i] * au) * av);
            }
            sw_blend(c, x, y, s[0] * cr / 255, s[1] * cg / 255, s[2] * cb / 255, s[3] * ca / 255);
        }
}

static void sw_rect(void *ctx, float x0, float y0, float x1, float y1, uint32_t color)
{
    SwCanvas *c = (SwCanvas *)ctx;
    for (int y = (int)floorf(y0 + 0.5f); y < (int)floorf(y1 + 0.5f); y++)
        for (int x = (int)floorf(x0 + 0.5f); x < (int)floorf(x1 + 0.5f); x++)
            sw_blend(c, x, y, color & 255, (color >> 8) & 255, (color >> 16) & 255, (color >> 24) & 255);
}

static UiBackend sw_backend(SwCanvas *c)
{
    UiBackend b = { sw_quad, sw_rect, c };
    return b;
}

static int sw_save_ppm(const SwCanvas *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", UI_W, UI_H);
    fwrite(c->px, 1, sizeof(c->px), f);
    fclose(f);
    return 1;
}

#endif
