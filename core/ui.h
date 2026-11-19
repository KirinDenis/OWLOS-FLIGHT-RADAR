/* ui - the screen and the touch.
 * ui_frame lays out once per frame, ui_strip draws strip by strip (the WiFi radar's renderer).
 * the UI only reads the sky; what it changes comes back in ui_cmd_t, the caller applies it.
 * secrets (WiFi password, the FR24 key) never come here: the screen only learns whether they
 * are set. the one way in is the WiFi page: a password typed there is handed to the firmware
 * with ui_cmd_t.wifi, and the firmware wipes the screen's copy */
#pragma once
#include "sky.h"
#include "gfx.h"
#include "map.h"
#include "watch.h"
#include "light.h"
#include "weather.h"

enum { VIEW_MAIN, VIEW_LIST, VIEW_SETUP, VIEW_CAL, VIEW_WIFI, VIEW_KEYS, VIEW_ABOUT };   /* WIFI: the networks, KEYS: the password */
enum { KB_PASS, KB_HOME, KB_WATCH };               /* what the keyboard types */
#define UI_THEMES 5

enum { SRC_FREE, SRC_FR24 };                       /* where the planes come from */
enum { FREE_ADSB_LOL, FREE_ADSB_FI, FREE_SERVERS };           /* open, no key: airplanes.live and adsb.one closed to strangers */
enum { DIST_KM, DIST_NM, DIST_MI };
enum { ALT_FT, ALT_M };
/* the beam over the ground, the flat map, the map in its own colours, the next 24 hours' weather on the dial */
enum { LOOK_RADAR, LOOK_MAP, LOOK_CLASSIC, LOOK_WEATHER, LOOKS };
/* the WEATHER look's pages, a tap on the dial or the graph (or BOOT) to the next: the day on the dial, the
 * pressure a day back and a day on, the temperature and the rain by the hour */
enum { WXP_DIAL, WXP_PRESSURE, WXP_TEMP, WX_PAGES };
/* the weather's units, SET's UNITS tab: 0 each the home country's own (the US's Fahrenheit, inches and mph,
 * the millimetres of mercury east of Poland...), else one of these. the pressure in hPa and inHg is at the
 * sea's level as forecasts give it, in mmHg at home's height as a barometer there reads */
enum { TEMP_AUTO, TEMP_C, TEMP_F, TEMP_UNITS };
enum { PRES_AUTO, PRES_HPA, PRES_MMHG, PRES_INHG, PRES_UNITS };
enum { WIND_AUTO, WIND_KMH, WIND_MS, WIND_MPH, WIND_KN, WIND_UNITS };
enum { RAIN_AUTO, RAIN_MM, RAIN_IN, RAIN_UNITS };
typedef struct { uint8_t temp, pres, wind, rain; } wx_units_t;
#define RANGE_MIN 2.0f                             /* the radius of the scope, in the distance unit */
#define RANGE_MAX 250.0f

/* settings the screen shows and changes. saved by the firmware */
typedef struct {
    uint16_t version;
    uint8_t  source;       /* SRC_* */
    uint8_t  server;       /* FREE_*: the first one tried, the others take over when it fails */
    uint8_t  dist_unit, alt_unit;
    uint8_t  flip;         /* the screen is mounted upside down */
    uint8_t  theme;
    uint8_t  labels;       /* 0 every plane, 1 only the picked one */
    uint8_t  look;         /* LOOK_* */
    uint8_t  map;          /* 1: OpenStreetMap under the scope, the board downloads it */
    int16_t  up;           /* compass direction at the top of the screen, degrees. 0 = north up */
    float    lat, lon;     /* home */
    char     place[40];    /* home's town and */
    char     country[32];  /* its country: from the site, or the board asks OpenStreetMap */
    float    range;        /* radius of the scope in the distance unit: zoomed by a finger, any value */
    uint8_t  ground;       /* 1: planes on the ground shown, 0: left out (a home next to an airport) */
} fr_cfg_t;

