/* The career data of the original game (data/career.dat, written by tools/convert_career.py from the tables decoded in docs/CAMPAIGN_FORMAT.md): the 103 challenges with their objective
 * lists, the trick table (ids, names, points) and the level table. No PSP dependencies (host-tested in tests/test_goals.c). */
#ifndef CAREER_H
#define CAREER_H

#include <stddef.h>
#include <stdint.h>

/* the original's game mode ids */
enum { MODE_FREESKATE = 0, MODE_TUTORIAL = 1, MODE_TUTORIAL_ADV = 2, MODE_JAM = 3, MODE_SKATE = 4, MODE_BESTTRICK = 5, MODE_OWNTHESPOT = 6, MODE_PHOTO = 7, MODE_FILM = 8, MODE_DEATHRACE = 9, MODE_CHECKPOINT = 10 };
/* objective types */
enum { OBJ_SCORE = 0, OBJ_LINE_SCORE = 1, OBJ_TRICK = 2, OBJ_AIR_TIME = 3, OBJ_SPIN = 4, OBJ_LINE = 5, OBJ_SKATE_GAME = 7, OBJ_JAM = 8, OBJ_BEST_TRICK = 9, OBJ_OWN_THE_SPOT = 10, OBJ_CHECKPOINT = 11 };
/* challenge flags */
enum { CHAL_CAREER = 1, CHAL_LINE = 2, CHAL_MP_ONLY = 4 };
/* objective flags */
enum { OBJF_CHAIN = 1, OBJF_LINE_ONLY = 2 };
#define OBJ_CLASS_MASK 0x40000000u     /* a0 = this | a mask of TRICK_CLASS_* bits instead of a trick id */
#define OBJ_MOD_SPIN 2                 /* mod: a1 is spin degrees, not a count */
#define OBJ_TRICK_PUSH 101             /* the "trick" id of a push */

typedef struct {
    uint16_t idx;
    uint8_t mode, level;
    int16_t time;                      /* seconds, -1 = none */
    uint8_t slot, nobj;                /* slot = the chalNNN object number in the level */
    uint32_t first_obj, name_off, desc_off, map_off;
    uint8_t pool, flags, opp[3], skate_set;
    uint8_t reward;                    /* the unlock set applied when it is completed (CAREER_NO_SET = none) */
    uint8_t pad;
} CareerChal;
#define CAREER_NO_SET 0xFF

typedef struct {
    uint8_t type, flags;
    uint16_t mod;
    uint32_t a0, a1;
    uint16_t rail[2];                  /* a grind line number or a zone number (see goals.c), 0 = none */
    uint32_t text_off;
} CareerObj;

typedef struct {
    uint16_t id, cls;                  /* cls: 0 flip, 1 nollie flip, 2 late flip, 3 grind, 4 grab, 5 manual */
    uint32_t points, per_sec, name_off;
} CareerTrick;

typedef struct {
    uint8_t id, area;
    uint16_t pad;
    uint32_t name_off, zip_off, pad2;
} CareerLevel;

typedef struct {
    uint8_t index, flag;               /* the original's skater index; the stance flag: 1 = regular, 0 = goofy [likely: Koston, Puig, Carroll, Dyrdek are 1, Danny Way 0] */
    uint16_t pad;
    uint32_t name_off, key_off;        /* display name ("Danny Way"), file key ("dannyway": data/skater_<key>.skt) */
    float scale;                       /* 0.6 = the five kids */
} CareerSkater;

/* the story (M7): the NIS dialogue as text scenes, which scene plays before / after a challenge and at a pool's start / end, the sponsor of a challenge, the credits */
typedef struct { uint16_t first, count; } CareerScene;             /* the lines of a scene: line[first .. first + count) */
typedef struct { uint8_t intro, win, slot, pad; uint32_t name_off, icon_off; } CareerStory;       /* scene before / after the challenge (0 = none); the sponsor slot (0xFF = not a sponsor challenge), its name and icon key */
typedef struct { uint8_t start, end; } CareerPoolScene;            /* the scene when the pool becomes the current one / when it is finished (0 = none) */
#define CAREER_NSCENES 57

/* a progression pool: the challenges are open while it is the current pool (src/progress.c) */
#define CAREER_POOL_MAX_CHAL 24
typedef struct {
    uint32_t name_off;
    uint8_t required;                  /* completions that finish the pool */
    int8_t sponsor;                    /* -1 none, else the sponsor slot that must be chosen: 0 board, 1 trucks, 2 wheels, 3 shoes */
    uint8_t unlock_set;                /* applied when the pool becomes the current one */
    uint8_t nchal;
    uint8_t chal[CAREER_POOL_MAX_CHAL];
} CareerPool;
/* an unlock set: entries kind << 8 | index; kinds: 0 skater, 2 hat, 3 My Spot part, 5 shoes, 6 trucks, 7 wheel art, 8 deck art, 10 challenge. The item numbers of 2, 5, 7 and 8 are the positions in the name tables of the ARM9
 * code (tools/convert_custom.py rom_orders; every sponsor's set holds exactly that sponsor's items). 1 (one per sponsor) and 9 (tops / logos?) are not understood, 4 (28 items) are probably the bottoms: all unlocked at the start. */
