# HTTPS on a small board: the client every source goes through

From OWLOS-FLIGHT-RADAR: `firmware/main/net.c`, `net.h`, `tiles.c`, `main.c`, `sdkconfig.defaults*`, CHANGES.md. Tags as
in SKILL.md: [measured], [PC test], [built], [docs], [reasoned].

## 1. One task asks, one request at a time

- Every web question of the radar runs on one fetch task: planes, the watch list, Nominatim, map tiles, the weather, the
  version check, the stats report. Blocking, one at a time, so one set of buffers serves them all. [measured]
- Fetch task: core 0, priority 4, 12 KB stack. On a one-core chip (the C3) it shares the screen's priority (5): below it,
  it had under 1 ms a frame, and TLS plus map work starved IDLE into the task watchdog, a reset every 29 s. [measured]
- On the JC8012 and the Waveshare 7 its 12 KB stack lives in PSRAM: the internal RAM has not got it to spare, and with code
  run from PSRAM a flash write leaves the cache on, so a stack there is safe. By chipguyhere. [built]
- The order in a round: a home typed on the screen first, then Nominatim's reverse lookup, then the map, then the planes,
  then the watch lookup, the version check, the stats report and the weather. A new setting cuts the wait short
  (`ulTaskNotifyTake`). [built, run on the boards]
- The console answers late (about 2.7 s) while the fetch task is in a TLS handshake: the console task's priority 3 is under
  the fetch task's 4. Old behaviour, left as it is. [measured]
- The ui task's waits for the shared lock can be logged with the fetch task's phase (`FP_*`: home, geocode, map, http,
  merge, watch, update, alive), to find out whether a lag is the network's. Added by chipguyhere for his boards. [built]

## 2. net_get

Every GET goes through `net_get(url, bearer, sink, ctx, &status)`: the body goes to `sink` in pieces as it arrives, only
for status 200, and is never held whole. Returns the bytes read, -1 for no connection or no answer.

The plain version, one connection a request, on every board without PSRAM (`code/` has none of this file; this is the
whole of it):

```c
int net_get(const char *url, const char *bearer, net_sink_fn sink, void *ctx, int *status)
{
    static char buf[1024];                            // one piece; the fetch task is the only caller
    *status = 0;
    if (!s_up) return -1;
    esp_http_client_config_t c = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 9000,
        .user_agent = NET_USER_AGENT,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) return -1;
    esp_http_client_set_header(h, "Accept", "application/json");
    if (bearer && bearer[0]) {
        static char auth[640];
        snprintf(auth, sizeof auth, "Bearer %s", bearer);
        esp_http_client_set_header(h, "Authorization", auth);
        esp_http_client_set_header(h, "Accept-Version", "v1");
    }
    int len = -1;
    if (esp_http_client_open(h, 0) == ESP_OK) {
        esp_http_client_fetch_headers(h);
        *status = esp_http_client_get_status_code(h);
        len = 0;
        while (*status == 200) {
            int r = esp_http_client_read(h, buf, sizeof buf);
            if (r <= 0) break;
            sink(ctx, buf, r);
            len += r;
        }
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return len;
}
```

The kept connection, on the boards with the moving layer (`RADAR_OVERLAY`: the Waveshare 7 and 7B, the JC8012), by
chipguyhere:

- Why: the planes are asked every 5 s, and a TLS handshake each time (6.5 s seen on the JC8012, measured by the
  contributor) kept the first core and the PSRAM busy.
- Only `esp_http_client_perform` reuses a connection: the open and read calls never write a second request line. So the
  body comes through the `HTTP_EVENT_ON_DATA` event (status 200 only) into the same sink.
- `keep_alive_enable = true`. A request to another host closes it inside `esp_http_client_set_url`. A failed `set_url`
  means a new client.
- A kept connection the server closed meanwhile fails: close it and try once more on a new one, unless part of a body
  already went into the sink (never feed a sink twice).
- The authorization headers are deleted when a request has no bearer: the client keeps headers between requests.
- `net_get_stats` counts requests on a kept connection against connections made, for the log.
- The other boards keep one connection a request: a kept one holds TLS's memory between requests, and the boards without
  PSRAM have none to spare. [built; run by the contributor on his boards]

