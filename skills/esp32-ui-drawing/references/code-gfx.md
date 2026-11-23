# The renderer, complete

Verbatim from OWLOS-FLIGHT-RADAR `core/gfx.h` and `core/gfx.c` (the newest, whole-number inner
loops) plus what OWLOS-AIR-QUALITY-UI added to its copy (`gfx_sector`, the sin table, the flat span
in `gfx_disc`, `gfx_bright`). Status: runs on the 2.8" and 3.5" CYD and the round C3, measured on
hardware. Copy these two files as they are into `core/` of a new repository, they need only
`config.h` with `GFX_W` and `GFX_H`.

## gfx.h

```c
/* gfx - strip renderer for the RGB565 panels (size from config.h).
 * 320x240 frame is 153600 bytes, biggest free block after WiFi is ~110 KB (no PSRAM), so we
 * draw the scene once per strip of rows in 0x00RRGGBB, then dither to 565 and send by DMA.
 * 8 bit channels + dither keeps dark gradients from banding. all primitives clip to the strip */
#pragma once
#include <stdint.h>

/* hot loops go to IRAM on ESP32: a frame runs all this code ten times over
 * and every flash cache miss costs microseconds */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define CORE_HOT  IRAM_ATTR
#define CORE_DATA DRAM_ATTR
#else
#define CORE_HOT
#define CORE_DATA
#endif

#include "config.h"   // GFX_W, GFX_H come from the chosen display

/* the ESP32-C3 (RISC-V, no F extension) has no floating point unit: a float costs tens of
 * cycles there, so the busiest loops take a whole number way. the ESP32's float is quicker */
#ifndef GFX_SOFT_FLOAT                // the PC preview may set it, to see the C3's way drawn
#if defined(__riscv) && !defined(__riscv_flen)
#define GFX_SOFT_FLOAT 1
#else
#define GFX_SOFT_FLOAT 0
#endif
#endif

typedef struct {
    uint32_t *px;   /* GFX_W * h pixels, 0x00RRGGBB, row by row */
    int y0;         /* first screen row of the strip */
    int h;          /* rows in strip */
    int helper;     /* drawn by the second core (main.c): its time is not in ui_t.prof */
} canvas_t;

#define RGB(r, g, b) ((uint32_t)((((r) & 255) << 16) | (((g) & 255) << 8) | ((b) & 255)))

// alpha is 0..256 everywhere (256 = opaque) so >> 8 is exact
static inline uint32_t gfx_mix(uint32_t d, uint32_t s, uint32_t a)
{
    uint32_t ia = 256 - a;
    uint32_t rb = ((s & 0xFF00FFu) * a + (d & 0xFF00FFu) * ia) >> 8;
    uint32_t g  = ((s & 0x00FF00u) * a + (d & 0x00FF00u) * ia) >> 8;
    return (rb & 0xFF00FFu) | (g & 0x00FF00u);
}

static inline uint32_t gfx_scale(uint32_t c, uint32_t a)
{
    return ((((c & 0xFF00FFu) * a) >> 8) & 0xFF00FFu) | ((((c & 0x00FF00u) * a) >> 8) & 0x00FF00u);
}

// saturating add per channel, light stops at 255 and never wraps to black
static inline uint32_t gfx_add(uint32_t d, uint32_t s)
{
    /* branchless: add low 7 bits of each channel, put the top bit back with xor,
     * then every channel that carried out gets filled with 0xFF */
    uint32_t t = (d & 0x7F7F7Fu) + (s & 0x7F7F7Fu);
    uint32_t sum = t ^ ((d ^ s) & 0x808080u);
    uint32_t ovf = ((d & s) | ((d | s) & t)) & 0x808080u;
    return sum | ((ovf >> 7) * 0xFFu);
}

/* fast sqrt for anti-aliasing: inverse root guess + one Newton step, ~0.2% off,
 * plenty for AA. ESP32 has no sqrt instruction and AA shapes need one per pixel */
static inline float gfx_sqrt(float x)
{
    if (x <= 0.0f) return 0.0f;
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759dfu - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

// pointer to screen row y, or 0 if its not in this strip
static inline uint32_t *gfx_row(const canvas_t *cv, int y)
{
    int r = y - cv->y0;
    return (r >= 0 && r < cv->h) ? cv->px + r * GFX_W : 0;
}

// is any of the rows ya..yb inside this strip?
static inline int gfx_touches(const canvas_t *cv, int ya, int yb)
{
    return yb >= cv->y0 && ya < cv->y0 + cv->h;
}

void gfx_fill(canvas_t *cv, int x, int y, int w, int h, uint32_t c, int a);
void gfx_hline(canvas_t *cv, int x0, int x1, int y, uint32_t c, int a);
void gfx_vline(canvas_t *cv, int x, int y0, int y1, uint32_t c, int a);
void gfx_hatch(canvas_t *cv, int x, int y, int w, int h, uint32_t c, int a);

/* --- anti-aliased, float coords at pixel centres --- */
void gfx_line(canvas_t *cv, float x0, float y0, float x1, float y1, float w, uint32_t c, int a);
void gfx_ring(canvas_t *cv, float cx, float cy, float r, float w, uint32_t c, int a);
void gfx_disc(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a);
void gfx_glow(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a);   /* additive */
void gfx_dot(canvas_t *cv, float x, float y, uint32_t c, int a);   /* a point, shared over 2x2 pixels */
uint32_t gfx_isqrt(uint32_t v);                                     /* whole number square root */
void gfx_tri(canvas_t *cv, float x0, float y0, float x1, float y1, float x2, float y2,
             uint32_t c, int a);
// convex polygon, xy = x0,y0,x1,y1.. (3..12 corners, either winding)
void gfx_poly(canvas_t *cv, const float *xy, int n, uint32_t c, int a);
// arc as a polyline. degrees, 0 = up, clockwise
void gfx_arc(canvas_t *cv, float cx, float cy, float r, float deg0, float deg1, float w,
             uint32_t c, int a);

// dither strip to RGB565, high byte first like the panel wants
void gfx_pack565(const canvas_t *cv, uint16_t *out);
```

