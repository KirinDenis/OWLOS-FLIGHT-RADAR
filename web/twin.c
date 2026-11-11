/*
 * twin - the flight radar in a web page: core/ as it is, the same files as the firmware and the PC
 * preview, built for WebAssembly (BUILD.BAT). the page's canvas is the screen, its pointer the
 * finger, and it fetches the map's tiles itself: this file only hands things across.
 * the 5" layout, 800x480. a demo sky till a board is connected, then the board's planes: it sends
 * them over the serial port in the free networks' own words, this reads them with sky_read.
 */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "ui_int.h"
#include "demo.h"
#include "map.h"

#define STRIP 48
#define ANSWER_MS 5000u                                /* an answer every 5 s, like the board asks */

static ui_t       s_ui;
static sky_t      s_sky;
static demo_t     s_demo;
static char       s_json[32768];
static uint32_t   s_answered;
static int        s_board;                             /* a board is connected: the planes are its */
static sky_reader_t s_rd;
static int        s_planes;                            /* in the answer coming in */
static int        s_changed;                           /* what a finger here changed: TWIN_* */
static uint32_t   s_sel_told;                          /* the picked plane the board knows of */

enum { TWIN_CFG = 1, TWIN_SEL = 2 };
static uint32_t   s_strip[GFX_PW * STRIP];
static uint32_t   s_rgba[GFX_PW * GFX_PH];             /* bytes R G B A, what a canvas wants */

static map_t      s_map;
static map_plan_t s_plan;
static int        s_making;                            /* the page is fetching this plan's tiles */

static void answer(uint32_t now)
{
    int n = demo_json(&s_demo, s_ui.cfg.lat, s_ui.cfg.lon, range_km(&s_ui.cfg), now, s_json, sizeof s_json);
    sky_read_begin(&s_rd, &s_sky, 0, now);
    sky_read(&s_rd, s_json, n);
    sky_read_end(&s_rd, &s_sky);
    sky_forget(&s_sky, now);
    s_answered = now;
}

static void say_where(const char *server)
{
    snprintf(s_ui.net.server, sizeof s_ui.net.server, "%s", server);
}

EMSCRIPTEN_KEEPALIVE int twin_width(void) { return GFX_PW; }
EMSCRIPTEN_KEEPALIVE int twin_height(void) { return GFX_PH; }

EMSCRIPTEN_KEEPALIVE int twin_init(uint32_t now)
{
    if (!ui_init(&s_ui, now)) {
        return 0;
    }
    fr_cfg_t c = s_ui.cfg;
    snprintf(c.place, sizeof c.place, "DEMO SKY");
    ui_set_cfg(&s_ui, &c);
    ui_version = FLIGHT_VERSION;
    s_ui.map = &s_map;
    s_ui.net.configured = s_ui.net.wifi = 1;           // the status line says what the planes are: DEMO
    snprintf(s_ui.net.ssid, sizeof s_ui.net.ssid, "PAGE");
    snprintf(s_ui.net.server, sizeof s_ui.net.server, "DEMO");
    sky_init(&s_sky, c.lat, c.lon);
    demo_init(&s_demo, range_km(&c), 82);
    answer(now);
    return 1;
}

/* a frame. down, x, y: the pointer on the canvas, in the glass's pixels: a sample every frame, with
 * a finger or without, as on the board. the answer is the bytes of the picture */
