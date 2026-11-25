/* film5 - the screens of the 0.16 release film (from film3, the 0.16.1 film's), drawn by the radar's own core/ code on PC (the firmware's code, as
 * tools/preview and tour.c draw it): clips as MP4 (4:4:4, for the film to scale) and stills as PNG, at the glass's
 * own size. which screen is chosen when it is built: RADAR_DISPLAY 10 the 5" glass, 1 the 2.8" CYD, 2 the 3.5".
 * home is Trencin (Chicago for the units), the weather a sample November day, the planes the demo sky; a finger
 * is drawn where it touches. nothing here is anyone's real home.
 * usage: film5 <ffmpeg.exe> <out dir> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sky.h"
#include "demo.h"
#include "ui.h"
#include "ui_int.h"

#include "png.inc"
#include "ground.inc"

#define FPS      25
#define FRAME_MS (1000 / FPS)
#define STRIP    24

static sky_t    sky;
static demo_t   demo;
static ui_t     ui;
static uint32_t t_ms = 1000;
static char     json[32768];
static FILE    *film;
static uint8_t  rgb[GFX_PW * GFX_PH * 3];
static const char *ffmpeg, *outdir;
static weather_t wx;
static uint32_t utc0;                                   // the home's clock at the clip's start, UTC
static uint32_t clip_t0;

/* --- the places and the weather --- */

typedef struct { const char *name, *country, *cc; float lat, lon; int utc_min, elev; } place_t;
static const place_t TRENCIN = { "Trenčín", "Slovakia", "sk", 48.8945f, 18.0444f, 60, 210 };
static const place_t CHICAGO = { "Chicago", "United States", "us", 41.8781f, -87.6298f, -360, 180 };
static const place_t *home;

