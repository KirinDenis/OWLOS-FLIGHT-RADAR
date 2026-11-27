// the ground under the scope, see map.h
#include "map.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_D 3.14159265358979323846

/* --- what a colour of the OpenStreetMap standard style shows. its tiles have a palette of
 * their own each, so the colours are matched to the nearest of these, not looked up --- */

static const struct { uint8_t r, g, b, cls; } REF[] = {
    { 242, 239, 233, M_LAND },  { 238, 240, 213, M_LAND },  { 245, 220, 186, M_LAND },    // land, farmland, farmyard
    { 214, 217, 159, M_LAND },  { 245, 233, 198, M_LAND },  { 238, 229, 220, M_LAND },    // heath, sand, rock
    { 255, 241, 186, M_LAND },  { 255, 255, 229, M_LAND },                                // beach, school grounds
    { 170, 211, 223, M_WATER }, { 77, 128, 179, M_WATER },                                // water and its names
    { 173, 209, 158, M_WOOD },  { 157, 202, 138, M_WOOD },  { 170, 203, 175, M_WOOD },    // forest, wood, cemetery
    { 205, 235, 176, M_GREEN }, { 200, 250, 204, M_GREEN }, { 223, 252, 226, M_GREEN },   // grass, park, pitches
    { 201, 225, 191, M_GREEN }, { 174, 223, 163, M_GREEN }, { 200, 215, 171, M_GREEN },   // allotments, orchard, scrub
    { 224, 223, 223, M_URBAN }, { 242, 218, 217, M_URBAN }, { 255, 214, 209, M_URBAN },   // residential, commercial, retail
    { 235, 219, 232, M_URBAN }, { 238, 238, 238, M_URBAN }, { 223, 221, 206, M_URBAN },   // industrial, parking, garages
    { 217, 208, 201, M_BUILDING }, { 196, 182, 171, M_BUILDING },
    { 255, 255, 255, M_ROAD },  { 153, 153, 153, M_ROAD },                                // a small road and its edge
    { 247, 250, 191, M_MAJOR }, { 252, 214, 164, M_MAJOR }, { 160, 107, 0, M_MAJOR }, { 112, 125, 5, M_MAJOR },
    { 232, 146, 162, M_MOTOR }, { 249, 178, 156, M_MOTOR }, { 220, 42, 103, M_MOTOR }, { 200, 78, 47, M_MOTOR },
    { 112, 112, 112, M_RAIL },
    { 172, 70, 172, M_BORDER }, { 207, 155, 203, M_BORDER },
    { 0, 0, 0, M_TEXT },        { 51, 51, 51, M_TEXT },
    { 85, 85, 85, M_TEXT },     { 128, 95, 60, M_TEXT },   // a letter's soft edge (rails began at 89), a hill's brown name
    { 187, 187, 204, M_AERO },  { 218, 218, 224, M_AERO },                                // runway, apron
    { 220, 220, 220, M_URBAN }, { 200, 200, 200, M_BUILDING },   // the plain greys of a town: an apron is blue-grey,
};                                                              // and towns lit up as runways

int map_class(int r, int g, int b)
{
    int best = M_LAND;
    long bd = -1;
    for (int i = 0; i < (int)(sizeof REF / sizeof REF[0]); i++) {
        long dr = r - REF[i].r, dg = g - REF[i].g, db = b - REF[i].b;
        long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;     // the eye cares for green most
        if (bd < 0 || d < bd) { bd = d; best = REF[i].cls; }
    }
    return best;
}

/* --- where the texels fall on the tiles --- */

static double world(int z) { return 256.0 * (double)(1u << z); }      // the world's width in pixels

// Web Mercator: the pixel row of a latitude
static double merc_y(double lat, double w)
{
    double f = lat * PI_D / 180.0;
    return (1.0 - log(tan(f) + 1.0 / cos(f)) / PI_D) / 2.0 * w;
}

