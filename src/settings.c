#include "settings.h"
#include <stdio.h>
#include <string.h>

/* the CTL_* bit of each action (src/control.h): CTL_CROSS = 1 .. CTL_R = 32, in the order of the ACT_* enum */
static const unsigned ACT_BIT[ACT_COUNT] = { 1, 2, 4, 8, 16, 32 };

/* map[action] = button for each preset, in ACT_* order: pop, push, brake, advanced pop, grab L, grab R */
static const uint8_t PRESET_MAP[PRESET_CUSTOM][ACT_COUNT] = {
    { PB_CROSS, PB_SQUARE, PB_CIRCLE, PB_TRIANGLE, PB_L, PB_R },        /* default: X pops the basic tricks, Triangle the advanced ones */
    { PB_TRIANGLE, PB_SQUARE, PB_CIRCLE, PB_CROSS, PB_L, PB_R },        /* swapped pops: Triangle basic, X advanced */
    { PB_CIRCLE, PB_CROSS, PB_SQUARE, PB_TRIANGLE, PB_L, PB_R },        /* circle pop: ollie on Circle (as in many Japanese games), push on X, brake on Square */
    { PB_CROSS, PB_SQUARE, PB_L, PB_TRIANGLE, PB_CIRCLE, PB_R },        /* shoulder brake: the brake on L, a back-hand grab on Circle */
};

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    settings_apply_preset(s, PRESET_DEFAULT);
    for (int t = 0; t < TUNE_COUNT; t++)
        s->tune[t] = 100;
    s->stance = STANCE_OWN; s->units = UNITS_METERS;
    s->sfx_vol = s->music_vol = VOL_MAX;
    s->profile_slot = 0;
}

void settings_apply_preset(Settings *s, int preset)
{
    if (preset < 0 || preset >= PRESET_COUNT)
        preset = PRESET_DEFAULT;
    if (preset != PRESET_CUSTOM)
        memcpy(s->map, PRESET_MAP[preset], ACT_COUNT);
    s->preset = preset;
}

void settings_assign(Settings *s, int action, int button)
{
    if (action < 0 || action >= ACT_COUNT || button < 0 || button >= PB_COUNT)
        return;
    int old = s->map[action];
    for (int a = 0; a < ACT_COUNT; a++)
        if (a != action && s->map[a] == button)
            s->map[a] = (uint8_t)old;
    s->map[action] = (uint8_t)button;
    s->preset = PRESET_CUSTOM;
}

unsigned settings_ctl_bits(const Settings *s, unsigned phys)
{
    unsigned r = 0;
    for (int a = 0; a < ACT_COUNT; a++)
        if (phys & PBM(s->map[a]))
            r |= ACT_BIT[a];
    return r;
}

int settings_match_preset(const Settings *s)
{
    for (int p = 0; p < PRESET_CUSTOM; p++)
        if (memcmp(s->map, PRESET_MAP[p], ACT_COUNT) == 0)
            return p;
    return PRESET_CUSTOM;
}

void settings_tune_step(Settings *s, int tune, int dir)
{
    if (tune < 0 || tune >= TUNE_COUNT)
        return;
    int v = (int)s->tune[tune] + (dir > 0 ? TUNE_STEP : -TUNE_STEP);
    if (v < TUNE_MIN) v = TUNE_MIN;
    if (v > TUNE_MAX) v = TUNE_MAX;
    s->tune[tune] = (uint8_t)v;
}

float settings_tune_scale(const Settings *s, int tune)
{
    if (tune < 0 || tune >= TUNE_COUNT || s->tune[tune] < TUNE_MIN || s->tune[tune] > TUNE_MAX)
        return 1.0f;
    return (float)s->tune[tune] / 100.0f;
}

int settings_riding_regular(const Settings *s, int skater_regular)
{
    if (s->stance == STANCE_REGULAR) return 1;
    if (s->stance == STANCE_GOOFY) return 0;
    return skater_regular != 0;
}

float settings_volume_scale(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > VOL_MAX) vol = VOL_MAX;
    return (float)vol / (float)VOL_MAX;
}

const char *settings_stance_name(int st)
{
    static const char *const n[STANCE_COUNT] = { "Skater's own", "Regular", "Goofy" };
    return st >= 0 && st < STANCE_COUNT ? n[st] : "";
}

const char *settings_units_name(int u)
{
    static const char *const n[UNITS_COUNT] = { "Meters", "Feet" };
    return u >= 0 && u < UNITS_COUNT ? n[u] : "";
}

const char *settings_stance_help(void)
{
    return "Which foot leads. Regular and goofy mirror the rider; backside and frontside follow the stance. The skater's own is the pro's real stance.";
}

