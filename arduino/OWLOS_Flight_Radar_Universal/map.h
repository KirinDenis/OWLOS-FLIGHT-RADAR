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
    M_BORDER, M_TEXT, M_AERO, M_CLASSES,
    M_NONE = 15                        /* nothing known there (yet) */
};

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ESP32S3)
#define MAP_W_MAX     800
#define MAP_H_MAX     480
#define MAP_TILES_MAX 32
#else
#define MAP_W_MAX     (GFX_SCALED ? GFX_PW : 480)   /* texels: the whole screen at most */
#define MAP_H_MAX     (GFX_SCALED ? GFX_PH : 320)
#define MAP_TILES_MAX (GFX_SCALED ? 32 : 16)
#endif
#define MAP_BAND      8                /* rows a piece */

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

/* the zoom whose tile pixels come nearest a texel: 2 .. 17 */
int  map_zoom(float lat, float kpx);
/* where everything lands for this home and scale: w x h texels (w a multiple of 8), home in the
 * middle, disc: only the circle that fits */
void map_plan(map_plan_t *p, float lat, float lon, float kpx, int w, int h, int disc);
int  map_url(const tile_id_t *t, char *buf, int cap);    /* the tile's address on tile.openstreetmap.org */

int  map_alloc(map_t *m, int w, int h, int disc);        /* 0 = no memory, then it holds none */
void map_begin(map_t *m);                               /* not ready, every texel M_NONE */
/* read tile k of the plan into the texels: png_decode's PNG_OK, PNG_BAD or PNG_NO_MEMORY */
int  map_tile(map_t *m, const map_plan_t *p, int k, png_read_fn rd, void *rctx);
void map_end(map_t *m, const map_plan_t *p);            /* ready, for the plan's home and scale */

int  map_class(int r, int g, int b);                    /* an OpenStreetMap colour -> M_* */

static inline int map_at(const map_t *m, int u, int v)
{
    return (int)(m->row[v][u >> 3] >> ((u & 7) * 4)) & 15;
}
