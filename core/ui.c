/* ui: colour themes, helpers, the background, touch, touch alignment, strip dispatch.
 * the scope and its panel are in ui_scope.c, the list and setup in ui_pages.c */
#include "ui_int.h"
#include <stdlib.h>

const theme_t UI_THEME[UI_THEMES] = {
    /* name        bg0              bg1              glass             panel
                   prim               quiet              focus               dim
                   low                alarm              home */
    { "ARCTIC",   RGB(3, 8, 18),   RGB(9, 22, 46),  RGB(16, 42, 82),  RGB(8, 26, 54),
                  RGB(92, 225, 255), RGB(46, 130, 178), RGB(234, 247, 255), RGB(120, 160, 196),
                  RGB(255, 184, 72), RGB(255, 80, 88), RGB(255, 60, 220) },
    { "PHOSPHOR", RGB(2, 9, 4),    RGB(6, 24, 10),  RGB(10, 46, 20),  RGB(6, 28, 12),
                  RGB(90, 255, 130), RGB(40, 150, 70),  RGB(225, 255, 230), RGB(130, 190, 145),
                  RGB(255, 200, 70), RGB(255, 90, 80), RGB(255, 60, 220) },
    { "AMBER",    RGB(10, 5, 0),   RGB(26, 14, 2),  RGB(46, 26, 4),   RGB(28, 16, 2),
                  RGB(255, 180, 40), RGB(160, 100, 20), RGB(255, 244, 214), RGB(200, 160, 110),
                  RGB(110, 215, 255), RGB(255, 70, 150), RGB(130, 255, 60) },
    { "CRIMSON",  RGB(10, 1, 4),   RGB(26, 5, 10),  RGB(46, 10, 18),  RGB(28, 6, 12),
                  RGB(255, 80, 100), RGB(160, 40, 60),  RGB(255, 226, 230), RGB(205, 140, 150),
                  RGB(255, 205, 90), RGB(255, 255, 255), RGB(60, 235, 255) },
    { "SYNTH",    RGB(6, 2, 16),   RGB(20, 6, 38),  RGB(34, 12, 64),  RGB(20, 8, 40),
                  RGB(205, 125, 255), RGB(110, 60, 170), RGB(250, 238, 255), RGB(170, 145, 205),
                  RGB(255, 190, 80), RGB(255, 90, 130), RGB(130, 255, 60) },
};
const theme_t *TH = &UI_THEME[0];

static const float NICE[] = { 2, 3, 5, 8, 10, 15, 20, 25, 30, 40, 50, 60, 80, 100, 120, 150, 200, 250 };
static const float BOOT_RANGES[] = { 5, 10, 25, 50, 100, 200 };
#define NNICE ((int)(sizeof NICE / sizeof NICE[0]))
const char *const SERVER_NAME[FREE_SERVERS] = { "ADSB.LOL", "ADSB.FI" };
const char *const LOOK_NAME[LOOKS] = { "RADAR", "MAP", "CLASSIC", "WEATHER" };
const char *ui_screen_name;
uint32_t (*ui_clock_us)(void);
static uint32_t s_lap;

// time since the last lap goes to prof[layer]. layer -1 only starts the clock. the second core's
// strips are not timed: one clock, one core
void ui_lap(ui_t *u, const canvas_t *cv, int layer)
{
    if (!ui_clock_us || cv->helper) return;
    uint32_t now = ui_clock_us();
    if (layer >= 0 && layer < (int)(sizeof u->prof / sizeof u->prof[0])) u->prof[layer] += now - s_lap;
    s_lap = now;
}
int CAL_PT[CAL_POINTS][2];

void fr_cfg_defaults(fr_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = FR_CFG_VERSION;
    c->range = 50;
    c->map = 1;
    c->ground = 1;
    if (RADAR_EPAPER) c->look = LOOK_CLASSIC;         // paper's own screen (ui_paper.c): the others are the colour ones printed grey
    c->lat = 48.8945f;                               // Trencin, Slovakia, until the site sends a home
    c->lon = 18.0444f;
    snprintf(c->place, sizeof c->place, "Trenčín");
    snprintf(c->country, sizeof c->country, "Slovakia");
}

void scope_opt_defaults(scope_opt_t *o)
{
    memset(o, 0, sizeof *o);
    o->version = SCOPE_OPT_VERSION;                    // beam_step off, as in the original: the planes move all the time
}

static const float TO_KM[3] = { 1.0f, 1.852f, 1.609344f };

float unit_km(int dist_unit) { return TO_KM[dist_unit >= 0 && dist_unit < 3 ? dist_unit : 0]; }

const char *unit_name(int dist_unit)
{
    static const char *const N[3] = { "KM", "NM", "MI" };
    return N[dist_unit >= 0 && dist_unit < 3 ? dist_unit : 0];
}

float range_km(const fr_cfg_t *c)
{
    return clampf(c->range, RANGE_MIN, RANGE_MAX) * TO_KM[c->dist_unit < 3 ? c->dist_unit : 0];
}

// a texel a pixel: the square looks the whole screen, the radar the range ring's disc (and the round
// glass that in every look)
void ui_ground_size(const fr_cfg_t *c, int *w, int *h, int *disc)
{
    int square = !RADAR_ROUND && c->look != LOOK_RADAR && c->look != LOOK_WEATHER;   // the weather keeps the radar's, ready for it
    /* a texel a pixel of the glass. a row is whole words of 8 texels (map.h), so the disc's square goes up
     * to the next 8, a few texels past the ring, home still in its middle: the 4.2" e-paper's ring is 2 x 118
     * across, and the 4 texels past 29 words went past the row's end and broke the heap (map.c map_alloc) */
    const int d = (2 * G_PR + 7) & ~7;
    *w = square ? GFX_PW : d;
    *h = square ? GFX_PH : d;
    *disc = !square;
}
float ui_ground_kpx(const fr_cfg_t *c) { return (float)G_PR / range_km(c); }

float range_snap(float v)
{
    int best = 0;
    for (int i = 1; i < NNICE; i++)                  // nearest by ratio: 44 is nearer 40 than 50
        if (fabsf(logf(v / NICE[i])) < fabsf(logf(v / NICE[best]))) best = i;
    return NICE[best];
}

float range_step(float v, int dir)
{
    if (dir > 0) { for (int i = 0; i < NNICE; i++) if (NICE[i] > v * 1.01f) return NICE[i]; return NICE[NNICE - 1]; }
    for (int i = NNICE - 1; i >= 0; i--) if (NICE[i] < v * 0.99f) return NICE[i];
    return NICE[0];
}

/* --- helpers --- */

