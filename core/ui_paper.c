/* ui_paper - the e-paper boards' own main screen (config.h RADAR_EPAPER): the CLASSIC look made as a
 * printed chart, not the colour radar printed in greys.
 *
 * what the glass allows, and so the rules here (the WiFi radar's ui_paper.c found them first):
 *  - 4 greys (the 4.2"'s waveform puts its two near the middle, epd.c LUT_4GRAY). a tone in between
 *    is a pattern of two of them, as engraved maps and newspapers made one
 *  - what changes is drawn on top with a partial refresh, which knows black and white only (epd.c).
 *    so grey only where nothing changes till the next full refresh: the sheet, the map. the planes,
 *    their trails and labels, the cards and the key tabs are black on white
 *  - no light of its own: big bold letters (DejaVu Sans Bold, font_paper_*.c), no soft edges
 *  - the sheet is printed with a full refresh when it is new: another range, the map made again
 *    (paper_new_sheet). between, only what moves is drawn on top
 *
 * so two layers: the sheet, the map's kinds (map.h) in white, greys and patterns, and over it what
 * moves, black on white. the keys (firmware/main/keys.c) have a strip at the edge they are on, a third
 * each, which says what each one does now: a key that does nothing here says nothing. a press prints its
 * third black first, then the screen is printed whole.
 *
 * drawn in the paper's inks (paper.h P_*), a channel each, which paper.c reads whole: an edge rounds
 * to one of the two inks it lies between, so gfx's soft lines and font's soft letters come out crisp */
#include "ui_int.h"
#include "paper.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#if RADAR_EPAPER

/* where the keys are, along which edge: on the right of the screen as drawn, from the top a key, the dial,
 * a key (the 4.2" in hand; the wiki and the pinout do not say). NOTE: which key is MENU and which EXIT
 * is a guess, MENU the top one */
#define KEYS_RIGHT 1
#define KS_W       PAPER_KEYS_W                        /* the keys' strip, 20-30 px in from the edge as asked */

#define LAB_H      24                                  /* a plane's label: its name and its height */
#define LABELS     8                                   /* labels at most: the picked one, then the nearest */
#define LOST_MS    12000                               /* a position older than this is a guess (ui_scope.c's) */
#define LOST_FR24_MS (LOST_MS + 20000)

typedef struct { int x, y, w, h; } pbox_t;

static pbox_t s_lab[SKY_MAX];                          /* each plane's label, w 0: none */
static uint32_t s_screen = UINT32_MAX;                 /* the screen on the glass: its view, look and page */
static uint32_t s_map_gen;                             /* and its map: 0 none, else the map's gen + 1 */

