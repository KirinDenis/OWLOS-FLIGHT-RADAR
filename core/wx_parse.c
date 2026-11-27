// Open-Meteo's answers into weather_t, see wx_parse.h
#include "wx_parse.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wx_url_forecast(char *buf, size_t cap, float lat, float lon)
{
    snprintf(buf, cap, "https://api.open-meteo.com/v1/forecast?latitude=%.1f&longitude=%.1f"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,pressure_msl,"
             "wind_speed_10m,wind_direction_10m,wind_gusts_10m,uv_index"
             "&hourly=temperature_2m,weather_code,cloud_cover,precipitation,pressure_msl,wind_speed_10m,"
             "wind_direction_10m,wind_gusts_10m,uv_index"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min"
             "&timezone=auto&timeformat=unixtime&forecast_hours=%d&forecast_days=%d",
             (double)lat, (double)lon, WX_HOURS, WX_DAYS + 1);
}

void wx_url_air(char *buf, size_t cap, float lat, float lon)
{
    snprintf(buf, cap, "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.1f&longitude=%.1f"
             "&current=european_aqi,pm2_5&hourly=european_aqi&timezone=auto&timeformat=unixtime&forecast_hours=%d",
             (double)lat, (double)lon, WX_HOURS);
}

void wx_url_past(char *buf, size_t cap, float lat, float lon)
{
    snprintf(buf, cap, "https://api.open-meteo.com/v1/forecast?latitude=%.1f&longitude=%.1f"
             "&hourly=pressure_msl,temperature_2m,weather_code,precipitation"
             "&timezone=auto&timeformat=unixtime&past_hours=%d&forecast_hours=1",
             (double)lat, (double)lon, WX_PAST);
}

// the body of the object "name":{...}: where it starts, NULL if it is not there
static const char *object(const char *json, const char *name)
{
    char key[40];
    snprintf(key, sizeof key, "\"%s\":{", name);
    const char *p = strstr(json, key);
    return p ? p + strlen(key) : NULL;
}

// where "name": stands inside the flat object at obj (before its closing brace), past the colon
static const char *field(const char *obj, const char *name, const char *tail)
{
    if (!obj) return NULL;
    char key[40];
    snprintf(key, sizeof key, "\"%s\":%s", name, tail);
    const char *p = strstr(obj, key), *end = strchr(obj, '}');
    if (!p || (end && p > end)) return NULL;
    return p + strlen(key);
}

// a number: 0 when it is not there or null
static int number(const char *obj, const char *name, float *out)
{
    const char *p = field(obj, name, "");
    if (!p || *p == 'n') return 0;
    char *e;
    *out = strtof(p, &e);
    return e != p;
}

// an array of numbers, up to n of them, a null as NAN: how many came
static int array(const char *obj, const char *name, float *out, int n)
{
    const char *p = field(obj, name, "[");
    if (!p) return 0;
    int i = 0;
    while (i < n && *p && *p != ']') {
        if (*p == 'n') {
            if (strncmp(p, "null", 4)) break;          // a body cut at "nu": 4 more would step past its end
            out[i++] = NAN;
            p += 4;
        } else {
            char *e;
            out[i++] = strtof(p, &e);
            if (e == p) break;
            p = e;
        }
        if (*p == ',') p++;
    }
    return i;
}

// the first of an array of whole numbers: the times, seconds since 1970, too big for a float's digits
static uint32_t first_time(const char *obj)
{
    const char *p = field(obj, "time", "[");
    return p ? (uint32_t)strtoul(p, NULL, 10) : 0;
}

static uint8_t kind_of(float code)
{
    const int c = isnan(code) ? 3 : (int)code;
    if (c <= 1) return WX_CLEAR;                       // WMO 0 clear, 1 mainly clear
    if (c == 2) return WX_PARTLY;
    if (c == 3) return WX_CLOUDY;
    if (c == 45 || c == 48) return WX_FOG;
    if (c >= 51 && c <= 57) return WX_DRIZZLE;
    if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return WX_RAIN;
    if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_SNOW;
    if (c >= 95) return WX_STORM;
    return WX_CLOUDY;
}

static int16_t tenths(float v) { return isnan(v) ? 0 : (int16_t)lroundf(v * 10.0f); }
// a pressure the dial can draw, else the one given: 800..1200 hPa, as the past's (the grid below it loops by the hPa)
static uint16_t hpa10_or(float v, uint16_t other) { return isnan(v) || v < 800.0f || v > 1200.0f ? other : (uint16_t)lroundf(v * 10.0f); }
// the rain of an hour, mm: never below 0 (a cast would wrap it to 6553), 500 at most
static uint16_t rain10(float v) { return (uint16_t)(isnan(v) || v <= 0.0f ? 0 : lroundf((v > 500.0f ? 500.0f : v) * 10.0f)); }
static uint8_t byte_of(float v) { return isnan(v) || v < 0 ? 0 : v > 254.0f ? 254 : (uint8_t)lroundf(v); }

