# The RGB LED, complete

`core/light.h`, `core/light.c` and `firmware/main/led.c` from OWLOS-FLIGHT-RADAR (0.17). Status: `light.c`
is PC-tested, each event at each level and the screen's blink (`light_shown`). The CYD's three pins were checked by
a person looking at the LED (the agent cannot see it). The WS2812 path is built, not run on a board.

The trap this file once carried into 0.16: `light_colour` kept the brightness in `k` and then looped with
`for (int k = 0; ...)`, which hid it. Each colour went out times its place in `ORDER`, an emergency times 0, and
only TEST lit the LED [measured on the PC, 2026-11-27]. A loop counter never takes the name of a value outside
it (MSVC `/W4` C4456, GCC `-Wshadow`), and a PC test asks each event for its colour.

## core/light.h

```c
/*
 * light - the RGB LED on the back of the 2.8" and 3.5" CYD, lit by what happens. each event is the
 * user's to switch on or off, and how bright and for how long. the firmware says what happened
 * (light_event, light_hold, light_sky) and asks each frame what to show (light_colour); a touch on
 * the screen puts out what is lit. no hardware here: firmware/main/led.c drives the pins.
 */
#pragma once
#include <stdint.h>
#include "sky.h"

/* in the order they are kept and sent (light_cfg_t.on, the site's "led" bits): a new one goes last.
 * which wins when several are lit is light.c's ORDER. WEATHER: a weather warning came (core/weather.h) */
enum { LIGHT_SOS, LIGHT_RANGE, LIGHT_UP, LIGHT_DOWN, LIGHT_NEAR, LIGHT_NODATA, LIGHT_WEATHER, LIGHT_EVENTS };
enum { LIGHT_LOW, LIGHT_MID, LIGHT_HIGH, LIGHT_LEVELS };
enum { LIGHT_TOUCH, LIGHT_10S, LIGHT_WHILE, LIGHT_HOLDS };     /* till touched (a minute at most), 10 s, while it lasts */
#define LIGHT_NEARS 4               /* how close is near: 1, 2, 5 or 10 of the distance unit */
/* the red one's pin: GPIO 4 on Sunton's CYDs, GPIO 22 on LCDWIKI's ESP32-32E boards (Elegoo sells
 * them too). the screen and the touch are on the same pins, nothing tells the two apart: the user
 * picks the one that is red in the LED test */
enum { LIGHT_RED_4, LIGHT_RED_22, LIGHT_REDS };

#define LIGHT_CFG_VERSION 4         /* 4: WEATHER. firmware/main/store.c reads 3 and 2 over the defaults */
#define LIGHT_TEST_MS 4500u         /* red, green, blue, 1.5 s each */

typedef struct {
    uint8_t version;
    uint8_t on[LIGHT_EVENTS];       /* 1: that event lights it */
    uint8_t level;                  /* LIGHT_LOW.. */
    uint8_t hold;                   /* LIGHT_TOUCH.. */
    uint8_t near;                   /* 0..LIGHT_NEARS-1: light_near_units */
    uint8_t red;                    /* LIGHT_RED_4..: light_red_gpio. version 2 ended before it */
} light_cfg_t;

typedef struct {
    uint32_t since[LIGHT_EVENTS];   /* ms it came, 0: not lit */
    uint8_t  holds[LIGHT_EVENTS];   /* what is behind it is still there: in range, squawking, near, no data */
    uint32_t test;                  /* ms the test began, 0: none */
    uint32_t close[SKY_MAX];        /* the planes near last time: a new one lights it again */
    int      n_close;
} light_t;

void light_defaults(light_cfg_t *c);
int  light_near_units(const light_cfg_t *c);                  /* 1, 2, 5 or 10 */
int  light_near_index(int units);                             /* and back: -1 for none of them */
int  light_red_gpio(const light_cfg_t *c);                    /* 4 or 22 */
int  light_red_index(int gpio);                               /* and back: -1 for neither */
void light_event(light_t *l, int ev, uint32_t now);           /* it happened now */
void light_hold(light_t *l, int ev, int on, uint32_t now);    /* a state: lights it when it begins */
/* the sky after an answer: an emergency squawk in it, a plane in the air within near_km of home */
void light_sky(light_t *l, const light_cfg_t *c, const sky_t *s, float unit_km, uint32_t now);
void light_touch(light_t *l);                                 /* the screen was touched: all of it goes out */
void light_test(light_t *l, uint32_t now);                    /* red, green, blue in turn */
/* what the LED shows now, 0..255 a colour, the brightness in it */
void light_colour(const light_t *l, const light_cfg_t *c, uint32_t now, uint8_t rgb[3]);
/* the same on the screen (SET's ON SCREEN, core/ui.c ui_alarm): the event in the lit part of its blink, -1 none or
 * dark. the blink is slower where the LED's is short, the test is the LED's alone */
int  light_shown(const light_t *l, const light_cfg_t *c, uint32_t now);
void light_screen_rgb(int ev, uint8_t rgb[3]);                /* an event's colour on a screen */
```

## core/light.c

```c
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
```

## firmware/main/led.c

