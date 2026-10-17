/* Host tests for src/mixer.c, src/sfx.c and src/sparks.c. usage: test_sound [sounds.snd] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/mixer.h"
#include "../src/sfx.h"
#include "../src/sparks.h"
#include "../src/rails.h"
#include "../src/trick.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int16_t dc[4000];
static Sound mk(int n, int rate, int loop, int16_t v) { for (int i = 0; i < n; i++) dc[i] = v; Sound s = { dc, (uint32_t)n, (uint32_t)rate, loop }; return s; }

static void test_mixer(void)
{
    static int16_t a[1000], b[1000], c[200];
    for (int i = 0; i < 1000; i++) a[i] = b[i] = 16384;
    for (int i = 0; i < 200; i++) c[i] = 30000;
    Sound sa = { a, 1000, 44100, 0 }, sb = { b, 1000, 22050, 0 }, sl = { c, 200, 44100, 1 };
    Mixer m; mixer_init(&m, 1.0f);
    int16_t out[2 * 512];

    mixer_play(&m, &sa, 1.0f, 1.0f, 0.0f);
    mixer_render(&m, out, 512);
    CHECK(abs(out[0] - 16384) <= 2 && abs(out[1022] - 16384) <= 2, "one-shot plays at full level (%d)", out[0]);
    CHECK(mixer_active(&m) == 1, "still playing after 512 of 1000 samples");
    mixer_render(&m, out, 512);
    CHECK(mixer_active(&m) == 0 && out[2 * 487] == 16384 && out[2 * 490] == 0, "ends after its 1000 samples (%d %d)", out[2 * 487], out[2 * 490]);

    mixer_play(&m, &sa, 1.0f, 1.0f, -1.0f);
    mixer_render(&m, out, 8);
    CHECK(out[0] > 16000 && out[1] == 0, "pan left: L %d R %d", out[0], out[1]);
    mixer_render(&m, out, 512); mixer_render(&m, out, 512);
    mixer_play(&m, &sa, 0.5f, 2.0f, 0.0f);                                  /* twice the speed: over in 500 frames, half the level */
    mixer_render(&m, out, 512);
    CHECK(abs(out[0] - 8192) <= 2 && mixer_active(&m) == 0, "pitch 2 lasts half as long (%d)", out[0]);
    mixer_play(&m, &sb, 1.0f, 1.0f, 0.0f);                                  /* a 22.05 kHz sound lasts twice as many output frames */
    mixer_render(&m, out, 512);
    CHECK(mixer_active(&m) == 1, "22.05 kHz sound is still playing after 512 frames");
    mixer_render(&m, out, 512);
    mixer_render(&m, out, 512);
    CHECK(mixer_active(&m) == 1, "still playing after 1536 frames");
    mixer_render(&m, out, 512);
    CHECK(mixer_active(&m) == 0, "and over after 2000 frames");

    mixer_play(&m, &sl, 1.0f, 1.0f, 0.0f);
    mixer_play(&m, &sl, 1.0f, 1.0f, 0.0f);
    mixer_render(&m, out, 8);
    CHECK(out[0] == 32767, "two loud sounds clip instead of wrapping (%d)", out[0]);
    mixer_render(&m, out, 512);

    for (int i = 0; i < MIX_SHOTS + 5; i++) mixer_play(&m, &sa, 0.1f, 1.0f, 0.0f);
    CHECK(mixer_active(&m) <= MIX_SHOTS, "voices are stolen, never exceeded (%d)", mixer_active(&m));
    mixer_render(&m, out, 512); mixer_render(&m, out, 512);

    /* loop channel: fades in, loops past the end of the sound, fades out and frees */
    mixer_loop(&m, 0, &sl, 0.5f, 1.0f);
    mixer_render(&m, out, 512);
    CHECK(out[0] < 100 && out[2 * 511] > 12000, "a loop fades in over the block (%d .. %d)", out[0], out[2 * 511]);
    mixer_render(&m, out, 512);
    CHECK(abs(out[0] - 15000) <= 2 && abs(out[2 * 511] - 15000) <= 2, "the loop keeps going past its 200 samples (%d)", out[0]);
    mixer_loop(&m, 0, &sl, 0.0f, 1.0f);
    mixer_render(&m, out, 512);
    CHECK(out[0] > 10000 && out[2 * 511] < 200, "fades out (%d .. %d)", out[0], out[2 * 511]);
    CHECK(mixer_active(&m) == 0, "a faded-out loop channel is freed");
    /* a non-looping sound on a loop channel repeats */
    Sound sn = mk(100, 44100, 0, 10000);
    mixer_loop(&m, 1, &sn, 1.0f, 1.0f);
    mixer_render(&m, out, 512); mixer_render(&m, out, 512);
    CHECK(mixer_active(&m) == 1 && out[2 * 500] > 9000, "loop channels repeat any sound");
    mixer_loop(&m, 1, NULL, 0.0f, 1.0f); mixer_render(&m, out, 512); mixer_render(&m, out, 512);
    CHECK(mixer_active(&m) == 0, "NULL stops the channel");
}

