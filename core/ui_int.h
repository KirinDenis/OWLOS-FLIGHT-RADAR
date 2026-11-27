// ui internals, shared by ui.c, ui_scope.c and ui_pages.c
#pragma once
#include "ui.h"
#include "font.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- themes: the WiFi radar's five. every colour has one meaning, the same in all five ---
 * prim   the instrument: rings, rim, the high middle of the sky
 * quiet  the instrument, quieter: ticks, grid, frames
 * focus  what you read first: north, the picked plane, the highest planes (cruise)
 * dim    secondary text, planes on the ground
 * low    planes under 10 000 ft: climbing out, coming in to land
 * alarm  emergency squawks (7500 7600 7700) and errors
 * home   the pin on home, and nothing else: so it differs from theme to theme */
typedef struct {
    const char *name;
    uint32_t bg0, bg1, glass, panel;
    uint32_t prim, quiet, focus, dim;
    uint32_t low, alarm, home;
} theme_t;

extern const theme_t UI_THEME[UI_THEMES];
extern const theme_t *TH;

#define C_BG0    (TH->bg0)
#define C_BG1    (TH->bg1)
#define C_GLASS  (TH->glass)
#define C_PANEL  (TH->panel)
#define C_PRIM   (TH->prim)
#define C_QUIET  (TH->quiet)
#define C_FOCUS  (TH->focus)
#define C_DIM    (TH->dim)
#define C_LOW    (TH->low)
#define C_ALARM  (TH->alarm)
#define C_HOME   (TH->home)

/* --- layout: the scope on the left, the panel on the right, like the WiFi radar, sized from the
 * screen. 320x240: scope at 114,120 r 108, panel at 228, 91 wide. 480x320: 154,160 r 148, panel
 * at 308, 171 wide. the round glass has no room for a panel: the scope is all of it --- */
#if RADAR_ROUND
#define SR       (GFX_W / 2 - 12)
#define SCX      (GFX_W / 2)
#define PX       GFX_W        /* no panel */
#define PW       0
#else
#define SR       (GFX_H / 2 - 12) /* the rim: ticks go in from it */
#define SCX      (SR + 6)
#define PX       (2 * SCX)    /* panel */
#define PW       (GFX_W - PX - 1)
#endif
#define SCY      (GFX_H / 2)
#define RL       (SR - 12)    /* the compass letters and numbers, in the band between rim and ring */
#define PR       (SR - 20)    /* the range ring: range_km lands here */
#define RQ       (SR + 6)     /* the disc and its glow fit in this radius */
/* the same on the glass (config.h), for what is drawn pixel by pixel there: the disc, the map */
#define G_SR     GFX_Z(SR)
#define G_PR     GFX_Z(PR)
#define G_RQ     GFX_Z(RQ)
#define G_SCX    GFX_Z(SCX)
#define G_SCY    GFX_Z(SCY)
#define TX       (PX + 6)
#define TX2      (PX + PW - 5)
#define BTN_Y    (GFX_H - 38)
#define BTN_H    32
#define BTN_W    ((PW - 13) / 3)
/* the look's button: top right of the scope, or of the square map. with touch an arrow at each end, each half a
 * way round the looks (ui.c look_button): 14 wider, to the left, so the seven-letter names keep their size. the
 * scaled screens write the name in a smooth font whose letters stand wider than the pixel font's: 18 there */
#define LOOK_ARROW (RADAR_HAS_TOUCH ? (GFX_SCALED ? 9 : 7) : 0)
#define LOOK_W   (62 + 2 * LOOK_ARROW)
#define LOOK_H   20
#define LOOK_Y   2
#define DOTS_W   24           /* three dots, the panel in and out: the square map's, the radar's on a wide panel */
/* the panel's letters go with its width, not with one board or another: the 2.8"'s 91 pixels take the
 * small ones, the wide panels (the 3.5" and the RPi LCD, the 4.3", the 5" and 7", the Tab5: 171 to 260)
 * the next size up and more in a row. its rows go with the height too (ui_scope.c) */
#define PANEL_WIDE (PW >= 150)

/* setup and list: the whole screen. on the round glass the corners are not there, so the pages
 * keep to the square inside the circle (PG_L..PG_R), rows are shorter and say less */
