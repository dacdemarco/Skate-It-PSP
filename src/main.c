/* Skate It PSP - the PSP side of the game: GU rendering, input, the front end -> play session -> front end loop, challenges, audio hook-up.
 * Loads data/<level>.lvl (tools/convert_level.py), data/skater_<name>.skt (tools/convert_skater.py) and data/board.brd
 * (tools/convert_board.py). The skater rides the board on the level's collision ground; poses come from the original DS pose library.
 *
 * Select cycles three modes:  PLAY (follow camera)   FREE (fly camera)   POSE (fly camera + pose browser)
 *
 *   PLAY (the buttons below are the defaults; Options > Controls remaps them, Options > Trick guide lists every trick on the mapping in use - the scheme itself is in src/control.h):
 *          nub  steer (the D-pad moves the camera: left / right orbit, up / down distance)      Square  push: tap = quick, hold = strong and slower      Circle  brake
 *          Cross  hold to crouch, release to ollie.  A FLICK of the nub (a quick snap to the rim) while it is held - or just before it is pressed - sets a flip trick up that starts with the pop:
 *                 up-left kickflip, up-right heelflip, left pop shuvit, right FS pop shuvit, down-left 360 pop shuvit, down-right FS 360 pop shuvit.  A diagonal held out at the release counts too.
 *          Triangle  the ADVANCED pop: the same with varial kickflip / varial heelflip / hardflip / inward heel / 360 flip / laser flip; both pops held: 360 hardflip / 360 inward heel / frontflip (up) / backflip (down)
 *          NOLLIE: hold the nub forward while crouching (the HUD says NOLLIE), then flick the trick (or just let go): the nollie version. A flick straight down goes back to the tail.
 *          In the air: a flick = a LATE flip (kickflip, heelflip, pop shuvit, FS pop shuvit, backfoot kickflip / heelflip); Square pressed in the air and held: the nub left / right = SPIN (BS left,
 *                 FS right; 180s, he finishes the half turn; landing a half turn is fakie, sideways is a bail), flick up / down = frontflip / backflip.
 *          Nub pulled DOWN (or UP) and held for a moment while rolling = manual (nose manual); then the nub balances it (forwards / back); a pop gets out of it (flip tricks too).
 *          Circle held: a flick left / right = REVERT (a half turn on the ground, then fakie); the nub held hard over at speed = POWERSLIDE.
 *          L / R  in the air: GRAB. L = the back hand (BS grab, FS grab with the nub right, tail grab up, stale down), R = the front hand (melon, nose grab up, mute down), both = double grab; a flick
 *                 while it is held = the variant (nosebone / tailbone / tuck knee, shifty, rocket, method, tweaks, coffin ...)
 *          GRINDS need no button: the board locks on to any rail or ledge it comes down on; the nub at that moment picks the grind (neutral 50-50, up nosegrind, down 5-0, left boardslide, right
 *                 lipslide, diagonals noseslide / tailslide / blunts), Triangle held in the air: smith / feeble / crooked / overcrooks / willy / overwilly / salad; BS or FS follows the side of the rail.
 *                 A flick while grinding switches to that grind. In the air along a tall wall at an angle, at speed: WALLRIDE, by itself too (Cross let go kicks off).
 *          On a rail or ledge there is no balancing: the board rides it to the end (or until too slow). A TAP of a pop button hops up and comes back down on the rail; held 0.25 s and let go it
 *                 jumps OFF (towards the nub, else away from a ledge's drop). Manuals still balance with the nub (the bar at the bottom).
 *          Options > Gravity: gravity, ollie power and push speed in percent (src/skate.h SkateTune).
 *          L + Select = HUD detail lines on / off (on the ground)     R + Select = music on / off     Start = the pause menu
 *   FREE:  Analog stick look   D-pad move/strafe   L / R down / up   Square 4x speed   Circle 0.25x speed
 *   POSE:  as FREE, plus Cross next pose / Triangle previous pose (hold Square: +-10)
 *   CHALLENGES (data/career.dat, the original's career; Career mode only): a green beam stands over the start of each challenge of the level (gold once done). Roll into one and choose START CHALLENGE in the pause menu: the rider is put on
 *          the start grid, 3 - 2 - 1 - GO, then the objectives on the right are to be done (a Photo challenge in one line; Film ones within the time; Own The Spot: beat the score on the spot).
 *          After COMPLETE / FAILED, Cross goes back to skating. Done challenges are kept in data/career.sav.
 *   START = the pause menu (resume, respawn, start / restart / leave a challenge, quit to the menu).
 *   HOME exits.
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psppower.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <malloc.h>

#include "skel.h"
#include "skate.h"
#include "rider.h"
#include "rails.h"
#include "trick.h"
#include "score.h"
#include "control.h"
#include "sfx.h"
#include "sparks.h"
#include "audio.h"
#include "coll.h"
#include "nearclip.h"
#include "levelobj.h"
#include "career.h"
#include "goals.h"
#include "ghost.h"
#include "stage.h"
#include "tutorial.h"
#include "progress.h"
#include "story.h"
#include "scene.h"
#include "stats.h"
#include "custom.h"
#include "ui.h"
#include "frontend.h"
#include "hud.h"
#include "settings.h"
#include "vx.h"

PSP_MODULE_INFO("SkateItPSP", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

#define BUF_WIDTH 512
#define SCR_W 480
#define SCR_H 272
#define LEVEL_FILE level_file                /* data/<name>.lvl: picked at start-up by pick_level() */
#define SKATER_FILE skater_file             /* data/skater_<key>.skt: picked in the front end */
#define BOARD_FILE "data/board.brd"
#define TRICKS_FILE "data/tricks.cfg"
#define SOUNDS_FILE "data/sounds.snd"
#define MUSIC_ENABLED 0                      /* background music (src/audio.c, data/music*.mus) is switched off for now: it keeps ~5 MB of tracks in RAM */
#define POSE_FPS 10.0f
#define PI_F 3.14159265f
#define FOV_Y_DEG 65.0f
#define NEAR_PLANE 0.6f
#define FOCAL_PX (136.0f / 0.63704518f)      /* 136 / tan(FOV_Y/2) */
#define NC_MAX 700
#define NC_RADIUS 16.0f
#define COLL_CELL 4.0f                       /* collision grid cell (m): 4 m scans ~2x fewer triangle records per query than 8 m */
#define FALL_RESPAWN_DROP 2.5f               /* metres below the lowest visible geometry before the rider is put back where he last rolled on flat ground */

/* ---- file formats (see tools/convert_*.py) --------------------------------------------------- */
typedef struct {
    char magic[4];
    uint32_t version, ntex, nbatch, nverts, ncv, ncp;
    uint32_t off_tex, off_batch, off_verts, off_collv, off_collp;
    float bounds[6];
    float start[3];
    uint32_t nrails, nrailpts, off_rails;      /* version 2: rail table {first point, npoints | kind << 16 | material << 24} * nrails, then the points (3 floats each) */
    uint32_t off_polymat;                      /* version 3: one material byte per collision polygon (tools/materials.py) */
    uint32_t off_railids, off_objs, off_paths, pad;   /* version 4: the original game's grind line number per rail (u16), the named objects, the non-grind paths (tools/convert_level.py) */
} LvlHeader;

typedef struct {
    uint16_t w, h;
    uint8_t bpp, swz, alpha, pad;
    uint32_t clut_off, pix_off, n, pad2;
} TexEntry;

typedef struct {
    uint16_t tex, flags;
    uint32_t first, count, pad;
} Batch;

typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} Vertex;

typedef struct {
    char magic[4];
    uint32_t version, nverts, nbones, nanims;
    uint32_t off_tex, off_verts, off_vbone, off_skel, off_anims;
    float scale, height;
    uint32_t pad[12];
} SktHeader;

typedef struct {
    char magic[4];
    uint32_t version, ntex, nverts, off_tex, off_batch, off_verts;
    float scale;
    uint32_t pad[8];
} BrdHeader;

_Static_assert(sizeof(LvlHeader) == 116, "LvlHeader");
_Static_assert(sizeof(TexEntry) == 24, "TexEntry");
_Static_assert(sizeof(Batch) == 16, "Batch");
_Static_assert(sizeof(Vertex) == 24, "Vertex");
_Static_assert(sizeof(CollPoly) == 12, "CollPoly");
_Static_assert(sizeof(SktHeader) == 96, "SktHeader");
_Static_assert(sizeof(BrdHeader) == 64, "BrdHeader");
_Static_assert(sizeof(SkelBone) == 16, "SkelBone");
_Static_assert(sizeof(SkelFrame) == 364, "SkelFrame");
_Static_assert(sizeof(BoardFrame) == 28, "BoardFrame");
_Static_assert(sizeof(SkelAnim) == 16, "SkelAnim");

typedef struct {
    uint8_t *file;
    size_t size;
    const LvlHeader *hdr;
    const TexEntry *tex;
    const Batch *batch;
    const Vertex *verts;
    const float *collv;        /* ncv * 3 floats */
    const CollPoly *collp;
    const uint8_t *polymat;    /* material of each collision polygon: bits 0-1 concrete / metal / wood, bits 2-3 medium / rough / smooth roll */
    Coll coll;
    Rails rails;               /* grindable rails (src/rails.h) */
    NcGrid nc;                 /* near-camera software clipping, see nearclip.h */
    LevelObjs objs;            /* the original game's named objects: start grid, bounds, challenge markers (src/levelobj.h) */
} Level;

typedef struct {
    uint8_t *file;
    size_t size;
    const SktHeader *hdr;
    const TexEntry *tex;
    const Vertex *verts;       /* bone-local bind data (triangle list) */
    const uint8_t *vbone;
    const SkelBone *bones;
    AnimSet anims;
    Vertex *skinned;           /* per-frame output, written by CPU skinning */
} Skater;

typedef struct {
    uint8_t *file;
    size_t size;
    const BrdHeader *hdr;
    const TexEntry *tex;
    const Batch *batch;
    const Vertex *verts;
} Board;

/* ---- system callbacks ------------------------------------------------------------------------ */
static int exit_callback(int a1, int a2, void *common)
{
    (void)a1; (void)a2; (void)common;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("exit_callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

static void fatal(const char *msg, const char *detail)
{
    pspDebugScreenInit();
    pspDebugScreenPrintf("Skate It PSP\n\nERROR: %s\n%s\n\nPress HOME to exit.\n", msg, detail ? detail : "");
    sceKernelSleepThread();
}

/* ---- what the front end picked --------------------------------------------------------------------- */
static char level_file[96] = "data/amphitheatre.lvl";
static char skater_file[96] = "data/skater_aaron.skt";

/* ---- file loading ---------------------------------------------------------------------------- */
static uint8_t *load_file(const char *base, const char *name, size_t *size_out)
{
    char path[320];
    snprintf(path, sizeof(path), "%s%s", base, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        fatal("cannot open file", path);
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)memalign(64, (size + 63) & ~(size_t)63);
    if (!buf)
        fatal("out of memory loading", path);
    if (fread(buf, 1, size, f) != size)
        fatal("short read on", path);
    fclose(f);
    *size_out = size;
    return buf;
}

/* a file that may be missing (the career data, the save): NULL if it cannot be read */
static uint8_t *load_file_opt(const char *base, const char *name, size_t *size_out)
{
    char path[320];
    snprintf(path, sizeof(path), "%s%s", base, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = size > 0 ? (uint8_t *)memalign(16, ((size_t)size + 15) & ~(size_t)15) : NULL;
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size_out = (size_t)size;
    return buf;
}

#define SAVE_NCHAL PROFILE_NDONE            /* done flags per profile (the original's challenge table has 103 entries) */

/* the settings (src/settings.c): button mapping, the information lines and the physics tuning; written whenever the options screens change something */
#define SETTINGS_FILE "data/settings.sav"
static Settings g_set;

/* the physics reads the tuning (Options > Gravity) from skate_tune */
static void apply_tune(void)
{
    skate_tune.gravity = settings_tune_scale(&g_set, TUNE_GRAVITY);
    skate_tune.ollie = settings_tune_scale(&g_set, TUNE_OLLIE);
    skate_tune.push = settings_tune_scale(&g_set, TUNE_PUSH);
}

static void apply_audio(void);

static void save_settings(const char *base)
{
    uint8_t buf[64];
    apply_tune();
    apply_audio();
    size_t n = settings_save(&g_set, buf, sizeof(buf));
    char path[320];
    snprintf(path, sizeof(path), "%s%s", base, SETTINGS_FILE);
    FILE *f = n ? fopen(path, "wb") : NULL;
    if (!f)
        return;
    fwrite(buf, 1, n, f);
    fclose(f);
}

static void load_settings(const char *base)
{
    settings_defaults(&g_set);
    size_t n = 0;
    uint8_t *b = load_file_opt(base, SETTINGS_FILE, &n);
    if (!b)
        return;
    if (!settings_load(&g_set, b, n))
        settings_defaults(&g_set);
    free(b);
    apply_tune();
    apply_audio();
}

/* the PSP buttons that can be mapped, as a mask of PBM(PB_*) */
static unsigned pad_phys(unsigned b)
{
    return ((b & PSP_CTRL_CROSS) ? PBM(PB_CROSS) : 0) | ((b & PSP_CTRL_SQUARE) ? PBM(PB_SQUARE) : 0) | ((b & PSP_CTRL_CIRCLE) ? PBM(PB_CIRCLE) : 0) |
           ((b & PSP_CTRL_TRIANGLE) ? PBM(PB_TRIANGLE) : 0) | ((b & PSP_CTRL_LTRIGGER) ? PBM(PB_L) : 0) | ((b & PSP_CTRL_RTRIGGER) ? PBM(PB_R) : 0);
}

static unsigned fe_buttons(unsigned b, float ax, float ay);

static void load_level(Level *L, const char *base)
{
    L->file = load_file(base, LEVEL_FILE, &L->size);
    L->hdr = (const LvlHeader *)L->file;
    if (memcmp(L->hdr->magic, "SKLV", 4) != 0 || L->hdr->version != 4)
        fatal("bad level file (magic/version)", LEVEL_FILE);
    L->tex = (const TexEntry *)(L->file + L->hdr->off_tex);
    L->batch = (const Batch *)(L->file + L->hdr->off_batch);
    L->verts = (const Vertex *)(L->file + L->hdr->off_verts);
    L->collv = (const float *)(L->file + L->hdr->off_collv);
    L->collp = (const CollPoly *)(L->file + L->hdr->off_collp);
    L->polymat = L->file + L->hdr->off_polymat;
    levelobj_init(&L->objs, L->file, L->size, L->hdr->off_objs);
    levelobj_init_paths(&L->objs, L->file, L->size, L->hdr->off_paths);              /* the progress spline of the race levels */
    if (!rails_build(&L->rails, (const uint32_t *)(L->file + L->hdr->off_rails), L->hdr->nrails, (const float *)(L->file + L->hdr->off_rails + 8 * L->hdr->nrails)))
        fatal("cannot build the rails", LEVEL_FILE);
    if (!coll_build(&L->coll, L->collv, L->hdr->ncv, L->collp, L->hdr->ncp, COLL_CELL))
        fatal("cannot build the collision grid", LEVEL_FILE);

    /* grid of the level triangles for the near-camera clipper (the far backdrop layer is skipped) */
    uint32_t nt = L->hdr->nverts / 3;
    uint16_t *tex = (uint16_t *)malloc(sizeof(uint16_t) * nt);
    uint8_t *skip = (uint8_t *)calloc(nt, 1);
    if (!tex || !skip)
        fatal("out of memory building the clip grid", NULL);
    for (uint32_t i = 0; i < L->hdr->nbatch; i++)
        for (uint32_t k = L->batch[i].first / 3; k < (L->batch[i].first + L->batch[i].count) / 3 && k < nt; k++) {
            tex[k] = L->batch[i].tex;
            skip[k] = (L->batch[i].flags & 1) != 0;
        }
    if (!nc_build(&L->nc, (const NcVertex *)L->verts, L->hdr->nverts, tex, skip, 16.0f))
        fatal("cannot build the clip grid", LEVEL_FILE);
    free(tex);
    free(skip);
}

/* the pointers into a skater file that is in S->file (a file of the pro skaters, or a custom skater built in memory) */
static void skater_attach(Skater *S, const char *file)
{
    S->hdr = (const SktHeader *)S->file;
    if (memcmp(S->hdr->magic, "SKS2", 4) != 0 || S->hdr->version != 2 || S->hdr->nbones != SKEL_NBONES)
        fatal("bad skater file (magic/version)", file);
    S->tex = (const TexEntry *)(S->file + S->hdr->off_tex);
    S->verts = (const Vertex *)(S->file + S->hdr->off_verts);
    S->vbone = S->file + S->hdr->off_vbone;
    S->bones = (const SkelBone *)(S->file + S->hdr->off_skel);
    S->anims.file = S->file;
    S->anims.anims = (const SkelAnim *)(S->file + S->hdr->off_anims);
    S->anims.nanims = S->hdr->nanims;
    S->skinned = (Vertex *)memalign(64, (S->hdr->nverts * sizeof(Vertex) + 63) & ~(size_t)63);
    if (!S->skinned)
        fatal("out of memory for skinned vertices", NULL);
    memcpy(S->skinned, S->verts, S->hdr->nverts * sizeof(Vertex));
}

static void load_skater_file(Skater *S, const char *base, const char *file)
{
    S->file = load_file(base, file, &S->size);
    skater_attach(S, file);
}

/* the custom skater (src/custom.c): the parts are data/custom.pak (1.7 MB, loaded while they are needed: the Customize screen keeps them, a build for a game or a viewer frees them again), the
 * skeleton and the animations those of Aaron's file */
static uint8_t *g_cpak_buf;
static CustomPak g_cpak;
static int g_keep_cpak;                      /* the Customize screen is open: the parts stay */
static int g_skater_custom;                  /* the rider of the next game is the custom skater ... */
static CustomSel g_custom_sel;               /* ... made of these parts */

static uint8_t *load_file_opt(const char *base, const char *name, size_t *size_out);

static int cpak_acquire(const char *base)
{
    if (g_cpak_buf) return 1;
    size_t n = 0;
    g_cpak_buf = load_file_opt(base, "data/custom.pak", &n);
    if (g_cpak_buf && !custom_pak_load(&g_cpak, g_cpak_buf, n)) { free(g_cpak_buf); g_cpak_buf = NULL; }
    return g_cpak_buf != NULL;
}

static void cpak_release(void)
{
    free(g_cpak_buf);
    g_cpak_buf = NULL;
}

static int load_custom_skater(Skater *S, const char *base, const CustomSel *sel)
{
    int had = g_cpak_buf != NULL, ok = 0;
    if (!cpak_acquire(base)) return 0;
    size_t bn = 0;
    uint8_t *bb = load_file_opt(base, "data/skater_aaron.skt", &bn);
    if (bb) {
        size_t n = 0;
        uint8_t *f = custom_build(&g_cpak, sel, bb, bn, &n);
        free(bb);
        if (f) {
            uint8_t *a = (uint8_t *)memalign(64, (n + 63) & ~(size_t)63);       /* (the GPU reads the texture: 64-byte aligned like every skater file) */
            if (a) { memcpy(a, f, n); S->file = a; S->size = n; skater_attach(S, "custom skater"); ok = 1; }
            free(f);
        }
    }
    if (!had && !g_keep_cpak) cpak_release();
    return ok;
}

static void load_skater(Skater *S, const char *base)
{
    if (g_skater_custom && load_custom_skater(S, base, &g_custom_sel))
        return;
    load_skater_file(S, base, g_skater_custom ? "data/skater_aaron.skt" : SKATER_FILE);       /* (Aaron stands in if the parts are missing) */
}

static void load_board(Board *B, const char *base)
{
    B->file = load_file(base, BOARD_FILE, &B->size);
    B->hdr = (const BrdHeader *)B->file;
    if (memcmp(B->hdr->magic, "SKBD", 4) != 0 || B->hdr->version != 1)
        fatal("bad board file (magic/version)", BOARD_FILE);
    B->tex = (const TexEntry *)(B->file + B->hdr->off_tex);
    B->batch = (const Batch *)(B->file + B->hdr->off_batch);
    B->verts = (const Vertex *)(B->file + B->hdr->off_verts);
    if (B->hdr->ntex < 4)
        fatal("board file has no shadow texture - rerun tools/convert_board.py", BOARD_FILE);
}

/* the custom skater's board: its deck and wheel art are painted into the board's texture (64 x 128 palette indices; the art is written without the PSP's swizzling, so the texture is switched to the plain layout) */
static void apply_board_art(Board *B, const char *base, const CustomSel *sel)
{
    static uint8_t pix[64 * 128];
    static uint16_t clut[256];
    int had = g_cpak_buf != NULL;
    if (!cpak_acquire(base)) return;
    if (custom_board_texture(&g_cpak, sel, pix, clut)) {
        TexEntry *t = (TexEntry *)&B->tex[0];
        if (t->w == 64 && t->h == 128 && t->bpp == 8) {
            memcpy((uint8_t *)B->file + t->pix_off, pix, sizeof(pix));
            memcpy((uint8_t *)B->file + t->clut_off, clut, sizeof(clut));
            t->swz = 0;
        }
    }
    if (!had && !g_keep_cpak) cpak_release();
}

static void unload_level(Level *L)
{
    nc_free(&L->nc);
    coll_free(&L->coll);
    rails_free(&L->rails);
    free(L->file);
    memset(L, 0, sizeof(*L));
}

static void unload_skater(Skater *S)
{
    free(S->skinned);
    free(S->file);
    memset(S, 0, sizeof(*S));
}

static void unload_board(Board *B)
{
    free(B->file);
    memset(B, 0, sizeof(*B));
}

/* CPU skinning: every vertex belongs to one bone; M = 3x4 row-major (translation last). */
static void skin_skater(Skater *S, float (*M)[12])
{
    for (uint32_t i = 0; i < S->hdr->nverts; i++) {
        const Vertex *s = &S->verts[i];
        const float *m = M[S->vbone[i]];
        Vertex *d = &S->skinned[i];
        d->x = m[0] * s->x + m[1] * s->y + m[2] * s->z + m[3];
        d->y = m[4] * s->x + m[5] * s->y + m[6] * s->z + m[7];
        d->z = m[8] * s->x + m[9] * s->y + m[10] * s->z + m[11];
    }
    sceKernelDcacheWritebackRange(S->skinned, S->hdr->nverts * sizeof(Vertex));
}

/* ---- small matrix helpers (column-major 4x4, as sceGumLoadMatrix expects) --------------------- */
static void mat4_mul(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1] +
                             a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
    memcpy(out, r, sizeof(r));
}

static void mat4_from_34(float m[16], const float r[12])
{
    m[0] = r[0]; m[1] = r[4]; m[2]  = r[8];  m[3]  = 0.0f;
    m[4] = r[1]; m[5] = r[5]; m[6]  = r[9];  m[7]  = 0.0f;
    m[8] = r[2]; m[9] = r[6]; m[10] = r[10]; m[11] = 0.0f;
    m[12] = r[3]; m[13] = r[7]; m[14] = r[11]; m[15] = 1.0f;
}

/* ---- collision callbacks for the physics ----------------------------------------------------- */
static int world_ground(void *ctx, float x, float z, float y_max, float *y)
{
    return coll_ground(&((Level *)ctx)->coll, x, z, y_max, y);
}

static int world_wall(void *ctx, const float a[3], const float b[3], float hit[3], float normal[3])
{
    return coll_wall(&((Level *)ctx)->coll, a, b, hit, normal);
}

static int world_surface(void *ctx, const float a[3], const float b[3], float hit[3], float normal[3])
{
    return coll_ray(&((Level *)ctx)->coll, a, b, COLL_RIDE_NY, 2.0f, 0.06f, hit, normal);     /* 6 cm: bridges the cracks between polygons */
}

/* ---- graphics -------------------------------------------------------------------------------- */
static unsigned int __attribute__((aligned(16))) display_list[65536];
static float __attribute__((aligned(16))) gum_matrix[16];

static void gfx_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void *)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);              /* DS winding is unknown/mixed */
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);     /* colour-keyed textures: transparent palette entries */
    sceGuEnable(GU_ALPHA_TEST);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void bind_tex(const uint8_t *base, const TexEntry *t, int linear)
{
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(t->bpp == 4 ? GU_PSM_T4 : GU_PSM_T8, 0, 0, t->swz);
    sceGuClutMode(GU_PSM_5551, 0, 0xff, 0);
    sceGuClutLoad(t->n / 8, base + t->clut_off);
    sceGuTexImage(0, t->w, t->h, t->w, base + t->pix_off);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
}

