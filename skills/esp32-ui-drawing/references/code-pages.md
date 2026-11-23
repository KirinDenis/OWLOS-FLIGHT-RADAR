# Screens as code: lists, setup, spectrum, hunt, walk map, round views

Real page code, so a new page can start from one. Runs on hardware on the 2.8" CYD, the 3.5" CYD and
the round C3 unless said otherwise.

# Flight Radar core/ui_pages.c: the list and setup with tabs

```c
/* the other pages: the list of flights, setup, the touch alignment */
#include "ui_int.h"
#include <stdlib.h>

/* --- the list: every plane, nearest first --- */

/* the columns were laid out on 320 wide, wider screens spread them. the round glass has room
 * for less: no column names, the name, the height and how far */
#define LX(x) ((x) * GFX_W / 320)
#define LIST_HEAD (RADAR_ROUND ? 0 : 14)

static int list_y(const ui_t *u, int k) { return LIST_Y0 + LIST_HEAD + k * LIST_ROW - u->list_scroll; }

int list_hit(const ui_t *u, int x, int y)
{
    (void)x;
    int b = bar_hit(x, y, 2);
    if (b >= 0) return HIT_BAR + b;
    if (y < LIST_Y0 + LIST_HEAD || y >= LIST_Y1) return HIT_NONE;
    int k = (y - LIST_Y0 - LIST_HEAD + u->list_scroll) / LIST_ROW;
    return k >= 0 && k < u->n_order ? HIT_ROW + k : HIT_NONE;
}

void list_draw(ui_t *u, canvas_t *cv)
{
    char buf[24];
    int ya = cv->y0 > LIST_Y0 + LIST_HEAD ? cv->y0 : LIST_Y0 + LIST_HEAD, yb = cv->y0 + cv->h - 1 < LIST_Y1 - 1 ? cv->y0 + cv->h - 1 : LIST_Y1 - 1;
    if (ya <= yb) {
        canvas_t lc = { cv->px + (ya - cv->y0) * GFX_W, ya, yb - ya + 1, cv->helper };
        for (int k = 0; k < u->n_order; k++) {
            int y = list_y(u, k);
            if (!gfx_touches(&lc, y, y + LIST_ROW)) continue;
            const plane_t *p = &u->snap.p[u->order[k]];
            int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_ROW + k;
            if (pressed || p->icao == u->sel) gfx_fill(&lc, PG_L + 4, y, PG_R - PG_L - 8, LIST_ROW - 2, C_PRIM, pressed ? 70 : 35);
            gfx_hline(&lc, PG_L + 6, PG_R - 7, y + LIST_ROW - 1, C_QUIET, 70);
            uint32_t col = alt_colour(u, p);
            silhouette(&lc, p->kind, PG_L + 14.0f, y + 10.0f, p->track, 0.9f, col, 255);
            char d[16];
#if RADAR_ROUND
            font_text_clip(&lc, &FONT_SMALL, PG_L + 26, y + 6, p->flight[0] ? p->flight : p->reg, C_FOCUS, 250, 58);
            fmt_alt(u, p, buf, sizeof buf, 1);
            font_text(&lc, &FONT_SMALL, PG_L + 88, y + 6, buf, col, 245);
            fmt_dist(u, p->dist, d, sizeof d);
            char *sp = strchr(d, ' ');
            if (sp) *sp = 0;                            // the number, the unit is in the title
            font_text(&lc, &FONT_SMALL, PG_R - 6 - font_width(&FONT_SMALL, d), y + 6, d, C_DIM, 235);
            continue;
#endif
            font_text_clip(&lc, &FONT_MEDIUM, 28, y + 4, p->flight[0] ? p->flight : p->reg, C_FOCUS, 250, LX(102) - 32);
            font_text_clip(&lc, &FONT_SMALL, LX(102), y + 6, p->type[0] ? p->type : "----", C_DIM, 235, 34);
            fmt_alt(u, p, buf, sizeof buf, 1);
            font_text(&lc, &FONT_SMALL, LX(140), y + 6, buf, col, 245);
            snprintf(buf, sizeof buf, p->has_spd ? "%d KT" : "--", (int)lroundf(p->gs));
            font_text(&lc, &FONT_SMALL, LX(190), y + 6, buf, C_PRIM, 235);
            fmt_dist(u, p->dist, d, sizeof d);
            snprintf(buf, sizeof buf, "%s %s", d, compass8(p->brg));
            font_text(&lc, &FONT_SMALL, GFX_W - 8 - font_width(&FONT_SMALL, buf), y + 6, buf, C_DIM, 235);
        }
        if (!u->n_order) micro_text(&lc, PG_L + 12, LIST_Y0 + 24, "NOTHING IN RANGE", C_DIM, 230, RADAR_ROUND ? 1 : 2);
    }
    char d[16], right[24];
    fmt_dist(u, range_km(&u->cfg), d, sizeof d);
    snprintf(right, sizeof right, "%d IN %s", u->n_order, d);
    page_header(cv, "FLIGHTS", right, C_FOCUS);
    if (!RADAR_ROUND && gfx_touches(cv, LIST_Y0, LIST_Y0 + 12)) {
        static const char *const H[5] = { "FLIGHT", "TYPE", "HEIGHT", "SPEED", "DISTANCE" };
        const int X[5] = { 28, LX(102), LX(140), LX(190), 0 };
        for (int i = 0; i < 4; i++) micro_text(cv, X[i], LIST_Y0 + 4, H[i], C_QUIET, 230, 1);
        micro_text(cv, GFX_W - 8 - micro_width(H[4], 1), LIST_Y0 + 4, H[4], C_QUIET, 230, 1);
    }
    if (RADAR_ROUND) snprintf(right, sizeof right, "ZOOM");            // the round bar is short
    else snprintf(right, sizeof right, "ZOOM %s", d);
    const char *L[2] = { right, "BACK" };
    bar(u, cv, L, 2, -1);
}
```

```c
/* --- setup: one row a setting, what it does under its name. the rows sit in tabs: how it looks,
 * what it shows and where from, the LED. the tabs are the bottom bar's buttons, BACK the last --- */

enum { S_RANGE, S_GROUND, S_WATCH, S_DIST, S_ALT, S_UP, S_FLIP, S_LABELS, S_THEME, S_LOOK, S_MAP, S_SOURCE, S_SERVER, S_HOME,
       S_WIFI,
#if RADAR_HAS_LED
       S_LED_SOS, S_LED_RANGE, S_LED_UP, S_LED_DOWN, S_LED_NEAR, S_LED_NEAR_KM, S_LED_NODATA, S_LED_LEVEL, S_LED_HOLD,
       S_LED_TEST,
#if RADAR_LED_RED_PIN
       S_LED_RED,
#endif
#endif
       S_TOUCH, S_ABOUT, S_ROWS };
enum { K_NUM, K_BTN, K_INFO };
enum { T_VIEW, T_DATA, T_LED };
#define TABS (RADAR_HAS_LED ? 3 : 2)

static const struct { const char *label, *desc; int kind, tab; } ROWS[S_ROWS] = {
    { "RANGE",       "or drag inside the scope",     K_NUM, T_VIEW },
    { "ON GROUND",   "planes parked or taxiing",     K_BTN, T_DATA },
    { "WATCH",       "",                             UI_WIFI_PAGE ? K_BTN : K_INFO, T_DATA },   /* planes to follow */
    { "DISTANCE",    "km, nautical miles or miles",  K_BTN, T_VIEW },
    { "ALTITUDE",    "feet or metres",               K_BTN, T_VIEW },
    { "MAP UP",      "or drag the scope's rim",      K_NUM, T_VIEW },
    { "SCREEN",      "mounted upside down?",         K_BTN, T_VIEW },
    { "LABELS",      "names beside the planes",      K_BTN, T_VIEW },
    { "COLOURS",     "five colour themes",           K_BTN, T_VIEW },
    { "LOOK",        "radar, flat map, map's colours", K_BTN, T_VIEW },
    { "MAP",         "OpenStreetMap under the scope", K_BTN, T_VIEW },
    { "DATA",        "free, or FR24 with a key",     K_BTN, T_DATA },
    { "FREE SERVER", "tried first, others back up",  K_BTN, T_DATA },
    { "HOME",        "",                             UI_WIFI_PAGE ? K_BTN : K_INFO, T_DATA },   /* typed on the WiFi keyboard */
    { "WIFI",        "",                             UI_WIFI_PAGE ? K_BTN : K_INFO, T_DATA },
#if RADAR_HAS_LED
    /* the LED on the back (core/light.h): each event on or off, in the order they win */
    { "SOS",         "7500 7600 7700 round home: red", K_BTN, T_LED },
    { "IN RANGE",    "a watched plane comes: green", K_BTN, T_LED },
    { "TAKE-OFF",    "a watched plane takes off: blue", K_BTN, T_LED },
    { "LANDING",     "a watched plane lands: cyan",  K_BTN, T_LED },
    { "ANY NEAR",    "any plane in the air near: white", K_BTN, T_LED },
    { "HOW NEAR",    "from home, for ANY NEAR",      K_BTN, T_LED },
    { "NO DATA",     "no WiFi or no answer: amber",  K_BTN, T_LED },
    { "BRIGHTNESS",  "low for the night",            K_BTN, T_LED },
    { "HOW LONG",    "till touched, 10 s, while it lasts", K_BTN, T_LED },
    { "TEST",        "red, green, blue in turn",     K_BTN, T_LED },
#if RADAR_LED_RED_PIN
    { "RED ON",      "no red in TEST: the other",    K_BTN, T_LED },   /* GPIO 4 or 22: light.h */
#endif
#endif
    { "TOUCH",       "align the touch screen",       K_BTN, T_VIEW },
    { "ABOUT",       "",                             RADAR_HAS_TOUCH ? K_BTN : K_INFO, T_DATA },   /* the version, the site */
};

#define SB_X  (PG_R - 84)                     /* the keys of a row */
#define SB_W  38
#define SK_H  (RADAR_ROUND ? 26 : 30)
#define SK_Y  ((ROW_H - SK_H) / 2)

// the k-th row seen in the tab shown: its S_ number, -1 past the last
static int row_at(const ui_t *u, int k)
{
    for (int i = 0; i < S_ROWS; i++) if (ROWS[i].tab == u->setup_tab && k-- == 0) return i;
    return -1;
}

int setup_rows(const ui_t *u)
{
    int n = 0;
    for (int i = 0; i < S_ROWS; i++) n += ROWS[i].tab == u->setup_tab;
    return n;
}

static int row_y(const ui_t *u, int k) { return LIST_Y0 + k * ROW_H - u->setup_scroll; }

int setup_hit(const ui_t *u, int x, int y)
{
    int b = bar_hit(x, y, TABS + 1);
    if (b >= 0) return HIT_BAR + b;
    if (y < LIST_Y0 || y > BAR_Y - 3) return HIT_NONE;
    int i = row_at(u, (y - LIST_Y0 + u->setup_scroll) / ROW_H);
    if (i < 0 || x < SB_X - 4) return HIT_NONE;
    if (ROWS[i].kind == K_NUM) return x < SB_X + SB_W + 2 ? HIT_SDEC + i : HIT_SINC + i;
    if (ROWS[i].kind == K_BTN) return HIT_SBTN + i;
    return HIT_NONE;
}

int setup_repeats(int hit) { return hit >= HIT_SDEC && hit < HIT_SBTN; }

```

