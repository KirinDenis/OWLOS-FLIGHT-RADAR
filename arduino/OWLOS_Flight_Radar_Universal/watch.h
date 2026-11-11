/*
 * watch - planes to follow, by callsign (EZY12AB), registration (G-EUPT) or ICAO address (4CA7B4).
 * the sky round home says when one comes in range: the screen picks it. the firmware asks the
 * free networks about the others, one at a time, and the screen says when one takes off or lands
 * anywhere. no network here: watch_sky reads the sky, watch_answer what a lookup found.
 */
#pragma once
#include <stdint.h>
#include "sky.h"

#define WATCH_MAX  8
#define WATCH_ID   11            /* "OE-IJW", "EJU39NJ", "44093E" and a 0 */
#define WATCH_TEXT 100           /* the list as typed: "EJU39NJ, OE-IJW" */
#define WATCH_GONE_MS (30u * 60u * 1000u)   /* unheard this long: not flying any more, as far as we know */

enum { WATCH_UNKNOWN, WATCH_GONE, WATCH_GROUND, WATCH_AIR };     /* GONE: no network hears it */
enum { WATCH_ANY, WATCH_CALLSIGN, WATCH_REG, WATCH_HEX };         /* what an id turned out to be */
enum { WATCH_NEWS_NONE, WATCH_NEWS_DOWN, WATCH_NEWS_UP, WATCH_NEWS_RANGE };   /* what happened, the bigger wins */

typedef struct {
    char     id[WATCH_ID];       /* upper case: letters, digits and the dash only */
    uint8_t  state;              /* WATCH_UNKNOWN.. */
    uint8_t  kind;               /* WATCH_ANY till a lookup finds it */
    uint8_t  near;               /* in the sky round home now */
    uint8_t  tries;              /* which of the kinds it may be to ask about next */
    uint32_t heard;              /* ms, when a network last had it */
    uint32_t checked;            /* ms, when it was last asked about */
} watch_t;

typedef struct {
    watch_t w[WATCH_MAX];
    int     n;
} watchlist_t;

/* "EJU39NJ, oe-ijw 44093e": up to WATCH_MAX ids, upper case, each once. one already there keeps
 * what is known of it. returns how many */
int  watch_set(watchlist_t *l, const char *text);
void watch_text(const watchlist_t *l, char *out, int cap);     /* "EJU39NJ, OE-IJW, 44093E" */
int  watch_is(const watch_t *w, const plane_t *p);             /* this plane is the one */
int  watch_count(const watchlist_t *l, int state);             /* how many are in that state */
int  watch_near(const watchlist_t *l);                         /* how many are round home */

/* the sky round home after an answer. say: what the screen should tell ("EJU39NJ IN RANGE"), left
 * as it was when nothing happened. pick: the address of one that just came in range, else as it was.
 * returns what happened (WATCH_NEWS_*), for the LED */
int  watch_sky(watchlist_t *l, const sky_t *s, uint32_t now, char *say, int cap, uint32_t *pick);

/* the lookups: which one to ask about next (not round home, asked longest ago; -1 none), what to
 * ask it as, and what came back: the plane, or NULL when that answer had no such plane */
int  watch_next(const watchlist_t *l);
int  watch_kind(const watch_t *w);
int  watch_answer(watch_t *w, int kind, const plane_t *p, uint32_t now, char *say, int cap);   /* WATCH_NEWS_* */