#define VTX_FMT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static void draw_level(const Level *L, int linear)
{
    int cur = -2;
    for (uint32_t i = 0; i < L->hdr->nbatch; i++) {
        const Batch *b = &L->batch[i];
        if (b->tex != cur) {
            if (b->tex == 0xFFFF)
                sceGuDisable(GU_TEXTURE_2D);
            else
                bind_tex(L->file, &L->tex[b->tex], linear);
            cur = b->tex;
        }
        sceGuDrawArray(GU_TRIANGLES, VTX_FMT, (int)b->count, 0, (const void *)(L->verts + b->first));
    }
}

static void draw_skater(const Skater *S, int linear)
{
    bind_tex(S->file, S->tex, linear);
    sceGuDrawArray(GU_TRIANGLES, VTX_FMT, (int)S->hdr->nverts, 0, (const void *)S->skinned);
}

static void draw_board(const Board *B, int linear)
{
    for (uint32_t i = 0; i < 3; i++) {
        const Batch *b = &B->batch[i];
        bind_tex(B->file, &B->tex[b->tex], linear);
        sceGuDrawArray(GU_TRIANGLES, VTX_FMT, (int)b->count, 0, (const void *)(B->verts + b->first));
    }
}

/* Blob shadow: the original boardshadow.dst (board texture #3) on a 3x3-vertex mesh that follows the collision ground under the
 * board, aligned with the heading. It fades and grows with the height above the ground. Drawn with the identity model matrix. */
#define SHADOW_TEX 3
#define SHADOW_LEN 0.85f          /* metres along the heading */
#define SHADOW_WID 0.50f          /* metres across */
#define SHADOW_ALPHA 0.6f
#define SHADOW_LIFT 0.04f         /* above the collision floor, so it wins the depth test against the visible floor */

static void draw_shadow(const Board *B, const Level *L, const Skate *s)
{
    float cg;
    if (!coll_ground(&L->coll, s->pos[0], s->pos[2], s->pos[1] + 0.35f, &cg))
        return;
    float h = s->pos[1] - cg;
    if (h < 0.0f) h = 0.0f;
    float alpha = SHADOW_ALPHA / (1.0f + 1.5f * h);
    float grow = 1.0f + 0.3f * h;
    if (alpha < 0.03f)
        return;
    float fx = sinf(s->heading), fz = cosf(s->heading);       /* forward */
    float lx = fz, lz = -fx;                                  /* left */
    Vertex *v = (Vertex *)sceGuGetMemory(24 * sizeof(Vertex));
    float gy[3][3];
    for (int i = 0; i < 3; i++)                               /* i: along the heading, j: across */
        for (int j = 0; j < 3; j++) {
            float a = (i - 1) * 0.5f * SHADOW_LEN * grow, b = (j - 1) * 0.5f * SHADOW_WID * grow;
            float x = s->pos[0] + fx * a + lx * b, z = s->pos[2] + fz * a + lz * b, y;
            if (!coll_ground(&L->coll, x, z, cg + 0.4f, &y) || y < cg - 0.4f)
                y = cg;
            gy[i][j] = y;
        }
    int n = 0;
    uint32_t col = ((uint32_t)(alpha * 255.0f) << 24) | 0x00FFFFFF;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            static const int ci[6] = { 0, 1, 0, 1, 1, 0 }, cj[6] = { 0, 0, 1, 0, 1, 1 };
            for (int k = 0; k < 6; k++) {
                int pi = i + ci[k], pj = j + cj[k];
                float a = (pi - 1) * 0.5f * SHADOW_LEN * grow, b = (pj - 1) * 0.5f * SHADOW_WID * grow;
                Vertex *o = &v[n++];
                o->u = (float)pj * 0.5f; o->v = (float)pi * 0.5f;
                o->color = col;
                o->x = s->pos[0] + fx * a + lx * b;
                o->y = gy[pi][pj] + SHADOW_LIFT;
                o->z = s->pos[2] + fz * a + lz * b;
            }
        }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    bind_tex(B->file, &B->tex[SHADOW_TEX], 1);                /* linear: soft edge (the colour-key texel is transparent black) */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDepthMask(GU_TRUE);                                  /* no depth writes: a translucent decal */
    sceGuDrawArray(GU_TRIANGLES, VTX_FMT, n, 0, v);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
}

/* sparks: short bright streaks along their motion (two lines a hair apart, so they read on a 480x272 screen), added onto the picture */
typedef struct {
    uint32_t color;
    float x, y, z;
} SparkVertex;

static void draw_sparks(const Sparks *sp)
{
    if (sp->n == 0)
        return;
    SparkVertex *v = (SparkVertex *)sceGuGetMemory((int)(sp->n * 4 * sizeof(SparkVertex)));
    int n = 0;
    for (int i = 0; i < sp->n; i++) {
        const Spark *k = &sp->s[i];
        uint32_t c = spark_color(k);
        float tx = k->p[0] - k->v[0] * 0.04f, ty = k->p[1] - k->v[1] * 0.04f, tz = k->p[2] - k->v[2] * 0.04f;
        for (int j = 0; j < 2; j++) {
            float o = j * 0.012f;
            v[n].color = c; v[n].x = k->p[0]; v[n].y = k->p[1] + o; v[n].z = k->p[2]; n++;
            v[n].color = c & 0x00FFFFFF; v[n].x = tx; v[n].y = ty + o; v[n].z = tz; n++;       /* the tail fades out */
        }
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF);          /* additive: src * alpha + dst */
    sceGuDepthMask(GU_TRUE);
    sceGuDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
}

/* the challenge beams: a translucent column of light over the start of each challenge (the original's green beams), added onto the picture */
static void draw_beams(const float (*pos)[3], const uint32_t *col, int n, float t)
{
    enum { SEG = 8 };
    if (n <= 0)
        return;
    SparkVertex *v = (SparkVertex *)sceGuGetMemory((int)((size_t)n * SEG * 6 * sizeof(SparkVertex)));
    int k = 0;
    for (int b = 0; b < n; b++) {
        float r = 0.8f + 0.12f * sinf(t * 3.0f + (float)b), h = 7.0f;
        uint32_t bot = (col[b] & 0x00FFFFFFu) | 0x80000000u, top = col[b] & 0x00FFFFFFu;
        for (int i = 0; i < SEG; i++) {
            float a0 = (float)i * (2.0f * PI_F / SEG) + t * 0.5f, a1 = (float)(i + 1) * (2.0f * PI_F / SEG) + t * 0.5f;
            float x0 = pos[b][0] + cosf(a0) * r, z0 = pos[b][2] + sinf(a0) * r, x1 = pos[b][0] + cosf(a1) * r, z1 = pos[b][2] + sinf(a1) * r, y0 = pos[b][1], y1 = pos[b][1] + h;
            v[k].color = bot; v[k].x = x0; v[k].y = y0; v[k].z = z0; k++;
            v[k].color = bot; v[k].x = x1; v[k].y = y0; v[k].z = z1; k++;
            v[k].color = top; v[k].x = x1; v[k].y = y1; v[k].z = z1; k++;
            v[k].color = bot; v[k].x = x0; v[k].y = y0; v[k].z = z0; k++;
            v[k].color = top; v[k].x = x1; v[k].y = y1; v[k].z = z1; k++;
            v[k].color = top; v[k].x = x0; v[k].y = y1; v[k].z = z0; k++;
        }
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF);          /* additive: src * alpha + dst */
    sceGuDepthMask(GU_TRUE);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, k, 0, v);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
}

/* tiny 3x5 pixel font for the HUD (digits + a few letters), drawn as sprites */
typedef struct {
    uint32_t color;
    int16_t x, y, z;
} RectVertex;

static const char *glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    switch (c) {
    case 'B': return "110101110101110";
    case 'X': return "101101010101101";
    case 'W': return "101101111111101";
    case 'V': return "101101101101010";
    case 'J': return "001001001101111";
    case 'Z': return "111001010100111";
    case '+': return "000010111010000";
    case '-': return "000000111000000";
    case '.': return "000000000000010";
    case '0': case 'O': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001001001001";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    case 'F': return "111100110100100";
    case 'P': return "111101111100100";
    case 'S': return "111100111001111";
    case 'T': return "111010010010010";
    case 'R': return "110101110101101";
    case 'I': return "111010010010111";
    case 'A': return "010101111101101";
    case 'N': return "111101101101101";
    case 'M': return "101111111101101";
    case 'L': return "100100100100111";
    case 'Y': return "101101010010010";
    case 'E': return "111100110100111";
    case 'D': return "110101101101110";
    case 'C': return "111100100100111";
    case 'G': return "111100101101111";
    case 'U': return "101101101101111";
    case 'H': return "101101111101101";
    case 'K': return "101101110101101";
    case '/': return "001001010100100";
    case 'Q': return "111101101111001";
    case '!': return "010010010000010";
    case '?': return "111001010000010";
    case ':': return "000010000010000";
    case ',': return "000000000010100";
    case '\'': return "010010000000000";
    case '(': return "010100100100010";
    case ')': return "010001001001010";
    case '>': return "100010001010100";
    case '%': return "101001010100101";
    case '=': return "000111000111000";
    case '#': return "101111101111101";
    case '*': return "101010111010101";
    default:  return NULL;
    }
}

static void draw_text(int x, int y, int scale, const char *s, uint32_t color)
{
    int n = 0;
    for (const char *p = s; *p; p++) {
        const char *g = glyph(*p);
        if (g)
            for (int k = 0; k < 15; k++) n += (g[k] == '1');
    }
    if (n == 0)
        return;
    RectVertex *v = (RectVertex *)sceGuGetMemory((int)(n * 2 * sizeof(RectVertex)));
    int i = 0;
    for (const char *p = s; *p; p++, x += 4 * scale) {
        const char *g = glyph(*p);
        if (!g)
            continue;
        for (int k = 0; k < 15; k++) {
            if (g[k] != '1')
                continue;
            int px = x + (k % 3) * scale, py = y + (k / 3) * scale;
            v[i].color = color; v[i].x = (int16_t)px; v[i].y = (int16_t)py; v[i].z = 0; i++;
            v[i].color = color; v[i].x = (int16_t)(px + scale); v[i].y = (int16_t)(py + scale); v[i].z = 0; i++;
        }
    }
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, i, 0, v);
}