typedef struct { uint16_t first, count; } CareerSetRef;
enum { UNLOCK_SKATER = 0, UNLOCK_HAT = 2, UNLOCK_SPOT_PART = 3, UNLOCK_SHOES = 5, UNLOCK_TRUCKS = 6, UNLOCK_WHEELS = 7, UNLOCK_DECK = 8, UNLOCK_CHALLENGE = 10, UNLOCK_KINDS = 16 };
#define CAREER_MAX_INDEX 256           /* unlock indices are < 256 */

typedef struct {
    const uint8_t *file;
    uint32_t nchal, nobj, ntricks, nlevels, nskaters, nareas, npools, nsets, nsetentries;
    const CareerPool *pool;
    const CareerSetRef *set;
    const uint16_t *setentry;
    const uint8_t *aiev;               /* the AI score records (career_ai_events) */
    uint32_t aiev_size;
    const uint16_t *ai;                /* nchal * 4: the opponents' recording lengths (career_opponent_samples) */
    const CareerSkater *skater;
    const uint32_t *area_name;         /* string offsets: Urban, Parks, Indoors, Small Spots, Real World (CareerLevel.area indexes them) */
    const CareerChal *chal;
    const CareerObj *obj;
    const CareerTrick *trick;
    const CareerLevel *level;
    uint32_t nscenes, nlines, ncredits;
    const CareerScene *scene;          /* nscenes: scene n (1 .. nscenes) is scene[n - 1] */
    const uint32_t *line;              /* string offsets of the dialogue lines */
    const CareerStory *story;          /* nchal */
    const CareerPoolScene *pool_scene; /* npools */
    const uint32_t *credit;            /* string offsets of the credits lines */
    uint32_t sign_str, soty_str, end_str;   /* "%s has been signed to", "%s has been named Thrasher Skater Of The Year!", the end-of-game text */
    const char *str;
    uint32_t str_size;
} Career;

/* buf stays owned by the caller; returns 0 when the file is not a version 5 career file or runs past its size */
int career_load(Career *c, const uint8_t *buf, size_t size);
const char *career_str(const Career *c, uint32_t off);
const CareerObj *career_objs(const Career *c, const CareerChal *ch);
/* the skaters that can be ridden (the table also holds Reda, the cameraman): the first CAREER_NRIDERS entries */
#define CAREER_NRIDERS 19

/* the level table id of a level file stem ("amphitheatre": case-insensitive compare with the zip name), -1 if unknown */
int career_level_by_stem(const Career *c, const char *stem);
/* the game can run this challenge today (modes Photo, Film, Own The Spot, Tutorial, Checkpoint, Deathrace, Jam, Best Trick, S.K.A.T.E.; objective types it knows; not multiplayer-only). The level's beam is the caller's business. */
int career_chal_runnable(const Career *c, int chal);
/* the display name of a game mode id ("Photo", "Own The Spot" ...) */
const char *career_mode_name(int mode);
/* the score an Own The Spot objective asks for: half way into the original's range, in steps of 25 [guess: the range is the ROM's, the pick is ours] */
long career_own_target(const CareerObj *o);
/* the length of the recorded run of opponent k (0..2) of a race challenge, in samples (0 = none). The original recorded 4 samples per second, so the opponent finished in samples / 4 s */
int career_opponent_samples(const Career *c, int chal, int k);
#define CAREER_AI_HZ 4
/* the recorded opponents of the score contests: kind 0 = the lines he banked in round `round` (n pairs {frame at 60 / s, points}), kind 1 = the S.K.A.T.E. tricks he sets (pairs {index, trick id}, round and
 * opponent 0); NULL when there is no such recording. The pairs are u16 and 2-byte aligned. */
const uint16_t *career_ai_events(const Career *c, int chal, int round, int opp, int kind, int *n);
/* the dialogue of a story scene (1 .. nscenes): how many lines, and one of them ("Reda: Hey kid." the speaker is part of the first line of a turn) */
int career_scene_nlines(const Career *c, int scene);
const char *career_scene_line(const Career *c, int scene, int i);
/* the sponsor of a challenge: its name ("Plan B") and icon key ("planb"), or NULL when it is not a sponsor challenge */
const char *career_sponsor_name(const Career *c, int chal);
const char *career_sponsor_icon(const Career *c, int chal);
/* the skater rides regular (the clips of the ROM are goofy ones: a regular rider is drawn mirrored) */
int career_skater_regular(const Career *c, int skater);
/* the indices of the challenges located in level `level` (career ones only, in slot order) */
int career_challenges_at(const Career *c, int level, int *out, int max);

#endif
