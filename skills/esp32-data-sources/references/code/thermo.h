/*
 * thermo - the thermostat itself: probe in, relay out, once a second. no hardware here, the
 * firmware feeds it the DS18B20 and drives the GPIO, the PC preview feeds it a simulated room.
 *
 * the rules, in the order they are checked every second:
 *  1. no probe for 3 s, or too hot (HEAT) / too cold (COOL) past the safety limit, or the
 *     no-rise alarm: relay OFF at once. a thermostat that can't see must not heat.
 *  2. HEAT: relay on at target - band/2 and below, off at target + band/2 and above.
 *     COOL the other way round. in between it keeps doing what it did (the hysteresis), so
 *     the relay does not chatter around the target.
 *  3. a relay that wants to change waits for its minimum off / on time. compressors die from
 *     short cycling, so after power on it also waits the off time before the first start.
 * every minute one point goes into the 24 h history (temperature, target, how long it ran).
 */
#pragma once
#include <stdint.h>

enum { MODE_OFF, MODE_HEAT, MODE_COOL, MODES };
enum { FAULT_NONE, FAULT_PROBE, FAULT_LIMIT, FAULT_NORISE };

/* settings, saved by the firmware. the UI edits a copy and hands it back whole */
typedef struct {
    uint16_t version;      /* THERMO_CFG_VERSION, an old blob is ignored */
    uint8_t  mode;
    uint8_t  units;        /* 0 C, 1 F. everything inside stays in C */
    float    set;          /* target, C */
    float    band;         /* hysteresis, full width, C */
    uint16_t min_on_s;     /* once on, stays on at least this long */
    uint16_t min_off_s;    /* once off, stays off at least this long, also after power on */
    float    offset;       /* added to what the probe reads */
    float    limit_hi;     /* HEAT: relay forced off at or above this */
    float    limit_lo;     /* COOL: relay forced off at or below this */
    uint16_t norise_min;   /* relay on this long without 0.5 C of change = alarm. 0 = no alarm */
    uint8_t  relay_low;    /* the relay module switches on a LOW input */
    uint8_t  led;          /* the board's RGB LED shows the relay */
    uint8_t  dim;          /* screen dims after DIM_S[dim] seconds */
    uint8_t  theme;
    uint8_t  sim;          /* the probe is simulated (core/sim.c), everything else works as usual */
    uint8_t  chosen;       /* the user picked a mode at least once (first run asks) */
} thermo_cfg_t;

#define THERMO_CFG_VERSION 1
#define SET_MIN   (-40.0f)
#define SET_MAX   110.0f

void thermo_cfg_defaults(thermo_cfg_t *c);
void thermo_cfg_clamp(thermo_cfg_t *c);       /* keeps every value in range and the target inside the limits */
float thermo_set_max(const thermo_cfg_t *c);  /* how far the target may go, the safety limits keep it in */
float thermo_set_min(const thermo_cfg_t *c);

/* one point a minute. t and set in 0.01 C */
typedef struct {
    int16_t t, set;
    uint8_t on;            /* seconds of that minute the relay was on, 0..60 */
    uint8_t flags;
} hpoint_t;

#define HIST_N      1440           /* 24 h */
#define HIST_NONE   INT16_MIN      /* no reading that minute */
#define HF_STARTS   0x03           /* relay starts in that minute, 0..3 */
#define HF_FAULT    0x04
#define HF_BOOT     0x08           /* first minute after power on: how long it was off is unknown */
#define HF_HEAT     0x10
#define HF_COOL     0x20
#define HF_SIM      0x40           /* the reading came from the simulated probe */

#define TREND_N 30                 /* one sample every 10 s = the last 5 minutes make the trend */

typedef struct {
    uint32_t up;           /* seconds since start */
    /* probe */
    float    t;            /* filtered, offset applied, C. only if have_t */
    int      have_t;
    int      bad;          /* failed reads in a row */
    /* relay */
    int      relay;        /* what the relay does */
    int      want;         /* what the temperature asks for */
    int      hold_s;       /* > 0: relay should change but waits this long (min on / off) */
    uint32_t switched;     /* `up` at the last change */
    uint32_t starts;       /* since power on */
    int      fault;
    int      norise;       /* latched, only thermo_clear_alarm() clears it */
    float    run_t0;       /* no-rise watch: temperature at the last 0.5 C of progress */
    uint32_t run_since;
    int      last_mode;
    /* trend */
    float    tr[TREND_N];
    int      tr_n, tr_head;
    float    rate;         /* C per hour, + = getting warmer */
    int      have_rate;
    /* history: h_head is the next slot to write, h_n how many are filled */
    hpoint_t h[HIST_N];
    int      h_head, h_n;
    float    acc_t;        /* this minute so far */
    float    acc_set;
    int      acc_n, acc_on, acc_s, acc_flags;
} thermo_t;

void thermo_init(thermo_t *th);
/* once a second. ok = the probe gave a good reading, probe_c = what it read */
void thermo_tick(thermo_t *th, const thermo_cfg_t *c, int ok, float probe_c);
void thermo_clear_alarm(thermo_t *th);
/* after power on with a history from flash: next point gets HF_BOOT */
void thermo_mark_boot(thermo_t *th);

/* where the relay switches next: on at, off at (C) */
float thermo_on_at(const thermo_cfg_t *c);
float thermo_off_at(const thermo_cfg_t *c);

/* history by age in minutes: 0 = the minute that is running now (partial), 1 = last full one.
 * returns 0 if there is nothing that old */
int thermo_point(const thermo_t *th, int age, hpoint_t *out);

typedef struct {
    float lo, hi, avg;     /* C, only if n > 0 */
    float duty;            /* 0..1 of the time the relay was on */
    float starts_h;        /* relay starts per hour */
    int   n;               /* minutes with a reading */
    int   minutes;         /* minutes looked at */
} tstats_t;
void thermo_stats(const thermo_t *th, int minutes, tstats_t *s);

/* history to bytes and back, for the flash (firmware/main/histstore.c) */
int  thermo_hist_size(void);
void thermo_hist_pack(const thermo_t *th, uint8_t *buf);
int  thermo_hist_unpack(thermo_t *th, const uint8_t *buf, int len);

/* units: temperatures and differences */
static inline float to_units(float c, int units) { return units ? c * 1.8f + 32.0f : c; }
static inline float from_units(float v, int units) { return units ? (v - 32.0f) / 1.8f : v; }
static inline float delta_units(float d, int units) { return units ? d * 1.8f : d; }