static void draw_rect(int x, int y, int w, int h, uint32_t color)
{
    RectVertex *v = (RectVertex *)sceGuGetMemory((int)(2 * sizeof(RectVertex)));
    v[0].color = color; v[0].x = (int16_t)x;       v[0].y = (int16_t)y;       v[0].z = 0;
    v[1].color = color; v[1].x = (int16_t)(x + w); v[1].y = (int16_t)(y + h); v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static void draw_text_right(int xr, int y, int scale, const char *s, uint32_t color)
{
    draw_text(xr - (int)strlen(s) * 4 * scale, y, scale, s, color);
}

/* ---- the user interface on the GPU (src/ui.h draws through these) ---------------------------------- */
typedef struct { float u, v; uint32_t color; float x, y, z; } UiVertex;
typedef struct { uint32_t color; float x, y, z; } UiRectVertex;

/* What the GPU was last told about the texture: text draws one quad per glyph (and the HUD outlines it eight times over), so the state is only sent when it changes. ui_gu_begin() forgets it. */
static const uint8_t *ui_c_pix, *ui_c_clut;
static int ui_c_fmt = -1, ui_c_tw, ui_c_th, ui_c_lin = -1;

static void ui_gu_quad(void *ctx, const UiTex *t, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, int linear)
{
    (void)ctx;
    sceGuEnable(GU_TEXTURE_2D);
    if (t->fmt != ui_c_fmt) {
        sceGuTexMode(t->fmt == 0 ? GU_PSM_T8 : GU_PSM_8888, 0, 0, 0);
        if (t->fmt == 0) sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        ui_c_fmt = t->fmt; ui_c_clut = NULL;
    }
    if (t->fmt == 0 && t->clut != ui_c_clut) {
        sceGuClutLoad(32, t->clut);
        ui_c_clut = t->clut;
    }
    if (t->pix != ui_c_pix || t->tw != ui_c_tw || t->th != ui_c_th) {
        sceGuTexImage(0, t->tw, t->th, t->tw, t->pix);
        ui_c_pix = t->pix; ui_c_tw = t->tw; ui_c_th = t->th;
    }
    if (linear != ui_c_lin) {
        sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
        ui_c_lin = linear;
    }
    UiVertex *v = (UiVertex *)sceGuGetMemory(2 * sizeof(UiVertex));
    /* GU_TRANSFORM_2D is the GPU's "through" mode: texture coordinates are in TEXELS there (0 .. 1 would sample only the first texel: every picture came out as one flat colour) */
    v[0].u = u0; v[0].v = v0; v[0].color = color; v[0].x = x0; v[0].y = y0; v[0].z = 0.0f;
    v[1].u = u1; v[1].v = v1; v[1].color = color; v[1].x = x1; v[1].y = y1; v[1].z = 0.0f;
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
}

static void ui_gu_rect(void *ctx, float x0, float y0, float x1, float y1, uint32_t color)
{
    (void)ctx;
    sceGuDisable(GU_TEXTURE_2D);
    UiRectVertex *v = (UiRectVertex *)sceGuGetMemory(2 * sizeof(UiRectVertex));
    v[0].color = color; v[0].x = x0; v[0].y = y0; v[0].z = 0.0f;
    v[1].color = color; v[1].x = x1; v[1].y = y1; v[1].z = 0.0f;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
}

static void ui_gu_begin(void)
{
    ui_c_pix = ui_c_clut = NULL; ui_c_fmt = -1; ui_c_lin = -1;
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

static void ui_gu_end(void)
{
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_TEXTURE_2D);
}

/* ---- camera ---------------------------------------------------------------------------------- */
static float wrap_angle(float a)
{
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

typedef struct {
    float yaw;                 /* direction the camera looks along, as a heading */
    float user;                /* manual orbit offset */
    float dist, height;
    float eye[3], look[3];
} FollowCam;

static void follow_cam_update(FollowCam *c, const Skate *s, const Level *L, float dt, int snap)
{
    float target = s->heading + c->user;
    if (snap)
        c->yaw = target;
    else
        c->yaw += wrap_angle(target - c->yaw) * (1.0f - expf(-3.2f * dt));
    float f[3] = { sinf(c->yaw), 0.0f, cosf(c->yaw) };
    c->eye[0] = s->pos[0] - f[0] * c->dist;
    c->eye[1] = s->pos[1] + c->height;
    c->eye[2] = s->pos[2] - f[2] * c->dist;
    float gy;
    if (coll_ground(&L->coll, c->eye[0], c->eye[2], c->eye[1] + 1.0f, &gy) && c->eye[1] < gy + 0.6f)
        c->eye[1] = gy + 0.6f;
    c->look[0] = s->pos[0]; c->look[1] = s->pos[1] + 1.0f; c->look[2] = s->pos[2];
}

static void set_view(const float view[16])
{
    memcpy(gum_matrix, view, sizeof(gum_matrix));
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
    sceGumMatrixMode(GU_MODEL);            /* callers load model matrices next */
}

/* triangles from nc_gather are in view space: draw them with identity view and model matrices */
static void draw_clipped(const Level *L, const NcTri *tris, int n, int linear)
{
    if (n <= 0)
        return;
    NcVertex *buf = (NcVertex *)sceGuGetMemory((int)((size_t)n * 3 * sizeof(NcVertex)));
    for (int i = 0; i < n; i++)
        memcpy(buf + i * 3, tris[i].v, 3 * sizeof(NcVertex));
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    int run = 0;
    for (int i = 1; i <= n; i++) {
        if (i < n && tris[i].tex == tris[run].tex)
            continue;
        if (tris[run].tex == 0xFFFF)
            sceGuDisable(GU_TEXTURE_2D);
        else
            bind_tex(L->file, &L->tex[tris[run].tex], linear);
        sceGuDrawArray(GU_TRIANGLES, VTX_FMT, (i - run) * 3, 0, (const void *)(buf + run * 3));
        run = i;
    }
}

/* ---- main ------------------------------------------------------------------------------------ */
enum { MODE_PLAY = 0, MODE_FREE = 1, MODE_POSE = 2, NMODES = 3 };

/* where the rider starts: the original game's free-skate grid slot 1 (start_pos_1, facing start_dir_1) when the level has one, else the converter's guess found by a ring search over the collision */
static void find_spawn(const Level *L, float *x, float *y, float *z, float *heading)
{
    *heading = 0.0f;
    float p[3], h;
    if (levelobj_start(&L->objs, 1, p, &h)) {
        float gy;
        if (coll_ground(&L->coll, p[0], p[2], p[1] + 1.0f, &gy)) {
            *x = p[0]; *y = gy; *z = p[2]; *heading = h;
            return;
        }
    }
    float sx = L->hdr->start[0], sz = L->hdr->start[2], sy = L->hdr->bounds[1];
    int found = 0;
    for (int ring = 0; ring < 40 && !found; ring++) {
        for (int k = 0; k < 8 && !found; k++) {
            float a = (float)k * 0.785398f, r = (float)ring * 2.0f;
            float tx = sx + cosf(a) * r, tz = sz + sinf(a) * r;
            if (coll_ground(&L->coll, tx, tz, L->hdr->bounds[4] + 5.0f, &sy)) {
                sx = tx; sz = tz; found = 1;
            }
        }
    }
    *x = sx; *y = sy; *z = sz;
}

/* the biggest block malloc can still hand out, in KB (binary search with real allocations, freed at once) */
static int heap_largest_free_kb(void)
{
    int lo = 0, hi = 65536;
    while (hi - lo > 16) {
        int mid = (lo + hi) / 2;
        void *p = malloc((size_t)mid * 1024u);
        if (p) { free(p); lo = mid; } else hi = mid;
    }
    return lo;
}

/* a float with one decimal, without float printf: -12.3 */
static void fmt1(char *out, size_t n, float v)
{
    int i = (int)(v < 0.0f ? v * 10.0f - 0.5f : v * 10.0f + 0.5f);
    int a = i < 0 ? -i : i;
    snprintf(out, n, "%s%d.%d", i < 0 ? "-" : "", a / 10, a % 10);
}

/* the sound and the front end are set up once; a game uses them */
static Mixer g_mixer;
static SoundBank g_sbank;
static Sfx g_sfx;
static int g_have_sound, g_have_music;

/* Options > Game settings: the sound effects volume is the mixer's master gain (0.7 at full), the music has its own */
static void apply_audio(void)
{
    g_mixer.master = 0.7f * settings_volume_scale(g_set.sfx_vol);
    if (g_have_music) audio_music_gain(&g_mixer, settings_volume_scale(g_set.music_vol));
}
static UiPak g_pak;
static Ui g_ui;
static Hud g_hud;
static Career g_career;
static Frontend g_fe;
static GhostPak g_ai;                       /* data/ai.pak: the original's recorded runs (opponents, ambient skaters) */
static int g_have_ai;
#define mixer g_mixer
#define sfx g_sfx
#define career g_career

/* the player's profile (src/progress.c): the career's progress; data/profile<slot>.sav, written to a .tmp file first. The sandbox's old data/career.sav (done flags only) is imported the first time. */
static Profile g_profile;
static int g_profile_slot = 0;

static void profile_path(const char *base, int slot, int tmp, char *out, size_t n)
{
    snprintf(out, n, "%sdata/profile%d.sav%s", base, slot, tmp ? ".tmp" : "");
}

static void save_profile(const char *base)
{
    uint8_t buf[1024];
    size_t n = profile_save(&g_profile, buf, sizeof(buf));
    char path[320], tmp[330];
    profile_path(base, g_profile_slot, 0, path, sizeof(path));
    profile_path(base, g_profile_slot, 1, tmp, sizeof(tmp));
    FILE *f = n ? fopen(tmp, "wb") : NULL;
    if (!f)
        return;
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    if (w == n) {
        sceIoRemove(path);                                      /* (a rename onto an existing file fails on the PSP) */
        sceIoRename(tmp, path);
    }
}

static int load_profile_file(const char *base, int tmp)
{
    char name[64];
    snprintf(name, sizeof(name), "data/profile%d.sav%s", g_profile_slot, tmp ? ".tmp" : "");
    size_t n = 0;
    uint8_t *b = load_file_opt(base, name, &n);
    if (!b)
        return 0;
    int ok = profile_load(&g_profile, &g_career, b, n);
    free(b);
    if (ok == 2)
        story_catch_up(&g_profile, &g_career);                  /* a file from before the story: what it has done already is not shown again */
    return ok;
}

static void load_profile(const char *base)
{
    int loaded = load_profile_file(base, 0);
    if (!loaded) loaded = load_profile_file(base, 1);          /* (the .tmp is what is left if the power went between the remove and the rename) */
    if (loaded) {
        if (loaded == 2) save_profile(base);                    /* (the old file is now a current one) */
        return;
    }
    size_t n = 0;
    uint8_t *old = load_file_opt(base, "data/career.sav", &n);
    int imported = old && profile_import_legacy(&g_profile, &g_career, old, n, "SKATER");
    free(old);
    if (!imported)
        profile_init(&g_profile, &g_career, "SKATER");
    else
        story_catch_up(&g_profile, &g_career);
    save_profile(base);
}


/* a career event was won: the profile records it (progress, unlocks, the sponsor), is saved, and msg gets what is new ("New skater: ...") */
static void career_win(const char *base, int chal, char msg[4][48], int *n_msg)
{
    Profile before = g_profile;
    ProgressEvent pev;
    progress_complete(&g_profile, &career, chal, &pev);
    save_profile(base);
    *n_msg = 0;
    for (uint32_t k = 0; k < career.nskaters && k < CAREER_NRIDERS && *n_msg < 3; k++)
        if (progress_unlocked(&g_profile, UNLOCK_SKATER, (int)k) && !progress_unlocked(&before, UNLOCK_SKATER, (int)k))
            snprintf(msg[(*n_msg)++], 48, "New skater: %.30s", career_str(&career, career.skater[k].name_off));
    int newev = 0;
    for (int k = 0; k < PROFILE_NDONE; k++) newev += progress_unlocked(&g_profile, UNLOCK_CHALLENGE, k) && !progress_unlocked(&before, UNLOCK_CHALLENGE, k);
    if (pev.finished && *n_msg < 4) snprintf(msg[(*n_msg)++], 48, "Career complete!");
    else if (newev > 0 && *n_msg < 4) snprintf(msg[(*n_msg)++], 48, newev == 1 ? "1 new event" : "%d new events", newev);
}

/* the profile slots of My Skate > Select Profile: what each saved file holds (for the list), and the requests the screen makes */
static void refresh_slots(const char *base)
{
    for (int i = 0; i < PROFILE_SLOTS; i++) {
        FeSlot *fs = &g_fe.slot[i];
        memset(fs, 0, sizeof(*fs));
        const Profile *pp = NULL;
        static Profile tmp;
        if (i == g_profile_slot) {
            pp = &g_profile;
        } else {
            char name[64];
            snprintf(name, sizeof(name), "data/profile%d.sav", i);
            size_t n = 0;
            uint8_t *b = load_file_opt(base, name, &n);
            if (b && profile_load(&tmp, &g_career, b, n)) pp = &tmp;
            free(b);
        }
        if (!pp) continue;
        fs->used = 1; fs->skater = pp->skater; fs->done = progress_total_done(pp); fs->play = pp->stat[STAT_PLAY_TIME];
        for (int k = 0; k < PROFILE_NSPONSOR; k++) fs->sponsors += pp->sponsor[k] != 0;
    }
    g_fe.cur_slot = g_profile_slot;
}

/* a new profile in the slot in use (the file is rewritten) */
static void reset_profile(const char *base)
{
    profile_init(&g_profile, &g_career, "SKATER");
    save_profile(base);
}

static void handle_profile_requests(const char *base)
{
    if (g_fe.req_slot >= 0) {                                  /* use this slot: the profile in use is saved, the other one loaded (or a new one made) */
        int s = g_fe.req_slot;
        g_fe.req_slot = -1;
        if (s >= 0 && s < PROFILE_SLOTS && s != g_profile_slot) {
            save_profile(base);
            g_profile_slot = s;
            g_set.profile_slot = (uint8_t)s;
            save_settings(base);
            if (!load_profile_file(base, 0) && !load_profile_file(base, 1)) reset_profile(base);
            g_fe.need_slots = 1;
        }
    }
    if (g_fe.req_delete >= 0) {                                /* clear this slot (the one in use starts again from nothing) */
        int s = g_fe.req_delete;
        g_fe.req_delete = -1;
        if (s >= 0 && s < PROFILE_SLOTS) {
            char path[320], tmp[330];
            profile_path(base, s, 0, path, sizeof(path));
            profile_path(base, s, 1, tmp, sizeof(tmp));
            sceIoRemove(path);
            sceIoRemove(tmp);
            if (s == g_profile_slot) reset_profile(base);
            g_fe.need_slots = 1;
        }
    }
    if (g_fe.need_slots) {
        refresh_slots(base);
        g_fe.need_slots = 0;
    }
}

/* the riders that are not the player: the opponents of a race (src/race.c) or of a contest and the ambient skaters of the free-skate places (src/stage.c). Their models, and a Skate each that the
 * recorded run fills in (position, heading, speed, the slope ...) so that the pose logic and the body matrix work as for the player */
static Skater g_opp_S[RACE_MAX_OPP];
static Skate g_opp_sk[RACE_MAX_OPP];
static Rider g_opp_rd[RACE_MAX_OPP];
static int g_opp_skater[RACE_MAX_OPP] = { -1, -1, -1 };      /* the career skater index loaded in each slot */

static void opps_unload(void)
{
    for (int i = 0; i < RACE_MAX_OPP; i++)
        if (g_opp_skater[i] >= 0) { unload_skater(&g_opp_S[i]); g_opp_skater[i] = -1; }
}

/* ids[i] = the career skater index wanted in slot i (-1 = none) */
static void opps_load_ids(const char *base, const int *ids, int n)
{
    for (int i = 0; i < RACE_MAX_OPP; i++) {
        int want = i < n ? ids[i] : -1;
        if (g_opp_skater[i] == want) continue;
        if (g_opp_skater[i] >= 0) { unload_skater(&g_opp_S[i]); g_opp_skater[i] = -1; }
        if (want >= 0 && (uint32_t)want < g_career.nskaters) {
            char f[96];
            snprintf(f, sizeof(f), "data/skater_%s.skt", career_str(&g_career, g_career.skater[want].key_off));
            load_skater_file(&g_opp_S[i], base, f);
            g_opp_skater[i] = want;
        }
        skate_init(&g_opp_sk[i], 0.0f, 0.0f, 0.0f, 0.0f);
        rider_init(&g_opp_rd[i]);
    }
    sceKernelDcacheWritebackAll();
}

static void opps_load(const char *base, const Race *r)
{
    int ids[RACE_MAX_OPP];
    for (int i = 0; i < RACE_MAX_OPP; i++) ids[i] = i < r->nopp ? r->opp[i].skater : -1;
    opps_load_ids(base, ids, RACE_MAX_OPP);
}

static int race_ground_cb(void *ctx, float x, float z, float y_hint, float *y)
{
    return coll_ground(&((const Level *)ctx)->coll, x, z, y_hint, y);
}

static const char *ordinal(int n)
{
    static const char *const o[] = { "0th", "1st", "2nd", "3rd", "4th", "5th" };
    return n >= 0 && n <= 5 ? o[n] : "?";
}

static int file_exists(const char *base, const char *name);

/* one other rider: slot j holds his model; gp = what his recorded run says (src/ghost.h) */
static void draw_opponent(int j, const GhostPlayer *gp, const Board *B, const Level *L, int linear)
{
    static SkelFrame ofr;
    static BoardFrame obf;
    static float omats[SKEL_NBONES][12];
    if (j < 0 || j >= RACE_MAX_OPP || g_opp_skater[j] < 0) return;
    Skate *o = &g_opp_sk[j];
    ghost_fill_skate(gp, o);
    rider_update(&g_opp_rd[j], o, 1.0f / 60.0f);
    rider_pose(&g_opp_rd[j], o, &g_opp_S[j].anims, &ofr, &obf);
    skel_matrices(g_opp_S[j].bones, &ofr, g_opp_S[j].hdr->scale, omats);
    skin_skater(&g_opp_S[j], omats);
    float obody[16], obm34[12], obm[16], obmat[16];
    skate_body_matrix(o, obody);
    if (career_skater_regular(&career, g_opp_skater[j])) skate_mirror_matrix(obody);
    board_matrix(&obf, g_opp_S[j].hdr->scale, obm34);
    mat4_from_34(obm, obm34);
    mat4_mul(obmat, obody, obm);
    draw_shadow(B, L, o);
    memcpy(gum_matrix, obody, sizeof(obody));
    sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
    sceGumUpdateMatrix();
    draw_skater(&g_opp_S[j], linear);
    memcpy(gum_matrix, obmat, sizeof(obmat));
    sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
    sceGumUpdateMatrix();
    draw_board(B, linear);
}

/* one game: the level and skater named by level_file / skater_file; returns when the player quits to the menu.
 * SESSION_FREE: just skating. SESSION_CAREER: the beams of the events that are open in the profile; begin_chal >= 0 (Go To Event on the career map) begins that event at once, and after its result
 * a menu offers the map, a retry or skating on. SESSION_TUTORIAL: begin_chal is the tutorial (src/tutorial.c): it begins at once, with its tips on screen, and the Cross after winning ends the
 * session; returns 1 if it was won. */
enum { SESSION_FREE = 0, SESSION_CAREER, SESSION_TUTORIAL };
static int play_session(const char *base, int kind, int begin_chal)
{
    static Level L;
    static Skater S;
    static Board B;
    load_level(&L, base);
    load_skater(&S, base);
    load_board(&B, base);
    if (g_skater_custom) apply_board_art(&B, base, &g_custom_sel);
    sceKernelDcacheWritebackAll();       /* the GPU reads straight from RAM */

    float spawn[3], spawn_heading;
    find_spawn(&L, &spawn[0], &spawn[1], &spawn[2], &spawn_heading);
    float play_lo[3], play_hi[3];
    int have_bounds = levelobj_bounds_outer(&L.objs, play_lo, play_hi);        /* the play area: past its outer limit the rider is put back, like a fall out of the world */

    SkateWorld world = { &L, world_ground, world_wall, world_surface, &L.rails };
    char tricks_path[300];
    snprintf(tricks_path, sizeof(tricks_path), "%s%s", base, TRICKS_FILE);
    tricks_defaults();
    if (!tricks_load(tricks_path))
        tricks_save(tricks_path);                   /* a file to edit: pose, points and difficulty of every grind type */
    int surf = 0;                                   /* material byte of the floor under the rider */
    Control ctl;
    control_init(&ctl);
    float safe_pos[3] = { spawn[0], spawn[1], spawn[2] }, safe_heading = spawn_heading, safe_t = 0.0f;     /* the last flat place the rider rolled on (sampled twice a second): where a fall out of the world puts him back */
    int flip_ready_dir = -1;                        /* the flip table slot that will start with the pop (shown on the HUD) */
    /* sound: the DS game's own effects (data/sounds.snd), mixed in software and played by a thread; the game runs silent if the file or the hardware channel is missing */
    static Sparks sparks;
    int have_sound = g_have_sound;
    int have_music = g_have_music;
    float music_msg_t = 0.0f;
    int heap_used_kb = 0, heap_free_kb = 0;
    GrindProbe probe;
    memset(&probe, 0, sizeof(probe));
    memset(&probe, 0, sizeof(probe));
    float heap_t = 0.0f;
    int music_msg_idx = 0, last_music_idx = 0;
    sparks_init(&sparks);
    Score score;
    score_init(&score);
    StatState stats;
    stats_begin(&stats, &score);
    /* the career: the original's challenges of this level (data/career.dat, tools/convert_career.py), their beams in the level, and what has been done (data/career.sav) */
    static Goals goals;
    static Stage stage;                              /* the other riders besides the race opponents: contest opponents, ambient skaters */
    char lvl_stem[48];
    {
        const char *sl = strrchr(level_file, '/');
        strncpy(lvl_stem, sl ? sl + 1 : level_file, sizeof(lvl_stem) - 1);
        lvl_stem[sizeof(lvl_stem) - 1] = 0;
        char *dot = strrchr(lvl_stem, '.');
        if (dot) *dot = 0;
    }
    stage_init(&stage, &g_ai, lvl_stem);
    unsigned char *chal_done = g_profile.done;       /* the profile's done flags: a won challenge goes through progress_complete */
    const int rider_skater = kind == SESSION_FREE ? (g_fe.nskater > 0 ? g_fe.skater[g_fe.skater_sel].index : 12) : g_profile.skater;      /* the career skater index of the rider */
    int regular = settings_riding_regular(&g_set, career_skater_regular(&career, rider_skater));
    int begin_req = begin_chal, tut_won = 0, res_menu = 0, n_msg = 0, sign_offer = 0;     /* sign_offer: a sponsor event was won and the player has not answered the sponsor yet */
    char sign_label[40];
    char msg[4][48];                                            /* what the last win unlocked ("New skater: ...") */                 /* begin_req: a challenge to begin at the next frame (the tutorial, or the one chosen in the pause menu) */
    int chal_list[24], nchal_list = 0, near_idx = -1, result_seen = 0;
    float beam_pos[24][3];
    uint32_t beam_col[24];
    {
        if (career.nchal > 0) {
            goals_init(&goals, &career, &L.objs, (const uint16_t *)(L.file + L.hdr->off_railids), L.hdr->nrails);
            if (g_have_ai) goals_set_recordings(&goals, &g_ai, lvl_stem);
            int lvl_id = career_level_by_stem(&career, lvl_stem), cand[24];
            int nc = lvl_id >= 0 ? career_challenges_at(&career, lvl_id, cand, 24) : 0;
            for (int i = 0; i < nc; i++)
                if (kind == SESSION_CAREER && goals_supported(&goals, cand[i]) && progress_chal_open(&g_profile, &career, cand[i]) && levelobj_find_num(&L.objs, "chal", 3, career.chal[cand[i]].slot, "_chalstart", beam_pos[nchal_list]))
                    chal_list[nchal_list++] = cand[i];
        }
    }
    float prev_push_t = -1.0f;
    int prev_bailing = 0;
    Skate sk;
    Rider rd;
    skate_init(&sk, spawn[0], spawn[1], spawn[2], spawn_heading);
    rider_init(&rd);
    FollowCam cam = { 0.0f, 0.0f, 4.2f, 1.7f, { 0, 0, 0 }, { 0, 0, 0 } };
    follow_cam_update(&cam, &sk, &L, 0.0f, 1);
    if (kind == SESSION_FREE && g_have_ai && ghost_route_count(&g_ai, lvl_stem) > 0) {          /* the free-skate places are not empty: a few riders skate the original's ambient routes */
        int cand_sk[CAREER_NRIDERS], ncand = 0, ids[STAGE_AMBIENT_MAX];
        for (uint32_t i = 0; i < g_career.nskaters && i < CAREER_NRIDERS; i++) {
            char f[64];
            snprintf(f, sizeof(f), "data/skater_%s.skt", career_str(&g_career, g_career.skater[i].key_off));
            if ((int)i != rider_skater && file_exists(base, f)) cand_sk[ncand++] = (int)i;
        }
        unsigned seed = (unsigned)sceKernelGetSystemTimeLow();
        int nam = ncand < STAGE_AMBIENT_MAX ? ncand : STAGE_AMBIENT_MAX;
        for (int i = 0; i < nam; i++) {                                                          /* a few different riders, picked at random */
            int pick = (int)((seed >> 4) % (unsigned)(ncand - i));
            seed = seed * 1664525u + 1013904223u;
            ids[i] = cand_sk[pick];
            cand_sk[pick] = cand_sk[ncand - i - 1];
        }
        if (nam > 0) {
            opps_load_ids(base, ids, nam);
            if (stage_ambient_begin(&stage, ids, nam, seed, sk.pos, race_ground_cb, &L) == 0) opps_unload();
        }
    }

    int mode = MODE_PLAY;
    int paused = 0, pause_sel = 0, quit_menu = 0, cd_shown = 0, cross_lock = 0, pause_n = 0, pause_act[6];
    int opt_open = 0;                               /* the options screens are open over the pause menu */
    unsigned opt_prev = 0;
    const char *pause_name[6];
    float fx = cam.eye[0], fy = cam.eye[1], fz = cam.eye[2], fyaw = 0.0f, fpitch = -0.2f;      /* fly camera */
    int pose_idx = 0;
    float pose_t = 0.0f;
    const int linear = 0;                           /* the world's texture filter: nearest (the toggle on L + Triangle is gone) */
    int nc_us = 0, clip_on = 1, cpu_us = 0, gpu_us = 0, phys_us = 0, skin_us = 0;
    unsigned int prev_buttons = 0;
    uint64_t last = sceKernelGetSystemTimeWide();
    float fps = 0.0f, fps_acc = 0.0f;
    int fps_frames = 0;
    char hud[64];

    for (;;) {
        uint64_t now = sceKernelGetSystemTimeWide();
        uint64_t frame_t0 = now;
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        fps_acc += dt; fps_frames++;
        if (fps_acc >= 0.5f) {
            fps = (float)fps_frames / fps_acc;
            fps_acc = 0.0f; fps_frames = 0;
        }

        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        float ax = ((int)pad.Lx - 128) / 128.0f, ay = ((int)pad.Ly - 128) / 128.0f;
        if (fabsf(ax) < 0.15f) ax = 0.0f;
        if (fabsf(ay) < 0.15f) ay = 0.0f;
        unsigned int pressed = pad.Buttons & ~prev_buttons;
        prev_buttons = pad.Buttons;

        if ((pressed & PSP_CTRL_SELECT) && (pad.Buttons & PSP_CTRL_LTRIGGER)) {
            /* (L + Select used to switch the information lines on and off: they are chosen in Options > Information now) */
        } else if ((pressed & PSP_CTRL_SELECT) && (pad.Buttons & PSP_CTRL_RTRIGGER)) {
            if (have_music) { audio_music_toggle(&mixer); music_msg_t = 2.0f; music_msg_idx = audio_music_muted() ? -1 : -2; }      /* R + Select: music off / on */
        } else if (pressed & PSP_CTRL_SELECT) {
            mode = (mode + 1) % NMODES;
            if (mode != MODE_PLAY) {            /* start the fly camera where the follow camera was looking */
                float dx = cam.look[0] - cam.eye[0], dy = cam.look[1] - cam.eye[1], dz = cam.look[2] - cam.eye[2];
                float len = sqrtf(dx * dx + dy * dy + dz * dz);
                fx = cam.eye[0]; fy = cam.eye[1]; fz = cam.eye[2];
                fyaw = atan2f(-dx, -dz);
                fpitch = len > 1e-6f ? asinf(dy / len) : 0.0f;
            }
        }
        if ((pressed & PSP_CTRL_START) && (pad.Buttons & PSP_CTRL_LTRIGGER)) {
            clip_on = !clip_on;                  /* diagnostic: compare with and without the near-camera clip pass */
        } else if ((pressed & PSP_CTRL_START) && (pad.Buttons & PSP_CTRL_RTRIGGER)) {
            if (have_music) audio_music_skip(&mixer);                    /* R + Start: next song (once it has loaded) */
        } else if ((pressed & PSP_CTRL_START) && !opt_open) {
            paused = !paused;                                                  /* Start: the pause menu */
            pause_sel = 0;
            if (have_sound) sfx_event(&sfx, &mixer, paused ? SFX_EV_ENTER : SFX_EV_BACK);
        }
        {                                                                      /* the pause menu: what it offers depends on where the rider is */
            enum { PA_RESUME, PA_RESPAWN, PA_BEGIN, PA_RESTART, PA_LEAVE, PA_OPTIONS, PA_QUIT, PA_SIGN, PA_DECLINE };
            int active = goals_active(&goals), shown = goals.phase == GOAL_WON || goals.phase == GOAL_LOST;
            int can_begin = !active && !shown && near_idx >= 0 && mode == MODE_PLAY;
            const char *quit_name = kind == SESSION_CAREER ? "QUIT TO MAP" : "QUIT TO MENU";
            if (!paused) res_menu = 0;
            pause_n = 0;
            if (res_menu && sign_offer) {                                      /* a sponsor wants him: sign (the event counts, the sponsor is his) or not now (the event stays open) */
                snprintf(sign_label, sizeof(sign_label), "SIGN WITH %.26s", career_sponsor_name(&career, goals.chal));
                for (char *q = sign_label; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
                pause_act[pause_n] = PA_SIGN; pause_name[pause_n++] = sign_label;
                pause_act[pause_n] = PA_DECLINE; pause_name[pause_n++] = "NOT NOW";
            } else if (res_menu) {                                             /* the result of a career event has been read: where to now */
                if (goals.phase == GOAL_WON) {
                    pause_act[pause_n] = PA_QUIT; pause_name[pause_n++] = "BACK TO MAP";
                    pause_act[pause_n] = PA_RESTART; pause_name[pause_n++] = "RETRY";
                } else {
                    pause_act[pause_n] = PA_RESTART; pause_name[pause_n++] = "RETRY";
                    pause_act[pause_n] = PA_QUIT; pause_name[pause_n++] = "BACK TO MAP";
                }
                pause_act[pause_n] = PA_LEAVE; pause_name[pause_n++] = "KEEP SKATING";
            } else {
                pause_act[pause_n] = PA_RESUME; pause_name[pause_n++] = "RESUME";
                if (can_begin) { pause_act[pause_n] = PA_BEGIN; pause_name[pause_n++] = "START CHALLENGE"; }
                if (active || shown) {
                    pause_act[pause_n] = PA_RESTART; pause_name[pause_n++] = "RESTART CHALLENGE";
                    if (kind != SESSION_TUTORIAL) { pause_act[pause_n] = PA_LEAVE; pause_name[pause_n++] = "LEAVE CHALLENGE"; }       /* (a tutorial is left through QUIT TO MENU) */
                } else {
                    pause_act[pause_n] = PA_RESPAWN; pause_name[pause_n++] = "RESPAWN";
                }
                pause_act[pause_n] = PA_OPTIONS; pause_name[pause_n++] = "OPTIONS";
                pause_act[pause_n] = PA_QUIT; pause_name[pause_n++] = quit_name;
            }
            if (pause_sel >= pause_n) pause_sel = 0;
            if (paused && opt_open) {                                          /* the options over the pause menu: the same screens as in the main menu */
                FeInput fi;
                fi.held = fe_buttons(pad.Buttons, ax, ay);
                fi.pressed = fi.held & ~opt_prev;
                opt_prev = fi.held;
                if (fe_options_update(&g_fe, &fi, dt))
                    opt_open = 0;
                if (have_sound) {
                    if (g_fe.ev_move) sfx_event(&sfx, &mixer, SFX_EV_MOVE);
                    if (g_fe.ev_ok) sfx_event(&sfx, &mixer, SFX_EV_ENTER);
                    if (g_fe.ev_back) sfx_event(&sfx, &mixer, SFX_EV_BACK);
                }
                g_fe.ev_move = g_fe.ev_ok = g_fe.ev_back = 0;
                if (g_fe.opt.changed) { save_settings(base); g_fe.opt.changed = 0; }
            } else if (paused) {
                if (pressed & PSP_CTRL_UP) { pause_sel = (pause_sel + pause_n - 1) % pause_n; if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_MOVE); }
                if (pressed & PSP_CTRL_DOWN) { pause_sel = (pause_sel + 1) % pause_n; if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_MOVE); }
                if (pressed & PSP_CTRL_CIRCLE) {
                    paused = 0; cross_lock = 1;
                    if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_BACK);
                } else if (pressed & PSP_CTRL_CROSS) {
                    int act = pause_act[pause_sel];
                    if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_ENTER);
                    if (act != PA_OPTIONS && act != PA_SIGN && act != PA_DECLINE) { paused = 0; cross_lock = 1; }       /* (the Cross that chose must not also pop an ollie) */
                    if (act == PA_BEGIN) begin_req = chal_list[near_idx];
                    else if (act == PA_RESTART) begin_req = goals.chal;
                    else if (act == PA_LEAVE) { goals_abort(&goals); sign_offer = 0; }
                    else if (act == PA_SIGN || act == PA_DECLINE) {                        /* the sponsor's offer: answered, the result menu follows (its entries change) */
                        if (act == PA_SIGN) {
                            char who[40];
                            snprintf(who, sizeof(who), "Signed with %.24s!", career_sponsor_name(&career, goals.chal));
                            career_win(base, goals.chal, msg, &n_msg);
                            if (n_msg < 4) memmove(msg[1], msg[0], (size_t)n_msg * sizeof(msg[0])), snprintf(msg[0], sizeof(msg[0]), "%s", who), n_msg++;
                        } else {
                            n_msg = 0;
                            snprintf(msg[n_msg++], sizeof(msg[0]), "Not signed - the event stays open");
                        }
                        sign_offer = 0;
                        pause_sel = 0;
                    }
                    else if (act == PA_OPTIONS) { opt_open = 1; opt_prev = fe_buttons(pad.Buttons, ax, ay); fe_options_open(&g_fe); g_fe.opt.no_credits = 1; }
                    else if (act == PA_RESPAWN) {
                        skate_init(&sk, spawn[0], spawn[1], spawn[2], spawn_heading);
                        rider_init(&rd);
                        score_cancel(&score);
                        if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_RESPAWN);
                        cam.user = 0.0f;
                        follow_cam_update(&cam, &sk, &L, 0.0f, 1);
                    } else if (act == PA_QUIT) quit_menu = 1;
                }
            }
        }
        if (begin_req >= 0) {                                                  /* a challenge begins: the rider to its start grid, facing the way the original does; the countdown runs first */
            int bi = begin_req;
            begin_req = -1;
            if (goals_begin(&goals, bi, &score)) {
                n_msg = 0;
                if (goals.is_race) opps_load(base, &goals.race);
                else if (goals.is_contest) {                                           /* the opponents of a contest skate their recorded runs */
                    stage_contest_begin(&stage, &goals.contest, &L.objs, race_ground_cb, &L);
                    int ids[STAGE_MAX];
                    for (int j = 0; j < STAGE_MAX; j++) ids[j] = j < stage.n ? stage.r[j].skater : -1;
                    opps_load_ids(base, ids, STAGE_MAX);
                } else opps_unload();
                float px = goals.have_start ? goals.start_pos[0] : sk.pos[0], pz = goals.have_start ? goals.start_pos[2] : sk.pos[2], py = goals.have_start ? goals.start_pos[1] : sk.pos[1], gy;
                if (!coll_ground(&L.coll, px, pz, py + 1.0f, &gy)) gy = py;
                skate_init(&sk, px, gy, pz, goals.have_start ? goals.start_heading : sk.heading);
                safe_pos[0] = px; safe_pos[1] = gy + 0.05f; safe_pos[2] = pz; safe_heading = sk.heading;
                rider_init(&rd);
                score_cancel(&score);
                result_seen = 0; cd_shown = 0; sign_offer = 0;
                cam.user = 0.0f;
                follow_cam_update(&cam, &sk, &L, 0.0f, 1);
            }
        }

        if (have_music) {
            audio_music_update(&mixer);
            int mi = audio_music_index();
            if (mi != last_music_idx && mi > 0) { music_msg_t = 3.0f; music_msg_idx = mi; }       /* a new song: say which */
            last_music_idx = mi;
        }
        if (music_msg_t > 0.0f) music_msg_t -= dt;
        heap_t -= dt;
        if (heap_t <= 0.0f) {                                          /* once a second */
            struct mallinfo mi = mallinfo();
            heap_used_kb = (int)((unsigned)mi.uordblks / 1024u);
            heap_free_kb = heap_largest_free_kb();
            heap_t = 1.0f;
        }

        uint64_t ph0 = sceKernelGetSystemTimeWide();
        if (paused) {
            if (have_sound)
                for (int ch = 0; ch < MIX_LOOPS; ch++)
                    mixer_loop(&mixer, ch, NULL, 0.0f, 1.0f);       /* paused: silent */
        } else if (mode == MODE_PLAY) {
            SkateInput in;
            {                                                   /* the nub + buttons -> the rider's input (src/control.c: flicks, manual holds, grabs on L / R) */
                CtlFrame cf;
                memset(&cf, 0, sizeof(cf));
                cf.ax = ax; cf.ay = ay;
                unsigned ph = pad_phys(pad.Buttons), pp = pad_phys(pressed);        /* the buttons the player holds -> the actions they are mapped to (Options > Controls) */
                if (cross_lock) {                                  /* the Cross that picked a menu item is not an ollie (or whatever Cross does now) */
                    ph &= ~PBM(PB_CROSS); pp &= ~PBM(PB_CROSS);
                    if (!(pad.Buttons & PSP_CTRL_CROSS)) cross_lock = 0;
                }
                cf.held = settings_ctl_bits(&g_set, ph);
                cf.pressed = settings_ctl_bits(&g_set, pp);
                cf.grounded = sk.grounded; cf.grinding = sk.grinding; cf.bail = sk.bail_t > 0.0f;
                cf.manual = sk.manual; cf.flip = sk.flip; cf.grab = sk.grab; cf.speed = fabsf(sk.speed); cf.wallriding = sk.wallriding;
                if (!sk.grounded && !sk.grinding) {                 /* a rail or ledge within reach: the nub picks the grind, it does not turn (the HUD's rail line shows the same probe) */
                    skate_grind_probe(&sk, NULL, &world, &probe);
                    cf.rail_near = probe.found && probe.dist < 1.0f && probe.dy > -0.6f && probe.dy < 1.2f;
                }
                control_update(&ctl, &cf, dt, &in);
                flip_ready_dir = ctl.ready;                     /* for the HUD: the flip that will start with the pop */
            }
            /* physics substeps of at most 1/60 s, never more than 2: a slow frame must not make the next one slower (the probes reach d + 0.35 m,
             * so even 12 m/s = 0.2 m per step cannot tunnel through anything) */
            int steps = (int)ceilf(dt * 60.0f - 0.1f);
            if (steps < 1) steps = 1;
            if (steps > 2) steps = 2;
            if (goals.phase == GOAL_COUNTDOWN_PHASE) steps = 0;       /* 3 - 2 - 1: the rider waits at the start */
            for (int i = 0; i < steps; i++) {
                skate_update(&sk, &in, &world, dt / (float)steps);
                score.regular = regular;
                score_update(&score, &sk, dt / (float)steps);
                in.push = 0;                      /* the push is a press, not a hold */
                in.manual = 0;                    /* so is the manual toggle */
                in.flip = 0;                      /* and the flip request */
                in.revert = 0;                    /* and the revert */
                in.grind_switch = 0;              /* and the change of grind */
            }
            stats_frame(&g_profile, &stats, &sk, &score, dt);          /* the Log Book's numbers (play time, distances, bests, the trick book) */
            if (stage.kind == 2) stage_ambient_update(&stage, dt, sk.pos, race_ground_cb, &L);
            if (goals.phase != GOAL_IDLE) {
                GoalIn gin;
                memset(&gin, 0, sizeof(gin));
                gin.dt = dt; gin.grounded = sk.grounded; gin.grinding = sk.grinding; gin.air_t = sk.air_t;
                gin.pos[0] = sk.pos[0]; gin.pos[1] = sk.pos[1]; gin.pos[2] = sk.pos[2];
                gin.pushes = sk.push_t >= 0.0f && prev_push_t < 0.0f;
                gin.score = &score;
                gin.bails = (sk.bail_t > 0.0f && !prev_bailing) ? 1 : 0;           /* a race takes seconds off the clock for each bail */
                gin.ground = race_ground_cb; gin.ground_ctx = &L;
                goals_update(&goals, &gin);
                if (goals.is_contest && stage.kind == 1) stage_contest_update(&stage, &goals.contest, dt, race_ground_cb, &L);
                if (goals.phase == GOAL_COUNTDOWN_PHASE) {                       /* 3 - 2 - 1 - GO */
                    int c = (int)ceilf(goals.countdown);
                    if (c != cd_shown) { cd_shown = c; if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_321); }
                } else if (cd_shown) {
                    if (goals.phase == GOAL_ACTIVE && have_sound) sfx_event(&sfx, &mixer, SFX_EV_GO);
                    cd_shown = 0;
                }
                if (goals.phase == GOAL_WON && !result_seen) {
                    result_seen = 1;
                    if (kind == SESSION_CAREER && career_sponsor_name(&career, goals.chal) && !progress_chal_done(&g_profile, goals.chal)) {
                        sign_offer = 1;                                                  /* a sponsor event: the sponsor offers, the player chooses (the result menu); it counts when he signs */
                        n_msg = 0;
                        snprintf(msg[n_msg++], sizeof(msg[0]), "%.28s wants to sign you!", career_sponsor_name(&career, goals.chal));
                    } else {
                        career_win(base, goals.chal, msg, &n_msg);
                    }
                    if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_WIN);
                }
                if (goals.phase == GOAL_LOST && !result_seen) {
                    result_seen = 1;
                    if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_LOSE);
                }
                if (goals.phase == GOAL_WON || goals.phase == GOAL_LOST) {
                    if ((pressed & PSP_CTRL_CROSS) && goals.result_t > 1.5f) {
                        if (kind == SESSION_TUTORIAL) {                                                    /* a tutorial: won = on to the list (the next one is chosen), lost = again */
                            if (goals.phase == GOAL_WON) { tut_won = 1; quit_menu = 1; }
                            else begin_req = goals.chal;
                        } else if (kind == SESSION_CAREER) {                                               /* the map, a retry or skating on */
                            paused = 1; res_menu = 1; pause_sel = 0;
                            if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_ENTER);
                        } else {
                            goals_abort(&goals);                                                           /* the result is read: back to skating */
                        }
                    }
                }
            }
            prev_push_t = sk.push_t;
            prev_bailing = sk.bail_t > 0.0f;
            near_idx = -1;                                       /* the beam the rider stands in (Start begins it) */
            if (!goals_active(&goals) && goals.phase != GOAL_WON && goals.phase != GOAL_LOST && sk.grounded)
                for (int i = 0; i < nchal_list; i++) {
                    float dx = sk.pos[0] - beam_pos[i][0], dz = sk.pos[2] - beam_pos[i][2];
                    if (dx * dx + dz * dz < 3.5f * 3.5f && fabsf(sk.pos[1] - beam_pos[i][1]) < 3.0f) { near_idx = i; break; }
                }
            safe_t -= dt;
            if (sk.grounded && !sk.grinding && sk.bail_t <= 0.0f && sk.cn[1] > 0.95f && safe_t <= 0.0f) {
                safe_pos[0] = sk.pos[0]; safe_pos[1] = sk.pos[1] + 0.05f; safe_pos[2] = sk.pos[2]; safe_heading = sk.heading;
                safe_t = 0.5f;
            }
            int out_of_bounds = have_bounds && (sk.pos[0] < play_lo[0] || sk.pos[0] > play_hi[0] || sk.pos[2] < play_lo[2] || sk.pos[2] > play_hi[2]);
            if (sk.pos[1] < L.hdr->bounds[1] - FALL_RESPAWN_DROP || out_of_bounds) {      /* fell out of the world (a hole in the collision) or left the play area: back to the last flat place, standing still */
                skate_init(&sk, safe_pos[0], safe_pos[1], safe_pos[2], safe_heading);
                rider_init(&rd);
                if (have_sound) sfx_event(&sfx, &mixer, SFX_EV_RESPAWN);
                score_cancel(&score);
                cam.user = 0.0f;
                follow_cam_update(&cam, &sk, &L, 0.0f, 1);
            }
            if (g_set.info[INFO_RAIL] && (sk.grounded || sk.grinding)) skate_grind_probe(&sk, &in, &world, &probe);      /* (in the air it was probed for the controls above) */
            rider_update(&rd, &sk, dt);
            sparks_update(&sparks, &sk, sk.ev_grind_mat, dt);
            if (sk.grounded) {                                   /* what the wheels are on: the material of the collision polygon underfoot (tools/materials.py) */
                float gy; uint32_t pid;
                if (coll_ground_poly(&L.coll, sk.pos[0], sk.pos[2], sk.pos[1] + 0.3f, &gy, &pid) && pid < L.hdr->ncp)
                    surf = L.polymat[pid];
            }
            if (have_sound) {
                sfx_update(&sfx, &mixer, &sk, &in, surf, dt);
                if (score.ev_bank) {                             /* a combo was banked: its jingle (a big one for a big combo) */
                    sfx_event(&sfx, &mixer, score.ev_bank_pts >= 400 || score.ev_bank_mult >= 3 ? SFX_EV_BANK_BIG : SFX_EV_BANK);
                }
            }
            score.ev_bank = 0;
            if (pad.Buttons & PSP_CTRL_LEFT)  cam.user += 1.8f * dt;              /* D-pad left / right: orbit the camera */
            if (pad.Buttons & PSP_CTRL_RIGHT) cam.user -= 1.8f * dt;
            if (pad.Buttons & PSP_CTRL_UP)   { cam.dist -= 3.0f * dt; if (cam.dist < 2.0f) cam.dist = 2.0f; }        /* D-pad up / down: camera distance */
            if (pad.Buttons & PSP_CTRL_DOWN) { cam.dist += 3.0f * dt; if (cam.dist > 9.0f) cam.dist = 9.0f; }
            if (!(pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)))
                cam.user *= expf(-1.5f * dt);     /* drift back behind the rider */
            follow_cam_update(&cam, &sk, &L, dt, 0);
        } else {
            if (have_sound)
                for (int ch = 0; ch < MIX_LOOPS; ch++)
                    mixer_loop(&mixer, ch, NULL, 0.0f, 1.0f);       /* the fly camera is silent */
            fyaw   -= ax * 2.2f * dt;
            fpitch -= ay * 1.6f * dt;
            if (fpitch > 1.5f) fpitch = 1.5f;
            if (fpitch < -1.5f) fpitch = -1.5f;
            float speed = 8.0f;
            if (pad.Buttons & PSP_CTRL_SQUARE) speed *= 4.0f;
            if (pad.Buttons & PSP_CTRL_CIRCLE) speed *= 0.25f;
            float dx = -sinf(fyaw) * cosf(fpitch), dy = sinf(fpitch), dz = -cosf(fyaw) * cosf(fpitch);
            float rx = cosf(fyaw), rz = -sinf(fyaw);
            float mf = 0.0f, ms = 0.0f, mu = 0.0f;
            if (pad.Buttons & PSP_CTRL_UP)    mf += 1.0f;
            if (pad.Buttons & PSP_CTRL_DOWN)  mf -= 1.0f;
            if (pad.Buttons & PSP_CTRL_RIGHT) ms += 1.0f;
            if (pad.Buttons & PSP_CTRL_LEFT)  ms -= 1.0f;
            if (pad.Buttons & PSP_CTRL_RTRIGGER) mu += 1.0f;
            if (pad.Buttons & PSP_CTRL_LTRIGGER) mu -= 1.0f;
            fx += (dx * mf + rx * ms) * speed * dt;
            fy += (dy * mf + mu) * speed * dt;
            fz += (dz * mf + rz * ms) * speed * dt;
            if (mode == MODE_POSE) {
                int step = (pad.Buttons & PSP_CTRL_SQUARE) ? 10 : 1;
                int n = (int)S.anims.nanims;
                if (pressed & PSP_CTRL_CROSS)    { pose_idx = (pose_idx + step) % n; pose_t = 0.0f; }
                if (pressed & PSP_CTRL_TRIANGLE) { pose_idx = ((pose_idx - step) % n + n) % n; pose_t = 0.0f; }
                pose_t += dt;
            }
        }

        phys_us = (int)(sceKernelGetSystemTimeWide() - ph0);
        uint64_t sk0 = sceKernelGetSystemTimeWide();
        /* ---- pose -> bone matrices -> skinned vertices ---- */
        static SkelFrame frame;
        static BoardFrame bframe;
        static float mats[SKEL_NBONES][12];
        if (mode == MODE_POSE) {
            const SkelAnim *an = &S.anims.anims[pose_idx];
            float f = fmodf(pose_t * POSE_FPS, (float)an->nframes);
            animset_sample(&S.anims, pose_idx, f, &frame);
            float bf = an->nframes > 1 ? f * (float)(an->board_nframes - 1) / (float)(an->nframes - 1) : 0.0f;
            animset_board(&S.anims, pose_idx, bf, &bframe);
        } else {
            rider_pose(&rd, &sk, &S.anims, &frame, &bframe);
        }
        skel_matrices(S.bones, &frame, S.hdr->scale, mats);
        skin_skater(&S, mats);
        skin_us = (int)(sceKernelGetSystemTimeWide() - sk0);
        float body[16], bm34[12], bm[16], bmat[16];
        regular = settings_riding_regular(&g_set, career_skater_regular(&career, rider_skater));       /* (the stance can be changed in the pause menu's options) */
        g_fe.regular = regular;
        skate_body_matrix(&sk, body);
        if (regular) skate_mirror_matrix(body);                       /* the clips are goofy ones: a regular rider is the mirror image (board and all) */
        board_matrix(&bframe, S.hdr->scale, bm34);
        mat4_from_34(bm, bm34);
        mat4_mul(bmat, body, bm);

        /* ---- camera: one view matrix for the GPU and for the near-camera clipper ---- */
        float eye[3], ctr[3], upv[3] = { 0.0f, 1.0f, 0.0f }, view[16];
        if (mode == MODE_PLAY) {
            memcpy(eye, cam.eye, sizeof(eye));
            memcpy(ctr, cam.look, sizeof(ctr));
        } else {
            eye[0] = fx; eye[1] = fy; eye[2] = fz;
            ctr[0] = fx - sinf(fyaw) * cosf(fpitch); ctr[1] = fy + sinf(fpitch); ctr[2] = fz - cosf(fyaw) * cosf(fpitch);
        }
        nc_lookat(view, eye, ctr, upv);
        static NcTri nc_out[NC_MAX];
        uint64_t nc_t0 = sceKernelGetSystemTimeWide();
        int nc_n = clip_on ? nc_gather(&L.nc, view, eye, NC_RADIUS, NEAR_PLANE, FOCAL_PX, nc_out, NC_MAX) : 0;
        nc_us = (int)(sceKernelGetSystemTimeWide() - nc_t0);

        /* ---- draw ---- */
        sceGuStart(GU_DIRECT, display_list);
        sceGuClearColor(0xFFE0B070);                     /* ABGR: light sky blue */
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(FOV_Y_DEG, (float)SCR_W / (float)SCR_H, NEAR_PLANE, 3000.0f);
        set_view(view);

        sceGuEnable(GU_DEPTH_TEST);
        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();
        sceGumUpdateMatrix();
        draw_level(&L, linear);
        draw_clipped(&L, nc_out, nc_n, linear);          /* the triangles the GE would have rejected, clipped on the CPU */
        set_view(view);

        if (mode != MODE_POSE)
            draw_shadow(&B, &L, &sk);

        memcpy(gum_matrix, body, sizeof(body));
        sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
        sceGumUpdateMatrix();
        draw_skater(&S, linear);

        memcpy(gum_matrix, bmat, sizeof(bmat));
        sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
        sceGumUpdateMatrix();
        draw_board(&B, linear);
        if (mode == MODE_PLAY) {                                          /* the other riders: the pose logic on the state their recorded run gives them, skinned and drawn like the player */
            if (goals.is_race && goals.phase != GOAL_IDLE) {
                for (int j = 0; j < goals.race.nopp && j < RACE_MAX_OPP; j++) draw_opponent(j, &goals.race.opp[j].view, &B, &L, linear);
            } else if (goals.is_contest && goals.phase != GOAL_IDLE) {
                for (int j = 0; j < stage.n; j++) draw_opponent(j, &stage.r[j].gp, &B, &L, linear);
            } else if (stage.kind == 2) {
                for (int j = 0; j < stage.n; j++) draw_opponent(j, &stage.r[j].gp, &B, &L, linear);
            }
        }
        if (mode == MODE_PLAY)
            draw_sparks(&sparks);
        if (mode == MODE_PLAY && goals.is_race && (goals.phase == GOAL_ACTIVE || goals.phase == GOAL_COUNTDOWN_PHASE) && goals.race.next < goals.race.ngates) {       /* the gate to pass next: a beam on each post, the one after dimmer */
            float gp[4][3];
            uint32_t gc2[4];
            int ng = 0;
            for (int k = 0; k < 2 && goals.race.next + k < goals.race.ngates; k++) {
                const RaceGate *rg = &goals.race.gate[goals.race.next + k];
                memcpy(gp[ng], rg->a, 12); gc2[ng++] = k == 0 ? (rg->finish ? 0xFF40E0FF : 0xFF40E040) : 0xFF909090;
                memcpy(gp[ng], rg->b, 12); gc2[ng++] = k == 0 ? (rg->finish ? 0xFF40E0FF : 0xFF40E040) : 0xFF909090;
            }
            draw_beams((const float (*)[3])gp, gc2, ng, (float)(sceKernelGetSystemTimeWide() / 1000) / 1000.0f);
        }
        if (mode == MODE_PLAY && nchal_list > 0 && goals.phase == GOAL_IDLE) {
            for (int i = 0; i < nchal_list; i++) beam_col[i] = chal_done[chal_list[i] < SAVE_NCHAL ? chal_list[i] : 0] ? 0xFF30B0F0 : 0xFF40E040;      /* green: to do, gold: done */
            draw_beams((const float (*)[3])beam_pos, beam_col, nchal_list, (float)(sceKernelGetSystemTimeWide() / 1000) / 1000.0f);
        }

        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_DEPTH_TEST);
        /* ---- HUD. The player's HUD (trick + points + multiplier strip, score, challenge, countdown) is drawn in the original's style through src/hud.c; the debug lines (the old pixel font:
         * FPS, the trick state with its pose numbers, position, nub, rail probe, timings) only show with L + Select. ---- */
        {
            const int HS = 2, LH = 11, HX = 4;
            int hy = 4;
            if (mode == MODE_PLAY) {
                const Hud *H = &g_hud;
                const uint32_t YEL = UI_RGBA(255, 214, 40, 255), GRN = UI_RGBA(120, 255, 120, 255), RED = UI_RGBA(255, 90, 80, 255);
                char t1[96];
                ui_gu_begin();
                ui_reset_view(&g_ui);
                snprintf(t1, sizeof(t1), "Score %ld", score.total);
                hud_text_right(H, &g_ui, H->f_m, SCR_W - 8, 6, t1, UI_WHITE);
                if (score.combo_active && score.combo_n > 0) {
                    int lvl = score.combo_n >= 4 ? 3 : score.combo_n - 1;
                    int line = goals.phase == GOAL_ACTIVE && (goals.ch->flags & CHAL_LINE);
                    hud_combo(H, &g_ui, score_trick_name(&score), line ? "Line: " : "", score_combo_points(&score), lvl);
                }
                if (ctl.nollie && sk.grounded && !sk.grinding)                       /* the nollie stance (the nub held forward while crouching): the pop and the trick will be nollie ones */
                    hud_text(H, &g_ui, H->f_s, 10, SCR_H - 82, "NOLLIE", YEL);
                if (score.msg != SCORE_MSG_NONE && score.msg_t > 0.0f) {                 /* the last result in the middle, fading out */
                    int a = score.msg_t < 0.5f ? (int)(255.0f * score.msg_t / 0.5f) : 255;
                    if (score.msg == SCORE_MSG_BANK) {
                        snprintf(t1, sizeof(t1), "+%ld pts", score.msg_pts);
                        hud_text_center(H, &g_ui, H->f_l, SCR_W / 2.0f, 78, 1.0f, t1, UI_RGBA(120, 255, 120, a));
                        if (score.msg_mult > 1) {
                            snprintf(t1, sizeof(t1), "%d tricks  x%d", score.msg_n, score.msg_mult);
                            hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, 104, 1.0f, t1, UI_RGBA(255, 255, 255, a));
                        }
                    } else if (score.msg == SCORE_MSG_BAIL) {
                        hud_text_center(H, &g_ui, H->f_l, SCR_W / 2.0f, 78, 1.0f, "Bail!", UI_RGBA(255, 90, 80, a));
                    }
                }
                if (goals.phase != GOAL_IDLE) {                                  /* the challenge: its name, the clock, the objectives; the countdown and the result in the middle */
                    float cy = 26.0f;
                    snprintf(t1, sizeof(t1), "%.30s", career_str(&career, goals.ch->name_off));
                    hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, t1, YEL); cy += 16.0f;
                    if (goals.timed) {
                        int tl = goals.time_left > 0.0f ? (int)ceilf(goals.time_left) : 0;
                        snprintf(t1, sizeof(t1), "%d:%02d", tl / 60, tl % 60);
                        hud_text_center(H, &g_ui, H->f_l, SCR_W / 2.0f, 6, 1.0f, t1, tl < 10 && goals.phase == GOAL_ACTIVE ? RED : UI_WHITE);
                    }
                    if (goals.is_contest) {                                           /* a Jam / Best Trick / S.K.A.T.E.: the scores or the letters, and what is going on */
                        const Contest *K = &goals.contest;
                        const uint32_t DIM = UI_RGBA(200, 200, 200, 255);
                        const char *tn = (K->target >= 0 && (uint32_t)K->target < career.ntricks) ? career_str(&career, career.trick[K->target].name_off) : "";
                        if (goals.phase == GOAL_ACTIVE || goals.phase == GOAL_WON || goals.phase == GOAL_LOST) {
                            if (K->mode == MODE_SKATE) {
                                char lt[12];
                                contest_letters(K->letters_player, lt); snprintf(t1, sizeof(t1), "You  %s", lt); hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, t1, K->letters_player >= 3 ? RED : UI_WHITE); cy += 15.0f;
                                contest_letters(K->letters_opp, lt); snprintf(t1, sizeof(t1), "%.12s  %s", career_str(&career, career.skater[K->opp_skater[0]].name_off), lt); hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, t1, K->letters_opp >= 3 ? GRN : UI_WHITE); cy += 15.0f;
                            } else {
                                if (K->mode == MODE_BESTTRICK) { snprintf(t1, sizeof(t1), "Round %d of %d", K->round < K->rounds ? K->round + 1 : K->rounds, K->rounds); hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, t1, DIM); cy += 15.0f; }
                                snprintf(t1, sizeof(t1), "You  %ld", K->player_score + (K->mode == MODE_BESTTRICK ? K->player_round : 0));
                                hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, t1, YEL); cy += 15.0f;
                                for (int j = 0; j < K->nopp; j++) {
                                    snprintf(t1, sizeof(t1), "%.12s  %ld", career_str(&career, career.skater[K->opp_skater[j]].name_off), K->opp_score[j]);
                                    hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, t1, UI_WHITE); cy += 15.0f;
                                }
                            }
                        }
                        if (goals.phase == GOAL_ACTIVE) {                                         /* the instruction in the middle, under the clock */
                            float ty = 34.0f;
                            if (K->mode == MODE_SKATE) {
                                if (K->brk > 0.0f) {
                                    const char *m = K->msg == CT_MSG_MATCHED ? "You landed it!" : K->msg == CT_MSG_MISSED ? "Missed - a letter" : K->msg == CT_MSG_OPP_MATCHED ? "He landed it" : K->msg == CT_MSG_OPP_MISSED ? "He missed - a letter for him!" : "No letter - his turn";
                                    hud_text_center(H, &g_ui, H->f_m, SCR_W / 2.0f, ty, 1.0f, m, K->msg == CT_MSG_MISSED ? RED : GRN);
                                    if (K->msg == CT_MSG_OPP_MATCHED || K->msg == CT_MSG_OPP_MISSED) { snprintf(t1, sizeof(t1), "Your trick: %s", tn); hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, ty + 22.0f, 1.0f, t1, UI_WHITE); }
                                } else if (K->state == SK_OPP_SETS) {
                                    snprintf(t1, sizeof(t1), "%.12s sets: %s", career_str(&career, career.skater[K->opp_skater[0]].name_off), tn);
                                    hud_text_center(H, &g_ui, H->f_m, SCR_W / 2.0f, ty, 1.0f, t1, YEL);
                                    hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, ty + 22.0f, 1.0f, "Land the same trick!", UI_WHITE);
                                } else {
                                    hud_text_center(H, &g_ui, H->f_m, SCR_W / 2.0f, ty, 1.0f, "Your turn: set a trick", YEL);
                                    hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, ty + 22.0f, 1.0f, "He has to copy the first one you land", UI_WHITE);
                                }
                            } else if (K->brk > 0.0f && K->mode == MODE_BESTTRICK) {
                                snprintf(t1, sizeof(t1), "Round %d: %ld", K->round, K->round_score[K->round - 1]);
                                hud_text_center(H, &g_ui, H->f_m, SCR_W / 2.0f, ty, 1.0f, t1, GRN);
                                hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, ty + 22.0f, 1.0f, "Next round starts in a moment", UI_WHITE);
                            }
                        } else if ((goals.phase == GOAL_WON || goals.phase == GOAL_LOST) && K->mode != MODE_SKATE) {
                            snprintf(t1, sizeof(t1), K->mode == MODE_JAM ? "Final score  %ld" : "Total  %ld", K->player_score);
                            hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, 152.0f, 1.0f, t1, YEL);
                            snprintf(t1, sizeof(t1), "%s place", ordinal(K->place));
                            hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, 167.0f, 1.0f, t1, K->place == 1 ? GRN : UI_WHITE);
                        }
                    }
                    if (goals.is_race) {
                        const Race *R = &goals.race;
                        if (goals.phase == GOAL_ACTIVE) {
                            if (R->ngates > 1) { snprintf(t1, sizeof(t1), "Gate %d/%d", R->next + 1 > R->ngates ? R->ngates : R->next + 1, R->ngates); hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, t1, UI_WHITE); cy += 15.0f; }
                            snprintf(t1, sizeof(t1), "%s of %d", ordinal(race_place(R)), R->nopp + 1);
                            hud_text_right(H, &g_ui, H->f_m, SCR_W - 8, cy, t1, race_place(R) == 1 ? GRN : UI_WHITE); cy += 20.0f;
                        } else if (goals.phase == GOAL_WON || goals.phase == GOAL_LOST) {                 /* the final positions */
                            struct { float time; const char *name; int me; } row[RACE_MAX_OPP + 1];
                            int nr = 0;
                            row[nr].time = R->player_done ? R->player_time : 1e9f; row[nr].name = "You"; row[nr++].me = 1;
                            for (int k = 0; k < R->nopp; k++) { row[nr].time = R->opp[k].finish_time; row[nr].name = career_str(&career, career.skater[R->opp[k].skater].name_off); row[nr++].me = 0; }
                            for (int a = 1; a < nr; a++) for (int b = a; b > 0 && row[b].time < row[b - 1].time; b--) { __typeof__(row[0]) tmp = row[b]; row[b] = row[b - 1]; row[b - 1] = tmp; }
                            hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, 152.0f, 1.0f, "Final positions", UI_RGBA(200, 200, 200, 255));
                            for (int k = 0; k < nr; k++) {
                                if (row[k].time > 9999.0f) snprintf(t1, sizeof(t1), "%d  %s   -", k + 1, row[k].name);
                                else snprintf(t1, sizeof(t1), "%d  %s   %d.%d s", k + 1, row[k].name, (int)row[k].time, (int)(row[k].time * 10.0f) % 10);
                                hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, 167.0f + 15.0f * (float)k, 1.0f, t1, row[k].me ? YEL : UI_WHITE);
                            }
                        }
                    }
                    for (int i = 0; i < goals.nobj; i++) {
                        const CareerObj *ob = &goals.obj[i];
                        int counted = !(goals.ch->flags & CHAL_LINE) && goals.need[i] > 1 && ob->type != OBJ_LINE;
                        if (counted) snprintf(t1, sizeof(t1), "%.24s  %d/%d", goals_obj_text(&goals, i), goals.prog[i] < goals.need[i] ? goals.prog[i] : goals.need[i], goals.need[i]);
                        else snprintf(t1, sizeof(t1), "%.30s", goals_obj_text(&goals, i));
                        hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, t1, goals.done[i] ? GRN : UI_WHITE); cy += 15.0f;
                    }
                    if (goals.phase == GOAL_ACTIVE && (goals.ch->flags & CHAL_LINE)) { hud_text_right(H, &g_ui, H->f_s, SCR_W - 8, cy, "one line", UI_RGBA(200, 200, 200, 255)); cy += 15.0f; }
                    if (kind == SESSION_TUTORIAL && (goals.phase == GOAL_ACTIVE || goals.phase == GOAL_COUNTDOWN_PHASE)) {        /* the tip for the objective that is next, with the buttons of the player's mapping */
                        const char *tip = tutorial_tip(goals.chal, tutorial_current_objective(goals.done, goals.nobj));
                        if (tip) {
                            char ex[160], *ln = ex;
                            float ty = 6.0f;
                            settings_expand_buttons(&g_set, tip, ex, sizeof(ex));
                            for (;;) {
                                char *nl = strchr(ln, '\n');
                                if (nl) *nl = 0;
                                hud_text(H, &g_ui, H->f_s, 8, ty, ln, UI_WHITE);
                                ty += 15.0f;
                                if (!nl) break;
                                ln = nl + 1;
                            }
                        }
                    }
                    if (goals.phase == GOAL_COUNTDOWN_PHASE) {
                        hud_digit(H, &g_ui, (int)ceilf(goals.countdown));
                    } else if (goals.phase == GOAL_ACTIVE && goals.t < 0.8f) {
                        hud_text_center(H, &g_ui, H->f_xl, SCR_W / 2.0f, 82, 1.5f, "GO!", GRN);
                    } else if (goals.phase == GOAL_WON) {
                        hud_text_center(H, &g_ui, H->f_xl, SCR_W / 2.0f, 82, 1.5f, "Complete!", GRN);
                        if (goals.result_t > 1.5f) hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, 132, 1.0f, "X  CONTINUE", UI_WHITE);
                        for (int k = 0; k < n_msg && kind != SESSION_FREE; k++)
                            hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, 152.0f + 15.0f * (float)k, 1.0f, msg[k], YEL);
                    } else if (goals.phase == GOAL_LOST) {
                        hud_text_center(H, &g_ui, H->f_xl, SCR_W / 2.0f, 82, 1.5f, "Failed", RED);
                        if (goals.result_t > 1.5f) hud_text_center(H, &g_ui, H->f_s, SCR_W / 2.0f, 132, 1.0f, "X  CONTINUE", UI_WHITE);
                    }
                } else if (near_idx >= 0) {                                      /* standing in a beam */
                    snprintf(t1, sizeof(t1), "START: %.34s%s", career_str(&career, career.chal[chal_list[near_idx]].name_off), chal_done[chal_list[near_idx] < SAVE_NCHAL ? chal_list[near_idx] : 0] ? " (done)" : "");
                    hud_text_center(H, &g_ui, H->f_m, SCR_W / 2.0f, SCR_H - 62, 1.0f, t1, YEL);
                }
                {                                                                // the information lines chosen in Options > Information, top left, one under the other
                    const unsigned char *inf = g_set.info;
                    const uint32_t IY = UI_RGBA(255, 214, 40, 255), IC = UI_RGBA(110, 210, 255, 255), IG = UI_RGBA(120, 255, 120, 255), IW = UI_RGBA(235, 235, 235, 255), IO = UI_RGBA(255, 170, 70, 255);
                    float iy = kind == SESSION_TUTORIAL ? 44.0f : 6.0f;           /* (a tutorial's tip takes the top left) */
                    const float IL = 15.0f;
                    if (inf[INFO_FPS]) { snprintf(hud, sizeof(hud), "FPS %d", (int)(fps + 0.5f)); hud_text(H, &g_ui, H->f_s, 8, iy, hud, IY); iy += IL; }
                    if (inf[INFO_SPEED]) { char sp[24]; settings_format_speed(&g_set, fabsf(sk.speed), sp, sizeof(sp)); snprintf(hud, sizeof(hud), "Speed %s", sp); hud_text(H, &g_ui, H->f_s, 8, iy, hud, IY); iy += IL; }
                    if (inf[INFO_CPU_GPU]) {
                        snprintf(hud, sizeof(hud), "CPU %d.%d  GPU %d.%d  Phys %d.%d  Skin %d.%d ms", cpu_us / 1000, (cpu_us / 100) % 10, gpu_us / 1000, (gpu_us / 100) % 10,
                                 phys_us / 1000, (phys_us / 100) % 10, skin_us / 1000, (skin_us / 100) % 10);
                        hud_text(H, &g_ui, H->f_s, 8, iy, hud, IO); iy += IL;
                    }
                    if (inf[INFO_HEAP]) { snprintf(hud, sizeof(hud), "Heap used %d K  biggest free %d K", heap_used_kb, heap_free_kb); hud_text(H, &g_ui, H->f_s, 8, iy, hud, IW); iy += IL; }
                    if (inf[INFO_POSITION]) {                          /* where the rider is (metres): to report places on the map - and the heading in degrees */
                        char fx[16], fy[16], fz[16];
                        fmt1(fx, sizeof(fx), sk.pos[0]); fmt1(fy, sizeof(fy), sk.pos[1]); fmt1(fz, sizeof(fz), sk.pos[2]);
                        snprintf(hud, sizeof(hud), "X %s  Y %s  Z %s  Heading %d", fx, fy, fz, (int)(sk.heading * 57.29578f + 360.0f) % 360);
                        hud_text(H, &g_ui, H->f_s, 8, iy, hud, IW); iy += IL;
                    }
                    if (inf[INFO_TRICK]) {                             /* the trick state with the number of its pose clip (Square / Circle change the clip of a grind or manual) */
                        hud[0] = 0;
                        if (sk.grinding) snprintf(hud, sizeof(hud), "%s %s  pose %d", (regular ? !sk.grind_bs : sk.grind_bs) ? "BS" : "FS", tricks.grind[sk.grind_type].name, tricks.grind[sk.grind_type].pose);
                        else if (flip_ready_dir >= 0) snprintf(hud, sizeof(hud), "%s%s  pose %d", ctl.nollie ? "NOLLIE " : "", tricks.flip[flip_ready_dir].name, ctl.nollie && tricks.flip[flip_ready_dir].alt >= 0 ? tricks.flip[flip_ready_dir].alt : tricks.flip[flip_ready_dir].pose);
                        else if (ctl.nollie) snprintf(hud, sizeof(hud), "NOLLIE");
                        else if (sk.wallriding) snprintf(hud, sizeof(hud), "%s", tricks.move[sk.wall_side > 0.0f ? MOVE_FS_WALLRIDE : MOVE_BS_WALLRIDE].name);
                        else if (sk.sliding) snprintf(hud, sizeof(hud), "%s", tricks.move[sk.slide_dir > 0.0f ? MOVE_BS_POWERSLIDE : MOVE_FS_POWERSLIDE].name);
                        else if (!sk.grounded && !sk.flip && !sk.grab && fabsf(sk.spin_ang) > 0.1f) snprintf(hud, sizeof(hud), "%s SPIN %d", sk.spin_dir > 0.0f ? "BS" : "FS", (int)(fabsf(sk.spin_ang) * 57.29578f + 0.5f));
                        else if (sk.flip || sk.grab) {
                            const GrindType *pt = sk.flip ? &tricks.flip[sk.flip - 1] : &tricks.grab[sk.grab - 1];
                            int nol = sk.flip && sk.flip_nollie && pt->alt >= 0;
                            snprintf(hud, sizeof(hud), "%s%s  pose %d", nol ? "NOLLIE " : "", pt->name, nol ? pt->alt : pt->pose);
                        } else if (sk.manual) snprintf(hud, sizeof(hud), "%s  pose %d", tricks.manual[sk.manual - 1].name, tricks.manual[sk.manual - 1].pose);
                        else if (sk.bail_t > 0.0f) snprintf(hud, sizeof(hud), "BAIL");
                        else snprintf(hud, sizeof(hud), "rolling");
                        hud_text(H, &g_ui, H->f_s, 8, iy, hud, sk.bail_t > 0.0f ? RED : IC); iy += IL;
                    }
                    if (inf[INFO_NUB]) {                               /* the nub and the last flick: for tuning the gesture on the real hardware */
                        static const char *dn[8] = { "UP", "UP-RIGHT", "RIGHT", "DOWN-RIGHT", "DOWN", "DOWN-LEFT", "LEFT", "UP-LEFT" };
                        char fax[16], fay[16];
                        fmt1(fax, sizeof(fax), ax); fmt1(fay, sizeof(fay), ay);
                        int age = ctl.flick_dir >= 0 ? (int)((ctl.t - ctl.flick_t) * 1000.0f) : -1;
                        if (age > 9999) age = 9999;
                        if (age >= 0) snprintf(hud, sizeof(hud), "Nub %s %s  flick %s %d ms ago", fax, fay, dn[ctl.flick_dir], age);
                        else snprintf(hud, sizeof(hud), "Nub %s %s  no flick yet", fax, fay);
                        hud_text(H, &g_ui, H->f_s, 8, iy, hud, ctl.flick_new || (age >= 0 && age < 300) ? IG : IW); iy += IL;
                    }
                    if (inf[INFO_RAIL]) {                              /* the nearest rail or ledge and why the board would (not) lock on: sideways distance D, height H over it (+ = above) and the reason */
                        if (probe.found) {
                            static const char *why[] = { "OK", "on ground", "bail", "wait", "too far", "too low", "too high", "wrong side", "too slow", "across" };
                            char fd[16], fh[16];
                            fmt1(fd, sizeof(fd), probe.dist); fmt1(fh, sizeof(fh), probe.dy);
                            snprintf(hud, sizeof(hud), "%s %u  D %s  H %s  %s", probe.kind == RAIL_KIND_LEDGE ? "Ledge" : "Rail", probe.rail, fd, fh, why[probe.reason]);
                            hud_text(H, &g_ui, H->f_s, 8, iy, hud, probe.reason == GP_OK ? IG : IC);
                        } else {
                            hud_text(H, &g_ui, H->f_s, 8, iy, "No rail near", UI_RGBA(170, 170, 170, 255));
                        }
                        iy += IL;
                    }
                    if (inf[INFO_SURFACE]) {
                        static const char *mn[4] = { "concrete", "metal", "wood", "concrete" }, *rn[4] = { "medium", "rough", "smooth", "medium" };
                        snprintf(hud, sizeof(hud), "Surface %s, %s", mn[surf & 3], rn[(surf >> 2) & 3]);
                        hud_text(H, &g_ui, H->f_s, 8, iy, hud, IW); iy += IL;
                    }
                    if (inf[INFO_CLIP]) {
                        snprintf(hud, sizeof(hud), "Clip %d tris  %d us%s", nc_n, nc_us, clip_on ? "" : "  (OFF)");
                        hud_text(H, &g_ui, H->f_s, 8, iy, hud, IO); iy += IL;
                    }
                }
                ui_gu_end();
                if (sk.manual) {                                              /* balance bar (manuals only: grinds have no balancing). Right = tipping over (back for a manual, onto the nose for a
                                                                               * nose manual: bail), left = the board comes down */
                    float ln = sk.lean < -1.0f ? -1.0f : (sk.lean > 1.0f ? 1.0f : sk.lean);
                    if (sk.manual) ln = -ln;
                    float al = fabsf(ln);
                    uint32_t nc = al < 0.5f ? 0xFF40FF40 : (al < 0.8f ? 0xFF00E0FF : 0xFF4040FF);
                    draw_rect(SCR_W / 2 - 104, SCR_H - 24, 208, 12, 0xFF303030);
                    draw_rect(SCR_W / 2 - 50, SCR_H - 22, 100, 8, 0xFF205020);
                    draw_rect(SCR_W / 2 - 1, SCR_H - 26, 2, 16, 0xFFA0A0A0);
                    draw_rect(SCR_W / 2 - (int)(ln * 100.0f) - 4, SCR_H - 28, 8, 20, nc);
                }
            } else {
                if (g_set.info[INFO_FPS]) {
                    snprintf(hud, sizeof(hud), "FPS %d", (int)(fps + 0.5f));
                    draw_text(HX, hy, HS, hud, 0xFF00FFFF); hy += LH;                    /* yellow */
                }
                if (mode == MODE_FREE) {
                    draw_text(HX, hy, HS, "FREE", 0xFF80FF80);
                } else {
                    snprintf(hud, sizeof(hud), "POSE %d/%d", pose_idx, (int)S.anims.nanims - 1);
                    draw_text(HX, hy, HS, hud, 0xFFFFFFFF);
                }
            }
        }

        if (music_msg_t > 0.0f) {
            if (music_msg_idx > 0) snprintf(hud, sizeof(hud), "MUSIC %d", music_msg_idx);
            else snprintf(hud, sizeof(hud), music_msg_idx == -1 ? "MUSIC OFF" : "MUSIC ON");
            draw_text(4, SCR_H - 14, 2, hud, 0xFFFFC080);
        }

        if (paused) {
            ui_gu_begin();
            if (opt_open) fe_options_draw(&g_fe, 1);
            else fe_draw_menu(&g_fe, res_menu ? (sign_offer ? "SPONSOR" : (goals.phase == GOAL_WON ? "COMPLETE" : "FAILED")) : "PAUSED", pause_name, NULL, pause_n, pause_sel);
            ui_gu_end();
        }

        sceGuFinish();
        uint64_t t_cpu = sceKernelGetSystemTimeWide();
        sceGuSync(0, 0);
        uint64_t t_gpu = sceKernelGetSystemTimeWide();
        cpu_us = (int)(t_cpu - frame_t0);
        gpu_us = (int)(t_gpu - t_cpu);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
        if (quit_menu)
            break;
    }
    if (have_sound)
        for (int ch = 0; ch < MIX_LOOPS; ch++)
            mixer_loop(&mixer, ch, NULL, 0.0f, 1.0f);
    opps_unload();
    unload_level(&L);
    unload_skater(&S);
    unload_board(&B);
    return tut_won;
}
#undef mixer
#undef sfx
#undef career

