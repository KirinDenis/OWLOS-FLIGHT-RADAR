// the weather's warnings, see weather.h
#include "weather.h"
#include <string.h>

int wx_alerts(const weather_t *w, unsigned mask, wx_alert_t *a)
{
    memset(a, 0, sizeof *a);
    if (!w || !w->at) return WXA_NONE;
    const int n = WXA_AHEAD < WX_HOURS - 1 ? WXA_AHEAD : WX_HOURS - 1;
    if (mask & (1u << WXA_STORM)) {
        // the strongest gust now or ahead; thunder ahead is amber at least
        int gust = w->gust_kmh, at = 0, thunder = -1;
        for (int i = 0; i <= n; i++) {
            if (w->hour[i].gust_kmh > gust) { gust = w->hour[i].gust_kmh; at = i; }
            if (thunder < 0 && w->hour[i].kind == WX_STORM) thunder = i;
        }
        if (thunder < 0 && w->now_kind == WX_STORM) thunder = 0;
        int level = gust >= 100 ? WXA_RED : gust >= 75 ? WXA_AMBER : WXA_NONE;
        if (level == WXA_NONE && thunder >= 0) {
            level = WXA_AMBER;
            at = thunder;
            a->thunder = 1;
        }
        a->level[WXA_STORM] = (uint8_t)level;
        a->hour[WXA_STORM] = (uint8_t)at;
        a->value[WXA_STORM] = (int16_t)gust;
    }
    if (mask & (1u << WXA_PRESSURE) && w->now_hpa10) {
        // the biggest change from now within the hours ahead, either way
        int worst = 0, at = 0;
        for (int i = 1; i <= n; i++) {
            if (!w->hour[i].hpa10) continue;
            const int d = (int)w->hour[i].hpa10 - (int)w->now_hpa10;
            if ((d < 0 ? -d : d) > (worst < 0 ? -worst : worst)) { worst = d; at = i; }
        }
        const int size = worst < 0 ? -worst : worst;
        a->level[WXA_PRESSURE] = (uint8_t)(size >= 100 ? WXA_RED : size >= 60 ? WXA_AMBER : WXA_NONE);
        a->hour[WXA_PRESSURE] = (uint8_t)at;
        a->value[WXA_PRESSURE] = (int16_t)worst;
    }
    if (mask & (1u << WXA_AIR)) {
        // the worst air now or ahead, of the hours the air's forecast gave
        int aqi = w->aqi != WX_NONE ? w->aqi : -1, at = 0;
        for (int i = 0; i <= n; i++) {
            if (w->hour[i].aqi != WX_NONE && w->hour[i].aqi > aqi) { aqi = w->hour[i].aqi; at = i; }
        }
        a->level[WXA_AIR] = (uint8_t)(aqi >= 80 ? WXA_RED : aqi >= 60 ? WXA_AMBER : WXA_NONE);
        a->hour[WXA_AIR] = (uint8_t)at;
        a->value[WXA_AIR] = (int16_t)(aqi < 0 ? 0 : aqi);
    }
    if (mask & (1u << WXA_RADIATION) && w->gamma_nsv) {
        const int nsv = w->gamma_nsv;
        a->level[WXA_RADIATION] = (uint8_t)(nsv >= 1000 ? WXA_RED : nsv >= 300 ? WXA_AMBER : WXA_NONE);
        a->value[WXA_RADIATION] = (int16_t)(nsv > 32767 ? 32767 : nsv);
    }
    int top = WXA_NONE;
    for (int k = 0; k < WXA_KINDS; k++) if (a->level[k] > top) top = a->level[k];
    return top;
}

int wx_alert_worst(const wx_alert_t *a)
{
    int k = -1;
    for (int i = 0; i < WXA_KINDS; i++) if (a->level[i] && (k < 0 || a->level[i] > a->level[k])) k = i;
    return k;
}
