/* the WEATHER look: the next 24 hours on the radar's own instrument. a clock of a day on the scope:
 * noon at the top, midnight at the bottom, a hand at now. round its edge a ring of sky, each hour in
 * the colour the sky will have then (the sun's height, worked out here), the night with its stars;
 * inside it the temperature as a loop in the colours of how warm, nearer the rim the warmer; rain
 * where it will fall. all of it there at once, no beam: the data, not the show. what moves is the
 * weather: the rain falling, the snow, the wind, the clouds, the stars.
 * the panel says it in words. its three dots (the radar's, kept the same) take it away: the dial
 * stands in the middle then, now's numbers either side of it.
 * behind the dial, a tap away, the pressure and the temperature as graphs on one time axis: the hours
 * before on the left, now at about the golden section of the width, the next day on the right; the
 * weather's pictures on the line, the rain falling into its bars.
 * the units are the person's: the weather comes in C and hPa (weather.h) */
#include "ui_int.h"
#include "sun.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the weather's colours, one job each, the same on the dial, the graphs and the panel:
 *   how warm   frost violet, ice, a pale mild, amber, a hot red (temp_colour): the loop, the line, their numbers
 *   rain       RAIN_C: the drops, the bars, the words of rain
 *   the sun    SUN_C; DAWN_C where it rises and sets
 *   now        the theme's focus: the dial's hand, the graphs' line and dot, the clock
 *   warnings   amber and red (alert_colour), and nothing else
 *   the air and the UV in their agencies' scales (the EEA's, the WHO's), always with the word
 * the instrument itself (rims, grid, frames, what a thing is) keeps the theme's quieter colours */
#define RAIN_C RGB(77, 166, 255)
#define SUN_C  RGB(255, 205, 80)
#define DAWN_C RGB(255, 150, 72)

static uint32_t temp_colour(int t10)
{
    static const int16_t T[7] = { -150, -50, 0, 80, 160, 230, 300 };
    static const uint32_t C[7] = { RGB(160, 140, 255), RGB(110, 168, 255), RGB(165, 212, 255), RGB(214, 236, 242),
                                   RGB(246, 236, 196), RGB(255, 192, 88), RGB(255, 104, 84) };
    if (t10 <= T[0]) return C[0];
    for (int i = 1; i < 7; i++)
        if (t10 < T[i]) return gfx_mix(C[i - 1], C[i], (uint32_t)((t10 - T[i - 1]) * 256 / (T[i] - T[i - 1])));
    return C[6];
}

/* the dial, in the layout's pixels: the ring of sky inside the rim, the loop between the middle
 * and the ring, the middle for now in big digits */
#define W_ROUT (SR - 3)
#define W_RIN  (SR - 27)
#define W_TMAX (SR - 46)
#define W_TMIN (SR * 30 / 100)
#define W_CORE (SR * 25 / 100)
#define W_MID  ((W_RIN + W_ROUT) / 2)

/* the pages: the hours before now a graph shows, so now stands at about 0.382 of its width */
#define PG_PAST 14
#define PG_N    (PG_PAST + WX_HOURS)       /* the hours a graph shows, -PG_PAST .. WX_HOURS - 1 from the forecast's first */
#define PG_S    200                        /* its line, a point every few columns of the glass */
#define N_PIC   (PG_N / 3 + 1)             /* its pictures, every three hours */
#define N_RAIN  64                         /* drops falling into its rain's bars */

/* the dial's tables, by the degree (0 up, clockwise) and by the radius: made again each minute and
 * for each new forecast. taken while the look is up and given back when it goes, as the map on the
 * 3.5" wants every byte the radar can spare */
/* the frame's things, worked out once a frame (weather_frame) and drawn by each strip they reach (the
 * strips of a frame share u->now): a strip is a few rows (6 on the 3.5"), and forty of them working
 * the stars, the clouds and the words out again each made the look three times slower */
#define N_STARS 60
#define N_DROPS 48
#define N_AIR   48
typedef struct { float x0, y0, x1, y1, w; uint32_t c; uint8_t a; } seg_t;   // a line
typedef struct { float x, y, w; uint32_t c; uint8_t a; } blob_t;           // a cloud, a star, a drop of snow
typedef struct { float x, y, lx, ly; uint32_t c; int16_t from; uint8_t kmh, gust; char t[8]; } pt_t;
typedef struct { float x, y, iy, ly; int16_t v; uint8_t kind, night, past, named; } pic_t;
typedef struct { float x, y; uint8_t a, snow; } fall_t;
typedef struct {
    int8_t   el[360];          /* the sun's height at the hour each degree stands for */
    uint8_t  loop2[360];       /* the loop's radius, half pixels of the layout */
    uint32_t sky[360];         /* the ring's colour there */
    uint32_t lcol[360];        /* the loop's colour there: how warm it will be */
    uint8_t  glass[G_RQ + 8];  /* the glass's light by radius, whole pixels of the glass */
    uint8_t  at[257];          /* atan(i / 256), quarter degrees: 0 .. 180 */
    uint32_t frame;            /* u->now the things below were worked out for */
    int      planned;          /* 0: not since the tables were made, whatever frame says */
    /* the pages: the sun's height at each hour from a day before hour[0] to a day after; the scales; where the
     * pressure's warning is; now */
    int8_t   pel[2 * WX_PAST + 1];
    uint32_t pel_at;           /* the forecast's first hour pel is for, 0: not made */
    float    pel_lat, pel_lon;
    float    p_lo, p_hi, p_step;   /* the pressure's scale and grid, in the unit shown */
    int16_t  p_d;              /* the warning's change, tenths of hPa */
    uint8_t  p_lv, p_at;       /* the warning's level, the hour from hour[0] it is worst */
    float    t_lo, t_hi, t_step;   /* the temperature's scale and grid, degrees of the unit shown */
    int16_t  t_min, t_max;     /* the next day's own, tenths of C */
    float    now_h;            /* now, hours after hour[0] */
    union {                    /* a frame plans the dial's things or its page's, never both */
        struct {
            int      n_star, n_tick, n_lab, n_cloud, n_bar, n_drop, n_snow, n_air, n_pt;
            blob_t   star[N_STARS], cloud[WX_HOURS], snow[N_DROPS];
            seg_t    tick[24 + 4], bar[WX_HOURS], drop[N_DROPS], air[N_AIR], hand[2];
            struct { float x, y; char s[3]; } lab[4];
            pt_t     pt[WX_HOURS / 3];
            float    now_x, now_y, phase;
            int      now_sun;
        };
        struct {
            /* the line, point by point along the glass: sg columns apart from sgx0, its height in sixteenths
             * of a glass pixel, how far it leans in sixteenths (16 flat: the line as thick across as along),
             * its colour, and what it is there: 0 not known, 1 the past, 2 to come, 3 what a warning is about */
            int      n_s, sg, sgx0;
            int32_t  sy16[PG_S];
            uint8_t  slean[PG_S];
            uint32_t sc[PG_S];
            uint8_t  sf[PG_S];
            int      n_pic, n_fall;
            pic_t    pic[N_PIC];
            fall_t   fall[N_RAIN];
            float    nx, ny;   /* now on the line, ny < 0: not known */
        };
    };
} wtab_t;

/* the European air quality index's six steps in the EEA's colours, good to extremely poor; the UV index's five in
 * the WHO's, low to extreme */
static const uint32_t AQI_C[6] = { RGB(80, 240, 230), RGB(80, 204, 170), RGB(240, 230, 65), RGB(255, 80, 80), RGB(170, 20, 60), RGB(150, 60, 170) };
static const uint32_t UV_C[5] = { RGB(90, 200, 90), RGB(247, 228, 0), RGB(248, 89, 0), RGB(216, 0, 29), RGB(107, 73, 200) };
static int aqi_level(int aqi) { return aqi < 20 ? 0 : aqi < 40 ? 1 : aqi < 60 ? 2 : aqi < 80 ? 3 : aqi < 100 ? 4 : 5; }
static int uv_level(int uv10) { return uv10 < 30 ? 0 : uv10 < 60 ? 1 : uv10 < 80 ? 2 : uv10 < 110 ? 3 : 4; }

static struct {
    uint32_t opened;           /* u->now when the dial or a page came up: a graph is drawn in from it */
    int      shown;            /* what was up the frame before: 0 nothing, else 1 + the page */
    uint32_t made_utc, made_for;
    float    lat, lon;
    int16_t  tmin10, tmax10;
    float    now_deg;          /* where now is on the dial */
    int      cx, gcx;          /* the dial's middle this frame, in the layout and on the glass: beside the panel or in the middle */
    float    anim;             /* the weather's own clock this frame, seconds: the rain falls by it */
    int      tu, pu, wu, ru;   /* this frame's units: TEMP_*, PRES_*, WIND_*, RAIN_* (never AUTO) */
    int      elev;             /* the forecast point's height, m: the pressure at home's height from it */
    uint32_t sun_utc;          /* the sun's next rise or set, 0: none in the next day (the far north and south) */
    int      sun_rise;         /* that one is a rise */
    wtab_t  *t;
    int      room;             /* this frame has the forecast and the tables: the dial, not why there is none */
} W;

/* --- the units: the person's, or home's country's own. what a country uses is what its weather service says: the
 * US and its islands Fahrenheit, inHg, mph and inches; the UK's wind in mph; the barometer in mmHg in the lands that
 * kept it; the wind in metres a second in the north, the Baltic, the east and the Far East. two small letters each,
 * as OpenStreetMap names the country of home --- */

static const char F_LANDS[]    = "us lr bs bz ky pw fm mh pr gu vi as mp ";
static const char INHG_LANDS[] = "us pr gu vi as mp ";
static const char MMHG_LANDS[] = "by ua kz uz kg tj tm am az ge md ";
static const char MPH_LANDS[]  = "us gb lr pr gu vi as mp ";
static const char MS_LANDS[]   = "no se fi dk is ee lv lt by ua kz uz kg tj tm am az ge md jp kr cn ";
static const char INCH_LANDS[] = "us lr pr gu vi as mp ";
static const char *const TEMP_NAME[TEMP_UNITS] = { "", "\xC2\xB0" "C", "\xC2\xB0" "F" };
static const char *const PRES_NAME[PRES_UNITS] = { "", "HPA", "MMHG", "INHG" };
static const char *const WIND_NAME[WIND_UNITS] = { "", "KM/H", "M/S", "MPH", "KT" };
static const char *const RAIN_NAME[RAIN_UNITS] = { "", "MM", "IN" };

static int cc_in(const char *cc, const char *lands)
{
    if (!cc[0] || !cc[1]) return 0;
    for (const char *p = lands; p[0] && p[1]; p += 3)
        if (p[0] == cc[0] && p[1] == cc[1]) return 1;
    return 0;
}

int wx_unit(const ui_t *u, int which)
{
    const char *cc = u->home_cc;
    switch (which) {
    case WXU_TEMP: return u->units.temp && u->units.temp < TEMP_UNITS ? u->units.temp : cc_in(cc, F_LANDS) ? TEMP_F : TEMP_C;
    case WXU_PRES:
        if (u->units.pres && u->units.pres < PRES_UNITS) return u->units.pres;
        return cc_in(cc, INHG_LANDS) ? PRES_INHG : cc_in(cc, MMHG_LANDS) ? PRES_MMHG : PRES_HPA;
    case WXU_WIND:
        if (u->units.wind && u->units.wind < WIND_UNITS) return u->units.wind;
        return cc_in(cc, MPH_LANDS) ? WIND_MPH : cc_in(cc, MS_LANDS) ? WIND_MS : WIND_KMH;
    default:       return u->units.rain && u->units.rain < RAIN_UNITS ? u->units.rain : cc_in(cc, INCH_LANDS) ? RAIN_IN : RAIN_MM;
    }
}

void wx_unit_text(const ui_t *u, int which, char *b, int sz)
{
    static const char *const *const NAMES[4] = { TEMP_NAME, PRES_NAME, WIND_NAME, RAIN_NAME };
    const uint8_t set[4] = { u->units.temp, u->units.pres, u->units.wind, u->units.rain };
    const char *name = NAMES[which & 3][wx_unit(u, which)];
    if (set[which & 3]) snprintf(b, (size_t)sz, "%s", name);
    else snprintf(b, (size_t)sz, "AUTO %s", name);
}

void wx_unit_next(ui_t *u, int which)
{
    switch (which) {                                   // the next one, AUTO again after the last
    case WXU_TEMP: u->units.temp = (uint8_t)((u->units.temp + 1) % TEMP_UNITS); break;
    case WXU_PRES: u->units.pres = (uint8_t)((u->units.pres + 1) % PRES_UNITS); break;
    case WXU_WIND: u->units.wind = (uint8_t)((u->units.wind + 1) % WIND_UNITS); break;
    default:       u->units.rain = (uint8_t)((u->units.rain + 1) % RAIN_UNITS); break;
    }
}

/* what the screen says, in the frame's units. inside it is all C, hPa at the sea's level, km/h and mm */
static float t_of(int t10) { return W.tu == TEMP_F ? (float)t10 * 0.18f + 32.0f : (float)t10 * 0.1f; }
static int c10_of(float t) { return (int)lroundf(W.tu == TEMP_F ? (t - 32.0f) * 50.0f / 9.0f : t * 10.0f); }   // back to tenths of C
// the pressure from the sea's level and the temperature with it: in mmHg at home's height, the barometer's way
static float p_of(int hpa10, int t10)
{
    const float hpa = (float)hpa10 * 0.1f;
    if (W.pu == PRES_INHG) return hpa * 0.02953f;
    if (W.pu != PRES_MMHG) return hpa;
    const float h = (float)W.elev, k = 1.0f - 0.0065f * h / ((float)t10 * 0.1f + 273.15f + 0.0065f * h);
    return (h > 0.0f && k > 0.5f ? hpa * powf(k, 5.257f) : hpa) * 0.750062f;
}
// a change of the pressure (tenths of hPa) in the unit shown: at home's height a little less than at the sea's
static float dp_of(int d10, const weather_t *w)
{
    const float d = (float)d10 * 0.1f, now = (float)w->now_hpa10 * 0.1f;
    if (W.pu == PRES_INHG) return d * 0.02953f;
    if (W.pu == PRES_MMHG && now > 0.0f) return d * p_of(w->now_hpa10, w->now_t10) / now;
    return d;
}
static int w_of(int kmh)
{
    switch (W.wu) {
    case WIND_MS:  return (int)lroundf((float)kmh / 3.6f);
    case WIND_MPH: return (int)lroundf((float)kmh * 0.621371f);
    case WIND_KN:  return (int)lroundf((float)kmh * 0.539957f);
    default:       return kmh;
    }
}
// a pressure, its unit after it or not: 1012, 759, 29.88
static void fmt_p(char *b, int sz, float p, int unit)
{
    if (W.pu == PRES_INHG) {
        if (unit) snprintf(b, (size_t)sz, "%.2f INHG", (double)p);
        else snprintf(b, (size_t)sz, "%.2f", (double)p);
    } else if (unit) snprintf(b, (size_t)sz, "%d %s", (int)lroundf(p), PRES_NAME[W.pu]);
    else snprintf(b, (size_t)sz, "%d", (int)lroundf(p));
}
// a change of it, signed
static void fmt_dp(char *b, int sz, float d)
{
    if (W.pu == PRES_INHG) snprintf(b, (size_t)sz, "%+.2f INHG", (double)d);
    else snprintf(b, (size_t)sz, "%+d %s", (int)lroundf(d), PRES_NAME[W.pu]);
}
// rain or snow: 4.3 MM, 0.17 IN
static void fmt_r(char *b, int sz, int mm10)
{
    if (W.ru == RAIN_IN) snprintf(b, (size_t)sz, "%.2f IN", (double)mm10 / 254.0);
    else snprintf(b, (size_t)sz, "%d.%d MM", mm10 / 10, mm10 % 10);
}
static void units_of(const ui_t *u)
{
    W.tu = wx_unit(u, WXU_TEMP);
    W.pu = wx_unit(u, WXU_PRES);
    W.wu = wx_unit(u, WXU_WIND);
    W.ru = wx_unit(u, WXU_RAIN);
    W.elev = u->wx ? u->wx->elev : 0;
}

