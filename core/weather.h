/* weather - what the WEATHER look shows: now, the next 24 hours an hour at a time, the next days.
 * plain data: the firmware fills it from the forecast, the PC preview and the site's radar with a
 * sample. temperatures in tenths of a degree C, pressure in tenths of hPa at sea level: the screen
 * turns them into the units the person picked */
#pragma once
#include <stdint.h>

#define WX_HOURS 24
#define WX_DAYS  4
#define WX_PAST  24           /* hours before now, for the graphs behind the dial */

enum { WX_CLEAR, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_STORM, WX_KINDS };

#define WX_NONE 255           /* a value the forecast did not give */

typedef struct {
    int16_t  t10;             /* the temperature */
    uint8_t  kind;            /* WX_* */
    uint8_t  cloud;           /* cloud cover, % */
    uint16_t rain10;          /* rain or snow in the hour, tenths of a mm */
    uint16_t hpa10;           /* pressure */
    uint8_t  wind_from2;      /* where the wind blows from, in 2 degree steps */
    uint8_t  wind_kmh, gust_kmh;
    uint8_t  uv10;            /* the UV index, tenths */
    uint8_t  aqi;             /* the European air quality index (0 good .. over 100), WX_NONE: none */
} wx_hour_t;

typedef struct {
    int16_t hi10, lo10;
    uint8_t kind;
    uint8_t wday;             /* 0 Monday .. 6 Sunday */
} wx_day_t;

typedef struct {
    uint32_t  at;             /* UTC of hour[0], on the hour: the hour now. 0: no forecast yet */
    uint32_t  updated;        /* UTC it came */
    int16_t   utc_min;        /* the home's clock: minutes ahead of UTC */
    int16_t   elev;           /* the forecast point's height over the sea, m: the pressure at home's height from it */
    int16_t   now_t10, feels10;
    uint8_t   now_kind, humidity;
    uint16_t  now_hpa10;
    int16_t   wind_from;      /* degrees, where it blows from */
    uint8_t   wind_kmh, gust_kmh;
    uint8_t   uv10, aqi;      /* now, as in an hour */
    uint16_t  pm25_10;        /* fine dust now, tenths of a microgram a cubic metre */
    uint16_t  gamma_nsv;      /* the background radiation at the nearest station, nSv/h. 0: none near */
    uint8_t   gamma_km;       /* how far that station is */
    wx_hour_t hour[WX_HOURS];
    wx_day_t  day[WX_DAYS];
    /* the hours before hour[0], the oldest first: [WX_PAST - 1] is an hour before it. an hour is known when its
     * pressure is (0: the third question did not come), the rest of it then from the same answer */
    uint16_t  past_hpa10[WX_PAST];
    int16_t   past_t10[WX_PAST];
    uint16_t  past_rain10[WX_PAST];
    uint8_t   past_kind[WX_PAST];
} weather_t;

/* the warnings (wx_alert.c): what in the next hours is worth saying on every look, amber or red. each
 * kind is the person's to switch off, all on at first:
 *   STORM      gusts from 75 km/h, red from 100; thunder amber
 *   PRESSURE   6 hPa up or down within 12 hours, red from 10: the change the body feels
 *   AIR        the European index poor (60), red very poor (80)
 *   RADIATION  the nearest station over 0.3 microsievert an hour, red over 1 */
enum { WXA_STORM, WXA_PRESSURE, WXA_AIR, WXA_RADIATION, WXA_KINDS };
#define WXA_ALL   ((1u << WXA_KINDS) - 1u)
#define WXA_AHEAD 12          /* hours looked ahead */
enum { WXA_NONE, WXA_AMBER, WXA_RED };

typedef struct {
    uint8_t level[WXA_KINDS]; /* WXA_NONE .. WXA_RED */
    uint8_t hour[WXA_KINDS];  /* hours from now it is at its worst, 0 now */
    int16_t value[WXA_KINDS]; /* the gust in km/h, the change in tenths of hPa (falling below 0), the index, nSv/h */
    uint8_t thunder;          /* the storm is thunder: at the hour in hour[WXA_STORM] if no gust is the worst */
} wx_alert_t;

/* the warnings in w of the kinds on in mask (1 << WXA_*), into a. back: the highest level of them */
int wx_alerts(const weather_t *w, unsigned mask, wx_alert_t *a);
/* the worst of them, the first kind on a tie (the order above), -1 none */
int wx_alert_worst(const wx_alert_t *a);