/* ---- the skater on the select screen: his model is loaded once the choice has rested for a moment, then he stands in the riding pose and turns slowly ---------------------------- */
static struct { Skater S; Board B; int loaded, have_board, idx, want; float rest; } g_pv = { .idx = -1, .want = -1 };

static void preview_free(void)
{
    if (g_pv.loaded) {
        unload_skater(&g_pv.S);
        g_pv.loaded = 0;
    }
    if (g_pv.have_board) {
        unload_board(&g_pv.B);
        g_pv.have_board = 0;
    }
    g_pv.idx = g_pv.want = -1;
}

static void preview_update(const char *base, float dt)
{
    if (g_fe.state == FE_STATE_CUSTOM) {                                              /* the Customize screen: the skater being made, rebuilt whenever the choice changes */
        if (!g_fe.cpak || (!g_fe.cust_dirty && g_pv.idx == -2)) return;
        g_fe.cust_dirty = 0;
        if (g_pv.loaded) { unload_skater(&g_pv.S); g_pv.loaded = 0; }
        if (load_custom_skater(&g_pv.S, base, &g_fe.cust)) g_pv.loaded = 1;
        if (g_pv.have_board) unload_board(&g_pv.B);
        load_board(&g_pv.B, base); g_pv.have_board = 1;
        apply_board_art(&g_pv.B, base, &g_fe.cust);
        sceKernelDcacheWritebackAll();
        g_pv.idx = -2; g_pv.want = -2;
        return;
    }
    if (g_fe.state != FE_STATE_SKATER || g_fe.nskater <= 0)
        return;
    int want = g_fe.skater_sel;
    if (want != g_pv.want) { g_pv.want = want; g_pv.rest = 0.0f; }
    else g_pv.rest += dt;
    if (g_pv.rest > 0.12f && g_pv.idx != want) {
        if (g_pv.loaded) { unload_skater(&g_pv.S); g_pv.loaded = 0; }
        if (g_fe.skater[want].index == PROFILE_CUSTOM_SKATER && load_custom_skater(&g_pv.S, base, &g_profile.custom)) {
            g_pv.loaded = 1;
        } else {
            snprintf(skater_file, sizeof(skater_file), "data/skater_%s.skt", g_fe.skater[want].index == PROFILE_CUSTOM_SKATER ? "aaron" : g_fe.skater[want].key);
            load_skater_file(&g_pv.S, base, skater_file);
            g_pv.loaded = 1;
        }
        if (g_pv.have_board) unload_board(&g_pv.B);
        load_board(&g_pv.B, base); g_pv.have_board = 1;
        if (g_fe.skater[want].index == PROFILE_CUSTOM_SKATER) apply_board_art(&g_pv.B, base, &g_profile.custom);
        sceKernelDcacheWritebackAll();
        g_pv.idx = want;
    }
}

