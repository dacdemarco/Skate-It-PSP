#include "ui.h"
#include <math.h>
#include <string.h>

const uint8_t __attribute__((aligned(16))) ui_font_clut[256 * 4] = {
    /* The XFN fonts are anti-aliased white glyphs: 1 = full coverage, 2..6 = the edge ramp (coverage falling in sixths), 7 and 0 = clear (7 marks the holes inside a glyph). An earlier
     * version drew 7 as an opaque black outline, which ate the gaps between the letters and speckled them. The text colour modulates the white. */
    /* 0 */ 255, 255, 255, 0,  /* 1 */ 255, 255, 255, 255,  /* 2 */ 255, 255, 255, 212,  /* 3 */ 255, 255, 255, 170,
    /* 4 */ 255, 255, 255, 128, /* 5 */ 255, 255, 255, 85,  /* 6 */ 255, 255, 255, 42,   /* 7 */ 255, 255, 255, 0,
    /* the rest is never used by the XFN fonts: white */
    [32] = 255, 255, 255, 255,
};

int uipak_load(UiPak *p, const uint8_t *buf, size_t size)
{
    memset(p, 0, sizeof(*p));
    if (size < 32 || memcmp(buf, "SKUI", 4) != 0)
        return 0;
    uint32_t h[8];
    memcpy(h, buf, sizeof(h));
    if (h[1] != 1 || h[2] > UI_MAX_IMG || h[4] > UI_MAX_FONT || h[6] > size)
        return 0;
    uint32_t nimg = h[2], off_img = h[3], nfont = h[4], off_font = h[5];
    if ((uint64_t)off_img + 48ull * nimg > size || (uint64_t)off_font + 32ull * nfont > size)
        return 0;
    p->file = buf; p->size = size; p->nimg = (int)nimg; p->nfont = (int)nfont;
    for (uint32_t i = 0; i < nimg; i++) {
        const uint8_t *e = buf + off_img + 48 * i;
        UiImage *im = &p->img[i];
        uint16_t w, hh, tw, th;
        uint32_t off_pix, off_clut;
        memcpy(im->name, e, 24); im->name[23] = 0;
        memcpy(&w, e + 24, 2); memcpy(&hh, e + 26, 2); memcpy(&tw, e + 28, 2); memcpy(&th, e + 30, 2);
        im->fmt = e[32];
        memcpy(&off_pix, e + 36, 4); memcpy(&off_clut, e + 40, 4);
        size_t bytes = (size_t)tw * th * (im->fmt == 1 ? 4 : 1);
        if ((uint64_t)off_pix + bytes > size || (im->fmt == 0 && (uint64_t)off_clut + 1024 > size) || (off_pix & 15) || (off_clut & 15))
            return 0;
        im->w = w; im->h = hh; im->tw = tw; im->th = th;
        im->pix = buf + off_pix;
        im->clut = im->fmt == 0 ? buf + off_clut : NULL;
    }
    for (uint32_t i = 0; i < nfont; i++) {
        const uint8_t *e = buf + off_font + 32 * i;
        UiFont *f = &p->font[i];
        uint16_t tw, th;
        uint32_t off_pix, off_gl;
        memcpy(f->name, e, 16); f->name[15] = 0;
        f->height = e[16];
        memcpy(&tw, e + 18, 2); memcpy(&th, e + 20, 2);
        memcpy(&off_pix, e + 24, 4); memcpy(&off_gl, e + 28, 4);
        if ((uint64_t)off_pix + (uint64_t)tw * th > size || (uint64_t)off_gl + 256 * 6 > size || (off_pix & 15) || (off_gl & 1))
            return 0;
        f->tw = tw; f->th = th;
        f->pix = buf + off_pix;
        f->glyph = (const UiGlyph *)(buf + off_gl);
    }
    return 1;
}

int uipak_image(const UiPak *p, const char *name)
{
    for (int i = 0; i < p->nimg; i++)
        if (strcmp(p->img[i].name, name) == 0)
            return i;
    return -1;
}

int uipak_font(const UiPak *p, const char *name)
{
    for (int i = 0; i < p->nfont; i++)
        if (strcmp(p->font[i].name, name) == 0)
            return i;
    return -1;
}

void ui_init(Ui *ui, const UiPak *pak, UiBackend be)
{
    ui->pak = pak;
    ui->be = be;
    ui_reset_view(ui);
}

void ui_set_view(Ui *ui, float ox, float oy, float scale) { ui->ox = ox; ui->oy = oy; ui->sc = scale; }
void ui_reset_view(Ui *ui) { ui->ox = 0.0f; ui->oy = 0.0f; ui->sc = 1.0f; }

static float snap(float v) { return floorf(v + 0.5f); }

static int is_whole(float v) { return fabsf(v - floorf(v + 0.5f)) < 0.01f; }

