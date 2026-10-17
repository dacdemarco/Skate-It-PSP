#include "story.h"
#include <stdio.h>
#include <string.h>

static int shown(const Profile *p, int bit) { return progress_seen(p, bit); }

int story_next(const Profile *p, const Career *c, StoryItem *it)
{
    if (!c->nscenes || !c->story) return 0;
    int cur = progress_pool(p, c);
    for (int i = 0; i < (int)c->npools; i++) {
        const CareerPool *pl = &c->pool[i];
        int s = c->pool_scene[i].start;
        if (s && p->pools_started > i && !shown(p, s)) { *it = (StoryItem){ STORY_SCENE, s, -1, -1, s }; return 1; }
        for (int k = 0; k < pl->nchal; k++) {                       /* the win scenes of the pool's challenges that are done */
            int ch = pl->chal[k], w = c->story[ch].win;
            if (w && progress_chal_done(p, ch) && !shown(p, w)) { *it = (StoryItem){ STORY_SCENE, w, -1, ch, w }; return 1; }
        }
        if (pl->sponsor >= 0 && p->sponsor[pl->sponsor] && !shown(p, SEEN_SIGNED + pl->sponsor)) {
            *it = (StoryItem){ STORY_SIGNED, 0, pl->sponsor, p->sponsor[pl->sponsor] - 1, SEEN_SIGNED + pl->sponsor };
            return 1;
        }
        int e = c->pool_scene[i].end;
        if (e && cur > i && !shown(p, e)) { *it = (StoryItem){ STORY_SCENE, e, -1, -1, e }; return 1; }
    }
    if (progress_finished(p, c)) {
        if (!shown(p, SEEN_ENDING)) { *it = (StoryItem){ STORY_ENDING, 0, -1, -1, SEEN_ENDING }; return 1; }
        if (!shown(p, SEEN_CREDITS)) { *it = (StoryItem){ STORY_CREDITS, 0, -1, -1, SEEN_CREDITS }; return 1; }
    }
    return 0;
}

void story_done(Profile *p, const StoryItem *it)
{
    progress_set_seen(p, it->bit);
}

int story_intro(const Profile *p, const Career *c, int chal)
{
    if (!c->story || chal < 0 || (uint32_t)chal >= c->nchal || progress_chal_done(p, chal)) return 0;
    int s = c->story[chal].intro;
    return s && !shown(p, s) ? s : 0;
}

void story_catch_up(Profile *p, const Career *c)
{
    StoryItem it;
    for (int guard = 0; guard < 200 && story_next(p, c, &it); guard++)
        story_done(p, &it);
}

/* "Name: text": a short name of letters, spaces and dots before the first colon */
const char *story_line_text(const char *line, char *speaker, size_t cap)
{
    if (cap) speaker[0] = 0;
    const char *colon = strchr(line, ':');
    if (!colon || colon - line > STORY_NAME_MAX - 1 || colon == line) return line;
    for (const char *q = line; q < colon; q++) {                  /* capitalised words only: "Rob Dyrdek", not "Wait for it" */
        int word_start = q == line || q[-1] == ' ';
        if (word_start ? !(*q >= 'A' && *q <= 'Z') : !((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || *q == ' ' || *q == '.')) return line;
    }
    if (cap && (size_t)(colon - line) < cap) {
        memcpy(speaker, line, (size_t)(colon - line));
        speaker[colon - line] = 0;
    }
    const char *t = colon + 1;
    while (*t == ' ') t++;
    return t;
}

int story_pages(const Career *c, int scene, StoryPage *out, int max)
{
    int n = 0, nl = career_scene_nlines(c, scene);
    char who[STORY_NAME_MAX] = "";
    for (int i = 0; i < nl && n < max; i++) {
        char sp[STORY_NAME_MAX];
        story_line_text(career_scene_line(c, scene, i), sp, sizeof(sp));
        int new_speaker = sp[0] != 0;
        if (new_speaker) snprintf(who, sizeof(who), "%s", sp);
        if (n > 0 && !new_speaker && out[n - 1].n < STORY_PAGE_LINES) {
            out[n - 1].n++;
            continue;
        }
        out[n].first = i; out[n].n = 1;
        snprintf(out[n].speaker, STORY_NAME_MAX, "%s", who);
        n++;
    }
    return n;
}
