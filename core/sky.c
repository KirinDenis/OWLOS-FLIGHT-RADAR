// the aircraft list, see sky.h
#include "sky.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define D2R (3.14159265358979f / 180.0f)
#define GLIDE_M  25.0f                                 /* metres in a unit of plane_t gx, gy */
/* a guess follows the curve 5 s at most and 90 degrees at most, then it goes straight on. the turn
 * comes from two positions 5 to 20 s old, and a real turn is often over by the time it is known:
 * replayed on 12 minutes of real sky round a busy airport, 10 s of curve turned the noses half as
 * much again as the planes turned, 25 s nearly twice; 5 s turned them as much as they turned, and
 * still kept them on the curve better than none */
#define ARC_S    5.0f
#define TURN_MAX 90.0f

/* the guess's four bytes went into what was padding: the sky, the reader and the screen keep 48
 * planes each, and on the classic ESP32 every byte of them is RAM the map wants */
_Static_assert(sizeof(plane_t) <= 172, "plane_t grew");

void geo_offset(float lat0, float lon0, float lat, float lon, float *east, float *north)
{
    float dlon = lon - lon0;
    if (dlon > 180.0f) dlon -= 360.0f;
    if (dlon < -180.0f) dlon += 360.0f;
    *east = dlon * 111.320f * cosf((lat0 + lat) * 0.5f * D2R);
    *north = (lat - lat0) * 110.574f;
}

void sky_init(sky_t *s, float lat, float lon)
{
    memset(s, 0, sizeof *s);
    s->home_lat = lat;
    s->home_lon = lon;
    s->status = SKY_WAIT;
}

static void place(const sky_t *s, plane_t *p)
{
    geo_offset(s->home_lat, s->home_lon, p->lat, p->lon, &p->x, &p->y);
    p->dist = sqrtf(p->x * p->x + p->y * p->y);
    float b = atan2f(p->x, p->y) / D2R;
    p->brg = b < 0 ? b + 360.0f : b;
}

// parked, taxiing, or a car of the airport's own
static int on_ground(const plane_t *p) { return p->ground || p->kind == KIND_GROUND; }

static void drop(sky_t *s, uint32_t icao)
{
    for (int i = 0; i < s->n; i++) if (s->p[i].icao == icao) { s->p[i] = s->p[--s->n]; return; }
}

void sky_hide_ground(sky_t *s, int hide)
{
    s->hide_ground = (uint8_t)(hide != 0);
    if (!hide) return;
    for (int i = 0; i < s->n;) {
        if (on_ground(&s->p[i])) s->p[i] = s->p[--s->n];
        else i++;
    }
}

void sky_home(sky_t *s, float lat, float lon)
{
    s->home_lat = lat;
    s->home_lon = lon;
    for (int i = 0; i < s->n; i++) {
        s->p[i].tn = 0;                              // the trail was measured from the old home
        s->p[i].gx = s->p[i].gy = 0;                 // and so was the slide
        place(s, &s->p[i]);
    }
}

// seconds from its position to `now`, as far as a guess goes
static float guess_dt(const sky_t *s, const plane_t *p, uint32_t now)
{
    float dt = (float)(int32_t)(now - p->seen) / 1000.0f;
    float most = (float)(s->fr24 ? SKY_GUESS_FR24 : SKY_GUESS) / 1000.0f;
    if (dt < -20.0f) dt = -20.0f;                    // a moment before that position: worked back the same way
    if (dt > most) dt = most;                        // no further on a guess
    return dt;
}

// degrees its track turns in dt seconds: ARC_S of them at most, and no more than TURN_MAX either way
static float turned(const plane_t *p, float dt)
{
    float t = dt > ARC_S ? ARC_S : dt < -ARC_S ? -ARC_S : dt;
    float a = p->turn * 0.1f * t;
    return a > TURN_MAX ? TURN_MAX : a < -TURN_MAX ? -TURN_MAX : a;
}

/* how much of a slide (gx gy, gh) is still to go at `now`: 1 when that position came, 0 when it is over.
 * all of it before it came: the beam looks back. it takes SKY_GLIDE, or longer when the slide is long
 * for how fast the plane flies: never faster than the plane itself at its fastest, the middle, or
 * a light one circling slid half a kilometre sideways in two seconds, four times its own speed */
