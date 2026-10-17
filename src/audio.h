/* PSP audio output: a thread that renders the software mixer (src/mixer.c) into one hardware channel, and the background music playlist. */
#ifndef AUDIO_H
#define AUDIO_H

#include "mixer.h"

/* starts the audio thread; returns 0 if the hardware channel could not be reserved (the game then runs silent) */
int audio_start(Mixer *m);

/* Music: data/music0.mus .. music3.mus (tools/convert_music.py) play one after the other, forever. A low-priority thread loads each track into memory in the background
 * (the next one is requested 25 s before the current one ends), so nothing stalls the game. */
void audio_music_init(Mixer *m, const char *base);
void audio_music_update(Mixer *m);            /* once per frame */
void audio_music_gain(Mixer *m, float gain);      /* the music volume 0 .. 1 (Options > Game settings) */
void audio_music_toggle(Mixer *m);            /* mute / unmute (the track keeps playing silently) */
void audio_music_skip(Mixer *m);              /* jump to the next track if it has been loaded already */
int audio_music_index(void);                  /* 1-based number of the track that plays, 0 = none yet */
int audio_music_muted(void);

#endif
