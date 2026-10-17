#include "audio.h"
#include <pspkernel.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_BLOCK 512             /* frames per hardware block (a multiple of 64): 11.6 ms at 44.1 kHz */

static Mixer *g_mixer;
static int16_t g_buf[2][AUDIO_BLOCK * 2] __attribute__((aligned(64)));

static int audio_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0)
        return 0;
    int i = 0;
    for (;;) {
        mixer_render(g_mixer, g_buf[i], AUDIO_BLOCK);
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, g_buf[i]);
        i ^= 1;
    }
    return 0;
}

int audio_start(Mixer *m)
{
    g_mixer = m;
    /* priority 0x18: ahead of the main thread (0x20), which is busy rendering; the thread sleeps in sceAudioOutputBlocking almost all of the time */
    int thid = sceKernelCreateThread("audio_thread", audio_thread, 0x18, 0x8000, 0, NULL);
    if (thid < 0)
        return 0;
    return sceKernelStartThread(thid, 0, NULL) >= 0;
}

/* ---- music playlist ---------------------------------------------------------------------------- */
#define NMUSIC 4
#define MUSIC_VOL 0.40f
static float g_gain = 1.0f;                           /* Options > Game settings > Music, 0 .. 1 */
#define PRELOAD_SECONDS 25.0f

typedef struct {
    uint8_t *mem;
    MusicTrack t;
    int idx;
} Slot;

static char g_base[256];
static volatile int g_req = -1;      /* track the loader should read, -1 none */
static volatile int g_state = 0;     /* loader: 0 idle, 1 working, 2 a track is ready (g_buf_mem / g_trk), 3 failed */
static uint8_t *g_mem;
static MusicTrack g_trk;
static int g_idx;
static Slot g_cur, g_nxt;
static uint8_t *g_retired;
static unsigned g_retire_frames;
static int g_started, g_muted, g_failed, g_index, g_next_failures;

/* the loader thread: reads one track at a time into freshly allocated memory (a blocking read of 1.4-2.6 MB from the memory stick: that is why it is not the game's thread) */
static int music_loader(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        if (g_state == 1 && g_req >= 0) {
            char path[320];
            snprintf(path, sizeof(path), "%sdata/music%d.mus", g_base, g_req);
            FILE *f = fopen(path, "rb");
            int ok = 0;
            if (f) {
                uint32_t hd[9];
                if (fread(hd, 4, 9, f) == 9 && memcmp(hd, "SKMU", 4) == 0 && hd[1] == 1 && hd[5] == 512 && hd[4] > 0) {
                    size_t bytes = (size_t)(hd[4] - 1) * 512 + hd[6];
                    uint8_t *mem = (uint8_t *)malloc(bytes);
                    if (mem && fread(mem, 1, bytes, f) == bytes) {
                        g_mem = mem;
                        g_trk.data = mem; g_trk.nblocks = hd[4]; g_trk.last_bytes = hd[6]; g_trk.nsamples = hd[3]; g_trk.rate = hd[2];
                        g_idx = g_req;
                        ok = 1;
                    } else {
                        free(mem);
                    }
                }
                fclose(f);
            }
            g_req = -1;
            g_state = ok ? 2 : 3;
        } else {
            sceKernelDelayThread(100000);
        }
    }
    return 0;
}

static void music_request(int idx)
{
    g_req = idx;
    g_state = 1;
}

void audio_music_init(Mixer *m, const char *base)
{
    (void)m;
    strncpy(g_base, base, sizeof(g_base) - 1);
    int thid = sceKernelCreateThread("music_loader", music_loader, 0x30, 0x4000, 0, NULL);     /* lower priority than the game */
    if (thid < 0 || sceKernelStartThread(thid, 0, NULL) < 0) { g_failed = 1; return; }
    music_request(0);
}

static void retire(Slot *s)
{
    if (g_retired) free(g_retired);                      /* (the previous one is long gone) */
    g_retired = s->mem; g_retire_frames = 0;
    s->mem = NULL;
}

void audio_music_update(Mixer *m)
{
    if (g_failed) return;
    if (g_state == 3) { g_state = 0; if (!g_started) g_failed = 1; else g_next_failures++; if (g_failed) return; }      /* the first track failed to load: no music; later ones are retried a few times */
    if (g_retired && ++g_retire_frames > 120) { free(g_retired); g_retired = NULL; }       /* the audio thread has long stopped reading it */

    if (g_state == 2) {                                  /* a track arrived */
        Slot *s = g_started ? &g_nxt : &g_cur;
        s->mem = g_mem; s->t = g_trk; s->idx = g_idx;
        g_state = 0;
        if (!g_started) { mixer_music_play(m, &s->t, g_muted ? 0.0f : MUSIC_VOL * g_gain); g_started = 1; g_index = s->idx + 1; }
        else mixer_music_queue(m, &s->t);
    }
    if (g_started && g_cur.mem && g_nxt.mem && mixer_music_current(m) == &g_nxt.t) {      /* the next track has begun: let go of the old one */
        retire(&g_cur);
        g_cur = g_nxt; g_nxt.mem = NULL;
        g_index = g_cur.idx + 1;
    }
    if (g_started && g_cur.mem && !mixer_music_current(m)) {                              /* ran out with nothing queued (a slow load): start over from the next one */
        int next = (g_cur.idx + 1) % NMUSIC;
        retire(&g_cur);
        g_started = 0;
        if (g_state == 0) music_request(next);
        return;
    }
    if (g_started && g_cur.mem && !g_nxt.mem && g_state == 0 && g_next_failures < 3 && mixer_music_remaining(m) < PRELOAD_SECONDS)
        music_request((g_cur.idx + 1) % NMUSIC);
}

void audio_music_toggle(Mixer *m)
{
    g_muted = !g_muted;
    mixer_music_volume(m, g_muted ? 0.0f : MUSIC_VOL * g_gain);
}

void audio_music_gain(Mixer *m, float gain)
{
    g_gain = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
    mixer_music_volume(m, g_muted ? 0.0f : MUSIC_VOL * g_gain);
}

void audio_music_skip(Mixer *m)
{
    if (g_started && g_cur.mem && g_nxt.mem)
        mixer_music_play(m, &g_nxt.t, g_muted ? 0.0f : MUSIC_VOL * g_gain);     /* audio_music_update retires the old track */
}

int audio_music_index(void) { return g_started ? g_index : 0; }
int audio_music_muted(void) { return g_muted; }