void settings_format_speed(const Settings *s, float mps, char *out, size_t cap)
{
    if (cap == 0) return;
    if (s->units == UNITS_FEET) snprintf(out, cap, "%d mph", (int)(mps * 2.23694f + 0.5f));
    else snprintf(out, cap, "%d km/h", (int)(mps * 3.6f + 0.5f));
}

void settings_format_distance(const Settings *s, float m, char *out, size_t cap)
{
    if (cap == 0) return;
    if (s->units == UNITS_FEET) snprintf(out, cap, "%d ft", (int)(m * 3.28084f + 0.5f));
    else snprintf(out, cap, "%d m", (int)(m + 0.5f));
}

const uint8_t *settings_preset_map(int p)
{
    return p >= 0 && p < PRESET_CUSTOM ? PRESET_MAP[p] : NULL;
}

const char *settings_preset_name(int p)
{
    static const char *const n[PRESET_COUNT] = { "Default", "Swapped pops", "Circle pop", "Shoulder brake", "Custom" };
    return p >= 0 && p < PRESET_COUNT ? n[p] : "";
}

const char *settings_action_name(int a)
{
    static const char *const n[ACT_COUNT] = { "Pop / ollie", "Push", "Brake / revert", "Advanced pop", "Grab (back hand)", "Grab (front hand)" };
    return a >= 0 && a < ACT_COUNT ? n[a] : "";
}

const char *settings_action_help(int a)
{
    static const char *const n[ACT_COUNT] = {
        "Hold to crouch, let go to ollie. Flick the nub while held for the basic flips; hold the nub forward first for a nollie.",
        "Tap for a quick push, hold for a strong one. Pressed in the air: the nub spins, a flick up / down flips the body.",
        "Brake. Held with a flick: revert. Held hard over: powerslide.",
        "Crouch and pop like the pop button, with the advanced flips on the flick. Held in the air: the advanced grinds. Both pops: body flips.",
        "In the air: a back-hand grab. While a pop is held on the ground: steer a little left.",
        "In the air: a front-hand grab. While a pop is held on the ground: steer a little right.",
    };
    return a >= 0 && a < ACT_COUNT ? n[a] : "";
}

const char *settings_button_name(int b)
{
    static const char *const n[PB_COUNT] = { "X", "Square", "Circle", "Triangle", "L", "R" };
    return b >= 0 && b < PB_COUNT ? n[b] : "";
}

/* the text with the {POP} {ADV} {PUSH} {BRAKE} {GL} {GR} tokens replaced by the names of the buttons of the mapping (anything else is copied as it is) */
void settings_expand_buttons(const Settings *s, const char *tmpl, char *out, size_t cap)
{
    static const struct { const char *tok; int act; } T[] = { { "{POP}", ACT_POP }, { "{ADV}", ACT_POP_ADV }, { "{PUSH}", ACT_PUSH }, { "{BRAKE}", ACT_BRAKE }, { "{GL}", ACT_GRAB_L }, { "{GR}", ACT_GRAB_R } };
    size_t o = 0;
    if (cap == 0)
        return;
    while (*tmpl && o + 1 < cap) {
        int done = 0;
        if (*tmpl == '{') {
            for (unsigned k = 0; k < sizeof(T) / sizeof(T[0]); k++) {
                size_t tl = strlen(T[k].tok);
                if (strncmp(tmpl, T[k].tok, tl) == 0) {
                    const char *b = settings_button_name(s->map[T[k].act]);
                    while (*b && o + 1 < cap) out[o++] = *b++;
                    tmpl += tl; done = 1;
                    break;
                }
            }
        }
        if (!done) out[o++] = *tmpl++;
    }
    out[o] = 0;
}

const char *settings_info_name(int i)
{
    static const char *const n[INFO_COUNT] = { "FPS", "Speed", "CPU / GPU time", "Heap memory", "Position", "Trick and pose", "Nub and flicks", "Rail probe", "Surface", "Clip stats" };
    return i >= 0 && i < INFO_COUNT ? n[i] : "";
}

const char *settings_info_help(int i)
{
    static const char *const n[INFO_COUNT] = {
        "Frames per second.",
        "The rider's speed in km/h.",
        "Milliseconds per frame: CPU, GPU, physics and skinning.",
        "Memory the game has used and the biggest free block.",
        "Where the rider is (x, y, z in metres) and which way he faces.",
        "The trick in progress and the number of its pose clip.",
        "The nub and the last flick, for tuning the gesture.",
        "The nearest rail or ledge and why the board would not lock on.",
        "The material under the wheels (concrete, metal, wood).",
        "Triangles redrawn by the near-camera clip pass.",
    };
    return i >= 0 && i < INFO_COUNT ? n[i] : "";
}

