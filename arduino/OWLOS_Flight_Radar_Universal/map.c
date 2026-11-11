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
    { 187, 187, 204, M_AERO },  { 218, 218, 224, M_AERO },                                // runway, apron
};

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

void map_end(map_t *m, const map_plan_t *p)
{
    m->lat = p->lat;
    m->lon = p->lon;
    m->kpx = p->kpx;
    m->gen++;
    m->ready = 1;
}

typedef struct {
    map_t            *m;
    const map_plan_t *p;
    tile_id_t         t;
    int               vp;              /* the first texel row not yet past */
    uint8_t           cls[256];        /* palette entry -> M_* */
    uint32_t          last_rgb;        /* the colour classed last: tiles without a palette */
    int               last_cls;
} tile_ctx_t;

static int tile_head(void *ctx, const png_info_t *in)
{
    tile_ctx_t *c = ctx;
    if (in->w != 256 || in->h != 256) return 0;
    for (int i = 0; i < in->npal; i++) c->cls[i] = (uint8_t)map_class(in->pal[i][0], in->pal[i][1], in->pal[i][2]);
    c->last_rgb = 0xFFFFFFFFu;
    return 1;
}

static int pixel_class(tile_ctx_t *c, const png_info_t *in, const uint8_t *px)
{
    if (in->type == 3) return c->cls[px[0]];
    uint32_t rgb = in->type < 2 || in->type == 4 ? px[0] * 0x010101u : (uint32_t)px[0] << 16 | px[1] << 8 | px[2];
    if (rgb != c->last_rgb) {
        c->last_rgb = rgb;
        c->last_cls = map_class((int)(rgb >> 16), (int)(rgb >> 8 & 255), (int)(rgb & 255));
    }
    return c->last_cls;
}

static void tile_row(void *ctx, const png_info_t *in, int y, const uint8_t *data)
{
    tile_ctx_t *c = ctx;
    const map_plan_t *p = c->p;
    map_t *m = c->m;
    const int32_t wy = c->t.y * 256 + y;
    while (c->vp < p->h && p->row[c->vp] < wy) c->vp++;
    const float hw = p->w * 0.5f, hh = p->h * 0.5f;
    const int64_t left = ((int64_t)c->t.x * 256 - p->xorg) * 65536, right = left + (256LL << 16);
    for (int v = c->vp; v < p->h && p->row[v] == wy; v++) {
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
            int k = pixel_class(c, in, data + px * in->bpp);
            const int sh = (u & 7) * 4;                      // a word at a time: it may be in the IRAM
            row[u >> 3] = (row[u >> 3] & ~(15u << sh)) | (uint32_t)k << sh;
        }
    }
}

int map_tile(map_t *m, const map_plan_t *p, int k, png_read_fn rd, void *rctx)
{
    if (k < 0 || k >= p->ntiles || !m->band[0] || m->w != p->w || m->h != p->h) return PNG_BAD;
    static tile_ctx_t c;                                      // 300 bytes: off the task's stack
    memset(&c, 0, sizeof c);
    c.m = m;
    c.p = p;
    c.t = p->tiles[k];
    int r = png_decode(rd, rctx, tile_head, tile_row, &c);
    m->tiles += r == PNG_OK;
    return r;
}