int map_zoom(float lat, float kpx)
{
    // a tile pixel at zoom z covers 40075 km * cos(lat) / (256 * 2^z)
    double c = cos(lat * PI_D / 180.0);
    if (c < 0.05) c = 0.05;
    int z = (int)floor(log2((double)kpx * 40075.017 * c / 256.0) + 0.5);
    return z < 2 ? 2 : z > 17 ? 17 : z;
}

void map_plan(map_plan_t *p, float lat, float lon, float kpx, int w, int h, int disc)
{
    memset(p, 0, sizeof *p);
    if (w > MAP_W_MAX) w = MAP_W_MAX;
    if (h > MAP_H_MAX) h = MAP_H_MAX;
    p->lat = lat;
    p->lon = lon;
    p->kpx = kpx;
    p->w = w;
    p->h = h;
    p->disc = disc;
    p->z = map_zoom(lat, kpx);
    const double ww = world(p->z), hw = w / 2.0, hh = h / 2.0;
    const double xc = ((double)lon + 180.0) / 360.0 * ww;
    p->xorg = (int32_t)floor(xc) - 4096;
    for (int v = 0; v < h; v++) {
        // sky.c's projection: north in km is the latitude, east is the longitude at the middle latitude
        double north = (hh - v - 0.5) / kpx;
        double la = lat + north / 110.574;
        double cm = cos((lat + la) * 0.5 * PI_D / 180.0);
        double s = ww / (360.0 * 111.320 * cm * kpx);           // world pixels a texel, east
        p->row[v] = (int32_t)floor(merc_y(la, ww));
        p->x0[v] = (int32_t)llround((xc + (0.5 - hw) * s - p->xorg) * 65536.0);
        p->dx[v] = (int32_t)llround(s * 65536.0);
    }
    // the tiles under it: every other texel asks, the last row and column too, nearest home first
    int32_t hx = (int32_t)floor(xc / 256.0), hy = (int32_t)floor(merc_y(lat, ww) / 256.0);
    for (int vs = 0; vs <= h; vs += 2)
        for (int us = 0; us <= w; us += 2) {
            int v = vs < h ? vs : h - 1, u = us < w ? us : w - 1;   // past the end: the edge itself
            double du = u + 0.5 - hw, dv = v + 0.5 - hh;
            if (disc && du * du + dv * dv > hh * hh) continue;
            int64_t x = (int64_t)p->x0[v] + (int64_t)u * p->dx[v];
            tile_id_t t = { p->z, (int32_t)floor((p->xorg + x / 65536.0) / 256.0), (int32_t)floor(p->row[v] / 256.0) };
            int have = 0;
            for (int k = 0; k < p->ntiles && !have; k++) have = p->tiles[k].x == t.x && p->tiles[k].y == t.y;
            if (have || p->ntiles == MAP_TILES_MAX) continue;
            int k = p->ntiles++, d = abs(t.x - hx) + abs(t.y - hy);
            while (k > 0 && abs(p->tiles[k - 1].x - hx) + abs(p->tiles[k - 1].y - hy) > d) { p->tiles[k] = p->tiles[k - 1]; k--; }
            p->tiles[k] = t;
        }
}

int map_url(const tile_id_t *t, char *buf, int cap)
{
    int32_t n = (int32_t)1 << t->z;
    if (t->y < 0 || t->y >= n) return 0;                       // past the poles: no such tile
    int32_t x = ((t->x % n) + n) % n;                           // round the world at 180 degrees
    snprintf(buf, (size_t)cap, "https://tile.openstreetmap.org/%d/%ld/%ld.png", t->z, (long)x, (long)t->y);
    return 1;
}

/* --- the texels --- */

void *(*map_mem)(size_t bytes);
void  *map_scratch;

static void map_free(map_t *m)
{
    m->ready = 0;
    for (int b = 0; b < MAP_H_MAX / MAP_BAND; b++) {
        free(m->band[b]);
        m->band[b] = NULL;
    }
    m->w = m->h = 0;
}