```c
static void small_key(ui_t *u, canvas_t *cv, int x, int y, int hit, int plus)
{
    int pressed = u->t_down && !u->t_moved && u->t_hit == hit;
    chamfer_box(cv, x, y, SB_W, SK_H, pressed ? C_PRIM : C_PANEL, pressed ? 150 : 170, pressed ? C_FOCUS : C_PRIM, 210);
    float cx = x + SB_W / 2.0f, cy = y + SK_H / 2.0f;
    uint32_t c = pressed ? C_FOCUS : C_PRIM;
    gfx_line(cv, cx - 7, cy, cx + 7, cy, 2.4f, c, 255);
    if (plus) gfx_line(cv, cx, cy - 7, cx, cy + 7, 2.4f, c, 255);
}

void setup_draw(ui_t *u, canvas_t *cv)
{
    int y1 = BAR_Y - 3;
    int ya = cv->y0 > LIST_Y0 ? cv->y0 : LIST_Y0, yb = cv->y0 + cv->h - 1 < y1 ? cv->y0 + cv->h - 1 : y1;
    if (ya <= yb) {
        canvas_t lc = { cv->px + (ya - cv->y0) * GFX_W, ya, yb - ya + 1, cv->helper };
        char v[32], d[112];
        for (int k = 0, i; (i = row_at(u, k)) >= 0; k++) {
            int y = row_y(u, k);
            if (!gfx_touches(&lc, y, y + ROW_H)) continue;
            gfx_hline(&lc, PG_L + 6, PG_R - 7, y + ROW_H - 1, C_QUIET, 90);
            font_text(&lc, RADAR_ROUND ? &FONT_SMALL : &FONT_MEDIUM, PG_L + 10, y + 4, ROWS[i].label,
                      ROWS[i].kind == K_INFO ? C_DIM : C_PRIM, 255);
            const char *desc = ROWS[i].desc;
            if (i == S_HOME) {
                if (u->cfg.place[0]) snprintf(d, sizeof d, "%s%s%s, %.3f %.3f", u->cfg.place, u->cfg.country[0] ? ", " : "",
                                              u->cfg.country, (double)u->cfg.lat, (double)u->cfg.lon);
                else snprintf(d, sizeof d, UI_WIFI_PAGE ? "%.4f %.4f" : "%.4f %.4f, from the site", (double)u->cfg.lat,
                              (double)u->cfg.lon);
                desc = d;
            } else if (i == S_WIFI) {
                if (!u->net.configured) snprintf(d, sizeof d, UI_WIFI_PAGE ? "not set: SET UP, or from the site" : "not set: send it from the site");
                else snprintf(d, sizeof d, "%s, %s", u->net.ssid, u->net.wifi ? u->net.ip : "not connected");
                desc = d;
            } else if (i == S_WATCH) {                  // what the planes on it are doing
                const watchlist_t *w = &u->watch;
                if (!w->n) snprintf(d, sizeof d, UI_WIFI_PAGE ? "planes to follow: none" : "from the site");
                else snprintf(d, sizeof d, RADAR_ROUND ? "%d: %d flying, %d near" : "%d set, %d flying, %d in range", w->n,
                              watch_count(w, WATCH_AIR), watch_near(w));   // the round glass has a short row
                desc = d;
            } else if (i == S_ABOUT) {                  // the version, and what the site has
                const char *v = ui_version ? ui_version : "?";
                int n = !u->newest[0] ? snprintf(d, sizeof d, "version %s", v)
                      : ui_update(u) ? snprintf(d, sizeof d, "version %s, %s on the site", v, u->newest)
                      : snprintf(d, sizeof d, "version %s, the newest", v);
                if (!RADAR_HAS_TOUCH && n > 0 && n < (int)sizeof d)   // no ABOUT to open: the key here
                    snprintf(d + n, sizeof d - (size_t)n, ", FR24 key %s", u->net.has_key ? "stored" : "none");
                desc = d;
            }
            value_text(u, i, v, sizeof v);
#if RADAR_ROUND
            /* the round glass: no room for what a row does. under the name: a number's value,
             * or what an information row says */
            const char *under = ROWS[i].kind == K_NUM ? v : ROWS[i].kind == K_INFO ? desc : NULL;
            if (under) font_text_clip(&lc, &FONT_SMALL, PG_L + 10, y + 17, under, ROWS[i].kind == K_NUM ? C_FOCUS : C_DIM, 240,
                                      ROWS[i].kind == K_INFO ? PG_R - PG_L - 20 : SB_X - PG_L - 14);
#else
            font_text_clip(&lc, &FONT_SMALL, 10, y + 22, desc, C_DIM, 235, (ROWS[i].kind == K_INFO ? GFX_W - 20 : SB_X - 16));
            if (ROWS[i].kind == K_NUM) font_text(&lc, &FONT_MEDIUM, SB_X - 8 - font_width(&FONT_MEDIUM, v), y + 6, v, C_FOCUS, 250);
#endif
            if (ROWS[i].kind == K_NUM) {
                small_key(u, &lc, SB_X, y + SK_Y, HIT_SDEC + i, 0);
                small_key(u, &lc, SB_X + SB_W + 4, y + SK_Y, HIT_SINC + i, 1);
            } else if (ROWS[i].kind == K_BTN) {
                int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_SBTN + i;
                button_font(&lc, SB_X, y + SK_Y, 2 * SB_W + 4, SK_H, v, pressed ? 2 : 0, &FONT_SMALL);
            }
        }
        int total = setup_rows(u) * ROW_H, view = y1 - LIST_Y0;
        if (total > view) {                            // a tab that fits has no scroll bar
            int th = view * view / total, ty = LIST_Y0 + (view - th) * u->setup_scroll / (total - view);
            gfx_fill(&lc, PG_R - 3, ty, 2, th, C_QUIET, 200);
        }
    }
    page_header(cv, "SETUP", RADAR_ROUND ? NULL : "DRAG TO SCROLL", C_FOCUS);
    static const char *const L[4] = { "VIEW", "DATA", RADAR_HAS_LED ? "LED" : "BACK", "BACK" };
    bar(u, cv, L, TABS + 1, u->setup_tab);
}
```

# Flight Radar core/ui.c: helpers every page uses

```c
void chamfer_box(canvas_t *cv, int x, int y, int w, int h, uint32_t fill, int fill_a, uint32_t line, int line_a)
{
    if (!gfx_touches(cv, y - 1, y + h)) return;
    const int k = 5, x1 = x + w - 1, y1 = y + h - 1;
    if (fill_a) gfx_fill(cv, x, y, w, h, fill, fill_a);
    // the straight sides are whole pixels, only the two cut corners need smoothing
    gfx_hline(cv, x + k + 1, x1, y, line, line_a);
    gfx_vline(cv, x1, y + 1, y1 - k - 1, line, line_a);
    gfx_line(cv, (float)x1, (float)(y1 - k), (float)(x1 - k), (float)y1, 1.0f, line, line_a);
    gfx_hline(cv, x, x1 - k - 1, y1, line, line_a);
    gfx_vline(cv, x, y + k + 1, y1 - 1, line, line_a);
    gfx_line(cv, (float)x, (float)(y + k), (float)(x + k), (float)y, 1.0f, line, line_a);
}
```

