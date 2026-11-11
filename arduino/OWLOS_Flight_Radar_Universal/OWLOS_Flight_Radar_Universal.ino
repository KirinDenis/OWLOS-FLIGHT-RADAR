/*
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 *  OWLOS FLIGHT RADAR â€” UNIVERSAL ARDUINO EDITION
 *  Real-Time Aircraft Radar with ADS-B Live Tracking & Offline Simulator
 *  Powered by AMK_CYD_Universal_TFT (v1.6.3)
 *
 *  Compatible with ALL Display Sizes and Panel Types:
 *   - 2.4", 2.8", 3.2" CYD (ESP32-2432S028, ILI9341, ST7789, 320x240)
 *   - 3.5", 4.0" CYD (ESP32-3248S035, ST7796, ILI9486, 480x320)
 *   - ESP32-S3 Boards (Elecrow Advance S3, LCDWIKI ES3C28P, NM-CYD-C5)
 *   - 4.3", 5.0", 7.0" ESP32-S3 Parallel RGB Boards (Sunton, Guition, Waveshare, 800x480)
 *
 *  Hardware Standards:
 *   - Dynamic library backlight pin detection via display.backlightPin()
 *   - 10-read sample LDR moving-average priming at boot
 *   - adjustBrightness() as FIRST line of void loop()
 *   - 80 MHz High-Speed SPI clock acceleration
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <AMK_CYD_Universal_TFT.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cities.h"

extern "C" {
#include "config.h"
#include "gfx.h"
#include "font.h"
#include "sky.h"
#include "demo.h"
#include "map.h"
#include "tiles.h"
#include "ui.h"
#include "ui_int.h"

  typedef void (*net_sink_fn)(void *ctx, const char *data, int len);
  int net_get(const char *url, const char *bearer, net_sink_fn sink, void *ctx, int *status);
}

extern "C" void net_time_start(void) {
  static bool started = false;
  if (started) return;
  started = true;
  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

extern "C" uint32_t net_utc(void) {
  time_t t = time(NULL);
  return t > 1700000000 ? (uint32_t)t : 0;
}

extern "C" int net_up(void) {
  return WiFi.status() == WL_CONNECTED;
}

// â”€â”€ Required External Symbols for Core â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
extern "C" {
  int GFX_W = 320;
  int GFX_H = 240;
  int GFX_PW = 320;
  int GFX_PH = 240;
}

static uint32_t my_clock_us() {
  return (uint32_t)micros();
}

// â”€â”€ Global Hardware Instances â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
AMK_CYD_display display;
AMK_CYD_touch touch;
TFT_eSPI tft;

Preferences prefs;

// â”€â”€ LDR Sensor & Backlight Configuration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#define LDR_PIN 34
#define NUM_LDR_READINGS 10

bool autoBrightness = true;
int manualBrightness = 180;
int activeBrightness = 180;
int lastLDRValue = 0;
int screenRotation = 3;  // Landscape default

// â”€â”€ Radar Engine & Aircraft State â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
ui_t g_ui;
sky_t g_sky;
demo_t g_demo;
light_t g_light;
SemaphoreHandle_t s_lock = NULL;
TaskHandle_t g_netTaskHandle = NULL;

// Strip Buffers (4 rows preserves >61 KB free DRAM for mbedTLS BIGNUM handshakes)
static const int STRIP_H = 4;
uint32_t *g_strip_px = NULL;
uint16_t *g_strip_565 = NULL;

// â”€â”€ IRAM & PSRAM Allocation Helper (from Denis Kirin's main.c) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void *iram_first(size_t bytes) {
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 100000) {
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (p) return p;
  }
  void *p = heap_caps_malloc(bytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
  if (!p) p = malloc(bytes);
  return p;
}

// Wi-Fi & Network State
String savedSSID = "";
String savedPass = "";
bool isWifiConnected = false;
uint32_t lastApiFetchMs = 0;
static uint32_t s_scan_start_ms = 0;

// â”€â”€ Dynamic Resolution Layout Calculator â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void computeLayout() {
  int sw = tft.width();
  int sh = tft.height();

  if (sw >= 750) {
    // 800x480 (4.3", 5.0", 7.0" ESP32-S3 RGB Panels: ST7262)
    GFX_W = 800;
    GFX_H = 480;
    GFX_PW = 800;
    GFX_PH = 480;
  } else if (sw >= 450) {
    // 480x320 (3.5", 4.0" CYD / ST7796 / ILI9486)
    GFX_W = 480;
    GFX_H = 320;
    GFX_PW = 480;
    GFX_PH = 320;
  } else {
    // 320x240 (2.4", 2.8", 3.2" CYD / ILI9341 / ST7789)
    GFX_W = 320;
    GFX_H = 240;
    GFX_PW = 320;
    GFX_PH = 240;
  }

  Serial.printf("[Radar Layout] Screen: %dx%d | Active: %dx%d\n", sw, sh, GFX_PW, GFX_PH);
}

// â”€â”€ Backlight Auto-Dimming per RULE[user_global] â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void adjustBrightness() {
  static int lastAppliedBrightness = -1;

  if (autoBrightness) {
    static int readings[NUM_LDR_READINGS] = { 0 };
    static int readIndex = 0;
    static long total = 0;
    static unsigned long lastReadTime = 0;

    if (millis() - lastReadTime > 80) {
      lastReadTime = millis();
      total = total - readings[readIndex];
      readings[readIndex] = analogRead(LDR_PIN);
      total = total + readings[readIndex];
      readIndex = (readIndex + 1) % NUM_LDR_READINGS;
      lastLDRValue = readings[readIndex];

      long average = total / NUM_LDR_READINGS;
      int target = constrain(map(average, 0, 1024, 255, 20), 20, 255);

      // 4-unit hysteresis deadband prevents flicker
      if (abs(target - lastAppliedBrightness) >= 4) {
        lastAppliedBrightness = target;
        activeBrightness = target;
        uint8_t blPin = display.backlightPin();
        analogWrite(blPin, target);
      }
    }
  } else {
    if (lastAppliedBrightness != manualBrightness) {
      lastAppliedBrightness = manualBrightness;
      activeBrightness = manualBrightness;
      uint8_t blPin = display.backlightPin();
      analogWrite(blPin, manualBrightness);
    }
  }
}

// â”€â”€ Rear RGB LED Handler â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void updateLED() {
  uint8_t rgb[3] = { 0, 0, 0 };
  light_colour(&g_light, &g_ui.light, millis(), rgb);
  int r_pin = light_red_gpio(&g_ui.light);  // GPIO 4 or 22
  analogWrite(r_pin, 255 - rgb[0]);
  analogWrite(16, 255 - rgb[1]);
  analogWrite(17, 255 - rgb[2]);
}

// â”€â”€ Original OWLOS Network & Streaming Reader Architecture â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

typedef struct {
  char *b;
  int n, cap;
} text_t;

static void to_text(void *ctx, const char *data, int len) {
  text_t *t = (text_t *)ctx;
  if (len > t->cap - 1 - t->n) len = t->cap - 1 - t->n;
  memcpy(t->b + t->n, data, (size_t)len);
  t->n += len;
  t->b[t->n] = 0;
}

static void to_reader(void *ctx, const char *data, int len) {
  sky_read((sky_reader_t *)ctx, data, len);
}

static float vague(float deg) {
  return roundf(deg * 100.0f) / 100.0f;
}

static float ask_km(const fr_cfg_t *c) {
  return range_km(c) * (!RADAR_ROUND && c->look != LOOK_RADAR ? 2.4f : 1.6f);
}

static const char *const FREE_URL[2] = {
  "https://api.adsb.lol/v2/point/%.2f/%.2f/%d",
  "https://opendata.adsb.fi/api/v3/lat/%.2f/lon/%.2f/dist/%d",
};
static const char *const FREE_NAME[2] = { "ADSB.LOL", "ADSB.FI" };

static void free_url(const fr_cfg_t *c, int server, char *url, int cap) {
  int nm = (int)ceilf(ask_km(c) / 1.852f);
  if (nm > 250) nm = 250;
  snprintf(url, (size_t)cap, FREE_URL[server], (double)vague(c->lat), (double)vague(c->lon), nm);
}

// Streaming HTTP GET: Streams chunks straight into sink/sky_read via ESP-IDF esp_http_client (~12 KB RAM)
extern "C" int net_get(const char *url, const char *bearer, net_sink_fn sink, void *ctx, int *status) {
  static char buf[1024];
  *status = 0;
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    return -1;
  }

  esp_http_client_config_t c = {};
  c.url = url;
  c.crt_bundle_attach = esp_crt_bundle_attach;
  c.timeout_ms = 9000;
  c.user_agent = "OWLOS-Flight-Radar/0.8 (+https://github.com/KirinDenis/OWLOS-FLIGHT-RADAR)";
  c.buffer_size = 2048;
  c.buffer_size_tx = 1024;
  c.skip_cert_common_name_check = true;

  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (!h) return -1;
  esp_http_client_set_header(h, "Accept", "application/json");
  if (bearer && bearer[0]) {
    static char auth[640];
    snprintf(auth, sizeof(auth), "Bearer %s", bearer);
    esp_http_client_set_header(h, "Authorization", auth);
    esp_http_client_set_header(h, "Accept-Version", "v1");
  }

  int len = -1;
  if (esp_http_client_open(h, 0) == ESP_OK) {
    esp_http_client_fetch_headers(h);
    *status = esp_http_client_get_status_code(h);
    len = 0;
    while (*status == 200) {
      int r = esp_http_client_read(h, buf, sizeof(buf));
      if (r <= 0) break;
      if (sink) sink(ctx, buf, r);
      len += r;
    }
  } else {
    Serial.printf("[HTTP ERROR] Failed to connect: %s\n", url);
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  return len;
}

// â”€â”€ WebSerial / Companion Site Protocol Support â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static volatile uint32_t s_sky_until = 0;
static volatile uint32_t s_pick = 0;
static volatile int s_pick_new = 0;
static uint32_t s_sel_told = 0;

static int page_there(void) {
  return (int32_t)(s_sky_until - millis()) > 0;
}

static void sky_out(void) {
  if (!page_there()) return;
  static const char *const CATEGORY[KINDS] = {
    "A3", "A1", "A2", "A5", "A6", "A7", "B1", "B2", "B6", "C1"
  };
  char server[32];
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    int n = g_sky.n;
    snprintf(server, sizeof(server), "%s", g_ui.net.server);
    Serial.printf("OWLOS SKY BEGIN %s\n", server);
    for (int i = 0; i < n; i++) {
      const plane_t *p = &g_sky.p[i];
      char alt[16];
      if (p->ground) snprintf(alt, sizeof(alt), "\"ground\"");
      else snprintf(alt, sizeof(alt), "%d", (int)p->alt);
      Serial.printf("OWLOS PLANE {\"hex\":\"%06x\",\"flight\":\"%s\",\"r\":\"%s\",\"t\":\"%s\",\"category\":\"%s\",\"lat\":%.5f,\"lon\":%.5f,"
                    "\"alt_baro\":%s,\"track\":%.1f,\"gs\":%.1f,\"seen_pos\":%.1f}\n",
                    (unsigned)p->icao, p->flight, p->reg, p->type, CATEGORY[p->kind] ? CATEGORY[p->kind] : "A0",
                    (double)p->lat, (double)p->lon, alt, (double)p->track, (double)p->gs, (double)p->seen);
    }
    Serial.printf("OWLOS SKY END %d\n", n);
    xSemaphoreGive(s_lock);
  }
}

// â”€â”€ WebSerial Device Configuration Publisher (Digital Twin Sync) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static void say_cfg(void) {
  const char *curSsid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID().c_str() : "";
  Serial.printf("OWLOS CFG {\"ssid\":\"%s\",\"nets\":[\"%s\"],\"lat\":%.4f,\"lon\":%.4f,\"place\":\"%s\",\"country\":\"%s\",\"src\":\"%s\",\"key\":false,"
                "\"up\":%d,\"flip\":%d,\"theme\":%d,\"look\":%d,\"map\":1,\"ground\":%d,\"watch\":\"%s\","
                "\"vectors\":%d,\"beam_step\":%d,\"clock\":%d,\"block\":%d,\"autopilot\":%d,\"lost\":%d,\"conflict\":%d,"
                "\"fl_lo\":%d,\"fl_hi\":%d,\"utc\":%u,\"version\":\"0.8\",\"newest\":\"0.8\","
                "\"range\":%.2f,\"dist_unit\":%d,\"alt_unit\":%d,\"labels\":%d}\n",
                curSsid, curSsid,
                (double)g_ui.cfg.lat, (double)g_ui.cfg.lon,
                g_ui.cfg.place, g_ui.cfg.country,
                g_ui.cfg.source == SRC_FR24 ? "fr24" : "free",
                g_ui.cfg.up, g_ui.cfg.flip, g_ui.cfg.theme, g_ui.cfg.look,
                g_ui.cfg.ground,
                g_ui.watch_text,
                g_ui.opt.vectors, g_ui.opt.beam_step, g_ui.opt.clock, g_ui.opt.block,
                g_ui.opt.autopilot, g_ui.opt.lost, g_ui.opt.conflict,
                g_ui.opt.fl_lo, g_ui.opt.fl_hi,
                (unsigned)(millis() / 1000u),
                (double)g_ui.cfg.range, g_ui.cfg.dist_unit, g_ui.cfg.alt_unit, g_ui.cfg.labels);
  Serial.flush();
}

// â”€â”€ OpenStreetMap Engine State & Service â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static map_t s_map;
static map_plan_t s_plan;
static int s_map_read = 0;
static uint32_t s_map_try = 0;

// â”€â”€ HTTPS OpenStreetMap Tile Downloader (esp_http_client ~12 KB RAM) â”€â”€â”€â”€â”€â”€â”€â”€â”€
extern "C" int fetch_tile_https(const char *url, void *writer, int (*put_fn)(void *, const void *, uint32_t)) {
  if (WiFi.status() != WL_CONNECTED) return 0;
  static char buf[1024];

  esp_http_client_config_t c = {};
  c.url = url;
  c.crt_bundle_attach = esp_crt_bundle_attach;
  c.timeout_ms = 9000;
  c.user_agent = "OWLOS-Flight-Radar/0.8 (+https://github.com/KirinDenis/OWLOS-FLIGHT-RADAR)";
  c.buffer_size = 2048;
  c.buffer_size_tx = 1024;
  c.skip_cert_common_name_check = true;

  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (!h) return 0;
  esp_http_client_set_header(h, "Accept", "image/png");

  int got = 0;
  if (esp_http_client_open(h, 0) == ESP_OK && esp_http_client_fetch_headers(h) >= 0) {
    int status = esp_http_client_get_status_code(h);
    if (status == 200) {
      int r;
      uint32_t totalBytes = 0;
      while ((r = esp_http_client_read(h, buf, sizeof(buf))) > 0) {
        if (put_fn) put_fn(writer, buf, (uint32_t)r);
        totalBytes += r;
      }
      if (esp_http_client_is_complete_data_received(h) || totalBytes > 500) {
        got = 1;
        Serial.printf("[TILE] Downloaded %s (%u bytes)\n", url, totalBytes);
      }
    } else {
      Serial.printf("[TILE] %s HTTP %d\n", url, status);
    }
  }
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  return got;
}

void map_service(const fr_cfg_t *c) {
  if (!c->map) return;
  int w, h, disc;
  ui_ground_size(c, &w, &h, &disc);
  float kpx = ui_ground_kpx(c);
  if (!disc && w < GFX_PW) {
    kpx = kpx * ((float)w / (float)GFX_PW);
  }

  int same = s_map.ready && s_map.lat == c->lat && s_map.lon == c->lon && s_map.kpx == kpx && s_map.w == w && s_map.h == h;
  if (same && (s_map_read == s_plan.ntiles || millis() - s_map_try < 15000u)) return;

  if (!same) map_plan(&s_plan, c->lat, c->lon, kpx, w, h, disc);
  s_map_try = millis();

  int kept = tiles_fetch(s_plan.tiles, s_plan.ntiles);
  if (kept == 0) {
    // No offline tiles stored in flash for this location/zoom.
    // Skip allocation to save ~76 KB DRAM for mbedTLS SSL/TLS handshakes!
    s_map_read = 0;
    return;
  }
  if (same && kept <= s_map_read) return;

  if (!map_alloc(&s_map, w, h, disc)) {
    Serial.printf("[MAP] No memory for %d x %d texels\n", w, h);
    return;
  }

  map_begin(&s_map);
  int read = 0;
  for (int k = 0; k < s_plan.ntiles; k++) {
    tile_src_t src;
    if (!tiles_open(&s_plan.tiles[k], &src)) continue;
    int r = map_tile(&s_map, &s_plan, k, tiles_read, &src);
    read += (r == PNG_OK);
  }
  map_end(&s_map, &s_plan);
  s_map_read = read;
  Serial.printf("[MAP] Loaded %d of %d tiles for %s (%.4f, %.4f) [Range: %.0f]\n",
                read, s_plan.ntiles, c->place, (double)c->lat, (double)c->lon, (double)c->range);
}

// â”€â”€ Home Geocoding & Finding â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static char s_home_ask[64] = "";

bool geocodeLocation(const char *query);

static void home_find() {
  char ask[64];
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    memcpy(ask, s_home_ask, sizeof(ask));
    s_home_ask[0] = 0;
    xSemaphoreGive(s_lock);
  } else {
    return;
  }
  if (!ask[0]) return;

  float lat = 0, lon = 0;
  char placeName[32] = "";
  int n = sscanf(ask, "%f %f %31s", &lat, &lon, placeName);
  if (n < 2) n = sscanf(ask, "%f,%f %31s", &lat, &lon, placeName);
  if (n >= 2 && lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f) {
    if (!placeName[0]) strncpy(placeName, "My Area", sizeof(placeName) - 1);
    updateHomeLocation(lat, lon, placeName, "");
    char msg[40];
    snprintf(msg, sizeof(msg), "HOME: %.3f, %.3f", (double)lat, (double)lon);
    toast(&g_ui, msg);
    Serial.printf("[LOCATION] Point updated: %.4f, %.4f (%s)\n", lat, lon, placeName);
    return;
  }

  // Lookup city
  geocodeLocation(ask);
}

#define DEMO_BYTES 6144
#define FREE_EVERY_MS 5000u

// â”€â”€ Core 0 Network Fetch Task (Faithful Denis Kirin Architecture) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void networkTask(void *pvParameters) {
  (void)pvParameters;
  static sky_reader_t rd;
  static char url[200];
  int next = -1;
  uint32_t down = 0;

  for (;;) {
    fr_cfg_t c;
    int configured;

    home_find();

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
      c = g_ui.cfg;
      configured = (savedSSID.length() > 0);
      sky_hide_ground(&g_sky, !c.ground);
      xSemaphoreGive(s_lock);
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    map_service(&c);

    uint32_t every = FREE_EVERY_MS;
    const int up = (WiFi.status() == WL_CONNECTED);
    if (up) net_time_start();

    int trying = 0;
    if (configured && !up) {
      uint32_t now = millis();
      if (!down) down = now;
      trying = (now - down < 20000u);
    } else {
      down = 0;
    }

    const int demo = !configured || (!up && !trying);
    static int s_demo_on = -1;
    if (demo != s_demo_on) {
      if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_sky.n = 0;
        s_demo_on = demo;
        g_ui.net.demo = demo;
        xSemaphoreGive(s_lock);
      }
    }

    if (demo) {
      static uint32_t told = 0;
      uint32_t now = millis();
      if (!configured || !g_sky.n || now - told >= FREE_EVERY_MS - 50u) {
        char *buf = (char *)malloc(DEMO_BYTES);
        if (buf) {
          told = now;
          int n = demo_json(&g_demo, c.lat, c.lon, range_km(&c), now, buf, DEMO_BYTES);
          if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            sky_read_begin(&rd, &g_sky, 0, now);
            sky_read(&rd, buf, n);
            sky_read_end(&rd, &g_sky);
            sky_forget(&g_sky, now);
            strncpy(g_ui.net.server, "DEMO", sizeof(g_ui.net.server) - 1);
            g_ui.net.fetched = now;
            xSemaphoreGive(s_lock);
          }
          free(buf);
          sky_out();
        }
      }
      if (configured) every = 1000;
    } else if (!up) {
      if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_sky.status = SKY_CONNECTING;
        snprintf(g_sky.note, sizeof(g_sky.note), "CONNECTING");
        xSemaphoreGive(s_lock);
      }
      every = 1000;
    } else {
      int server = (next < 0) ? (c.server < 2 ? c.server : 0) : next;
      free_url(&c, server, url, sizeof(url));

      static char said[sizeof(url)] = "";
      if (strcmp(url, said) != 0) {
        Serial.printf("[ADS-B] Query: %s\n", url);
        snprintf(said, sizeof(said), "%s", url);
      }

      int status = 0;
      uint32_t t0 = millis();
      if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        sky_read_begin(&rd, &g_sky, 0, millis());
        xSemaphoreGive(s_lock);
      }

      int len = net_get(url, NULL, to_reader, &rd, &status);
      int ms = (int)(millis() - t0);
      uint32_t now = millis();
      int n = -2;

      if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(150)) == pdTRUE) {
        if (len > 0 && status == 200) {
          rd.now = now;
          n = sky_read_end(&rd, &g_sky);
          if (n < 0) {
            g_sky.status = SKY_PARSE;
            snprintf(g_sky.note, sizeof(g_sky.note), "STRANGE ANSWER");
          } else {
            strncpy(g_ui.net.server, FREE_NAME[server], sizeof(g_ui.net.server) - 1);
            g_ui.net.fetched = now;
            char came[40] = "";
            uint32_t pick = 0;
            int ev = watch_sky(&g_ui.watch, &g_sky, now, came, sizeof(came), &pick);
            if (ev) light_event(&g_light, ev == WATCH_NEWS_RANGE ? LIGHT_RANGE : ev == WATCH_NEWS_UP ? LIGHT_UP
                                                                                                     : LIGHT_DOWN,
                                now);
            light_hold(&g_light, LIGHT_RANGE, watch_near(&g_ui.watch) > 0, now);
            light_sky(&g_light, &g_ui.light, &g_sky, unit_km(c.dist_unit), now);
            if (came[0]) toast(&g_ui, came);
            if (pick && !g_ui.sel) g_ui.sel = pick;
          }
        } else {
          g_sky.status = SKY_HTTP;
          if (status) snprintf(g_sky.note, sizeof(g_sky.note), "%s HTTP %d", FREE_NAME[server], status);
          else snprintf(g_sky.note, sizeof(g_sky.note), "%s NO ANSWER", FREE_NAME[server]);
        }
        g_ui.net.http = status;
        sky_forget(&g_sky, now);
        int planes = g_sky.n;
        xSemaphoreGive(s_lock);

        if (n >= 0) sky_out();
        if (n < 0) next = (server + 1) % 2;
        else next = server;

        Serial.printf("[%s] HTTP %d, %d bytes in %d ms | %d planes in answer, %d kept, %d in airspace | DRAM Free: %u\n",
                      FREE_NAME[server], status, len, ms, rd.total, n, planes,
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
      }
    }

    // Wait for next poll or wake immediately on UI interaction
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(every));
  }
}

// â”€â”€ Asynchronous Wi-Fi Scanner & State Machine â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void handleWifiScan() {
  if (g_ui.net.scan != SCAN_RUNNING) return;

  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    // Check timeout (12 seconds)
    if (s_scan_start_ms && (millis() - s_scan_start_ms > 12000)) {
      Serial.println("[WIFI] Scan timed out!");
      g_ui.net.scan = SCAN_FAILED;
      s_scan_start_ms = 0;
    }
    return;
  }

  if (n == WIFI_SCAN_FAILED) {
    Serial.println("[WIFI] Scan failed!");
    g_ui.net.scan = SCAN_FAILED;
    s_scan_start_ms = 0;
    return;
  }

  // n >= 0: Scan successfully finished!
  Serial.printf("[WIFI] Scan complete: found %d APs\n", n);
  g_ui.net.n_aps = 0;
  for (int i = 0; i < n && g_ui.net.n_aps < UI_APS; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;  // skip hidden

    // Deduplicate SSID
    bool dup = false;
    for (int k = 0; k < g_ui.net.n_aps; k++) {
      if (strcmp(g_ui.net.aps[k].ssid, ssid.c_str()) == 0) {
        dup = true;
        break;
      }
    }
    if (dup) continue;

    int idx = g_ui.net.n_aps++;
    strncpy(g_ui.net.aps[idx].ssid, ssid.c_str(), sizeof(g_ui.net.aps[idx].ssid) - 1);
    g_ui.net.aps[idx].ssid[sizeof(g_ui.net.aps[idx].ssid) - 1] = '\0';
    g_ui.net.aps[idx].rssi = (int8_t)WiFi.RSSI(i);
    g_ui.net.aps[idx].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? 1 : 0;
    Serial.printf("  AP[%d]: %s (%d dBm, %s)\n",
                  idx, g_ui.net.aps[idx].ssid, g_ui.net.aps[idx].rssi,
                  g_ui.net.aps[idx].open ? "OPEN" : "SECURED");
  }

  g_ui.net.scan = SCAN_DONE;
  s_scan_start_ms = 0;
  WiFi.scanDelete();
}

// â”€â”€ JSON Helper for Geocoding & Config Parsing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static const char *findJsonField(const char *json, const char *key) {
  if (!json || !key) return NULL;
  size_t kl = strlen(key);
  const char *p = json;
  while ((p = strchr(p, '"')) != NULL) {
    p++;
    if (!strncmp(p, key, kl) && p[kl] == '"') {
      p += kl + 1;
      while (*p == ' ') p++;
      if (*p != ':') continue;
      p++;
      while (*p == ' ') p++;
      return p;
    }
    while (*p && *p != '"') {
      if (*p == '\\' && p[1]) p++;
      p++;
    }
    if (*p) p++;
  }
  return NULL;
}

static bool getJsonStr(const char *json, const char *key, char *out, int cap) {
  const char *p = findJsonField(json, key);
  if (!p || *p != '"') return false;
  p++;
  int n = 0;
  while (*p && *p != '"' && n < cap - 1) {
    char ch = *p++;
    if (ch == '\\' && *p) ch = *p++;
    out[n++] = ch;
  }
  out[n] = '\0';
  return n > 0;
}

static bool getJsonNum(const char *json, const char *key, double *out) {
  const char *p = findJsonField(json, key);
  if (!p || (*p != '-' && (*p < '0' || *p > '9'))) return false;
  *out = strtod(p, NULL);
  return true;
}

// â”€â”€ Global Home Location Update (Memory + NVS Flash) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void updateHomeLocation(float lat, float lon, const char *place, const char *country) {
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(150)) == pdTRUE) {
    g_ui.cfg.lat = lat;
    g_ui.cfg.lon = lon;
    if (place && place[0]) {
      strncpy(g_ui.cfg.place, place, sizeof(g_ui.cfg.place) - 1);
      g_ui.cfg.place[sizeof(g_ui.cfg.place) - 1] = '\0';
    } else {
      strncpy(g_ui.cfg.place, "My Area", sizeof(g_ui.cfg.place) - 1);
      g_ui.cfg.place[sizeof(g_ui.cfg.place) - 1] = '\0';
    }
    if (country) {
      strncpy(g_ui.cfg.country, country, sizeof(g_ui.cfg.country) - 1);
      g_ui.cfg.country[sizeof(g_ui.cfg.country) - 1] = '\0';
    }
    sky_init(&g_sky, g_ui.cfg.lat, g_ui.cfg.lon);
    demo_init(&g_demo, range_km(&g_ui.cfg), 42);
    s_map.ready = 0;
    s_map_read = 0;
    s_map_try = 0;
    xSemaphoreGive(s_lock);
  }

  prefs.begin("owlos_radar", false);
  prefs.putFloat("lat", lat);
  prefs.putFloat("lon", lon);
  prefs.putString("place", g_ui.cfg.place);
  prefs.putString("country", g_ui.cfg.country);
  prefs.end();

  Serial.printf("[LOCATION] Successfully saved: %.4f, %.4f | Place: \"%s\" | Country: \"%s\"\n",
                lat, lon, g_ui.cfg.place, g_ui.cfg.country);
}

// â”€â”€ OpenStreetMap Nominatim Geocoder (with Offline Built-in Fast Path) â”€â”€â”€â”€â”€â”€â”€
bool geocodeLocation(const char *query) {
  // 1. Instant Offline Lookup from Built-in Database
  const BuiltinCity *city = findBuiltinCity(query);
  if (city != NULL) {
    updateHomeLocation(city->lat, city->lon, city->display, city->country);
    char msg[40];
    snprintf(msg, sizeof(msg), "HOME: %s", city->display);
    toast(&g_ui, msg);
    Serial.printf("[LOCATION] Matched built-in city: %s, %s (%.4f, %.4f)\n",
                  city->display, city->country, city->lat, city->lon);
    return true;
  }

  // 2. Online Geocode fallback via OpenStreetMap Nominatim
  if (WiFi.status() != WL_CONNECTED) {
    toast(&g_ui, "NO WIFI: TYPE LAT LON");
    Serial.println("[OSM] Cannot search online: Wi-Fi not connected.");
    return false;
  }

  toast(&g_ui, "SEARCHING...");
  Serial.printf("[OSM] Querying Nominatim for: \"%s\"...\n", query);

  String q = String(query);
  q.trim();
  q.replace(" ", "%20");

  char url[320];
  snprintf(url, sizeof(url),
           "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&addressdetails=1&accept-language=en&q=%s",
           q.c_str());

  static char s_osm_buf[2048];
  text_t t = { s_osm_buf, 0, (int)sizeof(s_osm_buf) };
  s_osm_buf[0] = 0;
  int code = 0;
  int len = net_get(url, NULL, to_text, &t, &code);

  if (code == 200 && len > 0) {
    char slat[32] = { 0 }, slon[32] = { 0 }, place[32] = { 0 }, country[32] = { 0 };
    getJsonStr(s_osm_buf, "lat", slat, sizeof(slat));
    getJsonStr(s_osm_buf, "lon", slon, sizeof(slon));

    static const char *const PLACE_KEYS[] = { "city", "town", "village", "municipality", "hamlet", "name" };
    for (int i = 0; i < 6 && !place[0]; i++) {
      getJsonStr(s_osm_buf, PLACE_KEYS[i], place, sizeof(place));
    }
    getJsonStr(s_osm_buf, "country", country, sizeof(country));

    if (slat[0] && slon[0]) {
      float lat = (float)atof(slat);
      float lon = (float)atof(slon);
      if (!place[0]) strncpy(place, query, sizeof(place) - 1);
      updateHomeLocation(lat, lon, place, country);

      char msg[40];
      snprintf(msg, sizeof(msg), "HOME: %s", place);
      toast(&g_ui, msg);
      return true;
    } else {
      toast(&g_ui, "PLACE NOT FOUND");
      Serial.println("[OSM] Place not found in Nominatim response.");
      return false;
    }
  } else {
    Serial.printf("[OSM] Online geocode failed (HTTP %d). Please enter coordinates: e.g. 16.84 96.17\n", code);
    toast(&g_ui, "LOOKUP FAILED: TYPE LAT LON");
    return false;
  }
}

// â”€â”€ Touch Gesture & Interaction Handling â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void handleTouch() {
  uint16_t tx = 0, ty = 0;
  bool down = tft.getTouch(&tx, &ty);

  if (down) {
    light_touch(&g_light);
  }

  ui_cmd_t cmd = { 0 };
  ui_touch(&g_ui, down ? 1 : 0, (int)tx, (int)ty, (int)tx, (int)ty, millis(), &cmd);

  if (cmd.light_test) {
    light_test(&g_light, millis());
  }

  // If configuration changed, save to Preferences and sync to WebSerial
  if (cmd.cfg || cmd.opt) {
    prefs.begin("owlos_radar", false);
    prefs.putInt("theme", g_ui.cfg.theme);
    prefs.putInt("dist_u", g_ui.cfg.dist_unit);
    prefs.putInt("alt_u", g_ui.cfg.alt_unit);
    prefs.putFloat("range", g_ui.cfg.range);
    prefs.putInt("up", g_ui.cfg.up);
    prefs.putInt("flip", g_ui.cfg.flip);
    prefs.putInt("look", g_ui.cfg.look);
    prefs.putInt("map", g_ui.cfg.map);
    prefs.putInt("ground", g_ui.cfg.ground);
    prefs.putInt("labels", g_ui.cfg.labels);
    prefs.end();

    s_map_try = 0;  // Trigger map refresh when range, look or map toggle changes

    if (g_ui.cfg.flip != (screenRotation == 1 ? 1 : 0)) {
      screenRotation = g_ui.cfg.flip ? 1 : 3;
      tft.setRotation(screenRotation);
    }
    if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);

    // Sync state change to WebSerial Digital Twin
    if (page_there()) {
      say_cfg();
    }
  }

  if (cmd.refetch) {
    s_map_try = 0;
    if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);
  }

  // Wi-Fi scan requested from screen (e.g. entering Wi-Fi page or tapping LOOK AGAIN)
  if (cmd.scan) {
    Serial.println("[WIFI] Wi-Fi scan requested from screen...");
    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);  // async = true, show_hidden = false
    g_ui.net.scan = SCAN_RUNNING;
    s_scan_start_ms = millis();
  }

  // Wi-Fi network selected and password entered on screen
  if (cmd.wifi) {
    savedSSID = String(g_ui.wifi_ssid);
    savedPass = String(g_ui.wifi_pass);
    memset(g_ui.wifi_pass, 0, sizeof(g_ui.wifi_pass));  // secure wipe

    prefs.begin("owlos_radar", false);
    prefs.putString("ssid", savedSSID);
    prefs.putString("pass", savedPass);
    prefs.end();

    Serial.printf("[WIFI] New credentials saved: SSID=\"%s\"\n", savedSSID.c_str());
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    g_ui.net.configured = 1;
    g_sky.status = SKY_CONNECTING;
    toast(&g_ui, "CONNECTING TO WIFI...");
  }

  if (cmd.opt) {
    prefs.begin("owlos_radar", false);
    prefs.putBytes("opt", &g_ui.opt, sizeof(g_ui.opt));
    prefs.end();
  }

  if (cmd.light) {
    prefs.begin("owlos_radar", false);
    prefs.putBytes("light", &g_ui.light, sizeof(g_ui.light));
    prefs.end();
  }

  // Watchlist updated from screen (tap WATCH in panel or typed in keyboard)
  if (cmd.watch) {
    watch_set(&g_ui.watch, g_ui.watch_text);
    prefs.begin("owlos_radar", false);
    prefs.putString("watch", g_ui.watch_text);
    prefs.end();
    Serial.printf("[WATCHLIST] Saved: \"%s\" (%d targets)\n", g_ui.watch_text, g_ui.watch.n);
    if (page_there()) say_cfg();
  }

  // Home location changed from screen keyboard (SETUP -> DATA -> HOME)
  if (cmd.home) {
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
      snprintf(s_home_ask, sizeof(s_home_ask), "%s", g_ui.home_text);
      xSemaphoreGive(s_lock);
    }
    if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);
  }
}

// â”€â”€ Frame Rendering Pipeline (Strip-by-Strip Blit) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void renderFrame() {
  canvas_t cv;
  cv.px = g_strip_px;
  cv.helper = 0;

  // 1. Copy fresh aircraft snapshot to g_ui.snap under lock (only ~2 microseconds)
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
    memcpy(&g_ui.snap, &g_sky, sizeof(sky_t));
    xSemaphoreGive(s_lock);
  }

  g_ui.now = millis();
  g_ui.net.wifi = (WiFi.status() == WL_CONNECTED);
  g_ui.net.configured = (savedSSID.length() > 0);
  g_ui.net.demo = !g_ui.net.wifi;
  if (g_ui.net.wifi) {
    strncpy(g_ui.net.ssid, WiFi.SSID().c_str(), sizeof(g_ui.net.ssid) - 1);
    strncpy(g_ui.net.ip, WiFi.localIP().toString().c_str(), sizeof(g_ui.net.ip) - 1);
  }

  // WebSerial plane selection sync
  if (page_there() && g_ui.sel != s_sel_told) {
    s_sel_told = g_ui.sel;
    Serial.printf("OWLOS SEL %06x\n", (unsigned)s_sel_told);
  }

  // 2. Perform frame layout & motion interpolation (runs freely outside lock)
  ui_frame(&g_ui, millis());

  // 3. Render strips and blast to display via 80 MHz SPI bus (runs freely outside lock)
  for (int y0 = 0; y0 < GFX_PH; y0 += STRIP_H) {
    cv.y0 = y0;
    cv.h = min(STRIP_H, GFX_PH - y0);

    ui_strip(&g_ui, &cv);

    gfx_pack565(&cv, g_strip_565);

    tft.pushImage(0, y0, GFX_PW, cv.h, g_strip_565);
  }

  // Real-time FPS Performance Monitor (reports every 5 seconds)
  static uint32_t s_fps_count = 0;
  static uint32_t s_fps_start = 0;
  s_fps_count++;
  uint32_t now_ms = millis();
  if (now_ms - s_fps_start >= 5000u) {
    float currentFps = (float)s_fps_count * 1000.0f / (float)(now_ms - s_fps_start);
    if (!page_there()) {
      Serial.printf("[PERF] Display: %.1f FPS | Free DRAM: %u bytes\n", currentFps, ESP.getFreeHeap());
    }
    s_fps_count = 0;
    s_fps_start = now_ms;
  }
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  SETUP & MAIN LOOP
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

void setup() {
  Serial.begin(115200);
  delay(100);
  setCpuFrequencyMhz(240);  // Lock ESP32 Xtensa Dual-Core to maximum 240 MHz turbo clock
  Serial.println("\n=======================================================");
  Serial.println("  OWLOS FLIGHT RADAR â€” UNIVERSAL ARDUINO EDITION");
  Serial.println("  Universal CYD & ESP32-S3 Auto-Detect Firmware");
  Serial.println("=======================================================\n");

  // 1. Load settings from NVS
  prefs.begin("owlos_radar", false);
  savedSSID = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  autoBrightness = prefs.getBool("auto_bl", true);  // Default true per standard
  manualBrightness = prefs.getInt("manual_bl", 180);
  screenRotation = prefs.getInt("rotation", 3);
  int savedTheme = prefs.getInt("theme", 0);
  int savedDist = prefs.getInt("dist_u", DIST_KM);
  int savedAlt = prefs.getInt("alt_u", ALT_FT);
  float savedRange = prefs.getFloat("range", 150.0f);
  int savedUp = prefs.getInt("up", 0);
  int savedFlip = prefs.getInt("flip", 0);
  int savedLook = prefs.getInt("look", LOOK_RADAR);
  int savedMap = prefs.getInt("map", 1);
  int savedGround = prefs.getInt("ground", 0);
  int savedLabels = prefs.getInt("labels", 0);
  prefs.end();

  if (savedFlip) screenRotation = 1;

  // 2. Dynamic Hardware Auto-Setup
  pinMode(0, INPUT_PULLUP);  // BOOT button
  tft.autoSetup(screenRotation);
  tft.setSpiClock(80000000);  // 80 MHz High-Speed SPI Acceleration
  tft.autoCalibrate();
  tft.setSwapBytes(true);
  tft.fillScreen(0x0000);

  // 3. Dynamic Resolution Probing
  computeLayout();

  // 4. Configure Backlight PWM on Auto-Detected Pin per RULE[user_global]
  pinMode(LDR_PIN, INPUT);
  uint8_t blPin = display.backlightPin();
  pinMode(blPin, OUTPUT);
  analogWriteResolution(blPin, 8);
  analogWrite(blPin, activeBrightness);

  // 5. Prime the LDR Filter Buffer at Boot (10 samples)
  for (int i = 0; i < NUM_LDR_READINGS; i++) {
    adjustBrightness();
    delay(10);
  }

  // 6. Allocate Frame Strip Buffers (IRAM preferred for 32-bit pixel canvas)
  size_t px_bytes = GFX_PW * STRIP_H * sizeof(uint32_t);
  size_t o_bytes = GFX_PW * STRIP_H * sizeof(uint16_t);
  g_strip_px = (uint32_t *)iram_first(px_bytes);
  g_strip_565 = (uint16_t *)heap_caps_malloc(o_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!g_strip_565) g_strip_565 = (uint16_t *)malloc(o_bytes);

  if (!g_strip_px || !g_strip_565) {
    Serial.println("[ERROR] Failed to allocate strip buffers!");
    while (true) delay(1000);
  }

  // 7. Initialize Authentic OWLOS Core UI
  ui_screen_name = "CYD Universal";
  ui_clock_us = my_clock_us;
  ui_version = "0.4";
  map_mem = iram_first;  // Matches Denis Kirin's main.c: iram_first preserves DRAM for TLS

  s_lock = xSemaphoreCreateMutex();
  ui_init(&g_ui, millis());
  scope_init();

  // Set identity calibration because AMK_CYD_Universal_TFT already handles touch calibration
  g_ui.cal = (touch_cal_t){ 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

  g_ui.cfg.theme = savedTheme;
  g_ui.cfg.dist_unit = savedDist;
  g_ui.cfg.alt_unit = savedAlt;
  g_ui.cfg.range = savedRange;
  g_ui.cfg.up = savedUp;
  g_ui.cfg.flip = savedFlip;
  g_ui.cfg.look = savedLook;
  g_ui.cfg.map = savedMap;
  g_ui.cfg.ground = savedGround;
  g_ui.cfg.labels = savedLabels;
  ui_set_theme(&g_ui, g_ui.cfg.theme);

  // Initialize RGB LED Pins (Active-LOW, Red: 4 or 22, Green: 16, Blue: 17)
  pinMode(4, OUTPUT);
  pinMode(16, OUTPUT);
  pinMode(17, OUTPUT);
  pinMode(22, OUTPUT);
  analogWrite(4, 255);   // off
  analogWrite(16, 255);  // off
  analogWrite(17, 255);  // off
  analogWrite(22, 255);  // off
  light_defaults(&g_ui.light);

  // Load persistent options, LED settings, watchlist, and home location from Preferences
  prefs.begin("owlos_radar", false);
  if (prefs.isKey("opt")) {
    prefs.getBytes("opt", &g_ui.opt, sizeof(g_ui.opt));
  } else {
    scope_opt_defaults(&g_ui.opt);
  }
  if (prefs.isKey("light")) {
    prefs.getBytes("light", &g_ui.light, sizeof(g_ui.light));
  }
  if (prefs.isKey("watch")) {
    String w = prefs.getString("watch", "");
    strncpy(g_ui.watch_text, w.c_str(), sizeof(g_ui.watch_text) - 1);
    watch_set(&g_ui.watch, g_ui.watch_text);
    Serial.printf("[WATCHLIST] Loaded from NVS: \"%s\" (%d targets)\n", g_ui.watch_text, g_ui.watch.n);
  }
  if (prefs.isKey("lat")) g_ui.cfg.lat = prefs.getFloat("lat", g_ui.cfg.lat);
  if (prefs.isKey("lon")) g_ui.cfg.lon = prefs.getFloat("lon", g_ui.cfg.lon);
  if (prefs.isKey("place")) {
    String p = prefs.getString("place", "");
    strncpy(g_ui.cfg.place, p.c_str(), sizeof(g_ui.cfg.place) - 1);
    g_ui.cfg.place[sizeof(g_ui.cfg.place) - 1] = '\0';
  }
  if (prefs.isKey("country")) {
    String c = prefs.getString("country", "");
    strncpy(g_ui.cfg.country, c.c_str(), sizeof(g_ui.cfg.country) - 1);
    g_ui.cfg.country[sizeof(g_ui.cfg.country) - 1] = '\0';
  }
  prefs.end();

  if (strcmp(g_ui.cfg.place, "TrenÄÃ­n") == 0 || g_ui.cfg.place[0] == '\0') {
    if (fabsf(g_ui.cfg.lat - 16.8409f) < 0.1f && fabsf(g_ui.cfg.lon - 96.1735f) < 0.1f) {
      strncpy(g_ui.cfg.place, "Yangon", sizeof(g_ui.cfg.place) - 1);
      strncpy(g_ui.cfg.country, "Myanmar", sizeof(g_ui.cfg.country) - 1);
    }
  }

  sky_init(&g_sky, g_ui.cfg.lat, g_ui.cfg.lon);
  demo_init(&g_demo, range_km(&g_ui.cfg), 42);

  // 8. Initialize OpenStreetMap Tile Engine & Link Map to UI
  tiles_init();
  g_ui.map = &s_map;

  // 9. Start Background FreeRTOS Task on Core 0 (12 KB stack depth)
  xTaskCreatePinnedToCore(
    networkTask,
    "fetch",
    12288,
    NULL,
    4,
    &g_netTaskHandle,
    0  // Core 0
  );

  // 10. Ensure Wi-Fi Station mode is always active
  WiFi.mode(WIFI_STA);
  if (savedSSID.length() > 0) {
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    Serial.printf("[WIFI] Connecting to %s...\n", savedSSID.c_str());
  } else {
    Serial.println("[WIFI] No Wi-Fi credentials saved. Ready for on-screen Wi-Fi setup.");
  }
}

// â”€â”€ Non-Blocking Serial Command Processor â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void handleSerialCommands() {
  static char s_rx_buf[256];
  static int s_rx_idx = 0;

  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      s_rx_buf[s_rx_idx] = '\0';
      if (s_rx_idx > 0) {
        String cmdLine = String(s_rx_buf);
        cmdLine.trim();

        // 1. WebSerial OWLOS Protocol
        if (cmdLine.startsWith("OWLOS HELLO")) {
          uint8_t m[6] = { 0 };
          WiFi.macAddress(m);
          const char *boardBuild = (GFX_PW >= 450) ? "st7796" : "ili9341";
          Serial.printf("OWLOS READY flight-radar 0.8 %s %02x%02x%02x%02x%02x%02x\n",
                        boardBuild, m[0], m[1], m[2], m[3], m[4], m[5]);
        } else if (cmdLine.startsWith("OWLOS GET")) {
          say_cfg();
        } else if (cmdLine.startsWith("OWLOS SKY ON")) {
          s_sky_until = millis() + 60000;
          sky_out();  // Website á€•á€±á€«á€ºá€á€½á€„á€º á€œá€±á€šá€¬á€‰á€ºá€™á€»á€¬á€¸á€€á€­á€¯ á€…á€±á€¬á€„á€·á€ºá€…á€›á€¬á€™á€œá€­á€¯á€˜á€² á€á€»á€€á€ºá€á€»á€„á€ºá€¸ Display á€•á€¼á€žá€…á€±á€á€¼á€„á€ºá€¸
          if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);
        } else if (cmdLine.startsWith("OWLOS PICK")) {
          const char *p = cmdLine.c_str() + 10;
          while (*p == ' ') p++;
          uint32_t icao = (uint32_t)strtoul(p, NULL, 16);
          g_ui.sel = icao;
        } else if (cmdLine.startsWith("OWLOS SET")) {
          const char *j = cmdLine.c_str() + 9;
          while (*j == ' ') j++;

          fr_cfg_t c = g_ui.cfg;
          bool cfg_changed = false;
          double v = 0;

          // 1. Home Coordinates & Location
          char slat[24] = { 0 }, slon[24] = { 0 }, spl[32] = { 0 }, sco[32] = { 0 };
          getJsonStr(j, "lat", slat, sizeof(slat));
          getJsonStr(j, "lon", slon, sizeof(slon));
          getJsonStr(j, "place", spl, sizeof(spl));
          getJsonStr(j, "country", sco, sizeof(sco));
          if (slat[0] && slon[0]) {
            updateHomeLocation((float)atof(slat), (float)atof(slon), spl[0] ? spl : "My Area", sco);
            c = g_ui.cfg;
            cfg_changed = true;
          }

          // 2. View Look, Theme, Zoom Range, Orientation
          if (getJsonNum(j, "theme", &v) && v >= 0 && v < UI_THEMES) {
            if (c.theme != (uint8_t)v) { c.theme = (uint8_t)v; cfg_changed = true; }
          }
          if (getJsonNum(j, "look", &v) && v >= 0 && v < LOOKS) {
            if (c.look != (uint8_t)v) { c.look = (uint8_t)v; cfg_changed = true; }
          }
          if (getJsonNum(j, "range", &v) && v >= RANGE_MIN && v <= RANGE_MAX) {
            if (fabs(c.range - (float)v) > 0.01f) { c.range = (float)v; cfg_changed = true; }
          }
          if (getJsonNum(j, "up", &v)) {
            int16_t upVal = (int16_t)((((int)lround(v)) % 360 + 360) % 360);
            if (c.up != upVal) { c.up = upVal; cfg_changed = true; }
          }
          if (getJsonNum(j, "flip", &v)) {
            uint8_t flipVal = v != 0;
            if (c.flip != flipVal) { c.flip = flipVal; cfg_changed = true; }
          }
          if (getJsonNum(j, "ground", &v)) {
            uint8_t gVal = v != 0;
            if (c.ground != gVal) { c.ground = gVal; cfg_changed = true; }
          }
          if (getJsonNum(j, "dist_unit", &v) && v >= 0 && v <= DIST_MI) {
            if (c.dist_unit != (uint8_t)v) { c.dist_unit = (uint8_t)v; cfg_changed = true; }
          }
          if (getJsonNum(j, "alt_unit", &v) && v >= 0 && v <= ALT_M) {
            if (c.alt_unit != (uint8_t)v) { c.alt_unit = (uint8_t)v; cfg_changed = true; }
          }
          if (getJsonNum(j, "labels", &v)) {
            uint8_t lVal = v != 0;
            if (c.labels != lVal) { c.labels = lVal; cfg_changed = true; }
          }

          // 3. Radar Display Options
          bool opt_changed = false;
          if (getJsonNum(j, "vectors", &v)) { if (g_ui.opt.vectors != (v != 0)) { g_ui.opt.vectors = v != 0; opt_changed = true; } }
          if (getJsonNum(j, "beam_step", &v)) { if (g_ui.opt.beam_step != (v != 0)) { g_ui.opt.beam_step = v != 0; opt_changed = true; } }
          if (getJsonNum(j, "clock", &v)) { if (g_ui.opt.clock != (v != 0)) { g_ui.opt.clock = v != 0; opt_changed = true; } }
          if (getJsonNum(j, "block", &v)) { if (g_ui.opt.block != (v != 0)) { g_ui.opt.block = v != 0; opt_changed = true; } }
          if (getJsonNum(j, "autopilot", &v)) { if (g_ui.opt.autopilot != (v != 0)) { g_ui.opt.autopilot = v != 0; opt_changed = true; } }
          if (getJsonNum(j, "lost", &v)) { if (g_ui.opt.lost != (v != 0)) { g_ui.opt.lost = v != 0; opt_changed = true; } }
          if (getJsonNum(j, "conflict", &v)) { if (g_ui.opt.conflict != (v != 0)) { g_ui.opt.conflict = v != 0; opt_changed = true; } }
          if (getJsonNum(j, "fl_lo", &v) && v >= 0 && v <= SCOPE_FL_MAX) { if (g_ui.opt.fl_lo != (int16_t)v) { g_ui.opt.fl_lo = (int16_t)v; opt_changed = true; } }
          if (getJsonNum(j, "fl_hi", &v) && v >= 0 && v <= SCOPE_FL_MAX) { if (g_ui.opt.fl_hi != (int16_t)v) { g_ui.opt.fl_hi = (int16_t)v; opt_changed = true; } }

          if (opt_changed) {
            prefs.begin("owlos_radar", false);
            prefs.putBytes("opt", &g_ui.opt, sizeof(g_ui.opt));
            prefs.end();
          }

          // 4. Watchlist Sync
          char swatch[WATCH_TEXT] = { 0 };
          if (getJsonStr(j, "watch", swatch, sizeof(swatch))) {
            if (strcmp(g_ui.watch_text, swatch) != 0) {
              strncpy(g_ui.watch_text, swatch, sizeof(g_ui.watch_text) - 1);
              watch_set(&g_ui.watch, g_ui.watch_text);
              prefs.begin("owlos_radar", false);
              prefs.putString("watch", g_ui.watch_text);
              prefs.end();
            }
          }

          if (cfg_changed) {
            ui_set_cfg(&g_ui, &c);

            prefs.begin("owlos_radar", false);
            prefs.putInt("theme", g_ui.cfg.theme);
            prefs.putInt("dist_u", g_ui.cfg.dist_unit);
            prefs.putInt("alt_u", g_ui.cfg.alt_unit);
            prefs.putFloat("range", g_ui.cfg.range);
            prefs.putInt("up", g_ui.cfg.up);
            prefs.putInt("flip", g_ui.cfg.flip);
            prefs.putInt("look", g_ui.cfg.look);
            prefs.putInt("map", g_ui.cfg.map);
            prefs.putInt("ground", g_ui.cfg.ground);
            prefs.putInt("labels", g_ui.cfg.labels);
            prefs.end();

            s_map_try = 0;  // Trigger map refresh/re-plan

            if (g_ui.cfg.flip != (screenRotation == 1 ? 1 : 0)) {
              screenRotation = g_ui.cfg.flip ? 1 : 3;
              tft.setRotation(screenRotation);
            }

            toast(&g_ui, "SYNCED FROM WEB");
            if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);
          }
        }
        // 2. Diagnostic & Help Commands
        else if (cmdLine.equalsIgnoreCase("STATUS") || cmdLine.equalsIgnoreCase("INFO")) {
          Serial.println("\n--- OWLOS FLIGHT RADAR STATUS ---");
          Serial.printf("  Location: %s%s%s\n", g_ui.cfg.place, g_ui.cfg.country[0] ? ", " : "", g_ui.cfg.country);
          Serial.printf("  Coordinates: %.4f N, %.4f E\n", g_ui.cfg.lat, g_ui.cfg.lon);
          Serial.printf("  Wi-Fi: %s (SSID: \"%s\", IP: %s)\n",
                        WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE",
                        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
          Serial.printf("  Free Heap: %u bytes\n", ESP.getFreeHeap());
          Serial.printf("  Radar Range: %d %s\n", (int)g_ui.cfg.range, g_ui.cfg.dist_unit == 1 ? "NM" : g_ui.cfg.dist_unit == 2 ? "MI" : "KM");
          Serial.printf("  Watchlist (%d): \"%s\"\n", g_ui.watch.n, g_ui.watch_text);
          Serial.println("---------------------------------");
        } else if (cmdLine.equalsIgnoreCase("HELP")) {
          Serial.println("\n--- LOCATION COMMAND GUIDE ---");
          Serial.println("  1. HOME <lat> <lon> [City] [Country]");
          Serial.println("     Example: HOME 16.8409 96.1735 Yangon Myanmar");
          Serial.println("  2. CITY <CityName>");
          Serial.println("     Example: CITY Yangon   or   CITY Bangkok");
          Serial.println("  3. Direct coordinates (lat, lon):");
          Serial.println("     Example: 16.8409, 96.1735");
          Serial.println("  4. STATUS");
          Serial.println("     Prints current location, coordinates, and Wi-Fi status.");
          Serial.println("------------------------------");
        } else if (cmdLine.startsWith("CITY ") || cmdLine.startsWith("city ")) {
          String cityName = cmdLine.substring(5);
          cityName.trim();
          if (cityName.length() > 0) {
            const BuiltinCity *city = findBuiltinCity(cityName.c_str());
            if (city != NULL) {
              updateHomeLocation(city->lat, city->lon, city->display, city->country);
              char msg[40];
              snprintf(msg, sizeof(msg), "HOME: %s", city->display);
              toast(&g_ui, msg);
              Serial.printf("[LOCATION] Matched built-in city: %s, %s (%.4f, %.4f)\n",
                            city->display, city->country, city->lat, city->lon);
              if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);
            } else {
              // Safe queueing for Core 0 networkTask to avoid dual-core TLS race condition
              if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
                snprintf(s_home_ask, sizeof(s_home_ask), "%s", cityName.c_str());
                xSemaphoreGive(s_lock);
                if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);
                Serial.printf("[SERIAL] City lookup safely queued for Core 0: \"%s\"\n", cityName.c_str());
              }
            }
          }
        } else {
          const char *p = cmdLine.c_str();
          if (strncasecmp(p, "HOME ", 5) == 0) p += 5;
          else if (strncasecmp(p, "SET ", 4) == 0) p += 4;
          while (*p == ' ') p++;

          float lat = 0, lon = 0;
          char pl[32] = { 0 }, co[32] = { 0 };
          int matched = sscanf(p, "%f %f %31s %31s", &lat, &lon, pl, co);
          if (matched < 2) matched = sscanf(p, "%f,%f %31s %31s", &lat, &lon, pl, co);

          if (matched >= 2 && lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f) {
            if (!pl[0]) strncpy(pl, "My Area", sizeof(pl) - 1);
            updateHomeLocation(lat, lon, pl, co);
            char msg[40];
            snprintf(msg, sizeof(msg), "HOME: %.3f, %.3f", (double)lat, (double)lon);
            toast(&g_ui, msg);
            Serial.printf("[SERIAL] Home location updated: %s %s (%.4f, %.4f)\n", pl, co, lat, lon);
          } else if (isalpha(p[0])) {
            const BuiltinCity *city = findBuiltinCity(p);
            if (city != NULL) {
              updateHomeLocation(city->lat, city->lon, city->display, city->country);
              char msg[40];
              snprintf(msg, sizeof(msg), "HOME: %s", city->display);
              toast(&g_ui, msg);
              Serial.printf("[LOCATION] Matched built-in city: %s, %s (%.4f, %.4f)\n",
                            city->display, city->country, city->lat, city->lon);
              if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);
            } else {
              // Safe queueing for Core 0 networkTask
              if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
                snprintf(s_home_ask, sizeof(s_home_ask), "%s", p);
                xSemaphoreGive(s_lock);
                if (g_netTaskHandle) xTaskNotifyGive(g_netTaskHandle);
                Serial.printf("[SERIAL] City lookup safely queued for Core 0: \"%s\"\n", p);
              }
            }
          } else {
            Serial.println("[SERIAL] Unknown command! Type HELP for available commands.");
          }
        }
      }
      s_rx_idx = 0;
    } else {
      if (s_rx_idx < (int)sizeof(s_rx_buf) - 1) {
        s_rx_buf[s_rx_idx++] = ch;
      }
    }
  }
}

void loop() {
  // Standard Rule: adjustBrightness() MUST be first line of loop()
  adjustBrightness();

  // Update rear RGB LED based on flight events (SOS, in-range, near, touch, test)
  updateLED();

  // Handle BOOT hardware button (GPIO 0)
  static int lastBtn = HIGH;
  int btn = digitalRead(0);
  if (btn == LOW && lastBtn == HIGH) {
    ui_cmd_t cmd = { 0 };
    ui_button(&g_ui, 1, &cmd);
  }
  lastBtn = btn;

  // Handle touch interactions
  handleTouch();

  // Handle background Wi-Fi scan state machine
  handleWifiScan();

  // Monitor Wi-Fi connection transitions
  bool nowConnected = (WiFi.status() == WL_CONNECTED);
  if (nowConnected && !isWifiConnected) {
    isWifiConnected = true;
    // Use DHCP DNS from local router (192.168.100.1) naturally, fallback only if unset
    if (WiFi.dnsIP() == IPAddress(0, 0, 0, 0)) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
      WiFi.setDNS(WiFi.gatewayIP(), IPAddress(8, 8, 8, 8));
#else
      // ESP32 Arduino Core 2.0.x backward compatibility
      #include <lwip/dns.h>
      ip_addr_t d1, d2;
      d1.type = IPADDR_TYPE_V4;
      d1.u_addr.ip4.addr = static_cast<uint32_t>(WiFi.gatewayIP());
      d2.type = IPADDR_TYPE_V4;
      d2.u_addr.ip4.addr = static_cast<uint32_t>(IPAddress(8, 8, 8, 8));
      dns_setserver(0, &d1);
      dns_setserver(1, &d2);
#endif
    }
    Serial.printf("[WIFI] Connected! IP: %s | Gateway: %s | DNS: %s\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  WiFi.dnsIP().toString().c_str());
    strncpy(g_ui.net.ssid, WiFi.SSID().c_str(), sizeof(g_ui.net.ssid) - 1);
    strncpy(g_ui.net.ip, WiFi.localIP().toString().c_str(), sizeof(g_ui.net.ip) - 1);
    g_ui.net.wifi = 1;
    g_ui.net.demo = 0;
    toast(&g_ui, "WIFI CONNECTED");
  } else if (!nowConnected && isWifiConnected) {
    isWifiConnected = false;
    Serial.println("[WIFI] Connection lost.");
    g_ui.net.wifi = 0;
    g_ui.net.demo = 1;
  }

  // Handle non-blocking Serial Monitor commands
  handleSerialCommands();

  // Render one radar frame
  renderFrame();

  // 1ms yield to prevent FreeRTOS watchdog starvation while maximizing rendering FPS
  delay(1);
}