const char *settings_tune_name(int t)
{
    static const char *const n[TUNE_COUNT] = { "Gravity", "Ollie power", "Push speed" };
    return t >= 0 && t < TUNE_COUNT ? n[t] : "";
}

const char *settings_tune_help(int t)
{
    static const char *const n[TUNE_COUNT] = {
        "How hard the ground pulls: lower for floatier jumps and longer air, higher for a heavier ride. 100 is as designed.",
        "How high a pop goes. 100 is as designed; more reaches higher rails, less keeps the rider low.",
        "How much speed a push gives, and how fast the rider can push up to. 100 is as designed.",
    };
    return t >= 0 && t < TUNE_COUNT ? n[t] : "";
}

/* file: 'SKST', u8 version = 3, u8 preset, u8 map[6], u8 ninfo, u8 info[ninfo], u8 ntune, u8 tune[ntune], u8 ngame, u8 game[ngame] = stance, units, sfx volume, music volume, profile slot
 * (version 1 files stop after the info lines: the tuning is 100; version 2 after the tuning: the game settings are the defaults) */
#define NGAME 5                 /* (a file with 4: from before the profile slots, still loads) */
size_t settings_save(const Settings *s, uint8_t *buf, size_t cap)
{
    size_t need = 4 + 2 + ACT_COUNT + 1 + INFO_COUNT + 1 + TUNE_COUNT + 1 + NGAME;
    if (cap < need)
        return 0;
    memcpy(buf, "SKST", 4);
    buf[4] = 3;
    buf[5] = (uint8_t)s->preset;
    memcpy(buf + 6, s->map, ACT_COUNT);
    buf[6 + ACT_COUNT] = INFO_COUNT;
    for (int i = 0; i < INFO_COUNT; i++)
        buf[7 + ACT_COUNT + i] = s->info[i] ? 1 : 0;
    size_t p = 7 + ACT_COUNT + INFO_COUNT;
    buf[p++] = TUNE_COUNT;
    for (int t = 0; t < TUNE_COUNT; t++)
        buf[p++] = s->tune[t];
    buf[p++] = NGAME;
    buf[p++] = s->stance; buf[p++] = s->units; buf[p++] = s->sfx_vol; buf[p++] = s->music_vol; buf[p++] = s->profile_slot;
    return need;
}

int settings_load(Settings *s, const uint8_t *buf, size_t size)
{
    if (size < 4 + 2 + ACT_COUNT + 1 || memcmp(buf, "SKST", 4) != 0 || (buf[4] < 1 || buf[4] > 3) || buf[5] >= PRESET_COUNT)
        return 0;
    unsigned seen = 0;
    for (int a = 0; a < ACT_COUNT; a++) {            /* a permutation of the buttons, or the file is damaged */
        if (buf[6 + a] >= PB_COUNT || (seen & PBM(buf[6 + a])))
            return 0;
        seen |= PBM(buf[6 + a]);
    }
    int n = buf[6 + ACT_COUNT];
    if (size < (size_t)(7 + ACT_COUNT + n))
        return 0;
    Settings t;
    settings_defaults(&t);
    t.preset = buf[5];
    memcpy(t.map, buf + 6, ACT_COUNT);
    for (int i = 0; i < INFO_COUNT && i < n; i++)
        t.info[i] = buf[7 + ACT_COUNT + i] ? 1 : 0;
    if (buf[4] >= 2) {
        size_t p = 7 + ACT_COUNT + n;
        if (size < p + 1)
            return 0;
        int nt = buf[p++];
        if (size < p + nt)
            return 0;
        for (int k = 0; k < nt && k < TUNE_COUNT; k++) {
            int v = buf[p + k];
            if (v < TUNE_MIN || v > TUNE_MAX || v % TUNE_STEP != 0)
                return 0;
            t.tune[k] = (uint8_t)v;
        }
        if (buf[4] >= 3) {
            p += (size_t)nt;
            if (size < p + 1)
                return 0;
            int ng = buf[p++];
            if (size < p + (size_t)ng || ng < 4)
                return 0;
            if (buf[p] >= STANCE_COUNT || buf[p + 1] >= UNITS_COUNT || buf[p + 2] > VOL_MAX || buf[p + 3] > VOL_MAX)
                return 0;
            t.stance = buf[p]; t.units = buf[p + 1]; t.sfx_vol = buf[p + 2]; t.music_vol = buf[p + 3];
            if (ng >= 5) {
                if (buf[p + 4] >= 3) return 0;
                t.profile_slot = buf[p + 4];
            }
        }
    }
    *s = t;
    return 1;
}