int in_box(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void toast(ui_t *u, const char *s)
{
    snprintf(u->toast, sizeof u->toast, "%s", s);
    u->toast_until = u->now + 2200u;
}

const plane_t *sel_plane(const ui_t *u)
{
    if (!u->sel) return NULL;
    for (int i = 0; i < u->snap.n; i++) if (u->snap.p[i].icao == u->sel) return &u->snap.p[i];
    return NULL;
}

static int emergency(const plane_t *p)
{
    return p->squawk == 7500 || p->squawk == 7600 || p->squawk == 7700;
}

// the height says the colour, the same in every theme
uint32_t alt_colour(const ui_t *u, const plane_t *p)
{
    if (emergency(p)) return ((u->now / 400u) & 1) ? C_ALARM : C_FOCUS;
    if (p->ground || !p->has_alt) return C_DIM;
    if (p->alt < 10000) return C_LOW;
    if (p->alt < 25000) return C_PRIM;
    return C_FOCUS;
}

void fmt_height(const ui_t *u, int feet, char *buf, int sz)
{
    if (u->cfg.alt_unit == ALT_M) snprintf(buf, (size_t)sz, "%d", (int)lroundf((float)feet * 0.3048f));
    else if (feet >= 10000) snprintf(buf, (size_t)sz, "FL%03d", (feet + 50) / 100);
    else snprintf(buf, (size_t)sz, "%d", feet);
}

/* height in the chosen unit. short form for labels: FL350 up high, 4200 down low */
void fmt_alt(const ui_t *u, const plane_t *p, char *buf, int sz, int short_form)
{
    if (p->ground) { snprintf(buf, (size_t)sz, "GND"); return; }
    if (!p->has_alt) { snprintf(buf, (size_t)sz, "---"); return; }
    if (short_form) { fmt_height(u, (int)p->alt, buf, sz); return; }
    if (u->cfg.alt_unit == ALT_M) snprintf(buf, (size_t)sz, "%d M", (int)lroundf((float)p->alt * 0.3048f));
    else snprintf(buf, (size_t)sz, "%d FT", (int)p->alt);
}

void fmt_dist(const ui_t *u, float km, char *buf, int sz)
{
    static const float K[3] = { 1.0f, 1.0f / 1.852f, 1.0f / 1.609344f };
    int du = u->cfg.dist_unit < 3 ? u->cfg.dist_unit : 0;
    float v = km * K[du];
    snprintf(buf, (size_t)sz, v < 10.0f ? "%.1f %s" : "%.0f %s", (double)v, unit_name(du));
}

const char *compass8(float deg)
{
    static const char *N[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    return N[(int)((wrap360(deg) + 22.5f) / 45.0f) % 8];
}

void chamfer_box(canvas_t *cv, int x, int y, int w, int h, uint32_t fill, int fill_a, uint32_t line, int line_a)
{
    if (!gfx_touches(cv, y - 1, y + h)) return;
    const int k = 5, x1 = x + w - 1, y1 = y + h - 1;
#if GFX_SCALED && GFX_SCALE_NUM != 3 && RADAR_OVERLAY
    /* scales other than 3/2 (the Waveshare 7B's 15/8, the JC8012's 5/2; chipguyhere): the straight sides as
     * whole pixels and the cut corners as smooth lines between pixel middles only line up at 3/2.
     * elsewhere the corners stepped. every side the same way instead, through the pixels' middles */
    {
        const float X0 = (float)x, Y0 = (float)y, X1 = (float)x1, Y1 = (float)y1, K = (float)k;
        const float P[6][2] = { { X0 + K, Y0 }, { X1, Y0 }, { X1, Y1 - K }, { X1 - K, Y1 }, { X0, Y1 }, { X0, Y0 + K } };
        /* the fill the same shape, a layout row at a time: from under the left side (one in, so its whole
         * glass pixels never show past the side) to under the right one, each cut row stopping at the cut.
         * a rectangle showed past the sides top and left and in the cut corners; the smooth polygon
         * did not, but cost so much a keyboard page took seconds */
        if (fill_a) {
            for (int yy = y + 1; yy <= y1; yy++) {
                const int l = yy < y + k ? x + (y + k - yy) : x + 1;
                const int r = yy > y1 - k ? x1 - (yy - (y1 - k)) : x1;
                if (r >= l) gfx_fill(cv, l, yy, r - l + 1, 1, fill, fill_a);
            }
        }
        for (int i = 0; i < 6; i++) gfx_line(cv, P[i][0], P[i][1], P[(i + 1) % 6][0], P[(i + 1) % 6][1], 1.0f, line, line_a);
        return;
    }
#endif
    if (fill_a) gfx_fill(cv, x, y, w, h, fill, fill_a);
    // the straight sides are whole pixels, only the two cut corners need smoothing
    gfx_hline(cv, x + k + 1, x1, y, line, line_a);
    gfx_vline(cv, x1, y + 1, y1 - k - 1, line, line_a);
    gfx_line(cv, (float)x1, (float)(y1 - k), (float)(x1 - k), (float)y1, 1.0f, line, line_a);
    gfx_hline(cv, x, x1 - k - 1, y1, line, line_a);
    gfx_vline(cv, x, y + k + 1, y1 - 1, line, line_a);
    gfx_line(cv, (float)x, (float)(y + k), (float)(x + k), (float)y, 1.0f, line, line_a);
}

/* --- the classic look's things on the map: light, rounded, a soft shadow, dark ink, the way a
 * map on paper floats its buttons. the instrument's cut corners belong to the dark looks --- */

// a box with round corners, smooth at the edges, one coat of colour all over (a shadow stays even)
void round_box(canvas_t *cv, int x, int y, int w, int h, int r, uint32_t c, int a)
{
    if (!gfx_touches(cv, y, y + h - 1)) return;
    w = GFX_Z(x + w) - GFX_Z(x);                       // from here on it is pixels of the glass
    h = GFX_Z(y + h) - GFX_Z(y);
    x = GFX_Z(x);
    y = GFX_Z(y);
    r = GFX_Z(r);
    const float x0 = (float)(x + r), x1 = (float)(x + w - r), y0 = (float)(y + r), y1 = (float)(y + h - r);   // the corners' middles
    for (int yy = y; yy < y + h; yy++) {
        uint32_t *row = gfx_row(cv, yy);
        if (!row) continue;
        float py = yy + 0.5f, dy = py < y0 ? y0 - py : py > y1 ? py - y1 : 0;
        for (int xx = x < 0 ? 0 : x; xx < x + w && xx < GFX_PW; xx++) {
            float px = xx + 0.5f, dx = px < x0 ? x0 - px : px > x1 ? px - x1 : 0;
            int k = a;
            if (dx > 0 && dy > 0) {                    // in a corner: how much of the pixel is inside its circle
                float cov = clampf(r - sqrtf(dx * dx + dy * dy) + 0.5f, 0, 1);
                k = (int)(a * cov);
            }
            if (k > 0) row[xx] = gfx_mix(row[xx], c, (uint32_t)k);
        }
    }
}

// a card for words on the light map
void paper_card(canvas_t *cv, int x, int y, int w, int h)
{
    round_box(cv, x, y + 1, w, h, 4, RGB(0, 0, 0), 40);
    round_box(cv, x, y, w, h, 4, CL_PAPER, 225);
}

// state 0 up, 1 lit (what it opens is open), 2 pressed: it sinks into its shadow
void map_button(canvas_t *cv, int x, int y, int w, int h, const char *label, int state)
{
    const int down = state == 2;
    if (!down) round_box(cv, x, y + 2, w, h, 6, RGB(0, 0, 0), 55);
    round_box(cv, x, y + down, w, h, 6, CL_RIM, 255);
    round_box(cv, x + 1, y + 1 + down, w - 2, h - 2, 5, down ? RGB(222, 227, 234) : state ? RGB(212, 228, 250) : CL_PAPER, 255);
    if (!label || !*label) return;
    int sc = micro_width(label, 2) <= w - 6 ? 2 : 1, lw = micro_width(label, sc);
    micro_text(cv, x + (w - lw) / 2, y + down + (h - 5 * sc) / 2, label, state == 1 ? CL_BLUE : CL_INK, 255, sc);
}

static const int FILL_A[3] = { 170, 70, 150 };

void button(canvas_t *cv, int x, int y, int w, int h, const char *label, int state)
{
    chamfer_box(cv, x, y, w, h, state ? C_PRIM : C_PANEL, FILL_A[state], state ? C_FOCUS : C_PRIM, state == 2 ? 255 : 210);
    if (!label || !*label) return;
    int sc = micro_width(label, 2) <= w - 8 ? 2 : 1;
    int lw = micro_width(label, sc);
    micro_text(cv, x + (w - lw) / 2, y + (h - 5 * sc) / 2, label, state ? C_FOCUS : C_PRIM, 250, sc);
}

// the same with a smooth font, for the pages you read
void button_font(canvas_t *cv, int x, int y, int w, int h, const char *label, int state, const font_t *f)
{
    chamfer_box(cv, x, y, w, h, state ? C_PRIM : C_PANEL, FILL_A[state], state ? C_FOCUS : C_PRIM, state == 2 ? 255 : 210);
    if (!label || !*label) return;
    uint32_t c = state ? C_FOCUS : C_PRIM;
    int lw = font_width(f, label);
    if (lw > w - 6) {                                  // too long for it: the narrow pixel font
        lw = micro_width(label, 1);
        micro_text(cv, x + (w - lw) / 2, y + (h - 5) / 2, label, c, 250, 1);
        return;
    }
    font_text(cv, f, x + (w - lw) / 2, y + (h - FONT_LINE_H(f)) / 2, label, c, 255);
}

static int bar_w(int n) { return (BAR_R - BAR_L - 8 - (n - 1) * 4) / n; }

void bar(ui_t *u, canvas_t *cv, const char *const *labels, int n, int lit)
{
    if (!gfx_touches(cv, BAR_Y, BAR_Y + BAR_H)) return;
    int w = bar_w(n);
    for (int i = 0; i < n; i++) {
        int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_BAR + i;
        button_font(cv, BAR_L + 4 + i * (w + 4), BAR_Y, w, BAR_H, labels[i], pressed ? 2 : (i == lit ? 1 : 0),
                    RADAR_ROUND ? &FONT_SMALL : &FONT_MEDIUM);
    }
}

int bar_hit(int x, int y, int n)
{
    if (y < BAR_Y - 4 || n <= 0 || x < BAR_L || x >= BAR_R) return -1;
    int w = bar_w(n), i = (x - BAR_L - 2) / (w + 4);
    return i >= 0 && i < n ? i : -1;
}

void page_header(canvas_t *cv, const char *title, const char *right, uint32_t col)
{
    if (!gfx_touches(cv, 0, HEAD_H)) return;
#if RADAR_ROUND
    // the top of the glass is narrow: the title in the middle, low, what is on the right under it
    int tw = font_width(&FONT_SMALL, title);
    gfx_fill(cv, 0, 0, GFX_W, HEAD_H, C_PANEL, 210);
    gfx_hline(cv, PG_L, PG_R - 1, HEAD_H, C_QUIET, 220);
    gfx_tri(cv, GFX_W / 2.0f - tw / 2.0f - 9, 20.5f, GFX_W / 2.0f - tw / 2.0f - 4, 16.0f, GFX_W / 2.0f - tw / 2.0f - 4, 25.0f, col, 240);
    font_text(cv, &FONT_SMALL, GFX_W / 2 - tw / 2, 15, title, col, 255);
    if (right) micro_text(cv, GFX_W / 2 - micro_width(right, 1) / 2, 28, right, C_DIM, 235, 1);
#else
    gfx_fill(cv, 0, 0, GFX_W, HEAD_H, C_PANEL, 210);
    gfx_hline(cv, 0, GFX_W - 1, HEAD_H, C_QUIET, 220);
    gfx_tri(cv, 9.0f, 4.5f, 9.0f, 13.5f, 4.0f, 9.0f, col, 240);
    font_text(cv, &FONT_SMALL, 14, 4, title, col, 255);
    int tw = font_width(&FONT_SMALL, title), x0 = 14 + tw + 6;
    int rw = right ? micro_width(right, 1) : 0;
    if (right) micro_text(cv, GFX_W - 6 - rw, 7, right, C_DIM, 235, 1);
    int x1 = GFX_W - 6 - (right ? rw + 6 : 0);
    if (x1 > x0) gfx_hatch(cv, x0, 7, x1 - x0, 5, C_QUIET, 110);
#endif
}

/* --- background: gradient, a faint grid, dust; rebuilt on theme change --- */

/* all of this is in pixels of the glass */
#define BG_GRID GFX_Z(12)
static CORE_BIG uint32_t s_bgrow[GFX_PH];
static CORE_BIG uint16_t s_vigx[GFX_PW], s_vigy[GFX_PH];
static int16_t  s_gridcols[GFX_PW / BG_GRID + 2];
static int      s_ngrid, s_grid_cx = -1;
static struct { int16_t x, y; uint8_t a; } s_stars[128];
static int      s_nstars, s_bg_theme = -1;
#define VIG_W GFX_Z(56)

/* the grid's columns: one goes through the scope's middle, wherever that is (the radar without its
 * panel has it in the middle of the screen). made again only when it moves: ui_frame, between frames */
static void grid_columns(int cx)
{
    if (cx == s_grid_cx) return;
    s_ngrid = 0;
    for (int x = cx % BG_GRID; x < GFX_PW; x += BG_GRID) s_gridcols[s_ngrid++] = (int16_t)x;
    s_grid_cx = cx;
}

static void build_background(void)
{
    for (int y = 0; y < GFX_PH; y++) {
        float t = (float)y / (GFX_PH - 1);
        s_bgrow[y] = gfx_mix(C_BG0, C_BG1, (uint32_t)(256.0f * (0.25f + 0.75f * t)));
        float v = ((float)y - GFX_PH / 2.0f) / (GFX_PH * 7.0f / 12.0f);
        s_vigy[y] = (uint16_t)(256.0f * clampf(1.0f - 0.45f * v * v, 0.3f, 1.0f));
    }
    for (int x = 0; x < GFX_PW; x++) {
        int e = x < GFX_PW / 2 ? x : GFX_PW - 1 - x;
        float v = e < VIG_W ? 1.0f - (float)e / VIG_W : 0.0f;
        s_vigx[x] = (uint16_t)(256.0f * (1.0f - 0.4f * v * v));
    }
    grid_columns(s_grid_cx < 0 ? G_SCX : s_grid_cx);
    s_nstars = 0;
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W && s_nstars < 128; x++) {
            uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            if ((h & 1023u) == 0) {
                s_stars[s_nstars].x = (int16_t)(GFX_Z(x) < GFX_PW ? GFX_Z(x) : GFX_PW - 1);
                s_stars[s_nstars].y = (int16_t)GFX_Z(y);
                s_stars[s_nstars].a = (uint8_t)(30 + ((h >> 10) & 127));
                s_nstars++;
            }
        }
}

CORE_HOT void draw_background(canvas_t *cv, int grid_on)
{
    const uint32_t grid = gfx_scale(C_QUIET, 40), node = gfx_scale(C_QUIET, 90);
    for (int r = 0; r < cv->h; r++) {
        int y = cv->y0 + r;
        uint32_t *row = cv->px + r * GFX_PW;
        uint32_t base = gfx_scale(s_bgrow[y], s_vigy[y]);
        int lined = grid_on && ((y - G_SCY + 100 * BG_GRID) % BG_GRID) == 0;
        uint32_t fill = lined ? gfx_add(base, grid) : base;
        uint32_t col = gfx_add(base, lined ? node : grid);
        for (int x = 0; x < GFX_PW; x++) row[x] = fill;
        if (grid_on) for (int i = 0; i < s_ngrid; i++) row[s_gridcols[i]] = col;
        for (int x = 0; x < VIG_W; x++) {
            row[x] = gfx_scale(row[x], s_vigx[x]);
            row[GFX_PW - 1 - x] = gfx_scale(row[GFX_PW - 1 - x], s_vigx[GFX_PW - 1 - x]);
        }
    }
    for (int i = 0; i < s_nstars && !RADAR_EPAPER; i++) {   // paper: the stars printed as specks of dust
        uint32_t *row = gfx_row(cv, s_stars[i].y);
        if (row) row[s_stars[i].x] = gfx_add(row[s_stars[i].x], gfx_scale(C_DIM, s_stars[i].a));
    }
}

void ui_set_theme(ui_t *u, int theme)
{
    u->theme = ((theme % UI_THEMES) + UI_THEMES) % UI_THEMES;
    TH = &UI_THEME[u->theme];       // careful: TH is global, all the C_* colours read from it
    if (s_bg_theme != u->theme) { build_background(); s_bg_theme = u->theme; }
}

int ui_init(ui_t *u, uint32_t now)
{
    memset(u, 0, sizeof *u);
    u->now = u->boot = now;
    if (!scope_init()) return 0;
    fr_cfg_defaults(&u->cfg);
    light_defaults(&u->light);
    u->alerts = WXA_ALL;
    u->turn_look = TURN_LOOK_DEFAULT;                  // the weather's turns off, the look's ready for them
    u->screen_alarm = 1;                               // the LED's events on the screen too
    /* default touch alignment of the 2.8" CYD (the WiFi radar's numbers) till the user aligns */
    u->cal = (touch_cal_t){ 0.0f, -0.0894f, 333.3f, -0.0666f, 0.0f, 251.1f };
    const int in = RADAR_ROUND ? (int)(GFX_W * 0.2f) : 30;   // round glass: the corners are not there, the diagonals are
    const int pts[CAL_POINTS][2] = { { in, in }, { GFX_W - in, in }, { GFX_W - in, GFX_H - in },
                                     { in, GFX_H - in }, { GFX_W / 2, GFX_H / 2 } };
    memcpy(CAL_PT, pts, sizeof CAL_PT);
    ui_set_theme(u, 0);
    return 1;
}

void ui_set_cfg(ui_t *u, const fr_cfg_t *c)
{
    u->cfg = *c;
    ui_set_theme(u, c->theme);
}

/* (chipguyhere) the page's list scroll and how far it goes, NULL: this page has no list that scrolls.
 * the numbers are the drag's in ui_touch (they were there inline) */
static int *list_scroll_of(ui_t *u, int *maxs)
{
    const int list = u->view == VIEW_LIST, wifi = u->view == VIEW_WIFI;
    if (!list && !wifi && u->view != VIEW_SETUP) return NULL;
    const int rows = list ? u->n_order : wifi ? wifi_rows(u) : setup_rows(u);
    const int rh = list ? LIST_ROW : wifi ? NET_H : ROW_H;
    const int y1 = list ? LIST_Y1 : BAR_Y - 3;
    *maxs = rows * rh - (y1 - LIST_Y0);
    if (*maxs < 0) *maxs = 0;
    return list ? &u->list_scroll : wifi ? &u->wifi_scroll : &u->setup_scroll;
}

/* (chipguyhere) momentum. the speed decays by e every FLING_TAU ms, so the list comes to rest
 * FLING_TAU * the speed further on (a flick of 1.5 px/ms: ~490 px, ~1.5 lists). it stops at either end */
#define FLING_TAU   325.0f
#define FLING_MIN   0.25f          /* px/ms at the lift: slower is a placed list, not a flick */
#define FLING_MAX   4.0f
#define FLING_STOP  0.02f          /* px/ms: at rest */
#define FLING_FRESH 150u           /* ms: the finger moved this recently before the lift. the library reports
                                    * the last point for ~4 reads after a lift, so the end of a flick looks still */

static void fling_step(ui_t *u, uint32_t now)
{
    if (!u->fling) return;
    int maxs, *sp = list_scroll_of(u, &maxs);
    if (u->t_down || !sp) { u->fling = 0; return; }
    int32_t dt = (int32_t)(now - u->fl_at);
    if (dt <= 0) return;
    if (dt > 100) dt = 100;                            // a long gap (a page drawn meanwhile) is not a jump
    u->fl_at = now;
    const float k = expf(-(float)dt / FLING_TAU);
    u->fl_pos += u->fl_v * FLING_TAU * (1.0f - k);     // the distance the decaying speed covers in dt
    u->fl_v *= k;
    int s = (int)lroundf(u->fl_pos);
    if (s <= 0) { s = 0; u->fling = 0; }
    if (s >= maxs) { s = maxs; u->fling = 0; }
    if (fabsf(u->fl_v) < FLING_STOP) u->fling = 0;
    *sp = s;
}

/* the weather's warnings, out of the forecast the firmware copied in. one that comes, or gets worse,
 * says so a few seconds over whatever is shown and tells the firmware, which lights the LED */
static void alerts_frame(ui_t *u)
{
    wx_alerts(u->wx, u->alerts, &u->alert);
    u->alert_new = 0;
    int said = -1;
    for (int k = 0; k < WXA_KINDS; k++) {
        if (u->alert.level[k] > u->alert_was[k]) {
            u->alert_new |= (uint8_t)(1u << k);
            if (said < 0 || u->alert.level[k] > u->alert.level[said]) said = k;
        }
        u->alert_was[k] = u->alert.level[k];
    }
    if (said >= 0) {
        char t[sizeof u->toast];
        alert_said(&u->alert, said, t, sizeof t);
        toast(u, t);
        u->toast_until = u->now + 5000u;               // longer than a look's name: it may be read across the room
    }
}

void ui_frame(ui_t *u, uint32_t now)
{
    fling_step(u, now);
    if (RADAR_EPAPER && u->cfg.look == LOOK_MAP) u->cfg.look = LOOK_CLASSIC;   // paper has no flat map, CLASSIC is its map
    u->now = now;
    if (!u->started && now >= UI_SCREEN_PICK_MS) u->started = 1;   // once: the clock's wrap never opens it again
    /* a forecast past WX_STALE_S is none: its hour 0 was taken as now, so with the WiFi or the server gone for hours
     * the warnings spoke of hours already past and NOW stood an hour or more wrong (the 2026-11-27 review). it is
     * asked for every 30 minutes, so a good one is never this old. signed: a clock behind the forecast is not old */
    if (u->wx && u->net.utc && (int64_t)u->net.utc - (int64_t)u->wx->at > WX_STALE_S) u->wx = NULL;
    weather_frame(u);                                       // the WEATHER look's page, and what its strips will read
    if (!ui_until(u->toast_until, now)) u->toast_until = 0;    // passed: let go, see ui_until
    alerts_frame(u);
    if (!ui_until(u->menu_until, now)) u->menu_until = 0;
    u->beam = wrap360((float)(now % BEAM_TURN_MS) * (360.0f / BEAM_TURN_MS));
    if (u->sel && !sel_plane(u)) u->sel = 0;                // it flew away
    grid_columns(scope_glass_cx(u));                        // the background's grid through the scope's middle
    scope_frame(u);
    {   /* a list grown shorter under its scroll (planes gone) back to its end: scrolled past every row the page stood
         * empty, with no NOTHING IN RANGE either, till a drag (the 2026-11-27 review) */
        int maxs, *sp = list_scroll_of(u, &maxs);
        if (sp && !u->t_down && !u->fling && *sp > maxs) *sp = maxs;
    }
#if RADAR_EPAPER
    paper_frame(u);                                         // the paper's own main screen, when it is the one shown
#endif
}

/* --- touch alignment: 5 crosses, the median of each press, least squares (the radar's) --- */

static int solve3(float m[3][4], float *x)
{
    for (int c = 0; c < 3; c++) {
        int p = c;
        for (int r = c + 1; r < 3; r++) if (fabsf(m[r][c]) > fabsf(m[p][c])) p = r;
        if (fabsf(m[p][c]) < 1e-6f) return 0;
        if (p != c) for (int j = 0; j < 4; j++) { float t = m[c][j]; m[c][j] = m[p][j]; m[p][j] = t; }
        for (int r = 0; r < 3; r++) {
            if (r == c) continue;
            float f = m[r][c] / m[c][c];
            for (int j = c; j < 4; j++) m[r][j] -= f * m[c][j];
        }
    }
    for (int i = 0; i < 3; i++) x[i] = m[i][3] / m[i][i];
    return 1;
}

void touch_apply(const touch_cal_t *c, int rx, int ry, int *x, int *y)
{
    *x = (int)lroundf(c->a * rx + c->b * ry + c->c);
    *y = (int)lroundf(c->d * rx + c->e * ry + c->f);
}

void ui_start_calibration(ui_t *u)
{
    u->view = VIEW_CAL;
    u->cal_step = 0;
    u->cal_n = 0;
    u->t_down = 0;
    u->t_turn = u->t_panel = 0;
    u->cal_wait = 1;
}

static int median16(const int16_t *v, int n)
{
    int16_t s[48];
    if (n > 48) n = 48;
    for (int i = 0; i < n; i++) {
        int16_t x = v[i];
        int j = i - 1;
        while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; }
        s[j + 1] = x;
    }
    return n ? s[n / 2] : 0;
}

