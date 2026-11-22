/*
 * OWLOS Flight Radar - the aircraft around home on the ViewOwl screens (core/config.h).
 * core 0: WiFi, the fetch task (asks the server every few seconds, merges the answer into the
 *         sky), the console (settings from the site over the USB line)
 * core 1: the screen task - touch and BOOT in, copy the sky, draw the frame in strips
 * before the site sent any settings it shows the demo sky, so the board is never blank.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"
#include "console.h"
#include "demo.h"
#include "display.h"
#include "font.h"
#include "gfx.h"
#include "led.h"
#include "logbuf.h"
#include "busy.h"
volatile uint32_t g_busy;                              // TEMP (busy.h): here, as every build sets the bits
#include "net.h"
#include "map.h"
#include "qr_site.h"
#include "sky.h"
#include "store.h"
#include "sun.h"
#include "tiles.h"
#include "touch.h"
#include "ui.h"
#include "wx_parse.h"
#if RADAR_OVERLAY
#include "ovl.h"
#endif

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// FLIGHT_VERSION comes from CHANGES.md (firmware/CMakeLists.txt), and FLIGHT_BETA when its line says (beta)
#ifndef FLIGHT_BETA
#define FLIGHT_BETA 0
#endif
#define DEMO_BYTES     6144            /* the demo's answer: 12 planes */
#define FREE_EVERY_MS  5000u           /* the free networks allow a request a second */
#define FR24_EVERY_MS  20000u          /* FR24 counts credits: slower */

static const char *TAG = "flight";

static CORE_BIG sky_t    s_sky;        // written by the fetch task, under s_lock
static demo_t            s_demo;
static fr_cfg_t          s_cfg;        // the settings in use: the screen changes them, the site too
static secrets_t         s_sec;
static nets_t            s_nets;       // the networks it remembers, the first is the one in s_sec
static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_fetch;
static CORE_BIG ui_t     s_ui;
static volatile int      s_cfg_new;    // the site sent settings: the screen takes s_cfg
static volatile int      s_skip_align; // the page said skip the touch alignment
static volatile int      s_align_told = -1;   // what the page last heard of it, -1: tell it again
static char              s_server[24];
static int               s_http;
static uint32_t          s_fetched;
static volatile uint32_t s_wifi_try;   // when WiFi settings last came, from the site or the screen
static volatile int      s_demo_on;    // the demo sky is up: no WiFi set, or the one set is not there
static volatile int      s_screen = -1, s_screen_set = -1;   // the 2.8" CYD's screen revision: now, and what the site asks
static char              s_home_ask[64];   // a home typed on the screen, for the fetch task to find. "": none
static char              s_said[40];       // what the fetch task tells the screen: a home found, a watched plane
static uint32_t          s_said_ms;        // for how long
static volatile int      s_said_new;
static watchlist_t       s_watch;          // the planes to follow (core/watch.h), under s_lock
static volatile uint32_t s_watch_pick;     // one of them just came in range: the screen picks it
static uint32_t          s_watch_asked;    // ms of the last lookup of one, 0: ask now
static light_t           s_light;          // what happened, for the LED (core/light.h), under s_lock
static light_cfg_t       s_light_cfg;      // what lights it: the screen's and the site's, under s_lock
static scope_opt_t       s_opt;            // the radar's options: the gear's and the site's, under s_lock
static volatile int      s_opt_new;        // the site sent them: the screen takes them
static volatile int      s_light_new;      // the site sent LED settings: the screen takes them
static volatile int      s_light_test;     // the site asked for the LED's red, green, blue
static char              s_newest[12];     // the version the site has (update_check), "": not heard. under s_lock

/* how the last run ended, for a board found standing still: did it start again (and why), or
 * did the picture stop with the board running? the uptime goes to NVS once a minute (store.h):
 * RTC memory lost it when the USB line's reset pulled EN, the site's port may do that. the boot
 * log and GET say it */
static uint32_t           s_ran_s;     // how long the run before this one was, s, to a minute
static uint32_t           s_ran_kept;  // the uptime last saved, s
static const char        *s_reset;     // why it ended

static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_SW:       return "restart";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default:               return "other";
    }
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static uint32_t now_us(void) { return (uint32_t)esp_timer_get_time(); }
/* (chipguyhere, 2026-11-14) what the ui task waits for, for the 10 s log: the lock (how long, and who held
 * it), and what the fetch task was doing meanwhile (FP_*, set as it goes). to find out whether the lags that
 * come every few seconds are the network's */
enum { FP_IDLE, FP_HOME, FP_GEOCODE, FP_MAP, FP_HTTP, FP_MERGE, FP_WATCH, FP_UPDATE, FP_ALIVE, FP_OTHER, FP_N };
static volatile uint8_t  s_fp = FP_OTHER;             // the fetch task's phase now
static TaskHandle_t      s_ui_task;
#if RADAR_OVERLAY
static const char *const FP_NAME[FP_N] = { "idle", "home", "geocode", "map", "http", "merge", "watch", "update", "alive", "other" };
static const char *volatile s_lock_by;                // the task holding the lock, NULL none
static volatile uint8_t  s_lock_fp;                   // the fetch task's phase when it was taken
static uint32_t          s_lw_max_us, s_lw_sum_us, s_lw_n;   // the ui task's waits for it
static const char       *s_lw_by = "";
static uint8_t           s_lw_fp;

static void lock(void)
{
    if (xTaskGetCurrentTaskHandle() != s_ui_task || !s_ui_task) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    } else if (xSemaphoreTake(s_lock, 0) != pdTRUE) {  // held: how long, and by whom
        const char *by = s_lock_by;
        const uint8_t fp = s_lock_fp;
        const int64_t t0 = esp_timer_get_time();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const uint32_t w = (uint32_t)(esp_timer_get_time() - t0);
        s_lw_sum_us += w;
        s_lw_n++;
        if (w > s_lw_max_us) { s_lw_max_us = w; s_lw_by = by ? by : "?"; s_lw_fp = fp; }
    }
    s_lock_by = pcTaskGetName(NULL);
    s_lock_fp = s_fp;
}
static void unlock(void) { s_lock_by = NULL; xSemaphoreGive(s_lock); }
#else
/* the other boards: the lock as it was, and no pacing line in the 10 s log */
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
#endif

/* --- asking the server --- */

/* the open networks: no key, a request a second at most. latitude, longitude, radius in NM.
 * airplanes.live and adsb.one used to be here: both now answer 403 to anyone not let in */
static const char *const FREE_URL[FREE_SERVERS] = {
    "https://api.adsb.lol/v2/point/%.2f/%.2f/%d",
    "https://opendata.adsb.fi/api/v3/lat/%.2f/lon/%.2f/dist/%d",
};
static const char *const FREE_NAME[FREE_SERVERS] = { "ADSB.LOL", "ADSB.FI" };

/* the server learns where to look, not where you live: home goes out rounded to 0.01 degree,
 * about a kilometre. the scope keeps the exact point, the answer reaches 1.6 times past its rim */
static float vague(float deg) { return roundf(deg * 100.0f) / 100.0f; }

// a bit more than the scope, so planes on their way in show on the rim first. the flat square
// reaches further, into its corners
static float ask_km(const fr_cfg_t *c) { return range_km(c) * (!RADAR_ROUND && c->look != LOOK_RADAR ? 2.4f : 1.6f); }

static void free_url(const fr_cfg_t *c, int server, char *url, int cap)
{
    int nm = (int)ceilf(ask_km(c) / 1.852f);
    if (nm > 250) nm = 250;                           // the most they answer
    snprintf(url, (size_t)cap, FREE_URL[server], (double)vague(c->lat), (double)vague(c->lon), nm);
}

static void fr24_url(const fr_cfg_t *c, char *url, int cap)
{
    float lat = vague(c->lat), lon = vague(c->lon);
    float r = ask_km(c), dlat = r / 110.574f, dlon = r / (111.320f * cosf(lat * 3.14159265f / 180.0f));
    snprintf(url, (size_t)cap, "https://fr24api.flightradar24.com/api/live/flight-positions/full?bounds=%.2f,%.2f,%.2f,%.2f",
             (double)(lat + dlat), (double)(lat - dlat), (double)(lon - dlon), (double)(lon + dlon));
}

static void to_reader(void *ctx, const char *data, int len) { sky_read((sky_reader_t *)ctx, data, len); }

/* --- the ground under the scope: OpenStreetMap's tiles, downloaded once into the flash
 * (tiles.c) and read into a picture of the scope's size (core/map.c) when home or the range
 * changes. the screen task draws it, this one makes it: while it is being made it is not ready,
 * and the scope shows the glass alone for that moment --- */

/* the map's pieces: the IRAM the code leaves free first. it takes 32 bit words only, map.c
 * reads and writes nothing smaller, and TLS and the WiFi cannot use it at all */