EMSCRIPTEN_KEEPALIVE uint8_t *twin_frame(uint32_t now, uint32_t utc, int down, int x, int y)
{
    if (!s_board && now - s_answered >= ANSWER_MS) {
        answer(now);
    }
    s_ui.net.utc = utc;
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    s_ui.snap = s_sky;
    ui_touch(&s_ui, down, x * GFX_W / GFX_PW, y * GFX_H / GFX_PH, 0, 0, now, &cmd);
    if (cmd.cfg || cmd.opt || cmd.bare) {
        s_changed |= TWIN_CFG;
    }
    s_ui.net.fetched = s_answered;
    ui_frame(&s_ui, now);
    if (s_ui.sel != s_sel_told) {
        s_sel_told = s_ui.sel;
        s_changed |= TWIN_SEL;
    }
    for (int y0 = 0; y0 < GFX_PH; y0 += STRIP) {
        canvas_t cv = { s_strip, y0, y0 + STRIP <= GFX_PH ? STRIP : GFX_PH - y0, 0 };
        ui_strip(&s_ui, &cv);
        uint32_t *to = s_rgba + y0 * GFX_PW;
        for (int i = 0; i < GFX_PW * cv.h; i++) {
            const uint32_t p = s_strip[i];             // 0x00RRGGBB, and in the bytes of a word it is the other way
            to[i] = 0xFF000000u | (p & 0x00FF00u) | (p >> 16 & 0xFFu) | (p & 0xFFu) << 16;
        }
    }
    return (uint8_t *)s_rgba;
}

/* --- a board: what it says of itself (OWLOS GET) and its planes (OWLOS SKY ON, sky_out in main.c) --- */

EMSCRIPTEN_KEEPALIVE void twin_board(int on)
{
    s_board = on;
    s_sky.n = 0;
    if (!on) {                                         // the demo again, and it says so
        fr_cfg_t c = s_ui.cfg;
        snprintf(c.place, sizeof c.place, "DEMO SKY");
        ui_set_cfg(&s_ui, &c);
        say_where("DEMO");
    }
}

// how the board shows it. a new home starts a new sky, and a new map follows by itself. a finger on
// the page wins: what it is doing goes to the board when it lifts
EMSCRIPTEN_KEEPALIVE void twin_cfg(double lat, double lon, const char *place, const char *country, double range,
                                   int up, int theme, int look, int map, int ground, int dist_unit, int alt_unit, int labels)
{
    if (s_ui.t_down) {
        return;
    }
    fr_cfg_t c = s_ui.cfg;
    const int moved = c.lat != (float)lat || c.lon != (float)lon;
    c.lat = (float)lat;
    c.lon = (float)lon;
    snprintf(c.place, sizeof c.place, "%s", place);
    snprintf(c.country, sizeof c.country, "%s", country);
    c.range = (float)range;
    c.up = (int16_t)up;
    c.theme = (uint8_t)theme;
    c.look = (uint8_t)look;
    c.map = (uint8_t)map;
    c.ground = (uint8_t)ground;
    c.dist_unit = (uint8_t)dist_unit;
    c.alt_unit = (uint8_t)alt_unit;
    c.labels = (uint8_t)labels;
    ui_set_cfg(&s_ui, &c);
    if (moved) {
        sky_init(&s_sky, c.lat, c.lon);
    }
}

// what a finger here changed since the last time the page asked: TWIN_*
EMSCRIPTEN_KEEPALIVE int twin_changed(void)
{
    int c = s_changed;
    s_changed = 0;
    return c;
}

// the same settings the other way: the JSON for OWLOS SET
EMSCRIPTEN_KEEPALIVE const char *twin_set_json(void)
{
    static char b[320];
    const fr_cfg_t *c = &s_ui.cfg;
    const scope_opt_t *o = &s_ui.opt;
    snprintf(b, sizeof b, "{\"up\":%d,\"theme\":%d,\"look\":%d,\"map\":%d,\"ground\":%d,\"range\":%.2f,\"dist_unit\":%d,\"alt_unit\":%d,"
                          "\"labels\":%d,\"vectors\":%d,\"beam_step\":%d,\"clock\":%d,\"block\":%d,\"autopilot\":%d,\"lost\":%d,"
                          "\"conflict\":%d,\"fl_lo\":%d,\"fl_hi\":%d,\"bare\":%d}",
             c->up, c->theme, c->look, c->map, c->ground, (double)c->range, c->dist_unit, c->alt_unit, c->labels,
             o->vectors, o->beam_step, o->clock, o->block, o->autopilot, o->lost, o->conflict, o->fl_lo, o->fl_hi, s_ui.bare);
    return b;
}

// the radar without its panel (its three dots), as the board keeps it. a finger on the page wins, as above
EMSCRIPTEN_KEEPALIVE void twin_bare(int bare)
{
    if (!s_ui.t_down) {
        s_ui.bare = (uint8_t)(bare == 1);
    }
}