#define SLIDE_MAX_MS 12000.0f
static float slide_left(const plane_t *p, uint32_t now)
{
    int32_t since = (int32_t)(now - p->heard);
    if (since <= 0) {
        return 1.0f;
    }
    if (since >= (int32_t)SLIDE_MAX_MS) {
        return 0.0f;
    }
    float ms = (float)SKY_GLIDE;
    if (p->gx || p->gy) {
        float v = p->has_spd && p->gs > 20.0f ? p->gs * (1852.0f / 3600.0f) : 10.0f;   // m a second
        float m = sqrtf((float)(p->gx * p->gx + p->gy * p->gy)) * GLIDE_M;
        float want = 1500.0f * m / v;                  // the eased slide is 1.5 times its mean speed at the middle
        if (want > ms) ms = want < SLIDE_MAX_MS ? want : SLIDE_MAX_MS;
    }
    if (since >= (int32_t)ms) {
        return 0.0f;
    }
    float f = (float)since / ms;
    return 1.0f - f * f * (3.0f - 2.0f * f);         // eased at both ends: no kick as it starts or as it lands
}

void plane_now(const sky_t *s, const plane_t *p, uint32_t now, float *x, float *y)
{
    float dt = guess_dt(s, p, now);
    float v = p->has_spd && !p->ground ? p->gs * 1.852f / 3600.0f : 0.0f;   // km a second
    if (!p->turn) {                                  // straight on, worked out as it always was
        float km = v * dt;
        *x = p->x + km * sinf(p->track * D2R);
        *y = p->y + km * cosf(p->track * D2R);
    } else {
        /* round the curve: its chord, v t sin(h)/h long with h half the angle turned, along the
         * track half way round. past ARC_S or TURN_MAX straight on from where the curve ended */
        float a = turned(p, dt);
        float on = a / (p->turn * 0.1f);             // seconds on the curve: dt, or till ARC_S or TURN_MAX
        float h = 0.5f * a * D2R;
        float chord = v * on * (fabsf(h) > 1e-4f ? sinf(h) / h : 1.0f);
        float mid = (p->track + 0.5f * a) * D2R, end = (p->track + a) * D2R;
        float rest = v * (dt - on);
        *x = p->x + chord * sinf(mid) + rest * sinf(end);
        *y = p->y + chord * cosf(mid) + rest * cosf(end);
    }
    if (p->gx || p->gy) {                            // sliding from where the guess had it when this position came
        float k = slide_left(p, now);
        *x += p->gx * (GLIDE_M / 1000.0f) * k;
        *y += p->gy * (GLIDE_M / 1000.0f) * k;
    }
}

/* asked by every strip the plane is drawn in: kept cheap, no fmodf. the track is 0..360, a guess turns
 * it 90 degrees at most and the nose's slide 127: one step back into 0..360 is enough */
float plane_track(const sky_t *s, const plane_t *p, uint32_t now)
{
    if (!p->turn && !p->gh) return p->track;
    float t = p->track;
    if (p->turn) t += turned(p, guess_dt(s, p, now));
    if (p->gh) t += p->gh * slide_left(p, now);
    return t >= 360.0f ? t - 360.0f : t < 0.0f ? t + 360.0f : t;
}

const char *kind_name(int kind)
{
    static const char *N[KINDS] = { "JET", "LIGHT", "BIZJET", "HEAVY", "FAST", "HELICOPTER",
                                    "GLIDER", "BALLOON", "DRONE", "GROUND" };
    return kind >= 0 && kind < KINDS ? N[kind] : "?";
}

/* --- a small JSON walker. it reads in place, nothing is allocated: on a board without PSRAM
 * the answer is the biggest thing in memory already --- */

typedef struct { const char *p, *end; } jc_t;

static void ws(jc_t *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\n' || *c->p == '\r' || *c->p == '\t')) c->p++;
}

static int skip_value(jc_t *c);

// a string at c->p (on the opening quote). copies at most cap-1 chars into out (may be NULL)
static int read_str(jc_t *c, char *out, int cap)
{
    if (c->p >= c->end || *c->p != '"') return 0;
    c->p++;
    int n = 0;
    while (c->p < c->end && *c->p != '"') {
        char ch = *c->p++;
        if (ch == '\\' && c->p < c->end) {
            ch = *c->p++;
            if (ch == 'u') { c->p += 4; ch = '?'; }  // nothing we show needs more than ASCII
            else if (ch == 'n' || ch == 't' || ch == 'r') ch = ' ';
        }
        if (out && n < cap - 1) out[n++] = ch;
    }
    if (out && cap) out[n] = 0;
    if (c->p < c->end) c->p++;
    return 1;
}