int map_alloc(map_t *m, int w, int h, int disc)
{
    if (m->band[0] && m->w == w && m->h == h) {
        m->disc = disc;
        return 1;
    }
    map_free(m);
    if (w & 7) return 0;                             // a row is whole words of 8 texels: the last few would go past it
    const int words = w / 8;
    for (int v0 = 0; v0 < h; v0 += MAP_BAND) {
        int rows = h - v0 < MAP_BAND ? h - v0 : MAP_BAND;
        size_t bytes = (size_t)rows * words * 4;
        uint32_t *p = map_mem ? map_mem(bytes) : NULL;
        if (!p) p = malloc(bytes);
        if (!p) {
            map_free(m);
            return 0;
        }
        m->band[v0 / MAP_BAND] = p;
        for (int r = 0; r < rows; r++) m->row[v0 + r] = p + r * words;
    }
    m->w = w;
    m->h = h;
    m->disc = disc;
    return 1;
}

void map_begin(map_t *m)
{
    m->ready = 0;
    m->tiles = 0;
    for (int v = 0; v < m->h; v++)                   // a word at a time: the IRAM takes nothing smaller
        for (int i = 0; i < m->w / 8; i++) m->row[v][i] = 0xFFFFFFFFu;
}

/* the names: the tiles write them dark with a light halo, made to read on a light map. every texel
 * round a letter (the eight about it) becomes M_HALO, which the dark looks paint dark under letters
 * they paint light: a name reads there too, where it was a hole with a glow round it. nothing
 * becomes a letter here, so a texel's neighbours are still what the tiles said, or a halo, which
 * is no letter either. a word at a time, as everywhere here: the texels may be in the IRAM */
static int by_letter(const map_t *m, int u, int v)
{
    for (int dv = -1; dv <= 1; dv++) {
        if (v + dv < 0 || v + dv >= m->h) continue;
        for (int du = -1; du <= 1; du++) {
            if ((du || dv) && u + du >= 0 && u + du < m->w) {
                const int k = map_at(m, u + du, v + dv);
                if (k == M_TEXT || k == M_SOFT) return 1;
            }
        }
    }
    return 0;
}

static void halos(map_t *m)
{
    const int words = m->w / 8;
    for (int v = 0; v < m->h; v++) {
        uint32_t *row = m->row[v];
        for (int i = 0; i < words; i++) {
            const uint32_t w = row[i];
            uint32_t out = w;
            for (int j = 0; j < 8; j++) {
                const int u = i * 8 + j, k = (int)(w >> (j * 4)) & 15;
                if (k == M_TEXT || k == M_SOFT || k == M_NONE || k == M_HALO) continue;
                if (by_letter(m, u, v)) out = (out & ~(15u << (j * 4))) | (uint32_t)M_HALO << (j * 4);
            }
            row[i] = out;
        }
    }
}

void map_end(map_t *m, const map_plan_t *p)
{
    halos(m);
    m->lat = p->lat;
    m->lon = p->lon;
    m->kpx = p->kpx;
    m->gen++;
    m->ready = 1;
}

/* --- a tile into the texels. the ground and its lines go the scaled way: a texel a pixel and a bit at
 * most, keeping what would be lost (KEEP). the names go word by word, each the size the tile writes it.
 * the tiles write a town's name some 9 px high, soft-edged, for their own scale; stretched or shrunk to
 * the screen's (up to 1.4 times either way) a stroke came out a pixel here and two there, an accent
 * ran into its letter, and Trencin with its hacek and its i's accent read "Trenc'n". so a word is
 * copied pixel for pixel, and only where it stands is scaled, as everything else is.
 *
 * what is a letter: OpenStreetMap's colours do not change. a grey (r, g and b within 12) under 100 is
 * a letter's core. a grey under 170 with its colours within 40 is an edge (the halo under a name is
 * half see-through: the ground's colour shows in the soft edge of a letter), a letter's when a core is
 * within NEAR pixels: the thin stem of an i with an accent has no core of its own, and a road's
 * casing a few pixels on is no letter. cores and their edges close together (GAP pixels in a row, a
 * row between) are a word; one no taller than WORD_H with cores enough is copied, the rest goes the
 * scaled way as it went before: a core a letter (M_TEXT), an edge beside one its soft edge (M_SOFT),
 * a grey on its own a rail or a road's edge. a word stands by its middle, or by the tile's edge it
 * touches: its other part is in the tile beside, stands by the same edge, and the two meet.
 *
 * read as the rows come, in little memory: the marks of the last RING rows, 4 bits a pixel. an edge is
 * known for a letter's NEAR rows on, a word is whole when two rows under it are known. 3.6 KB while a
 * tile is read (MAP_SCRATCH, map.h). from the heap they were 2.3 KB more than the letters took before,
 * and the 2.8" CYD, its heap in pieces round the PNG's window, could not read a third of its maps in a
 * soak: the firmware lends a buffer of its own now --- */