void weather_close(void)
{
    free(W.t);
    W.t = NULL;
    W.room = 0;
}

/* a clock time on the dial: noon up, the day clockwise */
static float dial_deg(float local_h) { return wrap360((local_h - 12.0f) * 15.0f); }

static float local_hours(const weather_t *w, uint32_t utc)
{
    int64_t s = (int64_t)utc + (int64_t)w->utc_min * 60;
    return (float)(s % 86400) / 3600.0f;
}

/* the hour each degree stands for, counted from now: 0 now, up to 24 a day on */
static float ahead_h(float deg, float now_deg)
{
    return wrap360(deg - now_deg) / 15.0f;
}

static uint32_t sky_colour(float el)
{
    if (el < -12.0f) return RGB(3, 6, 18);
    if (el < -6.0f) return gfx_mix(RGB(3, 6, 18), RGB(48, 30, 104), (uint32_t)((el + 12.0f) / 6.0f * 256.0f));
    if (el < 0.0f) return gfx_mix(RGB(48, 30, 104), RGB(255, 132, 56), (uint32_t)((el + 6.0f) / 6.0f * 256.0f));
    if (el < 10.0f) return gfx_mix(RGB(255, 132, 56), RGB(40, 122, 222), (uint32_t)(el / 10.0f * 256.0f));
    return gfx_mix(RGB(40, 122, 222), RGB(70, 162, 246), (uint32_t)(clampf((el - 10.0f) / 40.0f, 0, 1) * 256.0f));
}

/* the tables: the sky's colour and the loop by the degree, once a minute and when the forecast
 * changes; the glass by radius and the arc tangent once they are taken. 0: no memory for them */
static int make(const ui_t *u, const weather_t *w)
{
    if (!W.t) {
        W.t = malloc(sizeof *W.t);
        if (!W.t) return 0;
        const float rin = (float)W_RIN * GFX_PL(1.0f);
        for (int r = 0; r < G_RQ + 8; r++) {           // dark in the middle, lighter out to the ring
            const float t = clampf((float)r / rin, 0, 1);
            W.t->glass[r] = (uint8_t)(30.0f + 170.0f * t * t * t);
        }
        for (int i = 0; i <= 256; i++) W.t->at[i] = (uint8_t)lroundf(atanf((float)i / 256.0f) / D2R * 4.0f);
        W.made_utc = 0;
        W.t->pel_at = 0;
    }
    const uint32_t utc = u->net.utc ? u->net.utc : w->at;
    if (W.made_utc && W.made_utc / 60 == utc / 60 && W.made_for == w->updated && W.lat == u->cfg.lat && W.lon == u->cfg.lon) return 1;
    W.made_utc = utc;
    W.made_for = w->updated;
    W.lat = u->cfg.lat;
    W.lon = u->cfg.lon;
    W.now_deg = dial_deg(local_hours(w, utc));
    W.tmin10 = W.tmax10 = w->hour[0].t10;
    for (int i = 1; i < WX_HOURS; i++) {
        if (w->hour[i].t10 < W.tmin10) W.tmin10 = w->hour[i].t10;
        if (w->hour[i].t10 > W.tmax10) W.tmax10 = w->hour[i].t10;
    }
    if (W.tmax10 - W.tmin10 < 40) W.tmax10 = (int16_t)(W.tmin10 + 40);   // a flat day stays a ring, not a scribble
    for (int d = 0; d < 360; d++) {
        const float ah = ahead_h((float)d, W.now_deg);
        W.t->el[d] = (int8_t)clampf(sun_elevation(u->cfg.lat, u->cfg.lon, utc + (uint32_t)(ah * 3600.0f)), -90, 90);
        W.t->sky[d] = sky_colour((float)W.t->el[d]);
        // the hour's temperature, between the forecast's hours (hour[0] is the hour now began)
        float at = ah + (float)(utc - w->at) / 3600.0f;
        int i = (int)at;
        float f = at - (float)i;
        if (i >= WX_HOURS - 1) { i = WX_HOURS - 2; f = 1.0f; }
        const float t = (float)w->hour[i].t10 * (1.0f - f) + (float)w->hour[i + 1].t10 * f;
        const float r = W_TMIN + (t - W.tmin10) / (float)(W.tmax10 - W.tmin10) * (W_TMAX - W_TMIN);
        W.t->loop2[d] = (uint8_t)clampf(r * 2.0f, 0, 255);
        W.t->lcol[d] = temp_colour((int)lroundf(t));
    }
    /* the sun's next rise or set: the first degree after now where the table has it cross the horizon, then halved
     * down from a degree before to a degree after, the sun worked out exactly (its disc's upper edge, 0.833 degrees
     * under), to some seconds. a few dozen of those a minute, not one a degree: the C3 has no floating point unit */
    W.sun_utc = 0;
    const int d0 = (int)W.now_deg;
    for (int k = 1; k < 360; k++) {
        const int up0 = W.t->el[(d0 + k - 1) % 360] >= 0, up1 = W.t->el[(d0 + k) % 360] >= 0;
        if (up0 == up1) continue;
        float a = fmaxf((float)(k - 2) / 15.0f, 0.0f), b = (float)(k + 1) / 15.0f;
        for (int i = 0; i < 8; i++) {
            const float m = (a + b) * 0.5f;
            const int up = sun_elevation(u->cfg.lat, u->cfg.lon, utc + (uint32_t)(m * 3600.0f)) > -0.833f;
            if (up == up1) b = m;
            else a = m;
        }
        W.sun_utc = utc + (uint32_t)((a + b) * 0.5f * 3600.0f);
        W.sun_rise = up1;
        break;
    }
    W.t->planned = 0;
    return 1;
}

/* how long the dial or the page has been up. a copy of the ui older than the frame that brought it up (the
 * boards that draw the background from a copy) says 0, not a wrapped-round lifetime. no beam goes round the
 * weather: it shows its day whole from the first frame, the radar's sweep is the radar's (2026-11-18: "a
 * person wants to see the data here") */
static uint32_t since_open(const ui_t *u) { const int32_t d = (int32_t)(u->now - W.opened); return d > 0 ? (uint32_t)d : 0; }

static inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

/* --- the dial, pixel by pixel on the glass: glass, sky ring, the loop, rims.
 * in whole numbers as the scope's pass is, the C3 has no floating point unit: radii in sixteenths
 * of a glass pixel, angles in quarter degrees from straight up, clockwise. a pixel's radius and
 * angle hang on |dx| alone within a row, so they are worked out once a row for both halves --- */
static void dial_pass(ui_t *u, canvas_t *cv)
{
    (void)u;
    const wtab_t *T = W.t;
    const int cxg = W.gcx;
    const int k16 = (int)(GFX_PL(1.0f) * 16.0f + 0.5f);   // sixteenths of a glass pixel a layout pixel
    const int rim16 = SR * k16, rin16 = W_RIN * k16, rout16 = W_ROUT * k16, core16 = W_CORE * k16;
    const int edge16 = rim16 + 6 * k16, reach = (edge16 >> 4) + 1;
    const int loop_w = (23 * k16) / 20 + 8, rim_w = (13 * k16) / 20 + 8, rin_w = k16 / 2 + 8, glow16 = 5 * k16;
    const uint32_t prim = C_PRIM, bg0 = C_BG0, glassc = C_GLASS;
    uint16_t rad[G_RQ + 8], phi[G_RQ + 8];
    for (int y = G_SCY - reach; y <= G_SCY + reach; y++) {
        uint32_t *row = gfx_row(cv, y);
        if (!row) continue;
        const int dy = y - G_SCY, ady = dy < 0 ? -dy : dy;
        const int span = (int)gfx_sqrt((float)(reach * reach - ady * ady));
        const int xa = cxg - span < 0 ? 0 : cxg - span, xb = cxg + span > GFX_PW - 1 ? GFX_PW - 1 : cxg + span;
        const int amax = cxg - xa > xb - cxg ? cxg - xa : xb - cxg;
#if GFX_SOFT_FLOAT
        int32_t r2 = dy * dy;                          // the radius squared, whole px: a pixel out moves it under 1 px
        uint32_t r16 = gfx_isqrt((uint32_t)r2 << 8);
#endif
        for (int a = 0; a <= amax && a < G_RQ + 8; a++) {
#if GFX_SOFT_FLOAT
            if (a) {
                r2 += 2 * a - 1;
                const uint32_t t = (uint32_t)r2 << 8;
                while ((r16 + 1) * (r16 + 1) <= t) r16++;
                while (r16 * r16 > t) r16--;
            }
            rad[a] = (uint16_t)r16;
#else
            rad[a] = (uint16_t)(gfx_sqrt((float)(a * a + dy * dy)) * 16.0f);
#endif
            phi[a] = (uint16_t)(a <= ady ? (ady ? T->at[(a << 8) / ady] : 0) : 360 - T->at[(ady << 8) / a]);
        }
        for (int x = xa; x <= xb; x++) {
            const int dx = x - cxg, adx = dx < 0 ? -dx : dx;
            if (adx >= G_RQ + 8) continue;
            const int r16 = rad[adx];
            if (r16 > edge16) continue;
            const int ph = phi[adx];
            int A = dx >= 0 ? (dy < 0 ? ph : 720 - ph) : (dy >= 0 ? 720 + ph : 1440 - ph);
            if (A >= 1440) A -= 1440;
            const int di = A >> 2;
            uint32_t p = row[x];
            if (r16 <= rim16 + 8) {
                // the glass: dark in the middle, lighter out to the ring; the middle darker still for the digits
                p = gfx_mix(bg0, glassc, T->glass[r16 >> 4]);
                if (r16 < core16 + 16) p = gfx_mix(p, bg0, (uint32_t)(150 * clampi(core16 + 16 - r16, 0, 16) / 16));
                if (r16 >= rin16 - 8 && r16 <= rout16 + 8) {   // the ring of sky
                    const int edge = clampi(r16 - rin16 + 8, 0, 16) * clampi(rout16 + 8 - r16, 0, 16);   // 0..256
                    p = gfx_mix(p, T->sky[di], (uint32_t)(242 * edge >> 8));
                } else if (r16 < rin16 && r16 > core16) {    // the temperature: a loop in the colours of how warm, filled faintly
                    const int rl16 = T->loop2[di] * k16 / 2;
                    const uint32_t lc = T->lcol[di];
                    if (r16 < rl16) p = gfx_mix(p, lc, 34u);
                    const int e = clampi(loop_w - (r16 > rl16 ? r16 - rl16 : rl16 - r16), 0, 16);
                    if (e) p = gfx_mix(p, lc, (uint32_t)(e * 240 / 16));
                }
                const int ri = clampi(rin_w - (r16 > rin16 ? r16 - rin16 : rin16 - r16), 0, 16);   // the sky's inner edge
                if (ri) p = gfx_add(p, gfx_scale(prim, (uint32_t)(150 * ri / 16)));
            }
            const int rr = clampi(rim_w - (r16 > rim16 ? r16 - rim16 : rim16 - r16), 0, 16);   // the rim, and its glow outside
            if (rr) p = gfx_add(p, gfx_scale(prim, (uint32_t)(230 * rr / 16)));
            if (r16 > rim16) {
                const int h = clampi(glow16 - (r16 - rim16), 0, glow16);
                p = gfx_add(p, gfx_scale(prim, (uint32_t)(30 * h * h / (glow16 * glow16))));
            }
            row[x] = p;
        }
    }
}

/* --- small pictures of the weather, out of discs and lines, so they scale with the screen. t is the weather's
 * clock: the rain falls, the snow drifts, the sun's rays go slowly round; 0 a picture that stands still --- */

static void cloud(canvas_t *cv, float cx, float cy, float w, uint32_t c, int a)
{
    gfx_disc(cv, cx - 0.22f * w, cy + 0.02f * w, 0.20f * w, c, a);
    gfx_disc(cv, cx + 0.02f * w, cy - 0.10f * w, 0.27f * w, c, a);
    gfx_disc(cv, cx + 0.26f * w, cy + 0.03f * w, 0.18f * w, c, a);
    gfx_fill(cv, (int)(cx - 0.40f * w), (int)(cy + 0.02f * w), (int)(0.82f * w), (int)(0.19f * w), c, a);
}

static void sun_pic(canvas_t *cv, float cx, float cy, float r, float t, int a)
{
    gfx_glow(cv, cx, cy, r * 2.6f, SUN_C, a / 3);
    gfx_disc(cv, cx, cy, r, SUN_C, a);
    const float turn = t * 0.14f;                      // a turn in some 45 seconds
    for (int i = 0; i < 8; i++) {
        const float q = (float)i * 0.7854f + turn;
        gfx_line(cv, cx + sinf(q) * r * 1.45f, cy - cosf(q) * r * 1.45f, cx + sinf(q) * r * 1.9f, cy - cosf(q) * r * 1.9f,
                 r > 6 ? 1.6f : 1.1f, SUN_C, a);
    }
}

/* the moon as it is tonight, pixel by pixel on the glass: lit where the sun reaches it, the rest
 * a faint disc of earthshine. the edge between them is half an ellipse, cos(phase) wide: none lit at
 * a new moon, the right half at the first quarter, all of it full, then the left side going */
static void moon_pic(canvas_t *cv, float cx, float cy, float r, float phase, int a)
{
    const uint32_t lit = RGB(238, 242, 250);
    const float gx = GFX_PX(cx), gy = GFX_PX(cy), gr = GFX_PL(r), k = cosf(phase * 2.0f * 3.14159265f);
    const float side = phase < 0.5f ? 1.0f : -1.0f;      // waxing: lit on the right
    for (int y = (int)(gy - gr - 1); y <= (int)(gy + gr + 1); y++) {
        uint32_t *row = gfx_row(cv, y);
        if (!row) continue;
        const float ny = ((float)y - gy) / gr;
        if (ny < -1.05f || ny > 1.05f) continue;
        const float half = gfx_sqrt(clampf(1.0f - ny * ny, 0, 1));
        for (int x = (int)(gx - gr - 1); x <= (int)(gx + gr + 1); x++) {
            if (x < 0 || x >= GFX_PW) continue;
            const float nx = ((float)x - gx) / gr, d = gfx_sqrt(nx * nx + ny * ny);
            const float in = clampf((1.0f - d) * gr + 0.5f, 0, 1);   // the disc's edge, smoothed
            if (in <= 0) continue;
            const float l = clampf((side * nx - k * half) * gr + 0.5f, 0, 1);   // the sunlit side of the edge
            row[x] = gfx_mix(row[x], lit, (uint32_t)(in * (40.0f + 215.0f * l) * (float)a / 255.0f));
        }
    }
}

static float moon_phase(uint32_t utc)   // 0 new, 0.5 full: from a new moon of 2000, by the synodic month
{
    const double d = ((double)utc - 947182440.0) / 86400.0;
    double p = fmod(d / 29.530588853, 1.0);
    return (float)(p < 0 ? p + 1.0 : p);
}

