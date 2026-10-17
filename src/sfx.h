/* Sound effects: turns the skater's state and events into mixer commands (no PSP dependencies - host-tested in tests/test_sound.c).
 * The sounds are the DS game's own (data/sounds.snd, tools/convert_sounds.py), found by their original names. What plays:
 *   rolling      SFX_ROLL_MEDIUM / ROUGH / SMOOTH by the floor (loop; louder and higher with speed)
 *   ollie, land  SFX_OLLIE_<CNC|WOOD>_<BIG|SMALL>, SFX_LAND_<CNC|WOOD>_<TINY..BIG> by the floor and the impact speed
 *   grinds       SFX_GRIND_<IN|LOOP|OUT>_<CONC|WOOD|METAL>_<2T|BRD>: the rail's / ledge's material, trucks or board by the grind type
 *   manuals      SFX_KICK_FIRST / SECOND when the board is kicked up, SFX_SCRAPE_RIGHT when the tail is about to scrape (a warning)
 *   flips        SFX_KICK_FIRST when it starts, SFX_KICK_SECOND at 55 % of it;  grabs  SFX_GRABON / SFX_GRABOFF
 *   walls        SFX_SCRAPE_LEFT / RIGHT while sliding along a wall
 *   braking, push, wind in a long jump, and the bail: SFX_GRUNT_1 + SFX_WIPEOUT_*
 *   score        SFX_HUD_* jingles (sfx_event) for a banked combo and a respawn
 * `surf` = the material byte of the floor under the rider (bits 0-1 concrete / metal / wood, bits 2-3 medium / rough / smooth: tools/materials.py). */
#ifndef SFX_H
#define SFX_H

#include "mixer.h"
#include "skate.h"

typedef struct {
    Sound *snd;
    char (*name)[32];
    int n;
    void *blob;                 /* the file's memory (the sounds point into it) */
} SoundBank;

/* load data/sounds.snd; returns 0 if the file is missing or damaged */
int soundbank_load(SoundBank *b, const char *path);
void soundbank_free(SoundBank *b);
const Sound *soundbank_find(const SoundBank *b, const char *name);

enum { SFX_LOOP_ROLL = 0, SFX_LOOP_GRIND = 1, SFX_LOOP_AIR = 2, SFX_LOOP_BRAKE = 3 };      /* mixer loop channels */
enum { SFX_EV_BANK = 0, SFX_EV_BANK_BIG = 1, SFX_EV_RESPAWN = 2, SFX_EV_MOVE = 3, SFX_EV_ENTER = 4, SFX_EV_BACK = 5, SFX_EV_321 = 6, SFX_EV_GO = 7, SFX_EV_WIN = 8, SFX_EV_LOSE = 9,
       SFX_EV_CHECKPOINT = 10, SFX_EV_COUNT = 11 };      /* sfx_event(): jingles, the menu beeps and the challenge countdown / result sounds */

typedef struct {
    const Sound *ollie[2][2];                         /* [concrete / wood][small / big] */
    const Sound *land[2][4];                          /* [concrete / wood][tiny .. big] */
    const Sound *roll[3];                             /* medium, rough, smooth */
    const Sound *slide, *brake_in, *brake_loop, *air, *push_on, *push_off, *grunt, *wipe_body;
    const Sound *wipe_hi[3], *wipe_lo[3];             /* [concrete / metal / wood] */
    const Sound *grind_in[3][2], *grind_out[3][2], *grind_loop[3][2];        /* [concrete / metal / wood][trucks / board] */
    const Sound *kick_first, *kick_second, *scrape[2], *grab_on, *grab_off;    /* scrape: [left, right] */
    const Sound *jingle[SFX_EV_COUNT];
    /* state between frames */
    int was_grinding, was_bail, was_brake, was_manual, was_warn, was_grab, was_flip, flip_mid_done, push_on_done, push_off_done;
    int grind_mat, grind_board, last_surf;
    float scrape_cool;
} Sfx;

void sfx_init(Sfx *f, const SoundBank *b);
/* once per frame; reads and clears the skater's sound events. surf: see above. */
void sfx_update(Sfx *f, Mixer *m, Skate *s, const SkateInput *in, int surf, float dt);
void sfx_event(Sfx *f, Mixer *m, int what);

#endif
