// the aircraft list, see sky.h
#include "sky.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define D2R (3.14159265358979f / 180.0f)

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
        place(s, &s->p[i]);
    }
}

void plane_now(const plane_t *p, uint32_t now, float *x, float *y)
{
    float dt = (float)(int32_t)(now - p->seen) / 1000.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 20.0f) dt = 20.0f;                      // no further than 20 s on a guess
    float km = p->has_spd && !p->ground ? p->gs * 1.852f / 3600.0f * dt : 0.0f;
    *x = p->x + km * sinf(p->track * D2R);
    *y = p->y + km * cosf(p->track * D2R);
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
    if (p->icao == in->icao) {                         // known: the old position goes to the trail
        float dx = x - p->x, dy = y - p->y;
        if (dx * dx + dy * dy > 900.0f) p->tn = 0;    // jumped 30 km: lost and found again, the old trail would cross the map
        else if (dx * dx + dy * dy > 0.04f) {         // moved more than 200 m
            if (p->tn == TRAIL_N) { memmove(p->tx, p->tx + 1, sizeof p->tx[0] * (TRAIL_N - 1)); memmove(p->ty, p->ty + 1, sizeof p->ty[0] * (TRAIL_N - 1)); p->tn--; }
            p->tx[p->tn] = (int16_t)lroundf(p->x * 100.0f);
            p->ty[p->tn] = (int16_t)lroundf(p->y * 100.0f);
            p->tn++;
        }
    }
    int16_t tx[TRAIL_N], ty[TRAIL_N];
    int known = p->icao == in->icao;
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
}

static void fr24_field(plane_t *p, const char *k, const jv_t *v, int *have, float *age)
{
    (void)age;
    if (!strcmp(k, "hex") && v->is_str) p->icao = hex24(v->s);
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
static int parse_plane(const char *obj, int len, int fr24, uint32_t now, plane_t *p)
{
    jc_t c = { obj + 1, obj + len };
    memset(p, 0, sizeof *p);
    int have = 0;
    float age = 0;
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
        if (fr24) fr24_field(p, key, &v, &have, &age);
        else adsbx_field(p, key, &v, &have, &age);
    }
    if (have != 3 || !p->icao) return 0;               // lat and lon both there
    p->seen = now - (uint32_t)(age * 1000.0f);
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
    r->fr24 = fr24;
    r->home_lat = s->home_lat;
    r->home_lon = s->home_lon;
    r->hide_ground = s->hide_ground;
    r->now = now;
}

/* a character machine: strings and their escapes, the depth of {}[]. the list starts at the
 * '[' after the right name at the top level, each '{' at depth 2 is a plane, copied out till
 * its '}' and read then */
void sky_read(sky_reader_t *r, const char *data, int len)
{
    const char *want = r->fr24 ? "data" : "ac", *also = r->fr24 ? "data" : "aircraft";
    for (int i = 0; i < len && r->state < 2; i++) {
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
                if (!r->too_long && parse_plane(r->obj, r->objn, r->fr24, r->now, &p)) keep(r, &p);
            } else if (r->state == 1 && r->depth == 1) {
                r->state = 2;                          // the list is over, the rest is not needed
            }
        }
    }
}

int sky_read_end(sky_reader_t *r, sky_t *s)
{
    if (!r->state) return -1;
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
    for (int i = 0; i < s->n;) {
        if (now - s->p[i].heard > SKY_FORGET) s->p[i] = s->p[--s->n];
        else i++;
    }
}