static int overlaps(pbox_t a, pbox_t b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

int paper_shown(const ui_t *u) { return (u->view == VIEW_MAIN && u->cfg.look == LOOK_CLASSIC) || weather_paper_page(u); }

static const char *plane_name(const plane_t *p) { return p->flight[0] ? p->flight : p->reg[0] ? p->reg : "------"; }
static const char *home_name(const ui_t *u) { return u->net.demo ? "DEMO SKY" : u->cfg.place[0] ? u->cfg.place : "HOME"; }
static int emergency(const plane_t *p) { return p->squawk == 7500 || p->squawk == 7600 || p->squawk == 7700; }

/* the map, when it is this look's, this range's and this home's. a range just turned on the dial keeps
 * the old one till the new one is made: printed stretched it would be a wrong map for a picture or two */
static const map_t *sheet_map(const ui_t *u)
{
    const map_t *m = u->map;
    if (!m || !m->ready || !m->tiles || !u->cfg.map) return NULL;
    int w, h, disc;
    ui_ground_size(&u->cfg, &w, &h, &disc);
    if (m->lat != u->cfg.lat || m->lon != u->cfg.lon || m->w != w || m->h != h) return NULL;
    const float k = ui_ground_kpx(&u->cfg);
    return fabsf(m->kpx - k) <= k * 0.01f ? m : NULL;
}

/* --- the boxes: worked out once a frame, the strips draw them --- */

static pbox_t home_card(const ui_t *u)
{
    int w = font_width(&FONT_PAPER_M, home_name(u)) + 12;
    if (w < 110) w = 110;
    if (w > GFX_W / 2) w = GFX_W / 2;
    return (pbox_t){ 4, 4, w, 36 };
}

#define PICK_W 196
#define PICK_H 62
static pbox_t pick_card(void) { return (pbox_t){ 4, GFX_H - 20 - PICK_H, PICK_W, PICK_H }; }

static pbox_t osm_chip(void)
{
    const int w = font_width(&FONT_PAPER_S, "\xC2\xA9 OpenStreetMap") + 8;
    return (pbox_t){ 4, GFX_H - 17, w, 14 };
}

/* the keys' strip at the edge they are on: light grey, a third of the height for each, from the top the key,
 * the dial, the key ("отступить от края... разделить на три равных области... буквы повернуть на 90"). in
 * each, turned to read downwards, what pressing it does now; the dial's three, each with its sign. a key
 * that does nothing here says nothing. a key pressed prints its third black till the press is taken
 * (main.c: the next picture shows it, then the key goes in and the screen is printed whole) */
enum { TAB_MENU, TAB_EXIT, TAB_UP, TAB_OK, TAB_DOWN, TABS };
enum { ZONE_MENU, ZONE_DIAL, ZONE_EXIT, ZONES };
static const uint8_t TAB_KEY[TABS] = { KEY_MENU, KEY_BACK, KEY_UP, KEY_OK, KEY_DOWN };

static pbox_t zone_box(int z)
{
    const int y0 = z * GFX_H / ZONES + (z ? 1 : 0), y1 = (z + 1) * GFX_H / ZONES - (z < ZONES - 1 ? 1 : 0);
    return (pbox_t){ KEYS_RIGHT ? GFX_W - KS_W : 0, y0, KS_W, y1 - y0 };
}

static const char *tab_words(const ui_t *u, int t, const char **held)
{
    const int picked = sel_plane(u) != NULL;
    *held = NULL;
    if (u->cfg.look == LOOK_WEATHER && t != TAB_MENU) {   // the weather: the dial turns its pages, nothing to let go of
        if (t == TAB_UP) return WX_PAGE_NAME[(u->wx_page + WX_PAGES - 1) % WX_PAGES];
        return t == TAB_DOWN ? WX_PAGE_NAME[(u->wx_page + 1) % WX_PAGES] : NULL;
    }
    switch (t) {
    case TAB_MENU:
        *held = "HOLD: ABOUT";
        return LOOK_NAME[(u->cfg.look + 1) % LOOKS];
    case TAB_EXIT:
        return picked ? "LET GO" : NULL;
    case TAB_UP:
        return range_can_step(u, -1) ? "NEARER" : NULL;
    case TAB_OK:
        *held = picked ? "HOLD: WATCH" : NULL;
        return picked ? "NEXT" : "PLANE";
    default:
        return range_can_step(u, 1) ? "FURTHER" : NULL;
    }
}

// was this third's key the one pressed, its press not yet taken
static int zone_pressed(const ui_t *u, int z)
{
    if (!u->key_at) return 0;
    const int k = u->key_last;
    if (z == ZONE_MENU) return k == KEY_MENU || k == KEY_MENU_LONG;
    if (z == ZONE_EXIT) return k == KEY_BACK;
    return k == KEY_UP || k == KEY_DOWN || k == KEY_OK || k == KEY_OK_LONG;
}

static pbox_t ring_label(const ui_t *u, int ring, const char *t)
{
    const float r = ring ? (float)PR : PR * 0.5f, a = 45.0f * D2R;
    const int w = font_width(&FONT_PAPER_S, t) + 6;
    const int cx = GFX_W / 2 + (int)lroundf(r * sinf(a)), cy = GFX_H / 2 - (int)lroundf(r * cosf(a));
    (void)u;
    return (pbox_t){ cx - w / 2, cy - 7, w, 14 };
}

static void ring_text(const ui_t *u, int ring, char *buf, int sz)
{
    const float km = range_km(&u->cfg) * (ring ? 1.0f : 0.5f);
    if (ring) fmt_dist(u, km, buf, sz);
    else snprintf(buf, (size_t)sz, "%g", (double)(km / unit_km(u->cfg.dist_unit)));
}

static int lost(const ui_t *u, const plane_t *p)
{
    return !p->ground && (int32_t)(u->now - p->seen) > (u->snap.fr24 ? LOST_FR24_MS : LOST_MS);
}

/* --- once a frame --- */

/* a new screen is printed whole, its greys too (paper_new_sheet): another look, page or weather page,
 * or the map under it made again. what changes on a screen is drawn on top, black and white only:
 * a look's greys drawn so were missing till the panel's next full refresh, minutes later (epd.c).
 * a new look has its own map made: the screen waits for it (drawn on top meanwhile) and is printed
 * once with it, not twice, or after MAP_WAIT_MS without one: no WiFi and no tiles kept, none comes.
 * the weather is the same with its forecast for the map: printed when one comes and when an hour turns */
#define MAP_WAIT_MS 8000u
static uint32_t s_wait_at;                             /* when the screen changed, its print waiting for its map. 0 none */

static void screen_news(const ui_t *u)
{
    const uint32_t screen = (uint32_t)paper_style << 24 | (uint32_t)u->view << 16 | (uint32_t)u->cfg.look << 8 |
                            (u->cfg.look == LOOK_WEATHER ? u->wx_page : 0u);   // the sheet's tones changed: printed anew too
    const int wx = u->view == VIEW_MAIN && u->cfg.look == LOOK_WEATHER;
    const int wants = wx || (u->cfg.map && u->view == VIEW_MAIN);
    const map_t *m = wants && !wx ? sheet_map(u) : NULL;   // one made for a screen that does not show it is no news
    const uint32_t gen = wx ? weather_stamp(u) : m ? m->gen + 1u : 0u;
    if (screen != s_screen) {
        if (!s_wait_at) s_wait_at = u->now ? u->now : 1;
        if (!wants || gen || u->now - s_wait_at >= MAP_WAIT_MS) {
            paper_new_sheet = 1;
            s_screen = screen;
            s_map_gen = gen;
            s_wait_at = 0;
        }
    } else if (gen && gen != s_map_gen) {              // the same screen, its map made again (another range), a new forecast
        paper_new_sheet = 1;
        s_map_gen = gen;
    }
}

void paper_frame(ui_t *u)
{
    const int shown = paper_shown(u);
    paper_inks = shown;
    screen_news(u);
    if (!shown || u->cfg.look == LOOK_WEATHER) return;  // the weather has no labels to place

    // the labels: the picked one first, then the emergencies, then the nearest, each where it covers nothing
    static pbox_t used[SKY_MAX * 2 + 16];
    int nu = 0;
    used[nu++] = home_card(u);
    used[nu++] = osm_chip();
    if (sel_plane(u)) used[nu++] = pick_card();
    used[nu++] = (pbox_t){ KEYS_RIGHT ? GFX_W - KS_W - 3 : 0, 0, KS_W + 3, GFX_H };   // the keys' strip
    for (int r = 0; r < 2; r++) {
        char b[16];
        ring_text(u, r, b, sizeof b);
        used[nu++] = ring_label(u, r, b);
    }
    used[nu++] = (pbox_t){ GFX_W / 2 - 8, GFX_H / 2 - 8, 16, 16 };   // home
    const sky_t *s = &u->snap;
    for (int i = 0; i < s->n; i++) {
        s_lab[i].w = 0;
        const spot_t *sp = &u->spot[i];
        if (sp->vis == 1) used[nu++] = (pbox_t){ sp->x - 7, sp->y - 7, 14, 14 };
    }
    int order[SKY_MAX + 1], no = 0, sel = -1;
    for (int i = 0; i < s->n; i++) if (s->p[i].icao == u->sel) sel = i;
    if (sel >= 0) order[no++] = sel;
    for (int pass = 0; pass < 2; pass++) {              // emergencies first, then the rest, nearest first
        for (int k = 0; k < u->n_order; k++) {
            const int i = u->order[k];
            if (i != sel && emergency(&s->p[i]) == !pass) order[no++] = i;
        }
    }
    int placed = 0;
    for (int j = 0; j < no && placed < LABELS; j++) {
        const int i = order[j];
        const plane_t *p = &s->p[i];
        const spot_t *sp = &u->spot[i];
        if (sp->vis != 1) continue;
        char alt[16];
        fmt_alt(u, p, alt, sizeof alt, 1);
        int w = font_width(&FONT_PAPER_S, plane_name(p)), wa = font_width(&FONT_PAPER_S, alt);
        w = (w > wa ? w : wa) + 7;
        const int cand[6][2] = { { 10, -LAB_H / 2 }, { -10 - w, -LAB_H / 2 }, { 6, -LAB_H - 6 }, { 6, 6 },
                                 { -6 - w, -LAB_H - 6 }, { -6 - w, 6 } };
        for (int c = 0; c < 6; c++) {
            pbox_t b = { sp->x + cand[c][0], sp->y + cand[c][1], w, LAB_H };
            if (b.x < 2 || b.y < 2 || b.x + b.w > GFX_W - 2 || b.y + b.h > GFX_H - 2) continue;
            int clash = 0;
            for (int q = 0; q < nu && !clash; q++) {
                const pbox_t *o = &used[q];
                if (o->x == sp->x - 7 && o->y == sp->y - 7 && o->w == 14) continue;   // its own plane may touch
                clash = overlaps(b, *o);
            }
            if (clash) continue;
            s_lab[i] = b;
            if (nu < (int)(sizeof used / sizeof used[0])) used[nu++] = b;
            placed++;
            break;
        }
    }
}

/* --- the sheet: the map's kinds in the paper's inks and patterns --- */

/* the tones a pattern of two inks makes, from white down (the 4.2"'s greys, 0..255 as the glass shows):
 * white 255, TINT white and light 220, LIGHT 185, MID light and dark 140, DARK 95, DEEP dark and black
 * 47, BLACK 0. a chequer a pixel fine reads as its mean, and it takes a partial refresh well: what
 * passed over MID leaves white and black, the same tone. WOOD is MID with a black tree every 4 x 4,
 * TOWN the hatch on the light grey */
enum { S_WHITE, S_BLACK, S_DARK, S_LIGHT, S_HALF, S_HATCH, S_DASH, S_TINT, S_MID, S_DEEP, S_WOOD, S_TOWN };

int paper_style = 1;

/* what each kind of line is printed as. the hierarchy of a printed chart: the motorways, the runways
 * and the names black, the main roads dark grey, the small ones dotted and only near (further out
 * they are a net over the country), rails and borders dashed. the fields are white paper. the
 * areas, water, towns and woods, go by their neighbours (area_kind) */
static void sheet_kinds(const ui_t *u, uint8_t *kind)
{
    for (int i = 0; i < 16; i++) kind[i] = S_WHITE;
    kind[M_ROAD] = range_km(&u->cfg) <= 25.0f ? S_HALF : paper_style ? S_LIGHT : S_WHITE;   // far out a faint net
    kind[M_MAJOR] = paper_style ? S_DEEP : S_DARK;                    // darker than the woods it runs through
    kind[M_MOTOR] = S_BLACK;
    kind[M_RAIL] = S_DASH;
    kind[M_BORDER] = S_DASH;
    kind[M_TEXT] = S_BLACK;
    kind[M_SOFT] = S_BLACK;
    kind[M_AERO] = S_BLACK;
}

static inline uint32_t ink_at(int kind, int x, int y)
{
    switch (kind) {
    case S_BLACK: return P_BLACK;
    case S_DARK:  return P_DARK;
    case S_LIGHT: return P_LIGHT;
    case S_HALF:  return (x + y) & 1 ? P_WHITE : P_BLACK;                 // a chequer: a mid grey from afar
    case S_HATCH: return ((x - y) & 3) == 0 ? P_BLACK : P_WHITE;          // lines down to the right, a quarter of ink
    case S_DASH:  return ((x + y) >> 2) & 1 ? P_WHITE : P_BLACK;          // 4 on, 4 off
    case S_TINT:  return (x + y) & 1 ? P_WHITE : P_LIGHT;
    case S_MID:   return (x + y) & 1 ? P_LIGHT : P_DARK;
    case S_DEEP:  return (x + y) & 1 ? P_DARK : P_BLACK;
    case S_WOOD:  return (y & 3) == 0 && (x & 3) == (y & 4) >> 1 ? P_BLACK : (x + y) & 1 ? P_LIGHT : P_DARK;   // a row of trees in 4, every other one 2 along
    case S_TOWN:  return ((x - y) & 3) == 0 ? P_BLACK : P_LIGHT;
    default:      return P_WHITE;
    }
}

// how many of the eight texels about one are of these kinds (1 << kind). off the sheet counts: a lake has no bank at its edge
static int around(const map_t *m, int x, int y, uint32_t kinds)
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            const int u = x + dx, v = y + dy;
            if ((dx || dy) && (u < 0 || v < 0 || u >= m->w || v >= m->h || (kinds >> map_at(m, u, v) & 1u))) n++;
        }
    }
    return n;
}