## gfx.c

```c
// drawing primitives, everything clips to the current strip (see gfx.h)
#include "gfx.h"
#include <math.h>

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

// clip row range to the strip, returns 0 if nothing left
static int clip_rows(const canvas_t *cv, int *ya, int *yb)
{
    if (*ya < cv->y0) *ya = cv->y0;
    if (*yb > cv->y0 + cv->h - 1) *yb = cv->y0 + cv->h - 1;
    return *ya <= *yb;
}

static inline void plot(uint32_t *row, int x, uint32_t c, int a)
{
    if (a >= 256) row[x] = c;
    else if (a > 0) row[x] = gfx_mix(row[x], c, (uint32_t)a);
}

CORE_HOT void gfx_fill(canvas_t *cv, int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = imax(x, 0), x1 = imin(x + w, GFX_W) - 1;
    int ya = y, yb = y + h - 1;
    if (x0 > x1 || !clip_rows(cv, &ya, &yb)) return;
    for (int yy = ya; yy <= yb; yy++) {
        uint32_t *row = gfx_row(cv, yy);
        for (int xx = x0; xx <= x1; xx++) plot(row, xx, c, a);
    }
}

CORE_HOT void gfx_hline(canvas_t *cv, int x0, int x1, int y, uint32_t c, int a)
{
    uint32_t *row = gfx_row(cv, y);
    if (!row) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    x0 = imax(x0, 0); x1 = imin(x1, GFX_W - 1);
    for (int x = x0; x <= x1; x++) plot(row, x, c, a);
}

CORE_HOT void gfx_vline(canvas_t *cv, int x, int y0, int y1, uint32_t c, int a)
{
    if (x < 0 || x >= GFX_W) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (!clip_rows(cv, &y0, &y1)) return;
    for (int y = y0; y <= y1; y++) plot(gfx_row(cv, y), x, c, a);
}

// diagonal stripes, the "caution tape" bars like on the reference HUDs
CORE_HOT void gfx_hatch(canvas_t *cv, int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = imax(x, 0), x1 = imin(x + w, GFX_W) - 1;
    int ya = y, yb = y + h - 1;
    if (x0 > x1 || !clip_rows(cv, &ya, &yb)) return;
    for (int yy = ya; yy <= yb; yy++) {
        uint32_t *row = gfx_row(cv, yy);
        for (int xx = x0; xx <= x1; xx++)
            if (((xx + yy) & 3) < 2) plot(row, xx, c, a);
    }
}

uint32_t gfx_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* the per-pixel work below is in whole numbers, 1/256 of a pixel: the ESP32-C3 has no floating
 * point unit and a float there costs tens of cycles. floats only set things up, once a call */
#define FX 256

/* AA line as a capsule: coverage of a pixel = its distance to the segment. along the line and
 * across it are kept in 1/256 px and step by an add a pixel; only the round ends need a root.
 * each row only walks the span the line can reach, else long steep lines would cost the whole
 * bounding box */
CORE_HOT void gfx_line(canvas_t *cv, float x0, float y0, float x1, float y1, float w, uint32_t c, int a)
{
    float hw = w * 0.5f;
    if (w < 1.0f) { a = (int)(a * w); hw = 0.5f; }   // thinner than 1px: draw 1px but fainter
    if (a <= 0) return;
    float reach = hw + 1.0f;
    int ya = (int)floorf(fminf(y0, y1) - reach), yb = (int)ceilf(fmaxf(y0, y1) + reach);
    if (!clip_rows(cv, &ya, &yb)) return;
    float minx = fminf(x0, x1) - reach, maxx = fmaxf(x0, x1) + reach;
    float dx = x1 - x0, dy = y1 - y0;
    float ady = fabsf(dy), len = gfx_sqrt(dx * dx + dy * dy);
    float ux = len > 1e-3f ? dx / len : 1.0f, uy = len > 1e-3f ? dy / len : 0.0f;
    // along(x,y) = (x-x0)*ux + (y-y0)*uy, across = (x-x0)*-uy + (y-y0)*ux
    const int32_t a_x = (int32_t)lroundf(ux * FX), a_y = (int32_t)lroundf(uy * FX);
    const int32_t a_0 = (int32_t)lroundf(-(x0 * ux + y0 * uy) * FX);
    const int32_t p_x = -a_y, p_y = a_x, p_0 = (int32_t)lroundf((x0 * uy - y0 * ux) * FX);
    const int32_t L = (int32_t)lroundf(len * FX), edge = (int32_t)lroundf((hw + 0.5f) * FX);
    for (int y = ya; y <= yb; y++) {
        float sa = minx, sb = maxx;
        if (ady > 0.5f && ady * 2.0f > fabsf(dx)) {
            // steep: line crosses this row near one x, only look around it
            float t = ((float)y - y0) / dy;
            float xc = x0 + dx * t;
            float half = reach * len / ady + 1.0f;
            sa = fmaxf(minx, xc - half);
            sb = fminf(maxx, xc + half);
        }
        int xa = imax((int)floorf(sa), 0), xb = imin((int)ceilf(sb), GFX_W - 1);
        uint32_t *row = gfx_row(cv, y);
        int32_t al = a_x * xa + a_y * y + a_0, ac = p_x * xa + p_y * y + p_0;
        for (int x = xa; x <= xb; x++, al += a_x, ac += p_x) {
            int32_t d;
            if (al < 0 || al > L) {                    // past an end: the distance to that end
                uint32_t e = (uint32_t)(al < 0 ? -al : al - L), q = (uint32_t)(ac < 0 ? -ac : ac);
                if (e >= (uint32_t)edge || q >= (uint32_t)edge) continue;   // that far on either axis: no root needed
                d = (int32_t)gfx_isqrt(e * e + q * q);
            } else {
                d = ac < 0 ? -ac : ac;
            }
            int32_t cov = edge - d;
            if (cov <= 0) continue;
            plot(row, x, c, cov >= FX ? a : (a * cov) >> 8);
        }
    }
}

// a soft dot at a sub-pixel spot, shared out over the 2x2 pixels round it: a trail's point
CORE_HOT void gfx_dot(canvas_t *cv, float x, float y, uint32_t c, int a)
{
    int fx = (int)lroundf(x * 16.0f) - 8, fy = (int)lroundf(y * 16.0f) - 8;   // the dot's centre is a pixel's centre
    int ix = fx >> 4, iy = fy >> 4, wx = fx & 15, wy = fy & 15;
    for (int j = 0; j < 2; j++) {
        uint32_t *row = gfx_row(cv, iy + j);
        if (!row) continue;
        int ay = j ? wy : 16 - wy;
        for (int i = 0; i < 2; i++) {
            int px = ix + i;
            if (px < 0 || px >= GFX_W) continue;
            int k = a * ay * (i ? wx : 16 - wx) >> 8;
            if (k > 0) plot(row, px, c, k);
        }
    }
}

CORE_HOT void gfx_ring(canvas_t *cv, float cx, float cy, float r, float w, uint32_t c, int a)
{
    float hw = w * 0.5f;
    if (w < 1.0f) { a = (int)(a * w); hw = 0.5f; }
    float ro = r + hw + 1.0f, ri = r - hw - 1.0f;
    int ya = (int)floorf(cy - ro), yb = (int)ceilf(cy + ro);
    if (!clip_rows(cv, &ya, &yb)) return;
    for (int y = ya; y <= yb; y++) {
        float dy = (float)y - cy;
        if (fabsf(dy) > ro) continue;
        float xo = gfx_sqrt(ro * ro - dy * dy);
        float xi = (ri > 0.0f && fabsf(dy) < ri) ? gfx_sqrt(ri * ri - dy * dy) : -1.0f;
        int xa = imax((int)floorf(cx - xo), 0), xb = imin((int)ceilf(cx + xo), GFX_W - 1);
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx;
            if (xi > 0.0f && fabsf(dx) < xi - 1.0f) {     // hack: jump over the hole inside
                x = (int)floorf(cx + xi - 1.0f);
                continue;
            }
            float d = gfx_sqrt(dx * dx + dy * dy);
            float cov = hw + 0.5f - fabsf(d - r);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            plot(row, x, c, (int)(a * cov));
        }
    }
}

CORE_HOT void gfx_disc(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a)
{
    float ro = r + 1.0f;
    int ya = (int)floorf(cy - ro), yb = (int)ceilf(cy + ro);
    if (!clip_rows(cv, &ya, &yb)) return;
    for (int y = ya; y <= yb; y++) {
        float dy = (float)y - cy;
        int xa = imax((int)floorf(cx - ro), 0), xb = imin((int)ceilf(cx + ro), GFX_W - 1);
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx;
            float cov = r + 0.5f - gfx_sqrt(dx * dx + dy * dy);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            plot(row, x, c, (int)(a * cov));
        }
    }
}

// soft additive glow, (1 - d^2/r^2)^2 so no sqrt needed. in 1/16 px, whole numbers
CORE_HOT void gfx_glow(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a)
{
    if (a <= 0 || r <= 0.5f) return;
    int ya = (int)floorf(cy - r), yb = (int)ceilf(cy + r);
    if (!clip_rows(cv, &ya, &yb)) return;
    const int32_t cx16 = (int32_t)lroundf(cx * 16.0f), cy16 = (int32_t)lroundf(cy * 16.0f);
    const int32_t r16 = (int32_t)lroundf(r * 16.0f), r2 = r16 * r16;
    const int32_t inv = (256 << 16) / r2;              // d2 * inv >> 16 = 256 d2 / r2
    int xa = imax((int)floorf(cx - r), 0), xb = imin((int)ceilf(cx + r), GFX_W - 1);
    for (int y = ya; y <= yb; y++) {
        int32_t dy = y * 16 - cy16, dy2 = dy * dy;
        if (dy2 >= r2) continue;
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            int32_t dx = x * 16 - cx16, d2 = dx * dx + dy2;
            if (d2 >= r2) continue;
            int32_t f = 256 - ((d2 * inv) >> 16);
            int32_t k = (a * ((f * f) >> 8)) >> 8;
            if (k > 0) row[x] = gfx_add(row[x], gfx_scale(c, (uint32_t)(k > 256 ? 256 : k)));
        }
    }
}

/* convex shape, anti-aliased: the coverage of a pixel is its distance to the nearest edge. the
 * edges are set up once in floats, then each is a value in 1/256 px that steps by an add a pixel */
CORE_HOT static void convex(canvas_t *cv, const float *xy, int n, uint32_t c, int a)
{
    float area = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1];
    }
    if (fabsf(area) < 1e-3f) return;
    float sgn = area > 0 ? 1.0f : -1.0f, minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    int32_t ex[12], ey[12], e0[12];                    // inside when ex*x + ey*y + e0 >= 0
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * j], y1 = xy[2 * j + 1];
        float vx = x1 - x0, vy = y1 - y0, l = gfx_sqrt(vx * vx + vy * vy);
        if (l < 1e-4f) l = 1e-4f;
        float nx = -vy / l * sgn, ny = vx / l * sgn;
        ex[i] = (int32_t)lroundf(nx * FX);
        ey[i] = (int32_t)lroundf(ny * FX);
        e0[i] = (int32_t)lroundf(-(nx * x0 + ny * y0) * FX);
        minx = fminf(minx, x0); maxx = fmaxf(maxx, x0);
        miny = fminf(miny, y0); maxy = fmaxf(maxy, y0);
    }
    int ya = (int)floorf(miny) - 1, yb = (int)ceilf(maxy) + 1;
    if (!clip_rows(cv, &ya, &yb)) return;
    int xa = imax((int)floorf(minx) - 1, 0), xb = imin((int)ceilf(maxx) + 1, GFX_W - 1);
    for (int y = ya; y <= yb; y++) {
        uint32_t *row = gfx_row(cv, y);
        /* where on this row every edge leaves some coverage: edge i needs ex*x + c > -FX/2. a turned
         * thin part fills a few pixels of its box a row, the rest is not walked. the division
         * rounds towards zero, which only widens the span */
        int sa = xa, sb = xb;
        for (int i = 0; i < n && sa <= sb; i++) {
            int32_t need = -FX / 2 + 1 - (ey[i] * y + e0[i]);
            if (ex[i] > 0) sa = imax(sa, need / ex[i]);
            else if (ex[i] < 0) sb = imin(sb, need / ex[i]);
            else if (need > 0) sb = sa - 1;
        }
        if (sa > sb) continue;
        int32_t e[12];
        for (int i = 0; i < n; i++) e[i] = ex[i] * sa + ey[i] * y + e0[i];
        for (int x = sa; x <= sb; x++) {
            int32_t m = e[0];
            e[0] += ex[0];
            for (int i = 1; i < n; i++) {
                if (e[i] < m) m = e[i];
                e[i] += ex[i];
            }
            int32_t cov = m + FX / 2;
            if (cov <= 0) continue;
            plot(row, x, c, cov >= FX ? a : (a * cov) >> 8);
        }
    }
}

CORE_HOT void gfx_tri(canvas_t *cv, float x0, float y0, float x1, float y1, float x2, float y2,
             uint32_t c, int a)
{
    const float xy[6] = { x0, y0, x1, y1, x2, y2 };
    convex(cv, xy, 3, c, a);
}

void gfx_arc(canvas_t *cv, float cx, float cy, float r, float deg0, float deg1, float w,
             uint32_t c, int a)
{
    if (!gfx_touches(cv, (int)(cy - r - w - 1), (int)(cy + r + w + 1))) return;
    float span = deg1 - deg0;
    int n = (int)ceilf(fabsf(span) / 4.0f);   // one segment per 4 degrees
    if (n < 1) n = 1;
    float k = 3.14159265f / 180.0f;
    float px = cx + r * sinf(deg0 * k), py = cy - r * cosf(deg0 * k);
    for (int i = 1; i <= n; i++) {
        float d = (deg0 + span * (float)i / (float)n) * k;
        float qx = cx + r * sinf(d), qy = cy - r * cosf(d);
        gfx_line(cv, px, py, qx, qy, w, c, a);
        px = qx; py = qy;
    }
}

/* 4x4 ordered (bayer) dither 888 -> 565, then byte swap to big endian
 * so DMA can send the buffer as is */
CORE_HOT void gfx_pack565(const canvas_t *cv, uint16_t *out)
{
    /* NOTE: tried three lookup tables instead of this math, it was slower on the
     * board (10.8 ms a frame vs 9.7). dont go back to tables */
    static const CORE_DATA uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    for (int r = 0; r < cv->h; r++) {
        const uint32_t *row = cv->px + r * GFX_W;
        const uint8_t *bm = bayer[(cv->y0 + r) & 3];
        uint16_t *o = out + r * GFX_W;
        for (int x = 0; x < GFX_W; x++) {
            uint32_t p = row[x];
            unsigned d = bm[x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3;
            unsigned gg = (((p >> 8) & 255) + (d >> 2)) >> 2;
            unsigned bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            uint16_t v = (uint16_t)((rr << 11) | (gg << 5) | bb);
            o[x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}

/* convex polygon, any number of corners (up to 12), anti-aliased like gfx_tri: the coverage of
 * a pixel is its distance to the nearest edge. silhouettes are a few of these, overlapping */
CORE_HOT void gfx_poly(canvas_t *cv, const float *xy, int n, uint32_t c, int a)
{
    if (n < 3 || n > 12) return;
    convex(cv, xy, n, c, a);
}
```