static void wx_pic(canvas_t *cv, int kind, int night, float cx, float cy, float s, float t, int a)
{
    const uint32_t cc = night ? RGB(160, 176, 200) : RGB(226, 234, 246);
    switch (kind) {
    case WX_CLEAR:
        if (night) moon_pic(cv, cx, cy, s * 0.26f, 0.15f, a);
        else sun_pic(cv, cx, cy, s * 0.2f, t, a);
        break;
    case WX_PARTLY:
        if (night) moon_pic(cv, cx - s * 0.16f, cy - s * 0.14f, s * 0.19f, 0.15f, a);
        else sun_pic(cv, cx - s * 0.15f, cy - s * 0.13f, s * 0.14f, t, a);
        cloud(cv, cx + s * 0.06f, cy + s * 0.1f, s * 0.72f, cc, a);
        break;
    case WX_FOG:
        for (int i = 0; i < 3; i++) {                  // banks of it, drifting a little
            const float y = cy - s * 0.15f + (float)i * s * 0.15f, dx = t ? sinf(t * 0.6f + (float)i * 2.0f) * s * 0.05f : 0.0f;
            gfx_line(cv, cx - s * 0.35f + dx, y, cx + s * 0.35f + dx, y, 1.5f, cc, a * 2 / 3);
        }
        break;
    default:
        cloud(cv, cx, cy - (kind == WX_CLOUDY ? 0 : s * 0.08f), s * 0.8f, cc, a);
        for (int i = 0; i < 3 && kind != WX_CLOUDY && kind != WX_STORM; i++) {
            // the drops fall from the cloud and fade, each at its own time; still, they hang where they were drawn
            const float speed = kind == WX_SNOW ? 0.45f : kind == WX_DRIZZLE ? 0.9f : 1.5f;
            const float ph = t ? fmodf(t * speed + (float)i * 0.37f, 1.0f) : 0.35f;
            const int fa = (int)((float)a * clampf(ph * 6.0f, 0, 1) * (1.0f - ph * ph));
            const float x = cx - s * 0.18f + (float)i * s * 0.18f, y = cy + s * 0.13f + ph * s * 0.26f;
            if (kind == WX_RAIN) gfx_line(cv, x, y, x - s * 0.05f, y + s * 0.13f, 1.6f, RAIN_C, fa);
            else if (kind == WX_DRIZZLE) gfx_disc(cv, x, y + s * 0.04f, s * 0.035f + 0.6f, RAIN_C, fa);
            else gfx_disc(cv, x + (t ? sinf(t * 1.7f + (float)i * 2.1f) * s * 0.05f : 0.0f), y + s * 0.04f, s * 0.045f + 0.6f, RGB(255, 255, 255), fa);
        }
        if (kind == WX_STORM) {
            const float x = cx, y = cy + s * 0.12f;
            gfx_line(cv, x + s * 0.05f, y, x - s * 0.06f, y + s * 0.13f, 1.8f, C_LOW, a);
            gfx_line(cv, x - s * 0.06f, y + s * 0.13f, x + s * 0.06f, y + s * 0.13f, 1.8f, C_LOW, a);
            gfx_line(cv, x + s * 0.06f, y + s * 0.13f, x - s * 0.05f, y + s * 0.28f, 1.8f, C_LOW, a);
        }
        break;
    }
}

/* the European air quality index in its own colours (the EEA's): good, fair, moderate, poor, very
 * poor, extremely poor. the UV index in the WHO's: low, moderate, high, very high, extreme */
static uint32_t aqi_colour(int aqi) { return AQI_C[aqi_level(aqi)]; }
static const char *aqi_word(int aqi)
{
    return aqi < 20 ? "GOOD" : aqi < 40 ? "FAIR" : aqi < 60 ? "MODERATE" : aqi < 80 ? "POOR" : aqi < 100 ? "VERY POOR" : "EXTREME";
}
static uint32_t uv_colour(int uv10) { return UV_C[uv_level(uv10)]; }
static const char *uv_word(int uv10)
{
    return uv10 < 30 ? "LOW" : uv10 < 60 ? "MODERATE" : uv10 < 80 ? "HIGH" : uv10 < 110 ? "VERY HIGH" : "EXTREME";
}

/* a scale of its steps in their colours, best to worst, the one now lit and standing taller: where the value is
 * on it, seen before the word is read */
static void scale_bar(canvas_t *cv, int x, int y, int w, int n, int lv, const uint32_t *cs)
{
    if (!gfx_touches(cv, y - 1, y + 4) || w < n * 3) return;
    const int sw = (w - (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        const int sx = x + i * (sw + 1);
        if (i == lv) gfx_fill(cv, sx, y - 1, sw, 5, cs[i], 255);
        else gfx_fill(cv, sx, y, sw, 3, cs[i], 80);
    }
}

/* --- the warnings' words and colours, for every look --- */

const char *alert_word(const wx_alert_t *a, int k)
{
    switch (k) {
    case WXA_STORM:    return a->thunder ? "THUNDER" : "STORM";
    case WXA_PRESSURE: return "PRESSURE";
    case WXA_AIR:      return "AIR";
    default:           return "RADIATION";
    }
}

void alert_said(const wx_alert_t *a, int k, char *b, int sz)
{
    switch (k) {
    case WXA_STORM:
        snprintf(b, (size_t)sz, "%s %s", a->thunder ? "THUNDER" : "STORM", a->hour[k] ? "AHEAD" : "NOW");
        break;
    case WXA_PRESSURE: snprintf(b, (size_t)sz, a->value[k] < 0 ? "PRESSURE FALLING" : "PRESSURE RISING"); break;
    case WXA_AIR:      snprintf(b, (size_t)sz, a->level[k] == WXA_RED ? "VERY POOR AIR" : "POOR AIR"); break;
    default:           snprintf(b, (size_t)sz, "RADIATION HIGH"); break;
    }
}

uint32_t alert_colour(int level) { return level >= WXA_RED ? C_ALARM : C_LOW; }

/* the wind as an arrow the way it blows, longer the stronger, amber in gusts that knock you about */
static void wind_arrow(canvas_t *cv, float x, float y, int from_deg, int kmh, int gust, uint32_t c, int a)
{
    const float to = ((float)from_deg + 180.0f) * D2R, len = clampf(5.0f + (float)kmh * 0.35f, 7.0f, 16.0f);
    const float dx = sinf(to), dy = -cosf(to), nx = -dy, ny = dx;
    const uint32_t col = gust >= 50 ? C_LOW : c;
    const float x1 = x + dx * len * 0.5f, y1 = y + dy * len * 0.5f;
    gfx_line(cv, x - dx * len * 0.5f, y - dy * len * 0.5f, x1 - dx * 2.0f, y1 - dy * 2.0f, 1.5f, col, a);
    gfx_tri(cv, x1 + dx * 1.5f, y1 + dy * 1.5f, x1 - dx * 3.5f + nx * 3.0f, y1 - dy * 3.5f + ny * 3.0f,
            x1 - dx * 3.5f - nx * 3.0f, y1 - dy * 3.5f - ny * 3.0f, col, a);
}

static const char *const KIND_NAME[WX_KINDS] = { "CLEAR", "PARTLY CLOUDY", "CLOUDY", "FOG", "DRIZZLE", "RAIN", "SNOW", "THUNDER" };
static const char *const WDAY[7] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };

static void fmt_t(char *b, int sz, int t10) { snprintf(b, (size_t)sz, "%d\xC2\xB0", (int)lroundf(t_of(t10))); }

