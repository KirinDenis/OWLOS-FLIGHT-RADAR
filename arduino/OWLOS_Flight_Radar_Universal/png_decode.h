/* png_decode - a PNG read row by row, for the map's tiles. the picture is never whole in memory:
 * two of its rows and the inflater's 32 KB look-back window are all it takes, and the window is
 * eight pieces of 4 KB. the ESP32 has an inflater in ROM (miniz's tinfl), but it wants the 32 KB
 * in one piece, and after the WiFi starts the heap has nothing that big. so this one is our own,
 * after Mark Adler's puff: plain, a bit at a time.
 * 8 bit palette, grey, grey+alpha, RGB, RGBA; not interlaced (map tiles never are) */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define PNG_MAX_W 256                 /* a map tile */

typedef struct {
    int     w, h;
    int     type;                     /* 0 grey, 2 RGB, 3 palette, 4 grey+alpha, 6 RGBA */
    int     bpp;                      /* bytes a pixel */
    int     npal;
    uint8_t pal[256][3];
} png_info_t;

typedef int  (*png_read_fn)(void *ctx, uint8_t *buf, int cap);          /* the next bytes, 0 at the end */
typedef int  (*png_head_fn)(void *ctx, const png_info_t *info);         /* before the first row: 0 stops */
typedef void (*png_row_fn)(void *ctx, const png_info_t *info, int y, const uint8_t *row);

enum { PNG_OK = 1, PNG_BAD = 0, PNG_NO_MEMORY = -1 };

/* rd(rctx) gives the file, head(ctx) and row(ctx) take the picture. PNG_OK: every row went to
 * row(). PNG_BAD: not a PNG this reads, broken, or head() said no. it takes about 39 KB while it
 * runs, the largest piece 5 KB, and gives it all back */
int png_decode(png_read_fn rd, void *rctx, png_head_fn head, png_row_fn row, void *ctx);