```c
/* --- the classic look's things on the map: light, rounded, a soft shadow, dark ink, the way a
 * map on paper floats its buttons. the instrument's cut corners belong to the dark looks --- */

// a box with round corners, smooth at the edges, one coat of colour all over (a shadow stays even)
void round_box(canvas_t *cv, int x, int y, int w, int h, int r, uint32_t c, int a)
{
    if (!gfx_touches(cv, y, y + h - 1)) return;
    const float x0 = (float)(x + r), x1 = (float)(x + w - r), y0 = (float)(y + r), y1 = (float)(y + h - r);   // the corners' middles
    for (int yy = y; yy < y + h; yy++) {
        uint32_t *row = gfx_row(cv, yy);
        if (!row) continue;
        float py = yy + 0.5f, dy = py < y0 ? y0 - py : py > y1 ? py - y1 : 0;
        for (int xx = x < 0 ? 0 : x; xx < x + w && xx < GFX_W; xx++) {
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
```

```c
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
```

## The background

```c
/* --- background: gradient, a faint grid, dust; rebuilt on theme change --- */

static uint32_t s_bgrow[GFX_H];
static uint16_t s_vigx[GFX_W], s_vigy[GFX_H];
static int16_t  s_gridcols[GFX_W / 12 + 2];
static int      s_ngrid;
static struct { int16_t x, y; uint8_t a; } s_stars[128];
static int      s_nstars, s_bg_theme = -1;
#define VIG_W 56

static void build_background(void)
{
    for (int y = 0; y < GFX_H; y++) {
        float t = (float)y / (GFX_H - 1);
        s_bgrow[y] = gfx_mix(C_BG0, C_BG1, (uint32_t)(256.0f * (0.25f + 0.75f * t)));
        float v = ((float)y - GFX_H / 2.0f) / (GFX_H * 7.0f / 12.0f);
        s_vigy[y] = (uint16_t)(256.0f * clampf(1.0f - 0.45f * v * v, 0.3f, 1.0f));
    }
    s_ngrid = 0;
    for (int x = 0; x < GFX_W; x++) {
        int e = x < GFX_W / 2 ? x : GFX_W - 1 - x;
        float v = e < VIG_W ? 1.0f - (float)e / VIG_W : 0.0f;
        s_vigx[x] = (uint16_t)(256.0f * (1.0f - 0.4f * v * v));
        if (((x - SCX + 1200) % 12) == 0) s_gridcols[s_ngrid++] = (int16_t)x;
    }
    s_nstars = 0;
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W && s_nstars < 128; x++) {
            uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            if ((h & 1023u) == 0) {
                s_stars[s_nstars].x = (int16_t)x;
                s_stars[s_nstars].y = (int16_t)y;
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
        uint32_t *row = cv->px + r * GFX_W;
        uint32_t base = gfx_scale(s_bgrow[y], s_vigy[y]);
        int lined = grid_on && ((y - SCY + 1200) % 12) == 0;
        uint32_t fill = lined ? gfx_add(base, grid) : base;
        uint32_t col = gfx_add(base, lined ? node : grid);
        for (int x = 0; x < GFX_W; x++) row[x] = fill;
        if (grid_on) for (int i = 0; i < s_ngrid; i++) row[s_gridcols[i]] = col;
        for (int x = 0; x < VIG_W; x++) {
            row[x] = gfx_scale(row[x], s_vigx[x]);
            row[GFX_W - 1 - x] = gfx_scale(row[GFX_W - 1 - x], s_vigx[GFX_W - 1 - x]);
        }
    }
    for (int i = 0; i < s_nstars; i++) {
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
```

# Flight Radar core/ui_scope.c: the demo card and the round glass

```c
/* --- no WiFi: the demo sky, said in big letters, why, and the way to the WiFi --- */

static void demo_why(const ui_t *u, char *buf, int sz)
{
    if (!u->net.configured) snprintf(buf, (size_t)sz, "NO WIFI SET UP");
    else if (u->net.why == WHY_PASSWORD) snprintf(buf, (size_t)sz, "%s: WRONG PASSWORD?", u->net.ssid);
    else snprintf(buf, (size_t)sz, "CAN'T SEE WIFI %s", u->net.ssid);
}

// a network that is set but not there is a fault; none set yet is only news
static uint32_t demo_why_col(const ui_t *u)
{
    if (u->cfg.look == LOOK_CLASSIC) return u->net.configured ? CL_ALARM : CL_DIM;
    return u->net.configured ? C_ALARM : C_DIM;
}

// the first seconds of a board sold with different screens belong to the card that says how to
// pick one (boot_banner): the demo's card waits till then, the two would cover each other
static int demo_shown(const ui_t *u)
{
    return u->net.demo && !(ui_screen_name && u->now < UI_SCREEN_PICK_MS);
}

#if !RADAR_ROUND
/* under home, in the scope or on the map: DEMO in the pixel font this many times over, so it reads
 * across the room, the reason, and SET UP WIFI (the RPi LCD has no touch: a line about the site) */
demo_box_t demo_box(const ui_t *u)
{
    demo_box_t d;
    d.sc = GFX_H >= 320 ? 5 : 4;
    d.bw = GFX_H >= 320 ? 160 : 132;
    d.bh = UI_WIFI_PAGE ? (GFX_H >= 320 ? 30 : 24) : 7;
    d.w = d.bw + 40;
    d.h = 5 + 5 * d.sc + 5 + 10 + 5 + d.bh + 6;
    d.x = view_cx(u) - d.w / 2;
    d.y = SCY + 8;
    d.bx = view_cx(u) - d.bw / 2;
    d.by = d.y + d.h - 6 - d.bh;
    return d;
}

static void demo_card(ui_t *u, canvas_t *cv)
{
    if (!demo_shown(u)) return;
    const demo_box_t d = demo_box(u);
    if (!gfx_touches(cv, d.y, d.y + d.h)) return;
    const int classic = u->cfg.look == LOOK_CLASSIC, cx = view_cx(u);
    char why[64];
    demo_why(u, why, sizeof why);
    if (classic) paper_card(cv, d.x, d.y, d.w, d.h);
    else chamfer_box(cv, d.x, d.y, d.w, d.h, C_BG0, 215, C_FOCUS, 200);
    micro_text(cv, cx - micro_width("DEMO", d.sc) / 2, d.y + 5, "DEMO", classic ? CL_INK : C_FOCUS, 255, d.sc);
    int ww = font_width(&FONT_SMALL, why);
    if (ww > d.w - 12) ww = d.w - 12;
    font_text_clip(cv, &FONT_SMALL, cx - ww / 2, d.y + 10 + 5 * d.sc, why, demo_why_col(u), 245, d.w - 12);
#if UI_WIFI_PAGE
    const int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_WIFI;
    if (classic) map_button(cv, d.bx, d.by, d.bw, d.bh, "SET UP WIFI", pressed ? 2 : 0);
    else button(cv, d.bx, d.by, d.bw, d.bh, "SET UP WIFI", pressed ? 2 : 1);
#else
    const char *s = "SEND WIFI FROM THE SITE";
    micro_text(cv, cx - micro_width(s, 1) / 2, d.by + 1, s, classic ? CL_INK : C_PRIM, 235, 1);
#endif
}
#endif
```