/* an area by what is round it, as a printed map has only areas, no specks. the tiles have soft edges
 * and thin things the screen's texels cut in pieces, which printed came out a dust over the sheet:
 *  - water all round is a lake's or a wide river's middle, light grey. some is its bank, dark. two or
 *    three is a creek a texel wide: the tiles have every one in the country. near, a dotted line
 *  - a town is hatched where it is a town all round. far out the tiles' towns are specks between
 *    the fields, and their buildings the casings of the roads: those stay paper. near, the buildings
 *    are blocks of a mid grey in the hatch
 *  - a wood is light grey where it is wood all round, as the old charts tinted the forests: the hills
 *    here are woods, so the tint shows the country's shape. its pieces and edges stay paper, or every
 *    piece was a ripple over the map (the colour look's lesson). the water's light grey has a dark bank
 *    round it, the woods' none: the two do not mix. (the 4.2"'s light grey was 220 of 255 and not to be
 *    seen on the glass, a dot screen stood in for it: its waveform now has the greys nearer the middle,
 *    epd.c LUT_4GRAY.) where a label or a plane has passed over a wood the partial refresh puts paper,
 *    not grey, till the next full one: the grey debt (epd.c) */
/* the same with more tones (paper_style 1), each kind a step apart: the fields paper, the parks and
 * meadows light grey, the woods MID with trees and a light edge, the water flat dark grey (its creeks
 * too, near), the towns hatched on the light grey, near their buildings DEEP. the first try had the
 * woods light, the parks tinted and the water MID: on the glass it was all but the old sheet */