static int skip_value(jc_t *c)
{
    ws(c);
    if (c->p >= c->end) return 0;
    char ch = *c->p;
    if (ch == '"') return read_str(c, NULL, 0);
    if (ch == '{' || ch == '[') {
        int depth = 0;
        while (c->p < c->end) {
            ch = *c->p;
            if (ch == '"') { read_str(c, NULL, 0); continue; }
            if (ch == '{' || ch == '[') depth++;
            if (ch == '}' || ch == ']') { depth--; if (!depth) { c->p++; return 1; } }
            c->p++;
        }
        return 0;
    }
    while (c->p < c->end && *c->p != ',' && *c->p != '}' && *c->p != ']') c->p++;   // number, true, false, null
    return 1;
}

typedef struct {
    int  is_str, is_num;
    char s[24];
    double num;
} jv_t;

static int read_value(jc_t *c, jv_t *v)
{
    ws(c);
    memset(v, 0, sizeof *v);
    if (c->p >= c->end) return 0;
    if (*c->p == '"') { v->is_str = 1; return read_str(c, v->s, sizeof v->s); }
    if (*c->p == '-' || (*c->p >= '0' && *c->p <= '9')) {
        char *e;
        v->num = strtod(c->p, &e);
        c->p = e > c->end ? c->end : e;
        v->is_num = 1;
        return 1;
    }
    return skip_value(c);                              // object, array, true, false, null: not wanted
}

static void trim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
}

static uint8_t kind_of_category(const char *c)
{
    if (c[0] == 'A') {
        switch (c[1]) {
        case '1': return KIND_LIGHT;
        case '2': return KIND_BIZJET;
        case '5': return KIND_HEAVY;
        case '6': return KIND_FAST;
        case '7': return KIND_HELI;
        default:  return KIND_JET;
        }
    }
    if (c[0] == 'B') {
        switch (c[1]) {
        case '1': return KIND_GLIDER;
        case '2': return KIND_BALLOON;
        case '4': return KIND_LIGHT;                 // ultralight
        case '6': return KIND_DRONE;
        default:  return KIND_LIGHT;
        }
    }
    if (c[0] == 'C') return KIND_GROUND;
    return KIND_JET;
}

// FR24 gives no category: the type says enough for a silhouette
static uint8_t kind_of_type(const char *t)
{
    static const char *HEAVY[] = { "B74", "B77", "B78", "B76", "A33", "A34", "A35", "A38", "IL9", "MD1", "C5", "C17", "A40" };
    static const char *HELI[] = { "EC20", "EC25", "EC30", "EC35", "EC45", "EC55", "EC75", "AS32", "AS35", "AS50", "AS55", "AS65",
                                  "R22", "R44", "R66", "H160", "B06", "B407", "B412", "B429", "A109", "A119", "A139", "A169",
                                  "A189", "S76", "S92", "MI8", "NH90" };
    for (size_t i = 0; i < sizeof HEAVY / sizeof HEAVY[0]; i++) if (!strncmp(t, HEAVY[i], strlen(HEAVY[i]))) return KIND_HEAVY;
    for (size_t i = 0; i < sizeof HELI / sizeof HELI[0]; i++) if (!strncmp(t, HELI[i], strlen(HELI[i]))) return KIND_HELI;
    if (t[0] == 'C' && t[1] >= '1' && t[1] <= '2') return KIND_LIGHT;   // C150, C172, C182..
    if (!strncmp(t, "P28", 3) || !strncmp(t, "SR2", 3) || !strncmp(t, "DA4", 3)) return KIND_LIGHT;
    return KIND_JET;
}

