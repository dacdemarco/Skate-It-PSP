#include "sfx.h"
#include "rails.h"
#include "trick.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the sound file ---------------------------------------------------------------------------- */
int soundbank_load(SoundBank *b, const char *path)
{
    memset(b, 0, sizeof(*b));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size < 12) { fclose(f); return 0; }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);
    uint32_t ver, count;
    memcpy(&ver, buf + 4, 4); memcpy(&count, buf + 8, 4);
    if (memcmp(buf, "SKSN", 4) != 0 || ver != 1 || count == 0 || count > 4096 || (long)(12 + 48 * (size_t)count) > size) { free(buf); return 0; }
    b->snd = (Sound *)calloc(count, sizeof(Sound));
    b->name = (char (*)[32])calloc(count, 32);
    if (!b->snd || !b->name) { free(b->snd); free(b->name); free(buf); b->snd = NULL; b->name = NULL; return 0; }
    for (uint32_t i = 0; i < count; i++) {
        const unsigned char *e = buf + 12 + 48 * i;
        uint32_t rate, n, loop, off;
        memcpy(b->name[i], e, 32); b->name[i][31] = 0;
        memcpy(&rate, e + 32, 4); memcpy(&n, e + 36, 4); memcpy(&loop, e + 40, 4); memcpy(&off, e + 44, 4);
        if ((off & 3u) != 0 || (long)off + 2L * (long)n > size) continue;      /* damaged entry: stays empty (never played) */
        b->snd[i].pcm = (const int16_t *)(buf + off); b->snd[i].n = n; b->snd[i].rate = rate; b->snd[i].loop = (int)loop;
    }
    b->n = (int)count; b->blob = buf;
    return 1;
}

void soundbank_free(SoundBank *b)
{
    free(b->snd); free(b->name); free(b->blob);
    memset(b, 0, sizeof(*b));
}

const Sound *soundbank_find(const SoundBank *b, const char *name)
{
    for (int i = 0; i < b->n; i++)
        if (strcmp(b->name[i], name) == 0 && b->snd[i].pcm) return &b->snd[i];
    return NULL;
}

