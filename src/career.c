#include "career.h"
#include <string.h>
#include <strings.h>

_Static_assert(sizeof(CareerChal) == 32, "CareerChal");
_Static_assert(sizeof(CareerObj) == 20, "CareerObj");
_Static_assert(sizeof(CareerTrick) == 16, "CareerTrick");
_Static_assert(sizeof(CareerLevel) == 16, "CareerLevel");
_Static_assert(sizeof(CareerSkater) == 16, "CareerSkater");
_Static_assert(sizeof(CareerPool) == 32, "CareerPool");
_Static_assert(sizeof(CareerSetRef) == 4, "CareerSetRef");
_Static_assert(sizeof(CareerScene) == 4 && sizeof(CareerStory) == 12 && sizeof(CareerPoolScene) == 2, "story tables");

int career_load(Career *c, const uint8_t *buf, size_t size)
{
    memset(c, 0, sizeof(*c));
    if (size < 128 || memcmp(buf, "SKCR", 4) != 0)
        return 0;
    uint32_t h[26];
    memcpy(h, buf, sizeof(h));
    if (h[1] != 5)
        return 0;
    uint32_t nchal = h[2], off_chal = h[3], nobj = h[4], off_obj = h[5], nt = h[6], off_t = h[7], nl = h[8], off_l = h[9], off_s = h[10], size_s = h[11];
    if ((uint64_t)off_chal + (uint64_t)nchal * 32 > size || (uint64_t)off_obj + (uint64_t)nobj * 20 > size || (uint64_t)off_t + (uint64_t)nt * 16 > size ||
        (uint64_t)off_l + (uint64_t)nl * 16 > size || (uint64_t)off_s + size_s > size || size_s == 0 || buf[off_s + size_s - 1] != 0)
        return 0;
    if ((off_chal | off_obj | off_t | off_l) & 3)
        return 0;
    uint32_t nsk = h[12], off_sk = h[13];
    uint32_t nar = h[14], off_ar = h[15];
    if ((uint64_t)off_ar + (uint64_t)nar * 4 > size || (off_ar & 3))
        return 0;
    if ((uint64_t)off_sk + (uint64_t)nsk * 16 > size || (off_sk & 3))
        return 0;
    uint32_t npl = h[16], off_pl = h[17], nst = h[18], off_st = h[19], nse = h[20], off_se = h[21];
    if ((uint64_t)off_pl + (uint64_t)npl * 32 > size || (uint64_t)off_st + (uint64_t)nst * 4 > size || (uint64_t)off_se + (uint64_t)nse * 2 > size || ((off_pl | off_st | off_se) & 3) || (off_se & 1))
        return 0;
    uint32_t off_ai = h[22];
    if ((uint64_t)off_ai + (uint64_t)nchal * 8 > size || (off_ai & 1))
        return 0;
    c->ai = (const uint16_t *)(buf + off_ai);
    uint32_t off_ev = h[23], size_ev = h[24];
    if ((uint64_t)off_ev + size_ev > size || (off_ev & 1))
        return 0;
    {                                                            /* the records must run exactly to the end of the section */
        uint32_t at = 0;
        while (at + 6 <= size_ev) {
            uint32_t cnt = (uint32_t)buf[off_ev + at + 4] | ((uint32_t)buf[off_ev + at + 5] << 8);
            at += 6 + cnt * 4;
        }
        if (at != size_ev)
            return 0;
    }
    uint32_t off_story = h[25];
    if (off_story + 32 > size || (off_story & 3))
        return 0;
    {                                                            /* the story section: all its tables must lie inside the file */
        uint32_t sh[8];
        memcpy(sh, buf + off_story, sizeof(sh));
        uint32_t nsc = sh[0], nlines = sh[1], ncr = sh[2], npp = sh[3];
        uint64_t at = (uint64_t)off_story + 32;
        uint64_t o_sc = at, o_ln = o_sc + (uint64_t)nsc * 4, o_ch = o_ln + (uint64_t)nlines * 4, o_pp = o_ch + (uint64_t)nchal * 12, o_cr = o_pp + (((uint64_t)npp * 2 + 3) & ~3ull), end = o_cr + (uint64_t)ncr * 4;
        if (nsc != CAREER_NSCENES || npp != npl || end > size)
            return 0;
        c->nscenes = nsc; c->nlines = nlines; c->ncredits = ncr;
        c->scene = (const CareerScene *)(buf + o_sc);
        c->line = (const uint32_t *)(buf + o_ln);
        c->story = (const CareerStory *)(buf + o_ch);
        c->pool_scene = (const CareerPoolScene *)(buf + o_pp);
        c->credit = (const uint32_t *)(buf + o_cr);
        c->sign_str = sh[4]; c->soty_str = sh[5]; c->end_str = sh[6];
        for (uint32_t i = 0; i < nsc; i++)
            if ((uint32_t)c->scene[i].first + c->scene[i].count > nlines)
                return 0;
        for (uint32_t i = 0; i < nchal; i++) {
            const CareerStory *y = &c->story[i];
            if (y->intro > nsc || y->win > nsc || (y->slot != 0xFF && y->slot > 3))
                return 0;
        }
        for (uint32_t i = 0; i < npp; i++)
            if (c->pool_scene[i].start > nsc || c->pool_scene[i].end > nsc)
                return 0;
        for (uint32_t i = 0; i < nlines; i++)
            if (c->line[i] >= size_s)
                return 0;
        for (uint32_t i = 0; i < ncr; i++)
            if (c->credit[i] >= size_s)
                return 0;
    }
    c->aiev = buf + off_ev;
    c->aiev_size = size_ev;
    c->file = buf;
    c->npools = npl; c->nsets = nst; c->nsetentries = nse;
    c->pool = (const CareerPool *)(buf + off_pl);
    c->set = (const CareerSetRef *)(buf + off_st);
    c->setentry = (const uint16_t *)(buf + off_se);
    c->nskaters = nsk;
    c->nareas = nar;
    c->area_name = (const uint32_t *)(buf + off_ar);
    c->skater = (const CareerSkater *)(buf + off_sk);
    c->nchal = nchal; c->nobj = nobj; c->ntricks = nt; c->nlevels = nl;
    c->chal = (const CareerChal *)(buf + off_chal);
    c->obj = (const CareerObj *)(buf + off_obj);
    c->trick = (const CareerTrick *)(buf + off_t);
    c->level = (const CareerLevel *)(buf + off_l);
    c->str = (const char *)(buf + off_s);
    c->str_size = size_s;
    for (uint32_t i = 0; i < nchal; i++)                         /* every objective list must lie inside the table */
        if ((uint64_t)c->chal[i].first_obj + c->chal[i].nobj > nobj)
            return 0;
    for (uint32_t i = 0; i < nst; i++)                           /* every unlock set must lie inside the entry table */
        if ((uint32_t)c->set[i].first + c->set[i].count > nse)
            return 0;
    for (uint32_t i = 0; i < npl; i++) {                         /* every pool must name challenges and an unlock set that exist */
        const CareerPool *p = &c->pool[i];
        if (p->nchal > CAREER_POOL_MAX_CHAL || p->unlock_set >= nst || (p->sponsor < -1 || p->sponsor > 3))
            return 0;
        for (int k = 0; k < p->nchal; k++)
            if (p->chal[k] >= nchal)
                return 0;
    }
    for (uint32_t i = 0; i < nchal; i++)
        if (c->chal[i].reward != CAREER_NO_SET && c->chal[i].reward >= nst)
            return 0;
    return 1;
}

