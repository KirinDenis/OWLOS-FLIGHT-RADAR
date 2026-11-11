// the LED's events, see light.h
#include "light.h"
#include <string.h>

/* each event's colour and blink: lit on_ms of every period_ms. an emergency blinks fast, the no data
 * light does not blink at all */
static const struct { uint8_t r, g, b; uint16_t on_ms, period_ms; } LOOK[LIGHT_EVENTS] = {
    [LIGHT_SOS]    = { 255, 0, 0, 120, 250 },        // red
    [LIGHT_RANGE]  = { 0, 255, 0, 700, 1400 },       // green
    [LIGHT_UP]     = { 0, 0, 255, 700, 1400 },       // blue
    [LIGHT_DOWN]   = { 0, 255, 255, 700, 1400 },     // cyan
    [LIGHT_NEAR]   = { 255, 255, 255, 300, 2000 },   // white, a short blink now and then
    [LIGHT_NODATA] = { 255, 80, 0, 1000, 1000 },     // amber
};

static const uint8_t LEVEL[LIGHT_LEVELS] = { 12, 64, 255 };   // the CYD's LED is bright: low is for the night
static const uint8_t NEAR_UNITS[LIGHT_NEARS] = { 1, 2, 5, 10 };
static const uint8_t RED_GPIO[LIGHT_REDS] = { 4, 22 };

void light_defaults(light_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = LIGHT_CFG_VERSION;
    c->on[LIGHT_SOS] = c->on[LIGHT_RANGE] = c->on[LIGHT_UP] = c->on[LIGHT_DOWN] = 1;
    // any plane near and no data: off. next to an airport the one is on all day, a fresh board has the other
    c->level = LIGHT_MID;
    c->hold = LIGHT_TOUCH;
    c->near = 1;
}

int light_near_units(const light_cfg_t *c) { return NEAR_UNITS[c->near < LIGHT_NEARS ? c->near : 1]; }

int light_near_index(int units)
{
    for (int k = 0; k < LIGHT_NEARS; k++) if (NEAR_UNITS[k] == units) return k;
    return -1;
}

int light_red_gpio(const light_cfg_t *c) { return RED_GPIO[c->red < LIGHT_REDS ? c->red : LIGHT_RED_4]; }

int light_red_index(int gpio)
{
    for (int k = 0; k < LIGHT_REDS; k++) if (RED_GPIO[k] == gpio) return k;
    return -1;
}

void light_event(light_t *l, int ev, uint32_t now) { l->since[ev] = now ? now : 1; }

void light_hold(light_t *l, int ev, int on, uint32_t now)
{
    if (on && !l->holds[ev]) light_event(l, ev, now);
    l->holds[ev] = (uint8_t)(on != 0);
}

void light_sky(light_t *l, const light_cfg_t *c, const sky_t *s, float unit_km, uint32_t now)
{
    const float near_km = (float)light_near_units(c) * unit_km;
    int sos = 0, n = 0;
    uint32_t close[SKY_MAX] = { 0 };
    for (int i = 0; i < s->n; i++) {
        const plane_t *p = &s->p[i];
        sos |= p->squawk == 7500 || p->squawk == 7600 || p->squawk == 7700;   // hijack, radio, emergency
        if (p->ground || p->kind == KIND_GROUND || p->dist > near_km) continue;
        close[n++] = p->icao;
        int known = 0;                                   // near last time too: it lit the LED then
        for (int k = 0; k < l->n_close; k++) known |= l->close[k] == p->icao;
        if (!known) light_event(l, LIGHT_NEAR, now);
    }
    memcpy(l->close, close, sizeof close[0] * (size_t)n);
    l->n_close = n;
    light_hold(l, LIGHT_NEAR, n > 0, now);
    light_hold(l, LIGHT_SOS, sos, now);
}

void light_touch(light_t *l) { memset(l->since, 0, sizeof l->since); }

void light_test(light_t *l, uint32_t now) { l->test = now ? now : 1; }

// lit now: switched on, and still within its time. take-off and landing are moments, not states
static int lit(const light_t *l, const light_cfg_t *c, int ev, uint32_t now)
{
    if (!c->on[ev] || !l->since[ev]) return 0;
    uint32_t age = now - l->since[ev];
    int moment = ev == LIGHT_UP || ev == LIGHT_DOWN;
    if (c->hold == LIGHT_10S || (c->hold == LIGHT_WHILE && moment)) return age < 10000u;
    if (c->hold == LIGHT_WHILE) return l->holds[ev];
    return age < 60000u;                                // till touched, a minute at most
}

void light_colour(const light_t *l, const light_cfg_t *c, uint32_t now, uint8_t rgb[3])
{
    const int k = LEVEL[c->level < LIGHT_LEVELS ? c->level : LIGHT_MID];
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (l->test && now - l->test < LIGHT_TEST_MS) {     // red, green, blue
        rgb[(now - l->test) / 1500u] = (uint8_t)k;
        return;
    }
    for (int ev = 0; ev < LIGHT_EVENTS; ev++) {
        if (!lit(l, c, ev, now)) continue;
        if ((now - l->since[ev]) % LOOK[ev].period_ms >= LOOK[ev].on_ms) return;   // the dark part of its blink
        rgb[0] = (uint8_t)(LOOK[ev].r * k / 255);
        rgb[1] = (uint8_t)(LOOK[ev].g * k / 255);
        rgb[2] = (uint8_t)(LOOK[ev].b * k / 255);
        return;
    }
}