static uint32_t utc_at(int y, int mo, int d, int h)
{
    static const int before[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    long days = (y - 1970) * 365L + (y - 1969) / 4 + before[mo - 1] + (d - 1);
    if (mo > 2 && y % 4 == 0) days++;
    return (uint32_t)(days * 86400L + h * 3600L);
}

// the home's hour h on the sample day, in UTC
static uint32_t local_at(int h) { return utc_at(2026, 11, 18, h) - (uint32_t)(home->utc_min * 60); }

static void go_home(const place_t *p)
{
    home = p;
    ui.cfg.lat = p->lat;
    ui.cfg.lon = p->lon;
    snprintf(ui.cfg.place, sizeof ui.cfg.place, "%s", p->name);
    snprintf(ui.cfg.country, sizeof ui.cfg.country, "%s", p->country);
    snprintf(ui.home_cc, sizeof ui.home_cc, "%s", p->cc);
    sky_init(&sky, p->lat, p->lon);
}

/* a November day by the home's clock: mild in the afternoon, rain in the evening, the pressure going down ahead
 * of it (a warning: 7 hPa in 12 hours), the wind backing and freshening */
static float temp_at(float h) { return 4.6f + 3.4f * cosf((h - 13.5f) / 24.0f * 2.0f * 3.14159265f); }
static void kind_at(int h, uint8_t *kind, uint8_t *cloud, uint16_t *rain10)
{
    static const uint8_t RAIN[24] = { 5, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 6, 11, 9, 8 };
    *rain10 = RAIN[h];
    if (*rain10) { *kind = WX_RAIN; *cloud = 100; }
    else if (h >= 9 && h < 15) { *kind = WX_PARTLY; *cloud = 45; }
    else if (h >= 3 && h < 7) { *kind = WX_PARTLY; *cloud = 30; }
    else { *kind = WX_CLOUDY; *cloud = 85; }
}

static void sample_day(uint32_t utc, int now_h)
{
    memset(&wx, 0, sizeof wx);
    wx.utc_min = (int16_t)home->utc_min;
    wx.elev = (int16_t)home->elev;
    wx.at = utc - utc % 3600u;
    wx.updated = utc - 300u;
    for (int i = 0; i < WX_HOURS; i++) {
        const int h = (now_h + i) % 24;
        wx_hour_t *hr = &wx.hour[i];
        hr->t10 = (int16_t)lroundf(temp_at((float)h) * 10.0f);
        kind_at(h, &hr->kind, &hr->cloud, &hr->rain10);
        const int ahead = (h - 11 + 24) % 24;
        const float hpa = ahead <= 12 ? 1012.0f - 7.0f * sinf((float)ahead / 12.0f * 1.5708f) : 1005.0f + 0.5f * (float)(ahead - 12);
        hr->hpa10 = (uint16_t)lroundf(hpa * 10.0f);
        if (h >= 19 && h <= 23) { hr->wind_from2 = 110; hr->wind_kmh = 24; hr->gust_kmh = 52; }
        else if (h >= 17 && h < 19) { hr->wind_from2 = 115; hr->wind_kmh = 18; hr->gust_kmh = 35; }
        else if (h < 3) { hr->wind_from2 = 145; hr->wind_kmh = 20; hr->gust_kmh = 38; }
        else if (h < 11) { hr->wind_from2 = 150; hr->wind_kmh = 12; hr->gust_kmh = 22; }
        else { hr->wind_from2 = 130; hr->wind_kmh = 14; hr->gust_kmh = 25; }
        static const uint8_t UV[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 9, 12, 11, 7, 3, 0, 0, 0, 0, 0, 0, 0, 0 };
        hr->uv10 = UV[h];
        hr->aqi = (uint8_t)(h >= 7 && h < 17 ? 28 : h >= 17 && h < 20 ? 46 : h >= 20 ? 30 : 16);
    }
    wx.now_t10 = wx.hour[0].t10;
    wx.feels10 = (int16_t)(wx.now_t10 - 30);
    wx.now_kind = wx.hour[0].kind;
    wx.humidity = (uint8_t)(now_h >= 19 ? 93 : 81);
    wx.now_hpa10 = wx.hour[0].hpa10;
    wx.wind_from = wx.hour[0].wind_from2 * 2;
    wx.wind_kmh = wx.hour[0].wind_kmh;
    wx.gust_kmh = wx.hour[0].gust_kmh;
    wx.uv10 = wx.hour[0].uv10;
    wx.aqi = wx.hour[0].aqi;
    wx.pm25_10 = (uint16_t)(now_h >= 19 ? 62 : 94);
    wx.gamma_nsv = 0;                                   // no station: the sun's next rise or set in its place
    static const wx_day_t DAYS[WX_DAYS] = { { 70, 20, WX_RAIN, 3 }, { 50, 0, WX_CLOUDY, 4 }, { 30, -20, WX_SNOW, 5 }, { 60, 0, WX_CLEAR, 6 } };
    memcpy(wx.day, DAYS, sizeof DAYS);
    for (int k = 0; k < WX_PAST; k++) {
        const int h = (now_h - (WX_PAST - k) + 48) % 24;
        uint8_t cloud;
        wx.past_hpa10[k] = (uint16_t)lroundf((1017.0f - 5.0f * powf((float)(k + 1) / (float)WX_PAST, 2.2f)) * 10.0f);
        wx.past_t10[k] = (int16_t)lroundf(temp_at((float)h) * 10.0f);
        kind_at(h, &wx.past_kind[k], &cloud, &wx.past_rain10[k]);
        if (h >= 4 && h < 9) { wx.past_kind[k] = WX_FOG; wx.past_rain10[k] = 0; }
    }
}

/* a stormy day for the warnings (tools/preview's): a front in the evening, the pressure falling 11 hPa ahead of
 * it, gusts to 85 km/h */
static void stormy_day(uint32_t utc)
{
    memset(&wx, 0, sizeof wx);
    wx.at = utc - utc % 3600u;
    wx.updated = utc;
    wx.utc_min = (int16_t)home->utc_min;
    wx.elev = (int16_t)home->elev;
    for (int i = 0; i < WX_HOURS; i++) {
        wx_hour_t *h = &wx.hour[i];
        const int front = i >= 6 && i <= 10;
        h->t10 = (int16_t)(90 + 30 * (i < 6 ? i : 12 - i) / 6);
        h->kind = front ? WX_RAIN : i < 6 ? WX_CLOUDY : WX_PARTLY;
        h->cloud = (uint8_t)(front ? 100 : 70);
        h->rain10 = (uint16_t)(front ? 20 + 10 * (i - 6) : 0);
        h->hpa10 = (uint16_t)(10080 - (i <= 8 ? 14 * i : 112 - 4 * (i - 8)));
        h->wind_from2 = 120;
        h->wind_kmh = (uint8_t)(front ? 45 : 20);
        h->gust_kmh = (uint8_t)(i == 7 ? 85 : front ? 70 : 32);
        h->aqi = 25;
    }
    wx.now_t10 = wx.hour[0].t10;
    wx.feels10 = (int16_t)(wx.now_t10 - 30);
    wx.now_kind = WX_CLOUDY;
    wx.humidity = 80;
    wx.now_hpa10 = 10080;
    wx.wind_from = 240;
    wx.wind_kmh = 20;
    wx.gust_kmh = 32;
    wx.aqi = 25;
    wx.pm25_10 = 60;
    static const wx_day_t DAYS[WX_DAYS] = { { 80, 30, WX_RAIN, 3 }, { 60, 10, WX_CLOUDY, 4 }, { 50, -10, WX_SNOW, 5 }, { 70, 0, WX_CLEAR, 6 } };
    memcpy(wx.day, DAYS, sizeof DAYS);
    for (int k = 0; k < WX_PAST; k++) {
        wx.past_hpa10[k] = (uint16_t)(10120 - k * 3);
        wx.past_t10[k] = (int16_t)(80 + k % 6);
        wx.past_kind[k] = WX_CLOUDY;
    }
}

/* --- the sky and the clock move on --- */

static void feed_sky(void)
{
    int n = demo_json(&demo, home->lat, home->lon, range_km(&ui.cfg), t_ms, json, sizeof json);
    static sky_reader_t rd;
    sky_read_begin(&rd, &sky, 0, t_ms);
    sky_read(&rd, json, n);
    sky_read_end(&rd, &sky);
    sky_forget(&sky, t_ms);
}

static void tick(void)
{
    const uint32_t was = t_ms;
    t_ms += FRAME_MS;
    if (t_ms / 5000u != was / 5000u) feed_sky();     // an answer every 5 s, as the board asks
    ui.net.utc = utc0 + (t_ms - clip_t0) / 1000u;
    ui.net.fetched = t_ms - 1200u;
}

/* --- a frame --- */

static uint32_t strip_px[GFX_PW * STRIP];
static uint16_t screen565[GFX_PW * GFX_PH];
static float    fx = -100, fy = -100;
static int      f_down, f_shown;
static uint32_t f_lift;

// a fingertip seen from above: a soft light disc, firmer when it presses, a ripple when it lets go (tour.c's)
static void finger_overlay(void)
{
    if (!f_shown) return;
    const float k = GFX_PW >= 640 ? 1.6f : 1.0f;      // the 5" glass is bigger: a bigger finger on it
    const float cx = GFX_PX(fx), cy = GFX_PX(fy), R = 15.0f * k, rip = (float)(t_ms - f_lift) / 350.0f;
    for (int y = (int)(cy - 44 * k); y <= (int)(cy + 44 * k); y++) {
        if (y < 0 || y >= GFX_PH) continue;
        for (int x = (int)(cx - 44 * k); x <= (int)(cx + 44 * k); x++) {
            if (x < 0 || x >= GFX_PW) continue;
            const float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
            float a = (f_down ? 0.34f : 0.16f) * clampf(R - d, 0, 1);
            a += (f_down ? 0.85f : 0.55f) * clampf(1.2f * k - fabsf(d - R), 0, 1);
            if (f_down) a += 0.5f * clampf(4.5f * k - d, 0, 1);
            if (!f_down && rip < 1.0f) a += 0.7f * (1.0f - rip) * clampf(1.5f * k - fabsf(d - (R + 22.0f * k * rip)), 0, 1);
            if (a <= 0) continue;
            a = clampf(a, 0, 1);
            uint8_t *o = rgb + ((size_t)y * GFX_PW + x) * 3;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)(o[c] + (255 - o[c]) * a);
        }
    }
}