static void cal_touch(ui_t *u, int down, int rx, int ry, uint32_t now, ui_cmd_t *cmd)
{
    if (u->cal_wait) { if (!down) u->cal_wait = 0; return; }
    if (down) {
        if (!u->t_down) { u->t_down = 1; u->t_since = now; u->cal_n = 0; }
        if (now - u->t_since > 120u && u->cal_n < 48) {
            u->cal_sx[u->cal_n] = (int16_t)rx;
            u->cal_sy[u->cal_n] = (int16_t)ry;
            u->cal_n++;
        }
        return;
    }
    if (!u->t_down) return;
    u->t_down = 0;
    if (u->cal_n < 2 || now - u->t_since < 300u) return;
    int n = u->cal_n > 4 ? u->cal_n - 2 : u->cal_n;
    u->cal_raw[u->cal_step][0] = (float)median16(u->cal_sx, n);
    u->cal_raw[u->cal_step][1] = (float)median16(u->cal_sy, n);
    if (++u->cal_step < CAL_POINTS) return;
    float mx[3][4] = { { 0 } }, my[3][4] = { { 0 } }, X[3], Y[3];
    for (int i = 0; i < CAL_POINTS; i++) {
        float f[3] = { u->cal_raw[i][0], u->cal_raw[i][1], 1.0f };
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) { mx[r][c] += f[r] * f[c]; my[r][c] += f[r] * f[c]; }
            mx[r][3] += f[r] * (float)CAL_PT[i][0];
            my[r][3] += f[r] * (float)CAL_PT[i][1];
        }
    }
    int ok = solve3(mx, X) && solve3(my, Y);
    float worst = 0;
    if (ok) {
        for (int i = 0; i < CAL_POINTS; i++) {
            float ex = X[0] * u->cal_raw[i][0] + X[1] * u->cal_raw[i][1] + X[2] - (float)CAL_PT[i][0];
            float ey = Y[0] * u->cal_raw[i][0] + Y[1] * u->cal_raw[i][1] + Y[2] - (float)CAL_PT[i][1];
            float e = sqrtf(ex * ex + ey * ey);
            if (e > worst) worst = e;
        }
    }
    u->cal_err = worst;
    if (!ok || worst > 18.0f) {
        toast(u, "TRY AGAIN");
        u->cal_step = 0;
        return;
    }
    u->cal = (touch_cal_t){ X[0], X[1], X[2], Y[0], Y[1], Y[2] };
    cmd->save_cal = 1;
    u->view = VIEW_MAIN;
    toast(u, "TOUCH ALIGNED");
}

