#include "store.h"
#include <stddef.h>
#include <string.h>
#include "nvs.h"

#define NS "flight"        // nvs namespace

static int load_blob(const char *ns, const char *key, void *p, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t got = len;
    esp_err_t e = nvs_get_blob(h, key, p, &got);
    nvs_close(h);
    return e == ESP_OK && got == len;
}

static void save_blob(const char *key, const void *p, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, key, p, len);
    nvs_commit(h);
    nvs_close(h);
}

static void load_str(nvs_handle_t h, const char *key, char *out, size_t cap)
{
    size_t len = cap;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) out[0] = 0;
}

/* the settings as version 2 kept them, before the looks, the map and the country. a board
 * that ran it keeps its home, range and colours */
typedef struct {
    uint16_t version;
    uint8_t  source, server, dist_unit, alt_unit, flip, theme, labels;
    int16_t  up;
    float    lat, lon;
    char     place[20];
    float    range;
} cfg_v2_t;

int store_load_cfg(fr_cfg_t *c)
{
    fr_cfg_t t;
    if (load_blob(NS, "cfg", &t, sizeof t) && t.version == FR_CFG_VERSION) {
        *c = t;
        return 1;
    }
    /* version 3 is this one up to `ground`: its 100 bytes are read over the defaults, and a board
     * that ran it keeps all of its settings */
    _Static_assert(offsetof(fr_cfg_t, ground) == 100, "version 3 settings are the first 100 bytes");
    fr_cfg_defaults(&t);
    if (load_blob(NS, "cfg", &t, offsetof(fr_cfg_t, ground)) && t.version == 3) {
        t.version = FR_CFG_VERSION;
        *c = t;
        return 1;
    }
    cfg_v2_t o;
    if (!load_blob(NS, "cfg", &o, sizeof o) || o.version != 2) return 0;
    fr_cfg_defaults(c);
    c->source = o.source;
    c->server = o.server;
    c->dist_unit = o.dist_unit;
    c->alt_unit = o.alt_unit;
    c->flip = o.flip;
    c->theme = o.theme;
    c->labels = o.labels;
    c->up = o.up;
    c->lat = o.lat;
    c->lon = o.lon;
    memcpy(c->place, o.place, sizeof o.place);
    c->place[sizeof o.place - 1] = 0;
    c->country[0] = 0;                                   // the board asks OSM for it
    c->range = o.range;
    return 1;
}

void store_save_cfg(const fr_cfg_t *c) { save_blob("cfg", c, sizeof *c); }

void store_load_secrets(secrets_t *s)
{
    memset(s, 0, sizeof *s);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    load_str(h, "ssid", s->ssid, sizeof s->ssid);
    load_str(h, "pass", s->pass, sizeof s->pass);
    load_str(h, "key", s->key, sizeof s->key);
    nvs_close(h);
}

void store_save_secrets(const secrets_t *s)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", s->ssid);
    nvs_set_str(h, "pass", s->pass);
    nvs_set_str(h, "key", s->key);
    nvs_commit(h);
    nvs_close(h);
}

/* the first network stays in "ssid" and "pass", where every version looks for it: a board that
 * goes back to an older one keeps its WiFi. the others are one blob, the empty places in it too */
void store_load_nets(const secrets_t *s, nets_t *l)
{
    memset(l, 0, sizeof *l);
    if (!s->ssid[0]) return;
    memcpy(l->n[0].ssid, s->ssid, sizeof l->n[0].ssid);
    memcpy(l->n[0].pass, s->pass, sizeof l->n[0].pass);
    load_blob(NS, "nets", &l->n[1], sizeof l->n - sizeof l->n[0]);
    l->count = 1;
    while (l->count < NETS_KEPT && l->n[l->count].ssid[0]) l->count++;
}

void store_save_nets(const nets_t *l) { save_blob("nets", &l->n[1], sizeof l->n - sizeof l->n[0]); }

/* a board that ran the WiFi radar or the thermostat already knows its touch: same chip, same
 * panel. so without an alignment of its own it takes theirs */
int store_load_cal(touch_cal_t *cal)
{
    return load_blob(NS, "cal", cal, sizeof *cal) || load_blob("radarscan", "cal", cal, sizeof *cal) ||
           load_blob("thermo", "cal", cal, sizeof *cal);
}

void store_save_cal(const touch_cal_t *cal) { save_blob("cal", cal, sizeof *cal); }

int store_load_screen(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        if (nvs_open("radarscan", NVS_READONLY, &h) != ESP_OK) return -1;   // the radar may know the screen too
    }
    esp_err_t e = nvs_get_u8(h, "screen", &v);
    nvs_close(h);
    return e == ESP_OK ? v : -1;
}

void store_save_screen(int profile)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "screen", (uint8_t)profile);
    nvs_commit(h);
    nvs_close(h);
}

void store_load_watch(char *text, int cap)
{
    text[0] = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    load_str(h, "watch", text, (size_t)cap);
    nvs_close(h);
}

void store_save_watch(const char *text)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "watch", text);
    nvs_commit(h);
    nvs_close(h);
}

void store_load_light(light_cfg_t *c)
{
    if (load_blob(NS, "light", c, sizeof *c) && c->version == LIGHT_CFG_VERSION) return;
    /* version 2 is this one up to `red`: read over the defaults, the red one stays on GPIO 4 */
    _Static_assert(offsetof(light_cfg_t, red) == 10, "version 2 LED settings are the first 10 bytes");
    light_defaults(c);
    if (load_blob(NS, "light", c, offsetof(light_cfg_t, red)) && c->version == 2) {
        c->version = LIGHT_CFG_VERSION;
        c->red = LIGHT_RED_4;
        return;
    }
    light_defaults(c);
}

void store_save_light(const light_cfg_t *c) { save_blob("light", c, sizeof *c); }

void store_load_opt(scope_opt_t *o)
{
    if (load_blob(NS, "scope", o, sizeof *o) && o->version == SCOPE_OPT_VERSION) return;
    scope_opt_defaults(o);
}

void store_save_opt(const scope_opt_t *o) { save_blob("scope", o, sizeof *o); }

int store_load_help(void)
{
    uint8_t v = 0;
    return load_blob(NS, "help", &v, sizeof v) && v <= 2 ? v : 0;
}

void store_save_help(int help)
{
    uint8_t v = (uint8_t)help;
    save_blob("help", &v, sizeof v);
}

int store_load_bare(void)
{
    uint8_t v = 0;
    return load_blob(NS, "bare", &v, sizeof v) && v == 1;
}

void store_save_bare(int bare)
{
    uint8_t v = bare ? 1 : 0;
    save_blob("bare", &v, sizeof v);
}

uint32_t store_load_ran(void)
{
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    nvs_get_u32(h, "ran", &v);
    nvs_close(h);
    return v;
}

void store_save_ran(uint32_t s)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "ran", s);
    nvs_commit(h);
    nvs_close(h);
}