static plane_t *slot(sky_t *s, uint32_t icao, float dist)
{
    for (int i = 0; i < s->n; i++) if (s->p[i].icao == icao) return &s->p[i];
    if (s->n < SKY_MAX) { memset(&s->p[s->n], 0, sizeof s->p[0]); return &s->p[s->n++]; }
    int far = 0;                                       // full: a nearer one takes the farthest one's place
    for (int i = 1; i < s->n; i++) if (s->p[i].dist > s->p[far].dist) far = i;
    if (dist >= s->p[far].dist) return NULL;
    memset(&s->p[far], 0, sizeof s->p[0]);
    return &s->p[far];
}

// a plane read from either format, merged into the list
static void merge(sky_t *s, const plane_t *in, uint32_t now)
{
    float x, y;
    geo_offset(s->home_lat, s->home_lon, in->lat, in->lon, &x, &y);
    plane_t *p = slot(s, in->icao, sqrtf(x * x + y * y));
    if (!p) return;
    int known = p->icao == in->icao;
    /* what it had: where it is drawn now and which way it points, and its track and time, for how fast it turns */
    float was_x = 0.0f, was_y = 0.0f, was_track = p->track, was_nose = 0.0f;
    uint32_t was_seen = p->seen;
    int was_flying = known && p->has_spd && !p->ground;
    int8_t was_turn = p->turn;
    /* a guess that ran out stood still on the screen: one that moves again jumps to where it is */
    int stood = known && (int32_t)(now - p->seen) > (int32_t)(s->fr24 ? SKY_GUESS_FR24 : SKY_GUESS);
    if (known) {                                       // the old position goes to the trail
        plane_now(s, p, now, &was_x, &was_y);
        was_nose = plane_track(s, p, now);
        float dx = x - p->x, dy = y - p->y;
        if (dx * dx + dy * dy > 900.0f) p->tn = 0;    // jumped 30 km: lost and found again, the old trail would cross the map
        else if (fabsf(p->x) > 327.0f || fabsf(p->y) > 327.0f) p->tn = 0;   // past what 10 m units hold in 16 bits: a point there
                                                       // wrapped to the other side and its line crossed the scope (NM, MI: 250 is 463 km)
        else if (dx * dx + dy * dy > 0.04f) {         // moved more than 200 m
            if (p->tn == TRAIL_N) { memmove(p->tx, p->tx + 1, sizeof p->tx[0] * (TRAIL_N - 1)); memmove(p->ty, p->ty + 1, sizeof p->ty[0] * (TRAIL_N - 1)); p->tn--; }
            p->tx[p->tn] = (int16_t)lroundf(p->x * 100.0f);
            p->ty[p->tn] = (int16_t)lroundf(p->y * 100.0f);
            p->tn++;
        }
    }
    int16_t tx[TRAIL_N], ty[TRAIL_N];
    uint8_t tn = known ? p->tn : 0;
    uint32_t first = known ? p->first : now;
    memcpy(tx, p->tx, sizeof tx);
    memcpy(ty, p->ty, sizeof ty);
    *p = *in;
    memcpy(p->tx, tx, sizeof tx);
    memcpy(p->ty, ty, sizeof ty);
    p->tn = tn;
    p->first = first;
    p->heard = now;
    place(s, p);
    p->turn = 0;
    p->gx = p->gy = p->gh = 0;
    if (!known) return;
    if (was_flying && p->has_spd && !p->ground) {
        int32_t ms = (int32_t)(p->seen - was_seen);
        if (ms < 2000) {
            p->turn = was_turn;                        // the same position again, or hardly a newer one: turning as it was
        } else if (ms <= 60000) {                      // how far round its track went since the last one, a second
            float d = fmodf(p->track - was_track + 540.0f, 360.0f) - 180.0f;
            float rate = d * 1000.0f / (float)ms;
            /* over 12.7 a second no aeroplane turns: a track that came wrong, or not one flight */
            if (fabsf(rate) >= 0.25f && fabsf(rate) <= 12.7f) p->turn = (int8_t)lroundf(rate * 10.0f);
        }
    }
    /* from where it was drawn it slides onto where this position puts it, and does not jump there */
    float nx, ny;
    plane_now(s, p, now, &nx, &ny);
    float gx = (was_x - nx) * (1000.0f / GLIDE_M), gy = (was_y - ny) * (1000.0f / GLIDE_M);
    if (stood || fabsf(gx) >= 127.5f || fabsf(gy) >= 127.5f) {
        p->turn = 0;                                   // stood still, or over 3 km off the guess: lost and found, not one turn. it jumps
        return;
    }
    p->gx = (int8_t)lroundf(gx);
    p->gy = (int8_t)lroundf(gy);
    /* and its nose turns from where it pointed to where this position points it, as smoothly */
    float d = was_nose - plane_track(s, p, now);
    d = d > 180.0f ? d - 360.0f : d < -180.0f ? d + 360.0f : d;
    p->gh = (int8_t)lroundf(d > 127.0f ? 127.0f : d < -127.0f ? -127.0f : d);
}