/* --- touch --- */

static int hit_at(const ui_t *u, int x, int y)
{
    switch (u->view) {
    case VIEW_MAIN:  return main_hit(u, x, y);
    case VIEW_LIST:  return list_hit(u, x, y);
    case VIEW_SETUP: return setup_hit(u, x, y);
    case VIEW_WIFI:  return wifi_hit(u, x, y);
    case VIEW_KEYS:  return keys_hit(u, x, y);
    case VIEW_ABOUT: return about_hit(u, x, y);
    default:         return HIT_NONE;
    }
}

static int repeats(const ui_t *u, int hit)
{
    return (u->view == VIEW_SETUP && setup_repeats(hit)) || (u->view == VIEW_KEYS && keys_repeats(u, hit));
}

static void select_plane(ui_t *u, int i)
{
    u->sel = i >= 0 ? u->snap.p[i].icao : 0;
}

int watched(const ui_t *u, const plane_t *p)
{
    for (int i = 0; i < u->watch.n; i++) if (watch_is(&u->watch.w[i], p)) return 1;
    return 0;
}

/* WATCH on the picked plane's panel: onto the watch list, or off it again. by its registration,
 * else its address: the plane itself, whatever flight it flies next */
static void watch_toggle(ui_t *u, const plane_t *p, ui_cmd_t *cmd)
{
    char id[WATCH_ID], text[WATCH_TEXT], said[40];
    if (p->reg[0]) snprintf(id, sizeof id, "%s", p->reg);
    else snprintf(id, sizeof id, "%06X", (unsigned)(p->icao & 0xFFFFFF));
    int n = 0;
    text[0] = 0;
    for (int i = 0; i < u->watch.n; i++) {             // the list without it
        if (watch_is(&u->watch.w[i], p)) continue;
        n += snprintf(text + n, sizeof text - (size_t)n, "%s%s", n ? ", " : "", u->watch.w[i].id);
    }
    if (watched(u, p)) {
        snprintf(said, sizeof said, "NOT WATCHED: %s", id);
    } else if (u->watch.n >= WATCH_MAX) {
        toast(u, "WATCH LIST FULL");
        return;
    } else {
        snprintf(text + n, sizeof text - (size_t)n, "%s%s", n ? ", " : "", id);
        snprintf(said, sizeof said, "WATCHING %s", id);
    }
    snprintf(u->watch_text, sizeof u->watch_text, "%s", text);
    watch_set(&u->watch, text);                        // the button shows it at once, the firmware's copy next frame
    cmd->watch = 1;
    toast(u, said);
}