```c
static int menu_up(const ui_t *u) { return !u->sel && (int32_t)(u->menu_until - u->now) > 0; }

static void round_card(ui_t *u, canvas_t *cv, const plane_t *p)
{
    char buf[32], d[16];
    uint32_t col = alt_colour(u, p);
    int x = SCX - CARD_W / 2, y = CARD_Y;
    if (!gfx_touches(cv, y, y + CARD_H)) return;
    int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_PANEL;
    chamfer_box(cv, x, y, CARD_W, CARD_H, pressed ? C_PRIM : C_BG0, pressed ? 120 : 215, col, 220);
    font_text_clip(cv, &FONT_MEDIUM, x + 6, y + 2, p->flight[0] ? p->flight : "NO CALLSIGN", C_FOCUS, 255, 84);
    int sos = p->squawk == 7500 || p->squawk == 7600 || p->squawk == 7700;
    if (sos) snprintf(buf, sizeof buf, "%04u", (unsigned)p->squawk);
    const char *tag = sos ? buf : kind_name(p->kind);
    micro_text(cv, x + CARD_W - 6 - micro_width(tag, 1), y + 6, tag, sos ? C_ALARM : C_DIM, 235, 1);
    snprintf(buf, sizeof buf, "%s %s", p->type[0] ? p->type : "----", p->reg);
    font_text_clip(cv, &FONT_SMALL, x + 6, y + 17, buf, C_DIM, 235, CARD_W - 12);
    fmt_alt(u, p, buf, sizeof buf, 0);
    int w = font_text(cv, &FONT_MEDIUM, x + 6, y + 27, buf, col, 255);
    if (abs(p->vrate) >= 200) {                        // climbing or coming down
        float ax = x + 12.0f + w, ay = y + 34.0f;
        if (p->vrate > 0) gfx_tri(cv, ax - 3, ay + 3, ax + 3, ay + 3, ax, ay - 3, col, 240);
        else gfx_tri(cv, ax - 3, ay - 3, ax + 3, ay - 3, ax, ay + 3, col, 240);
    }
    fmt_dist(u, p->dist, d, sizeof d);
    snprintf(buf, sizeof buf, "%d KT  %s %s", (int)lroundf(p->gs), d, compass8(p->brg));
    micro_text(cv, x + 6, y + 45, buf, C_PRIM, 240, 1);
}

// what each button of the row is: HUE, the look (it says which one is on), LIST, SET
static int menu_hit(int i) { return i == 1 ? HIT_LOOK : HIT_BTN + (i ? i - 1 : 0); }

static void round_menu(ui_t *u, canvas_t *cv)
{
    const char *L[MENU_N] = { "HUE", LOOK_NAME[u->cfg.look < LOOKS ? u->cfg.look : 0], "LIST", "SET" };
    if (!gfx_touches(cv, MENU_Y, MENU_Y + MENU_H)) return;
    for (int i = 0; i < MENU_N; i++) {
        int pressed = u->t_down && !u->t_moved && u->t_hit == menu_hit(i);
        if (u->cfg.look == LOOK_CLASSIC) map_button(cv, menu_x(i), MENU_Y, MENU_W, MENU_H, L[i], pressed ? 2 : 0);   // paper on paper
        else button(cv, menu_x(i), MENU_Y, MENU_W, MENU_H, L[i], pressed ? 2 : 0);
    }
}

// no WiFi: DEMO in big letters, why, and that the site sends the WiFi (no room for a keyboard here)
static void round_demo(ui_t *u, canvas_t *cv)
{
    const int y = SCY + 34, h = 46, classic = u->cfg.look == LOOK_CLASSIC;
    if (!gfx_touches(cv, y, y + h)) return;
    char why[64];
    demo_why(u, why, sizeof why);
    const char *hint = "SEND WIFI FROM THE SITE";
    int ww = font_width(&FONT_SMALL, why), wh = micro_width(hint, 1);
    if (ww > 140) ww = 140;
    int w = (ww > wh ? ww : wh) + 14;
    if (classic) paper_card(cv, SCX - w / 2, y, w, h);
    else chamfer_box(cv, SCX - w / 2, y, w, h, C_BG0, 215, C_FOCUS, 200);
    micro_text(cv, SCX - micro_width("DEMO", 3) / 2, y + 4, "DEMO", classic ? CL_INK : C_FOCUS, 255, 3);
    font_text_clip(cv, &FONT_SMALL, SCX - ww / 2, y + 22, why, demo_why_col(u), 245, 140);
    micro_text(cv, SCX - wh / 2, y + 36, hint, classic ? CL_DIM : C_PRIM, 235, 1);
}

// where home is, how many around, where they come from: a small box under home, out of the
// planes' way as it can be
static void round_note(ui_t *u, canvas_t *cv)
{
    char top[80], mid[48], low[48], d[16], c[24];
    int n = 0, bad;
    const int y = SCY + 44, h = 32;
    if (demo_shown(u)) { round_demo(u, cv); return; }
    if (!gfx_touches(cv, y, y + h)) return;
    for (int i = 0; i < u->snap.n; i++) n += u->spot[i].vis == 1;
    const fr_cfg_t *cf = &u->cfg;
    if (cf->place[0] && cf->country[0]) snprintf(top, sizeof top, "%s, %s", cf->place, cf->country);
    else snprintf(top, sizeof top, "%s", cf->place[0] ? cf->place : cf->country[0] ? cf->country : "HOME");
    fmt_dist(u, range_km(&u->cfg), d, sizeof d);
    home_coords(u, c, sizeof c);
    snprintf(mid, sizeof mid, "%s  %d IN %s", c, n, d);
    sky_status(u, low, sizeof low, &bad);
    int wt = font_width(&FONT_SMALL, top), wm = micro_width(mid, 1), wl = micro_width(low, 1);
    int w = (wt > wm ? wt : wm);
    w = (w > wl ? w : wl) + 12;
    if (u->cfg.look == LOOK_CLASSIC) {                // on the light map: a paper card, dark ink
        paper_card(cv, SCX - w / 2, y, w, h);
        font_text(cv, &FONT_SMALL, SCX - wt / 2, y + 2, top, CL_INK, 255);
        micro_text(cv, SCX - wm / 2, y + 15, mid, CL_INK, 235, 1);
        micro_text(cv, SCX - wl / 2, y + 23, low, bad ? CL_ALARM : CL_DIM, 235, 1);
        return;
    }
    gfx_fill(cv, SCX - w / 2, y, w, h, C_BG0, 170);
    gfx_hline(cv, SCX - w / 2, SCX + w / 2 - 1, y, bad ? C_ALARM : C_QUIET, 160);
    font_text(cv, &FONT_SMALL, SCX - wt / 2, y + 2, top, C_FOCUS, 255);
    micro_text(cv, SCX - wm / 2, y + 15, mid, C_PRIM, 235, 1);
    micro_text(cv, SCX - wl / 2, y + 23, low, bad ? C_ALARM : C_DIM, 235, 1);
}
```

# Flight Radar core/ui_about.c: ABOUT with the QR

```c
/* the code: dark modules on a light plate with a light edge round it, as printed ones are. a phone
 * reads a light code on a dark screen less surely. modules as big as the screen lets them be */
#if RADAR_ROUND
#define QR_PX 3                    /* pixels a module */
#elif GFX_H >= 320
#define QR_PX 5
#else
#define QR_PX 4
#endif
#define QR_EDGE 2                  /* modules of light round it */
#define QR_W    ((QR_SITE_N + 2 * QR_EDGE) * QR_PX)
#define QR_LIGHT RGB(250, 250, 247)
#define QR_DARK  RGB(10, 12, 20)

static void qr(canvas_t *cv, int x, int y)
{
    if (!gfx_touches(cv, y, y + QR_W)) return;
    gfx_fill(cv, x, y, QR_W, QR_W, QR_LIGHT, 255);
    const int x0 = x + QR_EDGE * QR_PX, y0 = y + QR_EDGE * QR_PX;
    for (int r = 0; r < QR_SITE_N; r++) {
        const int py = y0 + r * QR_PX;                 // the row's top
        if (!gfx_touches(cv, py, py + QR_PX)) continue;
        const uint8_t *row = QR_SITE + r * QR_SITE_ROW;
        for (int c = 0; c < QR_SITE_N;) {              // a run of dark modules is one fill
            if (!(row[c >> 3] & (0x80 >> (c & 7)))) { c++; continue; }
            int e = c;
            while (e < QR_SITE_N && (row[e >> 3] & (0x80 >> (e & 7)))) e++;
            gfx_fill(cv, x0 + c * QR_PX, py, (e - c) * QR_PX, QR_PX, QR_DARK, 255);   // modules c..e-1
            c = e;
        }
    }
}
```

```c
void about_draw(ui_t *u, canvas_t *cv)
{
    char said[48], ver[24], site[64];
    const uint32_t said_col = site_said(u, said, sizeof said);
    snprintf(ver, sizeof ver, "%s", ui_version && ui_version[0] ? ui_version : "?");
    site_name(site, sizeof site);
#if RADAR_ROUND
    // the glass: the code in the middle, the version over it, what the site said under it
    char head[32];
    snprintf(head, sizeof head, "VERSION %s", ver);
    qr(cv, (GFX_W - QR_W) / 2, HEAD_H + 4);
    int y = HEAD_H + 4 + QR_W + 4;
    if (gfx_touches(cv, y, y + 26)) {
        int w = font_width(&FONT_SMALL, said);
        font_text(cv, &FONT_SMALL, GFX_W / 2 - w / 2, y, said, said_col, 255);
        const char *scan = ui_update(u) ? "SCAN: THE SITE FLASHES IT" : "SCAN: THE SITE";
        micro_text(cv, GFX_W / 2 - micro_width(scan, 1) / 2, y + 15, scan, C_DIM, 235, 1);
    }
    page_header(cv, "ABOUT", head, C_FOCUS);
#else
    const int qx = GFX_W - 8 - QR_W, qy = LIST_Y0 + 4, x = 10;
    qr(cv, qx, qy);
    int y = LIST_Y0 + 6;
#define AY(small, tall) (GFX_H >= 320 ? (tall) : (small))   /* a line's place: the taller screen spaces them out */
    if (gfx_touches(cv, y, qy + QR_W)) {
        char line[48];
        micro_text(cv, x, y, "OWLOS FLIGHT RADAR", C_PRIM, 240, 1);
        micro_text(cv, x, y + AY(12, 14), "VERSION", C_QUIET, 230, 1);
        font_text(cv, &FONT_LARGE, x - 1, y + AY(20, 24), ver, C_FOCUS, 255);
        const int w = qx - x - 6;                      // the pixel font has no " and no &: these in the smooth one
        font_text_clip(cv, &FONT_SMALL, x, y + AY(44, 54), said, said_col, 250, w);
        font_text_clip(cv, &FONT_SMALL, x, y + AY(62, 78), RADAR_BOARD, C_DIM, 235, w);
        snprintf(line, sizeof line, "FR24 key: %s", u->net.has_key ? "stored" : "none");
        font_text_clip(cv, &FONT_SMALL, x, y + AY(76, 94), line, C_DIM, 235, w);
        if (ui_update(u)) {                            // how: the same page that flashed it, settings kept
            font_text_clip(cv, &FONT_SMALL, x, y + AY(96, 120), "to update: the site,", C_FOCUS, 250, w);
            font_text_clip(cv, &FONT_SMALL, x, y + AY(110, 136), "CONNECT & FLASH.", C_FOCUS, 250, w);
            font_text_clip(cv, &FONT_SMALL, x, y + AY(124, 152), "no erase: settings stay", C_FOCUS, 250, w);
        }
    }
#undef AY
    y = BAR_Y - 20;                                    // the address, for who would rather type it
    if (gfx_touches(cv, y, y + 14)) font_text_clip(cv, &FONT_SMALL, x, y, site, C_PRIM, 250, GFX_W - 2 * x);
    page_header(cv, "ABOUT", "SCAN IT: THE SITE", C_FOCUS);
#endif
    static const char *const L[1] = { "BACK" };
    bar(u, cv, L, 1, -1);
}
```

