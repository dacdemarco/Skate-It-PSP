/* The in-game HUD in the look of the original's: the trick in progress with its points over the film-strip multiplier bar at the bottom left, outlined white text, the countdown digits.
 * No PSP dependencies: it draws through src/ui.h (host-tested by tests/fe_preview.c, which renders sample frames). Everything is in screen pixels (480 x 272) and 1:1 with the font pixels. */
#ifndef HUD_H
#define HUD_H

#include "ui.h"

typedef struct {
    int f_xl, f_l, f_m, f_s;
    int i_strip, i_fill, i_x15, i_x2, i_x3, i_digit[3];
} Hud;

void hud_init(Hud *h, const UiPak *pak);
/* The trick block at the bottom left: the trick's name ("KICKFLIP" is shown as "Kickflip"), "<label>N pts" under it and the multiplier strip: level 0 = empty, 1..3 = the x1.5 / x2 / x3
 * boxes lit (the fill reaches the end of the box). */
void hud_combo(const Hud *h, Ui *ui, const char *name, const char *label, long pts, int level);
/* outlined text: left, right-aligned or centred at x */
void hud_text(const Hud *h, Ui *ui, int font, float x, float y, const char *s, uint32_t color);
void hud_text_right(const Hud *h, Ui *ui, int font, float xr, float y, const char *s, uint32_t color);
void hud_text_center(const Hud *h, Ui *ui, int font, float cx, float y, float scale, const char *s, uint32_t color);
/* the challenge countdown: the big digit sprite for 3, 2, 1 */
void hud_digit(const Hud *h, Ui *ui, int n);
/* "NOLLIE KICKFLIP" -> "Nollie Kickflip" (words of up to two letters, "BS" / "FS", stay as they are) */
void hud_title_case(const char *in, char *out, size_t n);

#endif