/* rows of marks kept: a word, the rows that tell it is whole, and the NEAR rows tile_row reads ahead of them.
 * 22 forgot those last: the 3 rows a word's top reached past were the newest rows' slots (the 2026-11-27 review) */
#define RING   25
#define NEAR   3                       /* an edge this near a core is a letter's */
#define GAP    5                       /* letters this far apart in a row are one word: it moves as one */
#define WORD_H 16                      /* the tallest word copied */
#define WORD_W 180                     /* and the widest */
#define STACK  256                     /* pixels of a word waiting to be looked round */
_Static_assert(RING >= WORD_H + 2 * NEAR + 3, "a word, the rows that tell it is whole and the rows read ahead fit the ring");
_Static_assert(MAP_SCRATCH == RING * 128 + 256 + STACK * 2, "map.h's MAP_SCRATCH: the ring, a row of kinds, the stack");

enum { MK_NONE, MK_CORE, MK_DARK, MK_LIGHT, MK_TINT };  /* a grey under 133 (rails at 112), under 170, a tinted one */
#define MK_NEAR 3                                        /* added to an edge, 5..7: a core is near, it is a letter's */
#define MK_SEEN 8                                        /* in the word being gathered */

typedef struct {
    map_t            *m;
    const map_plan_t *p;
    tile_id_t         t;
    uint8_t           cls[256];        /* palette entry -> M_* */
    uint32_t          last_rgb;        /* the colour classed last: tiles without a palette */
    int               last_cls;
    uint8_t          *ring;            /* RING rows of 128 bytes, two marks a byte: row y at (y % RING) */
    uint8_t          *kind;            /* a row of 256 M_* */
    uint16_t         *stack;           /* STACK pixels: row << 8 | x */
} tile_ctx_t;

static int tile_head(void *ctx, const png_info_t *in)
{
    tile_ctx_t *c = ctx;
    if (in->w != 256 || in->h != 256) return 0;
    for (int i = 0; i < in->npal; i++) c->cls[i] = (uint8_t)map_class(in->pal[i][0], in->pal[i][1], in->pal[i][2]);
    c->last_rgb = 0xFFFFFFFFu;
    return 1;
}

static void pixel_rgb(const png_info_t *in, const uint8_t *px, int *r, int *g, int *b)
{
    if (in->type == 3) {
        *r = in->pal[px[0]][0];
        *g = in->pal[px[0]][1];
        *b = in->pal[px[0]][2];
    } else if (in->type < 2 || in->type == 4) {
        *r = *g = *b = px[0];
    } else {
        *r = px[0];
        *g = px[1];
        *b = px[2];
    }
}

static int pixel_class(tile_ctx_t *c, const png_info_t *in, const uint8_t *px)
{
    if (in->type == 3) return c->cls[px[0]];
    int r, g, b;
    pixel_rgb(in, px, &r, &g, &b);
    const uint32_t rgb = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    if (rgb != c->last_rgb) {
        c->last_rgb = rgb;
        c->last_cls = map_class(r, g, b);
    }
    return c->last_cls;
}