static int area_tones(const map_t *m, int k, int x, int y, int near)
{
    if (k == M_WATER) return around(m, x, y, 1u << M_WATER) >= 4 || near ? S_DARK : S_WHITE;
    if (k == M_WOOD) {
        const int n = around(m, x, y, 1u << M_WOOD);
        return n >= 6 ? S_WOOD : n >= 4 ? S_LIGHT : S_WHITE;
    }
    if (k == M_GREEN) return around(m, x, y, 1u << M_GREEN) >= 6 ? S_LIGHT : S_WHITE;
    if (around(m, x, y, 1u << M_URBAN | 1u << M_BUILDING) < 6) return S_WHITE;
    return k == M_BUILDING && near ? S_DEEP : S_TOWN;
}

static int area_kind(const map_t *m, int k, int x, int y, int near)
{
    if (paper_style) return area_tones(m, k, x, y, near);
    if (k == M_GREEN) return S_WHITE;
    if (k == M_WATER) {
        const int n = around(m, x, y, 1u << M_WATER);
        return n == 8 ? S_LIGHT : n >= 4 ? S_DARK : near ? S_HALF : S_WHITE;
    }
    if (k == M_WOOD) return around(m, x, y, 1u << M_WOOD) >= 6 ? S_LIGHT : S_WHITE;
    if (around(m, x, y, 1u << M_URBAN | 1u << M_BUILDING) < 6) return S_WHITE;
    return k == M_BUILDING && near ? S_HALF : S_HATCH;
}