static const char *NEEDED[] = { "SFX_OLLIE_CNC_BIG", "SFX_OLLIE_CNC_SMALL", "SFX_OLLIE_WOOD_BIG", "SFX_OLLIE_WOOD_SMALL",
    "SFX_LAND_CNC_TINY", "SFX_LAND_CNC_SMALL", "SFX_LAND_CNC_MED", "SFX_LAND_CNC_BIG", "SFX_LAND_WOOD_TINY", "SFX_LAND_WOOD_SMALL", "SFX_LAND_WOOD_MED", "SFX_LAND_WOOD_BIG",
    "SFX_ROLL_MEDIUM", "SFX_ROLL_ROUGH", "SFX_ROLL_SMOOTH", "SFX_ROLL_SLIDE", "SFX_BRAKE_IN", "SFX_BRAKE_LOOP", "SFX_AIR_LOOP", "SFX_PUSHON", "SFX_PUSHOFF", "SFX_GRUNT_1", "SFX_WIPEOUT_BODY_HI",
    "SFX_WIPEOUT_CONC_HI", "SFX_WIPEOUT_CONC_LO", "SFX_WIPEOUT_METAL_HI", "SFX_WIPEOUT_METAL_LO", "SFX_WIPEOUT_WOOD_HI", "SFX_WIPEOUT_WOOD_LO",
    "SFX_GRIND_IN_CONC_2T", "SFX_GRIND_IN_CONC_BRD", "SFX_GRIND_IN_METAL_2T", "SFX_GRIND_IN_METAL_BRD", "SFX_GRIND_IN_WOOD_2T", "SFX_GRIND_IN_WOOD_BRD",
    "SFX_GRIND_OUT_CONC_2T", "SFX_GRIND_OUT_CONC_BRD", "SFX_GRIND_OUT_METAL_2T", "SFX_GRIND_OUT_METAL_BRD", "SFX_GRIND_OUT_WOOD_2T", "SFX_GRIND_OUT_WOOD_BRD",
    "SFX_GRIND_LOOP_CONC_2T", "SFX_GRIND_LOOP_CONC_BRD", "SFX_GRIND_LOOP_METAL_2T", "SFX_GRIND_LOOP_METAL_BRD", "SFX_GRIND_LOOP_WOOD_2T", "SFX_GRIND_LOOP_WOOD_BRD",
    "SFX_GRABON", "SFX_GRABOFF", "SFX_KICK_FIRST", "SFX_KICK_SECOND", "SFX_SCRAPE_LEFT", "SFX_SCRAPE_RIGHT", "SFX_HUD_CHALLENGE_LETTER", "SFX_HUD_CHALLENGE_OBJECTIVE", "SFX_HUD_SPAWN_USE" };
#define NNEEDED ((int)(sizeof(NEEDED) / sizeof(NEEDED[0])))

static int playing(const Mixer *m, const Sound *s)
{
    for (int i = 0; i < MIX_SHOTS; i++) if (m->shot[i].snd == s) return 1;
    return 0;
}

