#include "paper.h"
#include <math.h>
#include <stdlib.h>

/* the 4 greys as the glass shows them, measured in ViewOwl (display-types.json).
 * the 4.2" pulls its two greys towards black and white, the 5.79" keeps them apart */
const uint8_t PAPER_LEVEL[4] = { 0, PAPER_DARK, PAPER_LIGHT, 255 };

/* how much ink a pixel gets goes by steps, no dither: dithering made text and thin lines fuzzy on the
 * real panel, and dots that differ from frame to frame would spoil the partial refresh (epd.c).
 * the ink is the picture's light: the radar is drawn light on dark glass, paper is ink on white.
 * steps, not the nearest grey: the
 * nearest one put the radar's dim words, light 100 or so, on the light grey, and the 4.2"'s light grey
 * is all but white, so a third of the words were gone (the first run on one, 2026-11-22). now the
 * faint ground, its grid and the panels' shading stay white, the rings and the quiet marks go light
 * grey, the dim words dark grey and all the rest black */
#define INK_WHITE 36        // below: white
#define INK_LIGHT 80        // below: light grey
#define INK_DARK  150       // below: dark grey, from here black

static uint8_t s_code[256];         // ink -> code: 3 white, 2 light grey, 1 dark grey, 0 black
static int     s_built;

static void build(void)
{
    for (int ink = 0; ink < 256; ink++)
        s_code[ink] = (uint8_t)(ink < INK_WHITE ? 3 : ink < INK_LIGHT ? 2 : ink < INK_DARK ? 1 : 0);
    s_built = 1;
}

int paper_inks, paper_new_sheet, paper_hold;

/* a pixel in the paper's inks (paper.h P_*): black from a third of it, the greys from half. black a
 * little before half: the bold letters' thin strokes (the middle of a W) are a third covered here and
 * there, and went at half */
static inline uint8_t ink_code(uint32_t q)
{
    return (uint8_t)((q >> 16 & 255) >= 90 ? 0 : (q >> 8 & 255) >= 128 ? 1 : (q & 255) >= 128 ? 2 : 3);
}

/* an edge a step of grey between the ink and what is under it, by how much of the pixel it covers: the halftone
 * smoothing ("сгладить полутонами"). the black as far as a crisp edge's (black from a third, above), the greys
 * outside it: smoother, no thinner. never lighter than what is there */
static int paper_dark(uint32_t q) { return (q >> 16 & 255) >= 90 ? 3 : (q >> 8 & 255) >= 128 ? 2 : (q & 255) >= 128 ? 1 : 0; }

uint32_t paper_soft(uint32_t under, uint32_t ink, int k)
{
    static const uint32_t INK[4] = { P_WHITE, P_LIGHT, P_DARK, P_BLACK };
    const int u = paper_dark(under), i = paper_dark(ink);
    const int step = k >= 115 ? 3 : k >= 60 ? 2 : k >= 30 ? 1 : 0;   // thirds of the way from under to the ink
    return INK[u + (i - u) * step / 3];
}

/* a lightness g (0 black .. 1 white) as one of the inks: an ordered dither between the two either side of it, for
 * gradients. the glass blurs the 1 px pattern into the tone between */