static uint32_t hex24(const char *h)
{
    uint32_t v = 0;
    for (; *h; h++) {
        char ch = *h;
        if (ch == '~') continue;                       // ADSBx marks non-ICAO addresses with ~
        v = v * 16 + (uint32_t)(ch >= 'a' ? ch - 'a' + 10 : ch >= 'A' ? ch - 'A' + 10 : ch - '0');
    }
    return v & 0xFFFFFF;
}

// as much of src as fits, always ended
static void copy(char *dst, int cap, const char *src)
{
    int n = 0;
    while (src[n] && n < cap - 1) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

// a direction as 1..360: 0 is left to mean not heard
static uint16_t degrees(double v)
{
    int d = (int)lround(v) % 360;
    return (uint16_t)(d <= 0 ? d + 360 : d);
}

static void adsbx_field(plane_t *p, const char *k, const jv_t *v, int *have, float *age)
{
    if (!strcmp(k, "hex") && v->is_str) p->icao = hex24(v->s);
    else if (!strcmp(k, "flight") && v->is_str) copy(p->flight, sizeof p->flight, v->s);
    else if (!strcmp(k, "r") && v->is_str) copy(p->reg, sizeof p->reg, v->s);
    else if (!strcmp(k, "t") && v->is_str) copy(p->type, sizeof p->type, v->s);
    else if (!strcmp(k, "category") && v->is_str) p->kind = kind_of_category(v->s);
    else if (!strcmp(k, "lat") && v->is_num) { p->lat = (float)v->num; *have |= 1; }
    else if (!strcmp(k, "lon") && v->is_num) { p->lon = (float)v->num; *have |= 2; }
    else if (!strcmp(k, "alt_baro")) {
        if (v->is_num) { p->alt = (int32_t)v->num; p->has_alt = 1; }
        else if (v->is_str && !strcmp(v->s, "ground")) { p->ground = 1; p->alt = 0; p->has_alt = 1; }
    }
    else if (!strcmp(k, "alt_geom") && v->is_num && !p->has_alt) { p->alt = (int32_t)v->num; p->has_alt = 1; }
    else if (!strcmp(k, "gs") && v->is_num) { p->gs = (float)v->num; p->has_spd = 1; }
    else if (!strcmp(k, "track") && v->is_num) p->track = (float)v->num;
    else if (!strcmp(k, "baro_rate") && v->is_num) p->vrate = (int16_t)v->num;
    else if (!strcmp(k, "geom_rate") && v->is_num && !p->vrate) p->vrate = (int16_t)v->num;
    else if (!strcmp(k, "squawk") && v->is_str) p->squawk = (uint16_t)atoi(v->s);
    else if (!strcmp(k, "seen_pos") && v->is_num) *age = (float)v->num;
    else if (!strcmp(k, "nav_altitude_mcp") && v->is_num) p->sel_fl = (int16_t)lround(v->num / 100.0);   // 32992 comes for 33000
    else if (!strcmp(k, "nav_heading") && v->is_num) p->sel_hdg = degrees(v->num);
    else if (!strcmp(k, "ias") && v->is_num) p->ias = (uint16_t)lround(v->num);
    else if (!strcmp(k, "mach") && v->is_num) p->mach = (uint16_t)lround(v->num * 1000.0);
    else if (!strcmp(k, "wd") && v->is_num) p->wind_dir = degrees(v->num);
    else if (!strcmp(k, "ws") && v->is_num) p->wind_kt = (uint8_t)(v->num > 255.0 ? 255 : lround(v->num));
}

/* "2024-01-15T10:30:00Z" as seconds since 2000-01-01, 0 when it is not that. what follows the
 * seconds (a fraction, Z, an offset) is left: only how far apart two of one answer are counts */
static uint32_t iso_seconds(const char *t)
{
    static const char SEP[] = "--T::";
    int f[6];
    for (int i = 0; i < 6; i++) {
        int v = 0, digits = 0;
        while (*t >= '0' && *t <= '9' && digits < 4) { v = v * 10 + (*t++ - '0'); digits++; }
        if (!digits) return 0;
        f[i] = v;
        if (i < 5) {
            if (*t != SEP[i]) return 0;
            t++;
        }
    }
    int y = f[0], m = f[1], d = f[2];
    if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31 || f[3] > 23 || f[4] > 59 || f[5] > 60) return 0;
    y -= m <= 2;                                       // the days from the calendar, as H. Hinnant's days_from_civil
    int era = y / 400, yoe = y - era * 400;
    int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    uint32_t days = (uint32_t)(era * 146097 + doe - 730425);   // 730425: 0000-03-01 to 2000-01-01
    return days * 86400u + (uint32_t)(f[3] * 3600 + f[4] * 60 + f[5]);
}

