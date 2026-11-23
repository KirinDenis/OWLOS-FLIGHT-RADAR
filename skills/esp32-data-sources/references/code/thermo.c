// the thermostat, see thermo.h for the rules
#include "thermo.h"
#include <math.h>
#include <string.h>

void thermo_cfg_defaults(thermo_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = THERMO_CFG_VERSION;
    c->mode = MODE_OFF;           // nothing switches until somebody says what is on the relay
    c->set = 21.0f;
    c->band = 0.5f;
    c->min_on_s = 30;
    c->min_off_s = 60;
    c->limit_hi = 35.0f;
    c->limit_lo = 0.0f;
    c->norise_min = 60;
    c->led = 1;
    c->dim = 1;
}

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

float thermo_set_max(const thermo_cfg_t *c)
{
    return c->mode == MODE_HEAT ? c->limit_hi - c->band : SET_MAX;     // off point stays under the limit
}

float thermo_set_min(const thermo_cfg_t *c)
{
    return c->mode == MODE_COOL ? c->limit_lo + c->band : SET_MIN;
}

void thermo_cfg_clamp(thermo_cfg_t *c)
{
    if (c->mode >= MODES) c->mode = MODE_OFF;
    c->units = c->units ? 1 : 0;
    c->band = clampf(c->band, 0.1f, 5.0f);
    if (c->min_on_s > 900) c->min_on_s = 900;
    if (c->min_off_s > 900) c->min_off_s = 900;
    c->offset = clampf(c->offset, -5.0f, 5.0f);
    c->limit_hi = clampf(c->limit_hi, 0.0f, 120.0f);
    c->limit_lo = clampf(c->limit_lo, -50.0f, 40.0f);
    if (c->norise_min > 480) c->norise_min = 480;
    c->relay_low = c->relay_low ? 1 : 0;
    c->led = c->led ? 1 : 0;
    if (c->dim > 3) c->dim = 1;
    if (c->theme > 4) c->theme = 0;
    c->sim = c->sim ? 1 : 0;
    c->set = clampf(c->set, thermo_set_min(c), thermo_set_max(c));
    c->set = clampf(c->set, SET_MIN, SET_MAX);
}

void thermo_init(thermo_t *th)
{
    memset(th, 0, sizeof *th);
    th->last_mode = -1;
}

void thermo_mark_boot(thermo_t *th) { th->acc_flags |= HF_BOOT; }

void thermo_clear_alarm(thermo_t *th)
{
    th->norise = 0;
    th->run_t0 = th->t;
    th->run_since = th->up;
}

float thermo_on_at(const thermo_cfg_t *c)
{
    return c->mode == MODE_COOL ? c->set + c->band * 0.5f : c->set - c->band * 0.5f;
}

float thermo_off_at(const thermo_cfg_t *c)
{
    return c->mode == MODE_COOL ? c->set - c->band * 0.5f : c->set + c->band * 0.5f;
}

// least squares slope over the last few trend samples (10 s apart), C per hour
static void trend(thermo_t *th)
{
    int n = th->tr_n;
    if (n < 12) { th->have_rate = 0; return; }        // under 2 minutes it is just noise
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
        float y = th->tr[(th->tr_head - n + i + TREND_N) % TREND_N];
        float x = (float)i;
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    float d = n * sxx - sx * sx;
    th->rate = d > 0 ? (n * sxy - sx * sy) / d * 360.0f : 0.0f;    // 360 samples an hour
    th->have_rate = 1;
}

static void push_minute(thermo_t *th, const thermo_cfg_t *c)
{
    hpoint_t p;
    p.t = th->acc_n ? (int16_t)lroundf(th->acc_t / (float)th->acc_n * 100.0f) : HIST_NONE;
    p.set = (int16_t)lroundf(c->set * 100.0f);
    p.on = (uint8_t)th->acc_on;
    p.flags = (uint8_t)th->acc_flags;
    th->h[th->h_head] = p;
    th->h_head = (th->h_head + 1) % HIST_N;
    if (th->h_n < HIST_N) th->h_n++;
    th->acc_t = 0;
    th->acc_n = th->acc_on = th->acc_s = th->acc_flags = 0;
}

