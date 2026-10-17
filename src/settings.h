/* The player's settings (no PSP dependencies - host-tested in tests/test_settings.c): which physical button does which action (a preset or the player's own mapping), which
 * information lines the game shows while skating, and the physics tuning (Options > Gravity). Saved in data/settings.sav. */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stddef.h>
#include <stdint.h>

/* the physical buttons that can be given to an action (the D-pad is the camera, Start the pause menu, the nub steers) */
enum { PB_CROSS = 0, PB_SQUARE, PB_CIRCLE, PB_TRIANGLE, PB_L, PB_R, PB_COUNT };
#define PBM(b) (1u << (b))              /* the bit of a button in a pressed / held mask */

/* the six actions of the control scheme (src/control.h: CTL_CROSS .. CTL_R are these, in this order). Grinds and wallrides need no button (the board locks on by itself). */
enum { ACT_POP = 0, ACT_PUSH, ACT_BRAKE, ACT_POP_ADV, ACT_GRAB_L, ACT_GRAB_R, ACT_COUNT };

enum { PRESET_DEFAULT = 0, PRESET_SWAP_POPS, PRESET_CIRCLE_POP, PRESET_SHOULDER_BRAKE, PRESET_CUSTOM, PRESET_COUNT };   /* PRESET_CUSTOM: the player's own mapping */

/* the information lines that can be shown while skating, each on its own */
enum { INFO_FPS = 0, INFO_SPEED, INFO_CPU_GPU, INFO_HEAP, INFO_POSITION, INFO_TRICK, INFO_NUB, INFO_RAIL, INFO_SURFACE, INFO_CLIP, INFO_COUNT };

/* the physics tuning, in percent of the designed values (SkateTune in src/skate.h) */
enum { TUNE_GRAVITY = 0, TUNE_OLLIE, TUNE_PUSH, TUNE_COUNT };
#define TUNE_MIN  50
#define TUNE_MAX  150
#define TUNE_STEP 10

/* Game settings (Options > Game settings, the original's My Skate > Game Settings): the stance the rider rides in, the units of distances and speeds, the sound volumes */
enum { STANCE_OWN = 0, STANCE_REGULAR, STANCE_GOOFY, STANCE_COUNT };      /* STANCE_OWN: the skater's own stance (the pros' stance flag in the ROM) */
enum { UNITS_METERS = 0, UNITS_FEET, UNITS_COUNT };
#define VOL_MAX 10                      /* a volume is 0 .. VOL_MAX (tenths) */

typedef struct {
    int preset;                         /* PRESET_*: which one the mapping came from (PRESET_CUSTOM once the player has changed a button) */
    uint8_t map[ACT_COUNT];             /* map[action] = the physical button (PB_*); always a permutation: no two actions share a button */
    uint8_t info[INFO_COUNT];           /* 1 = show */
    uint8_t tune[TUNE_COUNT];           /* percent, TUNE_MIN .. TUNE_MAX, 100 = as designed */
    uint8_t stance, units;              /* STANCE_*, UNITS_* */
    uint8_t sfx_vol, music_vol;         /* 0 .. VOL_MAX */
    uint8_t profile_slot;               /* the profile in use (0 .. 2) */
} Settings;

void settings_defaults(Settings *s);
/* set the mapping of a named preset (PRESET_CUSTOM keeps the current mapping and just marks it as the player's own) */
void settings_apply_preset(Settings *s, int preset);
/* give `action` the button: the action that had that button gets the old button of this one (a swap, so every button still does one thing); marks the mapping as custom */
void settings_assign(Settings *s, int action, int button);
/* physical buttons held / pressed (PBM masks) -> the CTL_* bits of the actions they are mapped to */
unsigned settings_ctl_bits(const Settings *s, unsigned phys);
/* the named preset whose mapping is the same as this one, or PRESET_CUSTOM */
int settings_match_preset(const Settings *s);
/* move a tuning value one TUNE_STEP up (dir > 0) or down, within TUNE_MIN .. TUNE_MAX */
void settings_tune_step(Settings *s, int tune, int dir);
/* a tuning value as a scale (100 -> 1.0) */
float settings_tune_scale(const Settings *s, int tune);

/* does the rider ride regular (the model and board are drawn mirrored: the clips are goofy ones) - the setting, or for STANCE_OWN the skater's own stance (career_skater_regular) */
int settings_riding_regular(const Settings *s, int skater_regular);
/* a volume as a gain 0 .. 1 */
float settings_volume_scale(int vol);
const char *settings_stance_name(int stance);
const char *settings_units_name(int units);
const char *settings_stance_help(void);
/* "25 km/h" / "16 mph" and "12 m" / "39 ft" */
void settings_format_speed(const Settings *s, float metres_per_second, char *out, size_t cap);
void settings_format_distance(const Settings *s, float metres, char *out, size_t cap);

/* the mapping of a named preset (ACT_COUNT bytes), NULL for PRESET_CUSTOM */
const uint8_t *settings_preset_map(int preset);
const char *settings_preset_name(int preset);
const char *settings_action_name(int action);
const char *settings_action_help(int action);      /* what the action does, short */
const char *settings_button_name(int button);
/* copy tmpl into out (cap bytes), replacing {POP} {ADV} {PUSH} {BRAKE} {GL} {GR} with the names of the buttons those actions are mapped to */
void settings_expand_buttons(const Settings *s, const char *tmpl, char *out, size_t cap);
const char *settings_info_name(int info);
const char *settings_info_help(int info);
const char *settings_tune_name(int tune);
const char *settings_tune_help(int tune);

/* the file: returns the bytes written (0 if cap is too small) / 1 if the buffer is a valid settings file (it is then loaded) */
size_t settings_save(const Settings *s, uint8_t *buf, size_t cap);
int settings_load(Settings *s, const uint8_t *buf, size_t size);

#endif