static void say_range(ui_t *u)
{
    char b[24], d[16];
    fmt_dist(u, range_km(&u->cfg), d, sizeof d);
    snprintf(b, sizeof b, "RANGE %s", d);
    toast(u, b);
}

// BOOT and the list's ZOOM: the next of the short list, round again after the last
static void next_range(ui_t *u, ui_cmd_t *cmd)
{
    float v = BOOT_RANGES[0];
    for (int i = 0; i < (int)(sizeof BOOT_RANGES / sizeof BOOT_RANGES[0]); i++)
        if (BOOT_RANGES[i] > u->cfg.range * 1.01f) { v = BOOT_RANGES[i]; break; }
    u->cfg.range = v;
    cmd->cfg = cmd->refetch = 1;
    say_range(u);
}

// the e-paper dial: the next of the short list nearer (-1) or further (1) from a range, it itself at the list's ends
static float range_from(float range, int dir)
{
    const int n = (int)(sizeof BOOT_RANGES / sizeof BOOT_RANGES[0]);
    float v = range;
    if (dir > 0) {
        for (int i = 0; i < n; i++) {
            if (BOOT_RANGES[i] > range * 1.01f) { v = BOOT_RANGES[i]; break; }
        }
    } else {
        for (int i = n - 1; i >= 0; i--) {
            if (BOOT_RANGES[i] < range * 0.99f) { v = BOOT_RANGES[i]; break; }
        }
    }
    return v;
}

static float range_next(const ui_t *u, int dir) { return range_from(u->cfg.range, dir); }

int range_can_step(const ui_t *u, int dir) { return range_next(u, dir) != u->cfg.range; }

// one step of it, standing at its ends
static void step_range(ui_t *u, ui_cmd_t *cmd, int dir)
{
    const float v = range_next(u, dir);
    if (v == u->cfg.range) return;                     // at the end of the list already
    u->cfg.range = v;
    cmd->cfg = cmd->refetch = 1;
    say_range(u);
}

// no touch: the next plane on the scope, nearest first, and past the last one none
static void step_plane(ui_t *u)
{
    int at = -1, next = -1;
    for (int k = 0; k < u->n_order; k++) if (u->snap.p[u->order[k]].icao == u->sel) at = k;
    for (int k = at + 1; k < u->n_order && next < 0; k++) if (u->spot[u->order[k]].vis == 1) next = u->order[k];
    select_plane(u, next);
}

static void next_theme(ui_t *u, ui_cmd_t *cmd)
{
    u->cfg.theme = (uint8_t)((u->theme + 1) % UI_THEMES);
    ui_set_theme(u, u->cfg.theme);
    cmd->cfg = 1;
    toast(u, UI_THEME[u->theme].name);
}

/* the look after this one: the radar, the flat map, the map in its own colours, the weather. paper has no flat map
 * ("экран MAP убираем"): CLASSIC is its map */
int ui_look_after(int look)
{
    look = (look + 1) % LOOKS;
    if (RADAR_EPAPER && look == LOOK_MAP) look = LOOK_CLASSIC;
    return look;
}

// and the one before it: paper's EXIT, when no plane is picked to let go ("нижняя кнопка не задействована")
int ui_look_before(int look)
{
    look = (look + LOOKS - 1) % LOOKS;
    if (RADAR_EPAPER && look == LOOK_MAP) look = LOOK_RADAR;
    return look;
}

static void set_look(ui_t *u, ui_cmd_t *cmd, int look)
{
    u->cfg.look = (uint8_t)look;
    u->panel_open = 0;
    u->wx_page = WXP_DIAL;                             // the weather comes on its dial
    cmd->cfg = cmd->refetch = 1;                       // its map is another size, the square asks further out
    if (!RADAR_EPAPER) toast(u, LOOK_NAME[u->cfg.look]);   // paper: its third of the strip said it before
}

static void next_look(ui_t *u, ui_cmd_t *cmd) { set_look(u, cmd, ui_look_after(u->cfg.look)); }

/* the look's button, top right: its name, and on a touch screen an arrow at each end. the left half goes to the
 * look before, the right half to the next (an owner's idea: back is one tap, not a whole round). main_hit tells
 * the halves apart with look_hit. paper: the light map's own button */
void ui_alarm(ui_t *u, int ev)
{
    u->alarm = (uint8_t)(UI_SCREEN_ALARM && u->screen_alarm && ev >= 0 && ev < LIGHT_EVENTS ? ev + 1 : 0);
}

/* what an alarm lights (ui_t.alarm): the face in the event's colour, the letters dark. 0: none lit now. letters in
 * the colour, as asked first, vanished where the theme is that colour already (green on PHOSPHOR, red on CRIMSON).
 * paper: ink letters, and ink for white, which the light map's paper is */
int alarm_colours(const ui_t *u, int paper, uint32_t *fill, uint32_t *ink)
{
    if (!UI_SCREEN_ALARM || !u->alarm) return 0;
    uint8_t c[3];
    light_screen_rgb(u->alarm - 1, c);
    *fill = RGB(c[0], c[1], c[2]);
    *ink = paper ? CL_INK : C_BG0;
    if (paper && u->alarm - 1 == LIGHT_NEAR) {
        *fill = CL_INK;
        *ink = CL_PAPER;
    }
    return 1;
}

// a button lit by an alarm: its face filled, its name dark, set as button and map_button set theirs
static void alarm_face(canvas_t *cv, int x, int y, int w, int h, const char *label, int paper, uint32_t fill, uint32_t ink)
{
    if (paper) {
        round_box(cv, x, y + 2, w, h, 6, RGB(0, 0, 0), 55);
        round_box(cv, x, y, w, h, 6, fill, 255);
    } else {
        chamfer_box(cv, x, y, w, h, fill, 255, fill, 255);
    }
    const int sc = micro_width(label, 2) <= w - (paper ? 6 : 8) ? 2 : 1;
    micro_text(cv, x + (w - micro_width(label, sc)) / 2, y + (h - 5 * sc) / 2, label, ink, 255, sc);
}