#define DTF (1.0f / 60.0f)
static void upd(Sfx *f, Mixer *m, Skate *s, const SkateInput *in, int surf) { sfx_update(f, m, s, in, surf, DTF); }

static void test_sfx(const char *real)
{
    tricks_defaults();
    SoundBank bank; memset(&bank, 0, sizeof(bank));
    static int16_t pcm[NNEEDED][8];
    static char names[NNEEDED][32];
    static Sound snds[NNEEDED];
    if (real) {
        CHECK(soundbank_load(&bank, real), "load %s", real);
        CHECK(bank.n == 72, "72 effects in the file (%d)", bank.n);
        for (int i = 0; i < NNEEDED; i++) CHECK(soundbank_find(&bank, NEEDED[i]) != NULL, "%s is in the sound file", NEEDED[i]);
        for (int i = 0; i < bank.n; i++) CHECK(bank.snd[i].pcm && bank.snd[i].n > 32 && bank.snd[i].rate >= 8000 && bank.snd[i].rate <= 48000, "effect %d (%s) is sane", i, bank.name[i]);
        const Sound *loop = soundbank_find(&bank, "SFX_GRIND_LOOP_METAL_2T"), *one = soundbank_find(&bank, "SFX_OLLIE_CNC_BIG");
        CHECK(loop && loop->loop && one && !one->loop, "loop flags");
        /* a decoded effect plays through the mixer without producing garbage: the level of the output is bounded and it ends */
        Mixer m; mixer_init(&m, 0.7f);
        static int16_t out[2 * 512];
        mixer_play(&m, one, 1.0f, 1.0f, 0.0f);
        int peak = 0;
        for (int i = 0; i < 40 && mixer_active(&m); i++) { mixer_render(&m, out, 512); for (int k = 0; k < 1024; k++) if (abs(out[k]) > peak) peak = abs(out[k]); }
        CHECK(!mixer_active(&m) && peak > 1000 && peak <= 32767, "real ollie sound plays and ends (peak %d)", peak);
    } else {
        for (int i = 0; i < NNEEDED; i++) { strncpy(names[i], NEEDED[i], 31); for (int k = 0; k < 8; k++) pcm[i][k] = 1000; snds[i].pcm = pcm[i]; snds[i].n = 8; snds[i].rate = 16000; snds[i].loop = 1; }
        bank.snd = snds; bank.name = names; bank.n = NNEEDED;
    }
    Sfx f; sfx_init(&f, &bank);
    int all = 1;
    for (int a = 0; a < 2; a++) { for (int b = 0; b < 2; b++) all &= f.ollie[a][b] != NULL; for (int b = 0; b < 4; b++) all &= f.land[a][b] != NULL; }
    for (int a = 0; a < 3; a++) { all &= f.roll[a] != NULL && f.wipe_hi[a] != NULL && f.wipe_lo[a] != NULL && f.jingle[a] != NULL; for (int b = 0; b < 2; b++) all &= f.grind_in[a][b] != NULL && f.grind_out[a][b] != NULL && f.grind_loop[a][b] != NULL; }
    all &= f.slide && f.brake_in && f.brake_loop && f.air && f.push_on && f.push_off && f.grunt && f.wipe_body && f.kick_first && f.kick_second && f.scrape[0] && f.scrape[1] && f.grab_on && f.grab_off;
    CHECK(all, "all effects resolved by name");

    Mixer m; mixer_init(&m, 1.0f);
    Skate s; memset(&s, 0, sizeof(s));
    SkateInput in; memset(&in, 0, sizeof(in));
    const int CONC = 0, METAL = 1, WOOD = 2, ROUGH = 1 << 2, SMOOTH = 2 << 2;
    s.grounded = 1; s.speed = 4.0f; s.cn[1] = 1.0f;
    upd(&f, &m, &s, &in, CONC);
    CHECK(m.loop[SFX_LOOP_ROLL].snd == f.roll[0] && m.loop[SFX_LOOP_ROLL].target > 0.1f, "rolling plays the roll loop");
    float v4 = m.loop[SFX_LOOP_ROLL].target;
    s.speed = 7.0f; upd(&f, &m, &s, &in, CONC);
    CHECK(m.loop[SFX_LOOP_ROLL].target > v4 && m.loop[SFX_LOOP_ROLL].pitch > 0.95f, "faster = louder and higher");
    upd(&f, &m, &s, &in, CONC | ROUGH);
    CHECK(m.loop[SFX_LOOP_ROLL].snd == f.roll[1], "a rough floor rolls rough");
    upd(&f, &m, &s, &in, CONC | SMOOTH);
    CHECK(m.loop[SFX_LOOP_ROLL].snd == f.roll[2], "a smooth floor rolls smooth");
    upd(&f, &m, &s, &in, WOOD);
    CHECK(m.loop[SFX_LOOP_ROLL].snd == f.roll[1], "wood rumbles");
    s.speed = 0.0f; upd(&f, &m, &s, &in, CONC);
    CHECK(m.loop[SFX_LOOP_ROLL].target == 0.0f, "standing still: silent");

    s.ev_ollie = 1; s.ollie_pop = 3.9f; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.ollie[0][1]) && s.ev_ollie == 0, "full ollie: the big pop");
    s.ev_ollie = 1; s.ollie_pop = 3.0f; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.ollie[0][0]), "short ollie: the small pop");
    s.ev_ollie = 1; s.ollie_pop = 3.9f; upd(&f, &m, &s, &in, WOOD);
    CHECK(playing(&m, f.ollie[1][1]), "ollie off wood");
    mixer_init(&m, 1.0f);
    s.ev_land = 1; s.land_impact = 6.0f; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.land[0][3]) && s.ev_land == 0, "hard landing: the big thud");
    mixer_init(&m, 1.0f);
    s.ev_land = 1; s.land_impact = 4.0f; upd(&f, &m, &s, &in, WOOD);
    CHECK(playing(&m, f.land[1][2]), "landing on wood: the wooden thud");
    mixer_init(&m, 1.0f);
    s.ev_land = 1; s.land_impact = 0.5f; upd(&f, &m, &s, &in, CONC);
    CHECK(!playing(&m, f.land[0][0]) && !playing(&m, f.land[0][1]), "a bump of 0.5 m/s makes no landing sound");

    /* walls */
    mixer_init(&m, 1.0f);
    s.ev_scrape = 1; s.scrape_side = 1.0f; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.scrape[0]) && s.ev_scrape == 0, "a wall on the left: scrape left");
    s.ev_scrape = 1; s.scrape_side = -1.0f; upd(&f, &m, &s, &in, CONC);
    CHECK(!playing(&m, f.scrape[1]), "scrapes are rate-limited");
    for (int i = 0; i < 30; i++) upd(&f, &m, &s, &in, CONC);
    s.ev_scrape = 1; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.scrape[1]), "a wall on the right, later: scrape right");

    /* grinding: metal rail on the trucks, a concrete ledge on the board, a wooden one */
    mixer_init(&m, 1.0f);
    f.was_grinding = 0;
    s.grounded = 0; s.grinding = 1; s.grind_type = 0; s.ev_grind_mat = METAL; s.speed = 4.0f;
    upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.grind_in[1][0]) && m.loop[SFX_LOOP_GRIND].snd == f.grind_loop[1][0], "metal rail, trucks: in + loop");
    CHECK(m.loop[SFX_LOOP_ROLL].target == 0.0f, "no wheel noise while grinding");
    s.grinding = 0; s.vel[1] = 1.0f; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.grind_out[1][0]) && m.loop[SFX_LOOP_GRIND].target == 0.0f, "the way off: out sound, loop fades");
    s.grinding = 1; s.grind_type = 1; s.ev_grind_mat = CONC; tricks.grind[1].board = 1;
    upd(&f, &m, &s, &in, CONC);
    CHECK(m.loop[SFX_LOOP_GRIND].snd == f.grind_loop[0][1], "concrete ledge, board: its own loop");
    s.grinding = 0; upd(&f, &m, &s, &in, CONC);
    s.grinding = 1; s.grind_type = 0; s.ev_grind_mat = WOOD;
    upd(&f, &m, &s, &in, CONC);
    CHECK(m.loop[SFX_LOOP_GRIND].snd == f.grind_loop[2][0], "wooden ledge: wood grind");
    s.grinding = 0; upd(&f, &m, &s, &in, CONC);

    /* manuals */
    mixer_init(&m, 1.0f);
    s.grounded = 1; s.manual = 1; s.lean = 0.2f;
    upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.kick_first), "starting a manual: the kick");
    s.lean = 0.9f; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.scrape[1]), "nearly falling off the back: the tail scrapes");
    s.manual = 0;

    /* air tricks */
    mixer_init(&m, 1.0f);
    s.grounded = 0; s.flip = 1; s.flip_t = 0.0f; s.flip_dur = 0.5f;
    upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.kick_first), "a flip starts with a kick");
    s.flip_t = 0.3f; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.kick_second), "and kicks again at 55 percent");
    s.flip = 0;
    mixer_init(&m, 1.0f);
    s.grab = 2; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.grab_on), "a grab: the board is grabbed");
    s.grab = 0; upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.grab_off), "and let go");

    /* bail */
    mixer_init(&m, 1.0f);
    s.grinding = 0; s.bail_t = 1.5f; s.grounded = 0;
    upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.grunt) && playing(&m, f.wipe_body), "bail: grunt and body hit");
    s.grounded = 1; s.ev_land = 1; s.land_impact = 5.0f; s.speed = 2.0f;
    upd(&f, &m, &s, &in, CONC);
    CHECK(playing(&m, f.wipe_hi[0]), "bail: hitting the ground");
    CHECK(m.loop[SFX_LOOP_ROLL].snd == f.slide, "bail: sliding along the ground");
    s.bail_t = 0.0f;
    mixer_init(&m, 1.0f);
    sfx_event(&f, &m, SFX_EV_BANK_BIG);
    CHECK(playing(&m, f.jingle[SFX_EV_BANK_BIG]), "a big combo has its jingle");
    if (real) soundbank_free(&bank);
}

