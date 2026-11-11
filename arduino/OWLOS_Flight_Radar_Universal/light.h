/*
 * light - the RGB LED on the back of the 2.8" and 3.5" CYD, lit by what happens. each event is the
 * user's to switch on or off, and how bright and for how long. the firmware says what happened
 * (light_event, light_hold, light_sky) and asks each frame what to show (light_colour); a touch on
 * the screen puts out what is lit. no hardware here: firmware/main/led.c drives the pins.
 */
#pragma once
#include <stdint.h>
#include "sky.h"

/* in the order they win when several are lit */
enum { LIGHT_SOS, LIGHT_RANGE, LIGHT_UP, LIGHT_DOWN, LIGHT_NEAR, LIGHT_NODATA, LIGHT_EVENTS };
enum { LIGHT_LOW, LIGHT_MID, LIGHT_HIGH, LIGHT_LEVELS };
enum { LIGHT_TOUCH, LIGHT_10S, LIGHT_WHILE, LIGHT_HOLDS };     /* till touched (a minute at most), 10 s, while it lasts */
#define LIGHT_NEARS 4               /* how close is near: 1, 2, 5 or 10 of the distance unit */
/* the red one's pin: GPIO 4 on Sunton's CYDs, GPIO 22 on LCDWIKI's ESP32-32E boards (Elegoo sells
 * them too). the screen and the touch are on the same pins, nothing tells the two apart: the user
 * picks the one that is red in the LED test */
enum { LIGHT_RED_4, LIGHT_RED_22, LIGHT_REDS };

#define LIGHT_CFG_VERSION 3
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
