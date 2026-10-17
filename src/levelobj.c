#include "levelobj.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define REC 44u

int levelobj_init(LevelObjs *o, const uint8_t *file, size_t size, uint32_t off_objs)
{
    o->tab = NULL;
    o->n = 0;
    o->paths = NULL;
    o->paths_size = 0;
    if (off_objs == 0 || (size_t)off_objs + 4 > size)
        return 0;
    uint32_t n;
    memcpy(&n, file + off_objs, 4);
    if ((size_t)off_objs + 4 + (size_t)n * REC > size)
        return 0;
    o->tab = file + off_objs + 4;
    o->n = n;
    return 1;
}

int levelobj_find(const LevelObjs *o, const char *name, float p[3])
{
    for (uint32_t i = 0; i < o->n; i++) {
        const uint8_t *r = o->tab + (size_t)i * REC;
        if (strncmp((const char *)r, name, 32) == 0) {
            memcpy(p, r + 32, 12);
            return 1;
        }
    }
    return 0;
}

int levelobj_find_num(const LevelObjs *o, const char *prefix, int digits, int number, const char *suffix, float p[3])
{
    char name[48];
    snprintf(name, sizeof(name), "%s%0*d%s", prefix, digits, number, suffix);
    return levelobj_find(o, name, p);
}

int levelobj_start(const LevelObjs *o, int k, float pos[3], float *heading)
{
    char a[16], b[16];
    float d[3];
    snprintf(a, sizeof(a), "start_pos_%d", k);
    snprintf(b, sizeof(b), "start_dir_%d", k);
    if (!levelobj_find(o, a, pos) || !levelobj_find(o, b, d))
        return 0;
    *heading = atan2f(d[0] - pos[0], d[2] - pos[2]);
    return 1;
}

int levelobj_bounds_outer(const LevelObjs *o, float lo[3], float hi[3])
{
    float a[3], b[3];
    if (!levelobj_find(o, "boundouter_a_1", a) || !levelobj_find(o, "boundouter_b_1", b))
        return 0;
    for (int i = 0; i < 3; i++) {
        lo[i] = a[i] < b[i] ? a[i] : b[i];
        hi[i] = a[i] < b[i] ? b[i] : a[i];
    }
    return 1;
}

int levelobj_init_paths(LevelObjs *o, const uint8_t *file, size_t size, uint32_t off_paths)
{
    o->paths = NULL;
    o->paths_size = 0;
    if (off_paths == 0 || (size_t)off_paths + 4 > size)
        return 0;
    uint32_t n;
    memcpy(&n, file + off_paths, 4);
    size_t at = (size_t)off_paths + 4;
    for (uint32_t i = 0; i < n; i++) {                           /* every spline must lie inside the file */
        uint32_t np;
        if (at + 36 > size)
            return 0;
        memcpy(&np, file + at + 32, 4);
        if (np > 100000u || at + 36 + (size_t)np * 12 > size)
            return 0;
        at += 36 + (size_t)np * 12;
    }
    o->paths = file + off_paths;
    o->paths_size = size - off_paths;
    return 1;
}

int levelobj_path(const LevelObjs *o, const char *name, float (*out)[3], int max)
{
    if (!o->paths)
        return 0;
    uint32_t n;
    memcpy(&n, o->paths, 4);
    size_t at = 4;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t np;
        memcpy(&np, o->paths + at + 32, 4);
        if (strncmp((const char *)(o->paths + at), name, 32) == 0) {
            for (int k = 0; k < (int)np && k < max; k++)
                memcpy(out[k], o->paths + at + 36 + (size_t)k * 12, 12);
            return (int)np;
        }
        at += 36 + (size_t)np * 12;
    }
    return 0;
}