# WiFi Radar core/ui_views.c: list, spectrum, hunt, walk map

```c
/* full screen views: network list, spectrum, target, walk map, touch calibration.
 * touch handling for them is in ui.c */
#include "ui_int.h"
#include <stdlib.h>

/* --- network list, one per row --- */

int list_row_at(const ui_t *u, int y)
{
    if (y < LIST_Y0 || y >= LIST_Y1) return -1;
    int k = (y - LIST_Y0 + u->scroll) / ROW_H;
    return k < u->snap.n ? u->list[k] : -1;
}

void view_list(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[64];
    for (int k = 0; k < a->n; k++) {
        int ry = LIST_Y0 + k * ROW_H - u->scroll;
        if (ry > GFX_H) break;
        if (ry + ROW_H < 18 || !gfx_touches(cv, ry, ry + ROW_H)) continue;
        int i = u->list[k];
        const contact_t *c = &a->c[i];
        const blip_t *b = &u->b[i];
        if (i == u->target) gfx_fill(cv, 0, ry, GFX_W - 4, ROW_H - 1, C_FOCUS, 26);
        gfx_fill(cv, 3, ry + 2, 2, ROW_H - 5, b->col, 230);
        font_text_clip(cv, &FONT_MEDIUM, 10, ry + 1, net_name(c), gfx_mix(b->col, C_FOCUS, 120), 255, GFX_W - 148);
        float d = app_distance_m(c->rssi);
        int n = snprintf(buf, sizeof buf, "CH %d  %s  ~%d M", c->channel, sec_name(c->sec), (int)lroundf(d));
        if (b->src) snprintf(buf + n, sizeof buf - (size_t)n, "  BRG %03d", (int)lroundf(b->ang) % 360);
        micro_text(cv, 10, ry + 15, buf, C_DIM, 225, 1);
        float f = clampf((c->rssi + 95.0f) / 65.0f, 0, 1);
        gfx_fill(cv, GFX_W - 130, ry + 8, 64, 4, C_QUIET, 50);
        gfx_fill(cv, GFX_W - 130, ry + 8, (int)(64 * f), 4, b->col, 220);
        float tr = app_trend(c);
        if (tr > TREND_DB) arrow(cv, GFX_W - 56.0f, ry + 10.0f, 1, C_FOCUS);
        else if (tr < -TREND_DB) arrow(cv, GFX_W - 56.0f, ry + 10.0f, 0, C_DIM);
        snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
        font_text(cv, &FONT_MEDIUM, GFX_W - 8 - font_width(&FONT_MEDIUM, buf), ry + 3, buf, C_FOCUS, 255);
        gfx_hline(cv, 8, GFX_W - 8, ry + ROW_H - 1, C_QUIET, 45);
    }
    int total = a->n * ROW_H, vis = GFX_H - LIST_Y0;
    if (total > vis) {                                  // scrollbar
        int th = vis * vis / total, ty = LIST_Y0 + (vis - th) * u->scroll / (total - vis);
        gfx_fill(cv, GFX_W - 3, LIST_Y0, 2, vis, C_QUIET, 60);
        gfx_fill(cv, GFX_W - 3, ty, 2, th, C_FOCUS, 200);
    }
    snprintf(buf, sizeof buf, "NETWORKS %d", a->n);
    view_header(cv, buf, 0);
    static const char *keys[3] = { "SIGNAL", "NAME", "CH" };    // tap() in ui.c expects these at x >= GFX_W - 120, 40 px each
    for (int s = 0; s < 3; s++) {
        int x = GFX_W - 120 + s * 40, w = micro_width(keys[s], 1);
        gfx_fill(cv, x, 2, 38, 13, C_PANEL, 255);
        micro_text(cv, x + (38 - w) / 2, 6, keys[s], s == u->sort ? C_FOCUS : C_QUIET, 240, 1);
        if (s == u->sort) gfx_hline(cv, x + 2, x + 35, 14, C_FOCUS, 230);
    }
}

/* --- spectrum, drawn like a wifi analyser app --- */

// plot box: left, right, top, height. round glass pulls it in from the edges
#if RADAR_ROUND
#define SP_L 30
#define SP_R (GFX_W - 30)
#define SP_T 46
#define SP_H 120
#else
#define SP_L 22
#define SP_R (GFX_W - 6)
#define SP_T 24
#define SP_H (GFX_H - 68)
#endif
#define SP_PITCH ((SP_R - SP_L - 28) / 12.0f)                                         // 22 px a channel on 320 wide
static float spx(float ch) { return SP_L + 8.0f + (ch - 1.0f) * SP_PITCH; }            // channel -> x
static float spy(float dbm) { return SP_T + (-30.0f - dbm) / 65.0f * SP_H; }           // dBm -> y, -30 at the top

int spectrum_channel_at(int x)
{
    int c = (int)lroundf(((float)x - SP_L - 8.0f) / SP_PITCH) + 1;
    return c < 1 ? 1 : c > 13 ? 13 : c;
}

int spectrum_channel_x(int ch)
{
    return (int)lroundf(spx((float)ch));
}

// rough shape of a 20 MHz channel: flat top, falls off past ~1.4 channels each side
static float hump(float rssi, float d)
{
    float e = fabsf(d) - 1.4f;
    return e > 0 ? rssi - 40.0f * e * e : rssi;
}

void view_spectrum(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[64];
    const float base = spy(-95.0f);
    for (int db = -40; db >= -90; db -= 10) {
        int y = (int)lroundf(spy((float)db));
        gfx_hline(cv, SP_L, SP_R, y, C_QUIET, db % 20 == 0 ? 70 : 35);
        snprintf(buf, sizeof buf, "%d", db);
        if (RADAR_ROUND) micro_text(cv, SP_L + 2, y - 7, buf, C_QUIET, 220, 1);   // no room left of the plot
        else micro_text(cv, SP_L - 20, y - 2, buf, C_QUIET, 220, 1);
    }
    for (int ch = 1; ch <= 13; ch++) {
        int x = (int)lroundf(spx((float)ch));
        if (ch == u->chan_sel) gfx_fill(cv, x - (int)(SP_PITCH / 2) + 1, SP_T - 2, (int)SP_PITCH - 1, (int)base - SP_T + 2, C_FOCUS, 18);
        gfx_vline(cv, x, (int)base, (int)base + 3, C_QUIET, 200);
        snprintf(buf, sizeof buf, "%d", ch);
        micro_text(cv, x - micro_width(buf, 1) / 2, (int)base + 5, buf, ch == a->channel ? C_FOCUS : C_DIM, 230, 1);
    }

    int count = u->n < 16 ? u->n : 16;
    for (int j = count - 1; j >= 0; j--) {                // weakest first, so loud ones end up on top
        int i = u->order[j];
        const contact_t *c = &a->c[i];
        uint32_t col = i == u->target ? C_FOCUS : u->b[i].col;
        int hot = u->chan_sel < 0 || c->channel == u->chan_sel || i == u->target;
        int x0 = (int)spx((float)c->channel - 2.3f), x1 = (int)spx((float)c->channel + 2.3f);
        float px = 0, py = 0;
        for (int x = x0; x <= x1; x += 2) {
            float lvl = hump(c->rssi, ((float)x - spx((float)c->channel)) / SP_PITCH);
            if (lvl < -95.0f) lvl = -95.0f;
            float y = spy(lvl);
            if (x >= SP_L && x < SP_R) {
                gfx_vline(cv, x, (int)y, (int)base, col, hot ? 26 : 10);
                gfx_vline(cv, x + 1, (int)y, (int)base, col, hot ? 26 : 10);
            }
            if (x > x0 && px >= SP_L && x < SP_R) gfx_line(cv, px, py, (float)x, y, 1.2f, col, hot ? 220 : 70);
            px = (float)x; py = y;
        }
    }
    // names on the peaks, moved up if they overlap, give up after a few tries
    int lx[10], ly[10], lw[10], nl = 0;
    for (int j = 0; j < count && nl < 10; j++) {
        int i = u->order[j];
        const contact_t *c = &a->c[i];
        if (u->chan_sel >= 0 && c->channel != u->chan_sel && i != u->target) continue;
        int w = font_width(&FONT_SMALL, net_name(c));
        if (w > 64) w = 64;
        int x = (int)spx((float)c->channel) - w / 2, y = (int)spy(c->rssi) - 12;
        if (x < SP_L) x = SP_L;
        if (x + w > SP_R) x = SP_R - w;
        int ok = 0;
        for (int tries = 0; tries < 4 && !ok; tries++) {
            ok = y >= SP_T - 4;
            for (int q = 0; q < nl && ok; q++)
                if (x < lx[q] + lw[q] && lx[q] < x + w && y < ly[q] + 10 && ly[q] < y + 10) ok = 0;
            if (!ok) y -= 11;
        }
        if (!ok) continue;
        lx[nl] = x; ly[nl] = y; lw[nl] = w; nl++;
        font_text_clip(cv, &FONT_SMALL, x, y, net_name(c), i == u->target ? C_FOCUS : u->b[i].col, 255, 64);
    }

#if RADAR_ROUND
    if (u->chan_sel > 0) {                              // round: just the count, centred
        int n = 0, len = snprintf(buf, sizeof buf, "CH %d: ", u->chan_sel);
        for (int j = 0; j < u->n; j++) if (a->c[u->order[j]].channel == u->chan_sel) n++;
        snprintf(buf + len, sizeof buf - (size_t)len, "%d NET%s", n, n == 1 ? "" : "S");
    } else {
        snprintf(buf, sizeof buf, "TAP A CHANNEL");
    }
    micro_text(cv, GFX_W / 2 - micro_width(buf, 1) / 2, SP_T + SP_H + 22, buf, u->chan_sel > 0 ? C_FOCUS : C_DIM, 235, 1);
#else
    if (u->chan_sel > 0) {
        int n = 0, len = 0;
        len += snprintf(buf, sizeof buf, "CH %d:", u->chan_sel);
        for (int j = 0; j < u->n; j++) if (a->c[u->order[j]].channel == u->chan_sel) n++;
        snprintf(buf + len, sizeof buf - (size_t)len, " %d NETWORK%s", n, n == 1 ? "" : "S");
        micro_text(cv, 8, GFX_H - 26, buf, C_FOCUS, 240, 1);
        int x = 8;
        for (int j = 0; j < u->n && x < GFX_W - 40; j++) {
            const contact_t *c = &a->c[u->order[j]];
            if (c->channel != u->chan_sel) continue;
            x += font_text_clip(cv, &FONT_SMALL, x, GFX_H - 17, net_name(c), u->b[u->order[j]].col, 255, 70) + 10;
        }
    } else {
        micro_text(cv, 8, GFX_H - 22, RADAR_EPAPER ? "DIAL: PICK A CHANNEL TO SEE WHO SHARES IT" : "TAP A CHANNEL TO SEE WHO SHARES IT", C_DIM, 230, 1);
    }
#endif
    snprintf(buf, sizeof buf, "TUNED CH %02d", a->channel);
    view_header(cv, "SPECTRUM 2.4 GHZ", buf);
}

/* --- target: one network, everything big --- */

void view_target(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    const contact_t *c = &a->c[u->target];
    const blip_t *b = &u->b[u->target];
    char buf[64];

    const int mid = GFX_W / 2;                         // right half starts here (160 on 320 wide)
    font_text_clip(cv, &FONT_MEDIUM, 8, 21, net_name(c), C_FOCUS, 255, GFX_W - 20);
    snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X   CH %d   %s", c->bssid[0], c->bssid[1], c->bssid[2],
             c->bssid[3], c->bssid[4], c->bssid[5], c->channel, sec_name(c->sec));
    micro_text(cv, 8, 37, buf, sec_color(c->sec), 230, 1);

    snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
    int w = font_text(cv, &FONT_HUGE, 4, 48, buf, C_FOCUS, 255);
    micro_text(cv, 6 + w, 78, "DBM", C_DIM, 230, 1);

    float tr = app_trend(c);
    const char *word = tr > TREND_DB ? "WARMER" : tr < -TREND_DB ? "COLDER" : "STEADY";
    uint32_t wc = tr > TREND_DB ? C_FOCUS : C_DIM;
    if (tr > TREND_DB) gfx_tri(cv, (float)mid, 64, mid + 16.0f, 64, mid + 8.0f, 50, wc, 240);
    else if (tr < -TREND_DB) gfx_tri(cv, (float)mid, 52, mid + 16.0f, 52, mid + 8.0f, 66, wc, 240);
    else gfx_fill(cv, mid, 57, 16, 3, wc, 230);
    font_text(cv, &FONT_MEDIUM, mid + 22, 50, word, wc, 255);
    snprintf(buf, sizeof buf, "%+.1f DB / 10 S", (double)tr);
    micro_text(cv, mid + 22, 66, buf, C_DIM, 230, 1);
    float d = app_distance_m(c->rssi);
    if (d < 10.0f) snprintf(buf, sizeof buf, "~%.1f M", (double)d);
    else snprintf(buf, sizeof buf, "~%d M", (int)lroundf(d));
    font_text(cv, &FONT_MEDIUM, mid, 76, buf, C_FOCUS, 255);

    // direction dial, relative to where you face. "?" if we have no bearing
    const float cx = GFX_W - 28.0f, cy = 66, r = 18;
    gfx_ring(cv, cx, cy, r, 1.2f, C_QUIET, 220);
    gfx_tri(cv, cx - 3, cy - r - 5, cx + 3, cy - r - 5, cx, cy - r - 1, C_FOCUS, 230);
    if (b->src) {
        float th = b->ang * D2R;
        gfx_arc(cv, cx, cy, r - 3, b->ang - b->sd, b->ang + b->sd, 2.0f, C_PRIM, 140);
        gfx_line(cv, cx, cy, cx + (r - 2) * sinf(th), cy - (r - 2) * cosf(th), 2.0f, C_FOCUS, 240);
        snprintf(buf, sizeof buf, "%03d", (int)lroundf(b->ang) % 360);
    } else {
        snprintf(buf, sizeof buf, "?");
    }
    micro_text(cv, (int)cx - micro_width(buf, 1) / 2, (int)(cy + r + 4), buf, C_DIM, 230, 1);

    float f = clampf((c->rssi + 92.0f) / 60.0f, 0, 1);
    int segs = (GFX_W - 8) / 12;                       // 26 on 320 wide
    for (int s = 0; s < segs; s++) {
        int lit = (float)s < f * (float)segs;
        gfx_fill(cv, 8 + s * 12, 100, 10, 16, lit ? (s > segs * 3 / 4 ? C_FOCUS : C_PRIM) : C_QUIET, lit ? 220 : 45);
    }
    micro_text(cv, 8, 119, "FAR", C_DIM, 220, 1);
    micro_text(cv, GFX_W - 8 - micro_width("NEAR", 1), 119, "NEAR", C_DIM, 220, 1);
    draw_sparkline(cv, c, 8, 128, GFX_W - 16, GFX_H - 168, 1);

    // 4 buttons along the bottom, a bit more gap in the middle (tap() splits by GFX_W / 4)
    const int by = GFX_H - 34;
#if RADAR_EPAPER
    key_hints(cv, 4, by, GFX_W - 8, 30, "BACK = RADAR    MENU = NEXT SCREEN    DIAL = NEXT NETWORK", "HOLD MENU = 360 TURN");
#else
    const int bw = (GFX_W - 24) / 4;
    button(cv, 4, by, bw, 30, "360 TURN", 0);
    button(cv, 4 + (bw + 4), by, bw, 30, "WALK MAP", 0);
    button(cv, 4 + 2 * (bw + 4) + 4, by, bw, 30, "NEXT", 0);
    button(cv, 4 + 3 * (bw + 4) + 4, by, bw, 30, "CLEAR", 0);
#endif
    int rank = 0;
    for (int k = 0; k < u->n; k++) if (u->order[k] == u->target) rank = k + 1;
    snprintf(buf, sizeof buf, "%02d/%02d", rank, u->n);
    view_header(cv, "TARGET", buf);
}

/* --- walk map, world units are metres --- */

typedef struct { float mx, my, s; } mapfit_t;
#define MAP_CX (GFX_W / 2.0f)
#if RADAR_ROUND
#define MAP_CY (GFX_H / 2.0f)                // round: centre of the glass, no buttons
#define MAP_FIT 150.0f                       // the walk is fitted in a square this big
#else
#define MAP_CY ((GFX_H - 18) / 2.0f)        // middle of the band between header and buttons
#endif
#define MAP_BOT (GFX_H - 38)                 // grid stops here, buttons below

static void to_screen(const mapfit_t *f, float x, float y, float *sx, float *sy)
{
    *sx = MAP_CX + (x - f->mx) * f->s;
    *sy = MAP_CY - (y - f->my) * f->s;
}

/* fit the walk and the networks on screen. unsolved networks and ones further then
 * 14 m from the walk centre are left out, so one far AP won't shrink the whole map.
 * scale is clamped to 4..60 px per m */
static mapfit_t map_fit(const mapview_t *mv)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, cx = 0, cy = 0;
    for (int t = 0; t < mv->T; t++) { cx += mv->px[t]; cy += mv->py[t]; }
    if (mv->T) { cx /= mv->T; cy /= mv->T; }
    for (int i = 0; i < mv->T + mv->n; i++) {
        float x = i < mv->T ? mv->px[i] : mv->ax[i - mv->T];
        float y = i < mv->T ? mv->py[i] : mv->ay[i - mv->T];
        if (i >= mv->T && (!mv->ok[i - mv->T] || hypotf(x - cx, y - cy) > 14.0f)) continue;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    mapfit_t f = { 0, 0, 20 };
    if (x1 < x0) return f;
    f.mx = 0.5f * (x0 + x1);
    f.my = 0.5f * (y0 + y1);
#if RADAR_ROUND
    float sx = MAP_FIT / fmaxf(x1 - x0, 2.0f), sy = MAP_FIT / fmaxf(y1 - y0, 2.0f);
#else
    float sx = (GFX_W - 44.0f) / fmaxf(x1 - x0, 2.0f), sy = (GFX_H - 90.0f) / fmaxf(y1 - y0, 2.0f);
#endif
    f.s = clampf(fminf(sx, sy), 4.0f, 60.0f);
    return f;
}

void view_map(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    const mapview_t *mv = &a->map;
    char buf[48];
    mapfit_t f = map_fit(mv);

    // metre grid, step depends on zoom
    float step = f.s >= 14.0f ? 1.0f : f.s >= 7.0f ? 2.0f : 5.0f;
    float wx0 = f.mx - (MAP_CX - 4) / f.s, wx1 = f.mx + (GFX_W - MAP_CX) / f.s;
    float wy0 = f.my - (MAP_BOT - MAP_CY) / f.s, wy1 = f.my + (MAP_CY - 18) / f.s;
    for (float gx = floorf(wx0 / step) * step; gx <= wx1; gx += step) {
        float sx, sy;
        to_screen(&f, gx, 0, &sx, &sy);
        int major = fabsf(remainderf(gx, 5.0f)) < 0.01f;
        gfx_vline(cv, (int)lroundf(sx), 18, MAP_BOT, C_QUIET, major ? 70 : 35);
    }
    for (float gy = floorf(wy0 / step) * step; gy <= wy1; gy += step) {
        float sx, sy;
        to_screen(&f, 0, gy, &sx, &sy);
        int major = fabsf(remainderf(gy, 5.0f)) < 0.01f;
        gfx_hline(cv, 0, GFX_W - 1, (int)lroundf(sy), C_QUIET, major ? 70 : 35);
    }

    /* walk path, older part fainter. averaged over 5 s (t-2..t+2) because the
     * solved path jitters about a metre, the person doesnt */
    float px = 0, py = 0;
    for (int t = 0; t < mv->T; t++) {
        float ax = 0, ay = 0;
        int n = 0;
        for (int q = t - 2; q <= t + 2; q++) {
            if (q < 0 || q >= mv->T) continue;
            ax += mv->px[q]; ay += mv->py[q]; n++;
        }
        if (t == mv->T - 1) { ax = mv->px[t] * n; ay = mv->py[t] * n; }   // last point not averaged, it's where you are now
        float sx, sy;
        to_screen(&f, ax / n, ay / n, &sx, &sy);
        if (t) gfx_line(cv, px, py, sx, sy, 1.4f, C_PRIM, 60 + 170 * t / (mv->T > 1 ? mv->T - 1 : 1));
        px = sx; py = sy;
    }

    // networks
    int lx[MAP_N], ly[MAP_N], nl = 0;
    for (int k = 0; k < mv->n; k++) {
        const contact_t *c = app_find(a, mv->id[k]);
        uint32_t col = c ? sec_color(c->sec) : C_DIM;
        if (c && c->id == a->target_id) col = C_FOCUS;
        float sx, sy;
        to_screen(&f, mv->ax[k], mv->ay[k], &sx, &sy);
#if RADAR_ROUND
        int off = hypotf(sx - MAP_CX, sy - MAP_CY) > 96.0f;
#else
        int off = sx < 6 || sx > GFX_W - 6 || sy < 22 || sy > MAP_BOT - 4;
#endif
        if (off) {                              // off screen: arrow on the edge pointing to it
            float dx = sx - MAP_CX, dy = sy - MAP_CY, l = sqrtf(dx * dx + dy * dy);
#if RADAR_ROUND
            float k1 = 96.0f / l;
#else
            float k1 = fminf((MAP_CX - 10) / fmaxf(fabsf(dx), 1e-3f), (MAP_CY - 26) / fmaxf(fabsf(dy), 1e-3f));
#endif
            sx = MAP_CX + dx * k1; sy = MAP_CY + dy * k1;
            float ux = dx / l, uy = dy / l;
            gfx_tri(cv, sx + ux * 6, sy + uy * 6, sx - uy * 4, sy + ux * 4, sx + uy * 4, sy - ux * 4, col, 200);
        } else if (mv->ok[k]) {
            float rr = mv->sd[k] * f.s;
            if (rr < 70.0f) gfx_ring(cv, sx, sy, rr, 1.0f, col, 60);
            gfx_glow(cv, sx, sy, 9.0f, col, 160);
            gfx_disc(cv, sx, sy, 2.4f, gfx_mix(col, C_FOCUS, 140), 255);
        } else {
            gfx_ring(cv, sx, sy, 3.4f, 1.0f, col, 200);
        }
        if (!c) continue;
        int x = (int)sx + 7, y = (int)sy - 9, w = 64;
        if (x + w > GFX_W - 4) x = (int)sx - 7 - w;
        int clash = 0;
        for (int q = 0; q < nl; q++) if (x < lx[q] + w && lx[q] < x + w && y < ly[q] + 17 && ly[q] < y + 17) clash = 1;
        if (clash || y < 20 || y > MAP_BOT - 16) continue;
        if (RADAR_ROUND && (hypotf(x - MAP_CX, y - MAP_CY) > 100.0f || hypotf(x + w - MAP_CX, y + 17 - MAP_CY) > 100.0f)) continue;
        lx[nl] = x; ly[nl] = y; nl++;
        font_text_clip(cv, &FONT_SMALL, x, y, net_name(c), col, 255, 64);
        float d = hypotf(mv->ax[k] - mv->px[mv->T - 1], mv->ay[k] - mv->py[mv->T - 1]);
        snprintf(buf, sizeof buf, "%s~%d M", mv->ok[k] ? "" : "? ", (int)lroundf(d));
        micro_text(cv, x, y + 10, buf, C_DIM, 220, 1);
    }

    // you: arrow if heading is known, else a dot
    if (mv->T) {
        float sx, sy;
        to_screen(&f, mv->px[mv->T - 1], mv->py[mv->T - 1], &sx, &sy);
        gfx_glow(cv, sx, sy, 12.0f, C_FOCUS, 120);
        if (mv->heading_ok) {
            float h = mv->heading * D2R, s = sinf(h), c = cosf(h);
            gfx_tri(cv, sx + 8 * s, sy - 8 * c, sx - 5 * c - 4 * s, sy - 5 * s + 4 * c,
                    sx + 5 * c - 4 * s, sy + 5 * s + 4 * c, C_FOCUS, 255);
        } else {
            gfx_disc(cv, sx, sy, 3.5f, C_FOCUS, 255);
        }
    }

    // scale bar + notes
#if RADAR_ROUND
    {
        int len = (int)(step * f.s), x0 = GFX_W / 2 - len / 2, y = GFX_H - 36;
        gfx_hline(cv, x0, x0 + len, y, C_FOCUS, 230);
        gfx_vline(cv, x0, y - 3, y, C_FOCUS, 230);
        gfx_vline(cv, x0 + len, y - 3, y, C_FOCUS, 230);
        snprintf(buf, sizeof buf, "%d M", (int)step);
        micro_text(cv, GFX_W / 2 - micro_width(buf, 1) / 2, y + 4, buf, C_FOCUS, 230, 1);
    }
#else
    gfx_hline(cv, 8, 8 + (int)(step * f.s), MAP_BOT - 4, C_FOCUS, 230);
    gfx_vline(cv, 8, MAP_BOT - 7, MAP_BOT - 4, C_FOCUS, 230);
    gfx_vline(cv, 8 + (int)(step * f.s), MAP_BOT - 7, MAP_BOT - 4, C_FOCUS, 230);
    snprintf(buf, sizeof buf, "%d M", (int)step);
    micro_text(cv, 12 + (int)(step * f.s), MAP_BOT - 8, buf, C_FOCUS, 230, 1);
    const char *up = mv->aligned ? "UP = WHERE YOU FACED AT THE TURN" : "UP = YOUR FIRST STEPS   L/R ?";
    micro_text(cv, GFX_W - 6 - micro_width(up, 1), MAP_BOT - 8, up, C_DIM, 220, 1);
#endif

    if (!mv->active) {
        const int bw = RADAR_ROUND ? 200 : 220, bx = GFX_W / 2 - bw / 2, by = GFX_H / 2 - 50;
        chamfer_box(cv, bx, by, bw, 74, C_BG0, 210, C_FOCUS, 220);
        micro_text(cv, bx + 12, by + 10, "WALK SLOWLY THROUGH", C_FOCUS, 245, 2);
        micro_text(cv, bx + 12, by + 26, "EVERY ROOM", C_FOCUS, 245, 2);
        snprintf(buf, sizeof buf, "THE MAP NEEDS 3 M OF WALK: %.1f", (double)mv->spread);
        micro_text(cv, bx + 12, by + 48, buf, C_DIM, 230, 1);
        micro_text(cv, bx + 12, by + 58, "A 360 TURN THEN FIXES LEFT/RIGHT", C_DIM, 230, 1);
    }

#if RADAR_EPAPER
    key_hints(cv, 4, GFX_H - 34, GFX_W - 8, 30, "BACK = RADAR    MENU = NEXT SCREEN    DIAL OK = NEW WALK", "HOLD MENU = 360 TURN");
#elif !RADAR_ROUND
    const int bw = (GFX_W - 20) / 3;
    button(cv, 4, GFX_H - 34, bw, 30, "NEW WALK", 0);
    button(cv, 4 + (bw + 6), GFX_H - 34, bw, 30, "360 TURN", 0);
    button(cv, 4 + 2 * (bw + 6), GFX_H - 34, bw, 30, "BACK", 0);
#endif
    snprintf(buf, sizeof buf, "%02u:%02u  %d NETS", (unsigned)(mv->seconds / 60u), (unsigned)(mv->seconds % 60u), mv->n);
    view_header(cv, "WALK MAP", buf);
}
```

