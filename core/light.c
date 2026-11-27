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
    [LIGHT_WEATHER] = { 255, 170, 0, 1000, 2000 },   // yellow, slow: no data's amber does not blink
};

/* which wins when several are lit: an emergency, a weather warning, the watched planes, any plane
 * near, no data last */
static const uint8_t ORDER[LIGHT_EVENTS] = { LIGHT_SOS, LIGHT_WEATHER, LIGHT_RANGE, LIGHT_UP, LIGHT_DOWN, LIGHT_NEAR, LIGHT_NODATA };

static const uint8_t LEVEL[LIGHT_LEVELS] = { 12, 64, 255 };   // the CYD's LED is bright: low is for the night
static const uint8_t NEAR_UNITS[LIGHT_NEARS] = { 1, 2, 5, 10 };
static const uint8_t RED_GPIO[LIGHT_REDS] = { 4, 22 };

void light_defaults(light_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = LIGHT_CFG_VERSION;
    c->on[LIGHT_SOS] = c->on[LIGHT_RANGE] = c->on[LIGHT_UP] = c->on[LIGHT_DOWN] = c->on[LIGHT_WEATHER] = 1;
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

// the one that wins of those lit now (ORDER), -1 none
static int winner(const light_t *l, const light_cfg_t *c, uint32_t now)
{
    for (int i = 0; i < LIGHT_EVENTS; i++)
        if (lit(l, c, ORDER[i], now)) return ORDER[i];
    return -1;
}

void light_colour(const light_t *l, const light_cfg_t *c, uint32_t now, uint8_t rgb[3])
{
    const int k = LEVEL[c->level < LIGHT_LEVELS ? c->level : LIGHT_MID];
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (l->test && now - l->test < LIGHT_TEST_MS) {     // red, green, blue
        rgb[(now - l->test) / 1500u] = (uint8_t)k;
        return;
    }
    const int ev = winner(l, c, now);
    /* the brightness is k. 0.16 named its loop's counter k too, which hid it: each colour went out times its
     * place in ORDER, an emergency times 0, the rest 1..6 of 255, and only TEST lit the LED (found 2026-11-27) */
    if (ev < 0 || (now - l->since[ev]) % LOOK[ev].period_ms >= LOOK[ev].on_ms) return;   // none, or the dark part of its blink
    rgb[0] = (uint8_t)(LOOK[ev].r * k / 255);
    rgb[1] = (uint8_t)(LOOK[ev].g * k / 255);
    rgb[2] = (uint8_t)(LOOK[ev].b * k / 255);
}

/* the screen's blink: a screen drawn a few times a second (the big ones' background) misses a short one, so
 * the lit part is 400 ms at least and so is the dark part, unless the LED's has none (no data's amber) */
int light_shown(const light_t *l, const light_cfg_t *c, uint32_t now)
{
    const int ev = winner(l, c, now);
    if (ev < 0) return -1;
    const uint32_t steady = LOOK[ev].on_ms >= LOOK[ev].period_ms;
    const uint32_t on = LOOK[ev].on_ms < 400u ? 400u : LOOK[ev].on_ms;
    const uint32_t period = steady ? on : (LOOK[ev].period_ms < on + 400u ? on + 400u : LOOK[ev].period_ms);
    return (now - l->since[ev]) % period < on ? ev : -1;
}

void light_screen_rgb(int ev, uint8_t rgb[3])
{
    /* as the LED, made to read on a screen: the LED's blue is too dark behind dark letters, its white is the
     * screens' own */
    static const uint8_t S[LIGHT_EVENTS][3] = {
        [LIGHT_SOS] = { 255, 56, 56 },   [LIGHT_RANGE] = { 70, 235, 100 }, [LIGHT_UP] = { 90, 150, 255 },
        [LIGHT_DOWN] = { 40, 225, 235 }, [LIGHT_NEAR] = { 242, 246, 252 }, [LIGHT_NODATA] = { 255, 135, 20 },
        [LIGHT_WEATHER] = { 255, 205, 40 },
    };
    const int e = ev >= 0 && ev < LIGHT_EVENTS ? ev : LIGHT_SOS;
    rgb[0] = S[e][0];
    rgb[1] = S[e][1];
    rgb[2] = S[e][2];
}