void thermo_tick(thermo_t *th, const thermo_cfg_t *c, int ok, float probe_c)
{
    th->up++;

    /* 1. the probe. DS18B20 steps 1/16 C, a little smoothing stops the last digit flickering.
     * a jump (offset changed, probe moved to another place) is taken as it is */
    if (ok) {
        float v = probe_c + c->offset;
        th->t = th->have_t && fabsf(v - th->t) < 2.0f ? th->t + 0.35f * (v - th->t) : v;
        th->have_t = 1;
        th->bad = 0;
    } else if (++th->bad >= 3) {
        th->have_t = 0;                               // one or two lost reads happen, three is a missing probe
    }
    if (c->mode != th->last_mode) {                   // new mode starts from "nothing wanted"
        th->want = 0;
        th->last_mode = c->mode;
    }

    /* 2. no-rise watch: the relay runs, the temperature must move the right way. a probe that
     * fell off the floor or out of the water reads the room and the heater never stops */
    if (th->relay && th->have_t && c->norise_min) {
        /* progress counts from the lowest point of the run (highest when cooling), not from the
         * start: the probe lags, so the room keeps falling a few minutes after the heater comes
         * on, and an open window drops it further. from the start the heater could need more
         * than the hour to make up for that, and the alarm went off on a working heater */
        if (c->mode == MODE_COOL ? th->t > th->run_t0 : th->t < th->run_t0) th->run_t0 = th->t;
        float gain = c->mode == MODE_COOL ? th->run_t0 - th->t : th->t - th->run_t0;
        if (gain >= 0.5f) { th->run_t0 = th->t; th->run_since = th->up; }
        else if (th->up - th->run_since >= (uint32_t)c->norise_min * 60u) th->norise = 1;
    }

    /* 3. faults, the worst one wins. the limit lets go 1 C back inside */
    int f = FAULT_NONE;
    if (!th->have_t) f = FAULT_PROBE;
    else if (th->norise) f = FAULT_NORISE;
    else if (c->mode == MODE_HEAT && (th->t >= c->limit_hi || (th->fault == FAULT_LIMIT && th->t > c->limit_hi - 1.0f)))
        f = FAULT_LIMIT;
    else if (c->mode == MODE_COOL && (th->t <= c->limit_lo || (th->fault == FAULT_LIMIT && th->t < c->limit_lo + 1.0f)))
        f = FAULT_LIMIT;
    th->fault = f;

    /* 4. what the temperature asks for. inside the band: whatever it asked last */
    float lo = c->set - c->band * 0.5f, hi = c->set + c->band * 0.5f;
    if (f || c->mode == MODE_OFF) th->want = 0;
    else if (c->mode == MODE_HEAT) { if (th->t <= lo) th->want = 1; else if (th->t >= hi) th->want = 0; }
    else { if (th->t >= hi) th->want = 1; else if (th->t <= lo) th->want = 0; }

    /* 5. the relay follows, unless its minimum on / off time is not over. safety and OFF don't wait */
    uint32_t in_state = th->up - th->switched;
    int next = th->relay;
    th->hold_s = 0;
    if (th->want != th->relay) {
        if (!th->want && (f || c->mode == MODE_OFF)) next = 0;
        else {
            uint32_t need = th->relay ? c->min_on_s : c->min_off_s;
            if (in_state >= need) next = th->want;
            else th->hold_s = (int)(need - in_state);
        }
    }
    if (next != th->relay) {
        th->relay = next;
        th->switched = th->up;
        if (next) {
            th->starts++;
            th->run_t0 = th->t;
            th->run_since = th->up;
            if ((th->acc_flags & HF_STARTS) < 3) th->acc_flags++;
        }
    }

    /* 6. trend, every 10 s */
    if (th->up % 10u == 0) {
        if (th->have_t) {
            th->tr[th->tr_head] = th->t;
            th->tr_head = (th->tr_head + 1) % TREND_N;
            if (th->tr_n < TREND_N) th->tr_n++;
        } else {
            th->tr_n = 0;
        }
        trend(th);
    }

    /* 7. history, a point a minute */
    if (th->have_t) { th->acc_t += th->t; th->acc_n++; }
    th->acc_set = c->set;
    th->acc_on += th->relay;
    if (f) th->acc_flags |= HF_FAULT;
    if (c->mode == MODE_HEAT) th->acc_flags |= HF_HEAT;
    if (c->mode == MODE_COOL) th->acc_flags |= HF_COOL;
    if (c->sim) th->acc_flags |= HF_SIM;
    if (++th->acc_s >= 60) push_minute(th, c);
}

int thermo_point(const thermo_t *th, int age, hpoint_t *out)
{
    if (age == 0) {
        if (!th->acc_s) return 0;
        out->t = th->acc_n ? (int16_t)lroundf(th->acc_t / (float)th->acc_n * 100.0f) : HIST_NONE;
        out->set = (int16_t)lroundf(th->acc_set * 100.0f);
        out->on = (uint8_t)(th->acc_on * 60 / th->acc_s);
        out->flags = (uint8_t)th->acc_flags;
        return 1;
    }
    if (age > th->h_n) return 0;
    *out = th->h[(th->h_head - age + HIST_N) % HIST_N];
    return 1;
}

void thermo_stats(const thermo_t *th, int minutes, tstats_t *s)
{
    memset(s, 0, sizeof *s);
    float sum = 0, on = 0;
    int starts = 0;
    for (int age = 1; age <= minutes; age++) {        // full minutes only, the running one would skew it
        hpoint_t p;
        if (!thermo_point(th, age, &p)) break;
        s->minutes++;
        on += p.on;
        starts += p.flags & HF_STARTS;
        if (p.t == HIST_NONE) continue;
        float t = p.t * 0.01f;
        if (!s->n || t < s->lo) s->lo = t;
        if (!s->n || t > s->hi) s->hi = t;
        sum += t;
        s->n++;
    }
    if (s->n) s->avg = sum / (float)s->n;
    if (s->minutes) {
        s->duty = on / (60.0f * (float)s->minutes);
        s->starts_h = (float)starts * 60.0f / (float)s->minutes;
    }
}

/* --- history in flash: magic, head, count, the points --- */
#define HIST_MAGIC 0x31485448u   // "THH1"

int thermo_hist_size(void) { return 12 + (int)sizeof(hpoint_t) * HIST_N; }

void thermo_hist_pack(const thermo_t *th, uint8_t *buf)
{
    uint32_t hdr[3] = { HIST_MAGIC, (uint32_t)th->h_head, (uint32_t)th->h_n };
    memcpy(buf, hdr, sizeof hdr);
    memcpy(buf + sizeof hdr, th->h, sizeof th->h);
}

int thermo_hist_unpack(thermo_t *th, const uint8_t *buf, int len)
{
    uint32_t hdr[3];
    if (len != thermo_hist_size()) return 0;
    memcpy(hdr, buf, sizeof hdr);
    if (hdr[0] != HIST_MAGIC || hdr[1] >= HIST_N || hdr[2] > HIST_N) return 0;
    th->h_head = (int)hdr[1];
    th->h_n = (int)hdr[2];
    memcpy(th->h, buf + sizeof hdr, sizeof th->h);
    return 1;
}