static void *iram_first(size_t bytes)
{
#if CONFIG_IDF_TARGET_ESP32
    return heap_caps_malloc(bytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
#elif CONFIG_SPIRAM
    /* every board with PSRAM: the 5" and the Tab5 have no room for its 190 KB in the RAM, the C5's
     * strips and AES want the RAM, and the S3 2.8" boards gave it to malloc, so their RAM held it too.
     * no PSRAM found after all: NULL, and map.c takes malloc's */
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#else
    (void)bytes;
    return NULL;                                       // the C3's RAM is one: malloc does it
#endif
}

static CORE_BIG map_t      s_map;
static CORE_BIG map_plan_t s_plan;
static int        s_map_read;          // tiles that went into s_map
static uint32_t   s_map_done;          // and which of the plan's, a bit each
static uint32_t   s_map_try;           // when it last asked for missing ones
static float      s_fail_kpx;          // a new picture that could not be made: its scale, and when, so it is
static uint32_t   s_fail_at;           // asked for again 15 s on and not in every round
_Static_assert(MAP_TILES_MAX <= 32, "a bit a tile in s_map_done");

static void map_service(const fr_cfg_t *c)
{
    if (!c->map) return;
    const float kpx = ui_ground_kpx(c);
    int w, h, disc;
    ui_ground_size(c, &w, &h, &disc);                       // the look sets its size: the whole screen or the disc
    int same = s_map.ready && s_map.lat == c->lat && s_map.lon == c->lon && s_map.kpx == kpx && s_map.w == w && s_map.h == h;
    if (same && (s_map_read == s_plan.ntiles || now_ms() - s_map_try < 15000u)) return;
    /* a new picture that could not be made last time (none of its tiles, or no memory for it): asked
     * for again 15 s on. each try costs TLS handshakes, and on the 3.5" every 5 s of them cut the
     * memory into pieces the tiles then could not use */
    if (!same && s_fail_at && s_fail_kpx == kpx && now_ms() - s_fail_at < 15000u) return;
    if (!same) map_plan(&s_plan, c->lat, c->lon, kpx, w, h, disc);   // a retry asks for the same tiles again
    s_map_try = now_ms();
    int kept = tiles_fetch(s_plan.tiles, s_plan.ntiles);      // the missing ones, when the WiFi is up
    uint32_t low_fetch = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    if (same && kept <= s_map_read) return;                   // nothing new to show
    /* a new range or turn with none of its tiles here yet: the map shown stays, the scope scales it by
     * its own kpx, and the plan goes back to it, so s_map_done is still its. wiped first, the map went
     * from the screen and stayed away while the new tiles could not come */
    if (!same && !kept && s_map.ready && s_map.w == w && s_map.h == h && s_map.lat == c->lat && s_map.lon == c->lon) {
        map_plan(&s_plan, s_map.lat, s_map.lon, s_map.kpx, s_map.w, s_map.h, s_map.disc);
        s_fail_kpx = kpx;
        s_fail_at = now_ms() | 1u;
        return;
    }
    int64_t t0 = esp_timer_get_time();
    /* a retry for the same plan reads only the tiles still missing, into the texels as they are.
     * it used to wipe them and read every tile again: with the memory short just then, a map that
     * showed went and stayed away till a restart */
    if (!same) {
        if (!map_alloc(&s_map, w, h, disc)) {
            ESP_LOGE(TAG, "map: no memory for %d x %d texels", w, h);
            s_fail_kpx = kpx;
            s_fail_at = now_ms() | 1u;
            return;
        }
        map_begin(&s_map);
        s_map_done = 0;
    }
    s_fail_at = 0;
    int read = 0, oom = 0, bad = 0;
    for (int k = 0; k < s_plan.ntiles; k++) {
        tile_src_t src;
        if (!(s_map_done & 1u << k) && tiles_open(&s_plan.tiles[k], &src)) {
            int r = map_tile(&s_map, &s_plan, k, tiles_read, &src);
            if (r == PNG_OK) s_map_done |= 1u << k;
            oom |= r == PNG_NO_MEMORY;
            if (r == PNG_BAD) {                                // kept but not readable: it was asked for again
                tiles_drop(&s_plan.tiles[k]);                  // never, and the map kept its hole for good
                bad++;
            }
        }
        read += (s_map_done >> k) & 1;
    }
    map_end(&s_map, &s_plan);
    if (oom) ESP_LOGW(TAG, "map: no memory to read a tile in, heap %u, largest %u",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (bad) ESP_LOGW(TAG, "map: %d kept tiles not readable, dropped, asked for again", bad);
    s_map_read = read;
    ESP_LOGI(TAG, "map: zoom %d, %d of %d tiles, made in %d ms, heap %u, at least %u after the tiles came, %u after reading them",
             s_plan.z, read, s_plan.ntiles, (int)((esp_timer_get_time() - t0) / 1000), (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)low_fetch, (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

/* --- home's town and country, when the site sent none: OpenStreetMap's Nominatim, asked once a
 * home with the same rounded point the plane servers get --- */

typedef struct { char *b; int n, cap; } text_t;

/* Nominatim's question and answer, for geocode() and the home search: both run on the fetch task,
 * one at a time. the ESP32's RAM is short, one pair is kept. and the same bytes are map.c's working
 * rows while map_service reads a tile, on the same task, never at the same time: from the heap, beside
 * the PNG's 32 KB window, they cost the 2.8" a third of its maps (map_scratch, core/map.h) */
static CORE_BIG union {
    struct { char url[420], body[2048]; } osm;
    uint8_t  map[MAP_SCRATCH];
    uint32_t align;                                    // the map's rows hold 16 bit words
} s_fetch_buf;
#define s_osm_url s_fetch_buf.osm.url
#define s_osm     s_fetch_buf.osm.body

static void to_text(void *ctx, const char *data, int len)
{
    text_t *t = ctx;
    if (len > t->cap - 1 - t->n) len = t->cap - 1 - t->n;
    memcpy(t->b + t->n, data, (size_t)len);
    t->n += len;
    t->b[t->n] = 0;
}

// the most a place can be called, from a city down to a county: the first one there
static const char *const PLACE_KIND[] = { "city", "town", "village", "municipality", "hamlet", "county", "state" };

/* home's country as OpenStreetMap has it, its two letters, "" till Nominatim said. at a home in Rusia the
 * board asks for no planes and no map. OSM puts Crimea and the occupied towns in Ukraine: asked 2026-11-10,
 * Simferopol, Sevastopol, Kerch, Donetsk, Luhansk, Mariupol and Melitopol all "ua", Narva "ee", Ivangorod
 * and Belgorod "ru". Nominatim silent: the board goes on as ever, nobody else is held up by it */
static char s_home_cc[3];

static int home_refused(void)
{
    lock();
    int r = !strcmp(s_home_cc, "ru");
    unlock();
    return r;
}

/* asked once a home, after power on too, with the same rounded point the plane servers get. a home
 * Nominatim did not answer about is asked again five minutes on */
static void geocode(void)
{
    static float asked_lat = 999, asked_lon = 999;
    static uint32_t asked_at;
    fr_cfg_t c;
    char cc[sizeof s_home_cc];
    lock();
    c = s_cfg;
    memcpy(cc, s_home_cc, sizeof cc);
    unlock();
    const int same_home = c.lat == asked_lat && c.lon == asked_lon;
    if (!net_up() || (same_home && (cc[0] || now_ms() - asked_at < 300000u))) return;
    asked_lat = c.lat;
    asked_lon = c.lon;
    asked_at = now_ms();
    snprintf(s_osm_url, sizeof s_osm_url, "https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=10&accept-language=en&lat=%.2f&lon=%.2f",
             (double)vague(c.lat), (double)vague(c.lon));
    const char *buf = s_osm;
    text_t t = { s_osm, 0, sizeof s_osm };
    s_osm[0] = 0;
    int status = 0;
    net_get(s_osm_url, NULL, to_text, &t, &status);
    char place[sizeof c.place] = "", country[sizeof c.country] = "", code[sizeof s_home_cc] = "";
    for (int i = 0; i < (int)(sizeof PLACE_KIND / sizeof PLACE_KIND[0]) && !place[0]; i++) json_str(buf, PLACE_KIND[i], place, sizeof place);
    json_str(buf, "country", country, sizeof country);
    json_str(buf, "country_code", code, sizeof code);
    ESP_LOGI(TAG, "where home is: HTTP %d, \"%s\", \"%s\", %s", status, place, country, code[0] ? code : "no code");
    if (status != 200 || (!place[0] && !country[0] && !code[0])) return;
    int named = 0;
    lock();
    if (s_cfg.lat == c.lat && s_cfg.lon == c.lon) {           // still the same home
        memcpy(s_home_cc, code, sizeof code);
        if (!s_cfg.place[0] && place[0]) { memcpy(s_cfg.place, place, sizeof place); named = 1; }
        if (!s_cfg.country[0] && country[0]) { memcpy(s_cfg.country, country, sizeof country); named = 1; }
        if (named) s_cfg_new = 1;
        c = s_cfg;
    }
    unlock();
    if (named) store_save_cfg(&c);                     // saved only when a name came that it had not
}

/* --- a home typed on the screen. "48.9 18.1" is taken as it is, no WiFi needed. a name or a
 * postcode is asked of Nominatim's search, and its first answer taken: a town's centre --- */

// "48.9 18.1" or "48.9, 18.1": a point, 1. a postcode or a name: 0
static int as_point(const char *s, float *lat, float *lon)
{
    char *end;
    double a = strtod(s, &end);
    if (end == s) return 0;
    while (*end == ' ' || *end == ',') end++;
    const char *at = end;
    double b = strtod(at, &end);
    if (end == at) return 0;
    while (*end == ' ') end++;
    if (*end || a < -90 || a > 90 || b < -180 || b > 180) return 0;
    *lat = (float)a;
    *lon = (float)b;
    return 1;
}

// the words for a URL: letters and digits as they are, the rest as %XX
static void url_words(const char *s, char *out, int cap)
{
    static const char HEX[] = "0123456789ABCDEF";
    int n = 0;
    for (; *s && n < cap - 4; s++) {
        unsigned char ch = (unsigned char)*s;
        if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '-' || ch == '.') {
            out[n++] = (char)ch;
        } else {
            out[n++] = '%';
            out[n++] = HEX[ch >> 4];
            out[n++] = HEX[ch & 15];
        }
    }
    out[n] = 0;
}

// the new home, as the site's SET makes it. no place: geocode() asks OSM for its name
static void home_set(float lat, float lon, const char *place, const char *country)
{
    fr_cfg_t c;
    lock();
    if (s_cfg.lat != lat || s_cfg.lon != lon) s_home_cc[0] = 0;   // another home: its country, geocode() asks
    s_cfg.lat = lat;
    s_cfg.lon = lon;
    snprintf(s_cfg.place, sizeof s_cfg.place, "%s", place);
    snprintf(s_cfg.country, sizeof s_cfg.country, "%s", country);
    sky_home(&s_sky, lat, lon);
    c = s_cfg;
    s_cfg_new = 1;
    unlock();
    store_save_cfg(&c);
}

/* a line for the screen's toast, s_lock held. wider than the screen: the shorter one, the rest
 * is on SET */
static void say_locked(const char *text, const char *shorter, uint32_t ms)
{
    if (snprintf(s_said, sizeof s_said, "%s", text) >= (int)sizeof s_said || font_width(&FONT_MEDIUM, s_said) > GFX_W - 24) {
        snprintf(s_said, sizeof s_said, "%s", shorter);
    }
    s_said_ms = ms;
    s_said_new = 1;
}

static void home_say(const char *text)
{
    lock();
    say_locked(text, "HOME FOUND", 4000u);
    unlock();
}

// one search of Nominatim: its first answer into s_osm, the HTTP status back
static int search(const char *q)
{
    int n = snprintf(s_osm_url, sizeof s_osm_url,
                     "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&addressdetails=1&accept-language=en&q=");
    url_words(q, s_osm_url + n, (int)sizeof s_osm_url - n);
    text_t t = { s_osm, 0, sizeof s_osm };
    s_osm[0] = 0;
    int status = 0;
    net_get(s_osm_url, NULL, to_text, &t, &status);
    return status;
}

static void home_find(void)
{
    char ask[sizeof s_home_ask], here[sizeof s_cfg.country];
    lock();
    memcpy(ask, s_home_ask, sizeof ask);
    s_home_ask[0] = 0;
    memcpy(here, s_cfg.country, sizeof here);
    unlock();
    if (!ask[0]) return;
    float lat, lon;
    char said[sizeof s_said + 8];
    if (as_point(ask, &lat, &lon)) {
        home_set(lat, lon, "", "");
        snprintf(said, sizeof said, "HOME %.4f %.4f", (double)lat, (double)lon);
        home_say(said);
        ESP_LOGI(TAG, "home from the screen: a point");
        return;
    }
    if (!net_up()) { home_say("NO WIFI: TYPE LAT LON"); return; }
    const char *buf = s_osm;
    char slat[24] = "", slon[24] = "", place[sizeof s_cfg.place] = "", country[sizeof s_cfg.country] = "";
    int status = 0;
    /* a postcode is the same number in many countries: 10115 alone is Santo Domingo. one with a
     * digit in it is looked for in home's country first ("91101, Slovakia"), then anywhere. a town
     * is looked for as it is: "Bratislava, United Kingdom" finds a house of that name */
    if (strpbrk(ask, "0123456789") && here[0]) {
        char q[sizeof ask + sizeof here + 2];
        snprintf(q, sizeof q, "%s, %s", ask, here);
        status = search(q);
        json_str(buf, "lat", slat, sizeof slat);
        if (!slat[0]) vTaskDelay(pdMS_TO_TICKS(1100));    // Nominatim takes a request a second
    }
    if (!slat[0]) {
        status = search(ask);
        json_str(buf, "lat", slat, sizeof slat);
    }
    json_str(buf, "lon", slon, sizeof slon);
    ESP_LOGI(TAG, "home from the screen: HTTP %d, %s", status, slat[0] ? "found" : "not found");
    if (status != 200) { home_say("NO ANSWER: TRY AGAIN"); return; }
    if (!slat[0] || !slon[0]) { home_say("NOT FOUND: TRY ANOTHER"); return; }
    for (int i = 0; i < (int)(sizeof PLACE_KIND / sizeof PLACE_KIND[0]) && !place[0]; i++) json_str(buf, PLACE_KIND[i], place, sizeof place);
    if (!place[0]) json_str(buf, "name", place, sizeof place);
    json_str(buf, "country", country, sizeof country);
    home_set(strtof(slat, NULL), strtof(slon, NULL), place, country);
    snprintf(said, sizeof said, "HOME: %s", place[0] ? place : country);
    home_say(said);
}

/* --- a newer version? the site publishes the one it flashes in version.json (the first "## " line
 * of CHANGES.md, .github/workflows/pages.yml). asked once the WiFi is up, then twice a day. the
 * screen says it with a small note and ABOUT (core/ui_about.c), GET with "newest" --- */

#define UPDATE_URL      SITE_URL "version.json"
#define UPDATE_EVERY_MS (12u * 3600u * 1000u)
#define UPDATE_RETRY_MS (30u * 60u * 1000u)   /* no answer, or not one: again in half an hour */

/* --- counted on the site's map of boards, only when the owner allowed it (SET, COUNT ME, or the site's
 * question): every 10 minutes "this build, this version", nothing else. where to is in the site's
 * version.json, "stats": it says none, nothing is sent. server/ in the repository is that server --- */
#define ALIVE_EVERY_MS (10u * 60u * 1000u)
static uint8_t           s_help;           // 0 never asked, 1 allowed, 2 not (store_load_help). under s_lock
static volatile int      s_help_new;       // the site changed it: the screen takes it
static char              s_stats_url[64];  // from version.json, "": none. under s_lock
/* the radar without its panel, the scope in the middle (its three dots, or the site): kept like help */
static uint8_t           s_bare;           // store_load_bare. under s_lock
static volatile int      s_bare_new;       // the site changed it: the screen takes it
/* the WEATHER look's page from the console (the site, a test rig): not kept, the look comes on its dial */
static uint8_t           s_wxpage;         // under s_lock
static volatile int      s_wxpage_new;     // asked for: the screen takes it
/* the screen's light, SET's BACKLIGHT or the site: a step of UI_BACKLIGHTS (core/ui.h), kept like help */
static uint8_t           s_backlight;      // store_load_backlight. under s_lock
static volatile int      s_backlight_new;  // the site changed it: the screen takes it
/* SET's NIGHT or the site: 0 off, else the step the light goes down to while the sun is down at home */
static uint8_t           s_night;          // store_load_night. under s_lock
static volatile int      s_night_new;      // the site changed it: the screen takes it
/* the weather's warnings shown, SET's ALERT rows or the site: 1 << WXA_* (core/weather.h), kept like help */
static uint8_t           s_alerts;         // store_load_alerts. under s_lock
static volatile int      s_alerts_new;     // the site changed it: the screen takes it
/* the weather's units, SET's UNITS rows or the site: each 0 home's country's own (core/ui.h), kept like help */
static wx_units_t        s_units;          // store_load_units. under s_lock
static volatile int      s_units_new;      // the site changed them: the screen takes them
/* the map's names on the dark looks, SET's MAP NAMES or the site: NAMES_* (core/ui.h), kept like help */
static uint8_t           s_names;          // store_load_names. under s_lock
static volatile int      s_names_new;      // the site changed it: the screen takes it

static const char *board_name(void);

static void alive_report(void)
{
    static uint32_t sent;                              // ms of the last report, 0: none yet
    char base[sizeof s_stats_url];
    lock();
    const int help = s_help;
    memcpy(base, s_stats_url, sizeof base);
    unlock();
    const uint32_t now = now_ms();
    if (help != 1 || !base[0] || !net_up() || (sent && now - sent < ALIVE_EVERY_MS)) return;
    sent = now ? now : 1;
    char url[sizeof base + 16], body[80];
    snprintf(url, sizeof url, "%s/api/alive", base);
    snprintf(body, sizeof body, "{\"build\":\"%s\",\"version\":\"%s\"}", board_name(), FLIGHT_VERSION);
    int status = 0;
    net_post_json(url, body, &status);
    ESP_LOGI(TAG, "counted on the map of boards: HTTP %d", status);
}

/* digits and dots, at least one: a version, or nothing to compare with */
static int version_like(const char *s)
{
    if (!s[0]) return 0;
    for (; *s; s++) {
        if (!((*s >= '0' && *s <= '9') || *s == '.')) return 0;
    }
    return 1;
}

/* --- the weather for the WEATHER look (core/ui_weather.c): Open-Meteo's forecast and air quality round
 * home, the point rounded to a tenth of a degree (about 10 km), every half an hour, a minute on after an
 * answer that did not read, at once for a new home. two of them: the screen reads one while the next
 * is filled. only asked once the planes' question went, one request at a time on this task --- */
#define WX_EVERY_MS (30u * 60u * 1000u)
#define WX_RETRY_MS (60u * 1000u)
#define WX_BUF      4096                               /* the forecast's answer is about 2.6 KB */
static weather_t s_wx[2];
static int       s_wx_cur = -1;                        // the one the screen reads, -1: none yet. under s_lock

static void wx_service(const fr_cfg_t *c)
{
    static uint32_t asked;
    static int heard;
    static float for_lat = 999, for_lon = 999;
    const float lat = roundf(c->lat * 10.0f) / 10.0f, lon = roundf(c->lon * 10.0f) / 10.0f;
    const uint32_t now = now_ms();
    if (lat == for_lat && lon == for_lon && asked && now - asked < (heard ? WX_EVERY_MS : WX_RETRY_MS)) return;
    asked = now ? now : 1;
    heard = 0;
    char *buf = malloc(WX_BUF);
    if (!buf) {
        ESP_LOGW(TAG, "weather: no memory for the answer");
        return;
    }
    lock();
    weather_t *w = &s_wx[s_wx_cur == 0 ? 1 : 0];       // the one the screen does not read
    unlock();
    static char url[512];
    text_t t = { buf, 0, WX_BUF };
    int status = 0;
    buf[0] = 0;
    wx_url_forecast(url, sizeof url, lat, lon);
    const uint32_t t0 = now_ms();
    net_get(url, NULL, to_text, &t, &status);
    if (status == 200 && t.n < WX_BUF - 1 && wx_parse_forecast(buf, w)) {
        const int fc_bytes = t.n;
        t.n = 0;
        buf[0] = 0;
        status = 0;
        wx_url_air(url, sizeof url, lat, lon);
        net_get(url, NULL, to_text, &t, &status);
        const int air = status == 200 && t.n < WX_BUF - 1 && wx_parse_air(buf, w);
        // the day before, for the graphs. when it does not come, the last answer's, moved on by the hours since
        t.n = 0;
        buf[0] = 0;
        status = 0;
        wx_url_past(url, sizeof url, lat, lon);
        net_get(url, NULL, to_text, &t, &status);
        const int past = status == 200 && t.n < WX_BUF - 1 && wx_parse_past(buf, w);
        if (!past && s_wx_cur >= 0 && s_wx[s_wx_cur].at) {   // s_wx_cur changes on this task alone
            wx_past_from(w, &s_wx[s_wx_cur]);
        }
        lock();
        s_wx_cur = w == &s_wx[0] ? 0 : 1;
        unlock();
        heard = 1;
        for_lat = lat;
        for_lon = lon;
        ESP_LOGI(TAG, "weather: %d bytes, %d.%d C, %u hPa, wind %d km/h, air %s, past %s, in %u ms, heap %u", fc_bytes,
                 w->now_t10 / 10, (w->now_t10 < 0 ? -w->now_t10 : w->now_t10) % 10, (unsigned)(w->now_hpa10 / 10), w->wind_kmh,
                 air ? "read" : "not read", past ? "read" : "not read", (unsigned)(now_ms() - t0),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    } else {
        ESP_LOGW(TAG, "weather: HTTP %d, %d bytes, not read", status, t.n);
    }
    free(buf);
}

static void update_check(void)
{
    static uint32_t asked;                             // ms of the last question, 0: never
    static int heard;
    uint32_t now = now_ms();
    if (!net_up() || (asked && now - asked < (heard ? UPDATE_EVERY_MS : UPDATE_RETRY_MS))) return;
    asked = now ? now : 1;
    char buf[160], v[sizeof s_newest] = "";
    text_t t = { buf, 0, sizeof buf };
    buf[0] = 0;
    int status = 0;
    net_get(UPDATE_URL, NULL, to_text, &t, &status);
    heard = status == 200 && json_str(buf, "version", v, sizeof v) && version_like(v);
#if FLIGHT_BETA
    /* a beta follows the betas: the site's beta while it has one above its stable, then the stable,
     * which is what a beta found good becomes. a stable build never hears of a beta */
    char b[sizeof s_newest] = "";
    if (heard && json_str(buf, "beta", b, sizeof b) && version_like(b) && ui_version_cmp(b, v) > 0) {
        snprintf(v, sizeof v, "%s", b);
    }
#endif
    ESP_LOGI(TAG, "the site's version: HTTP %d, %s, this one %s%s", status, heard ? v : "none", FLIGHT_VERSION,
             FLIGHT_BETA ? " beta" : "");
    if (!heard) return;
    // where boards are counted: an https address and nothing odd in it, or none. an answer that filled the
    // buffer may be cut in the middle of it: then none
    char stats[sizeof s_stats_url] = "";
    if (t.n < (int)sizeof buf - 1) json_str(buf, "stats", stats, sizeof stats);
    int fine = !strncmp(stats, "https://", 8) && strlen(stats) < sizeof stats - 1;
    for (const char *p = stats; fine && *p; p++) fine = *p > ' ' && *p != '"' && *p != '\\' && *p != '?' && *p != '#';
    for (size_t n = strlen(stats); fine && n > 8 && stats[n - 1] == '/'; n--) stats[n - 1] = 0;   // "/api/alive" goes after it
    lock();
    snprintf(s_newest, sizeof s_newest, "%s", v);
    snprintf(s_stats_url, sizeof s_stats_url, "%s", fine ? stats : "");
    unlock();
    if (ui_version_cmp(v, FLIGHT_VERSION) > 0) ESP_LOGI(TAG, "the site has a newer version: %s", v);
}

/* --- the watch list (core/watch.h): the sky round home says which are in range, these lookups
 * the rest, one plane at a time. both free networks answer one plane by callsign, registration
 * (with its dash: OE-IJW) or address; a list of them they do not take, only the first --- */

#define WATCH_EVERY_MS 15000u          /* a lookup every 15 s: 8 planes, each looked at every two minutes */

static const char *const WATCH_BASE[FREE_SERVERS] = { "https://api.adsb.lol/v2/", "https://opendata.adsb.fi/api/v2/" };
static const char *const WATCH_PATH[FREE_SERVERS][3] = {          // by callsign, registration, address
    { "callsign/", "reg/", "hex/" },
    { "callsign/", "registration/", "hex/" },
};
static const char *const WATCH_KIND[4] = { "?", "callsign", "registration", "address" };

// what the watch list said, for the LED. s_lock held
static void light_news(int news, uint32_t now)
{
    if (news == WATCH_NEWS_RANGE) light_event(&s_light, LIGHT_RANGE, now);
    else if (news == WATCH_NEWS_UP) light_event(&s_light, LIGHT_UP, now);
    else if (news == WATCH_NEWS_DOWN) light_event(&s_light, LIGHT_DOWN, now);
}

/* the answer is read with the planes' own reader: the plane of the list is among the ones it kept.
 * asked of the other free network than the planes': with both on one, adsb.lol said 429 */
static void watch_lookup(sky_reader_t *rd, int planes_server)
{
    const int server = (planes_server + 1) % FREE_SERVERS;
    uint32_t now = now_ms();
    if (s_watch_asked && now - s_watch_asked < WATCH_EVERY_MS) return;
    char id[WATCH_ID];
    int kind = WATCH_CALLSIGN;
    lock();
    int i = watch_next(&s_watch);
    if (i >= 0) {
        memcpy(id, s_watch.w[i].id, sizeof id);
        kind = watch_kind(&s_watch.w[i]);
        sky_read_begin(rd, &s_sky, 0, now);
    }
    unlock();
    if (i < 0) return;                                 // none, or all round home
    s_watch_asked = now;
    static char url[96];
    snprintf(url, sizeof url, "%s%s%s", WATCH_BASE[server], WATCH_PATH[server][kind - WATCH_CALLSIGN], id);
    int status = 0;
    net_get(url, NULL, to_reader, rd, &status);
    if (status != 200) {
        ESP_LOGW(TAG, "watch %s: %s by %s, HTTP %d", id, FREE_NAME[server], WATCH_KIND[kind], status);
        return;                                        // the same one again next time
    }
    char said[40] = "";
    const char *how = "not heard";
    lock();
    watch_t *w = &s_watch.w[i];
    if (!strcmp(w->id, id)) {                          // the list was not changed meanwhile
        const plane_t *p = NULL;
        for (int k = 0; k < rd->n && !p; k++) if (watch_is(w, &rd->got[k])) p = &rd->got[k];
        if (p) how = p->ground ? "on the ground" : "in the air";
        light_news(watch_answer(w, kind, p, now, said, sizeof said), now);
        if (said[0]) say_locked(said, "A WATCHED PLANE", 10000u);
    }
    unlock();
    ESP_LOGI(TAG, "watch %s: %s by %s, %s%s%s", id, FREE_NAME[server], WATCH_KIND[kind], how, said[0] ? ": " : "", said);
}

/* --- the page's radar (web/): while it asks, each answer goes out over the console too, a line a
 * plane in the free networks' own words, so its sky_read takes them as they are. a line each: a
 * list in one piece is more than the 3.5" has to spare, and the log may fall between lines. the
 * page says OWLOS SKY ON every half minute: a page that went away stops it by itself --- */
#define SKY_OUT_MS 60000u
static volatile uint32_t s_sky_until;                  // now_ms till which it goes out
static volatile uint32_t s_pick;                       // OWLOS PICK from the page: the plane picked there
static volatile int      s_pick_new;
static uint32_t          s_sel_told;                   // the picked plane the page knows of
static volatile int      s_wxpage_told = -1;           // the WEATHER look's page the page knows of, -1: tell it again (a GET)

/* 0: no page. a passed time is let go here (only if no SKY ON renewed it meanwhile): left as it was,
 * 24.8 days later the 32-bit milliseconds would wrap round to it and read it as ahead again, and
 * every answer would go out over the console for good */
static int page_there(void)
{
    uint32_t until = s_sky_until;
    if (until && (int32_t)(until - now_ms()) > 0) return 1;
    if (until) __atomic_compare_exchange_n(&s_sky_until, &until, 0u, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return 0;
}

static void sky_out(void)
{
    static const char *const CATEGORY[KINDS] = {
        [KIND_JET] = "A3", [KIND_LIGHT] = "A1", [KIND_BIZJET] = "A2", [KIND_HEAVY] = "A5", [KIND_FAST] = "A6",
        [KIND_HELI] = "A7", [KIND_GLIDER] = "B1", [KIND_BALLOON] = "B2", [KIND_DRONE] = "B6", [KIND_GROUND] = "C1",
    };
    static plane_t p;                                  // only this task: off its stack
    char server[sizeof s_server];
    if (!page_there()) return;
    lock();
    int n = s_sky.n, total = s_sky.total;
    snprintf(server, sizeof server, "%s", s_server);
    unlock();
    printf("OWLOS SKY BEGIN %s\n", server);
    for (int i = 0; i < n; i++) {
        lock();
        int here = i < s_sky.n;
        if (here) p = s_sky.p[i];
        unlock();
        if (!here) break;
        char alt[16];
        if (p.ground) snprintf(alt, sizeof alt, "\"ground\"");
        else snprintf(alt, sizeof alt, "%d", (int)p.alt);
        printf("OWLOS PLANE {\"hex\":\"%06x\",\"flight\":\"%s\",\"r\":\"%s\",\"t\":\"%s\",\"category\":\"%s\",\"lat\":%.5f,\"lon\":%.5f,"
               "\"alt_baro\":%s,\"gs\":%.1f,\"track\":%.1f,\"baro_rate\":%d,\"squawk\":\"%04d\",\"seen_pos\":%.1f,"
               "\"nav_altitude_mcp\":%d,\"nav_heading\":%d,\"ias\":%d,\"mach\":%.3f,\"wd\":%d,\"ws\":%d}\n",
               (unsigned)p.icao, p.flight, p.reg, p.type, CATEGORY[p.kind < KINDS ? p.kind : KIND_JET], (double)p.lat, (double)p.lon,
               alt, (double)p.gs, (double)p.track, (int)p.vrate, (int)p.squawk, (double)(now_ms() - p.seen) / 1000.0,
               p.sel_fl * 100, (int)p.sel_hdg, (int)p.ias, (double)p.mach / 1000.0, (int)p.wind_dir, (int)p.wind_kt);
    }
    printf("OWLOS SKY END %d\n", total);
}

#if RADAR_OVERLAY
/* --- a sky to test with (chipguyhere, Waveshare 7/7B builds): the same planes on every run, so builds
 * can be compared. OWLOS SKYSAVE keeps the sky as it is now (NVS); OWLOS SKYPLAY 1 shows that one, still,
 * from now on and after restarts, instead of asking the network; OWLOS SKYPLAY 0 goes back to it */
#include "nvs.h"
static CORE_BIG sky_t s_sky_saved;
static volatile int   s_skyplay, s_skysave;

static void skyplay_load(void)
{
    nvs_handle_t h;
    if (nvs_open("skyplay", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t on = 0;
    size_t sz = sizeof s_sky_saved;
    if (nvs_get_u8(h, "on", &on) == ESP_OK && on && nvs_get_blob(h, "sky", &s_sky_saved, &sz) == ESP_OK &&
        sz == sizeof s_sky_saved) {
        s_skyplay = 1;
        ESP_LOGW(TAG, "test sky: %d planes, the same each run (OWLOS SKYPLAY 0 to go back to the network)", s_sky_saved.n);
    }
    nvs_close(h);
}

static void skyplay_store(int save, int on)
{
    nvs_handle_t h;
    if (nvs_open("skyplay", NVS_READWRITE, &h) != ESP_OK) return;
    if (save) {
        lock();
        s_sky_saved = s_sky;
        unlock();
        esp_err_t e = nvs_set_blob(h, "sky", &s_sky_saved, sizeof s_sky_saved);
        ESP_LOGW(TAG, "test sky kept: %d planes, %s", s_sky_saved.n, esp_err_to_name(e));
    }
    if (on >= 0) {
        nvs_set_u8(h, "on", (uint8_t)on);
        s_skyplay = on;
        ESP_LOGW(TAG, "test sky %s", on ? "shown from now on, after restarts too" : "off: the network again");
    }
    nvs_commit(h);
    nvs_close(h);
}

// the kept sky put in place, its planes held still where they were
static void skyplay_show(void)
{
    const uint32_t now = now_ms();
    lock();
    s_sky = s_sky_saved;
    for (int i = 0; i < s_sky.n; i++) {
        plane_t *p = &s_sky.p[i];
        p->gs = 0.0f;                                  // still: nothing to move it on
        p->turn = 0;                                   // nor to turn it, nor a slide it was in when it was kept
        p->gx = p->gy = 0;
        p->seen = p->heard = now;
        p->first = now - 600000u;                      // long seen: no ring opening round it
    }
    s_sky.updated = now;
    s_sky.status = SKY_OK;
    unlock();
}
#endif

static void fetch_task(void *arg)
{
    (void)arg;
    static CORE_BIG sky_reader_t rd;                   // the answer is read into this as it comes
    static char url[200];
    int next = -1;                                     // free server to try, -1 = the one in the settings
    uint32_t down = 0;                                 // since when the WiFi is not there
#if RADAR_OVERLAY
    skyplay_load();
#endif
    for (;;) {
#if RADAR_OVERLAY
        if (s_skysave) {                               // asked on the console: done here, where the sky is written
            skyplay_store(s_skysave & 1, (s_skysave >> 1) - 1);
            s_skysave = 0;
        }
        if (s_skyplay) {                               // the kept sky instead of the network's
            skyplay_show();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
#endif
        fr_cfg_t c;
        int configured, fr24;
        static char key[sizeof s_sec.key];
        s_fp = FP_HOME;
        home_find();                                   // first: the planes are asked round the new home
        lock();
        c = s_cfg;
        configured = s_sec.ssid[0] != 0;
        fr24 = c.source == SRC_FR24 && s_sec.key[0];
        memcpy(key, s_sec.key, sizeof key);
        sky_hide_ground(&s_sky, !c.ground);            // the screen's or the site's setting, at once
        unlock();
        s_fp = FP_GEOCODE;
        geocode();                                     // home's country before anything is asked round it
        const int refused = home_refused();
        if (refused) {
            s_map.ready = 0;                           // no map of it either, the one shown goes
        } else {
            s_fp = FP_MAP;
            busy_on(BUSY_MAP);
            map_service(&c);                          // first: a new look or range shows its map before the planes come
            busy_off(BUSY_MAP);
            s_fp = FP_OTHER;
        }
        uint32_t every = FREE_EVERY_MS;
        const int up = net_up();
        if (up) net_time_start();                          // asked once the WiFi is up, drawn clock or not
        int trying = 0;
        if (configured && !up) {
            uint32_t now = now_ms();
            if (!down) down = now;
            trying = now - (s_wifi_try > down ? s_wifi_try : down) < 20000u;   // a router takes a while
        } else {
            down = 0;
        }
        /* no WiFi: none set, or the one set is not there. the demo sky flies then and the screen
         * says so in big letters; the real one comes back with the WiFi. the two never mix */
        const int demo = !configured || (!up && !trying);
        if (demo != s_demo_on) {
            lock();
            s_sky.n = 0;
            s_demo_on = demo;
            unlock();
        }
        if (demo) {
            static uint32_t told;                      // the demo answers like a server: every 5 s
            uint32_t now = now_ms();
            if (!configured || !s_sky.n || now - told >= FREE_EVERY_MS - 50u) {
                char *buf = malloc(DEMO_BYTES);
                if (!buf) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
                told = now;
                int n = demo_json(&s_demo, c.lat, c.lon, range_km(&c), now, buf, DEMO_BYTES);
                lock();
                sky_read_begin(&rd, &s_sky, 0, now);
                sky_read(&rd, buf, n);
                sky_read_end(&rd, &s_sky);
                sky_forget(&s_sky, now);
                snprintf(s_server, sizeof s_server, "DEMO");
                s_fetched = now;
                unlock();
                free(buf);
                sky_out();
            }
            if (configured) every = 1000;              // the WiFi set may come back any moment: look each second
        } else if (!up) {
            lock();
            s_sky.status = SKY_CONNECTING;
            snprintf(s_sky.note, sizeof s_sky.note, "CONNECTING");
            unlock();
            every = 1000;
        } else if (refused) {
            static uint32_t said;
            lock();
            s_sky.n = 0;
            s_sky.status = SKY_HTTP;                   // said on the screen as what is wrong, in the alarm colour
            snprintf(s_sky.note, sizeof s_sky.note, "NOT AVAILABLE IN RUSIA");
            unlock();
            if (!said || now_ms() - said > 600000u) {
                ESP_LOGW(TAG, "home is in Rusia: no planes and no map are asked");
                said = now_ms();
            }
            update_check();
        } else {
            int server = next < 0 ? (c.server < FREE_SERVERS ? c.server : 0) : next;
            if (fr24) fr24_url(&c, url, sizeof url);
            else free_url(&c, server, url, sizeof url);
            static char said[sizeof url];
            if (strcmp(url, said)) {                   // what goes out, once each time it changes (no key in it)
                ESP_LOGI(TAG, "asking %s", url);
                snprintf(said, sizeof said, "%s", url);
            }
            int status = 0;
            int64_t t0 = esp_timer_get_time();
            lock();
            sky_read_begin(&rd, &s_sky, fr24, now_ms());   // only the home is taken from the sky here
            unlock();
            s_fp = FP_HTTP;
            busy_on(BUSY_HTTP);
            int len = net_get(url, fr24 ? key : NULL, to_reader, &rd, &status);
            busy_off(BUSY_HTTP);
            s_fp = FP_MERGE;
            int ms = (int)((esp_timer_get_time() - t0) / 1000);
            uint32_t now = now_ms();
            int n = -2;
            lock();
            if (len > 0 && status == 200) {
                rd.now = now;                          // heard now; positions keep the time the request went out
                n = sky_read_end(&rd, &s_sky);
                if (n < 0) {
                    s_sky.status = SKY_PARSE;
                    snprintf(s_sky.note, sizeof s_sky.note, "STRANGE ANSWER");
                } else {
                    snprintf(s_server, sizeof s_server, "%s", fr24 ? "FLIGHTRADAR24" : FREE_NAME[server]);
                    s_fetched = now;
                    char came[40] = "";
                    uint32_t pick = 0;
                    light_news(watch_sky(&s_watch, &s_sky, now, came, sizeof came, &pick), now);   // a watched one round home?
                    light_hold(&s_light, LIGHT_RANGE, watch_near(&s_watch) > 0, now);
                    light_sky(&s_light, &s_light_cfg, &s_sky, unit_km(c.dist_unit), now);   // an emergency, a plane near
                    if (came[0]) {
                        say_locked(came, "A WATCHED PLANE", 10000u);
                        ESP_LOGI(TAG, "watch: %s", came);
                    }
                    if (pick) s_watch_pick = pick;
                }
            } else {
                s_sky.status = SKY_HTTP;
                if (status) snprintf(s_sky.note, sizeof s_sky.note, "%s HTTP %d", fr24 ? "FR24" : FREE_NAME[server], status);
                else snprintf(s_sky.note, sizeof s_sky.note, "%s NO ANSWER", fr24 ? "FR24" : FREE_NAME[server]);
            }
            s_http = status;
            sky_forget(&s_sky, now);
            int planes = s_sky.n;
            unlock();
            if (n >= 0) sky_out();
            if (!fr24 && n < 0) next = (server + 1) % FREE_SERVERS;   // that one failed: the next one next time
            else if (!fr24) next = server;
            ESP_LOGI(TAG, "%s: HTTP %d, %d bytes in %d ms, %d planes in it, %d kept, %d on the list, heap %u, at least %u since power on",
                     fr24 ? "FR24" : FREE_NAME[server], status, len, ms, rd.total, n, planes,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
            if (fr24) every = FR24_EVERY_MS;
            if (status == 429) every *= 3;            // too many requests: slow down
            s_fp = FP_WATCH;
            if (status != 429) watch_lookup(&rd, server);   // on the free networks, FR24 or not
            s_fp = FP_UPDATE;
            update_check();
            s_fp = FP_ALIVE;
            alive_report();
            s_fp = FP_OTHER;
            wx_service(&c);
        }
        lock();                                        // the LED's no data: the WiFi set is not there, or no answer
        light_hold(&s_light, LIGHT_NODATA, configured && (demo || s_sky.status >= SKY_HTTP), now_ms());
        unlock();
        s_fp = FP_IDLE;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(every)); // a new setting cuts the wait short
    }
}

/* --- settings from the site, over the USB line (console.h) --- */

// a string as JSON wants it: a quote or a backslash in a network name would end it early
static const char *json_esc(const char *s, char *out, int cap)
{
    int n = 0;
    for (; *s && n < cap - 3; s++) {
        if (*s == '"' || *s == '\\') out[n++] = '\\';
        out[n++] = (unsigned char)*s < 32 ? '?' : *s;
    }
    out[n] = 0;
    return out;
}

static void say_cfg(void)
{
    /* static, and used only under the lock: the site's line and the screen both say this. a GET says it on
     * the console's task, 4 KB of stack, and this printf of fifty fields needs much of it: with these on
     * the stack too, a GET overflowed it and the board restarted (2026-11-18, the CYD and the 3.5") */
    static char ssid[70], place[84], country[68], watch[WATCH_TEXT], dim[40], units[112], nets[NETS_KEPT * 72];
    lock();
    dim[0] = 0;
#if RADAR_HAS_DIM
    snprintf(dim, sizeof dim, ",\"backlight\":%d,\"night\":%d", s_backlight, s_night);   // only where the light turns down: the site shows them then
#endif
    /* the weather's units, 0 each home's country's own, and that country's two letters ("" not known yet): the site
     * shows what AUTO is. made here, not in the printf: its arguments are what fills the console's stack */
    snprintf(units, sizeof units, ",\"temp_unit\":%d,\"pressure_unit\":%d,\"wind_unit\":%d,\"rain_unit\":%d,\"cc\":\"%s\"",
             s_units.temp, s_units.pres, s_units.wind, s_units.rain, s_home_cc);
    watch_text(&s_watch, watch, sizeof watch);         // letters, digits, dashes and commas: nothing to escape
    nets[0] = 0;
    for (int i = 0, at = 0; i < s_nets.count; i++)     // names only: a password goes in, never out
        at += snprintf(nets + at, sizeof nets - (size_t)at, "%s\"%s\"", i ? "," : "", json_esc(s_nets.n[i].ssid, ssid, sizeof ssid));
    int led = 0;                                       // the LED's events as bits: 1 sos, 2 in range, 4 take-off, 8 landing, 16 near, 32 no data, 64 weather
    for (int ev = 0; ev < LIGHT_EVENTS; ev++) led |= s_light_cfg.on[ev] ? 1 << ev : 0;
    printf("OWLOS CFG {\"ssid\":\"%s\",\"nets\":[%s],\"lat\":%.5f,\"lon\":%.5f,\"place\":\"%s\",\"country\":\"%s\",\"src\":\"%s\",\"key\":%s,"
           "\"up\":%d,\"flip\":%d,\"theme\":%d,\"look\":%d,\"map\":%d,\"ground\":%d,\"watch\":\"%s\","
           "\"vectors\":%d,\"beam_step\":%d,\"clock\":%d,\"block\":%d,\"autopilot\":%d,\"lost\":%d,\"conflict\":%d,"
           "\"fl_lo\":%d,\"fl_hi\":%d,\"utc\":%u,"
           "\"led\":%d,\"led_level\":%d,\"led_hold\":%d,\"led_near\":%d,\"led_red\":%d,\"version\":\"%s\",\"newest\":\"%s\","
           "\"uptime\":%u,\"ran\":%u,\"reset\":\"%s\",\"screen\":%d,"
           "\"range\":%.2f,\"dist_unit\":%d,\"alt_unit\":%d,\"labels\":%d,\"help\":%d,\"bare\":%d,\"alerts\":%d,\"names\":%d%s%s}\n",
           json_esc(s_sec.ssid, ssid, sizeof ssid), nets, (double)s_cfg.lat, (double)s_cfg.lon,
           json_esc(s_cfg.place, place, sizeof place), json_esc(s_cfg.country, country, sizeof country),
           s_cfg.source == SRC_FR24 ? "fr24" : "free", s_sec.key[0] ? "true" : "false", s_cfg.up, s_cfg.flip,
           s_cfg.theme, s_cfg.look, s_cfg.map, s_cfg.ground, watch, s_opt.vectors, s_opt.beam_step, s_opt.clock,
           s_opt.block, s_opt.autopilot, s_opt.lost, s_opt.conflict, s_opt.fl_lo, s_opt.fl_hi,
           (unsigned)net_utc(), led, s_light_cfg.level, s_light_cfg.hold,
           light_near_units(&s_light_cfg), light_red_gpio(&s_light_cfg), FLIGHT_VERSION, s_newest,
           (unsigned)(now_ms() / 1000u), (unsigned)s_ran_s, s_reset, s_screen,
           (double)s_cfg.range, s_cfg.dist_unit, s_cfg.alt_unit, s_cfg.labels, s_help, s_bare, s_alerts, s_names, dim, units);
    unlock();
}

/* the list changed: its first network is what the secrets keep, the others are saved here */
static void nets_changed(secrets_t *s)
{
    memcpy(s->ssid, s_nets.n[0].ssid, sizeof s->ssid);
    memcpy(s->pass, s_nets.n[0].pass, sizeof s->pass);
    store_save_nets(&s_nets);
}

/* the name of this build on the site */
static const char *board_name(void)
{
#if RADAR_DISPLAY == DISPLAY_ILI9341_320x240
    return "ili9341";
#elif RADAR_DISPLAY == DISPLAY_ST7796_480x320
    return "st7796";
#elif RADAR_DISPLAY == DISPLAY_ILI9486_480x320
    return "ili9486";
#elif RADAR_DISPLAY == DISPLAY_GC9A01_240x240
    return "gc9a01";
#elif RADAR_DISPLAY == DISPLAY_ES3C28P_320x240
    return "es3c28p";
#elif RADAR_DISPLAY == DISPLAY_NMCYDC5_320x240
    return "nmcydc5";
#elif RADAR_DISPLAY == DISPLAY_CROWADV28_320x240
    return "crowadv28";
#elif RADAR_DISPLAY == DISPLAY_CROWADV50_800x480
    return "crowadv50";
#elif RADAR_DISPLAY == DISPLAY_JC8048W550_800x480
    return "jc8048w550";
#elif RADAR_DISPLAY == DISPLAY_TAB5_1280x720
    return "tab5";
#elif RADAR_DISPLAY == DISPLAY_4827S043_480x272
    return "esp4827s043";
#elif RADAR_DISPLAY == DISPLAY_CROW70_800x480
    return "crow70";
#elif RADAR_DISPLAY == DISPLAY_CROW50_800x480
    return "crow50";
#elif RADAR_DISPLAY == DISPLAY_WAVE7_800x480
    return "wave7";
#elif RADAR_DISPLAY == DISPLAY_WAVE7B_1024x600
    return "wave7b";
#elif RADAR_DISPLAY == DISPLAY_JC8012_1280x800
    return "jc8012";
#else
    return "other";
#endif
}

/* who is on this cable: the page keeps several boards apart by the MAC, a port's number can change */
static void say_ready(void)
{
    uint8_t m[6] = { 0 };
#if CONFIG_IDF_TARGET_ESP32P4
    esp_read_mac(m, ESP_MAC_BASE);                      // the WiFi's MAC is the C6's: the P4's own base one, as fixed to this board
#else
    esp_read_mac(m, ESP_MAC_WIFI_STA);
#endif
    printf("OWLOS READY flight-radar %s %s %02x%02x%02x%02x%02x%02x\n", FLIGHT_VERSION, board_name(), m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void on_line(const char *line)
{
    if (!strncmp(line, "OWLOS HELLO", 11)) { say_ready(); return; }
    if (!strncmp(line, "OWLOS GET", 9)) { s_wxpage_told = -1; say_cfg(); return; }
    if (!strcmp(line, "OWLOS SKIP")) { s_skip_align = 1; return; }
#if RADAR_OVERLAY
    // the test sky (see skyplay_load): bit 0 keep the sky now, bits 1.. 1 off / 2 on, 0 leave
#if DISP_JC8012
#if DISP_JC8012
    if (!strcmp(line, "OWLOS NOPACE")) {               // TEMP: the moving layer's builds not paced to the panel for 20 s
        extern volatile int64_t g_nopace_until;
        g_nopace_until = esp_timer_get_time() + 20000000;
        ESP_LOGI(TAG, "TEMP pacing test: the layer's builds unpaced for 20 s, from now");
        return;
    }
#endif
    if (!strcmp(line, "OWLOS SCROLLTEST")) { extern volatile uint32_t g_scrolltest_at; g_scrolltest_at = now_ms() | 1; return; }
#endif
    if (!strcmp(line, "OWLOS SKYSAVE")) { s_skysave = 1; if (s_fetch) xTaskNotifyGive(s_fetch); return; }
    if (!strcmp(line, "OWLOS SKYPLAY 1")) { s_skysave = 2 << 1; if (s_fetch) xTaskNotifyGive(s_fetch); return; }
    if (!strcmp(line, "OWLOS SKYPLAY 0")) { s_skysave = 1 << 1; if (s_fetch) xTaskNotifyGive(s_fetch); return; }
#endif
    if (!strcmp(line, "OWLOS SKY ON")) {               // the page's radar, see sky_out
        s_align_told = -1;
        int was = page_there();
        s_sky_until = (now_ms() + SKY_OUT_MS) | 1u;    // never 0: that is no page
        if (!was) xTaskNotifyGive(s_fetch);            // the first answer now, not in five seconds
        return;
    }
    if (!strcmp(line, "OWLOS SKY OFF")) { s_sky_until = 0; return; }
    if (!strncmp(line, "OWLOS PICK ", 11)) {           // a plane tapped on the page, 0: let go
        s_pick = (uint32_t)strtoul(line + 11, NULL, 16);
        s_pick_new = 1;
        return;
    }
    if (strncmp(line, "OWLOS SET ", 10)) return;
    const char *j = line + 10;
    secrets_t sec;
    fr_cfg_t c;
    lock();
    sec = s_sec;
    c = s_cfg;
    unlock();
    double v;
    char src[8];
    int wifi = 0, home = 0;
    if (json_str(j, "ssid", sec.ssid, sizeof sec.ssid)) wifi = 1;
    if (json_str(j, "pass", sec.pass, sizeof sec.pass)) wifi = 1;
    char forget[33] = "";
    json_str(j, "forget", forget, sizeof forget);
    json_str(j, "key", sec.key, sizeof sec.key);
    if (json_num(j, "lat", &v)) { c.lat = (float)v; home = 1; }
    if (json_num(j, "lon", &v)) { c.lon = (float)v; home = 1; }
    int named = json_str(j, "place", c.place, sizeof c.place);
    int land = json_str(j, "country", c.country, sizeof c.country);
    if (home && !named) c.place[0] = 0;                    // a new home without a name: the board asks OSM for it
    if (home && !land) c.country[0] = 0;
    if (json_str(j, "src", src, sizeof src)) c.source = !strcmp(src, "fr24") ? SRC_FR24 : SRC_FREE;
    if (c.source == SRC_FR24 && !sec.key[0]) c.source = SRC_FREE;
    // how the screen shows it: a board without touch has no other way to these
    if (json_num(j, "up", &v)) c.up = (int16_t)((((int)lround(v)) % 360 + 360) % 360);
    if (json_num(j, "flip", &v)) c.flip = v != 0;
    if (json_num(j, "theme", &v) && v >= 0 && v < UI_THEMES) c.theme = (uint8_t)v;
    if (json_num(j, "look", &v) && v >= 0 && v < LOOKS) c.look = (uint8_t)v;
    if (json_num(j, "map", &v)) c.map = v != 0;
    if (json_num(j, "ground", &v)) c.ground = v != 0;
    // what the page's radar changes besides those
    if (json_num(j, "range", &v) && v >= RANGE_MIN && v <= RANGE_MAX) c.range = (float)v;
    if (json_num(j, "dist_unit", &v) && v >= 0 && v <= DIST_MI) c.dist_unit = (uint8_t)v;
    if (json_num(j, "alt_unit", &v) && v >= 0 && v <= ALT_M) c.alt_unit = (uint8_t)v;
    if (json_num(j, "labels", &v)) c.labels = v != 0;
    scope_opt_t opt;                                   // the radar's options
    lock();
    opt = s_opt;
    unlock();
    int opted = 0;
    const struct { const char *name; uint8_t *on; } SWITCHES[] = {
        { "vectors", &opt.vectors }, { "beam_step", &opt.beam_step }, { "clock", &opt.clock }, { "block", &opt.block },
        { "autopilot", &opt.autopilot }, { "lost", &opt.lost }, { "conflict", &opt.conflict },
    };
    for (size_t i = 0; i < sizeof SWITCHES / sizeof SWITCHES[0]; i++)
        if (json_num(j, SWITCHES[i].name, &v)) { *SWITCHES[i].on = v != 0; opted = 1; }
    if (json_num(j, "fl_lo", &v) && v >= 0 && v <= SCOPE_FL_MAX) { opt.fl_lo = (int16_t)v; opted = 1; }
    if (json_num(j, "fl_hi", &v) && v >= 0 && v <= SCOPE_FL_MAX) { opt.fl_hi = (int16_t)v; opted = 1; }
    if (json_num(j, "help", &v) && v >= 0 && v <= 2) {     // the site's question, answered there
        lock();
        s_help = (uint8_t)v;
        s_help_new = 1;
        unlock();
        store_save_help((int)v);
    }
    if (json_num(j, "wxpage", &v) && v >= 0 && v < WX_PAGES) {   // the weather's page, the dial or a graph
        lock();
        s_wxpage = (uint8_t)v;
        s_wxpage_new = 1;
        unlock();
    }
    if (json_num(j, "bare", &v) && (v == 0 || v == 1)) {   // the radar's panel away or back, from the site
        lock();
        s_bare = (uint8_t)v;
        s_bare_new = 1;
        unlock();
        store_save_bare((int)v);
    }
#if RADAR_HAS_DIM
    if (json_num(j, "backlight", &v) && v >= 0 && v < UI_BACKLIGHTS) {   // the screen's light, from the site
        lock();
        s_backlight = (uint8_t)v;
        s_backlight_new = 1;
        unlock();
        store_save_backlight((int)v);
    }
    if (json_num(j, "night", &v) && v >= 0 && v < UI_BACKLIGHTS) {   // the night's light, from the site
        lock();
        s_night = (uint8_t)v;
        s_night_new = 1;
        unlock();
        store_save_night((int)v);
    }
#endif
    if (json_num(j, "alerts", &v) && v >= 0 && v <= WXA_ALL) {   // the weather's warnings shown, from the site
        lock();
        s_alerts = (uint8_t)v;
        s_alerts_new = 1;
        unlock();
        store_save_alerts((int)v);
    }
    /* the map's names on the dark looks, from the site. its screen says them in every SET it sends: saved only
     * when they change, the flash is written no more than it was */
    if (json_num(j, "names", &v) && v >= 0 && v < NAMES_STYLES) {
        lock();
        const int changed = s_names != (uint8_t)v;
        s_names = (uint8_t)v;
        s_names_new = 1;
        unlock();
        if (changed) {
            store_save_names((int)v);
        }
    }
    {                                                  // the weather's units from the site, 0 home's country's own
        wx_units_t un;
        int unit_set = 0;
        lock();
        un = s_units;
        unlock();
        if (json_num(j, "temp_unit", &v) && v >= 0 && v < TEMP_UNITS) { un.temp = (uint8_t)v; unit_set = 1; }
        if (json_num(j, "pressure_unit", &v) && v >= 0 && v < PRES_UNITS) { un.pres = (uint8_t)v; unit_set = 1; }
        if (json_num(j, "wind_unit", &v) && v >= 0 && v < WIND_UNITS) { un.wind = (uint8_t)v; unit_set = 1; }
        if (json_num(j, "rain_unit", &v) && v >= 0 && v < RAIN_UNITS) { un.rain = (uint8_t)v; unit_set = 1; }
        if (unit_set) {
            lock();
            s_units = un;
            s_units_new = 1;
            unlock();
            store_save_units(&un);
        }
    }
    char watch[WATCH_TEXT];
    int watched = json_str(j, "watch", watch, sizeof watch);   // "" empties it
    light_cfg_t light;
    lock();
    light = s_light_cfg;
    unlock();
    int lit = 0;                                       // the LED: its events as bits (say_cfg), how bright, how long
    if (json_num(j, "led", &v) && v >= 0 && v < (1 << LIGHT_EVENTS)) {
        for (int ev = 0; ev < LIGHT_EVENTS; ev++) light.on[ev] = ((int)v >> ev) & 1;
        lit = 1;
    }
    if (json_num(j, "led_level", &v) && v >= 0 && v < LIGHT_LEVELS) { light.level = (uint8_t)v; lit = 1; }
    if (json_num(j, "led_hold", &v) && v >= 0 && v < LIGHT_HOLDS) { light.hold = (uint8_t)v; lit = 1; }
    int near = -1;                                     // 1, 2, 5 or 10 of the distance unit
    if (json_num(j, "led_near", &v) && (near = light_near_index((int)v)) >= 0) { light.near = (uint8_t)near; lit = 1; }
    int red = -1;                                      // the red one's GPIO, 4 or 22
    if (json_num(j, "led_red", &v) && (red = light_red_index((int)v)) >= 0) { light.red = (uint8_t)red; lit = 1; }
    if (json_num(j, "led_test", &v) && v) s_light_test = 1;   // nothing kept: the screen task shows it
#if LCD_PROFILES
    if (json_num(j, "screen", &v) && v >= 0 && v < LCD_PROFILES) s_screen_set = (int)v;   // the screen's start-up, board.h LCD_PROFILES: on the CYD 0 v1, 1 v2, 2 v3
#endif
    if (c.lat < -90 || c.lat > 90 || c.lon < -180 || c.lon > 180) { printf("OWLOS ERR home is not on Earth\n"); return; }
    lock();
    if (wifi && sec.ssid[0]) nets_put(&s_nets, sec.ssid, sec.pass);
    else if (wifi) nets_forget(&s_nets, s_sec.ssid);    // "ssid":"": the one in use goes, the next takes its place
    if (forget[0] && nets_forget(&s_nets, forget)) wifi = 1;
    if (wifi) nets_changed(&sec);
    unlock();
    store_save_secrets(&sec);
    store_save_cfg(&c);
    lock();
    s_sec = sec;
    if (s_cfg.lat != c.lat || s_cfg.lon != c.lon) s_home_cc[0] = 0;   // another home: its country, geocode() asks
    s_cfg = c;
    s_cfg_new = 1;
    if (home) sky_home(&s_sky, c.lat, c.lon);
    if (wifi) { s_sky.n = 0; s_sky.status = SKY_CONNECTING; s_wifi_try = now_ms(); }   // the demo planes go
    if (watched) {
        watch_set(&s_watch, watch);
        watch_text(&s_watch, watch, sizeof watch);   // as it is kept: capitals, each once
        s_watch_asked = 0;
    }
    if (lit) {
        s_light_cfg = light;
        s_light_new = 1;
    }
    if (opted) {
        s_opt = opt;
        s_opt_new = 1;
    }
    nets_t nets = { 0 };                               // the list as it is now, taken under the lock: the screen's
    if (wifi) nets = s_nets;                           // WiFi page may change it meanwhile on the other task
    unlock();
    if (lit) store_save_light(&light);
    if (opted) store_save_opt(&opt);
    if (watched) {
        store_save_watch(watch);
        ESP_LOGI(TAG, "watch list from the site: %s", watch[0] ? watch : "none");
    }
    if (wifi) {
        net_reconnect(&nets);
        memset(&nets, 0, sizeof nets);                 // the passwords off the stack
    }
    ESP_LOGI(TAG, "settings from the site: network \"%s\", home %.4f %.4f, %s%s", sec.ssid, (double)c.lat, (double)c.lon,
             c.source == SRC_FR24 ? "FR24" : "free networks", sec.key[0] ? ", FR24 key stored" : "");
    printf("OWLOS OK\n");
    xTaskNotifyGive(s_fetch);
}

/* the screen's WiFi page: a network and the password typed for it, none typed for one it knows.
 * they go where the site's go, and the screen's copy of the password is wiped here */
static void wifi_from_screen(void)
{
    secrets_t sec;
    lock();
    sec = s_sec;
    const net_kept_t *kept = nets_find(&s_nets, s_ui.wifi_ssid);
    nets_put(&s_nets, s_ui.wifi_ssid, kept && !s_ui.wifi_pass[0] ? kept->pass : s_ui.wifi_pass);
    nets_changed(&sec);
    unlock();
    memset(s_ui.wifi_pass, 0, sizeof s_ui.wifi_pass);
    store_save_secrets(&sec);
    nets_t nets;
    lock();
    s_sec = sec;
    s_sky.n = 0;
    s_sky.status = SKY_CONNECTING;
    s_wifi_try = now_ms();
    nets = s_nets;                                     // under the lock: the site may change the list meanwhile
    unlock();
    net_reconnect(&nets);
    memset(&nets, 0, sizeof nets);                     // the passwords off the stack
    memset(sec.pass, 0, sizeof sec.pass);
    ESP_LOGI(TAG, "WiFi from the screen: network \"%s\"", sec.ssid);
    xTaskNotifyGive(s_fetch);
}

/* --- the screen --- */

typedef struct { uint8_t down; int16_t x, y, rx, ry; uint32_t t; } tsample_t;
static tsample_t s_ts[24];
static int       s_nts;
static uint32_t  s_ts_last;
static uint32_t  s_touch_us, s_send_us;   // reading the touch and handing strips over, for the 10 s log

static void touch_sample(int force);
#if DISP_JC8012
/* (chipguyhere) OWLOS SCROLLTEST on the console: the setup opened and dragged by a pretend finger, slowly
 * up, then quickly down, so the scrolling can be measured without anyone at the glass. the real touch is
 * ignored meanwhile */
volatile uint32_t g_scrolltest_at;
static uint32_t   s_st_passes, s_st_gap_max, s_st_last, s_st_moves;
static int        s_st_scroll_was = -1;
static void scrolltest(uint32_t now)
{
    const uint32_t at = g_scrolltest_at;
    if (!at) return;
    const uint32_t t = now - at;
    s_nts = 0;                                         // the glass is not listened to meanwhile
    if (t < 100) {
        if (s_ui.view != VIEW_SETUP) { s_ui.view = VIEW_SETUP; s_ui.setup_scroll = 0; ESP_LOGI(TAG, "scroll test: the setup"); }
        s_st_passes = s_st_gap_max = s_st_moves = 0;
        s_st_last = now;
        return;
    }
    int down = 0, y = 0;
    if (t >= 2500 && t < 3000) { down = 1; y = 260; }                                          // a finger resting,
    else if (t >= 3000 && t < 3800) { down = 1; y = 260 - (int)((t - 3000) * 12 / 800); }      // creeping (under the slop),
    else if (t >= 3800 && t < 5500) { down = 1; y = 248 - (int)((t - 3800) * 188 / 1700); }    // then up, slowly
    else if (t >= 7000 && t < 8000) { down = 1; y = 60 + (int)((t - 7000) * 200 / 1000); }    // down, quickly: 1 s
    if (down) {
        s_st_passes++;
        if (now - s_st_last > s_st_gap_max) s_st_gap_max = now - s_st_last;
        const int sc = s_ui.setup_scroll;
        if (sc != s_st_scroll_was) { s_st_moves++; s_st_scroll_was = sc; }
    }
    s_st_last = now;
    s_ts[s_nts++] = (tsample_t){ (uint8_t)down, (int16_t)(GFX_W / 2), (int16_t)y, 0, 0, now };
    if (t >= 9500) {
        g_scrolltest_at = 0;
        ESP_LOGI(TAG, "scroll test: done. %u ui passes with the finger down, %u of them with the list moved, the longest gap %u ms",
                 (unsigned)s_st_passes, (unsigned)s_st_moves, (unsigned)s_st_gap_max);
    }
}
#endif

/* the first moments after power on the glass is not read at all, and a press it says from the start counts only
 * once it has lifted: a panel's chip asked while the screen's bus and the touch were still settling now and then
 * said a press that was not there (on the 3.5" a tap on the look's button: the look one on after a restart) */
#define TOUCH_DEAF_MS 1500u
static int s_touch_lifted;                             // the glass read lifted once after them: presses count from now on

static void touch_sample(int force)
{
    uint32_t now = now_ms();
    if (now < TOUCH_DEAF_MS) return;
    if (s_nts >= (int)(sizeof s_ts / sizeof s_ts[0]) || (!force && now - s_ts_last < 15u)) return;
    int rx = 0, ry = 0, x = 0, y = 0;
    uint32_t t0 = now_us();
    int down = tp_read(&rx, &ry);
    s_touch_us += now_us() - t0;
    if (!s_touch_lifted) {
        if (down) down = 0;                            // pressed since before it was listened to: not yet a finger's
        else s_touch_lifted = 1;
    }
    if (down) touch_apply(&s_ui.cal, rx, ry, &x, &y);
    s_ts[s_nts++] = (tsample_t){ (uint8_t)down, (int16_t)x, (int16_t)y, (int16_t)rx, (int16_t)ry, now };
    s_ts_last = now;
}

/* --- the second core draws half of every frame while this core draws the rest: every view and every
 * look, the strips only read what ui_frame set up. two ways:
 *   pairs: every other strip is its own, in a strip of its own, packed under this core's, and the two
 *          go out in one transfer (DMA buffers of two strips). the 3.5" and the boards with PSRAM.
 *          on the 3.5" its strip, its stack and the DMA buffers' second half are taken for the radar
 *          and the weather looks only: the square maps want the memory, and there the SPI is what a
 *          frame waits for anyway
 *   halves (DISP_HELPER_HALF): the lower half of this core's strip, in this core's own canvas, and
 *          the strip goes out as it always did. no strip and no DMA memory of its own, only its stack:
 *          the 2.8", which has no room for the pairs --- */
#if DISP_HELPER
#define HELPER_STACK 7168
static TaskHandle_t      s_helper;
static SemaphoreHandle_t s_help_go, s_help_done;
static canvas_t          s_help_cv;            // the strip it draws now, or the lower half of this core's
static volatile int      s_help_stop;
static uint32_t          s_help_wait_us;       // this core waiting for it, for the 10 s log
/* halves: the row a strip is cut at, this core's rows above it. a row a frame towards who waits less:
 * the second core shares its core with the WiFi and the fetch, and is slower by what they do */
static int               s_half_top = DISP_STRIP / 2;

static void helper_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_help_go, portMAX_DELAY);
        if (s_help_stop) break;
        ui_strip(&s_ui, &s_help_cv);                   // the strips only read what ui_frame set up
        if (!DISP_HELPER_HALF) disp_pack_under(&s_help_cv);   // into the transfer, under this core's strip
        xSemaphoreGive(s_help_done);
    }
    xSemaphoreGive(s_help_done);
    vTaskDelete(NULL);
}

// between frames: on when wanted and the memory is there (with room left for TLS), off otherwise
static void helper_set(int on)
{
    if (on == (s_helper != NULL)) return;
    // what it takes besides its stack: a strip of its own and the DMA buffers' second half. halves: none
    const size_t px = DISP_HELPER_HALF ? 0 : GFX_PW * DISP_STRIP * sizeof(uint32_t);
    const size_t dma = DISP_HELPER_HALF ? 0 : GFX_PW * DISP_STRIP * sizeof(uint16_t) * 2;
    if (!on) {
        s_help_stop = 1;
        xSemaphoreGive(s_help_go);
        xSemaphoreTake(s_help_done, portMAX_DELAY);   // out of its loop, it deletes itself
        s_helper = NULL;
        if (!DISP_HELPER_HALF) {
            heap_caps_free(s_help_cv.px);
            s_help_cv.px = NULL;
            disp_rows(DISP_STRIP);
        }
        ESP_LOGI(TAG, "the second core stops drawing, heap %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
        return;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < px + dma + HELPER_STACK + 48 * 1024) return;
    if (!s_help_go) {
        s_help_go = xSemaphoreCreateBinary();
        s_help_done = xSemaphoreCreateBinary();
    }
    if (!DISP_HELPER_HALF) {
        s_help_cv = (canvas_t){ heap_caps_malloc(px, MALLOC_CAP_INTERNAL), 0, DISP_STRIP, 1, 0 };
        if (!s_help_cv.px) return;
    }
    s_help_stop = 0;
    if ((!DISP_HELPER_HALF && !disp_rows(2 * DISP_STRIP)) ||
        xTaskCreatePinnedToCore(helper_task, "strips", HELPER_STACK, NULL, 4, &s_helper, 0) != pdPASS) {
        s_helper = NULL;
        if (!DISP_HELPER_HALF) {
            disp_rows(DISP_STRIP);
            heap_caps_free(s_help_cv.px);
            s_help_cv.px = NULL;
        }
        return;
    }
    ESP_LOGI(TAG, "the second core draws %s, heap %u", DISP_HELPER_HALF ? "the lower half of every strip" : "every other strip",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
}
#endif

#if LCD_PROFILES
/* the 2.8" CYD's screen revision (board.h): BOOT in the first seconds, or the site. saved */
static void pick_screen(int screen, uint32_t now, const char *by)
{
    disp_profile(screen);
    store_save_screen(screen);
    s_screen = screen;
#if RADAR_HAS_BUTTON
    ui_screen_name = disp_profile_name();
#endif
    snprintf(s_ui.toast, sizeof s_ui.toast, "SCREEN %s", disp_profile_name());
    s_ui.toast_until = now + 3000u;
    ESP_LOGI(TAG, "%s: the screen is now %s, saved", by, disp_profile_name());
}
#endif

/* the screen turned 180 degrees, from its setup page or from the site. the touch alignment
 * turns with it, so it needs no new crosses */
static void screen_flip(int on)
{
#if DISP_LCD_SPI || DISP_LCD_RGB || DISP_LCD_DSI || DISP_ILI9486 || DISP_WAVE7 || DISP_JC8012
    disp_flip(on);
#endif
#if RADAR_TOUCH_CAL
    touch_cal_t *c = &s_ui.cal;
    c->a = -c->a; c->b = -c->b; c->c = (float)(GFX_W - 1) - c->c;
    c->d = -c->d; c->e = -c->e; c->f = (float)(GFX_H - 1) - c->f;
    store_save_cal(c);
#endif
    (void)on;
}

#if RADAR_OVERLAY
/* --- the boards with the moving layer (config.h RADAR_OVERLAY, chipguyhere) ---
 * the first core: a pass of the ui loop is a panel frame. it makes the moving layer for the frame
 * that starts next, and hands the background a copy of the ui's state when the background is ready
 * for another frame.
 * the second core: the panel's fill (the library's task, top priority), and under it the background,
 * drawn strip by strip from that copy into the frame that does not show, flipped in when whole. it
 * gets whatever time the fill leaves. a copy and not the ui's own state: the two cores draw at once */
#include "freertos/idf_additions.h"
#define TOP_ROWS 16                                    // the moving layer's band: 64 KB of RAM at 1024 wide. fewer bands, less of each plane done again a band
static canvas_t     s_top;                             // kept at 0 between uses (ovl.c)
static uint32_t    *s_bg_px;                           // the background's strip, DISP_STRIP rows
static ui_t        *s_ui_bg;                           // the state the background is drawn from, in the PSRAM
static TaskHandle_t s_bg_task;
static volatile uint32_t s_bg_gen;                     // (chipguyhere) frames the background task has drawn
static volatile int s_bg_busy;                         // from the copy till the frame is drawn
static volatile uint32_t s_bg_frames, s_bg_us;
static volatile int s_bg_view = -1;                    // the page the last whole background was drawn for
static int          s_view_up = -1;                    // and the page of the one on the screen now
static uint32_t     s_top_us;

#if DISP_JC8012
/* (chipguyhere) a background frame takes ~2 s here (the CPU's turn, paced to the panel), and a page, or a
 * button on the radar pressed or let go, waited for the one under way to finish: SET took seconds. so the ui
 * can give it up: s_bg_cancel = s_bg_kick, seen between strips and every 16 columns of a turn
 * (disp_bg_stop_fn). a frame given up is not shown; the frame not showing holds a part of it */
static volatile uint32_t s_bg_kick, s_bg_cancel;      // which frame was asked for; which is to be given up
static uint32_t s_bg_cancels;                          // for the 10 s log
static int  bg_cancelled(void) { return s_bg_cancel == s_bg_kick; }
static void bg_cancel(void) { if (s_bg_busy && s_bg_task) { s_bg_cancel = s_bg_kick; s_bg_cancels++; } }
void disp_bg_stop_fn(int (*fn)(void));                 // lcd_jc8012.c
#endif

static void bg_task(void *arg)
{
    (void)arg;
    canvas_t cv = { s_bg_px, 0, DISP_STRIP, 0, UI_LAYER_BG };
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int64_t t0 = esp_timer_get_time();
        busy_on(BUSY_BG);
        disp_bg_begin();
        for (int y = 0; y < GFX_PH; y += DISP_STRIP) {
#if DISP_JC8012
            if (bg_cancelled()) break;
#endif
            cv.y0 = y;
            cv.h = y + DISP_STRIP <= GFX_PH ? DISP_STRIP : GFX_PH - y;
            ui_strip(s_ui_bg, &cv);
            disp_strip(&cv);
        }
        busy_off(BUSY_BG);
#if DISP_JC8012
        if (bg_cancelled()) {                          // given up: not shown, and what the frame not showing holds is not known
            s_bg_gen++;
            s_bg_busy = 0;
            vTaskDelay(1);
            continue;
        }
#endif
        disp_frame_end();                              // up from the next panel frame on
        s_bg_gen++;                                    // the pages' band sums no longer say what the frames hold
        s_bg_view = s_ui_bg->view;
        s_bg_us += (uint32_t)(esp_timer_get_time() - t0);
        s_bg_frames++;
        s_bg_busy = 0;
        vTaskDelay(1);                                 // the idle task of this core gets its turn: the watchdog looks for it
    }
}

/* the screen comes up before the WiFi, from a short task on the second core: the bounce buffers
 * and the strips want internal RAM in one piece, which the WiFi and the tasks leave in bits, and the
 * library's fill task stays on the core it was started from */
static SemaphoreHandle_t s_early_done;

static void early_task(void *arg)
{
    (void)arg;
    disp_flip(s_cfg.flip);
    disp_init();
    s_top = (canvas_t){ heap_caps_calloc(GFX_PW * TOP_ROWS, sizeof(uint32_t), MALLOC_CAP_INTERNAL), 0, TOP_ROWS, 0, UI_LAYER_TOP };
    if (!s_top.px)                                     // (chipguyhere) the JC8012: its RAM may have no 80 KB in one piece; slower, but there
        s_top.px = heap_caps_calloc(GFX_PW * TOP_ROWS, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    s_bg_px = heap_caps_malloc(GFX_PW * DISP_STRIP * sizeof(uint32_t), MALLOC_CAP_INTERNAL);
    if (!s_bg_px)                                      // the JC8012 (chipguyhere): its fill's buffers leave no 40 KB in one piece;
        s_bg_px = heap_caps_malloc(GFX_PW * DISP_STRIP * sizeof(uint32_t), MALLOC_CAP_SPIRAM);   // the background has no deadline
    s_ui_bg = heap_caps_malloc(sizeof(ui_t), MALLOC_CAP_SPIRAM);
    xSemaphoreGive(s_early_done);
    vTaskDelete(NULL);
}

static void screen_early(void)
{
    s_early_done = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(early_task, "screen", 6144, NULL, 5, NULL, portNUM_PROCESSORS > 1 ? 1 : 0);
    xSemaphoreTake(s_early_done, portMAX_DELAY);
    /* its stack in the PSRAM: the internal RAM has none to spare, and nothing here runs with the cache off */
#if DISP_JC8012
    disp_bg_stop_fn(bg_cancelled);
#endif
    if (s_bg_px && s_ui_bg &&
        xTaskCreatePinnedToCoreWithCaps(bg_task, "background", 8192, NULL, 1, &s_bg_task, 1, MALLOC_CAP_SPIRAM) != pdPASS)
        s_bg_task = NULL;
    ESP_LOGI(TAG, "screen up before the WiFi: RAM %u free, %u in one piece, background %s, ui_t %u bytes, layer band in %s, strip in %s",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             s_bg_task ? "on the second core" : "NOT THERE", (unsigned)sizeof(ui_t),
             esp_ptr_internal(s_top.px) ? "RAM" : "PSRAM", esp_ptr_internal(s_bg_px) ? "RAM" : "PSRAM");
}

/* the planes move a pixel or a few at a time: their layer is made again at most this often. the sweep
 * is not in it, the fill turns that every panel frame. a pass between makes nothing and leaves the core
 * to the touch, the network and the pages */
#ifndef OVL_LAYER_MS
#define OVL_LAYER_MS 100
#endif
static uint32_t s_layer_at;
static uint32_t s_page_at, s_page_ms;                  // when the last page drawn here was done, and how long it took
#if DISP_JC8012
/* (chipguyhere) a page (settings, keyboard, lists) is drawn only when the ui's state has changed, and
 * only the 16-row bands that came out different are sent, into the frame that is showing (nothing moves on
 * a page, so no second frame to keep). a band costs a turn (~24 ms at 80 rows on the CPU, measured). the
 * first pass of a page is drawn whole into the frame not showing and flipped in */
void disp_page_begin(void);                            // lcd_jc8012.c: draw into the frame showing
void disp_page_turn(const canvas_t *cv);               // a band into it
void disp_page_flush(void);                            // the frame's cache synced, once after the bands
uint32_t disp_frames_now(void);                       // TEMP: panel frames so far (lcd_jc8012.c)
int  disp_twin(void);                                  // lcd_jc8012.c: the two frames hold the same picture
void disp_twin_set(int on);
void disp_twin_copy(void);                             // the hidden frame made the showing one
void disp_page_rect(const canvas_t *cv, int x0, int x1, int sync);   // a strip's columns x0 .. x1 - 1 only
/* changed bands kept in the PSRAM till all of a change is drawn, then turned one after another and the cache
 * written back once: turned as each was drawn, a full-width stripe (a pressed button's row) took tens of ms,
 * and the panel showed the button half new for a few frames */
enum { STAGE_BANDS = 25 };
static uint32_t *s_stage;
static int16_t   s_stage_y[STAGE_BANDS], s_stage_h[STAGE_BANDS], s_stage_x0[STAGE_BANDS], s_stage_x1[STAGE_BANDS];
static ui_t    *s_prev_ui;                             // the ui as the page showing was drawn from (ui_dirty_rects)
static uint32_t s_rects;                               // rectangles sent, for the 10 s log
static uint32_t s_touch_edge_at;                       // when a finger last went down or up (its sample's time)
static uint32_t s_lat_n, s_lat_sum, s_lat_max, s_lat_wait; // from that to the page on the glass, for the 10 s log
#define PAGE_BANDS ((GFX_PH + TOP_ROWS - 1) / TOP_ROWS)
static uint32_t s_band_sum[PAGE_BANDS];                // each band as the frame showing has it
static uint32_t s_band_gen = UINT32_MAX;
static int      s_page_view = -1, s_page_down = -1;
static uint32_t s_page_drawn;                          // when a page was last drawn
static uint32_t s_page_sent, s_page_passes, s_page_skips, s_sig_off;   // for the 10 s log
static ui_t    *s_sig[2];                              // the ui's state at the last draw and now, without the clock

static uint32_t band_sum(const uint32_t *p, size_t n)
{
    uint32_t a = 0x9E3779B9u, b = 0;
    for (size_t i = 0; i < n; i += 2) {
        a = (a ^ p[i]) * 0x01000193u;
        b = (b + p[i + 1]) ^ (b >> 7);
    }
    return a ^ (b * 0x85EBCA6Bu);
}

/* has anything a page shows changed since it was last drawn? the clock is cut to the cursor's blink */
static int page_changed(void)
{
    for (int i = 0; i < 2; i++)
        if (!s_sig[i] && !(s_sig[i] = heap_caps_calloc(1, sizeof(ui_t), MALLOC_CAP_SPIRAM))) return 1;
    ui_t *n = s_sig[1];
    memcpy(n, &s_ui, sizeof(ui_t));
    n->now = (s_ui.now / 500u) & 1u;
    n->beam = 0;
    memset(n->prof, 0, sizeof n->prof);
    memset(n->spot, 0, sizeof n->spot);                // where the radar draws the planes: made every pass, no page shows it
    memset(n->order, 0, sizeof n->order);
    n->n_order = 0;
    const uint8_t *a = (const uint8_t *)s_sig[0], *b = (const uint8_t *)n;
    for (size_t i = 0; i < sizeof(ui_t); i++)
        if (a[i] != b[i]) {
            s_sig_off = (uint32_t)i;                   // the first byte that changed, for the log: what keeps changing
            s_sig[1] = s_sig[0];
            s_sig[0] = n;
            return 1;
        }
    return 0;
}

/* (chipguyhere) a page that scrolls (lcd_jc8012.c: disp_scroll_*). while it is at rest its list is drawn
 * beforehand, its whole height, a viewport's height a pass, into a PSRAM buffer the fill gathers from; while
 * a finger drags it the fill does the scrolling and nothing here is drawn; when the finger lifts the page is
 * drawn at rest there and the gather stops with the panel frame that shows it */
uint16_t *disp_scroll_prepare(int gy0, int gy1, int hc, int bx0, int bx1);
void disp_scroll_put(const canvas_t *cv, int yc);
void disp_scroll_done(void);
void disp_scroll_to(int S);
void disp_scroll_stop_at_flip(void);
int  disp_scroll_on(void);
void disp_scroll_stats(uint32_t *n, uint32_t *late);
int  disp_copier_ok(void);
static uint32_t    s_sc_starts, s_sc_unready;
static ui_t       *s_sc_key[2], *s_sc_work;             // what the list was drawn from / is now; a copy to draw it from
static int         s_sc_valid, s_sc_stale, s_sc_step = -1, s_sc_view = -1, s_sc_stopping;
static ui_scroll_t s_sc_geo;
static uint32_t    s_sc_renders, s_sc_render_us, s_sc_drags;

// the ui without the clock and the finger: what a page shows apart from where a finger is and what it presses
static void strip_touch(ui_t *k)
{
    k->now = 0; k->beam = 0;
    memset(k->prof, 0, sizeof k->prof);
    memset(k->spot, 0, sizeof k->spot);
    k->t_down = k->t_x0 = k->t_y0 = k->t_x = k->t_y = k->t_moved = k->t_long = k->t_ns = 0;
    k->t_hit = k->t_reps = k->t_rpt = k->t_scroll0 = k->t_turn = 0;
    k->t_since = k->t_rep = 0;
    k->t_panel = 0;
    k->t_a0 = k->t_r0 = k->t_up0 = k->t_range0 = 0;
    k->fling = k->t_caught = 0;                        // the momentum (ui.c): where the list is, not what it shows
    k->fl_y = 0; k->fl_at = 0; k->fl_v = k->fl_pos = 0;
    memset(k->t_sx, 0, sizeof k->t_sx);
    memset(k->t_sy, 0, sizeof k->t_sy);
}

// the ui as the list looks: without the clock, the finger, the scroll, a toast
static void sc_strip(ui_t *k)
{
    strip_touch(k);
    k->list_scroll = k->wifi_scroll = k->setup_scroll = 0;
    k->toast_until = 0;
    memset(k->toast, 0, sizeof k->toast);
    k->menu_until = 0;
}

static int sc_key_changed(void)
{
    for (int i = 0; i < 2; i++)
        if (!s_sc_key[i] && !(s_sc_key[i] = heap_caps_calloc(1, sizeof(ui_t), MALLOC_CAP_SPIRAM))) return 1;
    memcpy(s_sc_key[1], &s_ui, sizeof(ui_t));
    sc_strip(s_sc_key[1]);
    if (!memcmp(s_sc_key[0], s_sc_key[1], sizeof(ui_t))) return 0;
    ui_t *t = s_sc_key[0]; s_sc_key[0] = s_sc_key[1]; s_sc_key[1] = t;
    return 1;
}

// a viewport's height of the list; when it is whole, the fill may use it
static void sc_render_step(void)
{
    const int64_t t0 = esp_timer_get_time();
    const ui_scroll_t *g = &s_sc_geo;
    const int gy0 = GFX_Z(g->y0), gy1 = GFX_Z(g->y1), h = gy1 - gy0;
    const int maxe = (g->max + 1) & ~1, hc = GFX_Z(maxe) + h;
    const int step = (g->y1 - g->y0) & ~1;             // even: 5/2 of it is a whole number of glass rows
    if (!s_sc_work && !(s_sc_work = heap_caps_malloc(sizeof(ui_t), MALLOC_CAP_SPIRAM))) { s_sc_step = -1; return; }
    if (s_sc_step == 0) {
        const int bx0 = g->bx1 > g->bx0 ? GFX_Z(g->bx0) - 1 : 0, bx1 = g->bx1 > g->bx0 ? GFX_Z(g->bx1) + 2 : 0;
        if (!disp_scroll_prepare(gy0, gy1, hc, bx0, bx1)) { s_sc_step = -1; return; }
    }
    memcpy(s_sc_work, &s_ui, sizeof(ui_t));
    const uint32_t now = s_sc_work->now;
    sc_strip(s_sc_work);
    s_sc_work->now = now;
    ui_scrolled(s_sc_work, s_sc_step);
    canvas_t sub = { s_top.px, 0, TOP_ROWS, 0, UI_LAYER_BG };
    for (int y = gy0 / TOP_ROWS * TOP_ROWS; y < gy1; y += TOP_ROWS) {
        sub.y0 = y;
        sub.h = y + TOP_ROWS <= GFX_PH ? TOP_ROWS : GFX_PH - y;
        ui_strip(s_sc_work, &sub);
        disp_scroll_put(&sub, GFX_Z(s_sc_step));
        touch_sample(0);
    }
    memset(s_top.px, 0, GFX_PW * TOP_ROWS * sizeof(uint32_t));
    s_sc_render_us += (uint32_t)(esp_timer_get_time() - t0);
    if (GFX_Z(s_sc_step) + h >= hc) {
        disp_scroll_done();
        s_sc_valid = 1;
        s_sc_step = -1;
        s_sc_renders++;
    } else {
        s_sc_step += step;
    }
}

static int  s_ring_in;                                 // the ring under the finger is in the list (sc_ring, below)
static void sc_ring(int on);

// 1: the page is being scrolled by the fill, not drawn here
static int sc_tick(void)
{
    ui_scroll_t g;
    const int can = ui_scroll_info(&s_ui, &g);
    const int drag = (s_ui.t_down && s_ui.t_moved) || s_ui.fling;   // coasting after the lift: still the fill's
    if (disp_scroll_on()) {
        if (can && drag && s_ui.view == s_sc_view && !s_sc_stopping) {
            disp_scroll_to(GFX_Z(g.scroll));
            return 1;
        }
        if (!s_sc_stopping) {                          // let go: the page drawn at rest there, the gather stops with it
            ESP_LOGI(TAG, "scrolling stops: %s", !can ? "the page no longer scrolls" : !drag ? (s_ui.t_down ? "the finger stopped counting as a drag" : "the finger lifted, or the list came to rest")
                                              : "another page");
            s_sc_stopping = 1;
            disp_scroll_stop_at_flip();
            s_page_view = -1;                          // drawn whole into the frame not showing, and flipped in
        }
        return 0;
    }
    s_sc_stopping = 0;
    if (s_ring_in) sc_ring(0);                         // the gather is off: the list as it was, for the next drag
    if (!can) { s_sc_valid = 0; s_sc_step = -1; s_ring_in = 0; return 0; }
    /* another page, or the list another length: what is drawn is no use. only what it says changed (the
     * setup shows the network's state, the plane list its planes): still usable for a drag, drawn again over
     * itself between drags; the page is drawn at rest anyway when the finger lifts */
    if (s_sc_view != s_ui.view || memcmp(&g.y0, &s_sc_geo.y0, sizeof(int) * 2) || g.max != s_sc_geo.max) {
        s_sc_valid = 0;
        s_sc_stale = 1;
        s_sc_step = -1;
        sc_key_changed();
    } else if (sc_key_changed()) {
        s_sc_stale = 1;
        if (s_sc_step > 0) s_sc_step = 0;              // a drawing under way: from the top again
    }
    if (s_sc_valid && drag) {
        if (s_sc_step >= 0) s_sc_step = -1;            // a drawing under way stops (the list is at worst partly new)
        if (s_ui.t_down) sc_ring(1);                   // the ring under the finger, into the list (coasting: none)
        disp_scroll_to(GFX_Z(g.scroll));
        s_sc_drags++;
        s_sc_starts++;
        return 1;
    }
    if (drag) s_sc_unready++;                          // a drag, the list not drawn yet: the page is drawn as before
    if ((!s_sc_valid || s_sc_stale) && !s_ui.t_down) { // at rest: a piece of the list a pass
        if (s_sc_step < 0) { s_sc_step = 0; s_sc_geo = g; s_sc_view = s_ui.view; s_sc_stale = 0; }
        sc_render_step();
    }
    return 0;
}

/* (chipguyhere) any page: when only the finger changed (where it is, what it presses), the rectangles that
 * shows in: a stripe across the page where it came down (a pressed row, button or tab lights there) and the
 * ring under it, before and after. -1: something else changed too (a value, a tab, a toast), all of it */
static ui_t *s_tr[2];
static int touch_rects(const ui_t *a, const ui_t *b, ui_rect_t *r, int max)
{
    if (max < 4) return -1;
    for (int i = 0; i < 2; i++)
        if (!s_tr[i] && !(s_tr[i] = heap_caps_malloc(sizeof(ui_t), MALLOC_CAP_SPIRAM))) return -1;
    if (ui_until(a->toast_until, a->now) != ui_until(b->toast_until, b->now)) return -1;
    memcpy(s_tr[0], a, sizeof(ui_t));
    memcpy(s_tr[1], b, sizeof(ui_t));
    strip_touch(s_tr[0]);
    strip_touch(s_tr[1]);
    if (memcmp(s_tr[0], s_tr[1], sizeof(ui_t))) return -1;
    int n = 0;
    const int pa = a->t_down && !a->t_moved, pb = b->t_down && !b->t_moved;
    if (pa != pb || (pa && (a->t_hit != b->t_hit || a->t_y0 != b->t_y0))) {   // what is pressed: lit or not
        if (pa) r[n++] = (ui_rect_t){ 0, (int16_t)(a->t_y0 - 24), (int16_t)GFX_W, (int16_t)(a->t_y0 + 24) };
        if (pb) r[n++] = (ui_rect_t){ 0, (int16_t)(b->t_y0 - 24), (int16_t)GFX_W, (int16_t)(b->t_y0 + 24) };
    }
    if (a->t_down != b->t_down || (b->t_down && (a->t_x != b->t_x || a->t_y != b->t_y))) {   // the ring under the finger
        if (a->t_down) r[n++] = (ui_rect_t){ (int16_t)(a->t_x - 12), (int16_t)(a->t_y - 12), (int16_t)(a->t_x + 13), (int16_t)(a->t_y + 13) };
        if (b->t_down) r[n++] = (ui_rect_t){ (int16_t)(b->t_x - 12), (int16_t)(b->t_y - 12), (int16_t)(b->t_x + 13), (int16_t)(b->t_y + 13) };
    }
    return n;
}

/* (chipguyhere) the ring under the finger, while the fill scrolls the page: drawn into the list where the
 * finger came down (what is under a dragging finger stays under it, so the ring goes with it), and taken
 * out again before the list is used for anything else. on: 1 draw it, 0 the list as it was */
static int s_ring_x, s_ring_y, s_ring_s;
static void sc_ring(int on)
{
    if (!s_sc_valid || !s_sc_work || on == s_ring_in) return;
    if (on) {                                          // where the finger is now, and the page's scroll then (even:
        ui_scroll_t now_g;                             // the list was drawn at even scrolls, 5/2 of them whole rows)
        s_ring_x = s_ui.t_x;
        s_ring_y = s_ui.t_y;
        s_ring_s = ui_scroll_info(&s_ui, &now_g) ? now_g.scroll & ~1 : 0;
    }
    const ui_scroll_t *g = &s_sc_geo;
    const int gy0 = GFX_Z(g->y0), gy1 = GFX_Z(g->y1);
    int a = GFX_Z(s_ring_y - 13), b = GFX_Z(s_ring_y + 14);
    if (a < gy0) a = gy0;
    if (b > gy1) b = gy1;
    s_ring_in = on;
    if (a >= b) return;
    memcpy(s_sc_work, &s_ui, sizeof(ui_t));
    const uint32_t now = s_sc_work->now;
    sc_strip(s_sc_work);                               // the list as drawn beforehand
    s_sc_work->now = now;
    if (on) { s_sc_work->t_down = s_sc_work->t_moved = 1; s_sc_work->t_x = s_ring_x; s_sc_work->t_y = s_ring_y; }
    ui_scrolled(s_sc_work, s_ring_s);
    canvas_t sub = { s_top.px, 0, TOP_ROWS, 0, UI_LAYER_BG };
    for (int y = a / TOP_ROWS * TOP_ROWS; y < b; y += TOP_ROWS) {
        sub.y0 = y;
        sub.h = y + TOP_ROWS <= GFX_PH ? TOP_ROWS : GFX_PH - y;
        ui_strip(s_sc_work, &sub);
        const int r0 = y > a ? y : a, r1 = y + sub.h < b ? y + sub.h : b;
        const canvas_t part = { sub.px + (size_t)(r0 - y) * GFX_PW, r0, r1 - r0, 0, UI_LAYER_BG };
        disp_scroll_put(&part, GFX_Z(s_ring_s));
    }
    memset(s_top.px, 0, GFX_PW * TOP_ROWS * sizeof(uint32_t));
    disp_scroll_done();
}

static int s_replay, s_replay_view;                    // (chipguyhere) staged bands flipped in, still to go into the other frame; their page
static void stage_turn(int nst)
{
    for (int i = 0; i < nst; i++) {
        const canvas_t st = { s_stage + (size_t)i * GFX_PW * TOP_ROWS, s_stage_y[i], s_stage_h[i], 0, UI_LAYER_BG };
        disp_page_rect(&st, s_stage_x0[i], s_stage_x1[i], 0);
    }
    if (nst) disp_page_flush();
}
/* the change last flipped in, into the frame that showed before it: the two alike again. on the radar the
 * background draws that frame whole anyway: not needed there. 0 done or nothing to do */
static int rects_replay(void)
{
    if (!s_replay) return 0;
    if (!disp_bg_ready()) return 1;                    // the flip is not up yet
    if (s_ui.view != VIEW_MAIN && s_ui.view == s_replay_view) {   // (another page: drawn whole anyway)
        disp_bg_begin();
        stage_turn(s_replay);
        disp_twin_set(1);
    }
    s_replay = 0;
    return 0;
}

/* the rectangles rc (layout pixels) of the ui as it is now, into the frame showing: each one's bands drawn and
 * kept in the PSRAM, then all of them turned at once and the cache written back once. 0: more than the stage
 * holds, nothing sent (the caller draws it whole) */
static int rects_to_glass(const ui_rect_t *rc, int nrc)
{
    if (rects_replay()) return 0;                      // the last change is not in both frames yet
    if (!s_stage) s_stage = heap_caps_malloc((size_t)STAGE_BANDS * GFX_PW * TOP_ROWS * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!s_stage) return 0;
    int nst = 0;
    canvas_t sub = { s_top.px, 0, TOP_ROWS, 0, UI_LAYER_BG };
    for (int i = 0; i < nrc && nst >= 0; i++) {
        int gx0 = GFX_Z(rc[i].x0) - 2, gx1 = GFX_Z(rc[i].x1) + 3, gy0 = GFX_Z(rc[i].y0) - 2, gy1 = GFX_Z(rc[i].y1) + 3;
        if (gx0 < 0) gx0 = 0;
        if (gy0 < 0) gy0 = 0;
        if (gx1 > GFX_PW) gx1 = GFX_PW;
        if (gy1 > GFX_PH) gy1 = GFX_PH;
        for (int y = gy0; y < gy1; y += TOP_ROWS) {
            if (nst >= STAGE_BANDS) { nst = -1; break; }   // more than fits: the whole page instead
            sub.y0 = y;
            sub.h = y + TOP_ROWS <= gy1 ? TOP_ROWS : gy1 - y;
            ui_strip(&s_ui, &sub);
            memcpy(s_stage + (size_t)nst * GFX_PW * TOP_ROWS, sub.px, (size_t)GFX_PW * sub.h * sizeof(uint32_t));
            s_stage_y[nst] = (int16_t)sub.y0;
            s_stage_h[nst] = (int16_t)sub.h;
            s_stage_x0[nst] = (int16_t)gx0;
            s_stage_x1[nst] = (int16_t)gx1;
            nst++;
            touch_sample(0);
        }
        s_rects++;
    }
    memset(s_top.px, 0, GFX_PW * TOP_ROWS * sizeof(uint32_t));
    if (nst < 0) return 0;
    /* (chipguyhere, 2026-11-14) into the frame that does not show, flipped in whole at a panel frame's start,
     * and drawn into the other one after (rects_replay). into the frame showing it took 20-48 ms and the glass
     * showed the button half done, in the cache's order */
    const int64_t ts = esp_timer_get_time();
    const uint32_t f0 = disp_frames_now();
    const int copied = !disp_twin();
    if (copied) disp_twin_copy();                      // the hidden frame as the glass shows it, first
    const int64_t tc = esp_timer_get_time();
    disp_bg_begin();
    stage_turn(nst);
    disp_twin_set(0);                                  // they differ till the replay
    disp_frame_end();                                  // up from the next panel frame's start
    s_replay = nst;
    s_replay_view = s_ui.view;
    const int64_t te = esp_timer_get_time();
    s_send_us += (uint32_t)(te - ts);
    ESP_LOGI(TAG, "TEMP rects to the hidden frame: %d bands, %s%u us drawn, %u panel frames meanwhile",
             nst, copied ? "copied first, " : "", (unsigned)(te - tc), (unsigned)(disp_frames_now() - f0));
    return 1;
}

static void page_draw(uint32_t now, int poked)
{
    int fresh = s_band_gen != s_bg_gen || s_page_view != s_ui.view;   // what the frame holds is not known
    s_page_passes++;
    if (!page_changed() && !fresh && !poked && now - s_page_drawn < 1000u) {
        s_page_skips++;
        s_page_at = now;                               // the next look in 30 ms
        return;
    }
    s_bg_busy = 1;                                     // the background task keeps off meanwhile
    s_page_down = s_ui.t_down;
    const int64_t t0 = esp_timer_get_time();
    if (!s_prev_ui) s_prev_ui = heap_caps_malloc(sizeof(ui_t), MALLOC_CAP_SPIRAM);
    /* the ui says which rectangles changed (the keyboard does): only their rows are drawn, only their
     * columns turned into the frame showing, only the panel rows they cover written back */
    ui_rect_t rc[8];
    int nrc = !fresh && s_prev_ui ? ui_dirty_rects(s_prev_ui, &s_ui, rc, 8) : -1;
    if (nrc < 0 && !fresh && s_prev_ui) nrc = touch_rects(s_prev_ui, &s_ui, rc, 8);   // any page: the finger alone
    if (nrc >= 0 && rects_to_glass(rc, nrc)) {
        memcpy(s_prev_ui, &s_ui, sizeof(ui_t));
        memset(s_band_sum, 0, sizeof s_band_sum);      // the bands' sums no longer say what the frame holds
        goto done;
    }
    /* more than the ui can place: the whole page into the frame not showing, flipped in at a panel frame's
     * start. written into the frame showing (as before), a pressed button showed half old, half new */
    fresh = 1;
    s_replay = 0;                                      // (chipguyhere) drawn whole: no change to replay
    if (fresh) disp_bg_begin();
    else disp_page_begin();
    canvas_t sub = { s_top.px, 0, TOP_ROWS, 0, UI_LAYER_BG };   // the moving layer's band: internal RAM, idle on a page
    /* the changed bands are kept in the PSRAM till all are drawn, then turned one after another: turned as
     * each was drawn, a key reached the glass in pieces over the ~100 ms of drawing */
    if (!s_stage) s_stage = heap_caps_malloc((size_t)STAGE_BANDS * GFX_PW * TOP_ROWS * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    int sent = 0, staged = 0;
    for (int y = 0, k = 0; y < GFX_PH; y += TOP_ROWS, k++) {
        sub.y0 = y;
        sub.h = y + TOP_ROWS <= GFX_PH ? TOP_ROWS : GFX_PH - y;
        ui_strip(&s_ui, &sub);
        touch_sample(0);                               // the touch read as it should be (every 15 ms): a lift needs four
                                                       // empty reads (the library's debounce), once a pass made it ~0.5 s
        const uint32_t sum = band_sum(sub.px, (size_t)GFX_PW * sub.h);
        if (!fresh && s_band_sum[k] == sum) continue;  // the frame has it already
        s_band_sum[k] = sum;
        sent++;
        if (!fresh && s_stage && staged < STAGE_BANDS) {   // the frame showing: kept for the end
            memcpy(s_stage + (size_t)staged * GFX_PW * TOP_ROWS, sub.px, (size_t)GFX_PW * sub.h * sizeof(uint32_t));
            s_stage_y[staged] = (int16_t)sub.y0;
            s_stage_h[staged] = (int16_t)sub.h;
            staged++;
            continue;
        }
        const int64_t ts = esp_timer_get_time();       // the frame not showing (a page's first pass): at once
        disp_page_turn(&sub);
        s_send_us += (uint32_t)(esp_timer_get_time() - ts);
    }
    {
        const int64_t ts = esp_timer_get_time();
        for (int i = 0; i < staged; i++) {
            const canvas_t st = { s_stage + (size_t)i * GFX_PW * TOP_ROWS, s_stage_y[i], s_stage_h[i], 0, UI_LAYER_BG };
            disp_page_turn(&st);
        }
        s_send_us += (uint32_t)(esp_timer_get_time() - ts);
    }
    if (sent) {
        const int64_t ts = esp_timer_get_time();
        disp_page_flush();
        s_send_us += (uint32_t)(esp_timer_get_time() - ts);
    }
    memset(s_top.px, 0, GFX_PW * TOP_ROWS * sizeof(uint32_t));   // the moving layer's band again: from nothing
    if (fresh) {
        disp_frame_end();                              // up from the next panel frame on
        s_band_gen = s_bg_gen;
        s_page_view = s_ui.view;
    }
    if (s_prev_ui) memcpy(s_prev_ui, &s_ui, sizeof(ui_t));
    s_page_sent += (uint32_t)sent;
done:
    if (poked && s_touch_edge_at) {                    // a finger down or up: how long till it shows
        s_lat_wait += (uint32_t)(t0 / 1000) - s_touch_edge_at;   // of it, before the drawing began
        const uint32_t lat = (uint32_t)(esp_timer_get_time() / 1000) - s_touch_edge_at;
        s_lat_n++;
        s_lat_sum += lat;
        if (lat > s_lat_max) s_lat_max = lat;
    }
    s_bg_view = s_ui.view;
    s_bg_us += (uint32_t)(esp_timer_get_time() - t0);
    s_bg_frames++;
    s_bg_busy = 0;
    s_page_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    s_page_at = s_page_drawn = (uint32_t)(esp_timer_get_time() / 1000);
}
#endif

#if DISP_JC8012
/* (chipguyhere, 2026-11-14) where a ui pass's time goes, for the 10 s log: each mark adds the time since the
 * last one to its section. the average a pass, and the slowest pass section by section. wall time: the fill
 * (top priority, this core) preempting the pass is in whichever section it hit */
enum { PS_INPUT, PS_PRESS, PS_LIST, PS_UIFRAME, PS_BGCOPY, PS_LDRAW, PS_LPACK, PS_PACE, PS_LEND, PS_PAGE, PS_REST, PS_WAIT, PS_N };
static const char *const PS_NAME[PS_N] = { "input", "press", "list", "ui_frame", "bg copy", "layer draw", "layer pack",
                                           "panel wait", "layer end", "page", "rest", "wait" };
static uint32_t s_ps[PS_N], s_ps_sum[PS_N], s_ps_worst[PS_N], s_ps_worst_total, s_ps_n, s_ps_view = 0;
static int64_t  s_ps_at;
uint32_t disp_pace_us(void);                           // lcd_jc8012.c
static void ps_mark(int k)
{
    const int64_t t = esp_timer_get_time();
    if (s_ps_at) s_ps[k] += (uint32_t)(t - s_ps_at);
    s_ps_at = t;
}
static void ps_pass_end(void)
{
    uint32_t total = 0;
    for (int k = 0; k < PS_N; k++) { total += s_ps[k]; s_ps_sum[k] += s_ps[k]; }
    if (total > s_ps_worst_total) { s_ps_worst_total = total; memcpy(s_ps_worst, s_ps, sizeof s_ps); s_ps_view = (uint32_t)s_ui.view; }
    memset(s_ps, 0, sizeof s_ps);
    s_ps_n++;
}
static void ps_log(void)
{
    char a[400], w[400];
    int na = 0, nw = 0;
    const uint32_t n = s_ps_n ? s_ps_n : 1;
    for (int k = 0; k < PS_N; k++) {
        na += snprintf(a + na, sizeof a - (size_t)na, " %s %u.%u", PS_NAME[k], (unsigned)(s_ps_sum[k] / n / 1000u), (unsigned)(s_ps_sum[k] / n / 100u % 10u));
        if (s_ps_worst[k] >= 1000u) nw += snprintf(w + nw, sizeof w - (size_t)nw, " %s %u", PS_NAME[k], (unsigned)(s_ps_worst[k] / 1000u));
    }
    a[na] = w[nw] = 0;
    ESP_LOGI(TAG, "pass ms, average of %u:%s", (unsigned)s_ps_n, a);
    ESP_LOGI(TAG, "pass ms, the slowest (%u, view %u):%s", (unsigned)(s_ps_worst_total / 1000u), (unsigned)s_ps_view, w);
    memset(s_ps_sum, 0, sizeof s_ps_sum);
    memset(s_ps_worst, 0, sizeof s_ps_worst);
    s_ps_worst_total = s_ps_n = 0;
}
#define PS_MARK(k) ps_mark(k)
#else
#define PS_MARK(k) ((void)0)
#endif

#if DISP_JC8012
/* (chipguyhere) a button on the radar (SET, LIST, the colours, a row) pressed or let go: drawn at once, a
 * stripe across where the finger came down, into the frame showing (as the pages' rectangles), the moving
 * layer goes on over it. the background under way shows the button as it was, so it is given up first,
 * and the next one starts from the pressed state. before, the press waited for the frame under way and then
 * a whole frame of its own: ~2-4 s */
static int s_mp_shown, s_mp_y;                         // the button the glass shows pressed (ui_pressed), where
static uint32_t s_mp_draws;                            // for the 10 s log
static void main_press(void)
{
    const int want = ui_pressed(&s_ui);
    static int s_mp_down;
    if (s_mp_down && !s_ui.t_down && s_ui.view == VIEW_MAIN) bg_cancel();   // a lift (a plane picked, a turn done): the
    s_mp_down = s_ui.t_down;                                                 // frame under way shows the old state
    if (s_ui.view != VIEW_MAIN || s_view_up != VIEW_MAIN) { s_mp_shown = want; s_mp_y = s_ui.t_y0; return; }
    if (want == s_mp_shown && (!want || s_ui.t_y0 == s_mp_y)) return;
    if (s_bg_busy) { bg_cancel(); return; }            // given up between its strips: the next pass draws it
    if (!disp_bg_ready()) return;                      // a frame on its way up: after it
    ui_rect_t rc[2];
    int n = 0;
    if (s_mp_shown) rc[n++] = (ui_rect_t){ 0, (int16_t)(s_mp_y - 24), (int16_t)GFX_W, (int16_t)(s_mp_y + 24) };
    if (want && (!s_mp_shown || s_ui.t_y0 != s_mp_y))
        rc[n++] = (ui_rect_t){ 0, (int16_t)(s_ui.t_y0 - 24), (int16_t)GFX_W, (int16_t)(s_ui.t_y0 + 24) };
    if (n) rects_to_glass(rc, n);                      // too big for the stage: the background brings it
    s_mp_shown = want;
    s_mp_y = s_ui.t_y0;
    s_mp_draws++;
}
#endif

/* where things are in the middle of the time a layer will show (frame_layered's lead): the build's time, as
 * measured, and the gap between builds */
static uint32_t s_lay_build_ms = 20, s_gap_ms = OVL_LAYER_MS;
static int64_t  s_prev_build;

#if DISP_JC8012
/* (chipguyhere, 2026-11-14) the moving layer built in a task of its own, beside the ui task on its core and
 * below it: with 48 planes a build took ~650 ms, and inline it held the ui (touch, buttons, pages) that long.
 * the ui hands it a copy of its state when it is free (as the background task gets one); it draws into a band
 * of its own in the PSRAM (s_top stays the ui's: pages, pressed buttons). a page coming up gives the build
 * under way up (between bands), as nothing of it would show */
void disp_pace_new_build(void);                        // lcd_jc8012.c
void disp_pace_end_phase(void);
static TaskHandle_t s_builder;
static ui_t        *s_ui_top;                          // the builder's copy of the ui
static canvas_t     s_top_b;                           // its band, kept at 0 between uses (ovl.c)
static volatile int s_build_busy, s_build_view_up, s_build_turning;
static volatile uint32_t s_build_gen, s_build_cancel;
enum { BS_DRAW, BS_PACK, BS_WAIT, BS_END, BS_N };
static uint32_t s_bs_n, s_bs_cancels, s_bs_sum[BS_N], s_bs_worst[BS_N], s_bs_worst_total;   // for the 10 s log

static void builder_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint32_t my = s_build_gen;
        const int64_t start = esp_timer_get_time();
        uint32_t bs[BS_N] = { 0 };
        ui_top_t ti;
        ui_top_info(s_ui_top, &ti);
        if (s_build_view_up != VIEW_MAIN) ti.any = ti.beam = 0;   // (see frame_layered)
        ovl_beam_t b = { ti.beam, ti.cx, ti.cy, ti.r, ti.prim, ti.focus, ti.turn_ms, ti.holes };
        for (int k = 0; k < ti.holes; k++) {
            b.hx0[k] = (int16_t)ti.hx0[k]; b.hy0[k] = (int16_t)ti.hy0[k];
            b.hx1[k] = (int16_t)ti.hx1[k]; b.hy1[k] = (int16_t)ti.hy1[k];
        }
        disp_pace_new_build();                         // paced only if the fill was late lately
        const uint32_t pace0 = disp_pace_us();
        ovl_build_begin(&b);
        int cancelled = 0;
        int64_t t = esp_timer_get_time(), t2;
        for (int y = ti.any ? ti.y0 / TOP_ROWS * TOP_ROWS : GFX_PH; y < ti.y1; y += TOP_ROWS) {
            if (s_build_cancel == my) { cancelled = 1; break; }
            s_top_b.y0 = y;
            s_top_b.h = y + TOP_ROWS <= GFX_PH ? TOP_ROWS : GFX_PH - y;
            busy_on(BUSY_DRAW);
            ui_top_strip(s_ui_top, &s_top_b);
            busy_off(BUSY_DRAW);
            t2 = esp_timer_get_time(); bs[BS_DRAW] += (uint32_t)(t2 - t); t = t2;
            busy_on(BUSY_PACK);
            ovl_build_band(&s_top_b, ti.x0, ti.x1);
            busy_off(BUSY_PACK);
            t2 = esp_timer_get_time(); bs[BS_PACK] += (uint32_t)(t2 - t); t = t2;
        }
        const uint32_t pace1 = disp_pace_us();
        busy_on(BUSY_END);
        disp_pace_end_phase();
        if (!cancelled) ovl_build_end(&b);             // given up: the slot is simply taken again next time
        busy_off(BUSY_END);
        t2 = esp_timer_get_time();
        bs[BS_END] += (uint32_t)(t2 - t);
        {   /* the panel waits inside the bands and the end, moved to their own */
            const uint32_t in_bands = pace1 - pace0, in_end = disp_pace_us() - pace1;
            bs[BS_WAIT] = in_bands + in_end;
            bs[BS_PACK] -= in_bands < bs[BS_PACK] ? in_bands : bs[BS_PACK];
            bs[BS_END] -= in_end < bs[BS_END] ? in_end : bs[BS_END];
        }
        const uint32_t total = (uint32_t)(t2 - start);
        if (cancelled) {
            s_bs_cancels++;
        } else {
            s_top_us += total;
            s_layer_at = (uint32_t)(t2 / 1000);        // from its end: the next is due OVL_LAYER_MS later
            if (!s_build_turning) s_lay_build_ms = (s_lay_build_ms * 3u + total / 1000u) / 4u;
            else s_prev_build = 0;                     // the gap after a turn is not a planes' gap
            s_bs_n++;
            for (int k = 0; k < BS_N; k++) s_bs_sum[k] += bs[k];
            if (total > s_bs_worst_total) { s_bs_worst_total = total; memcpy(s_bs_worst, bs, sizeof bs); }
        }
        s_build_busy = 0;
    }
}

static void builder_start(void)
{
    static int tried;
    if (tried) return;
    tried = 1;
    s_ui_top = heap_caps_malloc(sizeof(ui_t), MALLOC_CAP_SPIRAM);
    s_top_b = (canvas_t){ heap_caps_calloc(GFX_PW * TOP_ROWS, sizeof(uint32_t), MALLOC_CAP_SPIRAM), 0, TOP_ROWS, 0, UI_LAYER_TOP };
    if (!s_ui_top || !s_top_b.px ||
        xTaskCreatePinnedToCoreWithCaps(builder_task, "layer", 8192, NULL, 3, &s_builder, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        s_builder = NULL;
        ESP_LOGW(TAG, "no task for the moving layer: it is built in the ui task");
    }
}

static void builder_log(void)
{
    const uint32_t n = s_bs_n ? s_bs_n : 1;
    ESP_LOGI(TAG, "layer builds: %u, %u given up; ms a build: draw %u, pack %u, panel wait %u, end %u; the slowest %u "
             "(draw %u, pack %u, panel wait %u, end %u)", (unsigned)s_bs_n, (unsigned)s_bs_cancels,
             (unsigned)(s_bs_sum[BS_DRAW] / n / 1000u), (unsigned)(s_bs_sum[BS_PACK] / n / 1000u),
             (unsigned)(s_bs_sum[BS_WAIT] / n / 1000u), (unsigned)(s_bs_sum[BS_END] / n / 1000u),
             (unsigned)(s_bs_worst_total / 1000u), (unsigned)(s_bs_worst[BS_DRAW] / 1000u), (unsigned)(s_bs_worst[BS_PACK] / 1000u),
             (unsigned)(s_bs_worst[BS_WAIT] / 1000u), (unsigned)(s_bs_worst[BS_END] / 1000u));
    s_bs_n = s_bs_cancels = s_bs_worst_total = 0;
    memset(s_bs_sum, 0, sizeof s_bs_sum);
    memset(s_bs_worst, 0, sizeof s_bs_worst);
}
#endif

static void frame_layered(uint32_t now)
{
    const uint32_t period = disp_frame_us();
    const int64_t start = esp_timer_get_time();
    if (!s_bg_busy && disp_bg_ready()) s_view_up = s_bg_view;   // the last background drawn is the one up
    const int direct = s_ui.view != VIEW_MAIN;         // a page with nothing moving on it: drawn here, see below
#if DISP_JC8012
    PS_MARK(PS_INPUT);                                 // the loop's top: touch, commands, the sky copied
    builder_start();
    if (direct) bg_cancel();                           // the radar's background under way is no use to a page
    if (direct && s_build_busy && s_build_view_up == VIEW_MAIN) s_build_cancel = s_build_gen;   // nor the radar's layer
    main_press();                                      // before bg_go: a background is given up, or starts pressed
    if (!s_bg_busy) rects_replay();                    // the last change into the other frame too, once it is up
    PS_MARK(PS_PRESS);
#endif
    const int bg_go = s_bg_task && !direct && !s_bg_busy && disp_bg_ready();
    /* a finger turning the compass: the layer is the ring alone (ui_top_info), made every pass so the
     * ring follows the finger at the panel's pace. the planes and the map catch up when it lets go */
    const int turning = !direct && s_ui.t_down && s_ui.t_turn == 1 && s_ui.t_moved;
    const int build = direct || turning || now - s_layer_at >= OVL_LAYER_MS;
    /* a page drawn here leaves the core free after it for half as long as it took, and at least 50 ms:
     * drawn back to back, the menus kept the core so busy its idle task never ran, and the watchdog
     * restarted the board */
    uint32_t rest = s_page_ms / 2 > 50u ? s_page_ms / 2 : 50u;
#if DISP_JC8012
    rest = 30u;                                        // (chipguyhere) a look at the ui's state; drawn only if it changed
#else
    static int s_page_down = -1;
#endif
    /* (chipguyhere) a finger down or up is drawn at once, not after the rest: a key wants its answer.
     * at most twice a press, so the idle task still gets its turn */
    const int poked = s_ui.t_down != s_page_down;
#if DISP_JC8012
    const int scrolling = direct && sc_tick();         // (chipguyhere) the fill scrolls it: nothing drawn here
    const int draw_here = direct && !scrolling && !s_bg_busy && disp_bg_ready() && (poked || now - s_page_at >= rest);
    PS_MARK(PS_LIST);                                  // sc_tick: a list drawn beforehand, a piece a pass
#else
    const int draw_here = direct && !s_bg_busy && disp_bg_ready() && (poked || now - s_page_at >= rest);
#endif
    if (!build && !bg_go && !draw_here) {
        touch_sample(0);
        return;
    }
    /* where things are in the middle of the time this layer will show: it is up after this build and a
     * panel frame, and stays till the next one, so ahead by a frame, the build's time and half the time
     * between layers, all as measured. a fixed guess left the planes' glow 20-30 degrees behind the beam
     * once the layer came only every ~350 ms */
#if DISP_JC8012
    const int kick = build && (!s_builder || !s_build_busy);   // the builder busy: the next pass asks again
#else
    const int kick = build;
#endif
    if (kick && !direct && !turning) {                // the ring alone is quick: not counted
        const int64_t t = esp_timer_get_time();
        if (s_prev_build) s_gap_ms = (s_gap_ms * 3u + (uint32_t)((t - s_prev_build) / 1000)) / 4u;
        s_prev_build = t;
    }
    const uint32_t lead = kick && !direct ? s_lay_build_ms + s_gap_ms / 2u : 0u;
    ui_frame(&s_ui, now + period / 1000u + lead);
    PS_MARK(PS_UIFRAME);
    if (bg_go) {                                       // the background is free for another frame: this state
        memcpy(s_ui_bg, &s_ui, sizeof(ui_t));
#if DISP_JC8012
        s_bg_kick++;                                   // before busy: a cancel names this frame
#endif
        s_bg_busy = 1;
        xTaskNotifyGive(s_bg_task);
    }
    PS_MARK(PS_BGCOPY);
    if (!kick) goto drawn;
#if DISP_JC8012
    if (s_builder) {                                   // this state to the builder, which makes the layer meanwhile
        memcpy(s_ui_top, &s_ui, sizeof(ui_t));
        s_build_view_up = s_view_up;
        s_build_turning = turning;
        s_build_gen++;
        s_build_busy = 1;
        xTaskNotifyGive(s_builder);
        goto drawn;
    }
#endif
    ui_top_t ti;
    ui_top_info(&s_ui, &ti);
    /* the moving layer only over the radar page that is up: leaving a menu, the sweep waited for the
     * radar's background to replace the menu's, instead of turning over the menu meanwhile */
    if (s_view_up != VIEW_MAIN) ti.any = ti.beam = 0;
    ovl_beam_t b = { ti.beam, ti.cx, ti.cy, ti.r, ti.prim, ti.focus, ti.turn_ms, ti.holes };
    for (int k = 0; k < ti.holes; k++) {
        b.hx0[k] = (int16_t)ti.hx0[k]; b.hy0[k] = (int16_t)ti.hy0[k];
        b.hx1[k] = (int16_t)ti.hx1[k]; b.hy1[k] = (int16_t)ti.hy1[k];
    }
#if DISP_JC8012
    { void disp_pace_new_build(void); disp_pace_new_build(); }   // paced this build only if the fill was late lately
    const uint32_t pace0 = disp_pace_us();
#endif
    ovl_build_begin(&b);                               // an empty layer where nothing moves: the rows say so
    for (int y = ti.any ? ti.y0 / TOP_ROWS * TOP_ROWS : GFX_PH; y < ti.y1; y += TOP_ROWS) {
        s_top.y0 = y;
        s_top.h = y + TOP_ROWS <= GFX_PH ? TOP_ROWS : GFX_PH - y;
        ui_top_strip(&s_ui, &s_top);
        PS_MARK(PS_LDRAW);
        ovl_build_band(&s_top, ti.x0, ti.x1);
#if DISP_JC8012
        touch_sample(0);                               // (chipguyhere) a heavy sky's layer outlasts a frame: a lift needs 4 empty reads
#endif
        PS_MARK(PS_LPACK);
    }
#if DISP_JC8012
    const uint32_t pace1 = disp_pace_us();
#endif
    ovl_build_end(&b);
    PS_MARK(PS_LEND);
#if DISP_JC8012
    {   /* the panel waits inside the bands and the end: moved from where they were counted to their own */
        const uint32_t in_bands = pace1 - pace0, in_end = disp_pace_us() - pace1;
        s_ps[PS_PACE] += in_bands + in_end;
        s_ps[PS_LPACK] -= in_bands < s_ps[PS_LPACK] ? in_bands : s_ps[PS_LPACK];
        s_ps[PS_LEND] -= in_end < s_ps[PS_LEND] ? in_end : s_ps[PS_LEND];
    }
#endif
    s_top_us += (uint32_t)(esp_timer_get_time() - start);
    s_layer_at = (uint32_t)(esp_timer_get_time() / 1000);   // from its end: the core has that long to itself before the next
    if (!turning) s_lay_build_ms = (s_lay_build_ms * 3u + (uint32_t)((esp_timer_get_time() - start) / 1000)) / 4u;
    else s_prev_build = 0;                             // the gap after a turn is not a planes' gap
drawn:
    touch_sample(0);
    /* the settings, the keyboard, the lists: nothing moves there, this core has the time the moving
     * layer would take, and a key wants its answer now. so the page is drawn here, from the ui's own
     * state, not handed to the second core's background task (which takes ~1 s a frame under the fill) */
#if DISP_JC8012
    /* (chipguyhere) the gate is draw_here: before, any pass on a page drew it, the rest never applied */
    PS_MARK(PS_REST);
    if (draw_here) { busy_on(BUSY_PAGE); page_draw(now, poked); busy_off(BUSY_PAGE); }
    PS_MARK(PS_PAGE);
#else
    if (direct && !s_bg_busy && disp_bg_ready()) {
        s_bg_busy = 1;                                 // the background task keeps off meanwhile
        s_page_down = s_ui.t_down;
        const int64_t t0 = esp_timer_get_time();
        canvas_t cv = { s_top.px, 0, TOP_ROWS, 0, UI_LAYER_BG };
        const int rows = TOP_ROWS;
        disp_bg_begin();
        for (int y = 0; y < GFX_PH; y += rows) {
            cv.y0 = y;
            cv.h = y + rows <= GFX_PH ? rows : GFX_PH - y;
            ui_strip(&s_ui, &cv);
            const int64_t ts = esp_timer_get_time();
            disp_strip(&cv);
            s_send_us += (uint32_t)(esp_timer_get_time() - ts);
            touch_sample(0);                           // a finger is read between strips, as the original loop does
        }
        memset(s_top.px, 0, GFX_PW * TOP_ROWS * sizeof(uint32_t));   // the moving layer's band again: from nothing
        disp_frame_end();
        s_bg_view = s_ui.view;
        s_bg_us += (uint32_t)(esp_timer_get_time() - t0);
        s_bg_frames++;
        s_bg_busy = 0;
        s_page_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        s_page_at = (uint32_t)(esp_timer_get_time() / 1000);
    }
#endif
}
#endif

#if (DISP_JC8012 || DISP_WAVE7) && CONFIG_FREERTOS_USE_TRACE_FACILITY
/* (chipguyhere) where the internal RAM is, once a minute: each region of it (free, in one piece,
 * at the least) and each task's stack (bytes never used, and whether it is in the PSRAM) */
static void mem_report(void)
{
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    static TaskStatus_t st[40];
    UBaseType_t n = uxTaskGetSystemState(st, 40, NULL);
    char line[512];
    int at = 0;
    for (UBaseType_t i = 0; i < n && at < (int)sizeof line - 40; i++)
        at += snprintf(line + at, sizeof line - at, "%s %u%s, ", st[i].pcTaskName, (unsigned)st[i].usStackHighWaterMark,
                       esp_ptr_external_ram(st[i].pxStackBase) ? "P" : "");
    ESP_LOGI(TAG, "stacks, bytes never used (P: in the PSRAM): %s", line);
}
#endif

static void ui_task(void *arg)
{
    (void)arg;
    s_ui_task = xTaskGetCurrentTaskHandle();
    fr_cfg_t c;
    lock();
    c = s_cfg;
    unlock();
#if LCD_PROFILES
    static const int ORDER[LCD_PROFILES] = LCD_ORDER;  // what BOOT goes through, the first one if none was picked
    int screen = store_load_screen();                  // which CYD revision the screen is
    if (screen < 0 || screen >= LCD_PROFILES) screen = ORDER[0];
    disp_profile(screen);
    s_screen = screen;
#endif
#if RADAR_OVERLAY
    uint32_t *work = s_top.px;                         // screen_early made them
    ovl_waiter(xTaskGetCurrentTaskHandle());
    ovl_planner_start(disp_frame_us, 0, 6);            // the next frame's busiest chunks, beside and above this task
#else
#if DISP_LCD_SPI || DISP_LCD_RGB || DISP_LCD_DSI || DISP_ILI9486
    disp_flip(c.flip);
#endif
    disp_init();
    uint32_t *work = heap_caps_malloc(GFX_PW * DISP_STRIP * sizeof(uint32_t), MALLOC_CAP_INTERNAL);
#endif
    if (!work || !ui_init(&s_ui, now_ms())) {
        ESP_LOGE(TAG, "not enough memory for the screen: RAM %u free, %u in one piece, heap %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
        vTaskDelete(NULL);
    }
    ui_set_cfg(&s_ui, &c);
    lock();
    s_ui.light = s_light_cfg;                          // what lights the LED on the back
    s_ui.opt = s_opt;
    s_ui.help = s_help;                                // counted on the site's map: SET's COUNT ME
    s_ui.bare = s_bare;                                // the radar without its panel, as it was left
    s_ui.backlight = s_backlight;                      // the screen's light, as it was left
    s_ui.night = s_night;                              // and at night
    s_ui.alerts = s_alerts;                            // the weather's warnings shown
    s_ui.units = s_units;                              // and its units
    s_ui.names = s_names;                              // the map's names on the dark looks
    unlock();
    led_init(light_red_gpio(&s_ui.light));
    s_ui.map = &s_map;                                 // the fetch task makes it, the scope draws it
    ui_clock_us = now_us;                              // the layers get timed, the 10 s log says where it goes
    ui_version = FLIGHT_VERSION;                       // ABOUT says it, the note compares the site's with it
    ui_beta = FLIGHT_BETA;                             // and BETA after it
#if LCD_PROFILES && RADAR_HAS_BUTTON
    ui_screen_name = disp_profile_name();              // the screen offers the other ones, by BOOT. none: the site does
#endif
    button_init();
    esp_task_wdt_add(NULL);
    tp_init();
#if RADAR_OVERLAY
    /* the Waveshare 7/7B (chipguyhere): a capacitive GT911 that counts the glass's own pixels, so how
     * its numbers fall on the screen is known: the layout is the glass scaled down (config.h GFX_SCALE),
     * turned with the picture when it is turned. the five-cross alignment is never started. a port of
     * this build to a resistive panel (an XPT2046 and the like) wants it back: take this block out and
     * the stored-alignment check below runs as on the other boards */
    s_ui.cal = (touch_cal_t){ (float)GFX_W / (float)GFX_PW, 0, 0, 0, (float)GFX_H / (float)GFX_PH, 0 };
    if (c.flip) {                                      // as screen_flip turns it
        touch_cal_t *k = &s_ui.cal;
        k->a = -k->a; k->b = -k->b; k->c = (float)(GFX_W - 1) - k->c;
        k->d = -k->d; k->e = -k->e; k->f = (float)(GFX_H - 1) - k->f;
    }
#elif RADAR_TOUCH_CAL
    int have = store_load_cal(&s_ui.cal);
    const float want = (float)GFX_W / (float)tp_raw_span();
    float sx = fabsf(s_ui.cal.a) + fabsf(s_ui.cal.b), sy = fabsf(s_ui.cal.d) + fabsf(s_ui.cal.e);
    if (!have || sx < want / 3 || sx > want * 3 || sy < want / 3 || sy > want * 3) ui_start_calibration(&s_ui);
#endif
    ESP_LOGI(TAG, "%s %s, %dx%d, free heap %u", RADAR_NAME, FLIGHT_VERSION, GFX_W, GFX_H,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));

    uint32_t frames = 0, t_stat = now_ms(), busy_us = 0, dirty = 0;
    int light = 0;
    int dark = 0;                                      // the sun is down at home: SET's NIGHT light, if it is on
    uint32_t sun_at = 0;                               // when the sun was last worked out, 0: never
#if RADAR_OVERLAY
    /* (chipguyhere) the gaps between passes, for the 10 s log: the longest, and those over 100 ms by what
     * the fetch task was doing as the pass that made it began */
    uint32_t gap_max = 0, gap_long[FP_N] = { 0 };
    uint8_t  gap_fp = FP_IDLE, pass_fp = FP_IDLE;
    int64_t  pass_at = 0;
#endif
    for (;;) {
        uint32_t now = now_ms();
#if RADAR_OVERLAY
        {
            const int64_t t = esp_timer_get_time();
            if (pass_at) {
                const uint32_t g = (uint32_t)((t - pass_at) / 1000);
                if (g > gap_max) { gap_max = g; gap_fp = pass_fp; }
                if (g > 100u) gap_long[pass_fp]++;
            }
            pass_at = t;
            pass_fp = s_fp;
        }
#endif
        ui_cmd_t cmd;
        memset(&cmd, 0, sizeof cmd);

        int ev = button_poll(now);
#if LCD_PROFILES
        /* a short BOOT press starts the screen as the next CYD revision: on the touch alignment
         * page (the first thing after flashing) or in the first seconds after power on, both say
         * so on the screen. held on the alignment page it still skips the alignment */
        if (ev == BTN_SHORT && (s_ui.view == VIEW_CAL || now < UI_SCREEN_PICK_MS)) {
            int at = 0;
            while (at < LCD_PROFILES - 1 && ORDER[at] != screen) at++;
            screen = ORDER[(at + 1) % LCD_PROFILES];
            pick_screen(screen, now, "BOOT");
            ev = 0;
        }
        if (s_screen_set >= 0) {                       // the site says which
            screen = s_screen_set;
            s_screen_set = -1;
            pick_screen(screen, now, "the site");
        }
#endif
        if (ev) ui_button(&s_ui, ev == BTN_LONG ? 2 : 1, &cmd);
        if (s_skip_align) {
            s_skip_align = 0;
            if (s_ui.view == VIEW_CAL) ui_button(&s_ui, 1, &cmd);
        }
        touch_sample(1);
#if DISP_JC8012
        scrolltest(now);
#endif
        int finger = 0;                                // a sample comes every frame, with a finger or without
        for (int i = 0; i < s_nts; i++) finger |= s_ts[i].down;
        if (finger || ev) {                            // a finger or BOOT: what the LED shows is seen, it goes out
            lock();
            light_touch(&s_light);
            unlock();
        }
        for (int i = 0; i < s_nts; i++) {
            const tsample_t *s = &s_ts[i];
            ui_touch(&s_ui, s->down, s->x, s->y, s->rx, s->ry, s->t, &cmd);
#if DISP_JC8012
            static int s_was_down;
            if (s->down != s_was_down) { s_was_down = s->down; s_touch_edge_at = s->t; }
#endif
        }
        s_nts = 0;

        if (s_cfg_new) {                               // from the site: it is the master now
            int was = c.flip;
            lock();
            c = s_cfg;
            s_cfg_new = 0;
            unlock();
            ui_set_cfg(&s_ui, &c);
            if (c.flip != was) screen_flip(c.flip);
        }
        if (cmd.cfg) {
            int flipped = s_ui.cfg.flip != c.flip;
            lock();
            s_ui.cfg.lat = s_cfg.lat;                  // home and its name come from s_cfg: the site, or home_find
            s_ui.cfg.lon = s_cfg.lon;
            memcpy(s_ui.cfg.place, s_cfg.place, sizeof s_ui.cfg.place);
            memcpy(s_ui.cfg.country, s_cfg.country, sizeof s_ui.cfg.country);
            s_cfg = s_ui.cfg;
            unlock();
            c = s_ui.cfg;
            dirty = now ? now : 1;
            if (flipped) screen_flip(c.flip);
        }
        if (cmd.refetch) xTaskNotifyGive(s_fetch);
        if (cmd.scan) net_scan_request(now);
        if (cmd.wifi) wifi_from_screen();
        if (cmd.home) {                                // typed on the screen: the fetch task finds it
            lock();
            snprintf(s_home_ask, sizeof s_home_ask, "%s", s_ui.home_text);
            unlock();
            xTaskNotifyGive(s_fetch);
        }
        if (cmd.watch) {                               // a new watch list typed on the screen
            char text[WATCH_TEXT];
            lock();
            watch_set(&s_watch, s_ui.watch_text);
            watch_text(&s_watch, text, sizeof text);
            s_watch_asked = 0;
            unlock();
            store_save_watch(text);
            ESP_LOGI(TAG, "watch list from the screen: %s", text[0] ? text : "none");
            xTaskNotifyGive(s_fetch);
        }
        if (s_said_new) {                              // what the fetch task found: a home, a watched plane
            lock();
            snprintf(s_ui.toast, sizeof s_ui.toast, "%s", s_said);
            s_ui.toast_until = now + s_said_ms;
            s_said_new = 0;
            unlock();
        }
        if (s_watch_pick) {                            // a watched one came in range: picked, unless one is
            if (!s_ui.sel) s_ui.sel = s_watch_pick;
            s_watch_pick = 0;
        }
        net_scan_poll(now);
        if (dirty && now - dirty > 3000u) {           // holding + is one flash write, not twenty
            store_save_cfg(&s_ui.cfg);
            dirty = 0;
        }
        if (cmd.save_cal) store_save_cal(&s_ui.cal);
        if (s_light_new) {                             // the site's LED settings
            lock();
            s_ui.light = s_light_cfg;
            s_light_new = 0;
            unlock();
        }
        if (cmd.light) {                               // SET's LED rows
            lock();
            s_light_cfg = s_ui.light;
            unlock();
            store_save_light(&s_ui.light);
        }
        if (s_opt_new) {                               // the site's radar options
            lock();
            s_ui.opt = s_opt;
            s_opt_new = 0;
            unlock();
        }
        if (cmd.opt) {                                 // the gear's rows
            lock();
            s_opt = s_ui.opt;
            unlock();
            store_save_opt(&s_ui.opt);
        }
        if (s_help_new) {                              // counted on the map: the site's answer
            lock();
            s_ui.help = s_help;
            s_help_new = 0;
            unlock();
        }
        if (cmd.help) {                                // SET's COUNT ME
            lock();
            s_help = s_ui.help;
            unlock();
            store_save_help(s_ui.help);
        }
        if (s_bare_new) {                              // the site's: the radar's panel away or back
            lock();
            s_ui.bare = s_bare;
            s_bare_new = 0;
            unlock();
        }
        if (s_wxpage_new) {                            // the console's: the weather's page
            lock();
            s_ui.wx_page = s_wxpage;
            s_wxpage_new = 0;
            unlock();
        }
        if (cmd.bare) {                                // the radar's three dots
            lock();
            s_bare = s_ui.bare;
            unlock();
            store_save_bare(s_ui.bare);
        }
        if (s_backlight_new) {                         // the site's: the screen's light
            lock();
            s_ui.backlight = s_backlight;
            s_backlight_new = 0;
            unlock();
        }
        if (cmd.backlight) {                           // SET's BACKLIGHT
            lock();
            s_backlight = s_ui.backlight;
            unlock();
            store_save_backlight(s_ui.backlight);
        }
        if (s_night_new) {                             // the site's: the night's light
            lock();
            s_ui.night = s_night;
            s_night_new = 0;
            unlock();
            sun_at = 0;                                // where the sun is: now, not in a minute
        }
        if (cmd.night) {                               // SET's NIGHT
            lock();
            s_night = s_ui.night;
            unlock();
            store_save_night(s_ui.night);
            sun_at = 0;
        }
        if (s_alerts_new) {                            // the site's: the weather's warnings shown
            lock();
            s_ui.alerts = s_alerts;
            s_alerts_new = 0;
            unlock();
        }
        if (cmd.alerts) {                              // SET's ALERT rows
            lock();
            s_alerts = s_ui.alerts;
            unlock();
            store_save_alerts(s_ui.alerts);
        }
        if (s_units_new) {                             // the site's: the weather's units
            lock();
            s_ui.units = s_units;
            s_units_new = 0;
            unlock();
        }
        if (cmd.units) {                               // SET's UNITS rows
            lock();
            s_units = s_ui.units;
            unlock();
            store_save_units(&s_ui.units);
        }
        if (s_names_new) {                             // the site's: the map's names
            lock();
            s_ui.names = s_names;
            s_names_new = 0;
            unlock();
        }
        if (cmd.names) {                               // SET's MAP NAMES
            lock();
            s_names = s_ui.names;
            unlock();
            store_save_names(s_ui.names);
        }
        if (s_pick_new) {                              // tapped on the page: picked here too
            s_pick_new = 0;
            s_ui.sel = s_sel_told = s_pick;
        }
        if (page_there()) {                            // and what a finger did here goes there
            if (cmd.cfg || cmd.opt || cmd.help || cmd.bare || cmd.backlight || cmd.night || cmd.alerts || cmd.units || cmd.names)
                say_cfg();
            if (s_ui.sel != s_sel_told) {
                s_sel_told = s_ui.sel;
                printf("OWLOS SEL %06x\n", (unsigned)s_ui.sel);
            }
            if (s_ui.wx_page != s_wxpage_told) {       // the weather's page, turned here or asked for: the page's twin turns too
                s_wxpage_told = s_ui.wx_page;
                printf("OWLOS WXPAGE %d\n", s_wxpage_told);
            }
            if ((s_ui.view == VIEW_CAL) != s_align_told) {   // its screen waits for crosses: the page says so
                s_align_told = s_ui.view == VIEW_CAL;
                printf("OWLOS ALIGN %d\n", s_align_told);
            }
        }
        uint8_t rgb[3];
        lock();
        if (cmd.light_test || s_light_test) {
            light_test(&s_light, now);
            ESP_LOGI(TAG, "LED test from %s: red, green, blue", s_light_test ? "the site" : "the screen");
            s_light_test = 0;
        }
        light_colour(&s_light, &s_ui.light, now, rgb);
        int red = light_red_gpio(&s_ui.light);
        unlock();
        led_red(red);                                  // SET's RED ON row or the site moved it
        led_set(rgb);

        lock();
        memcpy(&s_ui.snap, &s_sky, sizeof s_sky);
        net_t *n = &s_ui.net;
        n->configured = s_sec.ssid[0] != 0;
        n->has_key = s_sec.key[0] != 0;
        n->demo = (uint8_t)s_demo_on;
        n->wifi = (uint8_t)net_up();
        n->why = (uint8_t)net_why();
        net_ssid(n->ssid, sizeof n->ssid);
        snprintf(n->server, sizeof n->server, "%s", s_server);
        n->http = s_http;
        n->fetched = s_fetched;
        n->utc = net_utc();
        s_ui.watch = s_watch;                          // SET says what the watched planes do
        memcpy(s_ui.newest, s_newest, sizeof s_ui.newest);   // and ABOUT what the site has
        memcpy(s_ui.home_cc, s_home_cc, sizeof s_ui.home_cc);   // the weather's units, AUTO: home's country's own
        s_ui.wx = s_wx_cur >= 0 ? &s_wx[s_wx_cur] : NULL;     // the WEATHER look's forecast, the one not being filled
        unlock();
        net_ip(n->ip, sizeof n->ip);
        int naps = 0;
        n->scan = (uint8_t)net_scan_get(n->aps, &naps);   // what the WiFi page shows
        n->n_aps = (uint8_t)naps;
        lock();                                        // the list may change on the console's task
        for (int k = 0; k < naps; k++) n->aps[k].kept = nets_find(&s_nets, n->aps[k].ssid) != NULL;
        unlock();

#if DISP_HELPER
        /* the second core draws every view and every look, the strips only read (ui_frame sets up what
         * they share). the 3.5" (pairs, no PSRAM): the radar's and the weather's looks only, with their
         * pages, the square maps want its memory. it is kept through those pages: taken at the radar and
         * given back at every page, its 38 KB cut the heap into pieces the map's tiles then could not use */
#if DISP_HELPER_HALF || CONFIG_SPIRAM
        helper_set(1);
#else
        helper_set(s_ui.cfg.look == LOOK_RADAR || s_ui.cfg.look == LOOK_WEATHER);
#endif
        const int pair = !DISP_HELPER_HALF && s_helper;  // every other strip is the second core's
        const int step = pair ? 2 * DISP_STRIP : DISP_STRIP;
#elif !RADAR_OVERLAY
        const int step = DISP_STRIP;
#endif
        int64_t t0 = esp_timer_get_time();
#if RADAR_OVERLAY
        frame_layered(now);
#else
        ui_frame(&s_ui, now);
#if DISP_HELPER
        uint32_t waited = 0;                           // this frame's, for where the halves' strips are cut
#endif
        for (int sy0 = 0; sy0 < GFX_PH; sy0 += step) {
            canvas_t cv = { work, sy0, sy0 + DISP_STRIP <= GFX_PH ? DISP_STRIP : GFX_PH - sy0, 0, 0 };
#if DISP_HELPER
            const int y1 = sy0 + DISP_STRIP, under = pair && y1 < GFX_PH;
            const int half = DISP_HELPER_HALF && s_helper && cv.h > 1;
            const int top = !half ? cv.h : s_half_top < cv.h ? s_half_top : cv.h / 2;
            if (under) {                               // the strip under it, on the other core meanwhile
                s_help_cv.y0 = y1;
                s_help_cv.h = y1 + DISP_STRIP <= GFX_PH ? DISP_STRIP : GFX_PH - y1;
                xSemaphoreGive(s_help_go);
            } else if (half) {                         // the lower half of this one, into this canvas
                s_help_cv = (canvas_t){ work + top * GFX_PW, sy0 + top, cv.h - top, 1, 0 };
                xSemaphoreGive(s_help_go);
            }
            canvas_t mine = { work, sy0, top, 0, 0 };  // this core's: the strip, or its upper half
            ui_strip(&s_ui, &mine);
            if (under || half) {
                uint32_t tw = now_us();
                xSemaphoreTake(s_help_done, portMAX_DELAY);
                tw = now_us() - tw;
                s_help_wait_us += tw;
                waited += tw;
            }
            uint32_t ts = now_us();
            if (under) disp_strip2(&cv, s_help_cv.h);  // both in one transfer
            else disp_strip(&cv);                      // halves: both of them, as one strip
#else
            ui_strip(&s_ui, &cv);
            uint32_t ts = now_us();
            disp_strip(&cv);                           // packs it to 565, waits for the strip before to be out
#endif
            s_send_us += now_us() - ts;
            touch_sample(0);
        }
        disp_frame_end();
#if DISP_HELPER
        /* halves: the cut a row lower when this core waited for the second over 1.5 ms of the frame, a row
         * higher when it hardly waited at all, the second core may have then. a quarter of a strip at least */
        if (DISP_HELPER_HALF && s_helper) {
            if (waited > 1500u && s_half_top < DISP_STRIP * 3 / 4) s_half_top++;
            else if (waited < 300u && s_half_top > DISP_STRIP / 4) s_half_top--;
        }
#endif
#endif
        busy_us += (uint32_t)(esp_timer_get_time() - t0);
        frames++;
        /* the weather's warnings: one that came or got worse this frame (ui_frame worked them out) lights the
         * LED, which stays lit as SET's HOW LONG says; "while it lasts" is while any is up */
        if (s_ui.alert_new) {                          // what, how much and in how many hours: nothing of where
            static const char *const KIND[WXA_KINDS] = { "storm", "pressure", "air", "radiation" };
            const wx_alert_t *a = &s_ui.alert;
            for (int k = 0; k < WXA_KINDS; k++) {
                if (!(s_ui.alert_new & (1u << k))) continue;
                char v[24];
                if (k == WXA_STORM && a->thunder) snprintf(v, sizeof v, "thunder");
                else if (k == WXA_STORM) snprintf(v, sizeof v, "gusts %d km/h", a->value[k]);
                else if (k == WXA_PRESSURE) snprintf(v, sizeof v, "%+.1f hPa", (double)a->value[k] / 10.0);
                else if (k == WXA_AIR) snprintf(v, sizeof v, "index %d", a->value[k]);
                else snprintf(v, sizeof v, "%d nSv/h", a->value[k]);
                ESP_LOGI(TAG, "weather warning: %s, %s, %s in %d h", KIND[k], a->level[k] >= WXA_RED ? "red" : "amber", v, a->hour[k]);
            }
        }
        lock();
        if (s_ui.alert_new) light_event(&s_light, LIGHT_WEATHER, now);
        light_hold(&s_light, LIGHT_WEATHER, wx_alert_worst(&s_ui.alert) >= 0, now);
        unlock();
        /* the night: the sun 3 degrees under the horizon at home, about a quarter of an hour after sunset
         * till as long before sunrise. worked out once a minute from the time the network gave, half a
         * degree either way so a sun that hangs at the edge does not blink the screen. no time: day */
        if (RADAR_HAS_DIM && s_ui.night && (!sun_at || now - sun_at >= 60000u)) {
            sun_at = now ? now : 1;
            const uint32_t utc = net_utc();
            const int was = dark;
            if (utc) {
                const float el = sun_elevation(s_ui.cfg.lat, s_ui.cfg.lon, utc);
                dark = dark ? el < -2.5f : el < -3.5f;
            }
            if (dark != was) ESP_LOGI(TAG, "the sun at home: %s", dark ? "down, the night's light" : "up, the day's light");
        }
        /* the light: up from dark at the start, then to SET's BACKLIGHT, or NIGHT while the sun is down
         * if that is dimmer, when either changes, 10% a frame. where it does not turn down, full as before */
        int light_to = ui_backlight_pct(s_ui.backlight);
        if (dark && s_ui.night && ui_backlight_pct(s_ui.night) < light_to) light_to = ui_backlight_pct(s_ui.night);
        if (!RADAR_HAS_DIM) light_to = 100;
        if (light != light_to) {
            if (light < light_to) light = light + 10 < light_to ? light + 10 : light_to;
            else light = light - 10 > light_to ? light - 10 : light_to;
            disp_backlight(light);
            if (light == light_to) ESP_LOGI(TAG, "the screen's light: %d%%", light);   // once a change, when it is there
        }

        if (now - t_stat >= 10000u) {
            uint32_t f = frames ? frames : 1;
            ESP_LOGI(TAG, "%u fps, %u ms a frame, view %d, %d planes, heap %u", (unsigned)(frames * 1000u / (now - t_stat)),
                     (unsigned)(busy_us / 1000u / f), s_ui.view, s_ui.snap.n, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
#if RADAR_OVERLAY
            {
                char by[160];
                int at = 0;
                for (int k = 0; k < FP_N; k++)
                    if (gap_long[k]) at += snprintf(by + at, sizeof by - (size_t)at, " %s %u", FP_NAME[k], (unsigned)gap_long[k]);
                by[at] = 0;
                uint32_t reused, fresh;
                net_get_stats(&reused, &fresh);
                ESP_LOGI(TAG, "pacing: longest gap between ui passes %u ms (fetch: %s); over 100 ms:%s; lock: %u waits, %u ms "
                         "the longest (held by %s, fetch: %s), %u ms in all; net: %u requests on a kept connection, %u connections made",
                         (unsigned)gap_max, FP_NAME[gap_fp], at ? by : " none", (unsigned)s_lw_n, (unsigned)(s_lw_max_us / 1000u),
                         s_lw_by[0] ? s_lw_by : "-", FP_NAME[s_lw_fp], (unsigned)(s_lw_sum_us / 1000u), (unsigned)reused, (unsigned)fresh);
#if DISP_JC8012
                ps_log();
                builder_log();
#endif
                gap_max = 0;
                memset(gap_long, 0, sizeof gap_long);
                s_lw_max_us = s_lw_sum_us = s_lw_n = 0;
                s_lw_by = "";
            }
#endif
#if CONFIG_SPIRAM
            // the heap above counts the PSRAM too: what the WiFi, the second core and the AES under TLS want is this
            ESP_LOGI(TAG, "RAM %u free, %u in one piece, %u at the least", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
#endif
#if (DISP_JC8012 || DISP_WAVE7) && CONFIG_FREERTOS_USE_TRACE_FACILITY
            static uint8_t s_mem_n;
            if (s_mem_n++ % 6 == 0) mem_report();
#endif
            const uint32_t *pr = s_ui.prof;                // where a frame's time goes, ms: what to make faster
            ESP_LOGI(TAG, "ms a frame: background %u, disc %u, compass %u, planes %u, panel %u, the rest %u, "
                          "sending %u, touch %u",
                     (unsigned)(pr[0] / 1000u / f), (unsigned)(pr[1] / 1000u / f), (unsigned)(pr[2] / 1000u / f),
                     (unsigned)(pr[3] / 1000u / f), (unsigned)(pr[4] / 1000u / f), (unsigned)(pr[5] / 1000u / f),
                     (unsigned)(s_send_us / 1000u / f), (unsigned)(s_touch_us / 1000u / f));
#if DISP_HELPER
            if (s_helper && DISP_HELPER_HALF)          // the layers above are this core's strips only
                ESP_LOGI(TAG, "the second core draws: waited for it %u ms a frame, its stack %u bytes free, %d rows of %d this core's",
                         (unsigned)(s_help_wait_us / 1000u / f), (unsigned)uxTaskGetStackHighWaterMark(s_helper), s_half_top, DISP_STRIP);
            else if (s_helper)
                ESP_LOGI(TAG, "the second core draws: waited for it %u ms a frame, its stack %u bytes free",
                         (unsigned)(s_help_wait_us / 1000u / f), (unsigned)uxTaskGetStackHighWaterMark(s_helper));
            s_help_wait_us = 0;
#endif
#if RADAR_OVERLAY
            ESP_LOGI(TAG, "moving layer: %u ms a frame to make; background: %u frames, %u ms each",
                     (unsigned)(s_top_us / 1000u / f), (unsigned)s_bg_frames,
                     (unsigned)(s_bg_frames ? s_bg_us / 1000u / s_bg_frames : 0));
#if DISP_JC8012
            ESP_LOGI(TAG, "touch: %u backgrounds given up, %u radar presses drawn at once", (unsigned)s_bg_cancels, (unsigned)s_mp_draws);
            s_bg_cancels = s_mp_draws = 0;
#endif
#if DISP_JC8012
            if (s_ui.view != VIEW_MAIN)
                ESP_LOGI(TAG, "page: %u looks, %u with nothing changed, %u bands sent (%d a whole page), last change at byte %u of ui_t; "
                              "finger down/up to the glass: %u times, %u ms on average (%u before the drawing began), %u at most; %u rectangles",
                         (unsigned)s_page_passes, (unsigned)s_page_skips, (unsigned)s_page_sent, PAGE_BANDS, (unsigned)s_sig_off,
                         (unsigned)s_lat_n, (unsigned)(s_lat_n ? s_lat_sum / s_lat_n : 0),
                         (unsigned)(s_lat_n ? s_lat_wait / s_lat_n : 0), (unsigned)s_lat_max, (unsigned)s_rects);
            s_rects = 0;
            s_page_sent = s_page_passes = s_page_skips = 0;
            s_lat_n = s_lat_sum = s_lat_max = s_lat_wait = 0;
            {
                uint32_t gn, gl;
                disp_scroll_stats(&gn, &gl);
                if (s_ui.view != VIEW_MAIN || gn)
                    ESP_LOGI(TAG, "scrolling: list drawn %u times (%u ms), %u drags, %u passes dragging before the list was ready, "
                                  "%u chunks gathered, %u gathered late%s",
                             (unsigned)s_sc_renders, (unsigned)(s_sc_render_us / 1000), (unsigned)s_sc_starts, (unsigned)s_sc_unready,
                             (unsigned)gn, (unsigned)gl, disp_copier_ok() ? "" : ", THE GDMA COPIER IS GONE");
                s_sc_renders = s_sc_render_us = s_sc_drags = s_sc_starts = s_sc_unready = 0;
            }
#endif
            {
                ovl_stats_t os;
                ovl_stats(&os, 1);
                ESP_LOGI(TAG, "moving layer: %u panel frames, %u made, %u never shown, %u too full, %u words at most",
                         (unsigned)os.frames, (unsigned)os.built, (unsigned)os.skipped, (unsigned)os.overflow,
                         (unsigned)os.words_max);
                ESP_LOGI(TAG, "moving layer: %u busiest rows a layer made whole beforehand", (unsigned)(os.built ? os.busy / os.built : 0));
                ESP_LOGI(TAG, "moving layer: %u chunks composed beforehand on this core, %u of them used by the fill",
                         (unsigned)os.pre_made, (unsigned)os.pre_used);
                ovl_log_chunks();
                ESP_LOGI(TAG, "rows lost by where the beam was, 12 3 6 9 o'clock and between: %u %u %u | %u %u %u | %u %u %u | %u %u %u",
                         (unsigned)os.late_by_beam[0], (unsigned)os.late_by_beam[1], (unsigned)os.late_by_beam[2],
                         (unsigned)os.late_by_beam[3], (unsigned)os.late_by_beam[4], (unsigned)os.late_by_beam[5],
                         (unsigned)os.late_by_beam[6], (unsigned)os.late_by_beam[7], (unsigned)os.late_by_beam[8],
                         (unsigned)os.late_by_beam[9], (unsigned)os.late_by_beam[10], (unsigned)os.late_by_beam[11]);
                ESP_LOGI(TAG, "moving layer in the fill: sweep %u us a frame, layer %u us a frame, %u words a frame, "
                              "%u rows with the sweep alone",
                         (unsigned)(os.frames ? os.beam_cyc / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / os.frames : 0),
                         (unsigned)(os.frames ? os.layer_cyc / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / os.frames : 0),
                         (unsigned)(os.frames ? os.words / os.frames : 0), (unsigned)os.layer_dropped);
            }
            disp_log();
            s_top_us = s_bg_us = s_bg_frames = 0;
            // the 7B's backlight is said again in disp_log, with what the board reports round it
#endif
            memset(s_ui.prof, 0, sizeof s_ui.prof);
            if (now / 1000u - s_ran_kept >= 60u) {     // a reset from here on knows how long this run was
                s_ran_kept = now / 1000u;
                store_save_ran(s_ran_kept);
            }
            s_send_us = s_touch_us = 0;
            frames = 0;
            busy_us = 0;
            t_stat = now;
        }
        esp_task_wdt_reset();
#if RADAR_OVERLAY
        /* till the next panel frame starts (ovl.c). a frame begun while this pass ran is let go: the pass
         * always waits for a fresh one, so this core is always free for part of a frame. with many planes
         * the moving layer takes longer than a frame, and with the fetch task's TLS the core had no time
         * left for its idle task: the watchdog restarted the board (2026-11-11). the layer then changes
         * every second or third panel frame; the sweep keeps every one, the fill draws it from the clock */
        PS_MARK(PS_REST);                              // after the drawing: the LED, the log, the stats
        busy_off(BUSY_UI);
        ulTaskNotifyTake(pdTRUE, 0);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        busy_on(BUSY_UI);
        PS_MARK(PS_WAIT);
#if DISP_JC8012
        ps_pass_end();
#endif
#else
        vTaskDelay(1);
#endif
    }
}

void app_main(void)
{
#if RADAR_OVERLAY
    logbuf_start();                                    // (chipguyhere) the log no longer waits for the UART
#endif
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_reset = reset_name(esp_reset_reason());
    s_ran_s = store_load_ran();
    store_save_ran(0);
    ESP_LOGI(TAG, "started after: %s; the run before lasted %u s%s", s_reset, (unsigned)s_ran_s,
             s_ran_s ? "" : " (under a minute, or not known)");

    s_lock = xSemaphoreCreateMutex();
    if (!store_load_cfg(&s_cfg)) fr_cfg_defaults(&s_cfg);
    store_load_secrets(&s_sec);
    store_load_nets(&s_sec, &s_nets);
    char watch[WATCH_TEXT];
    store_load_watch(watch, sizeof watch);
    watch_set(&s_watch, watch);
    store_load_light(&s_light_cfg);
    store_load_opt(&s_opt);
    s_help = (uint8_t)store_load_help();
    s_bare = (uint8_t)store_load_bare();
    s_backlight = (uint8_t)store_load_backlight();
    s_night = (uint8_t)store_load_night();
    s_alerts = (uint8_t)store_load_alerts();
    store_load_units(&s_units);
    s_names = (uint8_t)store_load_names();
    s_demo_on = !s_sec.ssid[0];                         // no WiFi set: the demo from the first frame
    sky_init(&s_sky, s_cfg.lat, s_cfg.lon);
    demo_init(&s_demo, range_km(&s_cfg), esp_random());
    tiles_init();
    map_mem = iram_first;
    map_scratch = s_fetch_buf.map;                     // the fetch task's own buffer while it reads a tile
#if DISP_LCD_DSI
    disp_board_up();                                   // the Tab5: the radio's power switch, before the WiFi asks for it
#endif
#if RADAR_OVERLAY
    screen_early();
#endif
    net_start(&s_nets);                                // up even without settings: a SET then just connects
    /* the TLS handshake runs on the fetch task, and the map is made there. one core (the C3): it
     * gets the screen's priority, the two take turns a tick each. under the screen it had less
     * than a millisecond a frame, a map took it over ten seconds and IDLE starved into the watchdog */
#if DISP_JC8012 || DISP_WAVE7
    /* (chipguyhere) its stack in the PSRAM: 12 KB the internal RAM has not got to spare. with XIP
     * from PSRAM a flash write leaves the cache on, so a stack there is safe (spi_flash_os_func_app.c) */
    if (xTaskCreatePinnedToCoreWithCaps(fetch_task, "fetch", 12288, NULL, 4, &s_fetch, 0, MALLOC_CAP_SPIRAM) != pdPASS)
#endif
    xTaskCreatePinnedToCore(fetch_task, "fetch", 12288, NULL, portNUM_PROCESSORS > 1 ? 4 : 5, &s_fetch, 0);
#if RADAR_OVERLAY
    /* the Waveshare 7/7B: the second core is the panel's (its fill follows every chunk's copy down
     * the rows and lays the moving layer over them); the drawing goes on the first, beside the WiFi */
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, 0);
#else
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, portNUM_PROCESSORS > 1 ? 1 : 0);
#endif
    /* the console last, and READY with it: the site sends its SET the moment it reads READY. said
     * before, the SET was lost (no console yet) or notified a fetch task not made yet (an assert) */
    console_start(on_line);
    say_ready();
}