/* which kind a texel keeps when its pixels say more than one: the one that would be lost. the zoom is
 * the one whose pixels come nearest a texel, so a texel is up to 1.4 pixels: read at one pixel, a
 * road or a river one pixel wide fell between the texels and came out dashed. the areas give way to
 * the lines, and everything to the letters of a name: the tiles write a name over everything too,
 * and a letter read away at a texel's edge broke it. nothing known, 0, gives way to anything */
static const uint8_t KEEP[16] = {
    [M_HALO] = 1, [M_LAND] = 1, [M_GREEN] = 2, [M_WOOD] = 3, [M_URBAN] = 4, [M_BUILDING] = 5, [M_ROAD] = 6,
    [M_WATER] = 7, [M_AERO] = 8, [M_RAIL] = 9, [M_BORDER] = 10, [M_MAJOR] = 11, [M_MOTOR] = 12, [M_SOFT] = 13,
    [M_TEXT] = 14,
};

// what a pixel may be of a letter, MK_*
static int letter_mark(const png_info_t *in, const uint8_t *px)
{
    int r, g, b;
    pixel_rgb(in, px, &r, &g, &b);
    const int lo = r < g ? (r < b ? r : b) : (g < b ? g : b), hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const int grey = (r + g + b) / 3;
    if (hi - lo <= 12) return grey < 100 ? MK_CORE : grey < 133 ? MK_DARK : grey < 170 ? MK_LIGHT : MK_NONE;
    return hi - lo <= 40 && grey < 170 ? MK_TINT : MK_NONE;
}

static inline int mk_get(const tile_ctx_t *c, int y, int x)
{
    return c->ring[(y % RING) * 128 + (x >> 1)] >> ((x & 1) * 4) & 15;
}

static inline void mk_set(tile_ctx_t *c, int y, int x, int v)
{
    uint8_t *q = &c->ring[(y % RING) * 128 + (x >> 1)];
    const int sh = (x & 1) * 4;
    *q = (uint8_t)((*q & ~(15 << sh)) | v << sh);
}

static inline int seen_get(const tile_ctx_t *c, int y, int x) { return mk_get(c, y, x) & MK_SEEN; }
static inline void seen_set(tile_ctx_t *c, int y, int x) { mk_set(c, y, x, mk_get(c, y, x) | MK_SEEN); }
static inline int mk_edge(int v) { return (v & 7) > MK_TINT ? (v & 7) - MK_NEAR : v & 7; }   // MK_*, near or not

// of a word: a core, or an edge with a core near
static inline int word_mark(int v) { return (v & 7) == MK_CORE || (v & 7) > MK_TINT; }

/* pixel row y's kinds, M_NONE where nothing, into the texels the row goes into: those whose middle it
 * is, or else the one above, when the texels are taller than the pixels and it falls between two
 * middles. each texel keeps the most kept kind of its pixels, up to the next texel's middle */
static void put_row(tile_ctx_t *c, int y, const uint8_t *kind)
{
    const map_plan_t *p = c->p;
    map_t *m = c->m;
    const int32_t wy = c->t.y * 256 + y;
    int va = 0, hi = p->h;                              // the first texel row at or past it
    while (va < hi) {
        const int mid = (va + hi) / 2;
        if (p->row[mid] < wy) va = mid + 1; else hi = mid;
    }
    int vb = va;
    while (vb < p->h && p->row[vb] == wy) vb++;
    if (va == vb) {
        if (va == 0 || va >= p->h) return;             // above the first middle, or past the last
        vb = va--;
    }
    const float hw = p->w * 0.5f, hh = p->h * 0.5f;
    const int64_t left = ((int64_t)c->t.x * 256 - p->xorg) * 65536, right = left + (256LL << 16);
    for (int v = va; v < vb; v++) {
        int ua = 0, ub = p->w - 1;
        if (p->disc) {                                      // the disc's chord only
            float dv = v + 0.5f - hh, ch = hh * hh - dv * dv;
            if (ch <= 0) continue;
            ch = sqrtf(ch);
            ua = (int)ceilf(hw - ch - 0.5f);
            ub = (int)floorf(hw + ch - 0.5f);
        }
        const int64_t x0 = p->x0[v], dx = p->dx[v];
        if (dx <= 0) continue;
        int ta = (int)((left - x0 + dx - 1) / dx), tb = (int)((right - x0 + dx - 1) / dx) - 1;   // and the tile's
        if (left - x0 < 0) ta = 0;
        if (ua < ta) ua = ta;
        if (ub > tb) ub = tb;
        if (ub >= p->w) ub = p->w - 1;
        uint32_t *row = m->row[v];
        for (int u = ua < 0 ? 0 : ua; u <= ub; u++) {
            int px = (int)((x0 + u * dx - left) >> 16);
            if (px < 0 || px > 255) continue;
            int pe = (int)((x0 + (u + 1) * dx - left) >> 16);   // its pixels: up to the next texel's middle
            if (pe > 256) pe = 256;
            int k = kind[px];
            for (int q = px + 1; q < pe; q++) {
                if (KEEP[kind[q]] > KEEP[k]) k = kind[q];
            }
            const int sh = (u & 7) * 4;                      // a word at a time: it may be in the IRAM
            const uint32_t w = row[u >> 3];
            if (KEEP[k] > KEEP[(w >> sh) & 15]) row[u >> 3] = (w & ~(15u << sh)) | (uint32_t)k << sh;
        }
    }
}

