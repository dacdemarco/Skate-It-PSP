/* 2D user interface drawing on top of data/ui.pak (tools/convert_ui.py: the original's front-end pictures and bitmap fonts). No PSP dependencies: the platform supplies a UiBackend that draws
 * textured quads and rectangles (src/ui_gu.c on the PSP, tests/ui_sw.c as a software renderer that writes screenshots for the host tests). Coordinates are screen pixels (480 x 272),
 * colours are PSP ABGR (0xAABBGGRR). */
#ifndef SKATE_UI_H
#define SKATE_UI_H

#include <stddef.h>
#include <stdint.h>

#define UI_W 480
#define UI_H 272
#define UI_MAX_IMG 96
#define UI_MAX_FONT 8
#define UI_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
#define UI_WHITE 0xFFFFFFFFu
#define UI_TRACK 1                  /* extra pixels between the letters of every text (the fonts' own advances are tight) */

typedef struct { uint16_t x, y; uint8_t w, adv; } UiGlyph;
typedef struct { char name[24]; int w, h, tw, th, fmt; const uint8_t *pix, *clut; } UiImage;       /* fmt 0 = T8 + RGBA8888 palette, 1 = RGBA8888 */
typedef struct { char name[16]; int height, tw, th; const uint8_t *pix; const UiGlyph *glyph; } UiFont;

typedef struct {
    const uint8_t *file;
    size_t size;
    int nimg, nfont;
    UiImage img[UI_MAX_IMG];
    UiFont font[UI_MAX_FONT];
} UiPak;

/* the buffer stays owned by the caller (16-byte aligned on the PSP: the GPU reads it); returns 0 if it is not a ui.pak or runs past its size */
int uipak_load(UiPak *p, const uint8_t *buf, size_t size);
int uipak_image(const UiPak *p, const char *name);      /* index or -1 */
int uipak_font(const UiPak *p, const char *name);

/* a texture as the backend gets it */
typedef struct { const uint8_t *pix, *clut; int tw, th, fmt; } UiTex;
extern const uint8_t ui_font_clut[256 * 4];             /* the palette of the font atlases: R, G, B, A per index (0 clear, 1 white fill, 2..6 shading, 7 black outline) */

typedef struct {
    /* the part (u0,v0)-(u1,v1) of the texture, in texels, onto the screen rectangle; colour modulates; linear = bilinear filtering */
    void (*quad)(void *ctx, const UiTex *t, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, int linear);
    void (*rect)(void *ctx, float x0, float y0, float x1, float y1, uint32_t color);
    void *ctx;
} UiBackend;

/* A view maps layout coordinates to the screen: screen = origin + layout * scale (pictures and rectangles scale; text keeps its pixel size and only its position moves), so a DS screen
 * (256 x 192) can be laid out in its own pixels and placed on part of the PSP screen. Widths from ui_text_width are in LAYOUT units. */
typedef struct { const UiPak *pak; UiBackend be; float ox, oy, sc; } Ui;
void ui_set_view(Ui *ui, float ox, float oy, float scale);
void ui_reset_view(Ui *ui);

void ui_init(Ui *ui, const UiPak *pak, UiBackend be);
void ui_image(Ui *ui, int img, float x, float y, float w, float h, uint32_t color);
void ui_image_part(Ui *ui, int img, float sx, float sy, float sw, float sh, float x, float y, float w, float h, uint32_t color);
/* the picture scaled so that it covers the whole screen (what does not fit is cropped, centred) */
void ui_image_cover(Ui *ui, int img, uint32_t color);
/* the picture as large as fits inside the screen's height, centred horizontally */
void ui_image_fit_height(Ui *ui, int img, uint32_t color);
void ui_rect(Ui *ui, float x, float y, float w, float h, uint32_t color);
void ui_text(Ui *ui, int font, float x, float y, float scale, const char *s, uint32_t color);
/* the same with a dark drop shadow one scaled pixel down and right (for the fonts without an outline: fetiny) */
void ui_text_shadow(Ui *ui, int font, float x, float y, float scale, const char *s, uint32_t color);
float ui_text_width(const Ui *ui, int font, float scale, const char *s);
void ui_text_center(Ui *ui, int font, float cx, float y, float scale, const char *s, uint32_t color);
void ui_text_right(Ui *ui, int font, float xr, float y, float scale, const char *s, uint32_t color);

#endif
