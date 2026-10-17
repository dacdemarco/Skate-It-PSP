#include "progress.h"
#include <string.h>

#define PROFILE_MAGIC 0x52504B53u       /* "SKPR" */
#define PROFILE_VERSION 5u              /* 1: no story flags, 2: + story flags, 3: + 16 statistics and the trick book, 4: + the custom skater (8 bytes), 5: the custom skater of 16 bytes (hair colours, beards, hats, logos) */
#define LEGACY_MAGIC 0x56534B53u        /* "SKSV": the old career.sav */

/* ---- unlocks ------------------------------------------------------------------------------------ */
int progress_unlocked(const Profile *p, int kind, int index)
{
    if (kind < 0 || kind >= UNLOCK_KINDS || index < 0 || index >= CAREER_MAX_INDEX) return 0;
    return (p->unlocked[kind][index >> 3] >> (index & 7)) & 1;
}

static int unlock(Profile *p, int kind, int index)       /* 1 if it was new */
{
    if (progress_unlocked(p, kind, index)) return 0;
    p->unlocked[kind][index >> 3] |= (uint8_t)(1u << (index & 7));
    return 1;
}

static void apply_set(Profile *p, const Career *c, int set, ProgressEvent *ev)
{
    if (set < 0 || (uint32_t)set >= c->nsets) return;
    const CareerSetRef *r = &c->set[set];
    for (int i = 0; i < r->count; i++) {
        uint16_t e = c->setentry[r->first + i];
        int kind = e >> 8, index = e & 0xFF;
        if (unlock(p, kind, index) && ev) {
            ev->unlocked_total++;
            if (kind == UNLOCK_SKATER) ev->skaters_unlocked++;
        }
    }
}

/* ---- pools -------------------------------------------------------------------------------------- */
int progress_chal_done(const Profile *p, int chal)
{
    return chal >= 0 && chal < PROFILE_NDONE && p->done[chal];
}

int progress_pool_done(const Profile *p, const Career *c, int pool)
{
    if (pool < 0 || (uint32_t)pool >= c->npools) return 0;
    const CareerPool *pl = &c->pool[pool];
    int n = 0;
    for (int k = 0; k < pl->nchal; k++) n += progress_chal_done(p, pl->chal[k]);
    return n;
}

int progress_pool(const Profile *p, const Career *c)
{
    for (uint32_t i = 0; i < c->npools; i++) {
        const CareerPool *pl = &c->pool[i];
        if (progress_pool_done(p, c, (int)i) < pl->required) return (int)i;
        if (pl->sponsor >= 0 && p->sponsor[pl->sponsor] == 0) return (int)i;          /* done, but the sponsor has not been chosen */
    }
    return (int)c->npools;
}

int progress_finished(const Profile *p, const Career *c)
{
    return progress_pool(p, c) >= (int)c->npools;
}

/* the start set of every pool up to the current one has been applied (a pool is "started" when it becomes the current one) */
static void advance(Profile *p, const Career *c, ProgressEvent *ev)
{
    int cur = progress_pool(p, c);
    while (p->pools_started <= cur && p->pools_started < (int)c->npools) {
        apply_set(p, c, c->pool[p->pools_started].unlock_set, ev);
        p->pools_started++;
    }
}

int progress_total_done(const Profile *p)
{
    int n = 0;
    for (int i = 0; i < PROFILE_NDONE; i++) n += p->done[i] != 0;
    return n;
}

int progress_chal_unlocked(const Profile *p, int chal)
{
    return chal >= 0 && chal < PROFILE_NDONE && progress_unlocked(p, UNLOCK_CHALLENGE, chal) && !p->done[chal];
}

int progress_chal_open(const Profile *p, const Career *c, int chal)
{
    if (chal < 0 || (uint32_t)chal >= c->nchal || !progress_chal_unlocked(p, chal)) return 0;
    int cur = progress_pool(p, c);
    if (cur >= (int)c->npools) return 0;
    const CareerPool *pl = &c->pool[cur];
    for (int k = 0; k < pl->nchal; k++)
        if (pl->chal[k] == chal) return 1;
    return 0;
}

int progress_open_list(const Profile *p, const Career *c, int *out, int max)
{
    int n = 0;
    for (uint32_t i = 0; i < c->nchal && n < max; i++)
        if (progress_chal_open(p, c, (int)i)) out[n++] = (int)i;
    return n;
}

int progress_seen(const Profile *p, int bit)
{
    return bit >= 0 && bit < PROFILE_NSEEN * 8 && ((p->seen[bit >> 3] >> (bit & 7)) & 1);
}