#define FR_CFG_VERSION 4
void fr_cfg_defaults(fr_cfg_t *c);

/* the radar look's options, all off until someone turns them on: the gear under the scope, the
 * site. kept by themselves like the LED's, so an older firmware flashed back finds fr_cfg_t as it left it */
#define SCOPE_OPT_VERSION 1
#define SCOPE_FL_MAX 450   /* LOWEST and HIGHEST go up to here */
typedef struct {
    uint8_t  version;
    uint8_t  vectors;      /* a line ahead of every plane: which way and how fast */
    uint8_t  beam_step;    /* a plane moves only when the beam goes over it, as on a real scope */
    uint8_t  clock;        /* the time in UTC */
    uint8_t  block;        /* the label says climbing or coming down, and the speed in tens of knots */
    uint8_t  autopilot;    /* the label says the height set on the autopilot */
    uint8_t  lost;         /* a plane with no fresh position is marked */
    uint8_t  conflict;     /* two planes closing on each other go red */
    int16_t  fl_lo, fl_hi; /* only planes between these, hundreds of feet. 0: no limit */
} scope_opt_t;
void scope_opt_defaults(scope_opt_t *o);
float range_km(const fr_cfg_t *c);                 /* the radius of the scope */
float unit_km(int dist_unit);                      /* km in one of DIST_* */
const char *unit_name(int dist_unit);              /* KM, NM or MI */

/* a network the board heard when it looked round */
typedef struct {
    char    ssid[33];
    int8_t  rssi;          /* dBm */
    uint8_t open;          /* no password */
    uint8_t kept;          /* the board remembers it: a tap joins it */
} ap_t;

#define UI_APS 16
enum { SCAN_NONE, SCAN_RUNNING, SCAN_DONE, SCAN_FAILED };
enum { WHY_NONE, WHY_NOT_FOUND, WHY_PASSWORD };      /* why the network that is set is not up */

/* how the network is doing, for the status line and the setup page */
typedef struct {
    uint8_t  configured;   /* a WiFi network is set, from the site or the WiFi page */
    uint8_t  wifi;         /* connected */
    uint8_t  has_key;      /* an FR24 key is stored */
    uint8_t  demo;         /* no WiFi set, or it is not there: the demo sky is up */
    char     ssid[33];     /* the one it is on or is trying, of those it remembers */
    char     ip[16];
    char     server[24];   /* who answered last */
    int      http;         /* last HTTP status, 0 none */
    uint32_t fetched;      /* ms of the last answer */
    uint32_t utc;          /* seconds since 1970, 0: the board does not know the time */
    uint8_t  why;          /* WHY_* */
    uint8_t  scan;         /* SCAN_*: the WiFi page asked the board to look round */
    uint8_t  n_aps;
    ap_t     aps[UI_APS];  /* what it found, strongest first, each name once */
} net_t;

// affine map, raw touch chip numbers -> screen pixels
typedef struct { float a, b, c, d, e, f; } touch_cal_t;

typedef struct {
    uint8_t cfg;           /* u->cfg changed: use it now, save it a little later */
    uint8_t save_cal;
    uint8_t refetch;       /* range, source or home changed: ask the server now */
    uint8_t scan;          /* the WiFi page opened: look for networks */
    uint8_t wifi;          /* join ui_t.wifi_ssid with ui_t.wifi_pass, then wipe the password */
    uint8_t home;          /* a new home: ui_t.home_text is a town, a postcode or "lat lon" */
    uint8_t watch;         /* a new watch list: ui_t.watch_text */
    uint8_t light;         /* ui_t.light changed: use it, keep it */
    uint8_t opt;           /* ui_t.opt changed: use it, keep it */
    uint8_t light_test;    /* the LED's red, green, blue */
    uint8_t help;          /* ui_t.help changed: use it, keep it */
    uint8_t bare;          /* ui_t.bare changed: keep it */
    uint8_t backlight;     /* ui_t.backlight changed: use it, keep it */
    uint8_t night;         /* ui_t.night changed: use it, keep it */
    uint8_t alerts;        /* ui_t.alerts changed: keep it */
    uint8_t units;         /* ui_t.units changed: keep it */
} ui_cmd_t;