#if RADAR_ROUND
#define HEAD_H   38           /* the title sits low, where the glass is wide enough */
#define PG_L     36
#define PG_R     (GFX_W - 36)
#define ROW_H    32
#define LIST_Y0  (HEAD_H + 2)
#define LIST_Y1  (GFX_H - 44)
#define BAR_Y    (GFX_H - 42)
#define BAR_H    24
#define BAR_L    64
#define BAR_R    (GFX_W - 64)
#else
#define HEAD_H   18
#define PG_L     0
#define PG_R     GFX_W
#define ROW_H    40
#define LIST_Y0  (HEAD_H + 2)
#define LIST_Y1  (GFX_H - 38)
#define BAR_Y    (GFX_H - 34)
#define BAR_H    31
#define BAR_L    0
#define BAR_R    GFX_W
#endif
#define LIST_ROW 22

enum {
    HIT_NONE, HIT_SCOPE, HIT_ROWNEAR, HIT_PANEL, HIT_LOOK, HIT_DOTS,
    HIT_WIFI,                      /* the demo card's SET UP WIFI */
    HIT_NEWS,                      /* the note that the site has a newer version: ABOUT */
    HIT_GEAR,                      /* the radar's options */
    HIT_ALERT,                     /* a weather warning's chip: the WEATHER look */
    HIT_BTN = 10,                  /* + panel button index */
    HIT_BAR = 20,                  /* + bottom bar button index */
    HIT_WXPAGE = 30,               /* the WEATHER look's dial or graph: its next page */
    HIT_LOOK_BACK = 31,            /* the look's button, its left half: the look before. HIT_LOOK, the right half: the next */
    HIT_ROW = 40,                  /* + list row */
    HIT_SDEC = 100, HIT_SINC = 160, HIT_SBTN = 220,  /* setup rows: 60 at most (S_ROWS, ui_pages.c) */
    HIT_NET = 280,                 /* + a network on the WiFi page */
    HIT_KEY = 300                  /* + a key of the keyboard */
};

/* the WiFi page with its keyboard: square screens with touch. the round glass is too small for
 * a keyboard and the RPi LCD has no touch: the site sends the WiFi there */
#ifndef RADAR_WEB
#define RADAR_WEB 0                                    /* web/: the screen in a page, the board has the WiFi */
#endif
#define UI_WIFI_PAGE (RADAR_HAS_TOUCH && !RADAR_ROUND && !RADAR_WEB)
/* the LED's events lit on the screen (ui_t.alarm): every board's own screen but paper, whose picture cannot
 * blink, and the site's radar, which has no events of its own. SET has the events' rows where the LED or the
 * screen shows them: LED's tab, ALARM's where there is no LED, the round's under DATA */
#define UI_SCREEN_ALARM (!RADAR_EPAPER && !RADAR_WEB)
#define UI_EVENTS (RADAR_HAS_LED || UI_SCREEN_ALARM)

#define D2R (3.14159265f / 180.0f)
#define BEAM_TURN_MS 6000u    /* the sweep goes round once in this */
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float wrap360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }

/* ranges: a finger zooms to any value, letting go snaps to the nearest of these, +/- step along
 * them, BOOT jumps through the short list */
float range_snap(float v);
float range_step(float v, int dir);
extern const char *const SERVER_NAME[FREE_SERVERS];
extern const char *const LOOK_NAME[LOOKS];
int ui_look_after(int look);                         /* MENU's next look: paper skips the flat MAP */
int ui_look_before(int look);                        /* and EXIT's, the one before */
/* the flat looks on a square screen: a map over the whole left side, north up, no compass round
 * it. the round glass keeps its disc in every look */
static inline int square_look(const ui_t *u) { return !RADAR_ROUND && u->cfg.look != LOOK_RADAR && u->cfg.look != LOOK_WEATHER; }
static inline int view_up(const ui_t *u) { return square_look(u) ? 0 : u->cfg.up; }   /* what is at the top */
/* the square map fills the screen, home in its middle. the panel slides in over its right side
 * from the three dots, and by itself while a plane is picked. the radar has the panel beside the
 * scope; its three dots, in the panel's corner, take it away and the scope goes to the middle of
 * the screen. a picked plane brings it back for as long as it is picked. the weather's dots are the
 * radar's, kept the same way: without the panel its dial stands in the middle, now's numbers round it */
static inline int panel_shown(const ui_t *u)
{
    if (u->t_panel) return u->t_panel - 1;          /* a finger turns or zooms: nothing moves under it till it lifts */
    if (u->cfg.look == LOOK_WEATHER) return !RADAR_ROUND && !u->bare;
    return square_look(u) ? u->panel_open || u->sel : RADAR_ROUND || !u->bare || u->sel;
}
static inline int view_cx(const ui_t *u) { return square_look(u) || !panel_shown(u) ? GFX_W / 2 : SCX; }
static inline int view_right(const ui_t *u) { return !panel_shown(u) ? GFX_W : PX - 2; }
/* the scope's middle on the glass: G_SCX beside the panel, else moved by the layout's pixels in the
 * glass's, rounded (GFX_Z alone floors, 3/4 of a pixel off on the 5"). the disc and the grid behind it */
