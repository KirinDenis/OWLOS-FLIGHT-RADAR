/*
 * sky - the aircraft around home. no network here: the firmware fetches the JSON and hands it
 * to sky_read piece by piece as it comes in, the PC preview makes it up. two formats:
 *   ADS-B Exchange v2   {"ac":[{"hex":..,"flight":..,"lat":..,"lon":..,"alt_baro":..}, ..]}
 *                       spoken by the free networks: adsb.lol as is, adsb.fi calls the list "aircraft"
 *   Flightradar24 API   {"data":[{"hex":..,"callsign":..,"lat":..,"lon":..,"alt":..}, ..]}
 *                       paid, with a key, adds where the flight comes from and goes to
 * the list keeps the nearest SKY_MAX, merges every answer into what it had (so each plane keeps
 * its trail) and forgets a plane a while after the server stops mentioning it.
 */
#pragma once
#include <stdint.h>

#define SKY_MAX    48
#define TRAIL_N    16            /* old positions kept for the trail */
#define SKY_FORGET 30000u        /* ms without a word about it, then it is gone */

/* what kind of aircraft, from the ADS-B emitter category (or guessed from the type) */
enum { KIND_JET, KIND_LIGHT, KIND_BIZJET, KIND_HEAVY, KIND_FAST, KIND_HELI, KIND_GLIDER,
       KIND_BALLOON, KIND_DRONE, KIND_GROUND, KINDS };

typedef struct {
    uint32_t icao;               /* 24-bit address, the key. 0 = free slot */
    char     flight[9];          /* callsign */
    char     reg[10];            /* registration */
    char     type[5];            /* ICAO type designator: B738, A320.. */
    char     orig[4], dest[4];   /* IATA airports, FR24 only */
    uint8_t  kind;
    uint8_t  ground;
    uint8_t  has_alt, has_spd;
    float    lat, lon;
    int32_t  alt;                /* feet, barometric */
    float    gs;                 /* knots */
    float    track;              /* degrees, true */
    int16_t  vrate;              /* feet a minute */
    uint16_t squawk;             /* as the four octal digits read in decimal: 7700 */
    uint32_t seen;               /* our ms clock at this position */
    uint32_t heard;              /* our ms clock at the last answer that had it */
    uint32_t first;              /* our ms clock when it first showed up, for the ring it gets */
    float    x, y;               /* km east and north of home at `seen` */
    float    dist, brg;          /* km and degrees from home */
    int16_t  tx[TRAIL_N], ty[TRAIL_N];   /* trail, 10 m units, oldest first */
    uint8_t  tn;
} plane_t;

enum { SKY_WAIT, SKY_OK, SKY_NO_SETUP, SKY_NO_WIFI, SKY_CONNECTING, SKY_HTTP, SKY_PARSE };

typedef struct {
    plane_t  p[SKY_MAX];
    int      n;                  /* used slots are p[0..n-1] */
    float    home_lat, home_lon;
    uint32_t updated;            /* ms of the last good answer */
    int      status;
    char     note[48];           /* what went wrong, for the screen */
    int      total;              /* how many the last answer had, before the cap */
    uint8_t  hide_ground;        /* planes on the ground left out, sky_hide_ground */
} sky_t;

void sky_init(sky_t *s, float lat, float lon);
void sky_home(sky_t *s, float lat, float lon);    /* moves home, positions recalculated */
/* planes on the ground left out or not. left out, the ones there go at once, and in the answers
 * they take none of the SKY_MAX places: next to a busy airport they would take most of them */
void sky_hide_ground(sky_t *s, int hide);

/* an answer read as it arrives. a busy sky is 100 planes, a kilobyte each: more than the board
 * has free. so one plane's object is held at a time and only the nearest SKY_MAX are kept;
 * sky_read_end merges them into the sky (the caller holds the lock for that, not for the
 * download) */
#define SKY_OBJ 2048             /* the longest object kept, ADS-B Exchange writes ~1 KB */
typedef struct {
    int      fr24;
    float    home_lat, home_lon;
    int      hide_ground;
    uint32_t now;
    int      state;              /* 0 looking for the list, 1 in it, 2 past it */
    int      depth, in_str, esc, keyn;
    char     key[12];            /* the last string at the top level: the name of what follows */
    char     obj[SKY_OBJ];
    int      objn, too_long;
    plane_t  got[SKY_MAX];
    int      n, total;
} sky_reader_t;

void sky_read_begin(sky_reader_t *r, const sky_t *s, int fr24, uint32_t now);
void sky_read(sky_reader_t *r, const char *data, int len);
int  sky_read_end(sky_reader_t *r, sky_t *s);     /* planes merged, -1 = the answer had no list */
/* a whole answer at once (the demo, the preview). returns planes read, -1 = no list in it */
int  sky_parse_adsbx(sky_t *s, const char *json, int len, uint32_t now);
int  sky_parse_fr24(sky_t *s, const char *json, int len, uint32_t now);
void sky_forget(sky_t *s, uint32_t now);          /* drops the ones not heard for SKY_FORGET */

/* where a plane is now, dead-reckoned from its last position along its track: km from home */
void plane_now(const plane_t *p, uint32_t now, float *x, float *y);
/* km east and north of (lat0, lon0). flat earth, good to a fraction of a percent at 300 km */
void geo_offset(float lat0, float lon0, float lat, float lon, float *east, float *north);
const char *kind_name(int kind);