const char *career_str(const Career *c, uint32_t off)
{
    return off < c->str_size ? c->str + off : "";
}

const CareerObj *career_objs(const Career *c, const CareerChal *ch)
{
    return c->obj + ch->first_obj;
}

int career_level_by_stem(const Career *c, const char *stem)
{
    for (uint32_t i = 0; i < c->nlevels; i++)
        if (strcasecmp(career_str(c, c->level[i].zip_off), stem) == 0)
            return c->level[i].id;
    return -1;
}

int career_scene_nlines(const Career *c, int scene)
{
    return scene >= 1 && (uint32_t)scene <= c->nscenes ? c->scene[scene - 1].count : 0;
}

const char *career_scene_line(const Career *c, int scene, int i)
{
    if (i < 0 || i >= career_scene_nlines(c, scene)) return "";
    return career_str(c, c->line[c->scene[scene - 1].first + (uint32_t)i]);
}

const char *career_sponsor_name(const Career *c, int chal)
{
    if (chal < 0 || (uint32_t)chal >= c->nchal || c->story[chal].slot == 0xFF) return NULL;
    return career_str(c, c->story[chal].name_off);
}

const char *career_sponsor_icon(const Career *c, int chal)
{
    if (chal < 0 || (uint32_t)chal >= c->nchal || c->story[chal].slot == 0xFF) return NULL;
    return career_str(c, c->story[chal].icon_off);
}

