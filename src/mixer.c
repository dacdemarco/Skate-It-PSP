#include "mixer.h"
#include <string.h>

void mixer_init(Mixer *m, float master)
{
    memset(m, 0, sizeof(*m));
    m->master = master;
}

void mixer_play(Mixer *m, const Sound *s, float vol, float pitch, float pan)
{
    if (!s || !s->pcm || s->n < 2 || vol <= 0.0f) return;
    Voice *v = &m->shot[0];
    for (int i = 0; i < MIX_SHOTS; i++) {                 /* a free voice, else the oldest */
        if (!m->shot[i].snd) { v = &m->shot[i]; break; }
        if (m->shot[i].age < v->age) v = &m->shot[i];
    }
    v->snd = NULL;                                        /* silence it while the fields change (the audio thread may be mixing) */
    v->pos = 0.0f; v->vol = v->target = vol; v->pitch = pitch > 0.05f ? pitch : 0.05f; v->pan = pan;
    v->age = ++m->clock;
    v->snd = s;
}

void mixer_loop(Mixer *m, int ch, const Sound *s, float vol, float pitch)
{
    Voice *v = &m->loop[ch];
    if (s && s->pcm && s->n >= 2 && vol > 0.001f) {
        if (v->snd != s) {                                /* a new sound on the channel: start it silent and fade in */
            v->snd = NULL;
            v->pos = 0.0f; v->vol = 0.0f;
            v->pan = 0.0f;
            v->snd = s;
        }
        v->target = vol;
        v->pitch = pitch > 0.05f ? pitch : 0.05f;
    } else {
        v->target = 0.0f;                                 /* fades out, then the render pass frees the voice */
    }
}

/* one voice into the float accumulators; returns 0 when it has finished */
static int mix_voice(Voice *v, float *acc, int frames, float master, int is_loop)
{
    const Sound *s = v->snd;
    if (!s) return 0;
    float step = ((float)s->rate / (float)MIX_RATE) * v->pitch;
    float dv = (v->target - v->vol) / (float)frames, vol = v->vol, pos = v->pos;
    float pl = v->pan <= 0.0f ? 1.0f : 1.0f - v->pan, pr = v->pan >= 0.0f ? 1.0f : 1.0f + v->pan;
    uint32_t n = s->n;
    for (int i = 0; i < frames; i++) {
        uint32_t ip = (uint32_t)pos;
        if (ip >= n) {
            if (is_loop && s->loop) { pos -= (float)n; ip = (uint32_t)pos; if (ip >= n) { ip = 0; pos = 0.0f; } }
            else if (is_loop)      { pos = 0.0f; ip = 0; }        /* a loop channel playing a non-looping sound repeats it */
            else { v->snd = NULL; return 0; }
        }
        float f = pos - (float)ip;
        uint32_t ip2 = ip + 1 < n ? ip + 1 : (is_loop ? 0 : ip);
        float smp = ((float)s->pcm[ip] * (1.0f - f) + (float)s->pcm[ip2] * f) * (vol * master * (1.0f / 32768.0f));
        acc[2 * i] += smp * pl;
        acc[2 * i + 1] += smp * pr;
        pos += step;
        vol += dv;
    }
    v->pos = pos; v->vol = v->target;
    if (is_loop && v->target <= 0.0f) { v->snd = NULL; return 0; }
    return 1;
}