static void fr24_field(plane_t *p, const char *k, const jv_t *v, int *have, uint32_t *fix)
{
    if (!strcmp(k, "hex") && v->is_str) p->icao = hex24(v->s);
    else if (!strcmp(k, "timestamp") && v->is_str) *fix = iso_seconds(v->s);   // when this position was fixed
    else if (!strcmp(k, "callsign") && v->is_str) copy(p->flight, sizeof p->flight, v->s);
    else if (!strcmp(k, "flight") && v->is_str && !p->flight[0]) copy(p->flight, sizeof p->flight, v->s);
    else if (!strcmp(k, "reg") && v->is_str) copy(p->reg, sizeof p->reg, v->s);
    else if (!strcmp(k, "type") && v->is_str) { copy(p->type, sizeof p->type, v->s); p->kind = kind_of_type(p->type); }
    else if (!strcmp(k, "orig_iata") && v->is_str) copy(p->orig, sizeof p->orig, v->s);
    else if (!strcmp(k, "dest_iata") && v->is_str) copy(p->dest, sizeof p->dest, v->s);
    else if (!strcmp(k, "lat") && v->is_num) { p->lat = (float)v->num; *have |= 1; }
    else if (!strcmp(k, "lon") && v->is_num) { p->lon = (float)v->num; *have |= 2; }
    else if (!strcmp(k, "alt") && v->is_num) { p->alt = (int32_t)v->num; p->has_alt = 1; p->ground = p->alt <= 0; }
    else if (!strcmp(k, "gspeed") && v->is_num) { p->gs = (float)v->num; p->has_spd = 1; }
    else if (!strcmp(k, "track") && v->is_num) p->track = (float)v->num;
    else if (!strcmp(k, "vspeed") && v->is_num) p->vrate = (int16_t)v->num;
    else if (!strcmp(k, "squawk") && v->is_str) p->squawk = (uint16_t)atoi(v->s);
}

/* --- reading an answer as it comes --- */

// one plane's object, whole, from '{' to '}'. 1 if it had a position
static int parse_plane(sky_reader_t *r, plane_t *p)
{
    jc_t c = { r->obj + 1, r->obj + r->objn };
    memset(p, 0, sizeof *p);
    int have = 0;
    float age = 0;
    uint32_t fix = 0;
    char key[24];
    for (;;) {
        ws(&c);
        if (c.p >= c.end || *c.p == '}') break;
        if (*c.p == ',') { c.p++; continue; }
        if (!read_str(&c, key, sizeof key)) return 0;
        ws(&c);
        if (c.p >= c.end || *c.p != ':') return 0;
        c.p++;
        jv_t v;
        if (!read_value(&c, &v)) return 0;
        if (r->fr24) fr24_field(p, key, &v, &have, &fix);
        else adsbx_field(p, key, &v, &have, &age);
    }
    if (have != 3 || !p->icao) return 0;               // lat and lon both there
    if (fix) {                                         // FR24: how much older than the answer's first position, s
        if (!r->fix0) r->fix0 = fix;
        int32_t d = (int32_t)(r->fix0 - fix);
        age = (float)(d < -600 ? -600 : d > 600 ? 600 : d);   // younger ones less than 0: sky_read_end sets them right
    }
    p->seen = age >= 0.0f ? r->now - (uint32_t)(age * 1000.0f) : r->now + (uint32_t)(-age * 1000.0f);
    trim(p->flight);
    return 1;
}