EMSCRIPTEN_KEEPALIVE uint32_t twin_sel(void) { return s_ui.sel; }

// picked on the board, 0: let go
EMSCRIPTEN_KEEPALIVE void twin_pick(uint32_t icao)
{
    s_ui.sel = s_sel_told = icao;
}

EMSCRIPTEN_KEEPALIVE void twin_opt(int vectors, int beam_step, int clock, int block, int autopilot, int lost, int conflict,
                                   int fl_lo, int fl_hi)
{
    scope_opt_t *o = &s_ui.opt;
    o->vectors = (uint8_t)vectors;
    o->beam_step = (uint8_t)beam_step;
    o->clock = (uint8_t)clock;
    o->block = (uint8_t)block;
    o->autopilot = (uint8_t)autopilot;
    o->lost = (uint8_t)lost;
    o->conflict = (uint8_t)conflict;
    o->fl_lo = (int16_t)fl_lo;
    o->fl_hi = (int16_t)fl_hi;
}

EMSCRIPTEN_KEEPALIVE void twin_sky_begin(uint32_t now, const char *server)
{
    static const char HEAD[] = "{\"ac\":[";
    say_where(server);
    sky_read_begin(&s_rd, &s_sky, 0, now);
    sky_read(&s_rd, HEAD, (int)sizeof HEAD - 1);
    s_planes = 0;
}

EMSCRIPTEN_KEEPALIVE void twin_sky_plane(const char *json)
{
    if (s_planes++) {
        sky_read(&s_rd, ",", 1);
    }
    sky_read(&s_rd, json, (int)strlen(json));
}

EMSCRIPTEN_KEEPALIVE int twin_sky_end(uint32_t now, int total)
{
    char tail[32];
    int n = snprintf(tail, sizeof tail, "],\"total\":%d}", total);
    sky_read(&s_rd, tail, n);
    s_rd.now = now;
    int read = sky_read_end(&s_rd, &s_sky);
    sky_forget(&s_sky, now);
    s_answered = now;
    return read;
}

/* --- the map: the page asks, while a finger is off, whether a new one is wanted. it fetches the
 * tiles, hands each over, and says when that is all. made here with the firmware's own reader --- */

EMSCRIPTEN_KEEPALIVE int twin_map_wanted(void)
{
    const fr_cfg_t *c = &s_ui.cfg;
    if (s_making || s_ui.t_down || !c->map) {
        return 0;
    }
    const float kpx = ui_ground_kpx(c);
    int w, h, disc;
    ui_ground_size(c, &w, &h, &disc);
    if (s_map.ready && s_map.lat == c->lat && s_map.lon == c->lon && s_map.kpx == kpx && s_map.w == w && s_map.h == h) {
        return 0;
    }
    map_plan(&s_plan, c->lat, c->lon, kpx, w, h, disc);
    if (!map_alloc(&s_map, w, h, disc)) {
        return 0;
    }
    map_begin(&s_map);
    s_making = 1;
    return s_plan.ntiles;
}

// the tile's address, "" past the poles
EMSCRIPTEN_KEEPALIVE const char *twin_map_url(int k)
{
    static char url[96];
    if (k < 0 || k >= s_plan.ntiles || !map_url(&s_plan.tiles[k], url, sizeof url)) {
        return "";
    }
    return url;
}

typedef struct { const uint8_t *p; int left; } bytes_t;

static int bytes_read(void *ctx, uint8_t *buf, int cap)
{
    bytes_t *b = ctx;
    int n = b->left < cap ? b->left : cap;
    memcpy(buf, b->p, (size_t)n);
    b->p += n;
    b->left -= n;
    return n;
}

EMSCRIPTEN_KEEPALIVE int twin_map_tile(int k, const uint8_t *png, int len)
{
    bytes_t b = { png, len };
    return map_tile(&s_map, &s_plan, k, bytes_read, &b) == PNG_OK;
}

EMSCRIPTEN_KEEPALIVE void twin_map_end(void)
{
    map_end(&s_map, &s_plan);
    s_making = 0;
}