/* ---- music ----------------------------------------------------------------------------------- */
static uint8_t synth[3][512];
static void test_music(const char *path)
{
    /* known answer (python tools/convert_music.py decode): header pred 100, step index 5, nibbles 1 7 A 9 5 3 8 C 0 0 ... */
    uint8_t blk[512];
    memset(blk, 0, sizeof(blk));
    blk[0] = 100; blk[1] = 0; blk[2] = 5; blk[3] = 0; blk[4] = 0x71; blk[5] = 0x9A; blk[6] = 0x35; blk[7] = 0xC8;
    int16_t out[1100];
    int n = mixer_adpcm_block(blk, 512, out);
    static const int16_t want[10] = { 104, 123, 110, 103, 128, 152, 149, 124, 127, 130 };
    int ok = n == 1016;
    for (int i = 0; i < 10; i++) ok &= out[i] == want[i];
    CHECK(ok, "ADPCM block decodes to the reference samples (n %d, first %d %d %d)", n, out[0], out[1], out[2]);
    CHECK(mixer_adpcm_block(blk, 223, out) == 438, "a short last block gives (bytes - 4) * 2 samples");

    /* two tiny tracks: the queue plays the second one when the first has ended, and then it is silent */
    for (int k = 0; k < 3; k++) { memset(synth[k], 0, 512); synth[k][0] = 0xE8; synth[k][1] = 0x03; }        /* predictor 1000 */
    MusicTrack a = { &synth[0][0], 1, 512, 1016, 22050 }, b = { &synth[1][0], 1, 512, 1016, 22050 };
    Mixer m; mixer_init(&m, 1.0f);
    static int16_t buf[2 * 512];
    mixer_music_play(&m, &a, 1.0f);
    mixer_music_queue(&m, &b);
    CHECK(mixer_music_current(&m) == &a && fabsf(mixer_music_remaining(&m) - 1016.0f / 22050.0f) < 0.002f, "play: current track and its length (%.3f s)", mixer_music_remaining(&m));
    for (int i = 0; i < 3; i++) mixer_render(&m, buf, 512);                    /* 1536 frames = 0.035 s: track a is 0.046 s long */
    CHECK(mixer_music_current(&m) == &a, "still on the first track");
    for (int i = 0; i < 2; i++) mixer_render(&m, buf, 512);
    CHECK(mixer_music_current(&m) == &b && m.music.ended == 1, "the queued track follows (ended %u)", m.music.ended);
    for (int i = 0; i < 6; i++) mixer_render(&m, buf, 512);
    CHECK(mixer_music_current(&m) == NULL && m.music.ended == 2, "and then silence");

    /* volume */
    mixer_music_play(&m, &a, 0.0f);
    mixer_render(&m, buf, 512); mixer_render(&m, buf, 512);
    int peak = 0; for (int i = 0; i < 1024; i++) if (abs(buf[i]) > peak) peak = abs(buf[i]);
    CHECK(peak == 0, "volume 0 is silent (%d)", peak);
    mixer_music_volume(&m, 1.0f);
    mixer_render(&m, buf, 512); mixer_render(&m, buf, 512);
    peak = 0; for (int i = 0; i < 1024; i++) if (abs(buf[i]) > peak) peak = abs(buf[i]);
    CHECK(peak > 500, "and audible once raised (%d)", peak);

    if (!path) return;
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "open %s", path);
    if (!f) return;
    uint32_t hd[9];
    CHECK(fread(hd, 4, 9, f) == 9 && memcmp(hd, "SKMU", 4) == 0, "music file header");
    size_t bytes = (size_t)(hd[4] - 1) * 512 + hd[6];
    uint8_t *mem = (uint8_t *)malloc(bytes);
    CHECK(mem && fread(mem, 1, bytes, f) == bytes, "music file data");
    fclose(f);
    MusicTrack t = { mem, hd[4], hd[6], hd[3], hd[2] };
    mixer_init(&m, 1.0f);
    mixer_music_play(&m, &t, 0.4f);
    double sq = 0; int mx = 0; long cnt = 0;
    for (int i = 0; i < 400; i++) {                                             /* 4.6 s */
        mixer_render(&m, buf, 512);
        for (int k = 0; k < 1024; k++) { sq += (double)buf[k] * buf[k]; if (abs(buf[k]) > mx) mx = abs(buf[k]); cnt++; }
    }
    double rms = sqrt(sq / (double)cnt);
    float secs_total = (float)t.nsamples / (float)t.rate;
    CHECK(rms > 200.0 && rms < 8000.0 && mx < 32767, "the real track plays at a sane level (rms %.0f, peak %d)", rms, mx);
    CHECK(fabsf(mixer_music_remaining(&m) - (secs_total - 4.6f)) < 0.1f, "its position advances in real time (%.1f s left of %.1f)", mixer_music_remaining(&m), secs_total);
    /* the last block: play from the end */
    MusicTrack tail = t; tail.data = mem + (size_t)(t.nblocks - 3) * 512; tail.nblocks = 3;
    mixer_init(&m, 1.0f);
    mixer_music_play(&m, &tail, 0.4f);
    for (int i = 0; i < 200 && mixer_music_current(&m); i++) mixer_render(&m, buf, 512);
    CHECK(mixer_music_current(&m) == NULL && m.music.ended == 1, "the real track's last (short) block plays out and ends");
    free(mem);
}