`net_post_json(url, body, &status)`: POST with `Content-Type: application/json`, 4 s timeout ("a server that is down holds
the planes up this long, no more"), 512 byte buffers both ways, the answer's body not read. Used only by the stats report
(`apis/radarstats.md`). [built, run on the boards]

## 3. Naming the board

One User-Agent on every request, the project's name, version and address. OpenStreetMap asks for one it can tell apart, and
refuses library defaults:

```c
#define NET_USER_AGENT "OWLOS-Flight-Radar/" FLIGHT_VERSION " (+https://github.com/KirinDenis/OWLOS-FLIGHT-RADAR)"
```

A new repository writes its own.

## 4. Certificates

- `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` with the common roots (`..._DEFAULT_CMN`): enough for the ADS-B networks, FR24,
  OpenStreetMap, Open-Meteo and GitHub Pages. [measured]
- adsb.fi sends its GTS Root R4 signed by an old GlobalSign root that is not in the bundle, while the same GTS root is
  trusted on its own. ESP-IDF 6 needs `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`. A 5.5 build warns that it
  does not know the line and skips it. Brought in by bertvm with his IDF 6 builds. [built here on 5.5; run by the
  contributor on 6]
- Certificate dates are not checked on the 5.5 builds (`CONFIG_MBEDTLS_HAVE_TIME_DATE` off). So HTTPS does not wait for
  NTP. A contributor's gate that held every request until the clock was set was taken out: a network that blocks NTP would
  never fetch. Asking the time as soon as WiFi is up was kept. [reasoned, from the config]

## 5. Memory for TLS

The classic ESP32 has no PSRAM on these boards. After WiFi starts its biggest heap block is about 24 KB. [measured]

What made room, in `sdkconfig.defaults`:

```
# TLS buffers only while they are used, the certificates let go after the handshake
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y
# 5 receive buffers kept for good instead of 10 (1.6 KB each)
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=5
CONFIG_ESP_WIFI_RX_BA_WIN=5
# the WiFi's code stays in flash, not in IRAM: frees about 36 KB of IRAM for the map
# CONFIG_ESP_WIFI_IRAM_OPT is not set
# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set
```

Delete `build*/sdkconfig` to make new defaults apply: an old one keeps the old values. [measured]

Numbers seen on the boards:

- Two-hour logs on 0.15: no leak on any of three boards. The heap at fetch time stayed flat: the 3.5" about 44 KB, the
  2.8" CYD about 68 KB, the round C3 about 57 KB. [measured]
- With the weather look added, the 2.8" CYD's heap went down to about 25 KB during fetches. [measured]
- The 3.5" in its square map looks: the map takes 77 KB, about 43 KB stays free, the tile reader takes about 38 KB. Its
  least free heap fell to 880 bytes once, and to 2.4 to 4 KB on other runs. That is why its tiles came broken at all.
  [measured]
- An ESP32 radio playing an HTTPS stream (FusionEdge, no PSRAM): TLS leaves about 5 KB. [measured, see
  `apis/internet-radio-streams.md`]
- A live stream (lightning, AIS over WebSocket) keeps TLS open for good, 20 to 40 KB. The 3.5" classic ESP32 bottoms out
  near 5 KB already, so such sources belong in a firmware for S3 boards with PSRAM. [reasoned]

Boards with PSRAM:

- On the ESP32-C5 (NM-CYD-C5) the screen's strips and the WiFi left about 35 KB of internal RAM on the first real board, and
  the screen did not start. TLS and the WiFi's buffers went to PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`,
  `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`), and `CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=6` (in that mode the WiFi keeps its
  send buffers for good, 16 of 1.6 KB unless told). [measured: the screen then worked]
- Then every TLS read failed with `esp-aes: Failed to allocate memory`, after the certificate was validated. The hardware
  AES copies data that lies in PSRAM through a DMA bounce buffer of up to 1600 bytes, which must be internal and
  DMA-capable. The internal RAM was spent: 103.7 KB of heap after static data, and 24-row strips took 61,440 bytes. Plain
  malloc still found 16 KB of LP RAM, which is not DMA-capable. [measured by a tester's log, cause read from
  `esp_aes_dma_core.c`]
- The fix: 12-row strips on the C5 (30 KB back). [built, not run on a C5 here]. The fallback is
  `CONFIG_MBEDTLS_HARDWARE_AES=n`: AES in software from ROM tables, no RAM cost. chipguyhere met the same error on the
  Waveshare 7 and set exactly that. [run by the contributor]
- Why only those: the ESP32 and the C3 have no PSRAM, so TLS is in DMA-capable RAM and needs no bounce; the ESP32's AES has no
  DMA at all; the S3 LCD boards keep TLS internal. [reasoned]

## 6. Timeouts and retries

- `timeout_ms = 9000` for every GET and tile. A tile server that answered nothing cost 18 s a tile (two tries), so a round of
  tiles stops at the first silent one. [measured]
- A failed plane answer moves to the other free server next time. A 429 multiplies the wait by 3.
- Retries per source are in the source's file. Each try costs a TLS handshake: on the 3.5" every 5 s of them cut the heap
  into pieces the tile reader could not use, so a picture that could not be made is asked again 15 s on, not every round.
  [measured]

## Traps

- A long Bearer token fails: `auth[640]` and `buffer_size_tx = 1024` are fixed, sized for FR24. Size them from a real token.
  [reasoned, not seen]
- A tile cut short (TLS short of memory) left the kept connection without its buffers, and the next request written into it
  crashed the 3.5" (LoadProhibited, a memcpy to NULL inside mbedTLS, 6 of 6 range changes). Fix: anything but a whole answer
  drops the connection. [measured]
- New sdkconfig defaults do nothing: an old `build*/sdkconfig` keeps the old values. [measured]
- `esp-aes: Failed to allocate memory` on a PSRAM board: see section 5.
