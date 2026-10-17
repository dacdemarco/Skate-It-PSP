#include "tutorial.h"
#include <stddef.h>

typedef struct {
    int chal;                           /* the original's challenge index */
    const char *summary;
    const char *const *tip;             /* by objective, in the order of the challenge's objective list */
    int ntips;
} TutorialText;

/* The controls are the ones of src/control.h (see the trick guide in src/options.c, which these tips agree with). A line is at most 46 characters (tests/test_tutorial.c). */
static const char *const TIPS_93[] = {
    "Hold {POP} to crouch, let go to pop.",
    "Hold {POP}, flick the nub UP-LEFT, let go.",
    "Hold {POP}, flick the nub UP-RIGHT, let go.",
    "Hold {POP}, flick the nub LEFT, let go.",
    "Hold {POP}, flick the nub RIGHT, let go.",
};
static const char *const TIPS_94[] = {
    "Tap {PUSH} to push. Hold it for a stronger,\nslower push.",
    "Roll at a ledge and let go of {POP} just\nbefore it to pop up and land on top.",
};
static const char *const TIPS_95[] = {
    "Pop off the top of a ramp or a bank\nfor big air.",
    "In the air, hold {GL} or {GR}.",
    "Do a trick and in the air press {PUSH} and\nhold the nub LEFT or RIGHT: a 180 spin.",
};
static const char *const TIPS_96[] = {
    "Ollie at a rail or ledge: the board locks\non by itself. The nub picks the grind.",
};
static const char *const TIPS_97[] = {
    "Crouch with {POP}, hold the nub forward until\nNOLLIE shows, then let go.",
};
static const char *const TIPS_98[] = {
    "Do a flip trick, then pull the nub back and\nhold it as you land to go into a manual.",
    "Pop out of the manual with a flick:\nhold {POP}, flick the nub, let go.",
};
static const char *const TIPS_99[] = {
    "Hold {ADV}, flick the nub UP-LEFT, let go.",
    "Hold {ADV}, flick the nub DOWN-LEFT, let go.",
    "Hold {ADV} and the nub forward until NOLLIE\nshows, flick RIGHT, let go.",
};
static const char *const TIPS_100[] = {
    "Front trucks: land on a rail with the nub UP\n(nosegrind), or hold {ADV} in the air first.",
    "Back trucks: land on a rail with the nub DOWN\n(5-0), or hold {ADV} in the air first.",
};
static const char *const TIPS_101[] = {
    "In the air hold {GL}, then flick the nub UP\n(a nosebone). Other flicks give other grabs.",
};
static const char *const TIPS_102[] = {
    "Chain two tricks without letting the board\nsettle: ollie, flip, grind, grab ...",
    "Keep the line going: every trick raises the\nmultiplier. Bank it by rolling for a moment.",
};
#define NT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const TutorialText TEXT[] = {
    { 93, "The basics: the ollie, then the flips you flick on the nub.", TIPS_93, NT(TIPS_93) },
    { 94, "Get rolling, then time an ollie up onto a ledge.", TIPS_94, NT(TIPS_94) },
    { 95, "Big air from the top of a ramp, grabs and spins.", TIPS_95, NT(TIPS_95) },
    { 96, "Line up a rail or a ledge and ollie onto it.", TIPS_96, NT(TIPS_96) },
    { 97, "The nollie: popping off the nose.", TIPS_97, NT(TIPS_97) },
    { 98, "Land a flip trick in a manual, then flip out of it.", TIPS_98, NT(TIPS_98) },
    { 99, "The advanced flips: varial, 360 flip and the nollie versions.", TIPS_99, NT(TIPS_99) },
    { 100, "Advanced grinds on the front and the back trucks.", TIPS_100, NT(TIPS_100) },
    { 101, "The grabs you get by flicking while a grab is held.", TIPS_101, NT(TIPS_101) },
    { 102, "Chain tricks into a line and build the multiplier.", TIPS_102, NT(TIPS_102) },
};
#define NTEXT ((int)(sizeof(TEXT) / sizeof(TEXT[0])))

static const TutorialText *find(int chal)
{
    for (int i = 0; i < NTEXT; i++)
        if (TEXT[i].chal == chal) return &TEXT[i];
    return NULL;
}

int tutorial_list(const Career *c, int *out, int max)
{
    int n = 0;
    for (uint32_t i = 0; i < c->nchal && n < max; i++)
        if (c->chal[i].mode == MODE_TUTORIAL || c->chal[i].mode == MODE_TUTORIAL_ADV)
            out[n++] = (int)i;
    return n;
}

const char *tutorial_summary(int chal)
{
    const TutorialText *t = find(chal);
    return t ? t->summary : "";
}

const char *tutorial_tip(int chal, int obj)
{
    const TutorialText *t = find(chal);
    if (!t || obj < 0 || obj >= t->ntips) return NULL;
    return t->tip[obj];
}

int tutorial_current_objective(const int *done, int nobj)
{
    for (int i = 0; i < nobj; i++)
        if (!done[i]) return i;
    return -1;
}
