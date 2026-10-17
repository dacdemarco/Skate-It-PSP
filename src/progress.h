/* The career's progression and the player's profile (no PSP dependencies - host-tested in tests/test_progress.c against the real data/career.dat).
 * The rules are the original's (docs/CAMPAIGN_FORMAT.md section 8, from the ROM's tables):
 *   - 19 sequential POOLS. The CURRENT pool is the first one with fewer than `required` challenges done, or - when that many are done - one that has a sponsor slot and no sponsor chosen yet; past
 *     the last pool the career is finished. Only the current pool's challenges are open, and only the ones unlocked so far that are not done.
 *   - UNLOCK SETS: sets 0-10 are the defaults of a new profile; the set named by a pool is applied when the pool becomes the current one; the set named by a challenge (CareerChal.reward, 30 + its
 *     index in the ROM) is applied when it is completed (it unlocks the challenges chained behind it, and gear / skaters). An unlock is (kind, index): kind 0 = skater, 10 = challenge, ...
 *   - SPONSORS: a pool with a sponsor slot (board / trucks / wheels / shoes) finishes with the first challenge of it that is completed: the sponsor of that challenge is the one chosen [likely - the
 *     ROM keeps the chosen sponsors in the save but the choice screen is not decoded; the sponsor challenge's reward set holds the gear]. The pool of the shoe sponsors also holds a bonus (A Piece
 *     Of China) that is not a sponsor challenge: it counts for the pool but does not choose a sponsor, so the pool stays current until a real sponsor is signed.
 * The profile is saved as data/profile<N>.sav (profile_save / profile_load work on buffers; the file access is the caller's). */
#ifndef PROGRESS_H
#define PROGRESS_H

#include "career.h"
#include "custom.h"

#define PROFILE_NAME_MAX 16
#define PROFILE_NDONE 128               /* one byte per challenge of the original's table (same size as the old career.sav) */
#define PROFILE_NSPONSOR 4
#define PROFILE_NSTAT 16
#define PROFILE_NLANDED 16              /* bytes of "trick landed" flags (the trick book): bit = landed_bit(class, type), src/stats.h */
#define PROFILE_SLOTS 3
#define PROFILE_CUSTOM_SKATER 100       /* Profile.skater of the player's own (custom) skater: the choice is in Profile.custom */
#define PROFILE_NSEEN 16                /* bytes of story flags: bit n = a story item has been shown (src/story.h says which bits mean what) */

/* indices into Profile.stat (src/stats.c fills them while the player skates). Times in seconds unless noted, distances in centimetres, STAT_BEST_AIR in milliseconds; the BEST ones are maxima, the others totals */
enum { STAT_PLAY_TIME = 0, STAT_TRICKS, STAT_BEST_LINE, STAT_BAILS, STAT_CHALLENGES,
       STAT_DIST, STAT_DIST_GRIND, STAT_DIST_MANUAL, STAT_DIST_SLIDE, STAT_PUSHES, STAT_BEST_AIR, STAT_BEST_GRIND, STAT_BEST_MANUAL, STAT_BEST_SLIDE, STAT_AIRGRAB_TIME, STAT_RESERVED };

typedef struct {
    char name[PROFILE_NAME_MAX];
    uint8_t skater;                     /* the rider chosen for the career (the original's skater index) */
    uint8_t done[PROFILE_NDONE];        /* 1 = completed */
    uint8_t unlocked[UNLOCK_KINDS][CAREER_MAX_INDEX / 8];     /* bit index of kind: unlocked */
    uint8_t pools_started;              /* the pools whose start set has been applied: 0 .. npools */
    uint16_t sponsor[PROFILE_NSPONSOR]; /* the challenge index + 1 of the chosen sponsor of each slot, 0 = none yet */
    uint32_t stat[PROFILE_NSTAT];
    uint8_t seen[PROFILE_NSEEN];        /* the story scenes / screens the player has been shown */
    uint8_t landed[PROFILE_NLANDED];    /* the tricks that have been landed (the trick book), see src/stats.h */
    uint8_t has_custom;                 /* a custom skater has been made (My Skate > Customize) */
    CustomSel custom;                   /* its parts (src/custom.h) */
} Profile;

/* what a completion changed */
typedef struct {
    int newly_done;                     /* the challenge was not done before */
    int pool_before, pool_after;        /* the current pool before / after */
    int skaters_unlocked;               /* skaters unlocked by this (a count) */
    int unlocked_total;                 /* unlocks of any kind */
    int finished;                       /* the career is finished */
} ProgressEvent;

/* a new profile: the defaults (sets 0 - 10) and the start set of pool 0 */
void profile_init(Profile *p, const Career *c, const char *name);

/* the current pool (0 .. npools - 1), or npools when the career is finished */
int progress_pool(const Profile *p, const Career *c);
int progress_finished(const Profile *p, const Career *c);
/* how many challenges of the pool are done */
int progress_pool_done(const Profile *p, const Career *c, int pool);
/* completions of challenges of pools, all in all (what the career asks for in total is the sum of the pools' `required`) */
int progress_total_done(const Profile *p);

int progress_unlocked(const Profile *p, int kind, int index);
/* open = in the current pool, unlocked, not done */
int progress_chal_open(const Profile *p, const Career *c, int chal);
/* unlocked and not done, whichever pool (the tutorial menu, the career map's "locked" marks use the difference) */
int progress_chal_unlocked(const Profile *p, int chal);
int progress_chal_done(const Profile *p, int chal);
/* the challenge indices that are open, in table order; returns how many */
int progress_open_list(const Profile *p, const Career *c, int *out, int max);

/* the player won this challenge: marks it done, applies its reward set, records the sponsor (when it is a sponsor challenge), advances the pools (applying their start sets). Challenges that are not in a pool
 * (the extra tutorials) just get marked. Returns 1 if the challenge was not done before. ev may be NULL. */
int progress_complete(Profile *p, const Career *c, int chal, ProgressEvent *ev);

/* the story flags (bits 0 .. PROFILE_NSEEN * 8 - 1) */
int progress_seen(const Profile *p, int bit);
void progress_set_seen(Profile *p, int bit);

/* ---- the file ---- */
size_t profile_save(const Profile *p, uint8_t *buf, size_t cap);        /* bytes written, 0 if cap is too small */
size_t profile_size(void);                                              /* the size of a saved profile */
/* 1 if valid (magic, version, checksum, ranges), p is untouched otherwise; 2 if it is a valid file of the first version (no story flags: the caller catches the story up, src/story.h); versions 2 and 3 load too (what they lack is empty) */
int profile_load(Profile *p, const Career *c, const uint8_t *buf, size_t size);
/* the old data/career.sav of the sandbox ('SKSV' v1: done flags only): a new profile with those challenges completed; returns 0 if it is not that file */
int profile_import_legacy(Profile *p, const Career *c, const uint8_t *buf, size_t size, const char *name);

#endif