int career_skater_regular(const Career *c, int skater)
{
    return skater >= 0 && (uint32_t)skater < c->nskaters && c->skater[skater].flag == 1;
}

int career_challenges_at(const Career *c, int level, int *out, int max)
{
    int n = 0;
    for (uint32_t i = 0; i < c->nchal && n < max; i++) {
        const CareerChal *ch = &c->chal[i];
        if (ch->level == level && (ch->flags & CHAL_CAREER) && ch->slot != 0xFF)
            out[n++] = (int)i;
    }
    for (int i = 1; i < n; i++) {                                /* by slot (insertion sort: a handful of entries) */
        int v = out[i], j = i - 1;
        while (j >= 0 && c->chal[out[j]].slot > c->chal[v].slot) { out[j + 1] = out[j]; j--; }
        out[j + 1] = v;
    }
    return n;
}

int career_chal_runnable(const Career *c, int chal)
{
    if (chal < 0 || (uint32_t)chal >= c->nchal) return 0;
    const CareerChal *ch = &c->chal[chal];
    int race = ch->mode == MODE_CHECKPOINT || ch->mode == MODE_DEATHRACE;           /* the races: the objectives are the gates (src/race.c) */
    int contest = ch->mode == MODE_JAM || ch->mode == MODE_SKATE || ch->mode == MODE_BESTTRICK;       /* the score contests against recorded opponents (src/contest.c) */
    if (!(ch->mode == MODE_PHOTO || ch->mode == MODE_FILM || ch->mode == MODE_OWNTHESPOT || ch->mode == MODE_TUTORIAL || ch->mode == MODE_TUTORIAL_ADV || race || contest)) return 0;
    if (ch->nobj == 0 || (ch->flags & CHAL_MP_ONLY)) return 0;
    for (int i = 0; i < ch->nobj; i++) {
        int t = career_objs(c, ch)[i].type;
        if (race) { if (t != OBJ_CHECKPOINT) return 0; continue; }
        if (contest) { if (t != OBJ_JAM && t != OBJ_SKATE_GAME && t != OBJ_BEST_TRICK) return 0; continue; }
        if (!(t == OBJ_SCORE || t == OBJ_LINE_SCORE || t == OBJ_TRICK || t == OBJ_AIR_TIME || t == OBJ_SPIN || t == OBJ_LINE || t == OBJ_OWN_THE_SPOT)) return 0;
    }
    return 1;
}

const char *career_mode_name(int mode)
{
    static const char *const n[] = { "Free Skate", "Tutorial", "Tutorial", "Jam", "S.K.A.T.E.", "Best Trick", "Own The Spot", "Photo", "Film", "Deathrace", "Checkpoint", "Checkpoint" };
    return mode >= 0 && mode < (int)(sizeof(n) / sizeof(n[0])) ? n[mode] : "";
}

long career_own_target(const CareerObj *o)
{
    long lo = (long)o->a0, hi = (long)o->a1;
    return (lo + (hi - lo) / 2) / 25 * 25;
}

int career_opponent_samples(const Career *c, int chal, int k)
{
    if (chal < 0 || (uint32_t)chal >= c->nchal || k < 0 || k >= 3) return 0;
    return c->ai[chal * 4 + k];
}

/* the records of the AI score section: {u8 chal, round, opp, kind; u16 count; count * {u16 a, u16 b}} */
const uint16_t *career_ai_events(const Career *c, int chal, int round, int opp, int kind, int *n)
{
    uint32_t at = 0;
    *n = 0;
    while (at + 6 <= c->aiev_size) {
        const uint8_t *r = c->aiev + at;
        uint32_t cnt = (uint32_t)r[4] | ((uint32_t)r[5] << 8);
        if (r[0] == chal && r[1] == round && r[2] == opp && r[3] == kind) {
            *n = (int)cnt;
            return (const uint16_t *)(r + 6);
        }
        at += 6 + cnt * 4;
    }
    return NULL;
}