void progress_set_seen(Profile *p, int bit)
{
    if (bit >= 0 && bit < PROFILE_NSEEN * 8) p->seen[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

/* ---- new profile, completions ---------------------------------------------------------------------- */
void profile_init(Profile *p, const Career *c, const char *name)
{
    memset(p, 0, sizeof(*p));
    if (name) {
        strncpy(p->name, name, PROFILE_NAME_MAX - 1);
        p->name[PROFILE_NAME_MAX - 1] = 0;
    }
    p->skater = 12;                                              /* Aaron, the original's first skater (the profile's defaults, set 0, unlock him) */
    for (int s = 0; s <= 10; s++)                                /* the new-profile defaults */
        apply_set(p, c, s, NULL);
    advance(p, c, NULL);                                         /* pool 0 starts */
}

int progress_complete(Profile *p, const Career *c, int chal, ProgressEvent *ev)
{
    ProgressEvent local;
    if (!ev) ev = &local;
    memset(ev, 0, sizeof(*ev));
    ev->pool_before = ev->pool_after = progress_pool(p, c);
    ev->finished = progress_finished(p, c);
    if (chal < 0 || (uint32_t)chal >= c->nchal || chal >= PROFILE_NDONE || p->done[chal])
        return 0;
    p->done[chal] = 1;
    ev->newly_done = 1;
    p->stat[STAT_CHALLENGES]++;
    apply_set(p, c, c->chal[chal].reward == CAREER_NO_SET ? -1 : c->chal[chal].reward, ev);
    for (uint32_t i = 0; i < c->npools; i++) {                   /* a sponsor challenge chooses its sponsor (the pool of the shoe sponsors also holds a bonus, A Piece Of China, that has none) */
        const CareerPool *pl = &c->pool[i];
        if (pl->sponsor < 0) continue;
        for (int k = 0; k < pl->nchal; k++)
            if (pl->chal[k] == chal && p->sponsor[pl->sponsor] == 0 && c->story && c->story[chal].slot == (uint8_t)pl->sponsor)
                p->sponsor[pl->sponsor] = (uint16_t)(chal + 1);
    }
    advance(p, c, ev);
    ev->pool_after = progress_pool(p, c);
    ev->finished = progress_finished(p, c);
    return 1;
}

/* ---- the file --------------------------------------------------------------------------------------- */
static uint32_t profile_crc(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
    }
    return ~c;
}

static void put32(uint8_t *b, size_t *o, uint32_t v) { for (int i = 0; i < 4; i++) b[(*o)++] = (uint8_t)(v >> (8 * i)); }
static void put16(uint8_t *b, size_t *o, uint32_t v) { for (int i = 0; i < 2; i++) b[(*o)++] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *b, size_t *o) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)b[(*o)++] << (8 * i); return v; }
static uint32_t get16(const uint8_t *b, size_t *o) { uint32_t v = 0; for (int i = 0; i < 2; i++) v |= (uint32_t)b[(*o)++] << (8 * i); return v; }

/* magic, version, name[16], skater, pools_started, 2 pad, sponsor u16 x4, stat u32 x8 (version 1, 2) or x16 (3), done[128], unlocked[16][32], seen[16] (version 2 on),
 * landed[16] (version 3), has_custom u8 + CustomSel 8 bytes (version 4) or 16 bytes (version 5), crc32 of all that */
static int nstat_of(int version) { return version >= 3 ? PROFILE_NSTAT : 8; }

static size_t size_of(int version)
{
    return 4 + 4 + PROFILE_NAME_MAX + 4 + PROFILE_NSPONSOR * 2 + (size_t)nstat_of(version) * 4 + PROFILE_NDONE + UNLOCK_KINDS * (CAREER_MAX_INDEX / 8) + (version >= 2 ? PROFILE_NSEEN : 0) + (version >= 3 ? PROFILE_NLANDED : 0) + (version == 4 ? 9 : (version >= 5 ? 17 : 0)) + 4;
}

size_t profile_size(void)
{
    return size_of((int)PROFILE_VERSION);
}