int wx_parse_forecast(const char *json, weather_t *w)
{
    const char *cur = object(json, "current"), *hr = object(json, "hourly"), *dy = object(json, "daily");
    if (!cur || !hr || !dy) return 0;
    float v, a[WX_HOURS];
    weather_t n;
    memset(&n, 0, sizeof n);
    n.at = first_time(hr);
    if (!n.at) return 0;
    if (number(json, "utc_offset_seconds", &v)) n.utc_min = (int16_t)lroundf(v / 60.0f);
    if (number(json, "elevation", &v) && !isnan(v) && v > -500.0f && v < 9000.0f) n.elev = (int16_t)lroundf(v);
    const char *ct = field(cur, "time", "");         // seconds since 1970: a float has not the digits, read whole
    n.updated = ct ? (uint32_t)strtoul(ct, NULL, 10) : n.at;
    if (number(cur, "temperature_2m", &v)) n.now_t10 = tenths(v);
    n.feels10 = number(cur, "apparent_temperature", &v) ? tenths(v) : n.now_t10;
    if (number(cur, "relative_humidity_2m", &v)) n.humidity = byte_of(v);
    n.now_kind = number(cur, "weather_code", &v) ? kind_of(v) : WX_CLOUDY;
    if (number(cur, "pressure_msl", &v)) n.now_hpa10 = hpa10_or(v, 0);
    if (number(cur, "wind_direction_10m", &v)) n.wind_from = (int16_t)lroundf(v);
    if (number(cur, "wind_speed_10m", &v)) n.wind_kmh = byte_of(v);
    if (number(cur, "wind_gusts_10m", &v)) n.gust_kmh = byte_of(v);
    if (number(cur, "uv_index", &v)) n.uv10 = byte_of(v * 10.0f);
    n.aqi = WX_NONE;
    if (array(hr, "temperature_2m", a, WX_HOURS) < WX_HOURS) return 0;
    for (int i = 0; i < WX_HOURS; i++) n.hour[i].t10 = tenths(a[i]);
    if (array(hr, "weather_code", a, WX_HOURS) == WX_HOURS)
        for (int i = 0; i < WX_HOURS; i++) n.hour[i].kind = kind_of(a[i]);
    if (array(hr, "cloud_cover", a, WX_HOURS) == WX_HOURS)
        for (int i = 0; i < WX_HOURS; i++) n.hour[i].cloud = byte_of(a[i]);
    if (array(hr, "precipitation", a, WX_HOURS) == WX_HOURS)
        for (int i = 0; i < WX_HOURS; i++) n.hour[i].rain10 = rain10(a[i]);
    if (array(hr, "pressure_msl", a, WX_HOURS) == WX_HOURS)
        for (int i = 0; i < WX_HOURS; i++) n.hour[i].hpa10 = hpa10_or(a[i], n.now_hpa10);
    if (array(hr, "wind_direction_10m", a, WX_HOURS) == WX_HOURS)
        for (int i = 0; i < WX_HOURS; i++) n.hour[i].wind_from2 = isnan(a[i]) ? 0 : (uint8_t)(lroundf(a[i]) % 360 / 2);
    if (array(hr, "wind_speed_10m", a, WX_HOURS) == WX_HOURS)
        for (int i = 0; i < WX_HOURS; i++) n.hour[i].wind_kmh = byte_of(a[i]);
    if (array(hr, "wind_gusts_10m", a, WX_HOURS) == WX_HOURS)
        for (int i = 0; i < WX_HOURS; i++) n.hour[i].gust_kmh = byte_of(a[i]);
    if (array(hr, "uv_index", a, WX_HOURS) == WX_HOURS)
        for (int i = 0; i < WX_HOURS; i++) n.hour[i].uv10 = byte_of(a[i] * 10.0f);
    for (int i = 0; i < WX_HOURS; i++) n.hour[i].aqi = WX_NONE;
    // the next days: today is the first of the daily ones, so from the second on
    float code[WX_DAYS + 1], hi[WX_DAYS + 1], lo[WX_DAYS + 1];
    /* all the days or no answer: short, the days stayed 0 and every look drew four "MON, clear, 0/0" (the
     * 2026-11-27 review). a cut body is asked again, the forecast before it stays meanwhile */
    const int days = array(dy, "weather_code", code, WX_DAYS + 1);
    if (days != WX_DAYS + 1 || array(dy, "temperature_2m_max", hi, WX_DAYS + 1) != days ||
        array(dy, "temperature_2m_min", lo, WX_DAYS + 1) != days) return 0;
    const uint32_t day0 = first_time(dy);              // midnight of today at home, as UTC
    for (int i = 0; i < WX_DAYS; i++) {
        n.day[i].kind = kind_of(code[i + 1]);
        n.day[i].hi10 = tenths(hi[i + 1]);
        n.day[i].lo10 = tenths(lo[i + 1]);
        const int64_t local = (int64_t)day0 + (int64_t)n.utc_min * 60 + (int64_t)(i + 1) * 86400;
        n.day[i].wday = (uint8_t)((local / 86400 + 3) % 7);   // 1970-01-01 was a Thursday
    }
    *w = n;
    return 1;
}

