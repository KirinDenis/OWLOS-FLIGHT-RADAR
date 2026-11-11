/*
 * preview - the flight radar's screen on PC, the same core/ code as the firmware. the demo sky
 * (core/demo.c) plays the free network: its JSON goes through the real parser. a script taps
 * like a finger and every page is saved as a PNG of what the panel shows (dithered RGB565).
 * usage: preview <out-dir>
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/sky.h"
#include "../../core/demo.h"
#include "../../core/ui.h"
#include "../../core/ui_int.h"

#include "png.inc"
#include "ground.inc"

static sky_t   sky;
static demo_t  demo;
static ui_t    ui;
static uint32_t t_ms;
static const char *g_dir;
static char    json[32768];

#define HOME_LAT 48.8945f              // Trencin, the board's home till the site sends one
#define HOME_LON 18.0444f

// the sky moves on: an answer every 5 s, like the firmware asks
static void fly(int seconds)
{
    for (int s = 0; s < seconds; s++) {
        t_ms += 1000u;
        if (t_ms % 5000u == 0) {
            int n = demo_json(&demo, HOME_LAT, HOME_LON, range_km(&ui.cfg), t_ms, json, sizeof json);
            static sky_reader_t rd;                  // in pieces, like the network hands it over
            sky_read_begin(&rd, &sky, 0, t_ms);
            for (int at = 0; at < n; at += 37) sky_read(&rd, json + at, n - at < 37 ? n - at : 37);
            if (sky_read_end(&rd, &sky) < 0) printf("parse failed\n");
            sky_forget(&sky, t_ms);
        }
    }
}

#define STRIP 24
static uint32_t strip_px[GFX_PW * STRIP];
static uint16_t frame565[GFX_PW * GFX_PH];

static void render(void)
{
    make_ground(&ui);
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    for (int y = 0; y < GFX_PH; y += STRIP) {
        canvas_t cv = { strip_px, y, y + STRIP <= GFX_PH ? STRIP : GFX_PH - y, 0 };
        ui_strip(&ui, &cv);
        gfx_pack565(&cv, frame565 + y * GFX_PW);
    }
}

static void shot(const char *name)
{
    render();
    const int zoom = GFX_SCALED ? 1 : 2, w = GFX_PW * zoom, h = GFX_PH * zoom;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t v = frame565[(y / zoom) * GFX_PW + x / zoom];
            if (!RADAR_RGB_BUS) v = (uint16_t)((v >> 8) | (v << 8));
            uint8_t *o = rgb + ((size_t)y * w + x) * 3;
            o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
            o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
            o[2] = (uint8_t)((v & 31) * 255 / 31);
        }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", g_dir, name);
    if (write_png(path, rgb, w, h)) printf("wrote %s\n", path);
    free(rgb);
}

static void apply(const ui_cmd_t *c) { (void)c; }

static void tap(int x, int y)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    for (int k = 0; k < 4; k++) ui_touch(&ui, 1, x, y, 0, 0, t_ms + k * 20u, &cmd);
    ui_touch(&ui, 0, x, y, 0, 0, t_ms + 100u, &cmd);
    apply(&cmd);
}

static void drag(int x0, int y0, int x1, int y1)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    for (int k = 0; k <= 10; k++) ui_touch(&ui, 1, x0 + (x1 - x0) * k / 10, y0 + (y1 - y0) * k / 10, 0, 0, t_ms + k * 20u, &cmd);
    ui_touch(&ui, 0, x1, y1, 0, 0, t_ms + 260u, &cmd);
}

// a finger put down, moved through points, lifted: like the touch task sends it, a sample every 20 ms
static void finger(const int (*pts)[2], int n, int lift)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    for (int k = 0; k < n; k++) { ui_touch(&ui, 1, pts[k][0], pts[k][1], 0, 0, t_ms, &cmd); t_ms += 20u; }
    if (lift) ui_touch(&ui, 0, pts[n - 1][0], pts[n - 1][1], 0, 0, t_ms, &cmd);
}

// along a circle round the scope centre, from angle a0 to a1 (degrees, 0 up, clockwise), radius r0 to r1
static int path(int (*pts)[2], float a0, float a1, float r0, float r1, int n)
{
    for (int k = 0; k < n; k++) {
        float f = (float)k / (float)(n - 1), a = (a0 + (a1 - a0) * f) * D2R, r = r0 + (r1 - r0) * f;
        pts[k][0] = (int)lroundf(SCX + r * sinf(a));
        pts[k][1] = (int)lroundf(SCY - r * cosf(a));
    }
    return n;
}

static int find(const char *flight)
{
    for (int i = 0; i < sky.n; i++) if (!strcmp(sky.p[i].flight, flight)) return i;
    return -1;
}

static void pick(const char *flight)
{
    int i = find(flight);
    ui.sel = i >= 0 ? sky.p[i].icao : 0;
    if (i < 0) printf("  %s is not in the sky now\n", flight);
}

// HUE LIST SET: the panel's buttons, or on the round glass the row a tap on nothing brings up
static void press(int i)
{
#if RADAR_ROUND
    ui.menu_until = t_ms + ROUND_MENU_MS;
    int m = i ? i + 1 : 0;                           // the row has the look's button second
    tap(menu_x(m) + MENU_W / 2, MENU_Y + MENU_H / 2);
#else
    tap(TX - 3 + i * (BTN_W + 3) + BTN_W / 2, BTN_Y + 10);
#endif
}

// the bottom bar of a page: n buttons, press the i-th
static void press_bar(int i, int n)
{
    int w = (BAR_R - BAR_L - 8 - (n - 1) * 4) / n;
    tap(BAR_L + 4 + i * (w + 4) + w / 2, BAR_Y + BAR_H / 2);
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : ".";
    setvbuf(stdout, NULL, _IONBF, 0);                // what it said reaches the screen even if it falls
    crc_init();
    ui_init(&ui, 0);
    ui_screen_name = "ILI9341 V2";                  // the 2.8" CYD offers its other screens; a new one starts as v2
    ui_version = FLIGHT_VERSION;                     // CHANGES.md's, BUILD.BAT passes it
    sky_init(&sky, HOME_LAT, HOME_LON);
    demo_init(&demo, range_km(&ui.cfg), 82);
    ui.net.demo = 1;

    fly(6);                                          // power on: the first planes, BOOT picks the screen
    shot("00-boot");
    fly(84);                                         // trails grow
    shot("01-demo-sky");
    press(1);                                        // LIST
    shot("04-list");
    press_bar(0, 2);                                 // ZOOM in the list: next range
    press_bar(1, 2);                                 // BACK

    // live: what it looks like with settings and WiFi
    ui.net.demo = 0;
    ui.net.configured = ui.net.wifi = 1;
    snprintf(ui.net.ssid, sizeof ui.net.ssid, "Kirin82");
    snprintf(ui.net.ip, sizeof ui.net.ip, "192.168.1.57");
    snprintf(ui.net.server, sizeof ui.net.server, "ADSB.LOL");
    ui.cfg.range = 50;
    fly(30);
    shot("02-live");
    ui.opt.vectors = ui.opt.beam_step = ui.opt.clock = ui.opt.block = ui.opt.autopilot = 1;   // the radar's options
    ui.net.utc = 14 * 3600 + 32 * 60;
    shot("42-radar-options");
    ui.opt.vectors = ui.opt.beam_step = ui.opt.clock = ui.opt.block = ui.opt.autopilot = 0;
    pick("OWL77H");
    shot("03-picked-heavy");
    pick("OWL205");
    shot("05-picked-climbing");
    ui.sel = 0;
#if !RADAR_ROUND && RADAR_HAS_TOUCH
    tap(RDOTS_X + RDOTS_W / 2, LOOK_Y + LOOK_H / 2);    // the radar's three dots: the panel away, the scope in the middle
    printf("radar after the dots: %s\n", ui.bare ? "no panel" : "with its panel");
    shot("43-radar-bare");
    pick("OWL77H");                                  // a picked plane brings the panel back while it is picked
    shot("44-radar-bare-picked");
    tap(RDOTS_X + RDOTS_W / 2, LOOK_Y + LOOK_H / 2);    // the dots, lit: what is shown goes, and the plane is let go
    printf("the dots while picked: %s, %s\n", ui.bare ? "no panel" : "with its panel", ui.sel ? "still picked" : "let go");
    tap(RDOTS_X + RDOTS_W / 2, LOOK_Y + LOOK_H / 2);    // and again: the panel back, kept
    printf("radar after the dots again: %s\n", ui.bare ? "no panel" : "with its panel");
#endif

    ui.cfg.up = 90;                                  // the screen stands facing east
    fly(5);
    shot("06-east-up");
    ui.cfg.up = 0;

    ui.cfg.range = 25;
    fly(10);
    shot("07-range-25");
    ui.cfg.range = 50;

    demo.emergency = 1;
    fly(10);
    pick("OWL412");
    shot("08-emergency");
    demo.emergency = 0;
    ui.sel = 0;

    // FR24: its own format, with where the flight goes
    {
        static char fr[] = "{\"data\":[{\"fr24_id\":\"x\",\"flight\":\"OW123\",\"callsign\":\"OWL123\",\"lat\":48.30,\"lon\":17.30,"
                           "\"track\":95,\"alt\":31000,\"gspeed\":447,\"vspeed\":-64,\"squawk\":\"6011\",\"timestamp\":\"t\","
                           "\"source\":\"ADSB\",\"hex\":\"A0C123\",\"type\":\"A21N\",\"reg\":\"OW-FRA\",\"painted_as\":\"OWL\","
                           "\"operating_as\":\"OWL\",\"orig_iata\":\"VIE\",\"orig_icao\":\"LOWW\",\"dest_iata\":\"LHR\","
                           "\"dest_icao\":\"EGLL\",\"eta\":\"t\"}]}";
        int n = sky_parse_fr24(&sky, fr, (int)strlen(fr), t_ms);
        printf("FR24 sample: %d plane(s)\n", n);
        snprintf(ui.net.server, sizeof ui.net.server, "FLIGHTRADAR24");
        pick("OWL123");
        shot("09-fr24-route");
        ui.sel = 0;
        snprintf(ui.net.server, sizeof ui.net.server, "ADSB.LOL");
    }

    // the looks: the flat map with its grid, then OpenStreetMap's own colours, then the radar close up
#if RADAR_ROUND
    ui.menu_until = t_ms + ROUND_MENU_MS;
    tap(menu_x(1) + MENU_W / 2, MENU_Y + MENU_H / 2);   // the look's button in the row
    ui.menu_until = 0;
#else
    tap(PX - 30, 8);                                 // the look's button over the scope
#endif
    printf("look after a tap: %s\n", LOOK_NAME[ui.cfg.look]);
    ui.cfg.look = LOOK_MAP;
    fly(5);
    shot("20-map");
    ui.cfg.range = 10;
    fly(5);
    shot("21-map-10");
    ui.cfg.look = LOOK_CLASSIC;
    shot("22-classic-10");
    ui.cfg.range = 50;
    shot("23-classic");
#if RADAR_ROUND
    ui.menu_until = t_ms + ROUND_MENU_MS;            // the classic look's menu: paper buttons
    shot("28-classic-menu");
    ui.menu_until = 0;
#endif
#if !RADAR_ROUND
    tap(GFX_W - 18, 12);                             // the three dots: the panel slides in over the map
    printf("panel after the dots: %s\n", ui.panel_open ? "in" : "out");
    shot("26-panel");
    tap(GFX_W - 18, 12);                             // and out again
    pick("OWL77H");                                  // a plane picked: the panel comes by itself
    shot("27-panel-plane");
    ui.sel = 0;
#endif
    ui.cfg.up = 30;
    shot("24-classic-turned");
    ui.cfg.up = 0;
    ui.cfg.look = LOOK_RADAR;
    ui.cfg.range = 10;
    shot("25-radar-10");
    ui.cfg.range = 50;
    fly(5);

    {
        // a finger on the rim turns the map, inside the scope it zooms against home
        static int pts[24][2];
        int n = path(pts, 0, 60, SR - 8, SR - 8, 16);
        finger(pts, n, 0);
        shot("14-turning");
        finger(pts + n - 1, 1, 1);
        printf("turned: up %d\n", ui.cfg.up);
        shot("15-turned");
        n = path(pts, 60, 2, SR - 8, SR - 8, 16);    // and back near north: it snaps to it
        finger(pts, n, 1);
        printf("back: up %d\n", ui.cfg.up);
        n = path(pts, 135, 135, PR * 0.45f, PR * 0.9f, 14);   // spread away from home: zoom in
        finger(pts, n, 0);
        shot("16-zooming");
        finger(pts + n - 1, 1, 1);
        printf("zoomed: range %.1f\n", (double)ui.cfg.range);
        fly(5);
        shot("17-zoomed");
        n = path(pts, 135, 135, PR * 0.9f, PR * 0.28f, 14);   // pinch toward home: out again
        finger(pts, n, 1);
        printf("zoomed out: range %.1f\n", (double)ui.cfg.range);
        ui.cfg.range = 50;
        press(0);                                    // HUE: the next colours
        shot("18-hue");
        ui_set_theme(&ui, 0);
        ui.cfg.theme = 0;
        fly(5);
    }

#if RADAR_ROUND
    ui.menu_until = t_ms + ROUND_MENU_MS;            // a tap on nothing: the row the panel has elsewhere
    shot("19-menu");
    ui.menu_until = 0;
#endif
    press(2);                                        // SET
    shot("10-setup");
    drag(120, 180, 120, 40);
    drag(120, 180, 120, 40);
    shot("11-setup-more");
    const int tabs = (RADAR_ROUND ? 2 : 3) + (RADAR_HAS_LED ? 1 : 0);   // the bar: VIEW DATA (RADAR) (LED) BACK
    press_bar(1, tabs + 1);
    shot("10-setup-data");
#if !RADAR_ROUND
    press_bar(2, tabs + 1);
    shot("10-setup-radar");
#endif
#if RADAR_HAS_LED
    press_bar(3, tabs + 1);
    shot("10-setup-led");
#endif
    press_bar(0, tabs + 1);                          // VIEW again, as it was
    press_bar(tabs, tabs + 1);                       // BACK

    ui.net.wifi = 0;
    shot("12-no-wifi");

    // the WiFi that is set is not there: the demo sky again, and it says why in big letters
    ui.net.demo = 1;
    fly(5);
    shot("30-demo-no-wifi");
#if UI_WIFI_PAGE
    {
        // SET UP WIFI: the networks it hears, then a password typed on the keyboard
        static const ap_t APS[] = {
            { "Kirin82", -48, 0 }, { "OWL-GUEST", -61, 1 }, { "Office-5G", -70, 0 }, { "Cafe Corner", -77, 0, 1 },
            { "DIRECT-7B-Printer", -83, 0 }, { "FreeWiFi Station", -88, 1 },
        };
        const demo_box_t d = demo_box(&ui);
        tap(d.bx + d.bw / 2, d.by + d.bh / 2);
        printf("demo card's button: %s\n", ui.view == VIEW_WIFI ? "the WiFi page" : "missed");
        ui.net.scan = SCAN_RUNNING;
        shot("31-wifi-looking");
        memcpy(ui.net.aps, APS, sizeof APS);
        ui.net.n_aps = (uint8_t)(sizeof APS / sizeof APS[0]);
        ui.net.scan = SCAN_DONE;
        shot("32-wifi-networks");
        tap(GFX_W / 2, LIST_Y0 + 2 * NET_H + NET_H / 2);  // Office-5G
        printf("picked %s: %s\n", ui.wifi_ssid, ui.view == VIEW_KEYS ? "the keyboard" : "no keyboard");
        static const int TYPE[][2] = { { 3, 0 }, { 1, 8 }, { 1, 1 }, { 2, 8 }, { 0, 8 }, { 0, 1 } };   // shift O w l 9 2: row, key
        for (int k = 0; k < 6; k++) tap(TYPE[k][1] * KB_CW + KB_CW / 2, KB_Y0 + TYPE[k][0] * KB_RH + KB_RH / 2);
        printf("typed %d letters\n", (int)strlen(ui.wifi_pass));
        ui.kb_typed = t_ms;                          // the last one still shows
        shot("33-wifi-password");
        tap(9 * KB_CW + KB_CW / 2, KB_Y0 + 3 * KB_RH + KB_RH / 2);   // #+=
        shot("34-wifi-signs");
        press_bar(0, 3);                             // SHOW
        shot("35-wifi-shown");
        press_bar(1, 3);                             // CANCEL: the password goes
        printf("after cancel: %s, %d letters kept\n", ui.view == VIEW_WIFI ? "the networks" : "?", (int)strlen(ui.wifi_pass));
        tap(GFX_W / 2, LIST_Y0 + 3 * NET_H + NET_H / 2);  // Cafe Corner, one the board remembers
        printf("picked %s, saved: %s\n", ui.wifi_ssid, ui.view == VIEW_MAIN ? "joins, no keyboard" : "the keyboard");
        ui.view = VIEW_WIFI;
        ui.toast_until = 0;                          // its JOINING THE WIFI would stay over the next pages' shots
        press_bar(1, 2);                             // BACK
        ui.net.n_aps = 0;
        ui.net.scan = SCAN_NONE;

        // SET, HOME: the same keyboard for a new home, a town typed
        home_open(&ui);
        static const int TOWN[][2] = { { 3, 0 }, { 1, 4 }, { 1, 3 }, { 1, 2 }, { 3, 6 }, { 3, 3 }, { 1, 7 }, { 3, 6 } };   // shift T r e n c i n
        for (int k = 0; k < 8; k++) tap(TOWN[k][1] * KB_CW + KB_CW / 2, KB_Y0 + TOWN[k][0] * KB_RH + KB_RH / 2);
        printf("home typed: %s\n", ui.home_text);
        shot("36-home");
        press_bar(0, 2);                             // CANCEL: back to SET
        press_bar(tabs, tabs + 1);                   // BACK

        // SET, WATCH: the planes to follow, on the same keyboard
        watch_set(&ui.watch, "EZY12AB");
        watch_open(&ui);
        snprintf(ui.watch_text, sizeof ui.watch_text, "EZY12AB, G-EUPT, 4CA7B4");   // as if typed
        shot("37-watch");
        press_bar(0, 2);                             // CANCEL
        press_bar(tabs, tabs + 1);                   // BACK
        watch_set(&ui.watch, "");
    }
#endif
    ui.net.demo = 0;
    ui.net.wifi = 1;

    for (int k = 0; k < UI_THEMES; k++) {
        char n[24];
        ui_set_theme(&ui, k);
        snprintf(n, sizeof n, "theme-%d", k);
        shot(n);
    }
    ui_set_theme(&ui, 0);

    // ABOUT: this version, the newest; then the site has a newer one, the note says so, a tap opens it
    snprintf(ui.newest, sizeof ui.newest, "%s", ui_version);
    about_open(&ui);
    shot("38-about");
    press_bar(0, 1);                                 // BACK
    {
        const char *dot = strrchr(ui_version, '.');  // the next one: the last number up by one
        int head = dot ? (int)(dot - ui_version) + 1 : 0;
        snprintf(ui.newest, sizeof ui.newest, "%.*s%d", head, ui_version, atoi(ui_version + head) + 1);
    }
    fly(2);
    shot("40-new-version");
    note_box_t nb;
    if (update_note_box(&ui, &nb)) tap(nb.x + nb.w / 2, nb.y + nb.h / 2);
    printf("the note %s: %s\n", ui.newest, ui.view == VIEW_ABOUT ? "ABOUT" : "missed");
    shot("39-about-new");
    press_bar(0, 1);                                 // BACK
#if !RADAR_ROUND
    ui.cfg.look = LOOK_MAP;                          // over the square map: a card under the town's
    fly(2);
    shot("41-new-version-map");
    ui.cfg.look = LOOK_RADAR;
#endif
    ui.newest[0] = 0;

    ui_start_calibration(&ui);
    shot("13-touch-align");
    printf("sky: %d planes, %d in the last answer\n", sky.n, sky.total);
    return 0;
}