#define TRAIL_GAP INT16_MIN       /* a trail point off the scope: the trail breaks there */

typedef struct {
    int16_t x, y;          /* where the plane is drawn now */
    int16_t vx, vy;        /* the end of its speed line, opt.vectors. the same as x, y: none */
    uint8_t alert;         /* opt.conflict: 1 + the index of the plane it is closing on, 0 none */
    uint8_t vis;           /* 1 inside the scope, 2 on the rim (outside the range) */
    int16_t lx, ly, lw;    /* its label, lw = 0: none */
    uint8_t tn;            /* its trail on the screen, 1/8 px, worked out once a frame: a strip */
    int16_t ty0, ty1;      /* only draws the pieces in it. ty0..ty1 the rows it covers */
    int16_t tx8[TRAIL_N], ty8[TRAIL_N];
} spot_t;

typedef struct {
    uint32_t now, boot;    /* boot: when ui_init ran */
    sky_t    snap;         /* copy of the sky for this frame */
    fr_cfg_t cfg;
    scope_opt_t opt;
    net_t    net;
    int      view, theme;
    uint32_t sel;          /* the picked plane's address, 0 none */
    spot_t   spot[SKY_MAX];
    uint8_t  order[SKY_MAX];   /* nearest first */
    int      n_order;
    float    beam;         /* the sweep, degrees */
    int      list_scroll, setup_scroll;
    int      setup_tab;    /* SET's tab: how it looks, what it shows and where from, the LED */
    /* touch */
    int      t_down, t_x0, t_y0, t_x, t_y, t_moved, t_long, t_ns, t_hit, t_reps, t_rpt, t_scroll0;
    uint32_t t_since, t_rep;
    int      t_turn;       /* the finger turns the map (1, on the rim) or zooms it (2, inside) */
    uint8_t  t_panel;      /* 1 + whether the panel was shown when that began: the scope stays put under the finger, 0 none */
    float    t_a0, t_r0, t_up0, t_range0;
    int16_t  t_sx[16], t_sy[16];
    /* (chipguyhere) momentum: a list let go while it moves goes on by itself, slowing (ui_frame steps it) */
    uint8_t  fling;        /* 1 while the list coasts */
    uint8_t  t_caught;     /* this press stopped a coasting list: it is not a tap, a long press or a repeat */
    int16_t  fl_y;         /* while dragging: the finger's last y that moved */
    uint32_t fl_at;        /* while dragging: when it moved; while coasting: the last step */
    float    fl_v;         /* the scroll's speed, layout pixels a ms (+: further down the list) */
    float    fl_pos;       /* while coasting: the scroll, unrounded */
    /* touch alignment */
    touch_cal_t cal;
    int      cal_step;
    float    cal_raw[5][2];
    int16_t  cal_sx[48], cal_sy[48];
    int      cal_n;
    float    cal_err;
    int      cal_wait;
    uint32_t toast_until;
    char     toast[40];
    uint32_t menu_until;   /* round glass: the HUE LIST SET row shows till then */
    uint8_t  panel_open;   /* the square map: the panel slid in over it (the three dots) */
    uint8_t  bare;         /* the radar without its panel, the scope in the middle (its three dots). the firmware keeps it */
    uint8_t  backlight;    /* the screen's light, a step of UI_BACKLIGHTS, 0 the brightest. SET's BACKLIGHT, the firmware keeps it */
    uint8_t  night;        /* SET's NIGHT: 0 off, else the step the light goes down to while the sun is down at home */
    /* the WiFi page and its keyboard. the keyboard types the home and the watch list too */
    int      wifi_scroll;
    char     wifi_ssid[33];
    char     wifi_pass[65];    /* while it is typed. ui_cmd_t.wifi hands it over */
    char     home_text[64];    /* a town, a postcode or "lat lon". ui_cmd_t.home hands it over */
    char     watch_text[WATCH_TEXT];   /* the watch list being typed. ui_cmd_t.watch hands it over */
    uint8_t  kb_mode;          /* KB_PASS, KB_HOME, KB_WATCH: what the keyboard types */
    watchlist_t watch;         /* the firmware's list, copied in like net: for SET */
    light_cfg_t light;         /* what lights the LED on the back (core/light.h), SET's LED rows */
    uint8_t  help;             /* counted on the site's map of boards: 0 never asked, 1 yes, 2 no. SET's COUNT ME */
    char     newest[12];       /* the version the site has, "": not heard yet. the firmware copies it in like net */
    int      about_back;       /* the view ABOUT goes back to */
    uint8_t  kb_page;          /* 0 small letters, 1 capitals, 2 signs */
    uint8_t  kb_show;          /* the password in clear, not dots */
    uint32_t kb_typed;         /* when the last letter came: it shows a moment before it turns to a dot */
    const map_t *map;      /* the ground under the scope, NULL: none */
    const weather_t *wx;   /* the forecast for the WEATHER look, NULL: none yet */
    uint8_t  wx_page;      /* the WEATHER look's page shown, WXP_*: the dial each time the look comes */
    uint8_t  alerts;      /* the weather's warnings shown on every look, 1 << WXA_* (weather.h): SET's DATA tab,
                              all of them at first. the firmware keeps it */
    wx_units_t units;      /* the weather's units, SET's UNITS tab: 0 each home's country's own. the firmware keeps it */
    char     home_cc[3];   /* home's country, its two small letters, "" not known: the firmware copies it in like net */
    wx_alert_t alert;      /* the warnings now: ui_frame works them out of wx each frame */
    uint8_t  alert_was[WXA_KINDS];   /* their levels the frame before */
    uint8_t  alert_new;    /* the kinds that came or got worse this frame (1 << WXA_*): the firmware lights the LED */
    uint32_t prof[6];      /* us spent a layer, summed till the firmware logs them: background, disc,
                              compass, planes, panel, the rest */
} ui_t;