void look_button(const ui_t *u, canvas_t *cv, const char *name, int paper)
{
    const int x = look_x(u), w = look_w(u);
    const int pressed = u->t_down && !u->t_moved && (u->t_hit == HIT_LOOK || u->t_hit == HIT_LOOK_BACK);
    uint32_t fill = 0, ink = 0;
    const int alarm = !pressed && alarm_colours(u, paper, &fill, &ink);   // a finger on it puts it out anyway
    if (alarm) alarm_face(cv, x, LOOK_Y, w, LOOK_H, name, paper, fill, ink);
    else if (paper) map_button(cv, x, LOOK_Y, w, LOOK_H, name, pressed ? 2 : 0);
    else button(cv, x, LOOK_Y, w, LOOK_H, name, pressed ? 2 : 0);
#if LOOK_ARROW
    const float cy = LOOK_Y + LOOK_H / 2.0f + (paper && pressed);   // the light map's button sinks when pressed
    for (int s = 0; s < 2; s++) {                     // s 0 the arrow back, on the left; 1 the one on
        const int lit = pressed && u->t_hit == (s ? HIT_LOOK : HIT_LOOK_BACK);
        const uint32_t c = alarm ? ink : paper ? (lit ? CL_BLUE : CL_INK) : (lit ? C_FOCUS : C_PRIM);
        const float tip = s ? (float)(x + w) - 4.0f : (float)x + 4.0f, back = s ? tip - 3.0f : tip + 3.0f;
        gfx_line(cv, back, cy - 3.5f, tip, cy, 1.5f, c, lit ? 255 : 215);
        gfx_line(cv, tip, cy, back, cy + 3.5f, 1.5f, c, lit ? 255 : 215);
    }
#endif
}

// which half of the look's button x is in: the left one goes back. one way only where it has no arrows
int look_hit(const ui_t *u, int x)
{
    return LOOK_ARROW && x < look_x(u) + look_w(u) / 2 ? HIT_LOOK_BACK : HIT_LOOK;
}

/* the steps of SET's WEATHER FOR and LOOK FOR, in minutes. the weather's 0 is off */
static const uint8_t TURN_WX[] = { 0, 1, 2, 3, 5, 10 };
static const uint8_t TURN_LOOK[] = { 1, 2, 3, 5, 10, 15, 30, 60 };

int ui_turn_step(int m, int dir, int wx)
{
    const uint8_t *t = wx ? TURN_WX : TURN_LOOK;
    const int n = wx ? (int)sizeof TURN_WX : (int)sizeof TURN_LOOK;
    int i = 0;
    while (i < n && t[i] != m) i++;
    if (!dir) return i < n ? m : -1;
    if (i == n) return t[wx ? 0 : 2];                 // not a step (it never is): back to off, or 3 minutes
    i += dir;
    return t[i < 0 ? 0 : i >= n ? n - 1 : i];
}

/* SET's WEATHER FOR and LOOK FOR (an owner's idea: "weather for a minute, then radar for 3 minutes, and keeps
 * running the cycle"). the weather comes by itself for its minutes, then the look the person chose for its
 * own, and round again. a finger, BOOT or a key, a picked plane, another page, or a look changed by any hand
 * (the button, the site) starts the count again: it never turns away from what someone is doing. the look it
 * turns to is kept as any look is, and told to the site the same way */
int ui_turn(ui_t *u, uint32_t now, int busy, ui_cmd_t *cmd)
{
    const int look = u->cfg.look, wx = look == LOOK_WEATHER;
    if (!wx) u->turn_back = (uint8_t)look;             // where the weather goes back to: the last look that was not it
    if (!u->turn_wx || busy || u->view != VIEW_MAIN || u->t_down || u->sel || u->pend_kind || look != u->turn_was ||
        !u->turn_at) {
        u->turn_was = (uint8_t)look;
        u->turn_at = now ? now : 1;                    // 0 is "start it again"
        return 0;
    }
    if (now - u->turn_at < (wx ? u->turn_wx : u->turn_look) * 60000u) return 0;
    u->turn_at = now ? now : 1;
    if (!wx && !u->wx) return 0;                       // no forecast yet: the look stays a turn more
    u->cfg.look = u->turn_was = (uint8_t)(wx ? u->turn_back : LOOK_WEATHER);
    u->panel_open = 0;
    u->wx_page = WXP_DIAL;                             // the weather comes on its dial
    cmd->cfg = cmd->refetch = 1;                       // as a look changed by hand: kept, told, the planes asked for
    return 1;
}

static void act(ui_t *u, int hit, int longp, int x, int y, ui_cmd_t *cmd)
{
    if (u->view == VIEW_SETUP) { setup_act(u, hit, longp, cmd); return; }
    if (longp) return;
    if (u->view == VIEW_WIFI) { wifi_act(u, hit, cmd); return; }
    if (u->view == VIEW_KEYS) { keys_act(u, hit, cmd); return; }
    if (u->view == VIEW_ABOUT) { about_act(u, hit); return; }
    if (u->view == VIEW_MAIN) {
        if (hit == HIT_WIFI) { wifi_open(u, cmd); return; }
        if (hit == HIT_NEWS) { about_open(u); return; }
        if (hit == HIT_GEAR) { setup_open_radar(u); return; }
        if (hit == HIT_ALERT) {                        // a warning's chip: the weather, where the panel says it in full
            const int k = wx_alert_worst(&u->alert);
            if (u->cfg.look == LOOK_WEATHER) {         // there already (the round glass has no panel): what it is, again
                if (k == WXA_PRESSURE) u->wx_page = WXP_PRESSURE;
                if (k >= 0) {
                    char t[sizeof u->toast];
                    alert_said(&u->alert, k, t, sizeof t);
                    toast(u, t);
                }
                return;
            }
            u->cfg.look = LOOK_WEATHER;
            u->wx_page = k == WXA_PRESSURE ? WXP_PRESSURE : WXP_DIAL;   // the pressure's warning: straight to its graph
            u->panel_open = 0;
            u->sel = 0;
            cmd->cfg = cmd->refetch = 1;
            toast(u, LOOK_NAME[LOOK_WEATHER]);
            return;
        }
        if (hit == HIT_WXPAGE) { next_wx_page(u); return; }
        if (hit == HIT_SCOPE) {
#if RADAR_ROUND
            if (u->cfg.look == LOOK_WEATHER) {         // the round glass's dial or graph: the next page, and the row to leave by
                next_wx_page(u);
                u->menu_until = u->now + ROUND_MENU_MS;
                return;
            }
#endif
            int i = scope_pick(u, x, y);
#if RADAR_ROUND
            // round glass, no panel: a tap on nothing brings the HUE LIST SET row, another one hides it
            if (i < 0 && !u->sel) { u->menu_until = ui_until(u->menu_until, u->now) ? 0 : u->now + ROUND_MENU_MS; return; }
            u->menu_until = 0;
#endif
            select_plane(u, i);
            return;
        }
        if (hit >= HIT_ROW && hit < HIT_ROW + SKY_MAX) { select_plane(u, u->order[hit - HIT_ROW]); return; }
        if (hit == HIT_LOOK || hit == HIT_LOOK_BACK) {   // the button's right half on, its left half back
            set_look(u, cmd, hit == HIT_LOOK ? ui_look_after(u->cfg.look) : ui_look_before(u->cfg.look));
            if (RADAR_ROUND) u->menu_until = u->now + ROUND_MENU_MS;   // stays up for the next look
            return;
        }
        if (hit == HIT_DOTS) {
            if (square_look(u)) {                      // the square map's panel: in, or out again
                u->panel_open ^= 1;
                if (!u->panel_open) u->sel = 0;
            } else {                                   // the radar's and the weather's: what is shown goes, what is away comes
                const uint8_t was = u->bare;
                if (panel_shown(u)) {                  // kept, or there for the picked plane: away, and the plane let go
                    u->bare = 1;
                    u->sel = 0;
                } else {
                    u->bare = 0;
                }
                cmd->bare = u->bare != was;
            }
            return;
        }
        if ((hit == HIT_BTN + 0 || hit == HIT_PANEL) && u->sel) {   // picked: the next nearest (round: its card)
            int at = 0;
            for (int k = 0; k < u->n_order; k++) if (u->snap.p[u->order[k]].icao == u->sel) at = k;
            if (u->n_order) select_plane(u, u->order[(at + 1) % u->n_order]);
        } else if (hit == HIT_BTN + 0) {
            next_theme(u, cmd);
            if (RADAR_ROUND) u->menu_until = u->now + ROUND_MENU_MS;   // stays up for the next colours
        } else if (hit == HIT_BTN + 1 && u->sel && !RADAR_ROUND) {    // picked: WATCH (round: its row has no WATCH)
            const plane_t *p = sel_plane(u);
            if (p) watch_toggle(u, p, cmd);
        } else if (hit == HIT_BTN + 1) {
            u->menu_until = 0;
            u->view = VIEW_LIST;
            u->list_scroll = 0;
        } else if (hit == HIT_BTN + 2) {
            u->menu_until = 0;
            if (u->sel) u->sel = 0;
            else { u->view = VIEW_SETUP; u->setup_scroll = 0; }
        }
        return;
    }
    if (u->view == VIEW_LIST) {
        if (hit >= HIT_ROW && hit < HIT_ROW + SKY_MAX) { select_plane(u, u->order[hit - HIT_ROW]); u->view = VIEW_MAIN; }
        else if (hit == HIT_BAR + 0) next_range(u, cmd);
        else if (hit == HIT_BAR + 1) u->view = VIEW_MAIN;
    }
}