// a texel, for a word's letter: kept as put_row keeps
static void put_texel(map_t *m, int u, int v, int k)
{
    if (u < 0 || v < 0 || u >= m->w || v >= m->h) return;
    const int sh = (u & 7) * 4;
    uint32_t *row = m->row[v];
    const uint32_t w = row[u >> 3];
    if (KEEP[k] > KEEP[(w >> sh) & 15]) row[u >> 3] = (w & ~(15u << sh)) | (uint32_t)k << sh;
}

/* row r's edges, its rows NEAR up and down known (none past last): a core near makes one a letter's.
 * a grey with none is a rail or a road's edge, and goes into the texels now: the scaled way took it for
 * a halo, as it does every grey till it is known. a tinted one it took for its own kind already */
static void near_row(tile_ctx_t *c, int r, int last)
{
    const int ya = r - NEAR < 0 ? 0 : r - NEAR, yb = r + NEAR > last ? last : r + NEAR;
    int any = 0;
    for (int x = 0; x < 256; x++) {
        const int v = mk_get(c, r, x);
        if (v < MK_DARK) {
            c->kind[x] = M_NONE;
            continue;
        }
        int near = 0;
        const int xa = x - NEAR < 0 ? 0 : x - NEAR, xb = x + NEAR > 255 ? 255 : x + NEAR;
        for (int y = ya; y <= yb && !near; y++) {
            for (int q = xa; q <= xb && !near; q++) near = (mk_get(c, y, q) & 7) == MK_CORE;
        }
        if (near) {
            mk_set(c, r, x, v + MK_NEAR);
            c->kind[x] = M_NONE;
        } else {
            c->kind[x] = (uint8_t)(v == MK_DARK ? M_RAIL : v == MK_LIGHT ? M_ROAD : M_NONE);
            any |= v != MK_TINT;
        }
    }
    if (any) put_row(c, r, c->kind);
}

/* where a point falls among the texels: wx pixels east of xorg, wy0 + wyf the world's pixel rows down,
 * in texels and their fractions, a texel's middle at a whole one. a texel row's middle is in its pixel
 * row (p->row), half way down it */
static void texel_of(const map_plan_t *p, float wx, int32_t wy0, float wyf, float *tu, float *tv)
{
    int v = 0, hi = p->h - 1;                           // the last texel row whose middle is at or above it
    while (v < hi) {
        const int mid = (v + hi + 1) / 2;
        if ((float)(p->row[mid] - wy0) + 0.5f <= wyf) v = mid; else hi = mid - 1;
    }
    float fv = (float)v;
    if (v + 1 < p->h && p->row[v + 1] > p->row[v]) fv += ((float)(wy0 - p->row[v]) + wyf - 0.5f) / (float)(p->row[v + 1] - p->row[v]);
    *tv = fv;
    *tu = (wx * 65536.0f - (float)p->x0[v]) / (float)p->dx[v];
}