// a plane of the answer: kept if it is among the nearest SKY_MAX so far
static void keep(sky_reader_t *r, const plane_t *p)
{
    float x, y;
    geo_offset(r->home_lat, r->home_lon, p->lat, p->lon, &x, &y);
    float d = x * x + y * y;
    // one on the ground left out is kept only to say it has landed: every plane in the air goes
    // before it. 1e9 km squared is further than anywhere on Earth
    if (r->hide_ground && on_ground(p)) d += 1e9f;
    int at = r->n;
    if (r->n == SKY_MAX) {                             // full: it takes the farthest one's place
        at = 0;
        for (int i = 1; i < r->n; i++) if (r->got[i].dist > r->got[at].dist) at = i;
        if (d >= r->got[at].dist) return;
    } else {
        r->n++;
    }
    r->got[at] = *p;
    r->got[at].dist = d;                               // squared, only for this choosing
}

void sky_read_begin(sky_reader_t *r, const sky_t *s, int fr24, uint32_t now)
{
    r->state = r->depth = r->in_str = r->esc = r->keyn = 0;
    memset(r->key, 0, sizeof r->key);
    r->objn = r->too_long = r->n = r->total = 0;
    r->fr24 = r->fr24_pace = fr24;
    r->home_lat = s->home_lat;
    r->home_lon = s->home_lon;
    r->hide_ground = s->hide_ground;
    r->now = r->asked = now;
    r->fix0 = 0;
    r->made = 0;
    r->made_have = r->numn = 0;
}

/* "now" as the free networks write it, the server's clock: ms since 1970 (adsb.lol, adsb.fi's v3) or s with
 * a fraction (adsb.fi's v2). its low 32 bits: only how far apart two of its times are counts. 0: not that */
static int server_ms(const char *t, uint32_t *ms)
{
    uint64_t v = 0;
    uint32_t f = 0;
    int digits = 0, point = 0, fd = 0;
    for (; *t; t++) {
        if (*t == '.' && !point) {
            point = 1;
        } else if (*t < '0' || *t > '9') {
            return 0;
        } else if (!point) {
            if (++digits > 15) return 0;
            v = v * 10u + (uint64_t)(*t - '0');
        } else if (fd < 3) {
            f = f * 10u + (uint32_t)(*t - '0');
            fd++;
        }
    }
    if (!digits) return 0;
    if (point || v < 100000000000ull) {                // seconds: a fraction, or too few digits for ms
        for (; fd < 3; fd++) f *= 10u;
        v = v * 1000u + f;
    }
    *ms = (uint32_t)v;
    return 1;
}

/* a character machine: strings and their escapes, the depth of {}[]. the list starts at the
 * '[' after the right name at the top level, each '{' at depth 2 is a plane, copied out till
 * its '}' and read then. the top level is read to the end: "now" may come after the list */
void sky_read(sky_reader_t *r, const char *data, int len)
{
    const char *want = r->fr24 ? "data" : "ac", *also = r->fr24 ? "data" : "aircraft";
    for (int i = 0; i < len; i++) {
        char ch = data[i];
        int in_plane = r->state == 1 && r->depth >= 3;
        if (in_plane) {
            if (r->objn < SKY_OBJ - 1) r->obj[r->objn++] = ch;
            else r->too_long = 1;
        }
        if (r->in_str) {
            if (r->esc) r->esc = 0;
            else if (ch == '\\') r->esc = 1;
            else if (ch == '"') r->in_str = 0;
            else if (r->depth == 1 && r->keyn < (int)sizeof r->key - 1) r->key[r->keyn++] = ch;
            continue;
        }
        if (r->depth == 1 && !r->made_have && !strcmp(r->key, "now")) {   // the server's clock, a number
            if ((ch >= '0' && ch <= '9') || ch == '.') {
                if (r->numn < (int)sizeof r->num - 1) r->num[r->numn++] = ch;
                continue;
            }
            if (r->numn) {
                r->num[r->numn] = 0;
                r->made_have = server_ms(r->num, &r->made);
                r->numn = 0;
            }
        }
        if (ch == '"') {
            r->in_str = 1;
            if (r->depth == 1) { r->keyn = 0; memset(r->key, 0, sizeof r->key); }
            continue;
        }
        if (ch == '{' || ch == '[') {
            if (r->state == 0 && r->depth == 1 && ch == '[' && (!strcmp(r->key, want) || !strcmp(r->key, also))) r->state = 1;
            r->depth++;
            if (r->state == 1 && r->depth == 3 && ch == '{') {   // a plane begins
                r->obj[0] = ch;
                r->objn = 1;
                r->too_long = 0;
            }
        } else if (ch == '}' || ch == ']') {
            r->depth--;
            if (r->state == 1 && r->depth == 2 && ch == '}') {   // and ends
                plane_t p;
                r->obj[r->objn] = 0;                   // strtod reads up to something that is not a digit
                r->total++;
                if (!r->too_long && parse_plane(r, &p)) keep(r, &p);
            } else if (r->state == 1 && r->depth == 1) {
                r->state = 2;                          // the list is over: only "now" may be wanted from the rest
            }
        }
    }
}