static inline int scope_glass_cx(const ui_t *u) { return G_SCX + (GFX_Z(2 * (view_cx(u) - SCX)) + 1) / 2; }
/* the radar's three dots: the top right of the screen, the corner of the panel or where it was */
#define RDOTS_W (PANEL_WIDE ? DOTS_W : 18)    /* the 2.8"'s panel is narrow: its title needs the room */
#define RDOTS_X (GFX_W - 6 - RDOTS_W)
static inline int look_w(const ui_t *u) { return square_look(u) || u->cfg.look == LOOK_WEATHER ? LOOK_W : LOOK_W - 10; }   /* the radar's corner is narrower */
static inline int look_x(const ui_t *u)
{
    if (square_look(u)) return view_right(u) - 6 - DOTS_W - 4 - LOOK_W;
    return panel_shown(u) ? PX - 9 - look_w(u) : RDOTS_X - 4 - look_w(u);
}

/* helpers (ui.c) */
void ui_lap(ui_t *u, const canvas_t *cv, int layer);  /* timing of the layers, see ui_t.prof */
void chamfer_box(canvas_t *cv, int x, int y, int w, int h, uint32_t fill, int fill_a, uint32_t line, int line_a);
void button(canvas_t *cv, int x, int y, int w, int h, const char *label, int state);   /* 0 normal, 1 lit, 2 pressed */
void button_font(canvas_t *cv, int x, int y, int w, int h, const char *label, int state, const font_t *f);
/* the look's button with its two arrows, and which half x is in (HIT_LOOK_BACK or HIT_LOOK). paper: the light map's */
void look_button(const ui_t *u, canvas_t *cv, const char *name, int paper);
int  look_hit(const ui_t *u, int x);
/* what an alarm (ui_t.alarm) lights now, a face and its letters, the look's button and the round glass's rim. 0: none */
int  alarm_colours(const ui_t *u, int paper, uint32_t *fill, uint32_t *ink);
/* the classic look: things on the light map are light, rounded, inked dark */
#define CL_PAPER RGB(250, 250, 247)
#define CL_RIM   RGB(176, 181, 190)
#define CL_INK   RGB(38, 44, 60)
#define CL_DIM   RGB(92, 99, 116)
#define CL_BLUE  RGB(32, 88, 186)
#define CL_ALARM RGB(196, 36, 44)
void round_box(canvas_t *cv, int x, int y, int w, int h, int r, uint32_t c, int a);
void paper_card(canvas_t *cv, int x, int y, int w, int h);
void map_button(canvas_t *cv, int x, int y, int w, int h, const char *label, int state);
void bar(ui_t *u, canvas_t *cv, const char *const *labels, int n, int lit);
int  bar_hit(int x, int y, int n);
void page_header(canvas_t *cv, const char *title, const char *right, uint32_t col);
void draw_background(canvas_t *cv, int grid);     /* grid 0: just the gradient, for pages of text */
void toast(ui_t *u, const char *s);
int  in_box(int x, int y, int bx, int by, int bw, int bh);
const plane_t *sel_plane(const ui_t *u);
int watched(const ui_t *u, const plane_t *p);         /* on the watch list */
uint32_t alt_colour(const ui_t *u, const plane_t *p);
void fmt_alt(const ui_t *u, const plane_t *p, char *buf, int sz, int short_form);
void fmt_height(const ui_t *u, int feet, char *buf, int sz);   /* in short: FL350, 4200, or metres */
void fmt_dist(const ui_t *u, float km, char *buf, int sz);
void sky_status(const ui_t *u, char *buf, int sz, int *bad);   /* where the planes come from and how fresh (ui_scope.c) */
const char *compass8(float deg);