/* ---- effects ----------------------------------------------------------------------------------- */
void sfx_init(Sfx *f, const SoundBank *b)
{
    memset(f, 0, sizeof(*f));
    static const char *floor_nm[2] = { "CNC", "WOOD" }, *size_nm[2] = { "SMALL", "BIG" }, *land_nm[4] = { "TINY", "SMALL", "MED", "BIG" };
    static const char *mat[3] = { "CONC", "METAL", "WOOD" }, *kind[2] = { "2T", "BRD" };
    char nm[48];
    for (int m = 0; m < 2; m++) {
        for (int k = 0; k < 2; k++) { snprintf(nm, sizeof(nm), "SFX_OLLIE_%s_%s", floor_nm[m], size_nm[k]); f->ollie[m][k] = soundbank_find(b, nm); }
        for (int k = 0; k < 4; k++) { snprintf(nm, sizeof(nm), "SFX_LAND_%s_%s", floor_nm[m], land_nm[k]); f->land[m][k] = soundbank_find(b, nm); }
    }
    f->roll[0] = soundbank_find(b, "SFX_ROLL_MEDIUM");
    f->roll[1] = soundbank_find(b, "SFX_ROLL_ROUGH");
    f->roll[2] = soundbank_find(b, "SFX_ROLL_SMOOTH");
    f->slide = soundbank_find(b, "SFX_ROLL_SLIDE");
    f->brake_in = soundbank_find(b, "SFX_BRAKE_IN");
    f->brake_loop = soundbank_find(b, "SFX_BRAKE_LOOP");
    f->air = soundbank_find(b, "SFX_AIR_LOOP");
    f->push_on = soundbank_find(b, "SFX_PUSHON");
    f->push_off = soundbank_find(b, "SFX_PUSHOFF");
    f->grunt = soundbank_find(b, "SFX_GRUNT_1");
    f->wipe_body = soundbank_find(b, "SFX_WIPEOUT_BODY_HI");
    for (int m = 0; m < 3; m++) {
        snprintf(nm, sizeof(nm), "SFX_WIPEOUT_%s_HI", mat[m]); f->wipe_hi[m] = soundbank_find(b, nm);
        snprintf(nm, sizeof(nm), "SFX_WIPEOUT_%s_LO", mat[m]); f->wipe_lo[m] = soundbank_find(b, nm);
        for (int k = 0; k < 2; k++) {
            snprintf(nm, sizeof(nm), "SFX_GRIND_IN_%s_%s", mat[m], kind[k]);   f->grind_in[m][k] = soundbank_find(b, nm);
            snprintf(nm, sizeof(nm), "SFX_GRIND_OUT_%s_%s", mat[m], kind[k]);  f->grind_out[m][k] = soundbank_find(b, nm);
            snprintf(nm, sizeof(nm), "SFX_GRIND_LOOP_%s_%s", mat[m], kind[k]); f->grind_loop[m][k] = soundbank_find(b, nm);
        }
    }
    f->kick_first = soundbank_find(b, "SFX_KICK_FIRST");
    f->kick_second = soundbank_find(b, "SFX_KICK_SECOND");
    f->scrape[0] = soundbank_find(b, "SFX_SCRAPE_LEFT");
    f->scrape[1] = soundbank_find(b, "SFX_SCRAPE_RIGHT");
    f->grab_on = soundbank_find(b, "SFX_GRABON");
    f->grab_off = soundbank_find(b, "SFX_GRABOFF");
    f->jingle[SFX_EV_BANK] = soundbank_find(b, "SFX_HUD_CHALLENGE_LETTER");
    f->jingle[SFX_EV_BANK_BIG] = soundbank_find(b, "SFX_HUD_CHALLENGE_OBJECTIVE");
    f->jingle[SFX_EV_RESPAWN] = soundbank_find(b, "SFX_HUD_SPAWN_USE");
    f->jingle[SFX_EV_MOVE] = soundbank_find(b, "SFX_MOVE");
    f->jingle[SFX_EV_ENTER] = soundbank_find(b, "SFX_ENTER");
    f->jingle[SFX_EV_BACK] = soundbank_find(b, "SFX_BACK");
    f->jingle[SFX_EV_321] = soundbank_find(b, "SFX_HUD_321");
    f->jingle[SFX_EV_GO] = soundbank_find(b, "SFX_HUD_GO");
    f->jingle[SFX_EV_WIN] = soundbank_find(b, "SFX_HUD_CHALLENGE_WIN");
    f->jingle[SFX_EV_LOSE] = soundbank_find(b, "SFX_HUD_CHALLENGE_LOSE");
    f->jingle[SFX_EV_CHECKPOINT] = soundbank_find(b, "SFX_HUD_CHALLENGE_CHECKPOINT");
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void sfx_event(Sfx *f, Mixer *m, int what)
{
    if (what >= 0 && what < SFX_EV_COUNT) mixer_play(m, f->jingle[what], 0.7f, 1.0f, 0.0f);
}

void sfx_update(Sfx *f, Mixer *m, Skate *s, const SkateInput *in, int surf, float dt)
{
    float spd = fabsf(s->speed);
    int bail = s->bail_t > 0.0f;
    int wood = (surf & 3) == 2;
    int roll_kind = (surf >> 2) & 3;
    if (roll_kind > 2) roll_kind = 0;
    if (wood) roll_kind = 1;                              /* no wooden roll sound: boards on planks rumble */
    int fm = (surf & 3) == 2 ? 1 : 0;                     /* floor sounds exist for concrete and wood */
    int wm = (surf & 3) < 3 ? (surf & 3) : 0;
    if (s->grounded) f->last_surf = surf;
    f->scrape_cool -= dt;

    /* one-shots from the skater's events */
    if (s->ev_ollie) {
        mixer_play(m, f->ollie[(f->last_surf & 3) == 2][s->ollie_pop > 3.4f], 0.9f, 1.0f, 0.0f);
        s->ev_ollie = 0;
    }
    if (s->ev_land) {
        float imp = s->land_impact;
        if (bail) {
            mixer_play(m, imp > 4.0f ? f->wipe_hi[wm] : f->wipe_lo[wm], 0.9f, 1.0f, 0.0f);
        } else if (imp > 1.0f) {
            int cls = imp < 2.0f ? 0 : (imp < 3.5f ? 1 : (imp < 5.5f ? 2 : 3));
            mixer_play(m, f->land[fm][cls], 0.55f + 0.1f * (float)cls, 1.0f, 0.0f);
        }
        s->ev_land = 0;
    }
    if (bail && !f->was_bail) {                          /* the fall: a grunt and the body hitting */
        mixer_play(m, f->grunt, 0.8f, 1.0f, 0.0f);
        mixer_play(m, f->wipe_body, 0.9f, 1.0f, 0.0f);
    }
    if (s->ev_scrape) {                                  /* sliding along a wall */
        if (f->scrape_cool <= 0.0f) {
            mixer_play(m, f->scrape[s->scrape_side > 0.0f ? 0 : 1], 0.7f, 1.0f, 0.0f);
            f->scrape_cool = 0.3f;
        }
        s->ev_scrape = 0;
    }
    if (s->push_t >= 0.0f && s->grounded) {              /* foot down at 25 % of the push, off again at 60 % */
        float ph = s->push_t / SKATE_PUSH_TIME;
        if (!f->push_on_done && ph >= 0.25f) { mixer_play(m, f->push_on, 0.6f, 1.0f, 0.0f); f->push_on_done = 1; }
        if (!f->push_off_done && ph >= 0.60f) { mixer_play(m, f->push_off, 0.5f, 1.0f, 0.0f); f->push_off_done = 1; }
    } else {
        f->push_on_done = f->push_off_done = 0;
    }
    int braking = in->brake && s->grounded && spd > 1.0f && !bail;
    if (braking && !f->was_brake) mixer_play(m, f->brake_in, 0.6f, 1.0f, 0.0f);

    /* manuals: the kick that raises the board, and the tail scraping when the balance is nearly gone */
    if (s->manual && !f->was_manual) mixer_play(m, s->manual == 1 ? f->kick_first : f->kick_second, 0.7f, 1.0f, 0.0f);
    int warn = s->manual && s->lean > 0.8f;
    if (warn && !f->was_warn) mixer_play(m, f->scrape[1], 0.6f, 1.0f, 0.0f);

    /* air tricks: a flip kicks the board twice, a grab has its own on / off sounds */
    if (s->flip && !f->was_flip) { mixer_play(m, f->kick_first, 0.7f, 1.0f, 0.0f); f->flip_mid_done = 0; }
    if (s->flip && !f->flip_mid_done && s->flip_t >= 0.55f * s->flip_dur) { mixer_play(m, f->kick_second, 0.7f, 1.0f, 0.0f); f->flip_mid_done = 1; }
    if (s->grab && !f->was_grab) mixer_play(m, f->grab_on, 0.7f, 1.0f, 0.0f);
    if (!s->grab && f->was_grab) mixer_play(m, f->grab_off, 0.7f, 1.0f, 0.0f);

    /* grinding: the lock-on, the loop, the way off */
    if (s->grinding && !f->was_grinding) {
        f->grind_mat = s->ev_grind_mat >= 0 && s->ev_grind_mat < 3 ? s->ev_grind_mat : 0;
        f->grind_board = s->grind_type >= 0 && s->grind_type < NGRIND_TYPES ? tricks.grind[s->grind_type].board != 0 : 0;
        mixer_play(m, f->grind_in[f->grind_mat][f->grind_board], 0.8f, 1.0f, 0.0f);
    }
    if (!s->grinding && f->was_grinding)
        mixer_play(m, f->grind_out[f->grind_mat][f->grind_board], 0.7f, 1.0f, 0.0f);
    if (s->grinding)
        mixer_loop(m, SFX_LOOP_GRIND, f->grind_loop[f->grind_mat][f->grind_board], 0.55f * clampf(spd / 3.0f, 0.35f, 1.0f), clampf(0.85f + 0.06f * spd, 0.85f, 1.35f));
    else
        mixer_loop(m, SFX_LOOP_GRIND, NULL, 0.0f, 1.0f);

    /* wheels on the ground (louder and higher with speed); a fallen rider slides instead */
    float rv = 0.0f;
    if (s->grounded && spd > 0.3f) rv = clampf(spd / 7.0f, 0.0f, 1.0f) * 0.5f;
    if (bail) mixer_loop(m, SFX_LOOP_ROLL, s->grounded && spd > 0.4f ? f->slide : NULL, clampf(spd / 4.0f, 0.0f, 1.0f) * 0.5f, 0.9f + 0.05f * spd);
    else      mixer_loop(m, SFX_LOOP_ROLL, rv > 0.0f ? f->roll[roll_kind] : NULL, rv, clampf(0.75f + 0.05f * spd, 0.75f, 1.5f));

    /* the board drags while braking */
    mixer_loop(m, SFX_LOOP_BRAKE, braking ? f->brake_loop : NULL, 0.45f * clampf(spd / 5.0f, 0.3f, 1.0f), clampf(0.9f + 0.04f * spd, 0.9f, 1.3f));

    /* wind in a long jump */
    float vv = sqrtf(s->vel[0] * s->vel[0] + s->vel[1] * s->vel[1] + s->vel[2] * s->vel[2]);
    int flying = !s->grounded && !s->grinding && s->air_t > 0.15f && !bail;
    mixer_loop(m, SFX_LOOP_AIR, flying ? f->air : NULL, 0.35f * clampf(vv / 9.0f, 0.0f, 1.0f), clampf(0.9f + 0.03f * vv, 0.9f, 1.3f));

    f->was_grinding = s->grinding; f->was_bail = bail; f->was_brake = braking; f->was_manual = s->manual; f->was_warn = warn; f->was_grab = s->grab != 0; f->was_flip = s->flip != 0;
}
