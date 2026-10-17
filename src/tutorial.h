/* The tutorials (no PSP dependencies - host-tested in tests/test_tutorial.c). The ten tutorial challenges of the original (career challenges 93-102 at the Community Center, modes Tutorial /
 * Tutorial advanced) are ordinary goals (src/goals.c); what this adds is the instruction text. The original's text describes the stylus gestures of the DS (TXT_TUTORIAL_nnn_NIS_mmm), so
 * the PSP gets its own: a short summary per tutorial and a tip per objective, written for this port's controls. The tips are templates: {POP} {ADV} {PUSH} {BRAKE} {GL} {GR} are replaced by
 * the buttons of the mapping in use (settings_expand_buttons), "\n" starts a second line. */
#ifndef TUTORIAL_H
#define TUTORIAL_H

#include "career.h"

#define TUT_MAX 12

/* the career challenge indices of the tutorials (modes Tutorial / Tutorial advanced), in the original's order; returns how many */
int tutorial_list(const Career *c, int *out, int max);
/* a PSP-adapted description of the tutorial (challenge index); "" if there is none */
const char *tutorial_summary(int chal);
/* the tip template for objective `obj` (0-based, in the challenge's own order) of the tutorial `chal`; NULL if there is none */
const char *tutorial_tip(int chal, int obj);
/* the index of the first objective that is not done (done[] has nobj entries): the one whose tip is shown; -1 when all are done */
int tutorial_current_objective(const int *done, int nobj);

#endif