static void test_sparks(void)
{
    Sparks sp; sparks_init(&sp);
    Skate s; memset(&s, 0, sizeof(s));
    s.fwd[2] = 1.0f; s.cn[1] = 1.0f; s.speed = 4.0f; s.pos[1] = 1.0f;
    for (int i = 0; i < 60; i++) sparks_update(&sp, &s, 1, 1.0f / 60.0f);
    CHECK(sp.n == 0, "no sparks without grinding");
    s.grinding = 1;
    for (int i = 0; i < 90; i++) sparks_update(&sp, &s, 1, 1.0f / 60.0f);
    int metal = sp.n;
    CHECK(metal > 40 && metal <= MAX_SPARKS, "a metal rail throws a lot of sparks (%d alive)", metal);
    int bad = 0;
    for (int i = 0; i < sp.n; i++) if (!(sp.s[i].life > 0.0f) || sp.s[i].p[1] < 0.0f || sp.s[i].p[1] > 3.0f || fabsf(sp.s[i].p[0]) > 3.0f) bad++;
    CHECK(bad == 0, "sparks stay near the contact point and above the floor (%d stray)", bad);
    Sparks sc; sparks_init(&sc);
    for (int i = 0; i < 90; i++) sparks_update(&sc, &s, 0, 1.0f / 60.0f);
    CHECK(sc.n > 5 && sc.n < metal, "a concrete ledge throws fewer (%d vs %d)", sc.n, metal);
    s.grinding = 0;
    for (int i = 0; i < 60; i++) sparks_update(&sp, &s, 1, 1.0f / 60.0f);
    CHECK(sp.n == 0, "they die out within a second of leaving the rail (%d left)", sp.n);
    Spark k = { { 0 }, { 0 }, 0.5f, 0.5f, 1 };
    uint32_t c0 = spark_color(&k); k.life = 0.05f; uint32_t c1 = spark_color(&k);
    CHECK((c0 >> 24) == 255 && (c1 >> 24) < 60, "fades to transparent (alpha %u -> %u)", c0 >> 24, c1 >> 24);
}