static void compose(void)
{
    if (!ui.t_down) make_ground(&ui);
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    for (int y = 0; y < GFX_PH; y += STRIP) {
        canvas_t cv = { strip_px, y, y + STRIP <= GFX_PH ? STRIP : GFX_PH - y, 0, 0 };
        ui_strip(&ui, &cv);
        gfx_pack565(&cv, screen565 + y * GFX_PW);
    }
    for (int i = 0; i < GFX_PW * GFX_PH; i++) {        // as the panel shows it: RGB565 back to 8 bits a colour
        uint16_t v = screen565[i];
        if (!RADAR_RGB_BUS) v = (uint16_t)((v >> 8) | (v << 8));
        rgb[i * 3] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((v & 31) * 255 / 31);
    }
    finger_overlay();
}

static void frame(void)
{
    compose();
    if (film) fwrite(rgb, 1, sizeof rgb, film);
    tick();
}

static void wait(int ms) { for (int t = 0; t < ms; t += FRAME_MS) frame(); }

static void clip(const char *name)
{
    char line[1024];
    utc0 = ui.net.utc;                                 // the clock goes on from where it is

    snprintf(line, sizeof line, "%s -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
             "-c:v libx264 -preset slow -crf 6 -pix_fmt yuv444p \"%s/%s.mp4\"", ffmpeg, GFX_PW, GFX_PH, FPS, outdir, name);
    film = _popen(line, "wb");
    if (!film) printf("could not start %s\n", ffmpeg);
    clip_t0 = t_ms;
    printf("clip %s\n", name);
}