## What Air Quality added

Its copy keeps the older float loops (`gfx_line` and `gfx_tri` in floats, fine on an ESP32 with an
FPU, slow on the C3). The parts below are new there. They run on the ESP32 at 480x320, measured on
hardware. They are written in floats: on a C3 they would need the whole-number treatment first.

### The declarations (gfx.h)

```c
/* an annular sector, anti-aliased on all four edges: radii r0..r1, degrees d0..d1 (0 = up,
 * clockwise, d1 > d0, a span of 360 or more is the whole ring). the effects come in fx (NULL = none):
 *   fade    alpha rises along the arc from (1 - fade) at d0 to 1 at d1: a comet tail.
 *           negative: the other way, bright at d0
 *   add     light is added (glows), not mixed
 *   dashes  > 0: so many dashes around the full circle, duty of them lit, turned by phase degrees
 *   glint   a brighter spot at that angle, glint_w degrees wide, glint_gain times brighter at
 *           its middle (a scanner sweeping the ring); glint_w 0 = none */
typedef struct {
    float fade;
    int   add;
    int   dashes;
    float duty, phase;
    float glint, glint_w, glint_gain;
} sector_fx_t;
void gfx_sector(canvas_t *cv, float cx, float cy, float r0, float r1, float d0, float d1,
                uint32_t c, int a, const sector_fx_t *fx);

/* everything that goes to the glass passes gfx_pack565, so one multiplier there dims the whole
 * picture: the night mode, on a panel whose backlight is wired to power (256 = full) */
extern int gfx_bright;

// the tables (sin/cos, the angle of every pixel round a centre): once, before any drawing
void gfx_init(void);
// sin and cos of degrees from the table, 0 = up the way gfx_sector counts: x = sin, y = -cos
void gfx_sincos(float deg, float *s, float *c);

// dither strip to RGB565, high byte first like the panel wants
void gfx_pack565(const canvas_t *cv, uint16_t *out);
```