/* the PCM stream (the intro movies' sound): samples pushed at 22050 Hz play at twice their count at 44100 Hz, an empty buffer is silent, the ring wraps */
static void test_stream(void)
{
    static Mixer m; static int16_t in[4096], out[2 * 4096];
    mixer_init(&m, 1.0f);
    mixer_stream_start(&m, 22050, 1.0f);
    CHECK(mixer_stream_space(&m) == MIX_STREAM_CAP && mixer_stream_queued(&m) == 0, "empty stream");
    mixer_render(&m, out, 64);
    CHECK(out[0] == 0 && out[100] == 0, "an empty stream is silent");
    for (int i = 0; i < 2000; i++) in[i] = 16384;
    CHECK(mixer_stream_push(&m, in, 2000) == 2000 && mixer_stream_queued(&m) == 2000, "push");
    mixer_render(&m, out, 1000);
    CHECK(abs(out[0] - 16384) <= 2 && abs(out[2 * 900] - 16384) <= 2 && out[0] == out[1], "plays at full level, both channels (%d)", out[0]);
    CHECK(mixer_stream_queued(&m) >= 1495 && mixer_stream_queued(&m) <= 1505, "1000 output frames consume about 500 samples at half the rate (%d left of 2000)", mixer_stream_queued(&m));
    /* fills up: pushes beyond the capacity are refused, and the ring wraps around without a glitch */
    int pushed = 0, total_pushed = 0;
    for (int r = 0; r < 40; r++) { pushed = mixer_stream_push(&m, in, 2000); total_pushed += pushed; if (!pushed) break; mixer_render(&m, out, 800); }
    CHECK(total_pushed > MIX_STREAM_CAP, "the ring accepted more than its size over time (%d)", total_pushed);
    int glitch = 0; for (int r = 0; r < 20; r++) { mixer_stream_push(&m, in, 600); mixer_render(&m, out, 500); for (int i = 0; i < 500; i++) glitch += abs(out[2 * i] - 16384) > 2; }
    CHECK(glitch == 0, "no glitch across the wrap (%d bad samples)", glitch);
    mixer_stream_stop(&m);
    mixer_render(&m, out, 64);
    CHECK(out[0] == 0 && mixer_stream_queued(&m) == 0, "stopped: silent and empty");
}
int main(int argc, char **argv)
{
    test_mixer();
    test_stream();
    test_sfx(NULL);
    if (argc > 1) test_sfx(argv[1]);
    test_sparks();
    test_music(argc > 2 ? argv[2] : NULL);
    printf(fails ? "%d FAILURES\n" : "all sound tests passed\n", fails);
    return fails != 0;
}