# WiFi Radar core/ui_round.c: the same views for a circle

```c
/* round glass (GC9A01 240x240): the main view is the radar itself, target and list
 * are laid out for a circle. nothing may sit in the corners, they are not there.
 * spectrum and walk map are the normal views with round margins (ui_views.c) */
#include "ui_int.h"

#if RADAR_ROUND

#define CX   (GFX_W / 2)
#define CY   (GFX_H / 2)
#define SAFE (GFX_W / 2 - 6)        // the bezel eats a few px, ViewOwl keeps to 112 on 240

// half width of the glass at row y, minus a margin
static int chord(int y, int margin)
{
    int dy = y - CY;
    int h2 = SAFE * SAFE - dy * dy;
    return h2 > 0 ? (int)sqrtf((float)h2) - margin : 0;
}

static void centre_micro(canvas_t *cv, int y, const char *s, uint32_t col, int a)
{
    micro_text(cv, CX - micro_width(s, 1) / 2, y, s, col, a, 1);
}

// little box under the radar centre, one or two lines
static void note_box(canvas_t *cv, const char *big, const char *small, uint32_t col)
{
    int wb = big ? font_width(&FONT_SMALL, big) : 0, ws = small ? micro_width(small, 1) : 0;
    int w = (wb > ws ? wb : ws) + 12, y = CY + 50, h = big && small ? 24 : 15;
    if (w > 2 * chord(y + h, 4)) w = 2 * chord(y + h, 4);
    gfx_fill(cv, CX - w / 2, y, w, h, C_BG0, 170);
    gfx_hline(cv, CX - w / 2, CX + w / 2 - 1, y, col, 150);
    if (big) font_text_clip(cv, &FONT_SMALL, CX - wb / 2, y + 2, big, col, 255, w - 8);
    if (small) centre_micro(cv, big ? y + 15 : y + 5, small, C_DIM, 230);
}

void round_main(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char big[48], small[40];
    radar_draw(u, cv, &G_MAIN, 1);
    ui_lap(u, 2);
    if (!gfx_touches(cv, CY + 48, CY + 76)) return;
    if (a->sweep == SWEEP_COUNTDOWN) {
        note_box(cv, "FACE FORWARD", "THEN TURN RIGHT WITH THE BEAM", C_FOCUS);
    } else if (a->sweep == SWEEP_RUN) {
        uint32_t el = u->now - a->sweep_t0;
        snprintf(small, sizeof small, "KEEP TURNING  %u S", (unsigned)((SWEEP_RUN_MS - el + 999u) / 1000u));
        note_box(cv, 0, small, C_FOCUS);
    } else if (a->sweep == SWEEP_RESULT) {
        snprintf(big, sizeof big, "%d/%d FOUND", a->sweep_resolved, a->sweep_total);
        note_box(cv, big, "UP = WHERE YOU FACED", C_FOCUS);
    } else if (u->target >= 0) {
        const contact_t *c = &a->c[u->target];
        snprintf(big, sizeof big, "%s", net_name(c));
        snprintf(small, sizeof small, "%d DBM  ~%d M", (int)lroundf(c->rssi), (int)lroundf(app_distance_m(c->rssi)));
        note_box(cv, big, small, C_FOCUS);
    } else if (a->n) {
        snprintf(small, sizeof small, "%d NETS  CH %02d", a->n, a->channel);
        note_box(cv, 0, small, C_PRIM);
    }
}

/* the hunt, for a circle: closeness is the arc around the edge, the number in the middle */
void round_target(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    const contact_t *c = &a->c[u->target];
    const blip_t *b = &u->b[u->target];
    char buf[48];

    // proximity: 26 segments on a 260 degree arc, open at the bottom
    float f = clampf((c->rssi + 92.0f) / 60.0f, 0, 1);
    for (int s = 0; s < 26; s++) {
        float a0 = -130.0f + s * 10.0f;
        int lit = (float)s < f * 26.0f;
        gfx_arc(cv, (float)CX, (float)CY, SAFE - 4.0f, a0 + 1.5f, a0 + 8.5f, 6.0f,
                lit ? (s > 19 ? C_FOCUS : C_PRIM) : C_QUIET, lit ? 230 : 60);
    }
    micro_text(cv, CX - 76, CY + 78, "FAR", C_DIM, 220, 1);
    micro_text(cv, CX + 76 - micro_width("NEAR", 1), CY + 78, "NEAR", C_DIM, 220, 1);

    int w = font_width(&FONT_MEDIUM, net_name(c));
    if (w > 150) w = 150;
    font_text_clip(cv, &FONT_MEDIUM, CX - w / 2, 26, net_name(c), C_FOCUS, 255, 150);
    snprintf(buf, sizeof buf, "CH %d  %s", c->channel, sec_name(c->sec));
    centre_micro(cv, 42, buf, sec_color(c->sec), 230);

    snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
    w = font_width(&FONT_HUGE, buf);
    font_text(cv, &FONT_HUGE, CX - w / 2 - 8, 50, buf, C_FOCUS, 255);
    micro_text(cv, CX + w / 2 - 4, 80, "DBM", C_DIM, 230, 1);

    float tr = app_trend(c);
    const char *word = tr > TREND_DB ? "WARMER" : tr < -TREND_DB ? "COLDER" : "STEADY";
    uint32_t wc = tr > TREND_DB ? C_FOCUS : C_DIM;
    w = font_width(&FONT_MEDIUM, word) + 20;
    int x = CX - w / 2;
    if (tr > TREND_DB) gfx_tri(cv, (float)x, 112, x + 14.0f, 112, x + 7.0f, 100, wc, 240);
    else if (tr < -TREND_DB) gfx_tri(cv, (float)x, 100, x + 14.0f, 100, x + 7.0f, 112, wc, 240);
    else gfx_fill(cv, x, 105, 14, 3, wc, 230);
    font_text(cv, &FONT_MEDIUM, x + 20, 98, word, wc, 255);

    float d = app_distance_m(c->rssi);
    if (d < 10.0f) snprintf(buf, sizeof buf, "~%.1f M", (double)d);
    else snprintf(buf, sizeof buf, "~%d M", (int)lroundf(d));
    if (b->src) {
        int n = (int)strlen(buf);
        snprintf(buf + n, sizeof buf - (size_t)n, "  BRG %03d", (int)lroundf(b->ang) % 360);
    }
    centre_micro(cv, 118, buf, C_FOCUS, 240);

    draw_sparkline(cv, c, CX - 70, 132, 140, 34, 0);

    // which way: small dial at the bottom, "?" when no bearing yet
    const float dx = (float)CX, dy = CY + 64.0f, r = 11.0f;
    gfx_ring(cv, dx, dy, r, 1.2f, C_QUIET, 220);
    if (b->src) {
        float th = b->ang * D2R;
        gfx_line(cv, dx, dy, dx + (r - 2) * sinf(th), dy - (r - 2) * cosf(th), 2.0f, C_FOCUS, 240);
    } else {
        micro_text(cv, CX - 1, (int)dy - 2, "?", C_DIM, 230, 1);
    }
}

/* list: same rows as the square screen, each one as wide as the glass is at that height */
void round_list(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[16];
    for (int k = 0; k < a->n; k++) {
        int ry = LIST_Y0 + k * ROW_H - u->scroll;
        if (ry >= LIST_Y1) break;
        if (ry + ROW_H <= LIST_Y0 || !gfx_touches(cv, ry, ry + ROW_H)) continue;
        int i = u->list[k];
        const contact_t *c = &a->c[i];
        const blip_t *b = &u->b[i];
        // the narrower end of the row decides how wide it can be
        int h1 = chord(ry, 4), h2 = chord(ry + ROW_H - 1, 4), half = h1 < h2 ? h1 : h2;
        int x0 = CX - half, x1 = CX + half;
        if (i == u->target) gfx_fill(cv, x0, ry, x1 - x0, ROW_H - 1, C_FOCUS, 30);
        gfx_fill(cv, x0, ry + 3, 2, ROW_H - 7, b->col, 230);
        snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
        int dw = font_width(&FONT_MEDIUM, buf);
        font_text(cv, &FONT_MEDIUM, x1 - dw, ry + 3, buf, C_FOCUS, 255);
        font_text_clip(cv, &FONT_MEDIUM, x0 + 6, ry + 3, net_name(c), gfx_mix(b->col, C_FOCUS, 120), 255, x1 - x0 - dw - 12);
        gfx_hline(cv, x0, x1, ry + ROW_H - 1, C_QUIET, 45);
    }
    snprintf(buf, sizeof buf, "%d", a->n);
    view_header(cv, "NETWORKS", buf);
    if (a->n * ROW_H > LIST_Y1 - LIST_Y0) {          // more below or above: small arrows
        if (u->scroll > 0) arrow(cv, (float)CX, LIST_Y0 - 3.0f, 1, C_DIM);
        if (u->scroll < a->n * ROW_H - (LIST_Y1 - LIST_Y0)) arrow(cv, (float)CX, LIST_Y1 + 5.0f, 0, C_DIM);
    }
}

#endif
```