/* the word that pixel (s, x) is of, gathered over the rows the ring holds up to k, the last row whose
 * edges are known. whole when it has nothing under row s (or the tile is all read): then copied pixel
 * for pixel, or, if it is no word, the scaled way. not whole: left marked as seen till the row is done
 * (words_at), and gathered again from a row further down. one whose top would leave the ring is not
 * left: the scaled way */
static int word_at(tile_ctx_t *c, int s, int x, int k, int last, int16_t *b)
{
    /* the ring holds the rows up to k + NEAR (tile_row is NEAR rows on): the oldest it still has is k + NEAR - RING
     * + 1. with 22 rows the walk went from k - 21 all the same, and the 3 rows above the oldest were the newest
     * rows' slots, read as if 22 rows up: holes in tall names, bits of letters 22 px off. k - 21 again, held */
    const int lo = k + NEAR - RING + 1 < 0 ? 0 : k + NEAR - RING + 1;
    int n = 0, sp = 0, ncore = 0, over = 0;
    int x0 = x, x1 = x, y0 = s, y1 = s;
    seen_set(c, s, x);
    c->stack[sp++] = (uint16_t)(s << 8 | x);
    while (sp) {
        const uint16_t at = c->stack[--sp];
        const int px = at & 255, py = at >> 8;
        n++;
        ncore += (mk_get(c, py, px) & 7) == MK_CORE;
        if (px < x0) x0 = px;
        if (px > x1) x1 = px;
        if (py < y0) y0 = py;
        if (py > y1) y1 = py;
        for (int dy = -2; dy <= 2; dy++) {
            const int y = py + dy;
            if (y < lo || y > k) continue;
            const int reach = dy == 0 ? GAP + 1 : dy == 1 || dy == -1 ? 2 : 1;
            for (int q = px - reach; q <= px + reach; q++) {
                if (q < 0 || q > 255 || seen_get(c, y, q) || !word_mark(mk_get(c, y, q))) continue;
                if (sp == STACK) {                      // more than a word holds: no word
                    over = 1;
                    continue;
                }
                seen_set(c, y, q);
                c->stack[sp++] = (uint16_t)(y << 8 | q);
            }
        }
    }
    const int whole = last || y1 <= s;
    if (!whole && k - y0 < RING - NEAR - 3) {           // more of it may come: left for a row further down (19, as ever)
        if (x0 < b[0]) b[0] = (int16_t)x0;
        if (x1 > b[1]) b[1] = (int16_t)x1;
        if (y0 < b[2]) b[2] = (int16_t)y0;
        if (y1 > b[3]) b[3] = (int16_t)y1;
        return 1;
    }
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    const int word = whole && !over && h <= WORD_H && w <= WORD_W && ncore >= 2 && ncore * 6 >= n;
    float tu = 0, tv = 0, cx = 0, cy = 0;
    if (word) {
        cx = x0 <= GAP ? 0.0f : x1 >= 255 - GAP ? 256.0f : (x0 + x1 + 1) * 0.5f;
        cy = y0 <= 2 ? 0.0f : y1 >= 253 ? 256.0f : (y0 + y1 + 1) * 0.5f;
        const float wx = (float)((int64_t)c->t.x * 256 - c->p->xorg) + cx;
        texel_of(c->p, wx, c->t.y * 256 + (int32_t)cy, cy - floorf(cy), &tu, &tv);
    }
    for (int y = y0; y <= y1; y++) {
        int any = 0;
        if (!word) memset(c->kind, M_NONE, 256);       // put_row reads a texel's pixels past the word's own
        for (int q = x0; q <= x1; q++) {
            if (!seen_get(c, y, q)) continue;
            const int v = mk_get(c, y, q), core = (v & 7) == MK_CORE;
            if (word) {                                 // the pixel's middle, the texel whose middle is nearest
                const float uf = tu + ((float)q + 0.5f - cx), vf = tv + ((float)y + 0.5f - cy);
                put_texel(c->m, (int)floorf(uf + 0.5f), (int)floorf(vf + 0.5f), core ? M_TEXT : M_SOFT);
            } else {
                int by = core;                          // the scaled way: an edge beside a core is its soft edge
                for (int dy = -1; dy <= 1 && !by; dy++) {
                    for (int dx = -1; dx <= 1 && !by; dx++) {
                        const int yy = y + dy, xx = q + dx;
                        by = yy >= lo && yy <= k && xx >= 0 && xx <= 255 && (mk_get(c, yy, xx) & 7) == MK_CORE;
                    }
                }
                const int e = mk_edge(v);
                c->kind[q] = (uint8_t)(core ? M_TEXT : by ? M_SOFT : e == MK_DARK ? M_RAIL : e == MK_LIGHT ? M_ROAD : M_NONE);
                any = 1;
            }
            mk_set(c, y, q, MK_NONE);                   // done with
        }
        if (any) put_row(c, y, c->kind);
    }
    return 0;
}