static void sheet(ui_t *u, canvas_t *cv)
{
    const map_t *m = sheet_map(u);
    uint8_t kind[16];
    const int near = range_km(&u->cfg) <= 10.0f;
    if (m) sheet_kinds(u, kind);
    for (int y = cv->y0; y < cv->y0 + cv->h; y++) {
        uint32_t *row = gfx_row(cv, y);
        if (!m) {
            for (int x = 0; x < GFX_PW; x++) row[x] = P_WHITE;
            continue;
        }
        for (int x = 0; x < GFX_PW; x++) {
            const int k = map_at(m, x, y);
            const int area = k == M_WATER || k == M_URBAN || k == M_BUILDING || k == M_WOOD || k == M_GREEN;
            const int s = area ? area_kind(m, k, x, y, near) : kind[k];
            row[x] = ink_at(s, x, y);
        }
    }
}

/* --- over it --- */

// the range rings, dashed, a dash a 6 degrees
static void rings(ui_t *u, canvas_t *cv)
{
    const float cx = GFX_W / 2.0f, cy = GFX_H / 2.0f;
    for (int r = 0; r < 2; r++) {
        const float rad = r ? (float)PR : PR * 0.5f;
        if (!gfx_touches(cv, (int)(cy - rad - 2), (int)(cy + rad + 2))) continue;
        for (int a = 0; a < 360; a += 6) gfx_arc(cv, cx, cy, rad, (float)a, a + 3.0f, 1.2f, P_BLACK, 255);
    }
    for (int r = 0; r < 2; r++) {
        char b[16];
        ring_text(u, r, b, sizeof b);
        const pbox_t l = ring_label(u, r, b);
        if (!gfx_touches(cv, l.y, l.y + l.h)) continue;
        gfx_fill(cv, l.x, l.y, l.w, l.h, P_WHITE, 255);
        font_text(cv, &FONT_PAPER_S, l.x + 3, l.y + 1, b, P_BLACK, 255);
    }
}

// home: a circle with a dot in a white ground, the chart's sign for a place you stand on
static void home(canvas_t *cv)
{
    const float cx = GFX_W / 2.0f, cy = GFX_H / 2.0f;
    if (!gfx_touches(cv, (int)cy - 9, (int)cy + 9)) return;
    gfx_disc(cv, cx, cy, 8.0f, P_WHITE, 255);
    gfx_ring(cv, cx, cy, 5.5f, 1.8f, P_BLACK, 255);
    gfx_disc(cv, cx, cy, 1.9f, P_BLACK, 255);
}