int wx_parse_air(const char *json, weather_t *w)
{
    const char *cur = object(json, "current"), *hr = object(json, "hourly");
    float v, a[WX_HOURS];
    if (!cur || !hr) return 0;
    if (number(cur, "european_aqi", &v)) w->aqi = byte_of(v);
    if (number(cur, "pm2_5", &v)) w->pm25_10 = (uint16_t)(isnan(v) || v < 0 ? 0 : lroundf(v * 10.0f));
    const uint32_t t0 = first_time(hr);
    const int n = array(hr, "european_aqi", a, WX_HOURS);
    for (int i = 0; i < n; i++) {                      // matched by the hour: the two answers may start apart
        const int64_t at = (int64_t)t0 + (int64_t)i * 3600 - (int64_t)w->at;
        const int k = (int)(at / 3600);
        if (at % 3600 == 0 && k >= 0 && k < WX_HOURS) w->hour[k].aqi = isnan(a[i]) ? WX_NONE : byte_of(a[i]);
    }
    return 1;
}

int wx_parse_past(const char *json, weather_t *w)
{
    const char *hr = object(json, "hourly");
    float a[WX_PAST + 1], t[WX_PAST + 1], code[WX_PAST + 1], rain[WX_PAST + 1];
    if (!hr || !w->at) return 0;
    const uint32_t t0 = first_time(hr);
    const int n = array(hr, "pressure_msl", a, WX_PAST + 1);
    const int nt = array(hr, "temperature_2m", t, WX_PAST + 1), nc = array(hr, "weather_code", code, WX_PAST + 1);
    const int nr = array(hr, "precipitation", rain, WX_PAST + 1);
    int got = 0;
    for (int i = 0; i < n; i++) {                      // by the hour, as the air: k hours before the forecast's first
        const int64_t before = (int64_t)w->at - ((int64_t)t0 + (int64_t)i * 3600);
        const int k = (int)(before / 3600);
        if (before % 3600 || k < 1 || k > WX_PAST || isnan(a[i]) || a[i] < 800.0f || a[i] > 1200.0f) continue;
        const int j = WX_PAST - k;
        w->past_hpa10[j] = (uint16_t)lroundf(a[i] * 10.0f);
        // the rest of the hour, or the forecast's first hour's where a field did not come
        w->past_t10[j] = i < nt && !isnan(t[i]) ? tenths(t[i]) : w->hour[0].t10;
        w->past_kind[j] = i < nc && !isnan(code[i]) ? kind_of(code[i]) : w->hour[0].kind;
        w->past_rain10[j] = (uint16_t)(i < nr && !isnan(rain[i]) && rain[i] > 0 ? lroundf(rain[i] * 10.0f) : 0);
        got++;
    }
    return t0 && got > 0;
}

void wx_past_from(weather_t *w, const weather_t *was)
{
    const int moved = (int)(((int64_t)w->at - (int64_t)was->at) / 3600);
    for (int k = 0; k < WX_PAST; k++) {
        const int h = moved - (WX_PAST - k);           // that hour, from the last answer's first
        if (h >= -WX_PAST && h < 0) {
            w->past_hpa10[k] = was->past_hpa10[WX_PAST + h];
            w->past_t10[k] = was->past_t10[WX_PAST + h];
            w->past_rain10[k] = was->past_rain10[WX_PAST + h];
            w->past_kind[k] = was->past_kind[WX_PAST + h];
        } else if (h >= 0 && h < WX_HOURS) {
            w->past_hpa10[k] = was->hour[h].hpa10;
            w->past_t10[k] = was->hour[h].t10;
            w->past_rain10[k] = was->hour[h].rain10;
            w->past_kind[k] = was->hour[h].kind;
        }
    }
}