static void clip_end(void)
{
    if (film) _pclose(film);
    film = NULL;
}

static void still(const char *name)
{
    compose();
    char path[600];
    snprintf(path, sizeof path, "%s/%s.png", outdir, name);
    if (write_png(path, rgb, GFX_PW, GFX_PH)) printf("still %s\n", name);
}

/* --- the finger --- */

static ui_cmd_t cmd;

static void touch(int down)
{
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    ui_touch(&ui, down, (int)lroundf(fx), (int)lroundf(fy), 0, 0, t_ms, &cmd);
}

static float ease(float f) { return f * f * (3.0f - 2.0f * f); }

static void glide(float x, float y, int ms)
{
    float x0 = fx, y0 = fy;
    if (!f_shown) { x0 = x + 40; y0 = (float)GFX_H + 20; f_shown = 1; }
    for (int t = 0; t < ms; t += FRAME_MS) {
        const float f = ease((float)(t + FRAME_MS) / (float)ms);
        fx = x0 + (x - x0) * f;
        fy = y0 + (y - y0) * f;
        frame();
    }
}

static void press(int frames_down)
{
    f_down = 1;
    for (int k = 0; k < frames_down; k++) { touch(1); frame(); }
    f_down = 0;
    touch(0);
    f_lift = t_ms;
    frame();
}

// the finger goes off the glass, down and out
static void away(int ms)
{
    const float x0 = fx, y0 = fy;
    for (int t = 0; t < ms; t += FRAME_MS) {
        const float f = ease((float)(t + FRAME_MS) / (float)ms);
        fx = x0 + 30 * f;
        fy = y0 + ((float)GFX_H + 40 - y0) * f;
        frame();
    }
    f_shown = 0;
}

/* --- the scenes, a screen each --- */

static void board(void)
{
    ui_init(&ui, 0);
    ui_version = FLIGHT_VERSION;
    ui.net.configured = ui.net.wifi = 1;
    snprintf(ui.net.ssid, sizeof ui.net.ssid, "HomeWiFi");
    snprintf(ui.net.server, sizeof ui.net.server, "ADSB.LOL");
    ui.view = VIEW_MAIN;
    ui.alerts = WXA_ALL;
    ui.cfg.map = 1;
    go_home(&TRENCIN);
    demo_init(&demo, range_km(&ui.cfg), 82);
    for (int s = 0; s < 60 * FPS; s++) tick();         // a minute of sky first: the trails grow
}

// the weather look at the home's hour h: the forecast in, a frame without it first as the board starts it
static void weather_at(int h)
{
    utc0 = local_at(h) + 47 * 60;
    clip_t0 = t_ms;                                    // the clock runs from here, filmed or not
    sample_day(utc0, h);
    ui.net.utc = utc0;
    ui.wx = NULL;
    ui.cfg.look = LOOK_WEATHER;
    compose();
    ui.wx = &wx;
    wait(5000);                                        // not filmed: the toast of the new warning goes first
    utc0 = ui.net.utc;
    clip_t0 = t_ms;
}

#if defined(HUD017)
/* built with the 0.17 branch's core/ (build017.cmd): its new weather display on the 2.8" and the 3.5", the
 * film's last look ahead. the day the film's sample one, the warning's toast gone before the clip */