static void planes(ui_t *u, canvas_t *cv)
{
    const sky_t *s = &u->snap;
    const float size = (float)GFX_H / 240.0f * 1.1f;   // a tenth over the colour screens': no glow to find it by
    for (int q = u->n_order - 1; q >= 0; q--) {         // farthest first, the nearest on top
        const int i = u->order[q];
        const plane_t *p = &s->p[i];
        const spot_t *sp = &u->spot[i];
        if (!sp->vis) continue;
        if (sp->vis == 2) {                             // off the sheet: a dart at its edge, pointing out
            if (!gfx_touches(cv, sp->y - 8, sp->y + 8)) continue;
            const float a = atan2f((float)(sp->x - GFX_W / 2), (float)(GFX_H / 2 - sp->y));
            const float ox = sinf(a), oy = -cosf(a), px = -oy, py = ox;
            gfx_tri(cv, sp->x + ox * 5, sp->y + oy * 5, sp->x - ox * 4 + px * 4, sp->y - oy * 4 + py * 4,
                    sp->x - ox * 4 - px * 4, sp->y - oy * 4 - py * 4, P_BLACK, 255);
            continue;
        }
        // where it was: a dot a position, the older the smaller. a still picture shows the way it flies
        if (gfx_touches(cv, sp->ty0 - 3, sp->ty1 + 3)) {
            for (int t = 0; t < sp->tn; t++) {
                if (sp->tx8[t] == TRAIL_GAP) continue;
                gfx_disc(cv, sp->tx8[t] * 0.125f, sp->ty8[t] * 0.125f, 0.9f + 0.5f * (float)t / (float)sp->tn, P_BLACK, 255);
            }
        }
        const float head = plane_track(s, p, u->now), a = head * D2R;
        /* and which way and how fast, as a controller's scope draws it: a minute's flight at 50 km, the
         * same length at every range (ui_scope.c's VECTOR_PX). a minute at 10 km crossed the sheet */
        if (p->has_spd && !p->ground && p->gs > 30.0f) {
            const float len = p->gs * ((float)PR / 1620.0f);
            const float bx = sp->x + len * sinf(a), by = sp->y - len * cosf(a);
            if (gfx_touches(cv, (int)fminf((float)sp->y, by) - 2, (int)fmaxf((float)sp->y, by) + 2))
                gfx_line(cv, (float)sp->x, (float)sp->y, bx, by, 1.2f, P_BLACK, 255);
        }
        if (!gfx_touches(cv, sp->y - 20, sp->y + 20)) continue;
        const float sz = p->ground ? size * 0.75f : size;
        silhouette(cv, p->kind, sp->x, sp->y, head, sz * 1.5f, P_WHITE, 255);   // a white ground round it
        silhouette(cv, p->kind, sp->x, sp->y, head, sz, P_BLACK, 255);
        if (lost(u, p)) silhouette(cv, p->kind, sp->x, sp->y, head, sz * 0.55f, P_WHITE, 255);   // a guess: hollow
        if (p->icao == u->sel) gfx_ring(cv, sp->x, sp->y, 14.0f * size, 2.0f, P_BLACK, 255);
    }
}

// a plane's label: a white card, black words. the picked one and an emergency inverted
static void labels(ui_t *u, canvas_t *cv)
{
    const sky_t *s = &u->snap;
    for (int i = 0; i < s->n; i++) {
        const pbox_t b = s_lab[i];
        const spot_t *sp = &u->spot[i];
        if (!b.w || sp->vis != 1) continue;
        const int ta = b.y < sp->y ? b.y : sp->y, tb = b.y + b.h > sp->y ? b.y + b.h : sp->y;
        if (!gfx_touches(cv, ta - 2, tb + 2)) continue;
        const plane_t *p = &s->p[i];
        const int inv = p->icao == u->sel || emergency(p);
        // a line to it when the card stands off
        const float nx = fminf(fmaxf((float)sp->x, (float)b.x), (float)(b.x + b.w - 1));
        const float ny = fminf(fmaxf((float)sp->y, (float)b.y), (float)(b.y + b.h - 1));
        const float dx = nx - sp->x, dy = ny - sp->y, l = sqrtf(dx * dx + dy * dy);
        if (l > 9.0f) gfx_line(cv, sp->x + dx / l * 7.0f, sp->y + dy / l * 7.0f, nx, ny, 1.0f, P_BLACK, 255);
        const uint32_t bg = inv ? P_BLACK : P_WHITE, fg = inv ? P_WHITE : P_BLACK;
        gfx_fill(cv, b.x, b.y, b.w, b.h, bg, 255);
        if (watched(u, p)) {                           // on the watch list: an edge round it, in the words' ink
            gfx_hline(cv, b.x, b.x + b.w - 1, b.y, fg, 255);
            gfx_hline(cv, b.x, b.x + b.w - 1, b.y + b.h - 1, fg, 255);
            gfx_vline(cv, b.x, b.y, b.y + b.h - 1, fg, 255);
            gfx_vline(cv, b.x + b.w - 1, b.y, b.y + b.h - 1, fg, 255);
        }
        char alt[16];
        if (emergency(p)) snprintf(alt, sizeof alt, "%04u", (unsigned)p->squawk);
        else fmt_alt(u, p, alt, sizeof alt, 1);
        font_text(cv, &FONT_PAPER_S, b.x + 3, b.y + 1, plane_name(p), fg, 255);
        font_text(cv, &FONT_PAPER_S, b.x + 3, b.y + 12, alt, fg, 255);
    }
}