size_t profile_save(const Profile *p, uint8_t *buf, size_t cap)
{
    if (cap < profile_size()) return 0;
    size_t o = 0;
    put32(buf, &o, PROFILE_MAGIC); put32(buf, &o, PROFILE_VERSION);
    memcpy(buf + o, p->name, PROFILE_NAME_MAX); o += PROFILE_NAME_MAX;
    buf[o++] = p->skater; buf[o++] = p->pools_started; buf[o++] = 0; buf[o++] = 0;
    for (int i = 0; i < PROFILE_NSPONSOR; i++) put16(buf, &o, p->sponsor[i]);
    for (int i = 0; i < PROFILE_NSTAT; i++) put32(buf, &o, p->stat[i]);
    memcpy(buf + o, p->done, PROFILE_NDONE); o += PROFILE_NDONE;
    memcpy(buf + o, p->unlocked, sizeof(p->unlocked)); o += sizeof(p->unlocked);
    memcpy(buf + o, p->seen, PROFILE_NSEEN); o += PROFILE_NSEEN;
    memcpy(buf + o, p->landed, PROFILE_NLANDED); o += PROFILE_NLANDED;
    buf[o++] = p->has_custom;
    memcpy(buf + o, &p->custom, sizeof(CustomSel)); o += sizeof(CustomSel);
    put32(buf, &o, profile_crc(buf, o));
    return o;
}

/* the ranges the editor can produce (the lists' lengths are checked when the parts are loaded) */
static int custom_fields_ok(const CustomSel *cs)
{
    return cs->gender < 2 && cs->build < CUSTOM_NBUILD && cs->skin < CUSTOM_NSKIN && cs->face < CUSTOM_NFACE && cs->hair < CUSTOM_NHAIR && cs->hair_col < CUSTOM_NCOL && cs->beard < CUSTOM_NBEARD &&
           cs->beard_col < CUSTOM_NCOL && cs->hat < CUSTOM_NHAT && custom_hat_exists(cs->gender, cs->hat) && cs->hat_tex < CUSTOM_MAXLIST && cs->top < CUSTOM_MAXLIST && cs->logo <= CUSTOM_MAXLIST &&
           cs->pants < CUSTOM_MAXLIST && cs->shoes < CUSTOM_MAXLIST;
}

int profile_load(Profile *p, const Career *c, const uint8_t *buf, size_t size)
{
    if (size < 8) return 0;
    size_t o = 0;
    if (get32(buf, &o) != PROFILE_MAGIC) return 0;
    int version = (int)get32(buf, &o);
    if (version < 1 || version > (int)PROFILE_VERSION || size != size_of(version)) return 0;
    size_t crc_at = size - 4, q = crc_at;
    if (get32(buf, &q) != profile_crc(buf, crc_at)) return 0;
    Profile t;
    memset(&t, 0, sizeof(t));
    memcpy(t.name, buf + o, PROFILE_NAME_MAX); o += PROFILE_NAME_MAX;
    t.name[PROFILE_NAME_MAX - 1] = 0;
    t.skater = buf[o++]; t.pools_started = buf[o++]; o += 2;
    for (int i = 0; i < PROFILE_NSPONSOR; i++) t.sponsor[i] = (uint16_t)get16(buf, &o);
    for (int i = 0; i < nstat_of(version); i++) t.stat[i] = get32(buf, &o);
    for (int i = 0; i < PROFILE_NDONE; i++) t.done[i] = buf[o++] ? 1 : 0;
    memcpy(t.unlocked, buf + o, sizeof(t.unlocked)); o += sizeof(t.unlocked);
    if (version >= 2) { memcpy(t.seen, buf + o, PROFILE_NSEEN); o += PROFILE_NSEEN; }
    if (version >= 3) { memcpy(t.landed, buf + o, PROFILE_NLANDED); o += PROFILE_NLANDED; }
    if (version >= 4) {
        t.has_custom = buf[o++] ? 1 : 0;
        if (version == 4) { t.custom = custom_from_v1(buf + o); o += 8; }          /* the first editor's choice: mapped onto the new fields */
        else { memcpy(&t.custom, buf + o, sizeof(CustomSel)); o += sizeof(CustomSel); }
        if (t.has_custom && !custom_fields_ok(&t.custom)) return 0;
    }
    if (t.pools_started > c->npools || (c->nskaters && t.skater >= c->nskaters && !(t.skater == PROFILE_CUSTOM_SKATER && t.has_custom))) return 0;
    for (int i = 0; i < PROFILE_NSPONSOR; i++)
        if (t.sponsor[i] > c->nchal) return 0;
    advance(&t, c, NULL);
    *p = t;
    return version == 1 ? 2 : 1;
}

int profile_import_legacy(Profile *p, const Career *c, const uint8_t *buf, size_t size, const char *name)
{
    if (size < 8 + PROFILE_NDONE) return 0;
    size_t o = 0;
    if (get32(buf, &o) != LEGACY_MAGIC || get32(buf, &o) != 1u) return 0;
    profile_init(p, c, name);
    for (int i = 0; i < PROFILE_NDONE; i++)
        if (buf[o + (size_t)i]) progress_complete(p, c, i, NULL);
    return 1;
}