/* ---- music ------------------------------------------------------------------------------------- */
static const int16_t ima_step[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t ima_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

int mixer_adpcm_block(const uint8_t *p, uint32_t bytes, int16_t *out)
{
    if (bytes < 5) return 0;
    int pred = (int16_t)(uint16_t)(p[0] | (p[1] << 8)), idx = p[2] | (p[3] << 8);
    if (idx > 88) idx = 88;
    int n = 0;
    for (uint32_t i = 4; i < bytes; i++)
        for (int h = 0; h < 2; h++) {
            int nib = h ? (p[i] >> 4) : (p[i] & 15);
            int step = ima_step[idx], diff = step >> 3;
            if (nib & 1) diff += step >> 2;
            if (nib & 2) diff += step >> 1;
            if (nib & 4) diff += step;
            pred += (nib & 8) ? -diff : diff;
            if (pred > 32767) pred = 32767;
            if (pred < -32768) pred = -32768;
            idx += ima_index[nib & 7];
            if (idx < 0) idx = 0;
            if (idx > 88) idx = 88;
            out[n++] = (int16_t)pred;
        }
    return n;
}

/* decode block blk of the current track into b[1..n] (b[0] keeps the last sample of the block before) */
static void music_block(MusicVoice *mv, uint32_t blk)
{
    const MusicTrack *t = mv->cur;
    uint32_t bytes = blk + 1 == t->nblocks ? t->last_bytes : 512;
    int16_t last = mv->n > 0 ? mv->b[mv->n] : 0;
    int16_t tmp[1020];
    int n = mixer_adpcm_block(t->data + (size_t)blk * 512, bytes, tmp);
    if (n > 1016) n = 1016;
    mv->b[0] = last;
    for (int i = 0; i < n; i++) mv->b[1 + i] = tmp[i];
    mv->b[n + 1] = n > 0 ? tmp[n - 1] : last;
    mv->n = n; mv->blk = blk;
}

static void music_start(MusicVoice *mv, const MusicTrack *t)
{
    mv->cur = t; mv->pos = 0.0f; mv->n = 0;
    if (t && t->nblocks > 0) music_block(mv, 0);
    else mv->cur = NULL;
    if (mv->cur && mv->n > 0) mv->b[0] = mv->b[1];
}

void mixer_music_play(Mixer *m, const MusicTrack *t, float vol)
{
    MusicVoice *mv = &m->music;
    mv->next = NULL;
    mv->vol = 0.0f; mv->target = vol;
    music_start(mv, t);
}

void mixer_music_queue(Mixer *m, const MusicTrack *t) { m->music.next = t; }
void mixer_music_volume(Mixer *m, float vol) { m->music.target = vol; }
void mixer_music_stop(Mixer *m) { m->music.target = 0.0f; m->music.next = NULL; m->music.cur = NULL; }
const MusicTrack *mixer_music_current(const Mixer *m) { return m->music.cur; }

float mixer_music_remaining(const Mixer *m)
{
    const MusicVoice *mv = &m->music;
    if (!mv->cur || mv->cur->rate == 0) return 0.0f;
    float done = (float)mv->blk * 1016.0f + mv->pos;
    float left = (float)mv->cur->nsamples - done;
    return left > 0.0f ? left / (float)mv->cur->rate : 0.0f;
}

static void mix_music(MusicVoice *mv, float *acc, int frames)
{
    const MusicTrack *t = mv->cur;
    float dv = (mv->target - mv->vol) / (float)frames, vol = mv->vol;
    if (!t) { mv->vol = mv->target; return; }
    float step = (float)t->rate / (float)MIX_RATE, pos = mv->pos;
    for (int i = 0; i < frames; i++) {
        if (pos >= (float)mv->n) {                              /* next block, or the next track */
            pos -= (float)mv->n;
            uint32_t nb = mv->blk + 1;
            if (nb >= t->nblocks) {
                mv->ended++;
                t = mv->next; mv->next = NULL; mv->cur = t;
                if (!t) { mv->vol = vol; mv->pos = 0.0f; return; }
                mv->n = 0; nb = 0; pos = 0.0f; step = (float)t->rate / (float)MIX_RATE;
            }
            music_block(mv, nb);
            if (mv->n <= 0) { mv->cur = NULL; return; }
        }
        int ip = (int)pos;
        float f = pos - (float)ip;
        float smp = ((float)mv->b[1 + ip] * (1.0f - f) + (float)mv->b[2 + ip] * f) * (vol * (1.0f / 32768.0f));
        acc[2 * i] += smp;
        acc[2 * i + 1] += smp;
        pos += step;
        vol += dv;
    }
    mv->pos = pos; mv->vol = mv->target;
}

static void mix_stream(StreamVoice *s, float *acc, int n, float master)
{
    if (!s->on) return;
    float step = (float)s->rate / (float)MIX_RATE, g = s->vol * master / 32768.0f;
    for (int i = 0; i < n; i++) {
        uint32_t q = s->wr - s->rd;
        if (q < 2) { s->starved++; continue; }                 /* underrun: silence */
        float a = (float)s->buf[s->rd & (MIX_STREAM_CAP - 1)], b = (float)s->buf[(s->rd + 1) & (MIX_STREAM_CAP - 1)];
        float x = (a + (b - a) * s->pos) * g;
        acc[2 * i] += x; acc[2 * i + 1] += x;
        s->pos += step;
        while (s->pos >= 1.0f) { s->pos -= 1.0f; s->rd++; }
    }
}

void mixer_stream_start(Mixer *m, uint32_t rate, float vol)
{
    StreamVoice *s = &m->stream;
    s->on = 0;
    s->rd = s->wr = 0; s->pos = 0.0f; s->rate = rate; s->vol = vol; s->starved = 0;
    s->on = 1;
}

int mixer_stream_space(const Mixer *m) { return (int)(MIX_STREAM_CAP - (m->stream.wr - m->stream.rd)); }
int mixer_stream_queued(const Mixer *m) { return (int)(m->stream.wr - m->stream.rd); }
uint32_t mixer_stream_starved(const Mixer *m) { return m->stream.starved; }

int mixer_stream_push(Mixer *m, const int16_t *pcm, int n)
{
    StreamVoice *s = &m->stream;
    int room = mixer_stream_space(m);
    if (n > room) n = room;
    for (int i = 0; i < n; i++) s->buf[(s->wr + (uint32_t)i) & (MIX_STREAM_CAP - 1)] = pcm[i];
    s->wr += (uint32_t)(n > 0 ? n : 0);
    return n > 0 ? n : 0;
}

void mixer_stream_stop(Mixer *m)
{
    m->stream.on = 0;
    m->stream.rd = m->stream.wr;
}

void mixer_render(Mixer *m, int16_t *out, int frames)
{
    float acc[2 * 1024];
    while (frames > 0) {
        int n = frames > 1024 ? 1024 : frames;
        memset(acc, 0, sizeof(float) * 2 * (size_t)n);
        for (int i = 0; i < MIX_SHOTS; i++) mix_voice(&m->shot[i], acc, n, m->master, 0);
        for (int i = 0; i < MIX_LOOPS; i++) mix_voice(&m->loop[i], acc, n, m->master, 1);
        mix_music(&m->music, acc, n);
        mix_stream(&m->stream, acc, n, m->master);
        for (int i = 0; i < 2 * n; i++) {
            float x = acc[i] * 32768.0f;
            out[i] = (int16_t)(x > 32767.0f ? 32767 : (x < -32768.0f ? -32768 : (int)x));      /* hard clip: the master volume keeps it rare */
        }
        out += 2 * n; frames -= n;
    }
}

int mixer_active(const Mixer *m)
{
    int c = 0;
    for (int i = 0; i < MIX_SHOTS; i++) c += m->shot[i].snd != NULL;
    for (int i = 0; i < MIX_LOOPS; i++) c += m->loop[i].snd != NULL;
    return c;
}