static void frame_box(canvas_t *cv, pbox_t b, int t)  // a white card with a black edge t thick
{
    gfx_fill(cv, b.x, b.y, b.w, b.h, P_BLACK, 255);
    gfx_fill(cv, b.x + t, b.y + t, b.w - 2 * t, b.h - 2 * t, P_WHITE, 255);
}

// where home is, and how the sky is
static void draw_home_card(ui_t *u, canvas_t *cv)
{
    const pbox_t b = home_card(u);
    if (!gfx_touches(cv, b.y, b.y + b.h + 20)) return;
    frame_box(cv, b, 1);
    font_text_clip(cv, &FONT_PAPER_M, b.x + 6, b.y + 4, home_name(u), P_BLACK, 255, b.w - 10);
    char t[48];
    if (u->net.demo) snprintf(t, sizeof t, "%s", u->net.configured ? "NO WIFI" : "NO WIFI SET UP");
    else if (!u->net.wifi) snprintf(t, sizeof t, "%s", u->snap.status == SKY_CONNECTING ? "CONNECTING" : "NO WIFI");
    else if (u->snap.status == SKY_OK) snprintf(t, sizeof t, "%d PLANES", u->n_order);
    else snprintf(t, sizeof t, "%s", u->snap.note[0] ? u->snap.note : "WAITING");
    font_text_clip(cv, &FONT_PAPER_S, b.x + 6, b.y + 21, t, P_BLACK, 255, b.w - 10);
    if (u->cfg.map && !sheet_map(u)) {                 // the map of this range is still being made, or can't be
        const char *m = u->net.demo || !u->net.wifi ? "MAP NEEDS WIFI" : "MAP COMING";
        const pbox_t c = { b.x, b.y + b.h + 3, font_width(&FONT_PAPER_S, m) + 8, 15 };
        frame_box(cv, c, 1);
        font_text(cv, &FONT_PAPER_S, c.x + 4, c.y + 2, m, P_BLACK, 255);
    }
}

// the picked plane: what it is, how high, how fast, how far
static void draw_pick_card(ui_t *u, canvas_t *cv)
{
    const plane_t *p = sel_plane(u);
    const pbox_t b = pick_card();
    if (!p || !gfx_touches(cv, b.y, b.y + b.h)) return;
    frame_box(cv, b, 2);
    char t[48], a[16], d[16];
    int x = b.x + 7 + font_text(cv, &FONT_PAPER_L, b.x + 7, b.y + 3, plane_name(p), P_BLACK, 255);
    snprintf(t, sizeof t, "%s%s", p->type[0] ? p->type : kind_name(p->kind), watched(u, p) ? "  WATCHED" : "");
    font_text_clip(cv, &FONT_PAPER_S, x + 6, b.y + 10, t, P_BLACK, 255, b.x + b.w - x - 12);
    fmt_alt(u, p, a, sizeof a, 1);
    fmt_dist(u, p->dist, d, sizeof d);
    if (p->has_spd) snprintf(t, sizeof t, "%s  %d KT  %s", a, (int)lroundf(p->gs), d);
    else snprintf(t, sizeof t, "%s  %s", a, d);
    font_text_clip(cv, &FONT_PAPER_M, b.x + 7, b.y + 26, t, P_BLACK, 255, b.w - 14);
    if (emergency(p)) snprintf(t, sizeof t, "%04u %s", (unsigned)p->squawk,
                               p->squawk == 7500 ? "HIJACK" : p->squawk == 7600 ? "RADIO LOST" : "EMERGENCY");
    else if (p->orig[0] || p->dest[0]) snprintf(t, sizeof t, "%s > %s  %s", p->orig[0] ? p->orig : "?", p->dest[0] ? p->dest : "?", p->reg);
    else snprintf(t, sizeof t, "%s", p->reg[0] ? p->reg : kind_name(p->kind));
    font_text_clip(cv, &FONT_PAPER_S, b.x + 7, b.y + 44, t, P_BLACK, 255, b.w - 14);
}