```c
/* the RGB LED on the back of the CYD: three LEDC channels on a timer of their own, the backlight's
 * (lcd_spi.c, timer 0 and channel 0) is left alone. each colour is pulled to ground to light, so
 * the outputs are inverted: duty 0 is dark. a WS2812 instead on the boards that have one (board.h
 * PIN_LED_WS2812). what it shows is core/light.c's */
#include "led.h"
#include "board.h"

#if RADAR_HAS_LED && defined(PIN_LED_WS2812)
/* one WS2812 on one pin: 24 bits, green red blue, the highest first. a bit is a high and a low,
 * 0.3 + 0.9 us a 0 and 0.9 + 0.3 us a 1, sent by the RMT. 50 us low and it takes them: the screen
 * task changes it at most once a frame, far more apart than that */
#include <string.h>
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

static const char *TAG = "led";
static rmt_channel_handle_t s_ch;
static rmt_encoder_handle_t s_bits;
static uint8_t s_now[3];
static uint8_t s_grb[3];                               // the RMT reads it after rmt_transmit is back
static int s_ready;

void led_init(int red_gpio)
{
    (void)red_gpio;                                    // one pin for all three colours here
    rmt_tx_channel_config_t ch = {
        .gpio_num = PIN_LED_WS2812,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,             // a tick is 0.1 us
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,   // 24 bits fit
        .trans_queue_depth = 1,
    };
    rmt_bytes_encoder_config_t bits = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
        .flags.msb_first = 1,
    };
    esp_err_t e = rmt_new_tx_channel(&ch, &s_ch);
    if (e == ESP_OK) e = rmt_new_bytes_encoder(&bits, &s_bits);
    if (e == ESP_OK) e = rmt_enable(s_ch);
    if (e != ESP_OK) { ESP_LOGE(TAG, "WS2812 on GPIO %d: %s", PIN_LED_WS2812, esp_err_to_name(e)); return; }
    s_ready = 1;
    s_now[0] = 1;                                      // not what led_set gets first: dark goes out too
    const uint8_t dark[3] = { 0, 0, 0 };
    led_set(dark);
    ESP_LOGI(TAG, "WS2812 LED on GPIO %d", PIN_LED_WS2812);
}

void led_red(int gpio) { (void)gpio; }                 // a CYD setting, nothing to move here

void led_set(const uint8_t rgb[3])
{
    if (!s_ready || !memcmp(rgb, s_now, 3)) return;
    memcpy(s_now, rgb, 3);
    rmt_tx_wait_all_done(s_ch, 20);                    // the last one out first: s_grb is read while it goes
    s_grb[0] = rgb[1];
    s_grb[1] = rgb[0];
    s_grb[2] = rgb[2];
    const rmt_transmit_config_t once = { .loop_count = 0 };
    rmt_transmit(s_ch, s_bits, s_grb, sizeof s_grb, &once);
}

#elif RADAR_HAS_LED
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "led";
static int s_pin[3] = { -1, PIN_LED_G, PIN_LED_B };           // red's comes from the settings
static const ledc_channel_t CH[3] = { LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3 };
static uint8_t s_now[3];
static int s_ready;

static esp_err_t channel(int i)
{
    ledc_channel_config_t ch = {
        .gpio_num = s_pin[i],
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CH[i],
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 1,
    };
    s_now[i] = 0;
    return ledc_channel_config(&ch);
}

void led_init(int red_gpio)
{
    s_pin[0] = red_gpio;
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                              .timer_num = LEDC_TIMER_1, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) { ESP_LOGE(TAG, "timer: %s", esp_err_to_name(e)); return; }
    for (int i = 0; i < 3; i++) {
        e = channel(i);
        if (e != ESP_OK) { ESP_LOGE(TAG, "GPIO %d: %s", s_pin[i], esp_err_to_name(e)); return; }
    }
    s_ready = 1;
    ESP_LOGI(TAG, "RGB LED on GPIO %d %d %d, lit by pulling them low", s_pin[0], s_pin[1], s_pin[2]);
}

void led_red(int gpio)
{
    if (!s_ready || gpio == s_pin[0]) return;
    gpio_reset_pin(s_pin[0]);                          // off the LEDC: an input with its pull-up, nothing driven
    s_pin[0] = gpio;
    esp_err_t e = channel(0);
    if (e != ESP_OK) ESP_LOGE(TAG, "GPIO %d: %s", gpio, esp_err_to_name(e));
    else ESP_LOGI(TAG, "red moved to GPIO %d", gpio);
}

void led_set(const uint8_t rgb[3])
{
    if (!s_ready) return;
    for (int i = 0; i < 3; i++) {
        if (rgb[i] == s_now[i]) continue;              // the same as now: nothing to write
        s_now[i] = rgb[i];
        ledc_set_duty(LEDC_LOW_SPEED_MODE, CH[i], (uint32_t)rgb[i] * 1023u / 255u);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, CH[i]);
    }
}

#else
void led_init(int red_gpio) { (void)red_gpio; }
void led_red(int gpio) { (void)gpio; }
void led_set(const uint8_t rgb[3]) { (void)rgb; }
#endif
```