/* the words that end in row s, the rows up to k known. one not whole yet stays seen till the row is
 * done, so it is gathered once a row and not once a pixel of it */
static void words_at(tile_ctx_t *c, int s, int k, int last)
{
    int16_t b[4] = { 256, -1, 256, -1 };                // round the ones left: x0, x1, y0, y1
    for (int x = 0; x < 256; x++) {
        if (!seen_get(c, s, x) && word_mark(mk_get(c, s, x))) word_at(c, s, x, k, last, b);
    }
    for (int y = b[2]; y <= b[3]; y++) {
        for (int x = b[0]; x <= b[1]; x++) {
            const int v = mk_get(c, y, x);
            if (v & MK_SEEN) mk_set(c, y, x, v & 7);
        }
    }
}

/* a pixel row as it comes: its marks kept, the ground into the texels with every grey a halo till it
 * is known; the edges NEAR rows up known, and the words that end two rows above them gathered */
static void tile_row(void *ctx, const png_info_t *in, int y, const uint8_t *data)
{
    tile_ctx_t *c = ctx;
    for (int x = 0; x < 256; x++) {
        const uint8_t *px = data + x * in->bpp;
        const int v = letter_mark(in, px);
        mk_set(c, y, x, v);
        c->kind[x] = (uint8_t)(v == MK_NONE || v == MK_TINT ? pixel_class(c, in, px) : M_HALO);
    }
    put_row(c, y, c->kind);
    if (y >= NEAR) near_row(c, y - NEAR, y);
    if (y >= NEAR + 2) words_at(c, y - NEAR - 2, y - NEAR, 0);
    if (y == 255) {                                    // tile_head took only 256 rows: the rest now
        for (int r = 256 - NEAR; r < 256; r++) near_row(c, r, 255);
        for (int r = 254 - NEAR; r < 256; r++) words_at(c, r, 255, 1);
    }
}

int map_tile(map_t *m, const map_plan_t *p, int k, png_read_fn rd, void *rctx)
{
    if (k < 0 || k >= p->ntiles || !m->band[0] || m->w != p->w || m->h != p->h) return MAP_NOT_NOW;   // not the tile's fault
    static tile_ctx_t c;                                      // 300 bytes: off the task's stack
    memset(&c, 0, sizeof c);
    c.m = m;
    c.p = p;
    c.t = p->tiles[k];
    uint8_t *work = map_scratch ? map_scratch : malloc(MAP_SCRATCH);   // the rows only while a tile is read
    if (!work) return PNG_NO_MEMORY;
    c.ring = work;
    c.kind = work + RING * 128;
    c.stack = (uint16_t *)(void *)(work + RING * 128 + 256);
    memset(c.ring, 0, RING * 128);
    int r = png_decode(rd, rctx, tile_head, tile_row, &c);
    if (work != map_scratch) free(work);
    c.ring = c.kind = NULL;
    c.stack = NULL;
    m->tiles += r == PNG_OK;
    return r;
}