static void scenes(void)
{
    char name[32];
    weather_at(11);
    ui.bare = 0;
    ui.wx_page = WXP_DIAL;
    snprintf(name, sizeof name, "h%d-hud", RADAR_DISPLAY);
    clip(name);
    wait(9000);
    clip_end();
    still(name);
}
#elif RADAR_DISPLAY == 10
static void scenes(void)
{
    weather_at(11);
    ui.bare = 0;
    ui.wx_page = WXP_DIAL;
    clip("d10-open");                                  // the film's first picture: the dial and its panel
    wait(16000);
    clip_end();
    ui.bare = 1;
    clip("d10-bare");                                  // the dial alone, now's numbers round it
    wait(14000);
    clip_end();
    printf("dial, bare: centre %.0f %.0f, rim radius %.0f (glass px)\n", (double)scope_glass_cx(&ui), (double)GFX_PX(SCY), (double)GFX_PX(SR));
    ui.bare = 0;
    printf("dial, panel: centre %.0f %.0f, rim radius %.0f (glass px)\n", (double)scope_glass_cx(&ui), (double)GFX_PX(SCY), (double)GFX_PX(SR));
    clip("d10-dots");                                  // the panel, then a finger on the three dots
    wait(2600);
    glide((float)(RDOTS_X + RDOTS_W / 2), (float)(LOOK_Y + LOOK_H / 2 + 1), 900);
    press(5);
    wait(500);
    away(600);
    wait(6000);
    clip_end();
    printf("after the dots: bare %d\n", ui.bare);
    ui.bare = 1;
    ui.wx_page = WXP_PRESSURE;
    clip("d10-pressure");
    wait(13000);
    clip_end();
    ui.wx_page = WXP_TEMP;
    clip("d10-temp");
    wait(13000);
    clip_end();
    ui.wx_page = WXP_DIAL;
    for (int k = 0; k < 2; k++) {                      // the units, AUTO: Trencin's, then Chicago's
        go_home(k ? &CHICAGO : &TRENCIN);
        weather_at(11);
        ui.bare = 1;
        wait(800);
        still(k ? "d10-auto-chicago" : "d10-auto-trencin");
    }
    go_home(&TRENCIN);
}
#elif RADAR_DISPLAY == 1
static void set_page(int tab, int first_row, const char *name)
{
    ui.view = VIEW_SETUP;
    ui.setup_tab = tab;                                // 0 VIEW, 1 DATA, 2 UNITS, 3 RADAR, 4 LED (ui_pages.c)
    ui.setup_scroll = first_row * ROW_H;
    still(name);
}

static void scenes(void)
{
    ui.cfg.look = LOOK_RADAR;
    ui.cfg.range = 50;
    ui.wx = NULL;
    utc0 = local_at(11);
    ui.net.utc = utc0;
    clip("d1-radar");                                  // the radar for the light's steps and the speed
    wait(15000);
    clip_end();
    for (int b = 0; b < 4; b++) {                      // SET, VIEW: BACKLIGHT at each step
        char n[32];
        ui.backlight = (uint8_t)b;
        ui.night = 0;
        snprintf(n, sizeof n, "d1-set-view-b%d", b);
        set_page(0, 2, n);
    }
    ui.backlight = 0;
    ui.night = 2;                                      // NIGHT at 25%
    set_page(0, 2, "d1-set-view-night");
    ui.night = 0;
    set_page(4, 5, "d1-set-led");                      // the LED: NO DATA, WEATHER, BRIGHTNESS...
    set_page(2, 1, "d1-set-units");                    // ALTITUDE, then the weather's four
    set_page(1, 6, "d1-set-data");                     // COUNT ME, then the four warnings
    ui.view = VIEW_MAIN;
    // a stormy day: the warning's chip on the radar, a finger on it, the weather opens at the pressure
    utc0 = local_at(17);
    stormy_day(utc0);
    ui.wx = &wx;
    ui.net.utc = utc0;
    ui.cfg.look = LOOK_RADAR;
    clip("d1-alert");                                  // the warning comes: its toast, then the chip stays
    wait(8800);                                        // the tap falls on the voice's "tap the chip"
    note_box_t ab;
    if (alert_box(&ui, &ab)) {
        glide((float)(ab.x + ab.w / 2), (float)(ab.y + ab.h / 2), 900);
        press(5);
        wait(400);
        away(500);
    } else {
        printf("no warning chip\n");
    }
    wait(7000);
    clip_end();
    printf("after the chip: look %d, page %d\n", ui.cfg.look, ui.wx_page);
    ui.cfg.look = LOOK_RADAR;
    ui.wx = NULL;
    ui_version = "0.15";                               // a board on 0.15 that heard the site has 0.16
    snprintf(ui.newest, sizeof ui.newest, "0.16");
    utc0 = local_at(11);
    ui.net.utc = utc0;
    wait(1500);
    still("d1-newversion");
    ui.view = VIEW_ABOUT;
    still("d1-about-newversion");
    ui.view = VIEW_MAIN;
    ui_version = FLIGHT_VERSION;
    ui.newest[0] = 0;
}
#elif RADAR_DISPLAY == 2
static void scenes(void)
{
    ui.cfg.look = LOOK_MAP;
    ui.cfg.range = 25;
    ui.wx = NULL;
    utc0 = local_at(11);
    ui.net.utc = utc0;
    wait(2000);
    clip("d2-map");
    wait(11000);
    clip_end();
}
#endif

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: film5 <ffmpeg.exe> <out dir>\n"); return 1; }
    ffmpeg = argv[1];
    outdir = argv[2];
    setvbuf(stdout, NULL, _IONBF, 0);
    crc_init();
    board();
    scenes();
    printf("sky: %d planes\n", sky.n);
    return 0;
}