#if RADAR_OVERLAY
/* (chipguyhere) see ui.h. the numbers are ui_touch's own (just below) and the pages' gfx_sub bounds */
#define SCROLL_LIST_HEAD (RADAR_ROUND ? 0 : 14)        /* ui_pages.c LIST_HEAD */
int ui_scroll_info(const ui_t *u, ui_scroll_t *s)
{
    const int list = u->view == VIEW_LIST, wifi = u->view == VIEW_WIFI, setup = u->view == VIEW_SETUP;
    if (!list && !wifi && !setup) return 0;
    const int rows = list ? u->n_order : wifi ? wifi_rows(u) : setup_rows(u);
    const int rh = list ? LIST_ROW : wifi ? NET_H : ROW_H;
    const int y1 = list ? LIST_Y1 : BAR_Y - 3;
    const int maxs = rows * rh - (y1 - LIST_Y0);
    if (maxs <= 0) return 0;
    s->y0 = list ? LIST_Y0 + SCROLL_LIST_HEAD : LIST_Y0;
    s->y1 = list ? LIST_Y1 : BAR_Y - 2;
    s->scroll = list ? u->list_scroll : wifi ? u->wifi_scroll : u->setup_scroll;
    s->max = maxs;
    s->bx0 = setup ? PG_R - 3 : 0;
    s->bx1 = setup ? PG_R - 1 : 0;
    return 1;
}

int ui_pressed(const ui_t *u)
{
    if (!u->t_down || u->t_moved || u->t_hit == HIT_NONE || u->t_hit == HIT_SCOPE) return 0;
    return u->t_hit;
}

void ui_scrolled(ui_t *u, int s)
{
    if (u->view == VIEW_LIST) u->list_scroll = s;
    else if (u->view == VIEW_WIFI) u->wifi_scroll = s;
    else if (u->view == VIEW_SETUP) u->setup_scroll = s;
}
#endif

void ui_touch(ui_t *u, int down, int x, int y, int rx, int ry, uint32_t now, ui_cmd_t *cmd)
{
    if (u->view == VIEW_CAL) { cal_touch(u, down, rx, ry, now, cmd); return; }
    if (down && !u->t_down) {
        u->t_down = 1;
        u->t_x0 = u->t_x = x;
        u->t_y0 = u->t_y = y;
        u->t_since = now;
        u->t_moved = u->t_long = u->t_ns = u->t_reps = u->t_rpt = u->t_turn = 0;
        u->t_panel = 0;
        u->t_caught = u->fling;                        // (chipguyhere) a finger on a coasting list stops it, nothing more
        u->fling = 0;
        u->t_hit = u->t_caught ? HIT_NONE : hit_at(u, x, y);
        u->t_scroll0 = u->view == VIEW_LIST ? u->list_scroll : u->view == VIEW_WIFI ? u->wifi_scroll : u->setup_scroll;
        u->fl_y = (int16_t)y;
        u->fl_at = now;
        u->fl_v = 0;
    }
    if (down) {
        u->t_x = x;
        u->t_y = y;
        if (u->t_ns < 16) { u->t_sx[u->t_ns] = (int16_t)x; u->t_sy[u->t_ns] = (int16_t)y; u->t_ns++; }
        if (u->t_ns == 3) {                            // the first readings of a resistive press wander
            u->t_x0 = median16(u->t_sx, 3);
            u->t_y0 = median16(u->t_sy, 3);
            u->t_hit = u->t_caught ? HIT_NONE : hit_at(u, u->t_x0, u->t_y0);
            if (repeats(u, u->t_hit)) { u->t_rpt = 1; u->t_rep = now + 450u; }
            if (u->view == VIEW_MAIN && u->t_hit == HIT_SCOPE) {
                /* the scope under a finger: on the rim it turns the map, inside it zooms. one finger
                 * pinches against home: what was under the finger stays under it */
                float dx = (float)(u->t_x0 - view_cx(u)), dy = (float)(SCY - u->t_y0), r = sqrtf(dx * dx + dy * dy);
                u->t_turn = r >= PR - 4 && !square_look(u) ? 1 : r >= 10 ? 2 : 0;   // the flat map has no rim: north stays up
                /* a plane picked or let go meanwhile (it flew off, a watched one came, the site picked one)
                 * would bring the panel or take it, and the scope would jump under the finger: it waits */
                if (u->t_turn) u->t_panel = (uint8_t)(1 + panel_shown(u));
                u->t_a0 = atan2f(dx, dy) / D2R;
                u->t_r0 = r;
                u->t_up0 = (float)u->cfg.up;
                u->t_range0 = u->cfg.range;
            }
        }
        int slop = u->t_turn ? 8 : 14;                 // a turn or a zoom should start soon, not jump
        if (u->t_ns >= 3 && abs(x - u->t_x0) + abs(y - u->t_y0) > slop) u->t_moved = 1;
        if (u->t_moved && u->t_turn) {
            float dx = (float)(x - view_cx(u)), dy = (float)(SCY - y);
            if (u->t_turn == 1) {
                float da = atan2f(dx, dy) / D2R - u->t_a0;   // the finger went round this far
                u->cfg.up = (int16_t)((int)lroundf(wrap360(u->t_up0 - da)) % 360);
            } else {
                float r = sqrtf(dx * dx + dy * dy);
                if (r < 8.0f) r = 8.0f;
                u->cfg.range = clampf(u->t_range0 * u->t_r0 / r, RANGE_MIN, RANGE_MAX);
            }
        } else if (u->t_moved && (u->view == VIEW_LIST || u->view == VIEW_SETUP || u->view == VIEW_WIFI)) {
            int maxs, *sp = list_scroll_of(u, &maxs);
            int s = u->t_scroll0 - (y - u->t_y0);
            if (s > maxs) s = maxs;
            if (s < 0) s = 0;
            *sp = s;
            /* (chipguyhere) the finger's speed, for the momentum at the lift: from the samples that moved,
             * smoothed over the last few. the scroll goes the other way to the finger */
            if (y != u->fl_y) {
                const int32_t dt = (int32_t)(now - u->fl_at);
                const float vi = (float)(u->fl_y - y) / (float)(dt > 0 ? dt : 1);
                u->fl_v = dt > 100 ? vi : 0.5f * u->fl_v + 0.5f * vi;   // after a pause: from now on
                u->fl_y = (int16_t)y;
                u->fl_at = now;
            }
        } else if (!u->t_moved && u->t_rpt && (int32_t)(now - u->t_rep) >= 0) {
            act(u, u->t_hit, 0, u->t_x0, u->t_y0, cmd);
            u->t_reps++;
            u->t_rep = now + (now - u->t_since > 2500u ? 50u : 110u);
        }
        if (!u->t_moved && !u->t_long && !u->t_rpt && !u->t_caught && now - u->t_since >= 1200u) {
            u->t_long = 1;
            act(u, u->t_hit, 1, u->t_x0, u->t_y0, cmd);
        }
        if (!u->t_moved && u->t_long == 1 && (u->t_hit == HIT_NONE || u->t_hit == HIT_WXPAGE) && now - u->t_since >= 4000u) {
            u->t_long = 2;
            ui_start_calibration(u);                   // hold 4 s on nothing: align the touch again
        }
    } else if (u->t_down) {
        u->t_down = 0;
        int dx = u->t_x - u->t_x0, dy = u->t_y - u->t_y0;
        if (!u->t_moved && !u->t_long && !u->t_reps && !u->t_caught) {   // (a catch only stopped the list: no tap)
            int n = u->t_ns > 2 ? u->t_ns - 1 : u->t_ns;
            int tx = median16(u->t_sx, n), ty = median16(u->t_sy, n);
            act(u, hit_at(u, tx, ty), 0, tx, ty, cmd);
        } else if (u->t_moved && u->t_turn == 1) {
            if (u->cfg.up <= 4 || u->cfg.up >= 356) { u->cfg.up = 0; toast(u, "NORTH UP"); }   // close is north
            cmd->cfg = 1;
        } else if (u->t_moved && u->t_turn == 2) {
            u->cfg.range = range_snap(u->cfg.range);
            say_range(u);
            cmd->cfg = cmd->refetch = 1;
        } else if (u->t_moved && u->view == VIEW_MAIN && abs(dx) > 70 && abs(dx) > 2 * abs(dy)) {
            u->view = dx < 0 ? VIEW_LIST : VIEW_SETUP;  // a swipe on the panel
#if RADAR_OVERLAY
        /* the boards with the moving layer only, for now: on the others a list stops where the
         * finger lets go, as it always has. a resistive panel's last readings may wander */
        } else if (u->t_moved) {                       // (chipguyhere) a list let go while it moved: it coasts on
            int maxs, *sp = list_scroll_of(u, &maxs);
            if (sp && now - u->fl_at <= FLING_FRESH && fabsf(u->fl_v) >= FLING_MIN) {
                u->fl_v = clampf(u->fl_v, -FLING_MAX, FLING_MAX);
                u->fl_pos = (float)*sp;
                u->fl_at = now;
                u->fling = 1;
            }
#endif
        }
        u->t_caught = 0;
        u->t_turn = 0;
        u->t_panel = 0;                                // the panel goes as the plane says again
        u->t_hit = HIT_NONE;
    }
}