// the home's clock now, hh:mm; dashes till the board has heard the time
static void clock_now(const ui_t *u, const weather_t *w, char *b, int sz)
{
    if (!u->net.utc) {
        snprintf(b, (size_t)sz, "--:--");
        return;
    }
    int64_t s = ((int64_t)u->net.utc + (int64_t)w->utc_min * 60) % 86400;
    if (s < 0) s += 86400;
    snprintf(b, (size_t)sz, "%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60));
}

// the hour by the home's clock, tt hours from the forecast's first
static int clock_hour(const weather_t *w, int tt) { return (int)local_hours(w, (uint32_t)((int64_t)w->at + (int64_t)tt * 3600)); }

// the sun's next rise or set by the home's clock (make() finds it): SUNRISE 07:05. 0: none in the next day
static int sun_next(const weather_t *w, char *key, int ksz, char *val, int vsz)
{
    if (!W.sun_utc) return 0;
    snprintf(key, (size_t)ksz, W.sun_rise ? "SUNRISE" : "SUNSET");
    int64_t s = ((int64_t)W.sun_utc + (int64_t)w->utc_min * 60) % 86400;
    if (s < 0) s += 86400;
    snprintf(val, (size_t)vsz, "%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60));
    return 1;
}

/* --- on the dial, over the glass: hours, the sun's edges, rain, clouds, stars, now --- */

static void at_deg(float deg, float r, float *x, float *y)
{
    *x = (float)W.cx + sinf(deg * D2R) * r;
    *y = (float)SCY - cosf(deg * D2R) * r;
}

static uint32_t hash(uint32_t v) { v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; return v ^ (v >> 16); }
static float rnd01(uint32_t v) { return (float)(hash(v) & 0xFFFFu) / 65535.0f; }

static void seg(seg_t *s, float x0, float y0, float x1, float y1, float w, uint32_t c, int a)
{
    *s = (seg_t){ x0, y0, x1, y1, w, c, (uint8_t)clampi(a, 0, 255) };
}

/* the wind now, in the glass: fine streaks drifting the way it blows (north up), as fast as it
 * blows, surging when the gusts are much stronger than the wind. each streak lives a few seconds
 * and comes back somewhere else */
static void plan_air(const ui_t *u, const weather_t *w, wtab_t *T, float t)
{
    (void)u;
    T->n_air = 0;
    if (!w->wind_kmh) return;
    const float life = 2.6f, to = ((float)w->wind_from + 180.0f) * D2R, vx = sinf(to), vy = -cosf(to);
    const float speed = 3.0f + (float)w->wind_kmh * 0.6f;   // pixels a second
    const int gusty = w->gust_kmh > w->wind_kmh + 12, n = 30 + clampi(w->wind_kmh * 6 / 5, 0, N_AIR - 30);
    for (int i = 0; i < n && T->n_air < N_AIR; i++) {
        const float cyc = t / life + rnd01((uint32_t)i * 3u + 7u);
        const uint32_t k = (uint32_t)cyc;
        const float ph = cyc - (float)k, at = rnd01(k * 131u + (uint32_t)i * 7u);
        const float ang = at * 6.2832f, rad = gfx_sqrt(rnd01(k * 977u + (uint32_t)i * 13u)) * (float)(W_RIN - 6);
        const float g = gusty ? 1.0f + 0.5f * sinf(t * 1.7f + (float)i * 0.37f) : 1.0f, run = speed * g * ph * life;
        const float x = (float)W.cx + sinf(ang) * rad + vx * run, y = (float)SCY - cosf(ang) * rad + vy * run;
        const float r = gfx_sqrt((x - W.cx) * (x - W.cx) + (y - SCY) * (y - SCY));
        if (r > W_RIN - 5 || r < W_CORE + 3) continue;
        const float len = 2.5f + speed * g * 0.25f;
        seg(&T->air[T->n_air++], x - vx * len, y - vy * len, x, y, 1.0f, C_FOCUS, (int)(130.0f * sinf(ph * 3.14159265f)));
    }
}

/* stars, the hours' ticks and numbers, sunrise and sunset, clouds, rain, now, and every three hours
 * the wind and the temperature: where each is this frame */
static void plan_marks(const ui_t *u, const weather_t *w, wtab_t *T, float t)
{
    // stars in the night's part of the ring: there, twinkling each at its own pace
    T->n_star = 0;
    for (int i = 0; i < N_STARS; i++) {
        const float deg = rnd01((uint32_t)i * 2u + 1u) * 360.0f, r = W_RIN + 3 + rnd01((uint32_t)i * 2u + 2u) * (W_ROUT - W_RIN - 6);
        if (T->el[(int)deg % 360] > -8) continue;
        blob_t *s = &T->star[T->n_star++];
        at_deg(deg, r, &s->x, &s->y);
        const float tw = 0.5f + 0.5f * sinf(t * (0.8f + rnd01((uint32_t)i + 99u) * 1.6f) + (float)i);
        s->w = i % 7 == 0 ? 1.1f : 0.0f;              // a few brighter ones, a disc; the rest a point
        s->c = RGB(255, 255, 255);
        s->a = (uint8_t)(s->w > 0 ? 120.0f + 135.0f * tw : 110.0f + 145.0f * tw);
    }
    // the hours: a tick each on the ring's inner edge, a longer one and its number every six
    T->n_tick = T->n_lab = 0;
    for (int h = 0; h < 24; h++) {
        const float deg = dial_deg((float)h), l = h % 6 ? 3.0f : 7.0f;
        float x0, y0, x1, y1;
        at_deg(deg, W_RIN - l, &x0, &y0);
        at_deg(deg, W_RIN, &x1, &y1);
        seg(&T->tick[T->n_tick++], x0, y0, x1, y1, 1.0f, h % 6 ? C_PRIM : C_FOCUS, h % 6 ? 170 : 245);
        if (h % 6 == 0) {
            at_deg(deg, W_RIN - 14, &T->lab[T->n_lab].x, &T->lab[T->n_lab].y);
            snprintf(T->lab[T->n_lab++].s, 3, "%02d", h);
        }
    }
    // sunrise and sunset: where the sun crosses the horizon on the ring, in the colour of the sun low
    for (int d = 0; d < 360 && T->n_tick < 24 + 4; d++) {
        const int a = T->el[d] >= 0, b = T->el[(d + 1) % 360] >= 0;
        if (a == b) continue;
        float x0, y0, x1, y1;
        at_deg((float)d + 0.5f, W_RIN, &x0, &y0);
        at_deg((float)d + 0.5f, W_ROUT, &x1, &y1);
        seg(&T->tick[T->n_tick++], x0, y0, x1, y1, 1.6f, DAWN_C, 230);
    }
    /* the hours ahead: clouds on the ring, each as big as the sky is covered, every other hour
     * unless it is overcast; heavy ones grey with rain. rain under the ring and falling through it */
    const float band = (float)(W_ROUT - W_RIN);
    T->n_cloud = T->n_bar = T->n_drop = T->n_snow = 0;
    for (int i = 0; i < WX_HOURS; i++) {
        const wx_hour_t *hr = &w->hour[i];
        const float deg = wrap360(dial_deg(local_hours(w, w->at + (uint32_t)i * 3600u)) + 7.5f);
        const int night = T->el[(int)deg % 360] < -3;
        if (hr->cloud >= 35 && (hr->cloud >= 85 || i % 2 == 0)) {
            const float sway = 2.0f * sinf(t * 0.3f + (float)i * 1.7f);
            blob_t *c = &T->cloud[T->n_cloud++];
            at_deg(deg + sway, W_MID + (i % 2 ? 2.5f : -1.5f) + 1.2f * sinf(t * 0.45f + (float)i), &c->x, &c->y);
            c->w = band * (0.8f + 0.42f * (float)hr->cloud / 100.0f);
            c->c = hr->rain10 ? (night ? RGB(62, 72, 96) : RGB(168, 178, 196)) : night ? RGB(74, 86, 114) : RGB(244, 248, 253);
            c->a = (uint8_t)(night ? 180 : 245);
        }
        if (hr->rain10) {
            const float mm = (float)hr->rain10 / 10.0f, len = clampf(3.0f + mm * 8.0f, 3.0f, 16.0f);
            float x0, y0, x1, y1;
            at_deg(deg, W_RIN - 7, &x0, &y0);
            at_deg(deg, W_RIN - 7 - len, &x1, &y1);
            seg(&T->bar[T->n_bar++], x0, y0, x1, y1, 2.6f, RAIN_C, 235);
            const int snow = hr->kind == WX_SNOW, drops = 3 + clampi((int)(mm * 4.0f), 0, 5);
            for (int k = 0; k < drops; k++) {        // falling through the ring at that hour, slanted by the wind
                const float ph = fmodf(t * (snow ? 0.3f : 1.3f) + rnd01((uint32_t)(i * 16 + k)), 1.0f);
                float x, y;
                at_deg(deg + ((float)k / (float)(drops - 1) - 0.5f) * 13.0f, W_MID, &x, &y);
                y += (ph - 0.5f) * band * 1.3f;
                x += (ph - 0.5f) * 5.0f * (w->wind_from > 180 ? 1.0f : -1.0f);
                const float rr = gfx_sqrt((x - W.cx) * (x - W.cx) + (y - SCY) * (y - SCY));
                if (rr < W_RIN + 2 || rr > W_ROUT - 2) continue;
                const int fade = (int)(255.0f * clampf(sinf(ph * 3.14159265f) * 1.6f, 0, 1));
                if (snow && T->n_snow < N_DROPS) T->snow[T->n_snow++] = (blob_t){ x + sinf(t * 2.0f + (float)k) * 1.5f, y, 1.3f, RGB(255, 255, 255), (uint8_t)fade };
                else if (!snow && T->n_drop < N_DROPS) seg(&T->drop[T->n_drop++], x, y, x - 1.6f, y + 6.0f, 1.3f, RGB(140, 200, 255), fade);
            }
        }
    }
    // now: a hand from the middle out to the ring, as a clock's, and the sun or the moon on the ring at its tip
    float hx0, hy0, hx1, hy1;
    at_deg(W.now_deg, W_CORE + 3, &hx0, &hy0);
    at_deg(W.now_deg, W_RIN - 2, &hx1, &hy1);
    seg(&T->hand[0], hx0, hy0, hx1, hy1, 4.5f, C_FOCUS, 46);
    seg(&T->hand[1], hx0, hy0, hx1, hy1, 1.7f, C_FOCUS, 240);
    at_deg(W.now_deg, W_MID, &T->now_x, &T->now_y);
    T->now_sun = T->el[(int)W.now_deg % 360] > -1;
    T->phase = moon_phase(u->net.utc ? u->net.utc : w->at);
    // every three hours: the wind there as an arrow on the loop, the temperature's number outside it in its colour
    T->n_pt = 0;
    for (int i = 0; i < WX_HOURS; i += 3) {
        const float deg = dial_deg(local_hours(w, w->at + (uint32_t)i * 3600u));
        const float rl = (float)T->loop2[(int)deg % 360] * 0.5f;
        pt_t *p = &T->pt[T->n_pt++];
        at_deg(deg, rl, &p->x, &p->y);
        at_deg(deg, rl + 11.0f, &p->lx, &p->ly);
        p->from = (int16_t)(w->hour[i].wind_from2 * 2);
        p->kmh = w->hour[i].wind_kmh;
        p->gust = w->hour[i].gust_kmh;
        p->c = temp_colour(w->hour[i].t10);
        fmt_t(p->t, sizeof p->t, w->hour[i].t10);
    }
}

static int strip_has(const canvas_t *cv, float y0, float y1) { return gfx_touches(cv, (int)y0 - 2, (int)y1 + 2); }
static int seg_here(const canvas_t *cv, const seg_t *s) { return strip_has(cv, s->y0 < s->y1 ? s->y0 : s->y1, s->y0 < s->y1 ? s->y1 : s->y0); }
static void seg_draw(canvas_t *cv, const seg_t *s) { if (seg_here(cv, s)) gfx_line(cv, s->x0, s->y0, s->x1, s->y1, s->w, s->c, s->a); }

static void draw_marks(canvas_t *cv, const wtab_t *T)
{
    for (int i = 0; i < T->n_air; i++) seg_draw(cv, &T->air[i]);   // the wind now, under all the rest
    for (int i = 0; i < T->n_star; i++) {
        const blob_t *s = &T->star[i];
        if (!strip_has(cv, s->y - 1, s->y + 1)) continue;
        if (s->w > 0) gfx_disc(cv, s->x, s->y, s->w, s->c, s->a);
        else gfx_dot(cv, s->x, s->y, s->c, s->a);
    }
    for (int i = 0; i < T->n_tick; i++) seg_draw(cv, &T->tick[i]);
    seg_draw(cv, &T->hand[0]);
    seg_draw(cv, &T->hand[1]);
    for (int i = 0; i < T->n_lab; i++)
        if (strip_has(cv, T->lab[i].y - 3, T->lab[i].y + 3))
            micro_text(cv, (int)T->lab[i].x - micro_width(T->lab[i].s, 1) / 2, (int)T->lab[i].y - 3, T->lab[i].s, C_PRIM, 255, 1);
    for (int i = 0; i < T->n_cloud; i++) {
        const blob_t *c = &T->cloud[i];
        if (strip_has(cv, c->y - c->w * 0.4f, c->y + c->w * 0.25f)) cloud(cv, c->x, c->y, c->w, c->c, c->a);
    }
    for (int i = 0; i < T->n_bar; i++) seg_draw(cv, &T->bar[i]);
    for (int i = 0; i < T->n_drop; i++) seg_draw(cv, &T->drop[i]);
    for (int i = 0; i < T->n_snow; i++)
        if (strip_has(cv, T->snow[i].y - 2, T->snow[i].y + 2)) gfx_disc(cv, T->snow[i].x, T->snow[i].y, T->snow[i].w, T->snow[i].c, T->snow[i].a);
    if (strip_has(cv, T->now_y - 18, T->now_y + 18)) {   // now: the sun, or the moon as it is tonight
        if (T->now_sun) sun_pic(cv, T->now_x, T->now_y, 6.5f, W.anim, 255);
        else {
            gfx_glow(cv, T->now_x, T->now_y, 16.0f, RGB(160, 176, 210), 60);
            moon_pic(cv, T->now_x, T->now_y, 7.0f, T->phase, 255);
        }
    }
    for (int i = 0; i < T->n_pt; i++) {
        const pt_t *p = &T->pt[i];
        if (strip_has(cv, p->y - 10, p->y + 10)) wind_arrow(cv, p->x, p->y, p->from, p->kmh, p->gust, C_FOCUS, 240);
        if (strip_has(cv, p->ly - 8, p->ly + 8)) {
            const int tw = font_width(&FONT_SMALL, p->t);
            font_text(cv, &FONT_SMALL, (int)p->lx - tw / 2, (int)p->ly - FONT_LINE_H(&FONT_SMALL) / 2, p->t, p->c, 250);
        }
    }
}

/* now, in the middle where the hand starts: its picture, the temperature in big digits in its colour, the
 * degree a ring, and under them the home's clock */
static void dial_core(ui_t *u, canvas_t *cv, const weather_t *w)
{
    const int cx = W.cx;
    if (!strip_has(cv, (float)(SCY - W_CORE), (float)(SCY + W_CORE))) return;
    char s[8];
    const int night = W.t->el[(int)W.now_deg % 360] < -1;
    wx_pic(cv, w->now_kind, night, (float)cx, (float)SCY - W_CORE * 0.68f, W_CORE * 0.5f, W.anim, 255);
    snprintf(s, sizeof s, "%d", (int)lroundf(t_of(w->now_t10)));
    const font_t *f = W_CORE >= 34 ? &FONT_HUGE : &FONT_LARGE;
    const int tw = font_width(f, s), th = FONT_ASCENT(f);
    const int x = cx - tw / 2 - 3, y = SCY - th / 2 - W_CORE / 12;
    font_text(cv, f, x, y - (FONT_LINE_H(f) - th) / 2, s, temp_colour(w->now_t10), 255);
    gfx_ring(cv, (float)(x + tw + 4), (float)y + 3.0f, f == &FONT_HUGE ? 3.2f : 2.4f, 1.6f, temp_colour(w->now_t10), 255);
    clock_now(u, w, s, sizeof s);
    const font_t *cf = W_CORE >= 34 ? &FONT_MEDIUM : &FONT_SMALL;
    font_text(cv, cf, cx - font_width(cf, s) / 2, y + th + (W_CORE >= 34 ? 4 : 2), s, C_FOCUS, 235);
}

/* --- the panel: what the dial shows, said --- */

/* a row of the panel: what, on the left, its value on the right. the 2.8"'s narrow panel says what
 * in the pixel font */
static int prow(canvas_t *cv, int y, const char *key, const char *value, uint32_t c)
{
    if (PANEL_WIDE) font_text(cv, &FONT_SMALL, TX, y, key, C_DIM, 255);
    else micro_text(cv, TX, y + 3, key, C_DIM, 240, 1);
    font_text(cv, &FONT_SMALL, TX2 - font_width(&FONT_SMALL, value), y, value, c, 255);
    return y + FONT_LINE_H(&FONT_SMALL) + (PANEL_WIDE ? 2 : 1);
}

// the panel's frame, title and the radar's three buttons, where they always are; the three dots in its corner
static int panel_frame(ui_t *u, canvas_t *cv)
{
    chamfer_box(cv, PX, 2, PW, GFX_H - 4, C_PANEL, 120, C_QUIET, 200);
    const int end = RADAR_HAS_TOUCH ? RDOTS_X - 4 : TX2;
    if (PANEL_WIDE) font_text(cv, &FONT_SMALL, TX, 4, "OWLOS WEATHER", C_FOCUS, 245);
    else micro_text(cv, TX, 7, "WEATHER", C_FOCUS, 240, 1);
    const int tw = PANEL_WIDE ? font_width(&FONT_SMALL, "OWLOS WEATHER") : micro_width("WEATHER", 1);
    if (end - TX - tw - 4 > 4) gfx_hatch(cv, TX + tw + 4, 7, end - TX - tw - 4, 5, C_QUIET, 130);
    gfx_hline(cv, TX, end, 15, C_QUIET, 150);
    static const char *const L[3] = { "HUE", "LIST", "SET" };
    for (int i = 0; i < 3; i++) {
        const int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_BTN + i;
        button(cv, TX - 3 + i * (BTN_W + 3), BTN_Y, BTN_W, BTN_H, L[i], pressed ? 2 : 0);
    }
    return 20;
}

// which way the pressure goes: down to the right, or up to the right
static void pressure_arrow(canvas_t *cv, float x, float y, int down, uint32_t c)
{
    const float y0 = down ? y : y + 6, y1 = down ? y + 6 : y;
    gfx_line(cv, x, y0, x + 6, y1, 1.6f, c, 255);
    gfx_line(cv, x + 6, down ? y + 1 : y + 5, x + 6, y1, 1.6f, c, 255);
    gfx_line(cv, x + 1, y1, x + 6, y1, 1.6f, c, 255);
}

/* a warning's second line in the panel: how much, and when by the home's clock. the 2.8"'s narrow
 * panel has some twenty letters of the pixel font: the gusts without their word, the station without
 * how far */
static void alert_detail(const weather_t *w, const wx_alert_t *a, int k, char *b, int sz, int wide)
{
    char when[12];
    if (a->hour[k]) {
        const int h = (int)local_hours(w, w->at + (uint32_t)a->hour[k] * 3600u);
        snprintf(when, sizeof when, "%s %02d:00", k == WXA_PRESSURE ? "BY" : "AT", h);
    } else snprintf(when, sizeof when, "NOW");
    char d[20];
    switch (k) {
    case WXA_STORM:
        if (a->thunder) snprintf(b, (size_t)sz, "%s", when);
        else if (wide) snprintf(b, (size_t)sz, "GUSTS %d %s %s", w_of(a->value[k]), WIND_NAME[W.wu], when);
        else snprintf(b, (size_t)sz, "%d %s %s", w_of(a->value[k]), WIND_NAME[W.wu], when);
        break;
    case WXA_PRESSURE:
        fmt_dp(d, sizeof d, dp_of(a->value[k], w));
        snprintf(b, (size_t)sz, "%s %s", d, when);
        break;
    case WXA_AIR:      snprintf(b, (size_t)sz, "INDEX %d %s", a->value[k], when); break;
    default:
        if (wide) snprintf(b, (size_t)sz, "%d.%02d \xC2\xB5SV/H, %d KM", a->value[k] / 1000, a->value[k] % 1000 / 10, w->gamma_km);
        else snprintf(b, (size_t)sz, "%d.%02d USV/H", a->value[k] / 1000, a->value[k] % 1000 / 10);
        break;
    }
}

/* the scope's corners, as the radar has them: where, and from where how fresh, on the left; the
 * look's button on the right (the three dots go over the panel, after it) */
static void corners(ui_t *u, canvas_t *cv, const weather_t *w)
{
    if (RADAR_ROUND || !gfx_touches(cv, 0, LOOK_Y + LOOK_H + 2 * FONT_LINE_H(&FONT_SMALL))) return;   // the round glass has no corners
    char v[24];
    if (u->cfg.place[0]) font_text_clip(cv, &FONT_SMALL, 6, 4, u->cfg.place, C_FOCUS, 245, view_cx(u) - 40);
    if (w) {
        const float h = local_hours(w, w->updated ? w->updated : w->at);
        snprintf(v, sizeof v, "OPEN-METEO %02d:%02d", (int)h, (int)(fmodf(h, 1.0f) * 60.0f));   // its licence asks for the name
        micro_text(cv, 6, 6 + FONT_LINE_H(&FONT_SMALL), v, C_DIM, 220, 1);
    }
    const int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_LOOK;
    button(cv, look_x(u), LOOK_Y, look_w(u), LOOK_H, "WEATHER", pressed ? 2 : 0);
}

/* the panel, row by row. a strip only words the rows it reaches: the panel is as tall as the screen,
 * and every strip of a frame comes through here */
static void panel(ui_t *u, canvas_t *cv, const weather_t *w)
{
    char v[32];
    const int wide = PANEL_WIDE, step = FONT_LINE_H(&FONT_SMALL) + (wide ? 2 : 1);
#define HERE(y, h) gfx_touches(cv, (y) - 1, (y) + (h) + 1)
    int y = panel_frame(u, cv);
    const font_t *kf = wide ? &FONT_MEDIUM : &FONT_SMALL;
    if (HERE(y, FONT_LINE_H(kf))) font_text_clip(cv, kf, TX, y, KIND_NAME[w->now_kind < WX_KINDS ? w->now_kind : 0], C_FOCUS, 255, TX2 - TX);
    y += FONT_LINE_H(kf) + (wide ? 4 : 2);
    if (HERE(y, step)) {
        fmt_t(v, sizeof v, w->feels10);
        prow(cv, y, wide ? "FEELS LIKE" : "FEELS", v, temp_colour(w->feels10));
    }
    y += step;
    if (HERE(y, step)) {
        snprintf(v, sizeof v, "%d%%", w->humidity);
        prow(cv, y, "HUMIDITY", v, C_FOCUS);
    }
    y += step;
    if (HERE(y, step)) {
        const char *from = compass8((float)w->wind_from);
        if (w->gust_kmh >= w->wind_kmh + 10) {
            if (wide) snprintf(v, sizeof v, "%s %d-%d %s", from, w_of(w->wind_kmh), w_of(w->gust_kmh), WIND_NAME[W.wu]);
            else snprintf(v, sizeof v, "%s %d-%d", from, w_of(w->wind_kmh), w_of(w->gust_kmh));
        } else if (wide) snprintf(v, sizeof v, "%s %d %s", from, w_of(w->wind_kmh), WIND_NAME[W.wu]);
        else snprintf(v, sizeof v, "%s %d", from, w_of(w->wind_kmh));
        prow(cv, y, "WIND", v, w->gust_kmh >= 50 ? C_LOW : C_FOCUS);
    }
    y += step;
    /* pressure: an arrow the way it goes over the next hours, amber or red as far as it goes to warn of.
     * the rows say what the forecast says, whichever warnings SET has off */
    wx_alert_t all;
    wx_alerts(w, WXA_ALL, &all);
    if (HERE(y, step)) {
        const int lv = all.level[WXA_PRESSURE], d = all.value[WXA_PRESSURE];
        fmt_p(v, sizeof v, p_of(w->now_hpa10, w->now_t10), wide);
        prow(cv, y, "PRESSURE", v, lv ? alert_colour(lv) : C_FOCUS);
        if (d <= -20 || d >= 20) pressure_arrow(cv, (float)(TX2 - font_width(&FONT_SMALL, v) - 12), (float)y + 3.0f, d < 0, lv ? alert_colour(lv) : C_DIM);
    }
    y += step;
    // the air: the index's word in its colour, the fine dust beside it, and under them where it is on the index's scale
    if (w->aqi != WX_NONE) {
        if (HERE(y, step + 5)) {
            if (wide) snprintf(v, sizeof v, "%s %d.%d", aqi_word(w->aqi), w->pm25_10 / 10, w->pm25_10 % 10);
            else snprintf(v, sizeof v, "%s", aqi_word(w->aqi));
            prow(cv, y, wide ? "AIR PM2.5" : "AIR", v, aqi_colour(w->aqi));
            scale_bar(cv, TX, y + step + 1, TX2 - TX, 6, aqi_level(w->aqi), AQI_C);
        }
        y += step + 5;
    }
    if (wide || w->uv10 >= 30) {
        if (HERE(y, step + 5)) {
            snprintf(v, sizeof v, "%d %s", (w->uv10 + 5) / 10, uv_word(w->uv10));
            prow(cv, y, "UV", v, w->uv10 >= 30 ? uv_colour(w->uv10) : C_FOCUS);
            scale_bar(cv, TX, y + step + 1, TX2 - TX, 5, uv_level(w->uv10), UV_C);
        }
        y += step + 5;
    }
    /* the background radiation at the nearest station: ordinary is a tenth of a microsievert an hour. till a source
     * gives it, the sun's next rise or set in its place */
    char sk[12];
    if (w->gamma_nsv) {
        if (HERE(y, step)) {
            snprintf(v, sizeof v, wide ? "%d.%02d \xC2\xB5SV/H" : "%d.%02d", w->gamma_nsv / 1000, w->gamma_nsv % 1000 / 10);
            prow(cv, y, "RADIATION", v, w->gamma_nsv >= 1000 ? C_ALARM : w->gamma_nsv >= 300 ? C_LOW : C_FOCUS);
        }
        y += step;
    } else if (sun_next(w, sk, sizeof sk, v, sizeof v)) {
        if (HERE(y, step)) prow(cv, y, sk, v, DAWN_C);
        y += step;
    }
    if (HERE(y, step)) {
        int rain_at = -1;
        for (int i = 0; i < WX_HOURS; i++) if (w->hour[i].rain10 >= 2) { rain_at = i; break; }
        if (rain_at >= 0) {
            const float h = local_hours(w, w->at + (uint32_t)rain_at * 3600u);
            snprintf(v, sizeof v, rain_at ? (wide ? "FROM %02d:00" : "%02d:00") : "NOW", (int)h);
        } else snprintf(v, sizeof v, wide ? "NONE TODAY" : "NONE");
        prow(cv, y, w->hour[rain_at >= 0 ? rain_at : 0].kind == WX_SNOW ? "SNOW" : "RAIN", v, rain_at >= 0 ? RAIN_C : C_DIM);
    }
    y += step;
    // the warnings SET leaves on, each in a box that breathes, never blinks: what, then how much and when
    for (int k = 0; k < WXA_KINDS; k++) {
        const int lv = u->alert.level[k], lh = wide ? FONT_LINE_H(&FONT_SMALL) : 8, bh = 2 * lh + 8;
        if (!lv) continue;
        y += 2;
        if (HERE(y, bh)) {
            const float b = 0.5f + 0.5f * sinf((float)u->now / 1000.0f * 2.0f);
            const uint32_t c = alert_colour(lv);
            char head[24];
            alert_said(&u->alert, k, head, sizeof head);
            alert_detail(w, &u->alert, k, v, sizeof v, wide);
            chamfer_box(cv, TX - 3, y, PW - 6, bh, c, (int)(18.0f + 26.0f * b), c, (int)(140.0f + 115.0f * b));
            if (wide) {
                font_text(cv, &FONT_SMALL, TX + 2, y + 3, head, c, 255);
                font_text(cv, &FONT_SMALL, TX + 2, y + 4 + lh, v, c, 230);
            } else {
                micro_text(cv, TX + 1, y + 4, head, c, 255, 1);
                micro_text(cv, TX + 1, y + 4 + lh, v, c, 230, 1);
            }
        }
        y += bh + 4;
    }
    y += 2;
    if (HERE(y, step)) {
        if (wide) font_text(cv, &FONT_SMALL, TX, y, "NEXT DAYS", C_PRIM, 255);
        else micro_text(cv, TX, y + 2, "NEXT DAYS", C_PRIM, 255, 1);
    }
    y += (wide ? FONT_LINE_H(&FONT_SMALL) : 9) + 1;
    gfx_hline(cv, PX + 2, PX + PW - 2, y, C_QUIET, 160);
    y += 3;
    int nd = WX_DAYS, rh = (BTN_Y - 4 - y) / nd;
    while (nd > 1 && rh < 15) rh = (BTN_Y - 4 - y) / --nd;   // as many days as fit, the nearest first
    for (int i = 0; i < nd && rh >= 12; i++, y += rh) {
        if (!HERE(y, rh)) continue;
        const wx_day_t *d = &w->day[i];
        const int ty = y + (rh - FONT_LINE_H(&FONT_SMALL)) / 2;
        if (wide) font_text(cv, &FONT_SMALL, TX, ty, WDAY[d->wday % 7], C_DIM, 255);
        else micro_text(cv, TX, y + rh / 2 - 3, WDAY[d->wday % 7], C_DIM, 255, 1);
        wx_pic(cv, d->kind, 0, (float)(TX + (wide ? 44 : 24)), (float)y + rh * 0.5f, (float)rh * 0.95f, W.anim, 255);
        char hi[8], lo[8], both[20];
        fmt_t(hi, sizeof hi, d->hi10);
        fmt_t(lo, sizeof lo, d->lo10);
        snprintf(both, sizeof both, wide ? "%s / %s" : "%s/%s", hi, lo);
        font_text(cv, &FONT_SMALL, TX2 - font_width(&FONT_SMALL, both), ty, both, C_FOCUS, 255);
    }
#undef HERE
}

/* --- without the panel: now's numbers either side of the dial, what each is in the pixel font over its
 * value, as an instrument's gauges. a warning SET leaves on stands in a box that breathes round its
 * number, as the panel's boxes do --- */

/* what an item is in the pixel font, its value under it with its unit small after it, and for the air and the UV
 * their scale under that */
typedef struct {
    char key[24], val[24], unit[8];
    uint32_t c;
    int lv, arrow;                 /* a warning's level round it; the pressure's way, -1 down, 1 up */
    int sn, slv;                   /* the scale's steps and the one now, sn 0: none */
    const uint32_t *sc;
} item_t;

#define HUD_SCALE 6                /* the rows a scale takes under its value */

// how wide the value and its unit are, as drawn
static int item_w(const item_t *it, const font_t *vf)
{
    return font_width(vf, it->val) + (it->arrow ? 10 : 0) + (it->unit[0] ? 3 + micro_width(it->unit, 1) : 0);
}

static void hud_item(canvas_t *cv, const item_t *it, int x, int y, int right, const font_t *vf, int bw, float br)
{
    const int vh = FONT_LINE_H(vf), h = MICRO_H + 3 + vh + (it->sn ? HUD_SCALE : 0), aw = it->arrow ? 10 : 0;
    const int kw = micro_width(it->key, 1), vw = item_w(it, vf), sw = it->sn ? bw : 0;
    const int w = kw > vw ? (kw > sw ? kw : sw) : (vw > sw ? vw : sw);
    const int x0 = right ? x - w : x, vy = y + MICRO_H + 3;
    if (!gfx_touches(cv, y - 4, y + h + 4)) return;
    if (it->lv) {
        const uint32_t ac = alert_colour(it->lv);
        chamfer_box(cv, x0 - 4, y - 3, w + 8, h + 5, ac, (int)(18.0f + 26.0f * br), ac, (int)(140.0f + 115.0f * br));
    }
    micro_text(cv, right ? x - kw : x, y, it->key, it->lv ? alert_colour(it->lv) : C_DIM, 235, 1);
    int vx = right ? x - vw : x;
    if (it->arrow) pressure_arrow(cv, (float)vx, (float)(vy + (FONT_ASCENT(vf) - 6) / 2), it->arrow < 0, it->c);
    vx += aw;
    font_text(cv, vf, vx, vy, it->val, it->c, 255);
    if (it->unit[0])                                   // the unit on the value's baseline, small
        micro_text(cv, vx + font_width(vf, it->val) + 3, vy + FONT_ASCENT(vf) - MICRO_H + 1, it->unit, C_DIM, 230, 1);
    if (it->sn) scale_bar(cv, right ? x - bw : x, vy + vh + 2, bw, it->sn, it->slv, it->sc);
}

// the room beside the dial for a thing at the rows y .. y + h: little by its middle, more up and down where the circle draws in
static int room_at(int y, int h)
{
    const int r = SR + 7, a = y + h < SCY ? SCY - (y + h) : y > SCY ? y - SCY : 0;
    const int half = a >= r ? 0 : (int)gfx_sqrt((float)(r * r - a * a));
    return W.cx - half - 10;
}

// the first of the ways to say a value that fits its room (b2 none: the first anyway); the unit goes where it does not fit
static void fit(item_t *it, const font_t *f, int room, const char *a, const char *b2)
{
    snprintf(it->val, sizeof it->val, "%s", a);
    if (b2 && item_w(it, f) > room) snprintf(it->val, sizeof it->val, "%s", b2);
    if (item_w(it, f) > room) it->unit[0] = 0;
}

/* four a side, a golden step apart (2.618 times an item's height), the longest words in the corners where the
 * circle leaves the most room: on the left how it is to be out in, how it feels, the damp, the rain, the sun's
 * UV; on the right what the air does, the wind, the pressure, the radiation (or till it has a source the sun's
 * next rise or set), the air itself. each number with its unit, the air's and the UV's with their scales */
static void hud(ui_t *u, canvas_t *cv, const weather_t *w)
{
    if (RADAR_ROUND || panel_shown(u)) return;
    const int wide = W.cx - SR >= 80, bw = wide ? 66 : 36;
    const font_t *vf = wide ? &FONT_MEDIUM : &FONT_SMALL;
    const int ih = MICRO_H + 3 + FONT_LINE_H(vf) + HUD_SCALE;
    const int top = LOOK_Y + LOOK_H + 12, bottom = GFX_H - 10;   // under the corners' words and the look's button
    int step = (int)((float)ih * 2.618f);
    if (3 * step + ih > bottom - top) step = (bottom - top - ih) / 3;
    const int y0 = top + (bottom - top - (3 * step + ih)) / 2;
    if (!gfx_touches(cv, y0 - 4, y0 + 3 * step + ih + 4)) return;
    int room[4];
    for (int i = 0; i < 4; i++) room[i] = room_at(y0 + i * step, ih);
    item_t L[4], R[4];
    char a[24], b[16];
    memset(L, 0, sizeof L);
    memset(R, 0, sizeof R);
    wx_alert_t all;
    wx_alerts(w, WXA_ALL, &all);                       // the numbers say what the forecast says, as the panel's rows do
    snprintf(L[0].key, sizeof L[0].key, wide ? "FEELS LIKE" : "FEELS");
    fmt_t(L[0].val, sizeof L[0].val, w->feels10);
    L[0].c = temp_colour(w->feels10);
    snprintf(L[1].key, sizeof L[1].key, "HUMIDITY");
    snprintf(L[1].val, sizeof L[1].val, "%d%%", w->humidity);
    L[1].c = C_FOCUS;
    int rain_at = -1;
    for (int i = 0; i < WX_HOURS; i++) if (w->hour[i].rain10 >= 2) { rain_at = i; break; }
    snprintf(L[2].key, sizeof L[2].key, w->hour[rain_at >= 0 ? rain_at : 0].kind == WX_SNOW ? "SNOW" : "RAIN");
    if (rain_at > 0) {
        snprintf(a, sizeof a, "FROM %02d:00", clock_hour(w, rain_at));
        snprintf(b, sizeof b, "%02d:00", clock_hour(w, rain_at));
        fit(&L[2], vf, room[2], a, b);
    } else snprintf(L[2].val, sizeof L[2].val, rain_at == 0 ? "NOW" : "NONE");
    L[2].c = rain_at >= 0 ? RAIN_C : C_DIM;
    if (u->alert.thunder) L[2].lv = u->alert.level[WXA_STORM];
    snprintf(L[3].key, sizeof L[3].key, wide ? "UV INDEX" : "UV");
    snprintf(a, sizeof a, "%d %s", (w->uv10 + 5) / 10, uv_word(w->uv10));
    fit(&L[3], vf, room[3], a, uv_word(w->uv10));      // the index and its word, or the word: it says what to do
    if (item_w(&L[3], vf) > room[3]) snprintf(L[3].val, sizeof L[3].val, "%d", (w->uv10 + 5) / 10);
    L[3].c = w->uv10 >= 30 ? uv_colour(w->uv10) : C_FOCUS;
    L[3].sn = 5;
    L[3].slv = uv_level(w->uv10);
    L[3].sc = UV_C;
    // the wind: where from in front of how strong, or in its key where the two do not fit
    const char *from = compass8((float)w->wind_from);
    if (w->gust_kmh >= w->wind_kmh + 10) snprintf(b, sizeof b, "%d-%d", w_of(w->wind_kmh), w_of(w->gust_kmh));
    else snprintf(b, sizeof b, "%d", w_of(w->wind_kmh));
    snprintf(a, sizeof a, "%s %s", from, b);
    snprintf(R[0].unit, sizeof R[0].unit, "%s", WIND_NAME[W.wu]);
    snprintf(R[0].key, sizeof R[0].key, "WIND");
    snprintf(R[0].val, sizeof R[0].val, "%s", a);
    if (item_w(&R[0], vf) > room[0]) {
        snprintf(R[0].key, sizeof R[0].key, "WIND %s", from);
        fit(&R[0], vf, room[0], b, NULL);
    }
    R[0].c = w->gust_kmh >= 50 ? C_LOW : C_FOCUS;
    if (!u->alert.thunder) R[0].lv = u->alert.level[WXA_STORM];
    const int plv = all.level[WXA_PRESSURE], pd = all.value[WXA_PRESSURE];
    snprintf(R[1].key, sizeof R[1].key, "PRESSURE");
    snprintf(R[1].unit, sizeof R[1].unit, "%s", PRES_NAME[W.pu]);
    R[1].arrow = pd <= -20 ? -1 : pd >= 20 ? 1 : 0;
    fmt_p(a, sizeof a, p_of(w->now_hpa10, w->now_t10), 0);
    fit(&R[1], vf, room[1], a, NULL);
    R[1].c = plv ? alert_colour(plv) : C_FOCUS;
    R[1].lv = u->alert.level[WXA_PRESSURE];
    if (w->gamma_nsv) {                                // the radiation; till a source gives it, the sun's next rise or set
        snprintf(R[2].key, sizeof R[2].key, "RADIATION");
        snprintf(R[2].unit, sizeof R[2].unit, "USV/H");
        snprintf(a, sizeof a, "%d.%02d", w->gamma_nsv / 1000, w->gamma_nsv % 1000 / 10);
        fit(&R[2], vf, room[2], a, NULL);
        R[2].c = w->gamma_nsv >= 1000 ? C_ALARM : w->gamma_nsv >= 300 ? C_LOW : C_FOCUS;
        R[2].lv = u->alert.level[WXA_RADIATION];
    } else if (sun_next(w, R[2].key, sizeof R[2].key, R[2].val, sizeof R[2].val)) {
        R[2].c = DAWN_C;
    } else {
        snprintf(R[2].key, sizeof R[2].key, "SUN");
        snprintf(R[2].val, sizeof R[2].val, "--");
        R[2].c = C_DIM;
    }
    snprintf(R[3].key, sizeof R[3].key, "AIR");
    if (w->aqi != WX_NONE) {                           // the index's word and the fine dust, the word alone, or its number
        snprintf(a, sizeof a, "%s %d.%d", aqi_word(w->aqi), w->pm25_10 / 10, w->pm25_10 % 10);
        if (wide && font_width(vf, a) <= room[3]) {
            snprintf(R[3].key, sizeof R[3].key, "AIR PM2.5");
            snprintf(R[3].val, sizeof R[3].val, "%s", a);
        } else {
            snprintf(b, sizeof b, "%d", w->aqi);
            fit(&R[3], vf, room[3], aqi_word(w->aqi), b);
        }
        R[3].c = aqi_colour(w->aqi);
        R[3].sn = 6;
        R[3].slv = aqi_level(w->aqi);
        R[3].sc = AQI_C;
    } else {
        snprintf(R[3].val, sizeof R[3].val, "--");
        R[3].c = C_DIM;
    }
    R[3].lv = u->alert.level[WXA_AIR];
    const float br = 0.5f + 0.5f * sinf((float)u->now / 1000.0f * 2.0f);
    for (int i = 0; i < 4; i++) {
        hud_item(cv, &L[i], 8, y0 + i * step, 0, vf, bw, br);
        hud_item(cv, &R[i], GFX_W - 8, y0 + i * step, 1, vf, bw, br);
    }
}

/* --- the pages behind the dial: the pressure and the temperature as graphs on one time axis, PG_PAST hours
 * before now on the left (now at about the golden section of the width), the next day on the right. the same
 * in both: now a line and a dot, the weather's pictures on the line every three hours, the sun rising and
 * setting on the time's line; the past thinner and fainter than what is to come. each in a frame where the
 * dial is, as wide as the screen without the panel; the round glass keeps them inside its circle --- */

const char *const WX_PAGE_NAME[WX_PAGES] = { "24 HOURS", "PRESSURE", "TEMPERATURE" };

void next_wx_page(ui_t *u)
{
    if (!u->wx || !u->wx->at) return;                  // no forecast yet: the glass says why, there are no pages
    u->wx_page = (uint8_t)((u->wx_page + 1) % WX_PAGES);
    toast(u, WX_PAGE_NAME[u->wx_page]);
}

#if RADAR_ROUND
#define PG_X0   (SCX - 86)
#define PG_X1   (SCX + 86)
#define PG_TY   (SCY - 86)                 /* the title and under it now, where the glass is wide enough */
#define PG_Y0   (SCY - 60)
#define PG_Y1   (SCY + 62)
#define PG_FY   (SCY + 67)                 /* a line under the frame */
#else
#define PG_X0   4
#define PG_TY   (LOOK_Y + LOOK_H + 5)
#define PG_FOOT (2 * FONT_LINE_H(&FONT_SMALL) + 8)
#define PG_FY   (GFX_H - 12 - PG_FOOT)     /* the footer: three numbers, or two lines */
#define PG_Y0   (PG_TY + FONT_LINE_H(&FONT_SMALL) + 4)
#define PG_Y1   (PG_FY - 4)
#endif
#define PG_REVEAL_MS 900u                  /* a graph drawn in from the left when its page comes */

// the frame's right edge: beside the panel, or as far as the screen goes without it
static int pg_right(const ui_t *u)
{
#if RADAR_ROUND
    (void)u;
    return PG_X1;
#else
    return panel_shown(u) ? PX - 6 : GFX_W - 5;
#endif
}

/* where a graph's things go, the same for planning it and for drawing it. both graphs alike: the scale's numbers
 * in a column of their own on the left, the time from there, now in the same place */
typedef struct {
    int   fx0, fy0, fx1, fy1;      /* the frame */
    int   gut;                     /* the scale's column: its numbers end here */
    int   every;                   /* hours between the pictures: three, or six where three would touch */
    float x0, k;                   /* the time: hour -PG_PAST from hour[0] at x0, k pixels an hour */
    float top, bot, base;          /* where the scale's high end and its low end stand; the time's line */
    float vlo, ky;                 /* the value at bot, in the unit shown, and pixels a unit */
    float s;                       /* a picture's size */
} geom_t;

static void geom(const ui_t *u, const wtab_t *T, int temp, geom_t *g)
{
    const int lh = FONT_LINE_H(&FONT_SMALL);
    g->fx0 = PG_X0;
    g->fx1 = pg_right(u);
    g->fy0 = PG_Y0;
    g->fy1 = PG_Y1;
    g->gut = g->fx0 + 4 + micro_width(W.pu == PRES_INHG ? "00.00" : "1020", 1);   // as wide on both pages: now in one place
    g->s = clampf((float)(g->fx1 - g->gut - 18) / 13.0f * 0.85f, 11.0f, 22.0f);
    g->x0 = (float)g->gut + 3.0f + g->s * 0.5f;
    g->k = ((float)g->fx1 - g->s * 0.5f - 4.0f - g->x0) / (float)(PG_N - 1);
    g->every = g->k * 3.0f >= 13.0f ? 3 : 6;
    if (g->every == 6) g->s = fminf(g->k * 6.0f * 0.75f, 18.0f);   // fewer, larger: the round glass
    else if (g->s > g->k * 3.0f - 1.0f) g->s = fmaxf(10.0f, g->k * 3.0f - 1.0f);   // three hours apart, not on each other
    g->base = (float)(g->fy1 - MICRO_H - 7);
    if (temp) {
        g->top = (float)g->fy0 + 5.0f + g->s + 2.0f + (float)lh + 3.0f;   // the warmest: its picture and number over it
        g->bot = g->top + (g->base - g->top) * 0.618f;                    // the golden section: the rain's bars under it
        g->vlo = T->t_lo;
        g->ky = (g->bot - g->top) / fmaxf(T->t_hi - T->t_lo, 0.1f);
    } else {
        g->top = (float)g->fy0 + 6.0f + g->s;                             // the highest: its picture over it
        g->bot = g->base - 5.0f;
        g->vlo = T->p_lo;
        g->ky = (g->bot - g->top) / fmaxf(T->p_hi - T->p_lo, 0.01f);
    }
}
static float gx_of(const geom_t *g, float tt) { return g->x0 + (tt + (float)PG_PAST) * g->k; }
static float gy_of(const geom_t *g, float v) { return g->bot - (v - g->vlo) * g->ky; }

/* a graph's value tt hours from hour[0], tenths: the pressure or the temperature. 0: not known. an hour before
 * hour[0] is known when its pressure is */
static int graph_value(const weather_t *w, int tt, int temp, int *v)
{
    if (tt < -WX_PAST || tt >= WX_HOURS) return 0;
    if (tt < 0) {
        const int k = WX_PAST + tt;
        if (!w->past_hpa10[k]) return 0;
        *v = temp ? w->past_t10[k] : w->past_hpa10[k];
        return 1;
    }
    *v = temp ? w->hour[tt].t10 : w->hour[tt].hpa10;
    return temp || w->hour[tt].hpa10;
}
static int kind_at(const weather_t *w, int tt) { return tt < 0 ? w->past_kind[WX_PAST + tt] : w->hour[tt].kind; }
static int rain_at(const weather_t *w, int tt)
{
    if (tt >= 0) return w->hour[tt].rain10;
    return w->past_hpa10[WX_PAST + tt] ? w->past_rain10[WX_PAST + tt] : 0;
}
// a graph's value in the unit shown: degrees, or the pressure (at home's height in mmHg, with that hour's temperature)
static int graph_disp(const weather_t *w, int tt, int temp, float *out)
{
    int v, t10;
    if (!graph_value(w, tt, temp, &v)) return 0;
    if (temp) {
        *out = t_of(v);
        return 1;
    }
    if (!graph_value(w, tt, 1, &t10)) t10 = w->now_t10;
    *out = p_of(v, t10);
    return 1;
}
// the pressure tt hours from hour[0] in the unit shown, 0: not known
static float pressure_disp(const weather_t *w, int tt) { float p; return graph_disp(w, tt, 0, &p) ? p : 0.0f; }
static float bar_h(int mm10, float rh) { return fmaxf(2.0f, gfx_sqrt(clampf((float)mm10 / 40.0f, 0, 1)) * rh); }

/* the line through the hours: a cubic between each two, its slope at each the harmonic mean of the two either
 * side (Fritsch and Butland's), so it never swings past the values it joins: no frost the forecast has not got */
static void slopes(const float *v, const uint8_t *ok, float *m, int n)
{
    for (int i = 0; i < n; i++) {
        m[i] = 0.0f;
        if (!ok[i]) continue;
        const int l = i > 0 && ok[i - 1], r = i < n - 1 && ok[i + 1];
        const float dl = l ? v[i] - v[i - 1] : 0.0f, dr = r ? v[i + 1] - v[i] : 0.0f;
        if (l && r) m[i] = dl * dr > 0.0f ? 2.0f * dl * dr / (dl + dr) : 0.0f;
        else m[i] = l ? dl : dr;
    }
}

// the line h hours after the first shown (0 .. n - 1): 0 where an hour either side is not known
static int curve_at(const float *v, const float *m, const uint8_t *ok, int n, float h, float *out)
{
    if (h < 0.0f || h > (float)(n - 1)) return 0;
    int i = (int)h;
    if (i > n - 2) i = n - 2;
    if (!ok[i] || !ok[i + 1]) return 0;
    const float s = h - (float)i, s2 = s * s, s3 = s2 * s;
    *out = (2.0f * s3 - 3.0f * s2 + 1.0f) * v[i] + (s3 - 2.0f * s2 + s) * m[i] + (3.0f * s2 - 2.0f * s3) * v[i + 1] + (s3 - s2) * m[i + 1];
    return 1;
}

/* what the pages share this frame: the sun's height by the hour (for the forecast, once), the scales, the
 * warning, now; then the page's own line point by point along the glass, its pictures and its rain. here and
 * not in a strip, as the dial's things are: the C3 works a float out slowly */
static void plan_pages(const ui_t *u, const weather_t *w, wtab_t *T, float t)
{
    const uint32_t utc = u->net.utc ? u->net.utc : w->at;
    T->now_h = clampf((float)((int64_t)utc - (int64_t)w->at) / 3600.0f, 0.0f, 1.0f);
    if (T->pel_at != w->at || T->pel_lat != u->cfg.lat || T->pel_lon != u->cfg.lon) {
        for (int k = 0; k <= 2 * WX_PAST; k++) {
            const int64_t at = (int64_t)w->at + (int64_t)(k - WX_PAST) * 3600;
            T->pel[k] = (int8_t)clampf(sun_elevation(u->cfg.lat, u->cfg.lon, (uint32_t)at), -90, 90);
        }
        T->pel_at = w->at;
        T->pel_lat = u->cfg.lat;
        T->pel_lon = u->cfg.lon;
    }
    /* the pressure: its scale in the unit shown over the hours shown and known. a grid of 2 hPa, 5 when it swings
     * far; of 1, 2 or 4 mmHg; of 0.05 or 0.1 inHg. a calm day 8 hPa high at the least, a line, not a scribble */
    float lo = 0.0f, hi = 0.0f, pv;
    int any = 0, v;
    for (int tt = -PG_PAST; tt < WX_HOURS; tt++) {
        if (!graph_disp(w, tt, 0, &pv)) continue;
        if (!any || pv < lo) lo = pv;
        if (!any || pv > hi) hi = pv;
        any = 1;
    }
    if (!any) lo = hi = p_of(10132, 150);
    float step, span;
    if (W.pu == PRES_INHG) {
        step = hi - lo > 0.47f ? 0.1f : 0.05f;
        span = 0.24f;
    } else if (W.pu == PRES_MMHG) {
        step = hi - lo > 12.0f ? 4.0f : hi - lo > 6.0f ? 2.0f : 1.0f;
        span = 6.0f;
    } else {
        step = hi - lo > 16.0f ? 5.0f : 2.0f;
        span = 8.0f;
    }
    float glo = floorf(lo / step + 0.001f) * step - step, ghi = ceilf(hi / step - 0.001f) * step + step;
    while (ghi - glo < span - step * 0.01f) {
        glo -= step;
        if (ghi - glo < span - step * 0.01f) ghi += step;
    }
    T->p_lo = glo;
    T->p_hi = ghi;
    T->p_step = step;
    wx_alert_t a;                                      // what the forecast says, whichever warnings SET has off (as the panel)
    wx_alerts(w, WXA_ALL, &a);
    T->p_lv = a.level[WXA_PRESSURE];
    T->p_at = (uint8_t)clampi(a.hour[WXA_PRESSURE], 0, WX_HOURS - 1);
    T->p_d = a.value[WXA_PRESSURE];
    /* the temperature: the next day's own for the title; the scale over every hour shown, a degree wider (two of
     * Fahrenheit), 4 degrees high at the least (8), the grid every 2 or 5 (5 or 10) */
    int tmin = w->hour[0].t10, tmax = tmin;
    for (int i = 1; i < WX_HOURS; i++) {
        if (w->hour[i].t10 < tmin) tmin = w->hour[i].t10;
        if (w->hour[i].t10 > tmax) tmax = w->hour[i].t10;
    }
    T->t_min = (int16_t)tmin;
    T->t_max = (int16_t)tmax;
    float tl = t_of(tmin), th = t_of(tmax);
    for (int tt = -PG_PAST; tt < 0; tt++) {
        if (!graph_disp(w, tt, 1, &pv)) continue;
        if (pv < tl) tl = pv;
        if (pv > th) th = pv;
    }
    const int fahr = W.tu == TEMP_F;
    const float margin = fahr ? 2.0f : 1.0f, least = fahr ? 8.0f : 4.0f;
    tl -= margin;
    th += margin;
    if (th - tl < least) {
        const float mid = (tl + th) * 0.5f;
        tl = mid - least * 0.5f;
        th = mid + least * 0.5f;
    }
    T->t_lo = tl;
    T->t_hi = th;
    T->t_step = fahr ? (th - tl > 24.0f ? 10.0f : 5.0f) : (th - tl > 12.0f ? 5.0f : 2.0f);
    // the page's line, its pictures and its rain
    T->n_s = T->n_pic = T->n_fall = 0;
    T->ny = -1.0f;
    if (u->wx_page != WXP_PRESSURE && u->wx_page != WXP_TEMP) return;
    const int temp = u->wx_page == WXP_TEMP;
    geom_t g;
    geom(u, T, temp, &g);
    float val[PG_N], m[PG_N];                          // in the unit shown: the line is drawn as it is read
    int16_t raw[PG_N];                                 // the temperature in tenths of C: its colour and its number
    uint8_t ok[PG_N];
    for (int k = 0; k < PG_N; k++) {
        ok[k] = (uint8_t)graph_disp(w, k - PG_PAST, temp, &val[k]);
        if (!ok[k]) val[k] = 0.0f;
        raw[k] = (int16_t)(temp && graph_value(w, k - PG_PAST, 1, &v) ? v : 0);
    }
    slopes(val, ok, m, PG_N);
    const float unit = GFX_PL(1.0f), off = GFX_PX(0.0f);
    const int ga = (int)ceilf(GFX_PX(g.x0)), gb = (int)GFX_PX(gx_of(&g, (float)(WX_HOURS - 1)));
    int sg = (int)(unit * 2.0f + 0.5f);
    if (sg < 2) sg = 2;
    while ((gb - ga) / sg + 1 > PG_S) sg++;
    T->sg = sg;
    T->sgx0 = ga;
    T->n_s = gb > ga ? (gb - ga) / sg + 1 : 0;
    const uint32_t ac = alert_colour(T->p_lv), prim = C_PRIM;
    for (int i = 0; i < T->n_s; i++) {
        const float x = ((float)(ga + i * sg) - off) / unit, h = (x - g.x0) / g.k;   // hours from the first shown
        float vv;
        T->sf[i] = 0;
        T->slean[i] = 16;
        if (!curve_at(val, m, ok, PG_N, h, &vv)) continue;
        const float tt = h - (float)PG_PAST;
        const int warned = !temp && T->p_lv && tt >= 0.0f && tt < (float)T->p_at;
        T->sy16[i] = (int32_t)lroundf(GFX_PX(gy_of(&g, vv)) * 16.0f);
        T->sf[i] = (uint8_t)(tt < T->now_h ? 1 : warned ? 3 : 2);
        T->sc[i] = temp ? temp_colour(c10_of(vv)) : warned ? ac : prim;
    }
    for (int i = 0; i < T->n_s; i++) {                 // how far each point leans: the line keeps its width across
        if (!T->sf[i]) continue;
        const int l = i > 0 && T->sf[i - 1] ? i - 1 : i, r = i < T->n_s - 1 && T->sf[i + 1] ? i + 1 : i;
        if (l == r) continue;
        const float sl = (float)(T->sy16[r] - T->sy16[l]) / (16.0f * (float)((r - l) * sg));
        T->slean[i] = (uint8_t)clampf(16.0f * gfx_sqrt(1.0f + sl * sl), 16.0f, 255.0f);
    }
    // the weather's pictures on the line every three hours by the home's clock; the temperature's numbers with them
    const int lh = FONT_LINE_H(&FONT_SMALL), every = g.every == 3 && g.k * 3.0f >= 20.0f ? 3 : 6;
    for (int tt = -PG_PAST; tt < WX_HOURS && T->n_pic < N_PIC; tt++) {
        const int k = tt + PG_PAST, hour = clock_hour(w, tt);
        if (!ok[k] || hour % g.every) continue;
        pic_t *p = &T->pic[T->n_pic++];
        p->x = gx_of(&g, (float)tt);
        p->y = gy_of(&g, val[k]);
        p->v = raw[k];
        p->kind = (uint8_t)kind_at(w, tt);
        p->night = (uint8_t)(T->pel[tt + WX_PAST] < -3);
        p->past = (uint8_t)((float)tt < T->now_h - 0.5f);
        p->named = (uint8_t)(temp && hour % every == 0);
        p->ly = p->y - (float)lh - 3.0f;
        const float above = temp ? (p->named ? p->ly - 1.0f : p->y - 3.0f) : p->y - 4.0f;
        p->iy = fmaxf(above - g.s * 0.5f, (float)g.fy0 + 3.0f + g.s * 0.5f);
    }
    // the rain to come, falling from under the line into its bars
    if (temp) {
        const float rh = g.base - g.bot - 10.0f, bw = clampf(g.k * 0.62f, 2.0f, 9.0f);
        for (int tt = 0; tt < WX_HOURS; tt++) {
            const int mm10 = w->hour[tt].rain10;
            if (!mm10 || (float)tt < T->now_h - 0.5f) continue;
            const float x = gx_of(&g, (float)tt), y0 = gy_of(&g, val[tt + PG_PAST]) + 6.0f, y1 = g.base - bar_h(mm10, rh) - 1.0f;
            if (y1 < y0 + 4.0f) continue;
            const int snow = w->hour[tt].kind == WX_SNOW, n = 1 + clampi(mm10 / 5, 0, 3);
            for (int d = 0; d < n && T->n_fall < N_RAIN; d++) {
                const float ph = fmodf(t * (snow ? 0.35f : 0.8f) + rnd01((uint32_t)(tt * 8 + d)), 1.0f);
                fall_t *f = &T->fall[T->n_fall++];
                f->x = x + (rnd01((uint32_t)(tt * 8 + d + 101)) - 0.5f) * bw * 1.3f + (snow ? sinf(t * 1.5f + (float)d) * 1.3f : 0.0f);
                f->y = y0 + (y1 - y0) * ph;
                f->a = (uint8_t)(235.0f * clampf(ph * 6.0f, 0, 1) * clampf((1.0f - ph) * 6.0f, 0, 1));
                f->snow = (uint8_t)snow;
            }
        }
    }
    float nv;
    T->nx = gx_of(&g, T->now_h);
    T->ny = curve_at(val, m, ok, PG_N, T->now_h + (float)PG_PAST, &nv) ? gy_of(&g, nv) : -1.0f;
}

/* the graph's line and the fill under it, a column of the glass at a time, in whole numbers: the line's height
 * there between the frame's points, as thick across as along wherever it leans, its glow, and under it the fill
 * fading to the foot (base). each pixel once, in whichever strip: no joins drawn twice. the past thinner and
 * fainter than the hours to come; xr is where the drawing in has got to */
static void graph_curve(canvas_t *cv, const wtab_t *T, int temp, float top, float base, float xr)
{
    if (T->n_s < 2 || !strip_has(cv, top - 8.0f, base)) return;
    const float unit = GFX_PL(1.0f);
    const int gbase = (int)GFX_PX(base), gtop = (int)GFX_PX(top), span = gbase - gtop > 0 ? gbase - gtop : 1;
    const int ylast = cv->y0 + cv->h - 1 < gbase ? cv->y0 + cv->h - 1 : gbase;
    const int lw_on = (int)(1.9f * unit * 8.0f), lw_past = (int)(1.3f * unit * 8.0f), gw = (int)(3.6f * unit * 16.0f);
    int gx1 = T->sgx0 + (T->n_s - 1) * T->sg;
    const int gxr = (int)GFX_PX(xr);
    if (gx1 > gxr) gx1 = gxr;
    if (gx1 > GFX_PW - 1) gx1 = GFX_PW - 1;
    for (int gx = T->sgx0 < 0 ? 0 : T->sgx0; gx <= gx1; gx++) {
        const int off = gx - T->sgx0, i = off / T->sg, f = off % T->sg, j = i + 1 < T->n_s ? i + 1 : i;
        if (!T->sf[i] || !T->sf[j]) continue;
        const int gy16 = T->sy16[i] + (T->sy16[j] - T->sy16[i]) * f / T->sg;
        const int near = f * 2 < T->sg ? i : j, past = T->sf[near] == 1, lean = T->slean[near];
        const uint32_t c = T->sc[near];
        const int lw16 = (past ? lw_past : lw_on) * lean / 16, gw16 = past ? 0 : gw * lean / 16;
        const int la = past ? 165 : 256, ga = past ? 0 : 56, fa = past ? 48 : temp ? 92 : 80;
        const int reach = gw16 > lw16 + 16 ? gw16 : lw16 + 16;
        int ya = (gy16 - reach) / 16 - 1;
        if (ya < cv->y0) ya = cv->y0;
        for (int y = ya; y <= ylast; y++) {
            uint32_t *px = cv->px + (y - cv->y0) * GFX_PW + gx;
            uint32_t p = *px;
            const int d16 = y * 16 - gy16, ad = d16 < 0 ? -d16 : d16;
            if (d16 > 0) {                             // under the line: the fill, fading to the foot
                const int a = fa * (gbase - y) / span;
                if (a > 0) p = gfx_mix(p, c, (uint32_t)a);
            }
            if (ad < gw16) {
                const int e = gw16 - ad;
                p = gfx_mix(p, c, (uint32_t)(ga * e / gw16 * e / gw16));
            }
            const int cov = lw16 + 8 - ad;             // sixteenths of the pixel the line covers
            if (cov > 0) p = gfx_mix(p, c, (uint32_t)(la * (cov > 16 ? 16 : cov) / 16));
            *px = p;
        }
    }
}

// the page's title: what on the left and the number on the right; on the round glass the two in the middle
static void page_title(canvas_t *cv, int x0, int x1, const char *what, const char *value, uint32_t vc)
{
    const int lh = FONT_LINE_H(&FONT_SMALL);
#if RADAR_ROUND
    (void)x0;
    (void)x1;
    if (gfx_touches(cv, PG_TY, PG_TY + 2 * lh + 2)) {
        font_text(cv, &FONT_SMALL, SCX - font_width(&FONT_SMALL, what) / 2, PG_TY, what, C_PRIM, 255);
        font_text(cv, &FONT_SMALL, SCX - font_width(&FONT_SMALL, value) / 2, PG_TY + lh + 1, value, vc, 255);
    }
#else
    if (gfx_touches(cv, PG_TY, PG_TY + lh)) {
        font_text(cv, &FONT_SMALL, x0 + 2, PG_TY, what, C_PRIM, 255);
        font_text(cv, &FONT_SMALL, x1 - 2 - font_width(&FONT_SMALL, value), PG_TY, value, vc, 255);
    }
#endif
}

// a line under the frame: on the round glass the only one, in the middle
static void page_line(canvas_t *cv, int x0, int x1, int y, const char *s, uint32_t c)
{
    const int lh = FONT_LINE_H(&FONT_SMALL);
    if (!gfx_touches(cv, y, y + lh)) return;
    if (RADAR_ROUND) font_text(cv, &FONT_SMALL, SCX - font_width(&FONT_SMALL, s) / 2, y, s, c, 255);
    else font_text_clip(cv, &FONT_SMALL, x0 + 4, y, s, c, 255, x1 - x0 - 8);
}

/* the time under a graph: its line, the hours every six by the home's clock, now, and a half sun on the
 * line where the sun rises and where it sets */
static void graph_axis(canvas_t *cv, const weather_t *w, const wtab_t *T, const geom_t *g)
{
    const float y = g->base, x0 = gx_of(g, (float)-PG_PAST), x1 = gx_of(g, (float)(WX_HOURS - 1)), nx = T->nx;
    if (!strip_has(cv, y - 8.0f, y + 4.0f + MICRO_H)) return;
    gfx_hline(cv, (int)x0, (int)x1, (int)y, C_QUIET, 170);
    char v[8];
    const int nw = micro_width("NOW", 1);
    for (int tt = -PG_PAST; tt < WX_HOURS; tt++) {
        const int h = clock_hour(w, tt);
        if (h % 6) continue;
        const float x = gx_of(g, (float)tt);
        gfx_vline(cv, (int)lroundf(x), (int)y, (int)y + 2, C_QUIET, 200);
        snprintf(v, sizeof v, "%02d", h);
        const int lw = micro_width(v, 1), lx = (int)lroundf(x) - lw / 2;
        if (fabsf(x - nx) < (float)(lw + nw) * 0.5f + 3.0f) continue;   // NOW is said there
        if (lx < g->fx0 + 2 || lx + lw > g->fx1 - 2) continue;          // none half off the frame
        micro_text(cv, lx, (int)y + 4, v, C_DIM, 230, 1);
    }
    micro_text(cv, (int)lroundf(nx) - nw / 2, (int)y + 4, "NOW", C_FOCUS, 255, 1);
    for (int tt = -PG_PAST; tt < WX_HOURS - 1; tt++) {
        const float e0 = (float)T->pel[tt + WX_PAST], e1 = (float)T->pel[tt + 1 + WX_PAST];
        if ((e0 > 0.0f) == (e1 > 0.0f)) continue;
        const float x = gx_of(g, (float)tt + e0 / (e0 - e1));
        float half[18];                                // the sun as half a disc standing on the line, its rays over it
        for (int k = 0; k < 9; k++) {
            const float q = (float)(k - 4) * 0.3927f;
            half[2 * k] = x + sinf(q) * 3.6f;
            half[2 * k + 1] = y - cosf(q) * 3.6f;
        }
        gfx_poly(cv, half, 9, DAWN_C, 240);
        for (int r = -1; r <= 1; r++) {
            const float q = (float)r * 0.8f;
            gfx_line(cv, x + sinf(q) * 5.2f, y - cosf(q) * 5.2f, x + sinf(q) * 6.9f, y - cosf(q) * 6.9f, 1.1f, DAWN_C, 225);
        }
    }
}

// now: a line up from NOW on the time's line to the graph's, as the dial's hand; on the graph's line a dot that pulses
static void graph_now(canvas_t *cv, const wtab_t *T, const geom_t *g, float xr)
{
    const float top = T->ny >= 0.0f ? T->ny + 5.0f : (float)g->fy0 + 3.0f;
    if (xr >= T->nx && strip_has(cv, top, g->base)) gfx_vline(cv, (int)lroundf(T->nx), (int)top, (int)g->base - 1, C_FOCUS, 150);
}
static void now_dot(canvas_t *cv, const ui_t *u, const wtab_t *T, float xr)
{
    if (T->ny < 0.0f || xr < T->nx || !strip_has(cv, T->ny - 6.0f, T->ny + 6.0f)) return;
    const float b = 0.5f + 0.5f * sinf((float)u->now / 1000.0f * 4.0f);
    gfx_disc(cv, T->nx, T->ny, 4.2f + 1.2f * b, C_BG0, 200);
    gfx_disc(cv, T->nx, T->ny, 2.4f + 0.8f * b, C_FOCUS, 255);
}

// the weather's pictures on the line, the past's fainter and still; on the temperature its dots and numbers
static void graph_pics(canvas_t *cv, const wtab_t *T, const geom_t *g, float xr, int temp)
{
    char v[8];
    const int lh = FONT_LINE_H(&FONT_SMALL);
    for (int i = 0; i < T->n_pic; i++) {
        const pic_t *p = &T->pic[i];
        if (p->x > xr) break;
        const int a = p->past ? 150 : 255;
        if (temp && strip_has(cv, p->y - 4.0f, p->y + 4.0f)) gfx_disc(cv, p->x, p->y, 2.0f, temp_colour(p->v), a);
        if (p->named && gfx_touches(cv, (int)p->ly, (int)p->ly + lh)) {
            fmt_t(v, sizeof v, p->v);
            font_text(cv, &FONT_SMALL, (int)p->x - font_width(&FONT_SMALL, v) / 2, (int)p->ly, v, temp_colour(p->v), a);
        }
        if (strip_has(cv, p->iy - g->s * 0.6f, p->iy + g->s * 0.6f)) wx_pic(cv, p->kind, p->night, p->x, p->iy, g->s, p->past ? 0.0f : W.anim, a);
    }
}

static float reveal(const ui_t *u) { return clampf((float)since_open(u) / (float)PG_REVEAL_MS, 0, 1); }

/* the pressure: a line, the hours a warning is about in its colour and filled in it, where it is worst and by how
 * much; the grid named inside the frame on the left. no band over the hours: what they are is on the line */
static void page_pressure(ui_t *u, canvas_t *cv, const weather_t *w)
{
    const wtab_t *T = W.t;
    geom_t g;
    geom(u, T, 0, &g);
    const uint32_t ac = alert_colour(T->p_lv);
    const int d12 = (int)w->hour[WX_HOURS / 2].hpa10 - (int)w->hour[0].hpa10;
    char v[32], n[20];
    const char *way = T->p_lv ? (T->p_d < 0 ? "FALLING" : "RISING") : d12 <= -15 ? "FALLING" : d12 >= 15 ? "RISING" : "STEADY";
    fmt_p(n, sizeof n, p_of(w->now_hpa10, w->now_t10), 1);
    snprintf(v, sizeof v, "%s %s", n, way);
    page_title(cv, g.fx0, g.fx1, "PRESSURE", v, T->p_lv ? ac : C_FOCUS);
    chamfer_box(cv, g.fx0, g.fy0, g.fx1 - g.fx0, g.fy1 - g.fy0, C_BG0, 110, C_QUIET, 190);
    const float f = reveal(u), xr = g.x0 + f * (gx_of(&g, (float)(WX_HOURS - 1)) - g.x0 + 2.0f);
    const int lines = (int)lroundf((T->p_hi - T->p_lo) / T->p_step);
    for (int j = 0; j <= lines; j++) {                 // the grid, every other line named in the scale's column
        const float gv = T->p_lo + (float)j * T->p_step, y = gy_of(&g, gv);
        if (!strip_has(cv, y - (float)MICRO_H, y + (float)MICRO_H)) continue;
        gfx_hline(cv, g.gut + 2, g.fx1 - 2, (int)lroundf(y), C_QUIET, j % 2 ? 40 : 85);
        if (j % 2 == 0) {
            fmt_p(v, sizeof v, gv, 0);
            micro_text(cv, g.gut - micro_width(v, 1), (int)lroundf(y) - MICRO_H / 2, v, C_DIM, 220, 1);
        }
    }
    graph_curve(cv, T, 0, g.top, g.base, xr);
    graph_now(cv, T, &g, xr);
    graph_pics(cv, T, &g, xr, 0);
    now_dot(cv, u, T, xr);
    if (T->p_lv && f >= 1.0f) {                        // the warning: where it is worst, and what it says
        const float qx = gx_of(&g, (float)T->p_at), qy = gy_of(&g, pressure_disp(w, T->p_at));
        if (strip_has(cv, qy - 6.0f, qy + 6.0f)) gfx_ring(cv, qx, qy, 4.0f, 1.5f, ac, 255);
        if (!RADAR_ROUND) {                            // the round glass says it under the frame, the graph is small
            fmt_dp(n, sizeof n, dp_of(T->p_d, w));
            snprintf(v, sizeof v, "%s BY %02d:00", n, clock_hour(w, T->p_at));
            const int bw = font_width(&FONT_SMALL, v) + 10, bh = FONT_LINE_H(&FONT_SMALL) + 5;
            // over the end of the stretch where the line is low and the sky over it free: a fall's last hour, a rise's first
            const float ax = gx_of(&g, T->p_d < 0 ? (float)T->p_at : T->now_h);
            const int bx = clampi((int)ax - bw / 2, g.fx0 + 3, g.fx1 - bw - 3), by = g.fy0 + 3;
            if (gfx_touches(cv, by, by + bh)) {
                chamfer_box(cv, bx, by, bw, bh, C_BG0, 215, ac, 230);
                font_text(cv, &FONT_SMALL, bx + 5, by + 3, v, ac, 255);
            }
        }
    }
    graph_axis(cv, w, T, &g);
#if RADAR_ROUND
    fmt_dp(n, sizeof n, dp_of(T->p_lv ? T->p_d : d12, w));
    if (T->p_lv) snprintf(v, sizeof v, "%s BY %02d:00", n, clock_hour(w, T->p_at));
    else snprintf(v, sizeof v, "%s IN 12 HOURS", n);
    page_line(cv, g.fx0, g.fx1, PG_FY, v, T->p_lv ? ac : C_DIM);
#else
    // half a day ago (the warning's own reach), now, and where the warning is worst or half a day on
    if (gfx_touches(cv, PG_FY, PG_FY + PG_FOOT)) {
        const int cw = (g.fx1 - g.fx0 - 8) / 3, at = T->p_lv ? T->p_at : WX_HOURS / 2;
        const float before = pressure_disp(w, -12);
        char head[3][12], val[3][16];
        snprintf(head[0], sizeof head[0], "12 H AGO");
        snprintf(head[1], sizeof head[1], "NOW");
        snprintf(head[2], sizeof head[2], "%s %02d:00", T->p_lv ? "BY" : "AT", clock_hour(w, at));
        if (before > 0.0f) fmt_p(val[0], sizeof val[0], before, 1);
        else snprintf(val[0], sizeof val[0], "--");
        fmt_p(val[1], sizeof val[1], p_of(w->now_hpa10, w->now_t10), 1);
        fmt_p(val[2], sizeof val[2], p_of(w->hour[at].hpa10, w->hour[at].t10), 1);
        for (int i = 0; i < 3; i++) {
            const int x = g.fx0 + i * (cw + 4);
            const uint32_t c = i == 2 && T->p_lv ? ac : i == 1 ? C_FOCUS : C_DIM;
            chamfer_box(cv, x, PG_FY, cw, PG_FOOT, C_PANEL, 150, C_QUIET, 170);
            micro_text(cv, x + 5, PG_FY + 4, head[i], i == 1 ? C_FOCUS : C_DIM, 235, 1);
            font_text_clip(cv, &FONT_SMALL, x + 5, PG_FY + 6 + MICRO_H, val[i], c, 255, cw - 8);
        }
    }
#endif
}

/* the temperature by the hour, coloured by how warm, the fill under it fading; the weather's pictures on the
 * line every three hours over the number of that hour; under it, in the lower part of the golden section,
 * the rain in bars as much as falls in the hour, the rain to come falling into them */
static void page_temp(ui_t *u, canvas_t *cv, const weather_t *w)
{
    const wtab_t *T = W.t;
    geom_t g;
    geom(u, T, 1, &g);
    char v[40], hi[8], lo[8];
    fmt_t(hi, sizeof hi, T->t_max);
    fmt_t(lo, sizeof lo, T->t_min);
    page_title(cv, g.fx0, g.fx1, "TEMPERATURE", "", C_DIM);
    {                                                  // the next day's high and low, each in the colour of how warm
        const int lh = FONT_LINE_H(&FONT_SMALL);
        const int w1 = font_width(&FONT_SMALL, "HIGH "), w2 = font_width(&FONT_SMALL, hi), w3 = font_width(&FONT_SMALL, "  LOW ");
        const int w4 = font_width(&FONT_SMALL, lo), ty = RADAR_ROUND ? PG_TY + lh + 1 : PG_TY;
        int x = RADAR_ROUND ? SCX - (w1 + w2 + w3 + w4) / 2 : g.fx1 - 2 - (w1 + w2 + w3 + w4);
        if (gfx_touches(cv, ty, ty + lh)) {
            font_text(cv, &FONT_SMALL, x, ty, "HIGH ", C_DIM, 255);
            font_text(cv, &FONT_SMALL, x += w1, ty, hi, temp_colour(T->t_max), 255);
            font_text(cv, &FONT_SMALL, x += w2, ty, "  LOW ", C_DIM, 255);
            font_text(cv, &FONT_SMALL, x + w3, ty, lo, temp_colour(T->t_min), 255);
        }
    }
    chamfer_box(cv, g.fx0, g.fy0, g.fx1 - g.fx0, g.fy1 - g.fy0, C_BG0, 110, C_QUIET, 190);
    const float f = reveal(u), xr = g.x0 + f * (gx_of(&g, (float)(WX_HOURS - 1)) - g.x0 + 2.0f);
    {                                                  // the grid over the line's part of the golden section, named in the scale's column
        const float first = ceilf(T->t_lo / T->t_step - 0.001f) * T->t_step;
        for (int j = 0; j < 40; j++) {
            const float gv = first + (float)j * T->t_step, y = gy_of(&g, gv);
            if (gv > T->t_hi + 0.001f) break;
            if (y > g.bot + 0.5f || y < g.top - 0.5f || !strip_has(cv, y - (float)MICRO_H, y + (float)MICRO_H)) continue;
            gfx_hline(cv, g.gut + 2, g.fx1 - 2, (int)lroundf(y), C_QUIET, 55);
            snprintf(v, sizeof v, "%d", (int)lroundf(gv));
            micro_text(cv, g.gut - micro_width(v, 1), (int)lroundf(y) - MICRO_H / 2, v, C_DIM, 220, 1);
        }
    }
    graph_curve(cv, T, 1, g.top, g.base, xr);
    // the rain: a bar an hour, as tall as the square root of how much (a shower is seen, a downpour fills it)
    const float rh = g.base - g.bot - 10.0f, bw = clampf(g.k * 0.62f, 2.0f, 9.0f);
    int most = 0, most_at = -1, r0 = -1, r1 = -1, sum = 0;
    for (int tt = -PG_PAST; tt < WX_HOURS; tt++) {
        const int mm10 = rain_at(w, tt);
        if (!mm10) continue;
        const int past = (float)tt < T->now_h - 0.5f;
        if (!past) {
            if (r0 < 0) r0 = tt;
            r1 = tt;
            sum += mm10;
            if (mm10 > most) { most = mm10; most_at = tt; }
        }
        const float bh = bar_h(mm10, rh), bx = gx_of(&g, (float)tt) - bw * 0.5f, by = g.base - bh;
        if (bx > xr || !strip_has(cv, by, g.base)) continue;
        gfx_fill(cv, (int)lroundf(bx), (int)lroundf(by), (int)lroundf(bw), (int)lroundf(bh),
                 kind_at(w, tt) == WX_SNOW ? RGB(235, 242, 255) : RAIN_C, past ? 105 : 225);
    }
    for (int i = 0; i < T->n_fall; i++) {              // the rain to come, falling
        const fall_t *d = &T->fall[i];
        if (d->x > xr || !strip_has(cv, d->y - 1.0f, d->y + 5.0f)) continue;
        if (d->snow) gfx_disc(cv, d->x, d->y, 1.2f, RGB(255, 255, 255), d->a);
        else gfx_line(cv, d->x, d->y, d->x - 0.5f, d->y + 3.5f, 1.2f, RGB(150, 205, 255), d->a);
    }
    graph_now(cv, T, &g, xr);
    graph_pics(cv, T, &g, xr, 1);
    now_dot(cv, u, T, xr);
    if (most_at >= 0 && gx_of(&g, (float)most_at) <= xr) {
        fmt_r(v, sizeof v, most);
        const float by = g.base - bar_h(most, rh) - (float)MICRO_H - 4.0f;
        if (strip_has(cv, by, by + (float)MICRO_H)) micro_text(cv, (int)gx_of(&g, (float)most_at) - micro_width(v, 1) / 2, (int)by, v, RAIN_C, 250, 1);
    } else if (most_at < 0 && strip_has(cv, g.base - (float)MICRO_H - 6.0f, g.base)) {
        const int x = (int)((T->nx + gx_of(&g, (float)(WX_HOURS - 1))) * 0.5f);
        micro_text(cv, x - micro_width("NO RAIN", 1) / 2, (int)g.base - MICRO_H - 6, "NO RAIN", C_DIM, 230, 1);
    }
    graph_axis(cv, w, T, &g);
    // under the frame: when it rains and how much, the strongest gust
    int gi = 0;
    for (int i = 1; i < WX_HOURS; i++) if (w->hour[i].gust_kmh > w->hour[gi].gust_kmh) gi = i;
    char gust[40], mm[16];
    snprintf(gust, sizeof gust, "GUSTS TO %d %s AT %02d:00", w_of(w->hour[gi].gust_kmh), WIND_NAME[W.wu], clock_hour(w, gi));
    const char *wet = r0 >= 0 && w->hour[r0].kind == WX_SNOW ? "SNOW" : "RAIN";
    fmt_r(mm, sizeof mm, sum);
#if RADAR_ROUND
    (void)r1;                                          // the round glass has room for when it starts only
    if (r0 >= 0) snprintf(v, sizeof v, "%s %02d:00  %s", wet, clock_hour(w, r0), mm);
    else snprintf(v, sizeof v, "GUSTS %d %s", w_of(w->hour[gi].gust_kmh), WIND_NAME[W.wu]);
    page_line(cv, g.fx0, g.fx1, PG_FY, v, r0 >= 0 ? RAIN_C : w->hour[gi].gust_kmh >= 50 ? C_LOW : C_DIM);
#else
    if (r0 >= 0) snprintf(v, sizeof v, "%s %02d:00-%02d:00  %s", wet, clock_hour(w, r0), clock_hour(w, r1 + 1), mm);
    else snprintf(v, sizeof v, "NO RAIN IN 24 HOURS");
    page_line(cv, g.fx0, g.fx1, PG_FY + 2, v, r0 >= 0 ? RAIN_C : C_DIM);
    page_line(cv, g.fx0, g.fx1, PG_FY + 4 + FONT_LINE_H(&FONT_SMALL), gust, w->hour[gi].gust_kmh >= 50 ? C_LOW : C_DIM);
#endif
}

// which page is up: a dot each under the dial or the graph, the one shown lit
static void page_dots(const ui_t *u, canvas_t *cv)
{
    const float y = (float)(GFX_H - (RADAR_ROUND ? 8 : 6)), cx = (float)view_cx(u);
    if (!strip_has(cv, y - 3, y + 3)) return;
    for (int i = 0; i < WX_PAGES; i++) {
        const float x = cx + ((float)i - (float)(WX_PAGES - 1) * 0.5f) * 10.0f;
        if (i == u->wx_page) gfx_disc(cv, x, y, 2.6f, C_PRIM, 255);
        else gfx_ring(cv, x, y, 2.3f, 1.0f, C_QUIET, 230);
    }
}

/* what writes the dial's tables: taken, made again each minute and for each forecast, the frame's things
 * placed on it, or the page's. the strips after it only read them, so the second core may draw half of
 * them at once */
static void prepare(const ui_t *u)
{
    const weather_t *w = u->wx;
    W.cx = view_cx(u);
    W.gcx = scope_glass_cx(u);
    units_of(u);                                       // the frame's units, before anything is worked out in them
    W.room = w && w->at && make(u, w);
    if (!W.room) return;
    const float t = (float)since_open(u) / 1000.0f;
    W.anim = t;
    if (u->wx_page == WXP_DIAL) {
        plan_air(u, w, W.t, t);
        plan_marks(u, w, W.t, t);
    } else {
        plan_pages(u, w, W.t, t);
    }
    W.t->frame = u->now;
    W.t->planned = 1;
}

/* each frame, before its strips (ui_frame). a page comes up when the look is chosen, back from LIST or SET,
 * when the first forecast is in, or turned to. told by the frames, not by the time between them: the C3
 * has one core for the screen and the network, and a frame there can take half a second, which a gap rule
 * took for the look coming up again.
 * then the frame's work on the tables, here and not in a strip: two cores drawing strips of one frame
 * both asked for it, and both gave the tables back the first frame of another look. the boards with the
 * moving layer draw their background on a task of their own from a copy of the ui while the ui goes
 * on: there the background's first strip of each frame does it, that task the only one with the dial */
void weather_frame(ui_t *u)
{
    const int up = u->view == VIEW_MAIN && u->cfg.look == LOOK_WEATHER && u->wx && u->wx->at;
    if (u->wx_page >= WX_PAGES) u->wx_page = WXP_DIAL;
    const int shown = up ? 1 + u->wx_page : 0;        // a graph that comes is drawn in again
    if (shown && shown != W.shown) W.opened = u->now ? u->now : 1;
    W.shown = shown;
#if !RADAR_OVERLAY
    if (u->cfg.look != LOOK_WEATHER) {
        weather_close();                               // another look: the tables back to the heap
    } else if (u->view == VIEW_MAIN) {
        const uint32_t t0 = ui_clock_us ? ui_clock_us() : 0;
        prepare(u);
        if (ui_clock_us) u->prof[3] += ui_clock_us() - t0;   // the 10 s log's "planes" (see weather_draw)
    }
#endif
}

void weather_draw(ui_t *u, canvas_t *cv)
{
    const weather_t *w = u->wx;
    /* the firmware's 10 s log of where a frame's time goes, in the radar's words: here "disc" is the dial's
     * pixels, "compass" the marks on it, "planes" working out where they are (weather_frame, or here on the
     * moving layer's background), "panel" the middle, the corners and the panel. the second core's strips
     * are not in it */
    ui_lap(u, cv, 4);
#if RADAR_OVERLAY
    if (!W.room || !W.t->planned || W.t->frame != u->now) prepare(u);   // the background's first strip of a frame
#endif
    ui_lap(u, cv, 3);
    const int ready = w && w->at, room = ready && W.room, panel_in = PW > 0 && panel_shown(u);
    if (!room) {                                       // no forecast yet, or no memory for the dial: the glass, and why
        const int cx = view_cx(u);
        gfx_ring(cv, (float)cx, (float)SCY, (float)SR, 1.3f, C_PRIM, 230);
        gfx_ring(cv, (float)cx, (float)SCY, (float)W_RIN, 1.0f, C_QUIET, 200);
        const char *l1 = ready ? "NO MEMORY" : u->net.wifi ? "ASKING FOR" : "NO WIFI:";
        const char *l2 = ready ? "FOR THE DIAL" : u->net.wifi ? "THE FORECAST" : "NO FORECAST";
        font_text(cv, &FONT_SMALL, cx - font_width(&FONT_SMALL, l1) / 2, SCY - FONT_LINE_H(&FONT_SMALL), l1, C_DIM, 255);
        font_text(cv, &FONT_SMALL, cx - font_width(&FONT_SMALL, l2) / 2, SCY + 1, l2, C_FOCUS, 255);
        corners(u, cv, NULL);
        if (panel_in) panel_frame(u, cv);
    } else {
        if (u->wx_page == WXP_DIAL) {
            dial_pass(u, cv);
            ui_lap(u, cv, 1);
            draw_marks(cv, W.t);
            ui_lap(u, cv, 2);
            dial_core(u, cv, w);
            hud(u, cv, w);
        } else {
            if (RADAR_ROUND) gfx_ring(cv, (float)SCX, (float)SCY, (float)SR, 1.3f, C_PRIM, 230);   // the glass's rim, as the dial has it
            if (u->wx_page == WXP_PRESSURE) page_pressure(u, cv, w);
            else page_temp(u, cv, w);
            ui_lap(u, cv, 1);
            ui_lap(u, cv, 2);
        }
        page_dots(u, cv);
        corners(u, cv, w);
        if (panel_in) panel(u, cv, w);
    }
#if RADAR_HAS_TOUCH && !RADAR_ROUND
    if (gfx_touches(cv, LOOK_Y, LOOK_Y + LOOK_H)) panel_dots(u, cv, RDOTS_X, LOOK_Y, RDOTS_W);   // over the panel's corner, or where it was
#endif
}