### gfx_disc with a flat middle span

The inside of a row is one flat span, only the rim needs a distance. Swap this in when discs are large.

```c
/* the inside of a row is one flat span, only the pixels at the rim need their distance: a disc
 * of radius 100 is 31000 pixels and a few hundred of them are rim */
CORE_HOT void gfx_disc(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a)
{
    float ro = r + 1.0f, rf = r - 0.5f;
    int ya = (int)floorf(cy - ro), yb = (int)ceilf(cy + ro);
    if (!clip_rows(cv, &ya, &yb)) return;
    for (int y = ya; y <= yb; y++) {
        float dy = (float)y - cy;
        if (fabsf(dy) > ro) continue;
        float xo = gfx_sqrt(ro * ro - dy * dy);
        int xa = imax((int)floorf(cx - xo), 0), xb = imin((int)ceilf(cx + xo), GFX_W - 1);
        int fa = xb + 1, fb = xb;                               // the full span, empty by default
        if (rf > fabsf(dy)) {
            float xf = gfx_sqrt(rf * rf - dy * dy) - 0.75f;
            fa = imax((int)ceilf(cx - xf), xa);
            fb = imin((int)floorf(cx + xf), xb);
        }
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            if (x == fa && fa <= fb) {
                if (a >= 256) for (; x <= fb; x++) row[x] = c;
                else for (; x <= fb; x++) row[x] = gfx_mix(row[x], c, (uint32_t)a);
                x = fb;
                continue;
            }
            float dx = (float)x - cx;
            float cov = r + 0.5f - gfx_sqrt(dx * dx + dy * dy);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            plot(row, x, c, (int)(a * cov));
        }
    }
}
```

