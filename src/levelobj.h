/* The named objects of a level (version 4 level files: the objects of the original game's track1.pvs, written by tools/convert_level.py): free-skate start grid, challenge beams and
 * start grids, race gates, trick zones, play-area bounds ... No PSP dependencies (host-tested in tests/test_levelobj.c).
 * Names are the original's: start_pos_1..4 / start_dir_1..4, boundinner_a_1 / _b_1, boundouter_a_1 / _b_1, chalNNN_chalstart, chalNNN_start_pos_k, checkpoint_NNN_a / _b ... */
#ifndef LEVELOBJ_H
#define LEVELOBJ_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *tab;     /* n * 44 bytes {char name[32], f32 x, y, z} (inside the level file) */
    uint32_t n;
    const uint8_t *paths;   /* the splines section (levelobj_init_paths): u32 n, then n * {char name[32], u32 npts, npts * 3 f32} */
    size_t paths_size;      /* bytes from `paths` to the end of the file */
} LevelObjs;

/* objs points at the u32 count followed by the records (file + off_objs); returns 0 when the section is missing or runs past `size` */
int levelobj_init(LevelObjs *o, const uint8_t *file, size_t size, uint32_t off_objs);

/* the level's splines (off_paths of the header; call after levelobj_init); returns 0 when the section is missing or malformed */
int levelobj_init_paths(LevelObjs *o, const uint8_t *file, size_t size, uint32_t off_paths);
/* copies the points of the spline called name (up to max points) into out; returns how many points the spline has (0 when there is none; more than max means it was cut) */
int levelobj_path(const LevelObjs *o, const char *name, float (*out)[3], int max);

/* the position of the object called name; 0 when there is none */
int levelobj_find(const LevelObjs *o, const char *name, float p[3]);

/* the position of the object called "<prefix><number><suffix>" with the number zero-padded to `digits` (chalNNN_chalstart: ("chal", 3, 2, "_chalstart")); 0 when there is none */
int levelobj_find_num(const LevelObjs *o, const char *prefix, int digits, int number, const char *suffix, float p[3]);

/* free-skate start grid slot k (1..4): position and the heading (atan2(dx, dz), the Skate heading convention) towards its start_dir_k point; 0 when the level has none */
int levelobj_start(const LevelObjs *o, int k, float pos[3], float *heading);

/* the play area's outer limit (boundouter_a_1 / _b_1) as a box; 0 when the level has none */
int levelobj_bounds_outer(const LevelObjs *o, float lo[3], float hi[3]);

#endif