/* a deadline in the 32-bit milliseconds still ahead. 0 is none: ui_frame lets a passed one go at once,
 * or 24.8 days on the clock would wrap round to it and read it as ahead again, a toast back for good */
static inline int ui_until(uint32_t until, uint32_t now) { return until && (int32_t)(until - now) > 0; }

/* the screen's light in SET's BACKLIGHT steps, as a percent: the firmware turns the glass to it where
 * it can (config.h RADAR_HAS_DIM). each step about half the one before, which the eye sees as even */
#define UI_BACKLIGHTS 4
static inline int ui_backlight_pct(int step)
{
    static const uint8_t PCT[UI_BACKLIGHTS] = { 100, 50, 25, 10 };
    return PCT[step >= 0 && step < UI_BACKLIGHTS ? step : 0];
}

int  ui_init(ui_t *u, uint32_t now);
void ui_set_cfg(ui_t *u, const fr_cfg_t *c);
// copy the sky and the net state into u->snap / u->net before this
void ui_frame(ui_t *u, uint32_t now);
void ui_strip(ui_t *u, canvas_t *cv);
#if RADAR_OVERLAY
/* (chipguyhere) which rectangles of the page can differ between the states a and b (layout pixels,
 * x1 / y1 not included; ui_wifi.c): how many were put in r[] (max at least 8), 0 none, -1 all of it */
typedef struct { int16_t x0, y0, x1, y1; } ui_rect_t;
int ui_dirty_rects(const ui_t *a, const ui_t *b, ui_rect_t *r, int max);
/* (chipguyhere) a page that scrolls (the plane list, the setup, the networks): its viewport (layout rows
 * y0 .. y1 - 1, what gfx_sub clips its rows to), where it is scrolled to, how far it goes, and columns inside
 * the viewport that do not scroll with it (the setup's scroll bar, bx0 .. bx1 - 1; none if equal). 0: this
 * page does not scroll, or it all fits. ui_scrolled: the same page drawn scrolled to s (a copy of the ui) */