### The sine table and gfx_sector

An annular sector with fade, dashes and a glint. All round HUDs are built from this one call.

```c
/* --- tables: sin/cos and the angle of every pixel around a centre ---
 * the HUDs asked for an atan2 and a division for every pixel of every arc and a sinf/cosf for
 * every tick of every strip: on the ESP32 that was most of a frame. both are tables now */

#define SIN_STEPS 4                            // a value every quarter degree
static float s_sin[360 * SIN_STEPS + 1];
#define ANG_N 120                              // pixels from the centre the angle table covers
static uint16_t *s_ang;                        // [ay][ax]: degrees from the vertical, 0..90 in 1/728 steps

static inline float deg_of(float dx, float dy);

void gfx_init(void)
{
    if (s_sin[SIN_STEPS * 90] > 0.5f) return;   // done
    for (int i = 0; i <= 360 * SIN_STEPS; i++) s_sin[i] = sinf((float)i / SIN_STEPS * 3.14159265f / 180.0f);
    /* the angle table: measured on the board it took a HUD from 62 to 60 ms and cost 29 KB of the
     * 98 KB left, so it stays off (s_ang NULL, the polynomial atan runs). GFX_ANGLE_TABLE turns it on */
#ifdef GFX_ANGLE_TABLE
    s_ang = malloc(sizeof(uint16_t) * ANG_N * ANG_N);
#endif
    if (s_ang)
        for (int ay = 0; ay < ANG_N; ay++)
            for (int ax = 0; ax < ANG_N; ax++)
                s_ang[ay * ANG_N + ax] = (uint16_t)lroundf(deg_of((float)ax, -(float)ay) * (65535.0f / 90.0f));
}

void gfx_sincos(float deg, float *s, float *c)
{
    float f = deg * SIN_STEPS;
    f = fmodf(f, 360.0f * SIN_STEPS);
    if (f < 0.0f) f += 360.0f * SIN_STEPS;
    int i = (int)f;
    float t = f - (float)i;
    float sv = s_sin[i] + (s_sin[i + 1] - s_sin[i]) * t;
    int j = i + 90 * SIN_STEPS;                 // cos = sin of 90 degrees on
    if (j >= 360 * SIN_STEPS) j -= 360 * SIN_STEPS;
    float cv = s_sin[j] + (s_sin[j + 1] - s_sin[j]) * t;
    *s = sv;
    *c = cv;
}

/* --- the annular sector, the stuff the round HUDs are made of --- */

// angle of (dx, dy) in degrees, 0 = up, clockwise, 0..360. polynomial atan, ~0.01 degree
static inline float deg_of(float dx, float dy)
{
    float x = -dy, y = dx;                                   // atan2(y, x) measured from up
    float ax = fabsf(x), ay = fabsf(y);
    float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx < 1e-6f) return 0.0f;
    float t = mn / mx, s = t * t;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * t + t;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.0f) r = 3.14159274f - r;
    if (y < 0.0f) r = -r;
    float d = r * 57.2957795f;
    return d < 0.0f ? d + 360.0f : d;
}

CORE_HOT void gfx_sector(canvas_t *cv, float cx, float cy, float r0, float r1, float d0, float d1,
                         uint32_t c, int a, const sector_fx_t *fx)
{
    if (a <= 0 || r1 <= 0.0f) return;
    if (r0 < 0.0f) r0 = 0.0f;
    float span = d1 - d0;
    if (span <= 0.0f) return;
    int full = span >= 360.0f;
    d0 = fmodf(d0, 360.0f);
    if (d0 < 0.0f) d0 += 360.0f;
    const float ro = r1 + 1.0f, ri = r0 - 1.0f;
    const float k = 3.14159265f / 180.0f;
    if (!gfx_touches(cv, (int)floorf(cy - ro) - 1, (int)ceilf(cy + ro) + 1)) return;
    /* only the box the arc can be in: its two ends at both radii, and the four compass points
     * that lie inside its span. a short arc on a big ring is then a few rows, not the ring.
     * half a circle or more covers most of the ring's box anyway: not worth the sums */
    float bx0 = cx - ro, bx1 = cx + ro, by0 = cy - ro, by1 = cy + ro;
    if (!full && span < 180.0f) {
        float s0, c0, s1, c1;
        gfx_sincos(d0, &s0, &c0);
        gfx_sincos(d0 + span, &s1, &c1);
        float px[4] = { s0 * ro, s1 * ro, s0 * ri, s1 * ri };
        float py[4] = { -c0 * ro, -c1 * ro, -c0 * ri, -c1 * ri };
        float mnx = 0, mxx = 0, mny = 0, mxy = 0;
        for (int i = 0; i < 4; i++) {
            if (i == 0 || px[i] < mnx) mnx = px[i];
            if (i == 0 || px[i] > mxx) mxx = px[i];
            if (i == 0 || py[i] < mny) mny = py[i];
            if (i == 0 || py[i] > mxy) mxy = py[i];
        }
        for (int q = 0; q < 4; q++) {                            // 0 up, 90 right, 180 down, 270 left
            float rel = (float)(q * 90) - d0;
            if (rel < 0.0f) rel += 360.0f;
            if (rel > span) continue;
            if (q == 0) mny = -ro;
            if (q == 1) mxx = ro;
            if (q == 2) mxy = ro;
            if (q == 3) mnx = -ro;
        }
        bx0 = cx + mnx - 1.0f; bx1 = cx + mxx + 1.0f; by0 = cy + mny - 1.0f; by1 = cy + mxy + 1.0f;
    }
    int ya = (int)floorf(by0), yb = (int)ceilf(by1);
    if (!clip_rows(cv, &ya, &yb)) return;
    const float fade = fx ? fx->fade : 0.0f;
    const int add = fx && fx->add, dashes = fx ? fx->dashes : 0;
    const float dash_deg = dashes > 0 ? 360.0f / (float)dashes : 0.0f;
    const float glint_w = fx ? fx->glint_w : 0.0f;
    const int need_ang = !full || fade != 0.0f || dashes > 0 || glint_w > 0.0f;
    const int bxa = imax((int)floorf(bx0), 0), bxb = imin((int)ceilf(bx1), GFX_W - 1);
    // the angle table works for a centre on a whole pixel and rings inside it
    const int icx = (int)cx, icy = (int)cy;
    const int table = s_ang && (float)icx == cx && (float)icy == cy && ro < (float)ANG_N - 1.0f;
    for (int y = ya; y <= yb; y++) {
        float dy = (float)y - cy;
        const int ady = y - icy < 0 ? icy - y : y - icy;
        const uint16_t *arow = table ? s_ang + ady * ANG_N : NULL;
        if (fabsf(dy) > ro) continue;
        float xo = gfx_sqrt(ro * ro - dy * dy);
        float xi = (ri > 0.0f && fabsf(dy) < ri) ? gfx_sqrt(ri * ri - dy * dy) : -1.0f;
        int xa = imax(imax((int)floorf(cx - xo), 0), bxa), xb = imin(imin((int)ceilf(cx + xo), GFX_W - 1), bxb);
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx;
            if (xi > 1.0f && fabsf(dx) < xi - 1.0f) {           // jump over the hole inside
                x = (int)floorf(cx + xi - 1.0f);
                continue;
            }
            float d = gfx_sqrt(dx * dx + dy * dy);
            float cov = d - r0 + 0.5f;                           // radial edges
            float c2 = r1 + 0.5f - d;
            if (c2 < cov) cov = c2;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            float ang = 0.0f, t = 0.0f;
            if (need_ang) {                                      // a plain full ring needs no angle at all
                if (arow) {
                    int adx = x - icx < 0 ? icx - x : x - icx;
                    float b = (float)arow[adx] * (90.0f / 65535.0f);
                    ang = x >= icx ? (y <= icy ? b : 180.0f - b) : (y > icy ? 180.0f + b : 360.0f - b);
                    if (ang >= 360.0f) ang -= 360.0f;
                } else {
                    ang = deg_of(dx, dy);
                }
                float rel = ang - d0;
                if (rel < 0.0f) rel += 360.0f;
                if (!full) {                                     // angular edges, in pixels along the circle
                    float px_per_deg = d * k;
                    if (rel > span) {
                        float out = rel - span, before = 360.0f - rel;
                        float dist = (out < before ? out : before) * px_per_deg;
                        if (dist >= 0.5f) continue;
                        cov *= 0.5f - dist;
                        t = out < before ? 1.0f : 0.0f;
                    } else {
                        float in = rel < span - rel ? rel : span - rel;
                        float e = in * px_per_deg + 0.5f;
                        if (e < 1.0f) cov *= e;
                        t = rel / span;
                    }
                } else {
                    t = rel / 360.0f;
                }
            }
            float al = (float)a * cov;
            if (fade > 0.0f) al *= 1.0f - fade + fade * t;
            else if (fade < 0.0f) al *= 1.0f + fade * t;              // the other way: bright at d0
            if (dashes > 0) {                                    // dashes with soft ends
                float p = fmodf(ang - fx->phase + 3600.0f, dash_deg) / dash_deg;
                float on = fx->duty;
                float e = (p < on ? fminf(p, on - p) : -fminf(p - on, 1.0f - p)) * dash_deg * d * k + 0.5f;
                if (e <= 0.0f) continue;
                if (e < 1.0f) al *= e;
            }
            if (glint_w > 0.0f) {
                float g = fabsf(ang - fx->glint);
                if (g > 180.0f) g = 360.0f - g;
                if (g < glint_w) al *= 1.0f + fx->glint_gain * (1.0f - g / glint_w);
            }
            int ai = (int)al;
            if (ai <= 0) continue;
            if (add) row[x] = gfx_add(row[x], gfx_scale(c, (uint32_t)(ai > 256 ? 256 : ai)));
            else plot(row, x, c, ai > 256 ? 256 : ai);
        }
    }
}
```

