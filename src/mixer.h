/* Software mixer (no PSP dependencies - host-tested in tests/test_mixer.c; src/audio.c feeds it to the PSP's audio hardware from its own thread).
 * Sounds are mono 16-bit PCM at their own sample rate (data/sounds.snd, tools/convert_sounds.py). The mixer outputs interleaved stereo at MIX_RATE.
 * Two kinds of voices:
 *   - one-shots: mixer_play() takes any free one-shot voice (the oldest is stolen when all are busy) and plays the sound once;
 *   - loop channels (MIX_LOOPS of them, addressed by index): mixer_loop() sets what a channel plays, how loud and at what pitch. Volume changes are ramped over the block, a
 *     channel whose volume is set to 0 fades out and stops, a new sound on a busy channel replaces the old one.
 * The game thread calls mixer_play / mixer_loop, the audio thread calls mixer_render; every field they share is a plain 32-bit value (the PSP's CPU is a single core). */
#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>

#define MIX_RATE    44100
#define MIX_SHOTS   10
#define MIX_LOOPS   4

typedef struct {
    const int16_t *pcm;
    uint32_t n;                 /* samples */
    uint32_t rate;              /* Hz */
    int loop;                   /* the sound was made to loop (whole-sample loop) */
} Sound;

typedef struct {
    const Sound *snd;           /* NULL = free */
    float pos;                  /* in samples of the sound */
    float vol, target;          /* current / wanted volume */
    float pitch, pan;           /* pan -1 (left) .. 1 (right) */
    uint32_t age;               /* for stealing the oldest one-shot */
} Voice;

/* Streamed music: mono IMA-ADPCM in 512-byte blocks (tools/convert_music.py), decoded block by block while it plays. The bytes belong to the caller. */
typedef struct {
    const uint8_t *data;        /* nblocks blocks of 512 bytes (the last one last_bytes long) */
    uint32_t nblocks, last_bytes;
    uint32_t nsamples, rate;
} MusicTrack;

typedef struct {
    const MusicTrack *cur, *next;     /* what plays now / what plays when it ends */
    uint32_t blk;                     /* the block decoded into b[] */
    int n;                            /* samples in it */
    int16_t b[1024];                  /* b[0] = the sample before the block (for the interpolation), b[1..n] = the block, b[n + 1] = b[n] */
    float pos;                        /* position in the block, in samples */
    float vol, target;
    uint32_t ended;                   /* tracks that have played to their end */
} MusicVoice;

/* Streamed PCM (the intro movies' sound): the game thread pushes mono 16-bit samples at their own rate into a ring buffer, the audio thread plays them (linear interpolation up to MIX_RATE).
 * An empty buffer plays silence. Counters are plain 32-bit values like the rest of the shared state. */
#define MIX_STREAM_CAP 16384            /* samples, a power of two */
typedef struct {
    int16_t buf[MIX_STREAM_CAP];
    volatile uint32_t wr, rd;           /* samples written / read since the start */
    uint32_t rate;
    float pos, vol;
    int on;
    uint32_t starved;                   /* output frames played as silence because the buffer was empty (the player logs it) */
} StreamVoice;

typedef struct {
    Voice shot[MIX_SHOTS];
    Voice loop[MIX_LOOPS];
    MusicVoice music;
    StreamVoice stream;
    float master;
    uint32_t clock;
} Mixer;

void mixer_init(Mixer *m, float master);
/* play a sound once: volume 0..1+, pitch 1 = original speed, pan -1..1 */
void mixer_play(Mixer *m, const Sound *s, float vol, float pitch, float pan);
/* set loop channel `ch` to play `s` (NULL or vol <= 0.001 = fade out) */
void mixer_loop(Mixer *m, int ch, const Sound *s, float vol, float pitch);
/* mix `frames` stereo frames into out[2 * frames] */
void mixer_render(Mixer *m, int16_t *out, int frames);
/* music: play a track now (replacing the one that plays), queue one to follow the current one, set its volume (fades), stop it (fades out) */
void mixer_music_play(Mixer *m, const MusicTrack *t, float vol);
void mixer_music_queue(Mixer *m, const MusicTrack *t);
void mixer_music_volume(Mixer *m, float vol);
void mixer_music_stop(Mixer *m);
const MusicTrack *mixer_music_current(const Mixer *m);
/* seconds left in the track that plays now (0 if none) */
float mixer_music_remaining(const Mixer *m);
/* decode one ADPCM block of `bytes` bytes (4-byte header + nibbles) to out[]; returns the number of samples. Exposed for the tests. */
int mixer_adpcm_block(const uint8_t *p, uint32_t bytes, int16_t *out);

/* the PCM stream: start it (resets the buffer), push samples (returns how many fit), room left, samples waiting, stop it */
void mixer_stream_start(Mixer *m, uint32_t rate, float vol);
int mixer_stream_push(Mixer *m, const int16_t *pcm, int n);
int mixer_stream_space(const Mixer *m);
int mixer_stream_queued(const Mixer *m);
uint32_t mixer_stream_starved(const Mixer *m);
void mixer_stream_stop(Mixer *m);

/* number of voices playing (for the tests and the HUD) */
int mixer_active(const Mixer *m);

#endif
