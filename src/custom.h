/* The custom skater (M11; no PSP dependencies, host-tested in tests/test_custom.c): a skater made of the parts of the original's create-a-skater (data/custom.pak, tools/convert_custom.py): a top, a
 * pair of pants, a head with hair, a beard and a hat on the shared skeleton, textures and overlays pasted into one 256 x 128 atlas. custom_build writes a complete .skt (the file format of the pro
 * skaters, tools/convert_skater.py) from the choice and a base skater file whose skeleton and animations it keeps, so the rest of the game treats it like any other skater. */
#ifndef CUSTOM_H
#define CUSTOM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t gender;                     /* 0 male, 1 female */
    uint8_t build;                      /* 0 average, 1 stocky, 2 skinny */
    uint8_t skin;                       /* 0 .. 3 */
    uint8_t face;                       /* 0 .. 7: the head shape is face / 2, the face texture face % 2 */
    uint8_t hair;                       /* 0 none, 1 .. 9 the styles of custom_hair_name */
    uint8_t hair_col;                   /* 0 black, 1 blonde, 2 brown */
    uint8_t beard;                      /* 0 none, 1 .. 8 */
    uint8_t beard_col;                  /* 0 .. 2 */
    uint8_t hat;                        /* 0 none, 1 .. 6 the styles of custom_hat_name */
    uint8_t hat_tex;                    /* index in the list of hat designs */
    uint8_t top;                        /* index in the list of tops (custom_count(CUST_TOP)) */
    uint8_t logo;                       /* 0 none, else 1 + the index in the list of logos (printed on the top) */
    uint8_t pants;                      /* index in the list of bottoms */
    uint8_t shoes;                      /* index in the list of shoes */
    uint8_t deck;                       /* the board's deck art: 0 = the generic one (Toon), else 1 + the index in the list of decks */
    uint8_t wheels;                     /* the wheel art: 0 = the generic white one, else 1 + the index in the list of wheels */
} CustomSel;
_Static_assert(sizeof(CustomSel) == 16, "CustomSel");

enum { CUST_TOP = 0, CUST_PANTS, CUST_SHOES, CUST_LOGO, CUST_HAT, CUST_DECK, CUST_WHEELS, CUST_NLISTS };
#define CUSTOM_NSKIN 4
#define CUSTOM_NFACE 8
#define CUSTOM_NBUILD 3
#define CUSTOM_NHAIR 10                 /* with none */
#define CUSTOM_NBEARD 9                 /* with none */
#define CUSTOM_NHAT 7                   /* with none */
#define CUSTOM_NCOL 3
#define CUSTOM_MAXLIST 80

/* the choice as it was saved by the first version of the editor (profile version 4): 8 bytes gender, build, skin, face texture, built-in hair, top, pants, shoes */
static inline CustomSel custom_from_v1(const uint8_t v1[8])
{
    CustomSel s = { v1[0], v1[1], v1[2], (uint8_t)(v1[3] & 1), (uint8_t)(1 + v1[4] % 5), 2, 0, 0, 0, 0, v1[5], 0, v1[6], v1[7], 0, 0 };     /* the five built-in hair styles: five of the new ones, brown */
    return s;
}

typedef struct {
    const uint8_t *file;
    size_t size;
    uint32_t nmesh, ntex;
    const uint8_t *mesh, *tex;          /* the entry tables */
    int nlist[CUST_NLISTS];             /* how many tops / bottoms / shoes / logos / hat designs there are */
    int list[CUST_NLISTS][CUSTOM_MAXLIST];   /* texture entry indices of each list, in file order */
} CustomPak;

/* buf stays owned by the caller; 0 if it is not a custom.pak of version 2 or 3 (version 2 has no board art: the deck and wheel lists are empty) or runs past its size */
int custom_pak_load(CustomPak *p, const uint8_t *buf, size_t size);
int custom_count(const CustomPak *p, int list);
const char *custom_name(const CustomPak *p, int list, int i);
/* the item number the original's unlock tables give list item i (shoes, hat designs, decks, wheels: kinds 5, 2, 8, 7 of src/career.h), -1 if it has none (it is never unlocked) */
int custom_rom(const CustomPak *p, int list, int i);
/* hat designs: 0 baseball cap, 1 diver, 2 helmet - the styles they go with (custom_hat_design_ok) */
int custom_hat_family(const CustomPak *p, int i);
int custom_hat_design_ok(const CustomPak *p, int hat, int i);
/* the unlock kind of a list (UNLOCK_*), -1 when its items are not gated */
int custom_unlock_kind(int list);
/* the first choice that is valid (a male, average build, the first of everything, nothing printed or worn) */
void custom_default(const CustomPak *p, CustomSel *s);
int custom_valid(const CustomPak *p, const CustomSel *s);
const char *custom_gender_name(int g);
const char *custom_build_name(int b);
const char *custom_hair_name(int h);         /* 0 .. CUSTOM_NHAIR - 1 */
const char *custom_hat_name(int h);          /* 0 .. CUSTOM_NHAT - 1 */
const char *custom_colour_name(int c);
/* a hat style that exists for the gender (the cap for long hair, style 3, is a woman's) */
static inline int custom_hat_exists(int gender, int hat) { return hat >= 0 && hat < CUSTOM_NHAT && !(gender == 0 && hat == 3); }
/* the board's texture for the choice (the generic deck / wheels when deck / wheels are 0): 64 x 128 palette indices, row by row (no swizzling), entry 0 = transparent, and its 256-entry palette (5551) */
int custom_board_texture(const CustomPak *p, const CustomSel *s, uint8_t pix[64 * 128], uint16_t clut[256]);
/* how many vertices the parts of a choice have, in the order custom_build writes them: top, pants, head, hair (0 = none), hat (0 = none); 0 if the choice is invalid (tests) */
int custom_part_counts(const CustomPak *p, const CustomSel *s, uint32_t out[5]);
/* a .skt file for the choice, keeping the skeleton and animations of `base` (an ordinary skater file); malloc'd, the caller frees it. NULL if the choice is invalid or the files do not fit. */
uint8_t *custom_build(const CustomPak *p, const CustomSel *s, const uint8_t *base, size_t base_size, size_t *out_size);

#endif