### gfx_pack565 with one brightness multiplier

A panel whose backlight is wired to power can still dim: every pixel passes here.

```c
int gfx_bright = 256;

/* 4x4 ordered (bayer) dither 888 -> 565, then byte swap to big endian
 * so DMA can send the buffer as is */
CORE_HOT void gfx_pack565(const canvas_t *cv, uint16_t *out)
{
    /* NOTE: tried three lookup tables instead of this math, it was slower on the
     * board (10.8 ms a frame vs 9.7). dont go back to tables */
    static const CORE_DATA uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    const int dim = gfx_bright < 256;
    const uint32_t br = (uint32_t)(gfx_bright < 0 ? 0 : gfx_bright);
    for (int r = 0; r < cv->h; r++) {
        const uint32_t *row = cv->px + r * GFX_W;
        const uint8_t *bm = bayer[(cv->y0 + r) & 3];
        uint16_t *o = out + r * GFX_W;
        for (int x = 0; x < GFX_W; x++) {
            uint32_t p = dim ? gfx_scale(row[x], br) : row[x];
            unsigned d = bm[x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3;
            unsigned gg = (((p >> 8) & 255) + (d >> 2)) >> 2;
            unsigned bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            uint16_t v = (uint16_t)((rr << 11) | (gg << 5) | bb);
            o[x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}
```