typedef struct { int y0, y1, scroll, max, bx0, bx1; } ui_scroll_t;
int  ui_scroll_info(const ui_t *u, ui_scroll_t *s);
void ui_scrolled(ui_t *u, int s);
/* (chipguyhere) what the page shows pressed under the finger (a button, a row), 0 nothing: the scope
 * under a finger is not a press (it turns or zooms) */
int  ui_pressed(const ui_t *u);
#endif
#if RADAR_OVERLAY
/* the boards that put the moving things over the frame as the panel is fed (config.h): ui_strip
 * draws the layer the canvas says (canvas_t.layer). BG leaves out the planes, the sweep and the compass
 * ring; ui_top_strip draws just those on a canvas of nothing (gfx.h GFX_ALPHA). a field of the canvas,
 * not a global: the two layers are drawn on different cores at once */
enum { UI_LAYER_ALL, UI_LAYER_BG, UI_LAYER_TOP };
void ui_top_strip(ui_t *u, canvas_t *cv);
/* what the display driver needs to add the sweep itself, in the glass's pixels, and where the
 * moving layer stops on the right (the square map's panel slides in over it) */
typedef struct {
    int      beam;          /* the sweep turns: the radar look on the main page */
    int      cx, cy, r;     /* its middle and the range ring's radius */
    uint32_t prim, focus;   /* the theme's colours for its trail and its edge, 0x00RRGGBB */
    uint32_t turn_ms;       /* once round in this */
    int      clip_x;        /* the layer is cut off from here rightwards */
    int      any;           /* 0: nothing moves on this page */
    int      holes;         /* popups over the scope (the DEMO card, a toast, the update note): nothing moving shows */
    int      hx0[4], hy0[4], hx1[4], hy1[4];   /* in them, glass pixels, x1 and y1 past the end */
    int      x0, y0, x1, y1;   /* where it can be, glass pixels, x1 and y1 past the end: no need to look elsewhere */
    int      ring_only;     /* a finger turns the compass: the layer is the ring alone, made every frame */
} ui_top_t;
void ui_top_info(const ui_t *u, ui_top_t *t);
#endif

void ui_touch(ui_t *u, int down, int x, int y, int rx, int ry, uint32_t now, ui_cmd_t *cmd);
void ui_button(ui_t *u, int ev, ui_cmd_t *cmd);     /* BOOT: 1 short, 2 long */
void ui_start_calibration(ui_t *u);
void ui_set_theme(ui_t *u, int theme);
void touch_apply(const touch_cal_t *c, int rx, int ry, int *x, int *y);

/* the ground the screen wants for these settings (map.h): its size in texels, whether only the
 * disc of it, texels a km */
void  ui_ground_size(const fr_cfg_t *c, int *w, int *h, int *disc);
float ui_ground_kpx(const fr_cfg_t *c);

/* boards sold with different screens (the 2.8" CYD): the firmware sets the one it started,
 * the screen then says in the first seconds and on the touch alignment page that BOOT picks
 * the next one. NULL where there is only one */
extern const char *ui_screen_name;
/* a microsecond clock for prof[], set by the firmware. NULL: nothing is timed */
extern uint32_t (*ui_clock_us)(void);
/* this firmware's version, the first "## " line of CHANGES.md, set by the firmware. NULL: none */
extern const char *ui_version;
extern int ui_beta;                                  /* a beta build ("(beta)" on that line): ABOUT says BETA */
int ui_version_cmp(const char *a, const char *b);   /* <0 a is older, 0 the same, >0 newer: 0.9 < 0.10 */
int ui_update(const ui_t *u);                       /* the site has a newer one than this */
#define UI_SCREEN_PICK_MS 8000u            /* BOOT picks the screen this long after power on */