static void preview_draw(const int rect[4])
{
    int customizing = g_fe.state == FE_STATE_CUSTOM;
    if ((g_fe.state != FE_STATE_SKATER && !customizing) || !g_pv.loaded || !g_pv.have_board || (!customizing && g_pv.idx != g_fe.skater_sel))
        return;
    static SkelFrame frame;
    static BoardFrame bframe;
    static float mats[SKEL_NBONES][12];
    animset_sample(&g_pv.S.anims, POSE_RIDE, 5.0f, &frame);
    animset_board(&g_pv.S.anims, POSE_RIDE, 0.0f, &bframe);
    skel_matrices(g_pv.S.bones, &frame, g_pv.S.hdr->scale, mats);
    skin_skater(&g_pv.S, mats);
    float c = cosf(g_fe.spin), sn = sinf(g_fe.spin);
    float body[16] = { c, 0, -sn, 0,  0, 1, 0, 0,  sn, 0, c, 0,  0, 0, 0, 1 }, bm34[12], bm[16], bmat[16];
    if (!customizing && settings_riding_regular(&g_set, career_skater_regular(&g_career, g_fe.skater[g_fe.skater_sel].index))) skate_mirror_matrix(body);       /* regular riders are shown as they ride */
    board_matrix(&bframe, g_pv.S.hdr->scale, bm34);
    mat4_from_34(bm, bm34);
    mat4_mul(bmat, body, bm);
    float eye[3] = { 0.0f, 1.05f, 4.1f }, ctr[3] = { 0.0f, 0.92f, 0.0f }, upv[3] = { 0.0f, 1.0f, 0.0f }, view[16];
    nc_lookat(view, eye, ctr, upv);
    int rx = rect[0], ry = rect[1], rw = rect[2], rh = rect[3];
    sceGuViewport(2048 + (rx + rw / 2 - SCR_W / 2), 2048 + (ry + rh / 2 - SCR_H / 2), rw, rh);        /* the 3D skater only draws inside his panel */
    sceGuScissor(rx, ry, rx + rw, ry + rh);
    sceGuClear(GU_DEPTH_BUFFER_BIT);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(30.0f, (float)rw / (float)rh, 0.5f, 40.0f);
    set_view(view);
    sceGuEnable(GU_DEPTH_TEST);
    memcpy(gum_matrix, body, sizeof(body));
    sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
    sceGumUpdateMatrix();
    draw_skater(&g_pv.S, 1);
    memcpy(gum_matrix, bmat, sizeof(bmat));
    sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
    sceGumUpdateMatrix();
    draw_board(&g_pv.B, 1);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuScissor(0, 0, SCR_W, SCR_H);
}