static void osm(ui_t *u, canvas_t *cv)               // whose map it is: its licence asks for that, where it shows
{
    const pbox_t b = osm_chip();
    if (!sheet_map(u) || !gfx_touches(cv, b.y, b.y + b.h)) return;
    gfx_fill(cv, b.x, b.y, b.w, b.h, P_WHITE, 255);
    font_text(cv, &FONT_PAPER_S, b.x + 4, b.y + 1, "\xC2\xA9 OpenStreetMap", P_BLACK, 255);
}

/* --- the keys' strip: what each key does now --- */

static void tab_sign(canvas_t *cv, int key, float x, float y, uint32_t c)
{
    if (key == KEY_UP) gfx_tri(cv, x - 4, y + 3, x + 4, y + 3, x, y - 4, c, 255);
    else if (key == KEY_DOWN) gfx_tri(cv, x - 4, y - 3, x + 4, y - 3, x, y + 4, c, 255);
    else if (key == KEY_OK) gfx_disc(cv, x, y, 3.2f, c, 255);
}

/* a third: its grey, and its lines side by side, each turned to read downwards and in the middle of the third:
 * a sign the right way up (the dial's), then the words. pressed: black, the words white */
static void zone(ui_t *u, canvas_t *cv, int z)
{
    const pbox_t b = zone_box(z);
    if (!gfx_touches(cv, b.y - 1, b.y + b.h + 1)) return;
    const int pressed = zone_pressed(u, z);
    const uint32_t fg = pressed ? P_WHITE : P_BLACK;
    gfx_fill(cv, b.x, b.y, b.w, b.h, pressed ? P_BLACK : P_LIGHT, 255);
    const char *say[3];
    int key[3], n = 0;
    const char *held;
    if (z == ZONE_DIAL) {
        for (int t = TAB_UP; t <= TAB_DOWN; t++) {
            const char *s = tab_words(u, t, &held);
            if (s) {
                say[n] = s;
                key[n++] = TAB_KEY[t];
            }
        }
    } else {
        const char *s = tab_words(u, z == ZONE_MENU ? TAB_MENU : TAB_EXIT, &held);
        if (s) {
            say[n] = s;
            key[n++] = 0;
            if (held) {
                say[n] = held;
                key[n++] = 0;
            }
        }
    }
    const int soft = cv->soft;
    cv->soft = !pressed;                               // pressed it is drawn on top in black and white: crisp
    for (int i = 0; i < n; i++) {
        /* the lines' letters 7 high from 2 under their line's top, 9 apart: three in the strip, the middle one in its middle */
        const float mid = (float)b.x + (float)KS_W * 0.5f + ((float)(n - 1) * 0.5f - (float)i) * 9.0f;
        const int sign = key[i] != 0, len = font_width(&FONT_PAPER_S, say[i]) + (sign ? 12 : 0);
        const int y = b.y + (b.h - len) / 2;
        if (sign) tab_sign(cv, key[i], mid, (float)y + 4.0f, fg);
        font_text_down(cv, &FONT_PAPER_S, (int)lroundf(mid + 5.5f), y + (sign ? 12 : 0), say[i], fg);
    }
    cv->soft = soft;
}

static void tabs(ui_t *u, canvas_t *cv)
{
    for (int z = 0; z < ZONES; z++) zone(u, cv, z);
}

static void toast_card(ui_t *u, canvas_t *cv)
{
    if (!ui_until(u->toast_until, u->now)) return;
    const int w = font_width(&FONT_PAPER_M, u->toast) + 20, h = 24;
    const pbox_t b = { (GFX_W - w) / 2, GFX_H / 2 + 22, w, h };
    if (!gfx_touches(cv, b.y, b.y + h)) return;
    frame_box(cv, b, 2);
    font_text(cv, &FONT_PAPER_M, b.x + 10, b.y + 5, u->toast, P_BLACK, 255);
}

void paper_draw(ui_t *u, canvas_t *cv)
{
    if (u->cfg.look == LOOK_WEATHER) {                 // the weather's pages (ui_weather.c), the keys' strip beside them
        const int soft = cv->soft;
        cv->soft = 1;                                  // printed whole whenever they change: the letters' edges in greys
        weather_paper(u, cv);                          // no toast: its warning is on the page, its name is the strip's
        cv->soft = soft;
        tabs(u, cv);
        return;
    }
    sheet(u, cv);
    rings(u, cv);
    home(cv);
    planes(u, cv);
    labels(u, cv);
    draw_home_card(u, cv);
    draw_pick_card(u, cv);
    osm(u, cv);
    tabs(u, cv);
    toast_card(u, cv);
}

#endif