// BOOT: short = next range, long = back to the scope. on the alignment page: skip it
void ui_button(ui_t *u, int ev, ui_cmd_t *cmd)
{
    if (u->view == VIEW_CAL) { u->view = VIEW_MAIN; toast(u, "TOUCH SKIPPED"); return; }
    /* the weather has no range and no planes to step through: a press turns its pages, the one way to them
     * on a board without touch */
    if (ev == 1 && u->view == VIEW_MAIN && u->cfg.look == LOOK_WEATHER) { next_wx_page(u); return; }
#if !RADAR_HAS_TOUCH
    /* no touch (the RPi LCD): a press steps through the planes on the scope, nearest first, and
     * past the last one lets go. held: the next range */
    if (ev == 1) step_plane(u);
    else next_range(u, cmd);
    return;
#endif
    if (ev == 2) {
        u->view = VIEW_MAIN;
        u->sel = 0;
        memset(u->wifi_pass, 0, sizeof u->wifi_pass);   // left the keyboard: what was typed goes
        return;
    }
    next_range(u, cmd);
}

/* the keys of the e-paper boards: the dial nearer and further, pressed the next plane, held the watch
 * list for it; MENU the next look, held ABOUT; EXIT lets the plane go and leaves a page */
void ui_key_wait(ui_t *u, int key)
{
    u->key_last = (uint8_t)key;                        // its third of the keys' strip prints pressed in the next picture
    u->key_at = u->now ? u->now : 1;
}

int ui_key_pend(ui_t *u, int key, uint32_t now, ui_cmd_t *cmd)
{
    if (u->view != VIEW_MAIN) return 0;
    const int wx = u->cfg.look == LOOK_WEATHER;
    int kind = PEND_NONE;
    if (key == KEY_UP || key == KEY_DOWN) kind = wx ? PEND_PAGE : PEND_RANGE;
    else if (key == KEY_MENU || (key == KEY_BACK && !sel_plane(u))) kind = PEND_LOOK;
    if (kind == PEND_NONE) return 0;
    if (u->pend_kind && (u->pend_kind != kind || (kind == PEND_LOOK && u->pend_key != key))) ui_pend_take(u, cmd);
    if (u->pend_kind != kind) {                        // gathering starts from what is shown
        u->pend_kind = (uint8_t)kind;
        u->pend_range = u->cfg.range;
        u->pend_page = u->wx_page;
        u->pend_look = u->cfg.look;
    }
    const int d = key == KEY_UP ? -1 : 1;
    if (kind == PEND_RANGE) u->pend_range = range_from(u->pend_range, d);
    else if (kind == PEND_PAGE) u->pend_page = (uint8_t)((u->pend_page + WX_PAGES + d) % WX_PAGES);
    else u->pend_look = (uint8_t)(key == KEY_MENU ? ui_look_after(u->pend_look) : ui_look_before(u->pend_look));
    u->pend_key = (uint8_t)key;
    u->pend_at = now ? now : 1;
    u->key_at = 0;
    return 1;
}

int ui_pend_take(ui_t *u, ui_cmd_t *cmd)
{
    const int kind = u->pend_kind;
    u->pend_kind = PEND_NONE;
    if (kind == PEND_RANGE && u->pend_range != u->cfg.range) {
        u->cfg.range = u->pend_range;
        cmd->cfg = cmd->refetch = 1;
        return 1;
    }
    if (kind == PEND_PAGE && u->pend_page != u->wx_page) {
        u->wx_page = u->pend_page;
        return 1;
    }
    if (kind == PEND_LOOK && u->pend_look != u->cfg.look) {
        set_look(u, cmd, u->pend_look);
        return 1;
    }
    return 0;                                          // asked back to where it was: nothing to print
}

void ui_key(ui_t *u, int key, ui_cmd_t *cmd)
{
    u->key_at = 0;                                     // the press is taken: its third back to grey
    if (u->view != VIEW_MAIN) {                        // ABOUT, or a page the site opened: back to the radar
        if (key == KEY_BACK || key == KEY_MENU) u->view = VIEW_MAIN;
        return;
    }
    const int wx = u->cfg.look == LOOK_WEATHER;        // no range and no planes there: the dial turns its pages
    switch (key) {
    case KEY_UP:
        if (wx) step_wx_page(u, -1);                   // up: the page before
        else step_range(u, cmd, -1);
        break;
    case KEY_DOWN:
        if (wx) next_wx_page(u);
        else step_range(u, cmd, 1);
        break;
    case KEY_OK:
        if (!wx) step_plane(u);
        break;
    case KEY_OK_LONG:
        for (int i = 0; i < u->snap.n && !wx; i++) {
            if (u->sel && u->snap.p[i].icao == u->sel) {
                watch_toggle(u, &u->snap.p[i], cmd);
                break;
            }
        }
        break;
    case KEY_MENU:
        next_look(u, cmd);
        break;
    case KEY_MENU_LONG:
        u->view = VIEW_ABOUT;                          // the version, and the QR code to the site that sets it up
        break;
    case KEY_BACK:
        u->sel = 0;
        break;
    default:
        break;
    }
}

/* --- draw one strip --- */

#if RADAR_OVERLAY
void ui_top_strip(ui_t *u, canvas_t *cv) { main_top(u, cv); }
#endif

void ui_strip(ui_t *u, canvas_t *cv)
{
#if RADAR_EPAPER
    if (paper_shown(u)) {                              // its own picture, its own cards (ui_paper.c)
        paper_draw(u, cv);
        return;
    }
#endif
    ui_lap(u, cv, -1);
    if (u->view != VIEW_MAIN || !square_look(u))       // the square map covers every pixel of it
        draw_background(cv, u->view == VIEW_MAIN || u->view == VIEW_CAL);   // pages of text read better on a plain ground
    ui_lap(u, cv, 0);
    switch (u->view) {
    case VIEW_LIST:  list_draw(u, cv); break;
    case VIEW_SETUP: setup_draw(u, cv); break;
    case VIEW_CAL:   cal_draw(u, cv); break;
    case VIEW_WIFI:  wifi_draw(u, cv); break;
    case VIEW_KEYS:  keys_draw(u, cv); break;
    case VIEW_ABOUT: about_draw(u, cv); break;
    default:         main_draw(u, cv); boot_banner(u, cv); break;
    }
    if (ui_until(u->toast_until, u->now)) {
        int w = font_width(&FONT_MEDIUM, u->toast) + 20;
        int ty = u->view == VIEW_CAL ? GFX_H / 2 + 52 : GFX_H / 2 - 16;
        chamfer_box(cv, (GFX_W - w) / 2, ty, w, 26, C_BG0, 225, C_FOCUS, 230);
        font_text(cv, &FONT_MEDIUM, (GFX_W - w) / 2 + 10, ty + 6, u->toast, C_FOCUS, 255);
    }
    if (u->t_down && u->view != VIEW_CAL) {
        gfx_ring(cv, (float)u->t_x, (float)u->t_y, 9.0f, 1.2f, C_FOCUS, 210);
        gfx_disc(cv, (float)u->t_x, (float)u->t_y, 1.6f, C_FOCUS, 255);
    }
    ui_lap(u, cv, 5);
}