/* ---- the front end ------------------------------------------------------------------------------------- */
static unsigned fe_buttons(unsigned b, float ax, float ay)
{
    unsigned r = 0;
    if (b & PSP_CTRL_UP) r |= FE_UP;
    if (b & PSP_CTRL_DOWN) r |= FE_DOWN;
    if (b & PSP_CTRL_LEFT) r |= FE_LEFT;
    if (b & PSP_CTRL_RIGHT) r |= FE_RIGHT;
    if (b & PSP_CTRL_CROSS) r |= FE_OK;
    if (b & PSP_CTRL_CIRCLE) r |= FE_BACK;
    if (b & PSP_CTRL_START) r |= FE_START;
    if (b & PSP_CTRL_LTRIGGER) r |= FE_L;
    if (b & PSP_CTRL_RTRIGGER) r |= FE_R;
    if (b & PSP_CTRL_SQUARE) r |= FE_SQUARE;
    if (b & PSP_CTRL_TRIANGLE) r |= FE_TRIANGLE;
    if (ay < -0.6f) r |= FE_UP;
    if (ay > 0.6f) r |= FE_DOWN;
    if (ax < -0.6f) r |= FE_LEFT;
    if (ax > 0.6f) r |= FE_RIGHT;
    return r;
}

static void end_frame(void)
{
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* one frame saying that the game is loading */
static void draw_loading(void)
{
    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(0xFF000000);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    ui_gu_begin();
    ui_image_cover(&g_ui, g_fe.i_dim, UI_WHITE);
    ui_text_center(&g_ui, g_fe.f_l, UI_W / 2, 118, 1.0f, "LOADING...", UI_WHITE);
    ui_gu_end();
    end_frame();
}

/* ---- the story scenes: the guest in his level behind the dialogue pages (src/scene.c, data/scenes.pak) ----------------------------------------------------------------------- */
static struct {
    int scene;                                  /* the scene set up (0 = none) */
    int ok;                                     /* it has a 3D view */
    uint8_t *pak_buf; ScenePak pak; int pak_state;       /* pak_state: 0 not tried, 1 loaded, -1 not there */
    int level_id; int have_level; Level L;
    int skater; int have_skater; Skater S;
    int have_board; Board B;
    ScenePlayer sp;
    float pos[3];
    SkelBone bones[SKEL_NBONES];
    int page;
} g_sc;

static void scene_free_level(void)
{
    if (g_sc.have_level) { unload_level(&g_sc.L); g_sc.have_level = 0; }
    g_sc.level_id = -1;
}

static void scene_release(void)
{
    scene_free_level();
    if (g_sc.have_skater) { unload_skater(&g_sc.S); g_sc.have_skater = 0; }
    if (g_sc.have_board) { unload_board(&g_sc.B); g_sc.have_board = 0; }
    if (g_sc.pak_buf) { free(g_sc.pak_buf); g_sc.pak_buf = NULL; }
    g_sc.pak_state = 0;
    g_sc.scene = 0;
    g_sc.ok = 0;
    g_fe.scene3d = 0;
}

/* the guest of the current line: his skater file and the skeleton the clips were made for */
static void scene_guest(const char *base)
{
    int idx = g_sc.sp.guest;
    if (g_sc.have_skater && g_sc.skater == idx) return;
    if (g_sc.have_skater) { unload_skater(&g_sc.S); g_sc.have_skater = 0; }
    char file[96];
    snprintf(file, sizeof(file), "data/skater_%s.skt", idx >= 0 && (uint32_t)idx < g_career.nskaters ? career_str(&g_career, g_career.skater[idx].key_off) : "aaron");
    if (!file_exists(base, file)) snprintf(file, sizeof(file), "data/skater_aaron.skt");
    load_skater_file(&g_sc.S, base, file);
    scenepak_bones(&g_sc.pak, g_sc.S.bones, g_sc.bones);
    g_sc.skater = idx;
    g_sc.have_skater = 1;
    sceKernelDcacheWritebackAll();
}

static void scene_setup(const char *base, int scene)
{
    g_sc.scene = scene;
    g_sc.ok = 0;
    g_sc.page = -1;
    if (g_sc.pak_state == 0) {
        size_t n = 0;
        g_sc.pak_buf = load_file_opt(base, "data/scenes.pak", &n);
        if (g_sc.pak_buf && scenepak_load(&g_sc.pak, g_sc.pak_buf, n)) g_sc.pak_state = 1;
        else { free(g_sc.pak_buf); g_sc.pak_buf = NULL; g_sc.pak_state = -1; }
    }
    if (g_sc.pak_state != 1) return;
    sceneplayer_init(&g_sc.sp, &g_sc.pak, scene, (uint32_t)(sceKernelGetSystemTimeWide() & 0xFFFFFF) | 1);
    if (!sceneplayer_active(&g_sc.sp)) return;                                      /* a scene without a guest: the text only */
    int lv = scenepak_level(&g_sc.pak, scene), chal = g_fe.story.chal;
    if (lv < 0 && chal >= 0 && (uint32_t)chal < g_career.nchal) lv = g_career.chal[chal].level;      /* (a scene that names no place is filmed where its event is) */
    if (lv < 0) lv = career_level_by_stem(&g_career, "amphitheatre");
    const char *zip = NULL;
    for (uint32_t i = 0; i < g_career.nlevels; i++) if (g_career.level[i].id == lv) zip = career_str(&g_career, g_career.level[i].zip_off);
    if (!zip) return;
    if (!g_sc.have_level || g_sc.level_id != lv) {
        char path[96], saved[96];
        snprintf(path, sizeof(path), "data/%s.lvl", zip);
        for (char *q = path + 5; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
        if (!file_exists(base, path)) return;
        scene_free_level();
        draw_loading();
        memcpy(saved, level_file, sizeof(saved));
        snprintf(level_file, sizeof(level_file), "%s", path);
        load_level(&g_sc.L, base);
        memcpy(level_file, saved, sizeof(level_file));
        g_sc.have_level = 1;
        g_sc.level_id = lv;
    }
    int slot = chal >= 0 && (uint32_t)chal < g_career.nchal && g_career.chal[chal].level == lv ? g_career.chal[chal].slot : -1;
    if (!scene_guest_place(&g_sc.L.objs, slot, g_sc.pos)) {
        float h;
        find_spawn(&g_sc.L, &g_sc.pos[0], &g_sc.pos[1], &g_sc.pos[2], &h);
    }
    if (!g_sc.have_board) { load_board(&g_sc.B, base); g_sc.have_board = 1; }
    g_sc.ok = 1;
}

static void scene_update(const char *base, float dt)
{
    int want = g_fe.state == FE_STATE_STORY && g_fe.story.kind == STORY_SCENE ? g_fe.story.scene : 0;
    if (want != g_sc.scene) {
        if (!want) { scene_release(); return; }
        scene_setup(base, want);
    }
    g_fe.scene3d = g_sc.scene != 0 && g_sc.ok;
    if (!g_fe.scene3d) return;
    g_sc.sp.flip = g_fe.scene_flip;
    if (g_fe.page != g_sc.page) {
        g_sc.page = g_fe.page;
        int line = g_fe.page >= 0 && g_fe.page < g_fe.npages ? g_fe.pages[g_fe.page].first : 0;
        sceneplayer_line(&g_sc.sp, line, career_scene_nlines(&g_career, want));
        scene_guest(base);
    }
    sceneplayer_update(&g_sc.sp, dt);
}

static void scene_draw(void)
{
    if (!g_fe.scene3d || !g_sc.have_skater || !g_sc.have_level || !g_sc.have_board) return;
    static SkelFrame frame;
    static BoardFrame bframe;
    static float mats[SKEL_NBONES][12];
    static NcTri nc_out[NC_MAX];
    sceneplayer_pose(&g_sc.sp, &frame, &bframe);
    skel_matrices(g_sc.bones, &frame, g_sc.S.hdr->scale, mats);
    skin_skater(&g_sc.S, mats);
    float body[16], bm34[12], bm[16], bmat[16];
    sceneplayer_body(&g_sc.sp, g_sc.pos, body);
    board_matrix(&bframe, g_sc.S.hdr->scale, bm34);
    mat4_from_34(bm, bm34);
    mat4_mul(bmat, body, bm);
    float eye[3], ctr[3], upv[3] = { 0.0f, 1.0f, 0.0f }, view[16];
    sceneplayer_camera(&g_sc.sp, g_sc.pos, eye, ctr);
    nc_lookat(view, eye, ctr, upv);
    int nc_n = nc_gather(&g_sc.L.nc, view, eye, NC_RADIUS, NEAR_PLANE, FOCAL_PX, nc_out, NC_MAX);
    sceGuClearColor(0xFFE0B070);                                                    /* the sky of the game */
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(FOV_Y_DEG, (float)SCR_W / (float)SCR_H, NEAR_PLANE, 3000.0f);
    set_view(view);
    sceGuEnable(GU_DEPTH_TEST);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumUpdateMatrix();
    draw_level(&g_sc.L, 0);
    draw_clipped(&g_sc.L, nc_out, nc_n, 0);
    set_view(view);
    memcpy(gum_matrix, body, sizeof(body));
    sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
    sceGumUpdateMatrix();
    draw_skater(&g_sc.S, 0);
    memcpy(gum_matrix, bmat, sizeof(bmat));
    sceGumLoadMatrix((const ScePspFMatrix4 *)gum_matrix);
    sceGumUpdateMatrix();
    draw_board(&g_sc.B, 0);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
}

/* the title, the menus: returns when a game is to be started (the choices are in g_fe) */
static void run_frontend(const char *base)
{
    SceCtrlData pad0;
    sceCtrlPeekBufferPositive(&pad0, 1);
    unsigned prev = fe_buttons(pad0.Buttons, ((int)pad0.Lx - 128) / 128.0f, ((int)pad0.Ly - 128) / 128.0f);      /* a button still held from the game (the Cross of QUIT TO MENU) must not count as a press */
    uint64_t last = sceKernelGetSystemTimeWide();
    for (;;) {
        uint64_t now = sceKernelGetSystemTimeWide();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        float ax = ((int)pad.Lx - 128) / 128.0f, ay = ((int)pad.Ly - 128) / 128.0f;
        FeInput in;
        in.held = fe_buttons(pad.Buttons, ax, ay);
        in.pressed = in.held & ~prev;
        prev = in.held;
        g_keep_cpak = g_fe.state == FE_STATE_CUSTOM;                                          // the Customize screen needs its parts; nothing else keeps them
        if (g_keep_cpak) { cpak_acquire(base); g_fe.cpak = g_cpak_buf ? &g_cpak : NULL; }
        else { if (g_cpak_buf) cpak_release(); g_fe.cpak = NULL; }
        g_fe.have_music = g_have_music;
        g_fe.regular = settings_riding_regular(&g_set, career_skater_regular(&g_career, g_fe.mode == FE_MODE_CAREER ? g_profile.skater : (g_fe.nskater > 0 ? g_fe.skater[g_fe.skater_sel].index : 12)));
        int go = fe_update(&g_fe, &in, dt);
        handle_profile_requests(base);
        scene_update(base, dt);
        if (g_have_sound) {
            if (g_fe.ev_move) sfx_event(&g_sfx, &g_mixer, SFX_EV_MOVE);
            if (g_fe.ev_ok) sfx_event(&g_sfx, &g_mixer, SFX_EV_ENTER);
            if (g_fe.ev_back) sfx_event(&g_sfx, &g_mixer, SFX_EV_BACK);
        }
        g_fe.ev_move = g_fe.ev_ok = g_fe.ev_back = 0;
        if (g_fe.opt.changed) { save_settings(base); g_fe.opt.changed = 0; }
        if (g_fe.ev_profile) { save_profile(base); g_fe.ev_profile = 0; }       /* a skater was chosen, an event skipped */
        sceGuStart(GU_DIRECT, display_list);
        sceGuClearColor(0xFF000000);
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        preview_update(base, dt);
        scene_draw();
        ui_gu_begin();
        fe_draw_back(&g_fe);
        ui_gu_end();
        preview_draw(g_fe.preview);
        ui_gu_begin();
        fe_draw_front(&g_fe);
        ui_gu_end();
        end_frame();
        if (go) {
            scene_release();                                                                  /* (the level of the scene makes room for the game's) */
            return;
        }
    }
}

/* ---- the intro movies (data/ealogo.vx, data/tempintro.vx: the original's Actimagine VX video, decoded by src/vx.c) -------------------------------------------------------------------------- */
#define VX_RING 6
static struct { uint32_t __attribute__((aligned(16))) rgba[256 * 256]; int16_t pcm[1600]; int npcm; } g_slot[VX_RING];
static Vx g_vx;

/* plays one movie full screen (256 x 192 scaled to the screen's height). The picture follows the clock (frame = time * fps) and always shows the newest decoded frame; a few frames are decoded ahead
 * and the sound of the NEXT frame is queued as a frame is shown. At most 2 frames are decoded per pass so the pad is read and the picture redrawn even if the CPU cannot keep up (a movie that
 * is too slow is cut short after 3 s without 15 decoded frames). What happened is appended to data/vx_log.txt. Start / Cross / Circle skip it: returns 1 if the player did. */
static int play_movie(const char *base, const char *name)
{
    size_t n = 0;
    uint8_t *file = load_file_opt(base, name, &n);
    if (!file)
        return 0;
    if (!vx_open(&g_vx, file, n)) {
        free(file);
        return 0;
    }
    int total = vx_frames(&g_vx);
    float fps = vx_fps(&g_vx);
    int decoded = 0, shown = -1, apushed = 0, skipped = 0, apending = 0, passes = 0, behind = 0, too_slow = 0;
    uint32_t dec_sum = 0, dec_max = 0;
    uint64_t t0 = sceKernelGetSystemTimeWide();
    if (g_have_sound)
        mixer_stream_start(&g_mixer, (uint32_t)vx_audio_rate(&g_vx), 0.9f);
    unsigned prev = 0xFFFFFFFFu;
    float el = 0.0f;
    for (;;) {
        passes++;
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned pressed = pad.Buttons & ~prev;
        prev = pad.Buttons;
        if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) { skipped = 1; break; }
        el = (float)(sceKernelGetSystemTimeWide() - t0) / 1000000.0f;
        int due = (int)(el * fps);
        if (due >= total)
            break;
        if (el > 3.0f && (decoded < 15 || decoded * 2 < due)) { too_slow = 1; break; }              /* the CPU cannot keep up (under half speed): give up on the movie */
        for (int budget = 2; budget > 0 && decoded < total && decoded < due + 4; budget--) {      /* decode ahead, but never for long */
            int slot = decoded % VX_RING;
            uint64_t d0 = sceKernelGetSystemTimeWide();
            int ok = vx_next(&g_vx, g_slot[slot].rgba, 256, g_slot[slot].pcm, 1600, &g_slot[slot].npcm);
            uint32_t d = (uint32_t)(sceKernelGetSystemTimeWide() - d0);
            dec_sum += d;
            if (d > dec_max) dec_max = d;
            if (!ok) { total = decoded; break; }
            decoded++;
        }
        shown = due < decoded ? due : decoded - 1;                              /* the frame that is due, or the newest there is */
        if (shown < due) behind++;
        while (g_have_sound && decoded > 0 && apushed <= shown + 1 && apushed < decoded) {      /* the sound goes one frame ahead of the picture */
            int s2 = apushed % VX_RING;
            if (apending < g_slot[s2].npcm) {
                apending += mixer_stream_push(&g_mixer, g_slot[s2].pcm + apending, g_slot[s2].npcm - apending);
                if (apending < g_slot[s2].npcm) break;                           /* the buffer is full: next time */
            }
            apending = 0;
            apushed++;
        }
        sceGuStart(GU_DIRECT, display_list);
        sceGuClearColor(0xFF000000);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
        if (shown >= 0) {
            sceKernelDcacheWritebackRange(g_slot[shown % VX_RING].rgba, 256 * 192 * 4);
            ui_gu_begin();
            UiTex tex = { (const uint8_t *)g_slot[shown % VX_RING].rgba, NULL, 256, 256, 1 };
            float w = (float)UI_H * (float)vx_width(&g_vx) / (float)vx_height(&g_vx);
            ui_gu_quad(NULL, &tex, 0.0f, 0.0f, (float)vx_width(&g_vx), (float)vx_height(&g_vx), ((float)UI_W - w) * 0.5f, 0.0f, ((float)UI_W + w) * 0.5f, (float)UI_H, UI_WHITE, 1);
            float fade = 0.0f;                                                   /* a quarter second from / to black at the ends */
            if (el < 0.25f) fade = 1.0f - el / 0.25f;
            float left = (float)total / fps - el;
            if (left < 0.25f) fade = 1.0f - left / 0.25f;
            if (fade > 0.0f) ui_gu_rect(NULL, 0, 0, UI_W, UI_H, UI_RGBA(0, 0, 0, (int)(255.0f * (fade > 1.0f ? 1.0f : fade))));
            ui_gu_end();
        }
        end_frame();
    }
    uint32_t starved = g_have_sound ? mixer_stream_starved(&g_mixer) : 0;
    if (g_have_sound)
        mixer_stream_stop(&g_mixer);
    {                                                                            /* the log: all integers (no float printf) */
        char path[320];
        snprintf(path, sizeof(path), "%sdata/vx_log.txt", base);
        FILE *lf = fopen(path, "a");
        if (lf) {
            fprintf(lf, "%s: %d frames, decoded %d in %d ms of playing (clock %d ms); decode time per frame: average %u us, worst %u us; %d passes, %d behind the clock; sound starved %u output frames; %s%s\n",
                    name, vx_frames(&g_vx), decoded, (int)(el * 1000.0f), (int)(el * 1000.0f), decoded ? dec_sum / (uint32_t)decoded : 0u, dec_max, passes, behind, starved,
                    skipped ? "skipped by the player" : (too_slow ? "CUT SHORT: too slow" : "played to the end"), "");
            fclose(lf);
        }
    }
    free(file);
    return skipped ? 1 : (too_slow ? 2 : 0);
}

/* the EA logo, then the intro movie; any of Start / Cross / Circle skips what is left; a movie the CPU cannot play in time ends the intro */
static void play_intro(const char *base)
{
    if (play_movie(base, "data/ealogo.vx"))              /* skipped, or too slow for this CPU: no more movies */
        return;
    play_movie(base, "data/tempintro.vx");
}

static int file_exists(const char *base, const char *name)
{
    char path[320];
    snprintf(path, sizeof(path), "%s%s", base, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);

    char base[256] = "";
    if (argc > 0 && argv && argv[0]) {
        strncpy(base, argv[0], sizeof(base) - 1);
        char *slash = strrchr(base, '/');
        if (slash)
            slash[1] = 0;
        else
            base[0] = 0;
    }

    pspDebugScreenInit();
    gfx_init();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    /* sound: the DS game's own effects (data/sounds.snd), mixed in software and played by a thread; the game runs silent if the file or the hardware channel is missing */
    char snd_path[300];
    snprintf(snd_path, sizeof(snd_path), "%s%s", base, SOUNDS_FILE);
    mixer_init(&g_mixer, 0.7f);
    g_have_sound = soundbank_load(&g_sbank, snd_path);
    if (g_have_sound) {
        sfx_init(&g_sfx, &g_sbank);
        g_have_sound = audio_start(&g_mixer);
    }
    g_have_music = MUSIC_ENABLED && g_have_sound;
    if (g_have_music)
        audio_music_init(&g_mixer, base);            /* background music: data/music0.mus ..., loaded in the background */

    /* the front end's pictures, fonts and the career data (skater and location lists) */
    size_t n = 0;
    uint8_t *ub = load_file(base, "data/ui.pak", &n);
    if (!uipak_load(&g_pak, ub, n))
        fatal("bad file", "data/ui.pak");
    uint8_t *cb = load_file(base, "data/career.dat", &n);
    if (!career_load(&g_career, cb, n))
        fatal("bad file", "data/career.dat");
    {                                                /* the recorded runs of the original's skaters (opponents, ambient riders): the game plays without them if the file is missing */
        size_t an = 0;
        uint8_t *ab = load_file_opt(base, "data/ai.pak", &an);
        g_have_ai = ab && ghost_pak_load(&g_ai, ab, an);
    }
    sceKernelDcacheWritebackAll();                   /* the GPU reads the pictures straight from RAM */
    UiBackend be = { ui_gu_quad, ui_gu_rect, NULL };
    ui_init(&g_ui, &g_pak, be);
    hud_init(&g_hud, &g_pak);
    unsigned char sk_ok[32], lv_ok[32];
    memset(sk_ok, 0, sizeof(sk_ok));
    memset(lv_ok, 0, sizeof(lv_ok));
    for (uint32_t i = 0; i < g_career.nskaters && i < CAREER_NRIDERS; i++) {
        char f[64];
        snprintf(f, sizeof(f), "data/skater_%s.skt", career_str(&g_career, g_career.skater[i].key_off));
        sk_ok[i] = (unsigned char)file_exists(base, f);
    }
    for (uint32_t i = 0; i < g_career.nlevels && g_career.level[i].id < 32; i++) {
        char f[96], *q;
        snprintf(f, sizeof(f), "data/%s.lvl", career_str(&g_career, g_career.level[i].zip_off));
        for (q = f + 5; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
        lv_ok[g_career.level[i].id] = (unsigned char)file_exists(base, f);
    }
    {                                                        // the trick table (the Log Book's trick book lists it; a game loads it again)
        char tp[300];
        snprintf(tp, sizeof(tp), "%s%s", base, TRICKS_FILE);
        tricks_defaults();
        tricks_load(tp);
    }
    fe_init(&g_fe, &g_ui, &g_career, sk_ok, lv_ok);
    load_settings(base);
    g_fe.set = &g_set;
    g_profile_slot = g_set.profile_slot < PROFILE_SLOTS ? g_set.profile_slot : 0;
    load_profile(base);
    g_fe.cur_slot = g_profile_slot;
    fe_set_profile(&g_fe, &g_profile);                       /* the career map, the unlocked skaters and the tutorial ticks read it */

    play_intro(base);

    for (;;) {
        run_frontend(base);
        preview_free();                                     /* the memory goes to the game */
        const FeItem *lv = &g_fe.loc[g_fe.loc_sel];
        snprintf(level_file, sizeof(level_file), "data/%s.lvl", lv->key);
        for (char *q = level_file + 5; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
        if (g_fe.nskater > 0)
            snprintf(skater_file, sizeof(skater_file), "data/skater_%s.skt", g_fe.skater[g_fe.skater_sel].key);
        if (g_fe.mode != FE_MODE_FREESKATE) {                                       /* the career and the tutorials ride the profile's skater */
            for (int i = 0; i < g_fe.nskater_all; i++)
                if (g_fe.skater_all[i].index == g_profile.skater)
                    snprintf(skater_file, sizeof(skater_file), "data/skater_%s.skt", g_fe.skater_all[i].key);
        }
        {                                                                           // the custom skater rides when it is the profile's (career, tutorials) or the one chosen (free skate)
            int rider = g_fe.mode != FE_MODE_FREESKATE ? g_profile.skater : (g_fe.nskater > 0 ? g_fe.skater[g_fe.skater_sel].index : -1);
            g_skater_custom = rider == PROFILE_CUSTOM_SKATER && g_profile.has_custom;
            g_custom_sel = g_profile.custom;
        }
        draw_loading();
        if (g_fe.mode == FE_MODE_TUTORIAL) {
            int won = play_session(base, SESSION_TUTORIAL, fe_tutorial_chal(&g_fe));
            fe_back_to_tutorials(&g_fe, won);                // (the next tutorial that is not done is chosen after a win)
        } else if (g_fe.mode == FE_MODE_CAREER) {
            play_session(base, SESSION_CAREER, g_fe.launch_chal);
            fe_back_to_menu(&g_fe);                          // (the place's list of events, as the profile has them now)
        } else {
            play_session(base, SESSION_FREE, -1);
            fe_back_to_menu(&g_fe);
        }
        save_profile(base);                                  // (the statistics and the trick book changed while he skated)
    }
    return 0;
}