/* the scope and the panel (ui_scope.c) */
int  scope_init(void);                               /* its tables, 0 = no memory */
#define ROUND_MENU_MS 5000u                          /* round glass: the HUE LIST SET row stays this long */
#if RADAR_ROUND
/* the round glass: the card of the picked plane and the HUE LIST SET row, both under home */
#define CARD_W   150
#define CARD_Y   (SCY + 22)
#define CARD_H   56
#define MENU_W   44
#define MENU_Y   (SCY + 34)
#define MENU_H   26
#define MENU_N   4            /* HUE, the look, LIST, SET */
static inline int menu_x(int i) { return SCX - (MENU_N * MENU_W + (MENU_N - 1) * 4) / 2 + i * (MENU_W + 4); }
#endif
void boot_banner(ui_t *u, canvas_t *cv);            /* the first seconds: the name, and which screen */
void scope_frame(ui_t *u);
void main_draw(ui_t *u, canvas_t *cv);
int  range_can_step(const ui_t *u, int dir);          /* the dial's short list (ui.c) goes on nearer (-1) or further (1) */
#if RADAR_EPAPER
/* the e-paper's own main screen (ui_paper.c): the CLASSIC look as a printed chart, the weather's first page */
int  paper_shown(const ui_t *u);                     /* this frame is the paper's own */
void paper_frame(ui_t *u);                           /* once a frame, after scope_frame: labels, the sheet's news */
void paper_draw(ui_t *u, canvas_t *cv);              /* a strip of it */
#endif
#if RADAR_OVERLAY
void main_top(ui_t *u, canvas_t *cv);
#define LAYER_BG  (cv->layer == UI_LAYER_BG)    /* drawing the frame in memory: no moving things */
#define LAYER_TOP (cv->layer == UI_LAYER_TOP)   /* drawing the moving things alone */
#else
#define LAYER_BG  0
#define LAYER_TOP 0
#endif
int  main_hit(const ui_t *u, int x, int y);
void panel_dots(ui_t *u, canvas_t *cv, int x, int y, int w);   /* the three dots: the panel in and out */
int  scope_pick(const ui_t *u, int x, int y);      /* plane index or -1 */
void silhouette(canvas_t *cv, int kind, float x, float y, float heading, float size, uint32_t c, int a);

/* the other pages (ui_pages.c) */
void list_draw(ui_t *u, canvas_t *cv);
int  list_hit(const ui_t *u, int x, int y);
void setup_draw(ui_t *u, canvas_t *cv);
int  setup_hit(const ui_t *u, int x, int y);
int  setup_rows(const ui_t *u);                      /* in the tab shown */
int  setup_repeats(int hit);
void setup_act(ui_t *u, int hit, int longp, ui_cmd_t *cmd);
void setup_open_radar(ui_t *u);                      /* the gear: SET at the radar's tab */
void cal_draw(ui_t *u, canvas_t *cv);

/* the WEATHER look (ui_weather.c): the next 24 hours on the dial, the pressure and the temperature behind it */
void weather_frame(ui_t *u);                         /* each frame, before the strips: the dial just up, its tables, the frame's things */
void weather_draw(ui_t *u, canvas_t *cv);            /* a strip: reads the tables only, on either core */
void weather_close(void);                            /* another look: its tables go back to the heap */
extern const char *const WX_PAGE_NAME[WX_PAGES];     /* what the toast says when a page comes */
void next_wx_page(ui_t *u);                          /* a tap on the dial or the graph, or BOOT: the next page */
void step_wx_page(ui_t *u, int d);                   /* the page after (d 1) or before (d -1): the paper's dial */
/* the colour dial with the printed dial's column beside it in place of the panel (ui_weather.c wx_hud): the 480x320
 * and the 320x240 screens */
#define WX_HUD (RADAR_DISPLAY == DISPLAY_ST7796_480x320 || RADAR_DISPLAY == DISPLAY_ILI9486_480x320 || \
                RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || RADAR_DISPLAY == DISPLAY_ES3C28P_320x240 || \
                RADAR_DISPLAY == DISPLAY_NMCYDC5_320x240 || RADAR_DISPLAY == DISPLAY_CROWADV28_320x240 || \
                RADAR_DISPLAY == DISPLAY_2432S032_320x240)
#if RADAR_EPAPER
int  weather_sheet_up(const ui_t *u);                /* the weather's first page is the printed dial (paper_style 1) */
int  weather_paper_page(const ui_t *u);              /* the weather is the paper's own, all three pages (the 4.2"'s) */
uint32_t weather_stamp(const ui_t *u);               /* what the printed weather shows: the forecast and the hour. 0: none */
void weather_paper(ui_t *u, canvas_t *cv);           /* a strip of it */
#endif
enum { WXU_TEMP, WXU_PRES, WXU_WIND, WXU_RAIN };     /* the weather's units, SET's UNITS rows */
int  wx_unit(const ui_t *u, int which);              /* the one in use: the setting, or home's country's (ui.h TEMP_* ...) */
void wx_unit_text(const ui_t *u, int which, char *b, int sz);   /* SET's value: KM/H, or AUTO KM/H */
void wx_unit_next(ui_t *u, int which);               /* SET's tap: the next one, AUTO after the last */

