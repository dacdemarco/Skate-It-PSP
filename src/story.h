/* The career's story (M7; no PSP dependencies, host-tested in tests/test_story.c): which story scene, signing screen or ending the player is due to see, given the profile.
 * The scenes are the original's NIS dialogue (57 of them, data/career.dat), shown as text. Which scene plays when is partly the ROM's (the challenge fields name the scenes before / after
 * a sponsor challenge) and partly ours (the pool changes, from the order of the story: tools/convert_career.py POOL_SCENES).
 * What is DUE is worked out from the profile, in the order of the career (pool by pool: its start scene, the win scenes of its challenges, the sponsor that was signed, its end scene; then the
 * ending and the credits), and what has been SHOWN is a bit of Profile.seen: scene n = bit n, the signing of sponsor slot k = SEEN_SIGNED + k, the ending, the credits. So the story
 * cannot be lost by quitting half way: it comes up again at the next map. */
#ifndef STORY_H
#define STORY_H

#include "progress.h"

enum { STORY_NONE = 0, STORY_SCENE, STORY_SIGNED, STORY_ENDING, STORY_CREDITS };
#define SEEN_SIGNED 64                  /* + the sponsor slot (0 board .. 3 shoes) */
#define SEEN_ENDING 70                  /* the "Skater Of The Year" cover */
#define SEEN_CREDITS 71

typedef struct {
    int kind;                           /* STORY_* */
    int scene;                          /* STORY_SCENE: 1 .. CAREER_NSCENES */
    int slot;                           /* STORY_SIGNED: the sponsor slot */
    int chal;                           /* STORY_SIGNED: the sponsor challenge; STORY_SCENE: the challenge it belongs to (-1 none) */
    int bit;                            /* the Profile.seen bit that marks it */
} StoryItem;

/* the next item that is due and not shown yet; returns 1 and fills `it`, or 0 when there is none */
int story_next(const Profile *p, const Career *c, StoryItem *it);
/* it has been shown */
void story_done(Profile *p, const StoryItem *it);
/* the scene to show before the event is started (0 = none, or it has been shown, or the event is done) */
int story_intro(const Profile *p, const Career *c, int chal);
/* an old profile (version 1) has no story flags: everything that is due now counts as shown */
void story_catch_up(Profile *p, const Career *c);

/* a scene as pages: a page is the lines of one speaker, at most STORY_PAGE_LINES of them; the speaker's name comes from the first line of a turn ("Reda: Hey kid.") and carries over to the
 * pages that follow it without a name */
#define STORY_PAGE_LINES 3
#define STORY_MAX_PAGES 24
#define STORY_NAME_MAX 24
typedef struct { int first, n; char speaker[STORY_NAME_MAX]; } StoryPage;
int story_pages(const Career *c, int scene, StoryPage *out, int max);
/* the text of a line without its "Name: " prefix; the name is copied to `speaker` (empty if the line has none) */
const char *story_line_text(const char *line, char *speaker, size_t cap);

#endif
