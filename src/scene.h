/* The story scene player (no PSP dependencies, host-tested in tests/test_scene.c): what the original's NIS scenes show, from data/scenes.pak (tools/convert_scenes.py).
 * Every scene is one speaker filmed in close-up (docs/CAMPAIGN_FORMAT.md section 13): the guest (a career skater) stands on the start grid of the scene's challenge and plays a conversation
 * clip, the camera orbits him at a fixed offset. The scene's dialogue lines are shown as pages by the front end (src/story.c); this module says who stands there, which clip he plays, and where
 * the camera is for a given line.
 * What is the ROM's and what is ours: the guest, his place, the camera and the choice of clips by the span's link are the ROM's; which clip of a type plays when, the cross-fade between clips and
 * the facing of the guest (the numbers of the ROM put his front to -z while the camera stands at +z; `flip` turns him round) are ours. */
#ifndef SCENE_H
#define SCENE_H

#include <stddef.h>
#include <stdint.h>
#include "skel.h"
#include "levelobj.h"

#define SCENE_FPS 30.0f                 /* the conversation clips' frame rate (the animation database says 30) */
#define SCENE_CLIP_FADE 0.30f           /* seconds of cross-fade between two clips */

typedef struct { uint32_t nframes, frame_off, board_nframes, board_off; } SceneClipTab;
typedef struct { uint8_t type, sub, flags, pad; float face; } SceneClipInfo;
typedef struct { uint16_t frame; uint8_t skater, pad; } SceneSpawn;
typedef struct { uint16_t start, end; uint8_t link, pad[3]; } SceneTalk;
typedef struct { uint16_t start, end; } SceneSub;

typedef struct {
    const uint8_t *buf;
    size_t size;
    uint32_t nclips, nscenes;
    const SkelAnim *clips;              /* the conversation clips as an animation table (SkelAnim = {nframes, frame_off, board_nframes, board_off}) over buf */
    const SceneClipInfo *info;
    const float (*bone_off)[3];         /* the 22 rest offsets the clips were made for */
} ScenePak;

typedef struct {
    uint32_t dist;                      /* the camera distance in 1.19.12 metres (0x2000 = 2 m) */
    uint32_t length;                    /* frames */
    int nspawn, ntalk, nsub;
    const SceneSpawn *spawn;
    const SceneTalk *talk;
    const SceneSub *sub;
} SceneScript;

/* buf stays owned by the caller (4-byte aligned); returns 0 when it is not a version 1 scenes file or runs past its size */
int scenepak_load(ScenePak *p, const uint8_t *buf, size_t size);
/* the level table id the scene is filmed in (-1 = the scene names no place), and its script (scene 1 .. nscenes) */
int scenepak_level(const ScenePak *p, int scene);
int scenepak_script(const ScenePak *p, int scene, SceneScript *out);
/* the skeleton of a skater with the offsets the conversation clips were made for */
void scenepak_bones(const ScenePak *p, const SkelBone *skater_bones, SkelBone out[SKEL_NBONES]);

/* where the guest stands: the scene's challenge `slot` (-1 = none) names the start grid chalNNN_start_pos_k, else the level's start_pos_k; k = 2 (the guest is actor 1), or 1 when the grid
 * has only one point. Returns 0 when the level has no start grid. */
int scene_guest_place(const LevelObjs *o, int chal_slot, float pos[3]);

typedef struct {
    const ScenePak *pak;
    SceneScript sc;
    int scene;
    int frame;                          /* the script frame the page being shown starts at */
    int flip;                           /* 0: the guest faces the camera, 1: turned round */
    int link;                           /* the kind of clip asked for (the talk span's link) */
    int guest;                          /* the career skater index of the guest */
    int clip, from_clip, fresh;         /* the clip playing (-1 none) and the one being faded out; fresh = it has just been picked (the cross-fade starts) */
    float t;                            /* seconds into the clip */
    float fade;                         /* 0 .. 1: cross-fade from `from` to the clip */
    SkelFrame from;
    BoardFrame bfrom;
    uint32_t rng;
} ScenePlayer;

/* 0 for a scene without a guest (57: a wide shot with no speaker): the front end shows its text only */
int sceneplayer_active(const ScenePlayer *sp);
void sceneplayer_init(ScenePlayer *sp, const ScenePak *p, int scene, uint32_t seed);
/* show the line `line` of the scene: the guest and the kind of clip at its start; picks a new clip if either changed */
void sceneplayer_line(ScenePlayer *sp, int line, int nlines);
/* advance the clip clock */
void sceneplayer_update(ScenePlayer *sp, float dt);
/* the pose now (skeleton data frame; fixed, so draw it with scenepak_bones and the guest's own scale) and the board's */
void sceneplayer_pose(const ScenePlayer *sp, SkelFrame *out, BoardFrame *board);
/* the world matrix (column-major 4x4) of the guest standing at pos: turned about the up axis so that the way his clip faces points at the camera (flip: away from it) */
void sceneplayer_body(const ScenePlayer *sp, const float pos[3], float m[16]);
/* the camera: look-at = guest + (0, 1, 0); eye = look-at + (0, 1, distance) (orbit yaw 90 degrees, as the ROM's NIS camera) */
void sceneplayer_camera(const ScenePlayer *sp, const float pos[3], float eye[3], float look[3]);
/* the number of clips of a kind (type), and the id of the n-th of them */
int scenepak_type_count(const ScenePak *p, int type);
int scenepak_type_clip(const ScenePak *p, int type, int n);

#endif