void ui_image_part(Ui *ui, int img, float sx, float sy, float sw, float sh, float x, float y, float w, float h, uint32_t color)
{
    if (img < 0 || img >= ui->pak->nimg || !ui->be.quad)
        return;
    const UiImage *im = &ui->pak->img[img];
    UiTex t = { im->pix, im->clut, im->tw, im->th, im->fmt };
    float x0 = snap(ui->ox + x * ui->sc), y0 = snap(ui->oy + y * ui->sc), x1 = snap(ui->ox + (x + w) * ui->sc), y1 = snap(ui->oy + (y + h) * ui->sc);
    int linear = !(is_whole((x1 - x0) / sw) && is_whole((y1 - y0) / sh));
    ui->be.quad(ui->be.ctx, &t, sx, sy, sx + sw, sy + sh, x0, y0, x1, y1, color, linear);
}

void ui_image(Ui *ui, int img, float x, float y, float w, float h, uint32_t color)
{
    if (img < 0 || img >= ui->pak->nimg)
        return;
    ui_image_part(ui, img, 0, 0, (float)ui->pak->img[img].w, (float)ui->pak->img[img].h, x, y, w, h, color);
}

void ui_image_cover(Ui *ui, int img, uint32_t color)
{
    if (img < 0 || img >= ui->pak->nimg)
        return;
    const UiImage *im = &ui->pak->img[img];
    float sc = (float)UI_W / (float)im->w, sc2 = (float)UI_H / (float)im->h;
    if (sc2 > sc) sc = sc2;
    float vw = (float)UI_W / sc, vh = (float)UI_H / sc;                  /* the part of the picture that is seen */
    Ui whole = *ui;
    ui_reset_view(&whole);
    ui_image_part(&whole, img, ((float)im->w - vw) * 0.5f, ((float)im->h - vh) * 0.5f, vw, vh, 0, 0, UI_W, UI_H, color);
}

void ui_image_fit_height(Ui *ui, int img, uint32_t color)
{
    if (img < 0 || img >= ui->pak->nimg)
        return;
    const UiImage *im = &ui->pak->img[img];
    float sc = (float)UI_H / (float)im->h, w = (float)im->w * sc;
    Ui whole = *ui;
    ui_reset_view(&whole);
    ui_image(&whole, img, ((float)UI_W - w) * 0.5f, 0, w, UI_H, color);
}

void ui_rect(Ui *ui, float x, float y, float w, float h, uint32_t color)
{
    if (ui->be.rect)
        ui->be.rect(ui->be.ctx, snap(ui->ox + x * ui->sc), snap(ui->oy + y * ui->sc), snap(ui->ox + (x + w) * ui->sc), snap(ui->oy + (y + h) * ui->sc), color);
}

float ui_text_width(const Ui *ui, int font, float scale, const char *s)
{
    if (font < 0 || font >= ui->pak->nfont)
        return 0.0f;
    const UiFont *f = &ui->pak->font[font];
    float w = 0.0f;
    for (; *s; s++)
        if (f->glyph[(uint8_t)*s].adv) w += (float)(f->glyph[(uint8_t)*s].adv + UI_TRACK) * scale;
    return w / ui->sc;
}

void ui_text(Ui *ui, int font, float x, float y, float scale, const char *s, uint32_t color)
{
    if (font < 0 || font >= ui->pak->nfont || !ui->be.quad)
        return;
    const UiFont *f = &ui->pak->font[font];
    UiTex t = { f->pix, ui_font_clut, f->tw, f->th, 0 };
    int linear = !is_whole(scale);
    x = snap(ui->ox + x * ui->sc);                                       /* the position goes through the view, the glyphs keep their pixel size */
    y = snap(ui->oy + y * ui->sc);
    for (; *s; s++) {
        const UiGlyph *g = &f->glyph[(uint8_t)*s];
        if (g->adv == 0)
            continue;
        if (g->w > 0) {                                        /* glyphs sit on whole pixels (a half-pixel offset would drop and double texel columns) */
            float gx = floorf(x + 0.5f), gy = floorf(y + 0.5f);
            ui->be.quad(ui->be.ctx, &t, g->x, g->y, (float)(g->x + g->w), (float)(g->y + f->height), gx, gy, gx + (float)g->w * scale, gy + (float)f->height * scale, color, linear);
        }
        x += (float)(g->adv + UI_TRACK) * scale;
    }
}

void ui_text_shadow(Ui *ui, int font, float x, float y, float scale, const char *s, uint32_t color)
{
    ui_text(ui, font, x + scale, y + scale, scale, s, UI_RGBA(0, 0, 0, ((color >> 24) & 255) * 200 / 255));
    ui_text(ui, font, x, y, scale, s, color);
}

void ui_text_center(Ui *ui, int font, float cx, float y, float scale, const char *s, uint32_t color)
{
    ui_text(ui, font, cx - ui_text_width(ui, font, scale, s) * 0.5f, y, scale, s, color);
}

void ui_text_right(Ui *ui, int font, float xr, float y, float scale, const char *s, uint32_t color)
{
    ui_text(ui, font, xr - ui_text_width(ui, font, scale, s), y, scale, s, color);
}