uint32_t paper_tone(float g, int x, int y)
{
    static const uint32_t INK[4] = { P_BLACK, P_DARK, P_LIGHT, P_WHITE };
    static const float LV[4] = { 0.0f, PAPER_DARK / 255.0f, PAPER_LIGHT / 255.0f, 1.0f };
    static const uint8_t BAYER[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
    int i = 0;
    while (i < 2 && g > LV[i + 1]) i++;
    const float t = (g - LV[i]) / (LV[i + 1] - LV[i]);
    return t * 16.0f > (float)BAYER[y & 3][x & 3] + 0.5f ? INK[i + 1] : INK[i];
}

/* a line w wide with round ends, its edges soft where the canvas is (cv->soft), else gfx_line's. each pixel by its
 * distance from the line: a polyline's joints come out round, the darker of two kept */
void paper_line(canvas_t *cv, float x0, float y0, float x1, float y1, float w, uint32_t ink)
{
    if (!cv->soft) {
        gfx_line(cv, x0, y0, x1, y1, w, ink, 255);
        return;
    }
    const float r = w * 0.5f + 1.0f, dx = x1 - x0, dy = y1 - y0, ll = dx * dx + dy * dy;
    int ya = (int)floorf(fminf(y0, y1) - r), yb = (int)ceilf(fmaxf(y0, y1) + r);
    int xa = (int)floorf(fminf(x0, x1) - r), xb = (int)ceilf(fmaxf(x0, x1) + r);
    if (ya < cv->y0) ya = cv->y0;
    if (yb > cv->y0 + cv->h - 1) yb = cv->y0 + cv->h - 1;
    if (xa < 0) xa = 0;
    if (xb > GFX_W - 1) xb = GFX_W - 1;
    for (int y = ya; y <= yb; y++) {
        uint32_t *row = gfx_row(cv, y);
        if (!row) continue;
        const float py = (float)y + 0.5f;
        for (int x = xa; x <= xb; x++) {
            const float px = (float)x + 0.5f;
            float t = ll > 0.0f ? ((px - x0) * dx + (py - y0) * dy) / ll : 0.0f;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            const float ex = x0 + t * dx - px, ey = y0 + t * dy - py;
            const float cov = w * 0.5f + 0.5f - sqrtf(ex * ex + ey * ey);
            if (cov > 0.0f) row[x] = paper_soft(row[x], ink, cov >= 1.0f ? 256 : (int)(cov * 256.0f));
        }
    }
}

/* a disc of radius r, or a ring w wide (w 0: the disc), soft-edged where the canvas is, else gfx's. the
 * pictures and the clouds printed whole: their round edges without steps ("не так грубо прорисовать облачка") */
static void soft_round(canvas_t *cv, float cx, float cy, float r, float w, uint32_t ink)
{
    const float out = (w > 0.0f ? r + w * 0.5f : r) + 1.0f;
    int ya = (int)floorf(cy - out), yb = (int)ceilf(cy + out), xa = (int)floorf(cx - out), xb = (int)ceilf(cx + out);
    if (ya < cv->y0) ya = cv->y0;
    if (yb > cv->y0 + cv->h - 1) yb = cv->y0 + cv->h - 1;
    if (xa < 0) xa = 0;
    if (xb > GFX_W - 1) xb = GFX_W - 1;
    for (int y = ya; y <= yb; y++) {
        uint32_t *row = gfx_row(cv, y);
        if (!row) continue;
        const float dy = (float)y + 0.5f - cy;
        for (int x = xa; x <= xb; x++) {
            const float dx = (float)x + 0.5f - cx, d = sqrtf(dx * dx + dy * dy);
            const float cov = w > 0.0f ? w * 0.5f + 0.5f - fabsf(d - r) : r + 0.5f - d;
            if (cov > 0.0f) row[x] = paper_soft(row[x], ink, cov >= 1.0f ? 256 : (int)(cov * 256.0f));
        }
    }
}

void paper_disc(canvas_t *cv, float cx, float cy, float r, uint32_t ink)
{
    if (cv->soft) soft_round(cv, cx, cy, r, 0.0f, ink);
    else gfx_disc(cv, cx, cy, r, ink, 255);
}

void paper_ring(canvas_t *cv, float cx, float cy, float r, float w, uint32_t ink)
{
    if (cv->soft) soft_round(cv, cx, cy, r, w, ink);
    else gfx_ring(cv, cx, cy, r, w, ink, 255);
}

void paper_pack(const canvas_t *cv, uint8_t *fb)
{
    if (!s_built) build();
    const int inks = paper_inks;
    for (int r = 0; r < cv->h; r++) {
        const uint32_t *row = cv->px + r * GFX_W;
        uint8_t *out = fb + (cv->y0 + r) * PAPER_STRIDE;
        uint8_t acc = 0;
        for (int x = 0; x < GFX_W; x++) {
            uint32_t q = row[x];
            int l = (int)((((q >> 16) & 255) * 77 + ((q >> 8) & 255) * 150 + (q & 255) * 29) >> 8);
            acc = (uint8_t)(acc << 2 | (inks ? ink_code(q) : s_code[l]));   // negative: light is ink
            if ((x & 3) == 3) { out[x >> 2] = acc; acc = 0; }
        }
        if (GFX_W & 3) {                                     // pad the last byte with white
            int left = 4 - (GFX_W & 3);
            out[GFX_W >> 2] = (uint8_t)(acc << (2 * left) | ((1 << (2 * left)) - 1));
        }
    }
}
