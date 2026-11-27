/* map - the ground under the scope, from OpenStreetMap's tiles.
 * the tiles are pictures in the Web Mercator projection; the scope has its own (sky.c's
 * geo_offset: kilometres east and north of home). so the tiles are not shown, they are read
 * once into a picture of the screen's own scale and projection, north up, a texel a screen
 * pixel at the range it was made for: the radar's disc, or the whole screen for the square
 * looks (the round glass: the disc in every look). the screen turns and stretches it as the
 * map turns and zooms, and it is made again when the range settles or the look changes.
 * a texel keeps not a colour but what is there: water, wood, a road (map_class). the screen
 * paints that in the theme's colours, or in the map's own for the classic look.
 * 4 bits a texel, the whole 480 x 320 screen is 75 KB. the ESP32 has not that much to spare
 * where TLS lives, so the picture is many small pieces, read and written a 32 bit word at a
 * time: then it may lie in the IRAM the code leaves free, which takes words only (map_mem) */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "png_decode.h"

enum {
    M_LAND, M_WATER, M_WOOD, M_GREEN, M_URBAN, M_BUILDING, M_ROAD, M_MAJOR, M_MOTOR, M_RAIL,
    M_BORDER, M_TEXT, M_AERO,
    M_HALO,                            /* not in the tiles: map_end's, round the letters of a name. the
                                        * tiles have a light halo there; the dark looks paint it dark */
    M_SOFT,                            /* not in the tiles' colours: a letter's soft edge (map.c's letter_mark).
                                        * the classic look paints it as the letter, the dark ones dimmer
                                        * (ui.h NAMES_*): there light strokes as fat as the dark ones ran */
    M_CLASSES,
    M_NONE = 15                        /* nothing known there (yet) */
};

/* texels: the whole screen at most. the 5.79" paper is 792 wide unscaled: at 480 its plan was cut and every tile
 * was refused against the map's 792, dropped and asked again each 15 s (the 2026-11-27 review) */
#define MAP_W_MAX     (GFX_SCALED || GFX_PW > 480 ? GFX_PW : 480)
#define MAP_H_MAX     (GFX_SCALED ? GFX_PH : 320)
#define MAP_BAND      8                /* rows a piece */
/* the square looks of the biggest glasses need up to 49 (the 10.1" at some ranges, the Tab5 43): 32 left the
 * edges empty */
#define MAP_TILES_MAX (GFX_SCALED ? 64 : 16)
/* map_tile: the map and the plan do not match (a new plan meanwhile): nothing read, the tile itself is fine */
#define MAP_NOT_NOW   (-2)

typedef struct { int z; int32_t x, y; } tile_id_t;   /* x may run off the world's edge: map_url wraps it */

/* how the texels sit on the tiles, worked out for a home and a scale */
typedef struct {
    float     lat, lon;                /* home */
    float     kpx;                     /* texels a km */
    int       w, h, z;
    int       disc;                    /* only the disc in the middle */
    int32_t   xorg;                    /* world pixel at zoom z, west of everything below */
    int32_t   row[MAP_H_MAX];          /* world pixel row of each texel row */
    int32_t   x0[MAP_H_MAX];           /* world x of the row's first texel, past xorg, 1/65536 px */
    int32_t   dx[MAP_H_MAX];           /* and from one texel to the next */
    int       ntiles;
    tile_id_t tiles[MAP_TILES_MAX];
} map_plan_t;

/* the picture. made by one task, drawn by another: ready says the texels match the rest */
typedef struct {
    uint32_t         *row[MAP_H_MAX];  /* w / 8 words a row: texel u is bits (u % 8) * 4 of word u / 8 */
    uint32_t         *band[MAP_H_MAX / MAP_BAND];
    int               w, h, disc, tiles;   /* tiles: how many went in, 0 = nothing to show */
    float             lat, lon, kpx;
    volatile int      ready;
    volatile uint32_t gen;             /* one up every time it is made again */
} map_t;

/* where the pieces come from: NULL = malloc. the firmware points it at the word-only IRAM first */
extern void *(*map_mem)(size_t bytes);
/* map_tile's working rows while a tile is read (the names, word by word): MAP_SCRATCH bytes lent, or
 * NULL: malloc. the heap is shortest just then, the PNG's window takes 32 KB of it, so the firmware
 * lends a buffer of its own that nothing uses while the map is read */
#define MAP_SCRATCH 3968               /* map.c's ring of 25 rows, a row of kinds, the stack */
extern void  *map_scratch;

/* the zoom whose tile pixels come nearest a texel: 2 .. 17 */
int  map_zoom(float lat, float kpx);
/* where everything lands for this home and scale: w x h texels (w a multiple of 8), home in the
 * middle, disc: only the circle that fits */
void map_plan(map_plan_t *p, float lat, float lon, float kpx, int w, int h, int disc);
int  map_url(const tile_id_t *t, char *buf, int cap);    /* the tile's address on tile.openstreetmap.org */

int  map_alloc(map_t *m, int w, int h, int disc);        /* 0 = no memory, then it holds none */
void map_begin(map_t *m);                               /* not ready, every texel M_NONE */
/* read tile k of the plan into the texels: png_decode's PNG_OK, PNG_BAD or PNG_NO_MEMORY. a texel
 * that covers more than one pixel of the tile keeps the kind that would be lost: the road, not the
 * field beside it. the names go word by word, pixel for pixel, only their places scaled (map.c) */
int  map_tile(map_t *m, const map_plan_t *p, int k, png_read_fn rd, void *rctx);
/* ready, for the plan's home and scale. every texel beside a letter of a name becomes M_HALO first */
void map_end(map_t *m, const map_plan_t *p);

int  map_class(int r, int g, int b);                    /* an OpenStreetMap colour -> M_* */

static inline int map_at(const map_t *m, int u, int v)
{
    return (int)(m->row[v][u >> 3] >> ((u & 7) * 4)) & 15;
}

/* the outlines: where water meets land, a town the country, a wood the fields. the eye reads where
 * things end more than what fills them, and on these screens a fill is a step or two above black.
 * a texel of a town or of water is on an outline's inside when one beside it (left, right, up,
 * down) is lower: the open country 1, a wood 2, a town 3, water 4. roads, rails, borders, runways
 * and names are 0: lines of their own, and a name's white halo would be outlined. a wood has none:
 * the country is woods in pieces, every piece outlined was a ripple over the whole map, and asking
 * its four neighbours cost a forest's every texel. map_rim says the kind, MAP_RIM more on an
 * outline. worked out as the screen asks: nothing kept, so the classic look can leave them out */
#define MAP_RIM 16
static inline int map_rim(const map_t *m, int u, int v)
{
    static const uint8_t UP[16] = { [M_LAND] = 1, [M_GREEN] = 1, [M_WOOD] = 2, [M_URBAN] = 3, [M_BUILDING] = 3, [M_WATER] = 4 };
    const int k = map_at(m, u, v), up = UP[k];
    if (up < 3) {
        return k;
    }
    int n;
    if (u > 0 && (n = UP[map_at(m, u - 1, v)]) && n < up) return k | MAP_RIM;
    if (u + 1 < m->w && (n = UP[map_at(m, u + 1, v)]) && n < up) return k | MAP_RIM;
    if (v > 0 && (n = UP[map_at(m, u, v - 1)]) && n < up) return k | MAP_RIM;
    if (v + 1 < m->h && (n = UP[map_at(m, u, v + 1)]) && n < up) return k | MAP_RIM;
    return k;
}