/* the WiFi page and the keyboard (ui_wifi.c) */
#if UI_WIFI_PAGE
void wifi_open(ui_t *u, ui_cmd_t *cmd);            /* the networks, and look for them again */
void wifi_draw(ui_t *u, canvas_t *cv);
int  wifi_hit(const ui_t *u, int x, int y);
int  wifi_rows(const ui_t *u);
void wifi_act(ui_t *u, int hit, ui_cmd_t *cmd);
void home_open(ui_t *u);                             /* the keyboard, for a new home */
void watch_open(ui_t *u);                            /* the keyboard, for the watch list */
void keys_draw(ui_t *u, canvas_t *cv);
int  keys_hit(const ui_t *u, int x, int y);
int  keys_repeats(const ui_t *u, int hit);           /* the key that rubs out goes on while held */
void keys_act(ui_t *u, int hit, ui_cmd_t *cmd);
#else
/* no WiFi page here: nothing that can be reached calls these, and ui_wifi.c builds none of its own */
static inline void wifi_open(ui_t *u, ui_cmd_t *cmd) { (void)u; (void)cmd; }
static inline void wifi_draw(ui_t *u, canvas_t *cv) { (void)u; (void)cv; }
static inline int  wifi_hit(const ui_t *u, int x, int y) { (void)u; (void)x; (void)y; return HIT_NONE; }
static inline int  wifi_rows(const ui_t *u) { (void)u; return 0; }
static inline void wifi_act(ui_t *u, int hit, ui_cmd_t *cmd) { (void)u; (void)hit; (void)cmd; }
static inline void home_open(ui_t *u) { (void)u; }
static inline void watch_open(ui_t *u) { (void)u; }
static inline void keys_draw(ui_t *u, canvas_t *cv) { (void)u; (void)cv; }
static inline int  keys_hit(const ui_t *u, int x, int y) { (void)u; (void)x; (void)y; return HIT_NONE; }
static inline int  keys_repeats(const ui_t *u, int hit) { (void)u; (void)hit; return 0; }
static inline void keys_act(ui_t *u, int hit, ui_cmd_t *cmd) { (void)u; (void)hit; (void)cmd; }
#endif
#define NET_H (GFX_H >= 320 ? 38 : 30)                /* a network's row */
#define KB_Y0 (HEAD_H + (GFX_H >= 320 ? 50 : 42))     /* the keyboard's first row of keys */
#define KB_RH ((BAR_Y - 4 - KB_Y0) / 4)               /* a row's height, four rows */
#define KB_CW (GFX_W / 10)                             /* a key's width, ten a row */
#if !RADAR_ROUND
/* the demo card under home, and its SET UP WIFI (ui_scope.c) */
typedef struct { int x, y, w, h, bx, by, bw, bh, sc; } demo_box_t;
demo_box_t demo_box(const ui_t *u);
#endif

/* ABOUT and the note on the main screen that the site has a newer version (ui_about.c) */
void about_open(ui_t *u);                            /* from the view shown, BACK goes back there */
void about_draw(ui_t *u, canvas_t *cv);
int  about_hit(const ui_t *u, int x, int y);
void about_act(ui_t *u, int hit);
typedef struct { int x, y, w, h; } note_box_t;
int  update_note_box(const ui_t *u, note_box_t *b);  /* where the note is now, 0: not shown */
void update_note(ui_t *u, canvas_t *cv);             /* the radar panel's title, else a card */
void update_text(const ui_t *u, char *buf, int sz, int short_form);   /* NEW VERSION 0.4, short NEW 0.4 */

/* the weather's warnings on every look: their words and colours (ui_weather.c), the chip in the
 * corner the note has (ui_scope.c; the note moves under it). the WEATHER look says them in its panel */
const char *alert_word(const wx_alert_t *a, int k);   /* STORM, THUNDER, PRESSURE, AIR, RADIATION */
void alert_said(const wx_alert_t *a, int k, char *b, int sz);   /* what the toast says when it comes */
uint32_t alert_colour(int level);                     /* amber, red */
int  alert_box(const ui_t *u, note_box_t *b);         /* where the chip is now, 0: not shown */
void alert_chip(ui_t *u, canvas_t *cv);

#define CAL_POINTS 5
extern int CAL_PT[CAL_POINTS][2];