int sky_read_end(sky_reader_t *r, sky_t *s)
{
    if (!r->state) return -1;
    /* the positions were read as if the answer was made when the question went out. it was made by the
     * server's clock at `made`: the free networks say it ("now"), FR24's is its newest position, each of
     * its positions as much older than that as their times say. without them every position counted as
     * that new, and one that was older jumped ahead with the next answer */
    int32_t newest = 0;                                // FR24: the newest position, ms after the question
    int dated = 0;
    uint32_t made = 0;
    if (r->fr24 && r->fix0 && r->n) {
        newest = INT32_MIN;
        for (int i = 0; i < r->n; i++) {
            int32_t d = (int32_t)(r->got[i].seen - r->asked);
            if (d > newest) newest = d;
        }
        made = r->fix0 * 1000u + (uint32_t)newest;     // its time on FR24's clock, ms (low 32 bits)
        dated = 1;
    } else if (!r->fr24 && r->made_have) {
        made = r->made;
        dated = 1;
    }
    int32_t shift = -newest;                           // no clock: FR24's newest taken as fixed when it was asked
    if (dated) {
        /* our clock less the server's, and the time the answer took: the least of it is the truest, the
         * answer that came the quickest. 1 ms more an answer for the two clocks' drift. one more than a
         * minute off is another server, or a clock set again */
        uint32_t off = r->now - made;
        int32_t d = (int32_t)(off - s->clk);
        if (!s->clk_have || d > 60000 || d < -60000) s->clk = off;
        else s->clk = d < 1 ? off : s->clk + 1u;
        s->clk_have = 1;
        int32_t at = (int32_t)(made + s->clk - r->asked);   // when it was made, our ms after the question
        if (at > -30000 && at <= (int32_t)(r->now - r->asked) + 1000) shift = at - newest;
    }
    if (shift) {
        for (int i = 0; i < r->n; i++) r->got[i].seen += (uint32_t)shift;
    }
    s->fr24 = (uint8_t)(r->fr24_pace != 0);
    for (int i = 0; i < r->n; i++) {
        if (r->hide_ground && on_ground(&r->got[i])) drop(s, r->got[i].icao);   // landed: gone now, not SKY_FORGET later
        else merge(s, &r->got[i], r->now);
    }
    s->total = r->total;                               // cut short: what was read stays
    s->updated = r->now;
    s->status = SKY_OK;
    s->note[0] = 0;
    return r->n;
}

static int parse_all(sky_t *s, const char *json, int len, int fr24, uint32_t now)
{
    static sky_reader_t r;                             // 10 KB, too much for a task's stack
    sky_read_begin(&r, s, fr24, now);
    sky_read(&r, json, len);
    return sky_read_end(&r, s);
}

int sky_parse_adsbx(sky_t *s, const char *json, int len, uint32_t now) { return parse_all(s, json, len, 0, now); }
int sky_parse_fr24(sky_t *s, const char *json, int len, uint32_t now) { return parse_all(s, json, len, 1, now); }

void sky_forget(sky_t *s, uint32_t now)
{
    uint32_t keep = s->fr24 ? SKY_FORGET_FR24 : SKY_FORGET;
    for (int i = 0; i < s->n;) {
        if (now - s->p[i].heard > keep) s->p[i] = s->p[--s->n];
        else i++;
    }
}
