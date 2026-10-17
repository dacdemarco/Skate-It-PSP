#include "hud.h"
#include <stdio.h>
#include <string.h>

#define STRIP_X 10
#define STRIP_BODY_Y 4                  /* the film strip's picture is 86 x 20: its body (where the fill goes) starts 4 rows down and is 13 rows high */
#define STRIP_BODY_H 13

void hud_init(Hud *h, const UiPak *p)
{
    h->f_xl = uipak_font(p, "fe_extralarge"); h->f_l = uipak_font(p, "fe_large"); h->f_m = uipak_font(p, "fe_medium"); h->f_s = uipak_font(p, "fe_small");
    h->i_strip = uipak_image(p, "spr_strip"); h->i_fill = uipak_image(p, "spr_fill");
    h->i_x15 = uipak_image(p, "spr_x15"); h->i_x2 = uipak_image(p, "spr_x2"); h->i_x3 = uipak_image(p, "spr_x3");
    h->i_digit[0] = uipak_image(p, "spr_d1"); h->i_digit[1] = uipak_image(p, "spr_d2"); h->i_digit[2] = uipak_image(p, "spr_d3");
}

/* the white text with a dark outline all round (the originals' HUD text reads on any background) */
static void outlined(Ui *ui, int font, float x, float y, float scale, const char *s, uint32_t color)
{
    static const int d[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    uint32_t dark = UI_RGBA(0, 0, 0, (color >> 24) & 255);
    for (int k = 0; k < 8; k++)
        ui_text(ui, font, x + (float)d[k][0] * scale, y + (float)d[k][1] * scale, scale, s, dark);
    ui_text(ui, font, x, y, scale, s, color);
}

void hud_text(const Hud *h, Ui *ui, int font, float x, float y, const char *s, uint32_t color)
{
    (void)h;
    outlined(ui, font, x, y, 1.0f, s, color);
}

void hud_text_right(const Hud *h, Ui *ui, int font, float xr, float y, const char *s, uint32_t color)
{
    (void)h;
    outlined(ui, font, xr - ui_text_width(ui, font, 1.0f, s), y, 1.0f, s, color);
}

void hud_text_center(const Hud *h, Ui *ui, int font, float cx, float y, float scale, const char *s, uint32_t color)
{
    (void)h;
    outlined(ui, font, cx - ui_text_width(ui, font, scale, s) * 0.5f, y, scale, s, color);
}

void hud_title_case(const char *in, char *out, size_t n)
{
    size_t o = 0;
    int start = 1, wlen = 0;
    for (const char *p = in; *p && o + 1 < n; p++) {
        if (*p == ' ') { start = 1; out[o++] = *p; continue; }
        if (start) {
            wlen = 0;
            for (const char *q = p; *q && *q != ' '; q++) wlen++;
        }
        char c = *p;
        if (wlen > 2 && !start && c >= 'A' && c <= 'Z') c = (char)(c + 32);
        out[o++] = c;
        start = 0;
    }
    out[o] = 0;
}

void hud_combo(const Hud *h, Ui *ui, const char *name, const char *label, long pts, int level)
{
    char nm[64], line[48];
    hud_title_case(name, nm, sizeof(nm));
    snprintf(line, sizeof(line), "%s%ld pts", label ? label : "", pts);
    float sy = (float)UI_H - 30.0f;                                       /* the strip's top */
    outlined(ui, h->f_s, STRIP_X, sy - 36.0f, 1.0f, nm, UI_WHITE);
    outlined(ui, h->f_s, STRIP_X, sy - 20.0f, 1.0f, line, UI_WHITE);
    ui_image(ui, h->i_strip, STRIP_X, sy, 88, 32, UI_WHITE);
    static const int reach[4] = { 0, 43, 63, 85 };                       /* where the fill ends for each level (the end of its box) */
    if (level > 3) level = 3;
    if (level > 0) {
        float w = (float)reach[level] - 1.0f;
        ui_image_part(ui, h->i_fill, 0, 0, w, STRIP_BODY_H, STRIP_X + 1, sy + STRIP_BODY_Y, w, STRIP_BODY_H, UI_WHITE);
        ui_image(ui, h->i_x15, STRIP_X + 22, sy + STRIP_BODY_Y, 24, 16, UI_WHITE);
        if (level > 1) ui_image(ui, h->i_x2, STRIP_X + 42, sy + STRIP_BODY_Y, 24, 16, UI_WHITE);
        if (level > 2) ui_image(ui, h->i_x3, STRIP_X + 62, sy + STRIP_BODY_Y, 24, 16, UI_WHITE);
    }
}

void hud_digit(const Hud *h, Ui *ui, int n)
{
    if (n < 1 || n > 3)
        return;
    ui_image(ui, h->i_digit[n - 1], (float)UI_W / 2.0f - 32.0f, 70.0f, 64, 64, UI_WHITE);
}
