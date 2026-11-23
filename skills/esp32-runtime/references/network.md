# WiFi, HTTPS, the fetch loop, time

How sure each fact is: the tags of SKILL.md. Whole files: `code/net.c`, `code/net.h` (Flight Radar as of
2026-11-04, one network), `code/radio.c` (WiFi Radar), `code/tiles.c` for a keep-alive connection. Servers, their
limits and their answers are in esp32-data-sources.

## Why this part is plain

Old OWLOS (Arduino-ESP32 1.0.4) stopped in 2021 because its network was unstable: DHCP dropped, IPs were lost,
nodes failed after a day and a half, and the watchdog did not save them. Its diagnosis: four transports with
their own route tables and logic, getters with side effects, many tasks touching shared state, persistence
scattered through setters. A restart that comes back into the same fault was called "the disease of the old OWLOS".

What the new firmwares do about it:

- ESP-IDF, not a wrapper around it.
- One task talks to the network, one request at a time, in a fixed order.
- A network failure is a state on the screen, never a reason to restart.
- The board works alone. Thermo Relay and Air Quality have no WiFi at all, on purpose: "a thermostat that works
  offline can't be broken from outside". Flight Radar flies a demo sky.

Honest limit: nobody has measured that this cures it. The longest clean logs on record are hours: 2 hours on WiFi
Radar (which never joins a network), two-hour logs and a soak of 0.16.2 on three Flight Radar boards on real WiFi,
with no restart [measured]. A soak with join, DHCP and TLS over days, with the network dropped on purpose, is still
owed.

## Station

```c
void net_start(const nets_t *l)
{
    /* ... once only ... */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));    // our NVS keeps the settings, not the driver's
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    apply(0);                                                   // the network set last, first
    ESP_ERROR_CHECK(esp_wifi_start());
}

static int apply(int at)
{
    const net_kept_t *n = &s_nets.n[at];
    wifi_config_t wc;
    memset(&wc, 0, sizeof wc);
    // the driver's fields need no ending zero when full: a 32 letter name fills all 32
    memcpy(wc.sta.ssid, n->ssid, strnlen(n->ssid, sizeof wc.sta.ssid));
    memcpy(wc.sta.password, n->pass, strnlen(n->pass, sizeof wc.sta.password));
    wc.sta.threshold.authmode = n->pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
    /* a network served by several access points: look on every channel and join the strongest.
     * ESP-IDF's default takes the first it hears, the lowest channel */
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    s_have_ssid = n->ssid[0] != 0;
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) return 0;
    s_why = WHY_NONE;                                 // a new network: nothing known about it yet
    s_at = at;
    return 1;
}
```

Things to keep:

- WiFi starts **even with no settings**, so a SET or the on-screen keyboard only has to connect. The scan works
  too. `net_start` with an empty name does nothing visible.
- `WIFI_STORAGE_RAM`: the driver writes nothing to flash, we own the settings.
- `memcpy` with `strnlen`, not `snprintf`: a 32-letter SSID would lose its last letter.
- The auth threshold is set low on purpose: the user's network may be old. Open only with no password.
- All-channel scan and join by signal: a contributor's network had one access point at -85 dBm on a low channel
  and another at -47 dBm a few metres away, and the default joined the far one [docs, FORK-NOTES.md]. It is on for
  every board. An A/B on the round C3 with it taken out changed nothing for that board's drops (265 against 315 in
  40 minutes) [measured].
- Modem sleep: `esp_wifi_set_ps` is left at the default everywhere except the 7" boards of the moving layer, which
  set `WIFI_PS_NONE`: their antenna sits behind the panel and its backlight, the signal was -77 to -85 dBm, and a
  radio sleeping between beacons missed DNS answers and lost the network. Those boards run from a wall supply
  [docs, the comment in net.c].

## Several networks

`nets.c` keeps five: the one set last first, a name it has moves up with its new password, past five the oldest
goes. The first is in NVS as `ssid` and `pass` as before, so older firmware still finds it; the rest are a blob
`nets`. On each failed join the next one is tried, round and round, and the one in reach comes up; with one network
nothing changes. A disconnect of the board's own doing (`WIFI_REASON_ASSOC_LEAVE`, reason 8) does not move on. GET
says `nets` (names only), SET `{"forget":"name"}` takes one out. Tested with the PC list test, the preview and the
site with a fake port [measured on the PC]; a board falling through from a wrong network to a right one is not
recorded [built].

## Reconnect

```c
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_have_ssid) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_up = 0;
        s_ip[0] = 0;
        const wifi_event_sta_disconnected_t *d = data;
        int reason = d ? d->reason : -1;
        // the password is wrong, or there is nobody of that name. the screen says which
        s_why = reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                reason == WIFI_REASON_HANDSHAKE_TIMEOUT ? WHY_PASSWORD : WHY_NOT_FOUND;
        ESP_LOGW(TAG, "WiFi lost (reason %d), trying again", reason);
        vTaskDelay(pdMS_TO_TICKS(2000));              // the event task waits, the router gets a breath
        if (s_have_ssid && s_scan != SCAN_RUNNING) {
            if (reason != WIFI_REASON_ASSOC_LEAVE) next_network();   // that one is the board's own doing
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        const wifi_event_sta_scan_done_t *e = data;
        scan_collect(e && e->status == 0);
        if (s_have_ssid && !s_up) esp_wifi_connect(); // back to joining
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
        s_up = 1;
        s_why = WHY_NONE;
    }
}
```

- `s_up` means "has an address", and it is the only test the rest of the firmware uses (`net_up()`).
- The retry is every 2 s, for ever, with no limit and no reboot.
- The handler runs in the sys_evt task, stack 2304 (4096 on the S3 LCD boards and the C5). Keep it small.
  `scan_collect` runs there too, with a list of 16 x 35 bytes on that stack (a 752-byte frame on the ESP32 and the
  S3), and it has been fine on the ESP32. It is the thing to look at on a stack overflow. One S3 board (CrowPanel
  Advance 2.8", a tester's) reset with `Interrupt wdt timeout on CPU0` right after a scan on the screen, on 0.4;
  the cause was not found, the S3 LCD file raised that stack to 4096 later for the two-port log, and whether that
  was the cause is [reasoned] only.
- The 2 s `vTaskDelay` blocks that event task, so other events wait. It is simple and it works here. If you
  add events that matter, replace it with a timer.
- A new network from the site: `esp_wifi_disconnect()`, `apply()`, `esp_wifi_connect()`.

**What the screen says.** The fetch task keeps a `down` time. With a network set and no WiFi, the first 20 s
show CONNECTING: a router takes a while. After that it is the demo with the reason, and the sky is checked each
second so the real one comes back at once:

```c
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
```

**A weak radio is a place, not a bug.** The round C3 on the bench: about 9 joins in 10 ended in reason 201 (no AP
found), planes about 40% of the time, -92 dBm in a soak, one evening no AP at all on two firmware versions that had
connected the same morning. The CYD and the 3.5" on the same access point had no drops [measured]. A lower TX power
on the C3 is the next thing to try, not tried. Write such a board down as "weak here", and do not tune the firmware
round it.

## Scan

The driver will not scan while it is joining, and with a network set that is not there it joins every two
seconds. So the join waits, and the scan is a small state machine polled from the frame loop:

```c
void net_scan_poll(uint32_t now)
{
    if (s_scan != SCAN_RUNNING) return;
    if (s_scan_started) {
        if (now - s_scan_asked > 20000u) {            // its answer never came
            s_scan = SCAN_FAILED;
            s_scan_started = 0;
            if (s_have_ssid && !s_up) esp_wifi_connect();
        }
        return;
    }
    if (now - s_scan_try < 300u) return;
    s_scan_try = now;
    if (!s_up) esp_wifi_disconnect();                 // a join in the way: stop it, it comes back after
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t e = esp_wifi_scan_start(&sc, false);
    if (e == ESP_OK) s_scan_started = 1;
    else if (now - s_scan_asked > 8000u) {
        s_scan = SCAN_FAILED;
        ESP_LOGW(TAG, "could not look round: %s", esp_err_to_name(e));
        if (s_have_ssid && !s_up) esp_wifi_connect();
    }
}
```

`scan_collect` reads the results one at a time with `esp_wifi_scan_get_ap_record`, so no full list sits in the
heap, keeps the 16 strongest with each name once, and ends with `esp_wifi_clear_ap_list()`. What it found before
stays on the page till the new list comes.

## Promiscuous listening (WiFi Radar)

Passive only: never a probe, never an association.

```c
wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_wifi_init(&cfg));
ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
wifi_country_t country = { .cc = "XX", .schan = 1, .nchan = 13, .policy = WIFI_COUNTRY_POLICY_MANUAL };   // your own country's two letters
ESP_ERROR_CHECK(esp_wifi_set_country(&country));
ESP_ERROR_CHECK(esp_wifi_start());
wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(rx_cb));
ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
```

- Keeps beacons and probe responses (name, channel, security, RSSI) and FromDS data frames (only BSSID and RSSI).
  Busy access points send those far more often than beacons, so a near router reads faster and steadier.
- `sig_len` includes the 4-byte FCS. Addr3 is the BSSID in a beacon, addr2 in a FromDS frame. Payloads are never
  looked at.
- The callback runs in the WiFi task: parse, `xQueueSend(.., 0)`, count drops. `radio_task` (priority 6, core 0)
  does the rest, hops by `esp_wifi_set_channel` with a dwell from the app logic, ticks the model every 50 ms.
- No IP stack is started: no `esp_netif_init`, no event handlers for WiFi.
- The round C3 heard fewer networks (10) than the ESP32 boards [measured]. Why is not known.

## HTTP over TLS

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

- `open` + `fetch_headers` + `read`, not `perform`: the body is pushed to a sink as it arrives and never held.
- `status 0` means no answer at all, a number means the server spoke. The screen tells them apart:
  `FR24 HTTP 429` and `ADSB.LOL NO ANSWER`.
- The certificate bundle `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y` was enough for adsb.lol, adsb.fi,
  OpenStreetMap, Open-Meteo and GitHub Pages [measured]. `sdkconfig.defaults` says it covers Flightradar24 too. No
  run with a key is on record. The bundle lives in flash. adsb.fi sends its root signed by an older root not in the
  bundle; `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y` handles that on ESP-IDF 6, and a 5.5 build warns
  and skips the line [docs, the comment in sdkconfig.defaults].
- Name the program in `user_agent`. OSM's policy asks for it: `OWLOS-Flight-Radar/<version> (+<repo url>)`.
- Certificate dates are not checked, see Time.
- A request takes 2.5 to 4 s over TLS on these boards, and a server makes its answer somewhere 1 to 3 s into it
  [measured]. Anything timed from an answer should use the time the server writes in it, not the moment the board
  asked: see esp32-data-sources.

**A keep-alive connection for many small files** (`tiles_fetch` in `code/tiles.c`): one client,
`.keep_alive_enable = true`, `esp_http_client_set_url` for the next one, two attempts because the server may have
closed it, and a new connection only then. A TLS handshake per tile would cost more than the tile. Check
`esp_http_client_is_complete_data_received(h)` before you trust a file. On a non-200 use
`esp_http_client_flush_response` or the next request reads the old body. Since 0.10 and 0.14 [measured]:

- **A tile that came broken drops the connection.** Reusing the kept TLS connection after a tile cut short (TLS
  short of memory: the 3.5"'s map look dipped to 3-4 KB free) crashed the next request: `LoadProhibited`, a
  `memcpy` to NULL inside mbedTLS, 6 of 6 range changes on 0.8. After the fix, 4 broken tiles, 0 crashes in 9 range
  changes on each of two boards.
- The round stops on no answer, a broken tile, 403 or 429, and only counts the rest. A failed `set_url` makes a new
  client. A tile dropped as unreadable has its header magic cleared in flash.

**A kept connection for the planes too** (the 7" and 10.1" boards only, under the moving layer's switch): only
`esp_http_client_perform` reuses a connection (the open and read calls never write a second request line), so the
body comes through its `ON_DATA` event. A plane request took 0.2 to 1.1 s instead of 5 to 10 s; a handshake was seen
at 6.5 s on the 10.1" [docs, FORK-NOTES.md]. On the other boards the original code stays: a connection kept open for
good costs memory a classic ESP32 does not have.

Blocking: a TLS handshake takes seconds of the fetch task. The console, at a lower priority, answers late (about
2.7 s) while one runs [measured]. Harmless, but do not read a slow answer as a hang. On the 10.1" P4 handshakes still
failed now and then (`-0x7280` after 20-30 s) with 110 KB free, and adsb.fi took 8-10 s for 18 KB: the link to its
WiFi chip or the servers, not the RAM [docs].

## The fetch loop

Trimmed from `fetch_task` in `main.c`, order kept:

```c
static void fetch_task(void *arg)
{
    static sky_reader_t rd;                            // the answer is read into this as it comes
    static char url[200];
    int next = -1;                                     // free server to try, -1 = the one in the settings
    uint32_t down = 0;                                 // since when the WiFi is not there
    for (;;) {
        fr_cfg_t c;
        home_find();                                   // first: the planes are asked round the new home
        lock();
        c = s_cfg;
        /* ... which source, the key, a copy for this round ... */
        unlock();
        geocode();                                     // home's country before anything is asked round it
        map_service(&c);                               // a new look or range shows its map before the planes come
        uint32_t every = FREE_EVERY_MS;
        const int up = net_up();
        if (up) net_time_start();                      // asked once the WiFi is up
        /* ... demo or not, see above ... */
        if (demo) {
            /* demo_json() through the same reader, every 5 s; each second while a set network is away */
        } else if (!up) {
            every = 1000;
        } else {
            int server = next < 0 ? (c.server < FREE_SERVERS ? c.server : 0) : next;
            /* build the url */
            int status = 0;
            lock();
            sky_read_begin(&rd, &s_sky, fr24, now_ms());
            unlock();
            int len = net_get(url, fr24 ? key : NULL, to_reader, &rd, &status);
            int n = -2;
            lock();
            if (len > 0 && status == 200) {
                n = sky_read_end(&rd, &s_sky);
                /* ... SKY_PARSE note when n < 0, else server name, time, watch list, LED ... */
            } else {
                s_sky.status = SKY_HTTP;
                if (status) snprintf(s_sky.note, sizeof s_sky.note, "%s HTTP %d", name, status);
                else snprintf(s_sky.note, sizeof s_sky.note, "%s NO ANSWER", name);
            }
            sky_forget(&s_sky, now);
            unlock();
            if (n >= 0) sky_out();                     // to a page on the cable, if one asks
            if (!fr24 && n < 0) next = (server + 1) % FREE_SERVERS;   // that one failed: the next one next time
            else if (!fr24) next = server;
            /* ... the fetch line of the log: bytes, ms, planes, heap, at least ... */
            if (fr24) every = FR24_EVERY_MS;
            if (status == 429) every *= 3;            // too many requests: slow down
            if (status != 429) watch_lookup(&rd, server);
            update_check();
            alive_report();                            // the opt-in count, every 10 minutes
            wx_service(&c);                            // the weather, every half hour
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(every)); // a new setting cuts the wait short
    }
}
```

Timings [from the code]:

- Planes, free networks: every 5 s (`FREE_EVERY_MS`); 15 s after a 429; the other server after a failure.
- Planes, FR24: every 20 s; 60 s after a 429.
- WiFi down: look each 1 s.
- Demo sky answer: every 5 s.
- Missing map tiles, or a map that could not be made: 15 s between tries.
- One watched plane: every 15 s, on the **other** server; the same one again next time.
- The weather (Open-Meteo forecast, air quality, and the past day): every 30 min; 1 min after an answer that did
  not read; at once for a new home.
- Version check: 12 h after an answer, 30 min after none. A beta build also reads the site's `beta` field; a stable
  build never hears of a beta.
- Nominatim: once per home; 1.1 s between two searches (one a second is its rule); a failed name lookup tries again
  every 5 minutes. After several quick restarts Nominatim refused, and the home's country stayed empty for 5 to 10
  minutes [measured]: a settings diff showing only that field right after flashing is this, not lost settings.
- The opt-in count to the project's stats server: every 10 minutes, only when the person said yes on the board or
  the site, and only when the site's `version.json` names a server. Empty means nothing is sent.
- One request: timeout 9 s.

Notes:

- It is a fixed table. There is no exponential back-off and no jitter. With a handful of boards it is enough. A
  large fleet on one small server would want jitter.
- Both plane and watch lookups on adsb.lol gave HTTP 429 [measured], so a lookup goes to the other network.
- The planes keep flying on the screen between answers: `plane_now()` dead-reckons from position, track and
  speed. A plane not mentioned for 30 s is forgotten, so a dead link empties the screen by itself, and the note
  says why.
- Privacy: home goes out rounded, `roundf(deg * 100.0f) / 100.0f`, about a kilometre, to the plane servers, and to
  a tenth of a degree, about 10 km, to the weather.
- The URL is logged once when it changes, with no key in it. The "WiFi up" log line names the network and the
  address: mask both before a log is shown to anyone.
- Every periodic fetch lives on this one task, one at a time. A new data source joins the loop; it does not get a
  task of its own.

## Time

WiFi Radar, Thermo Relay and Air Quality have no wall clock: no SNTP, no RTC chip, no `time()` [computed: no such
call in their `firmware/main` or `core/`]. Flight Radar has had SNTP since 0.6, at first only while its CLOCK option
was on, and since 0.15 on every board once WiFi is up [from the code and CHANGES.md]:

```c
void net_time_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&c);
}

uint32_t net_utc(void)
{
    time_t t = time(NULL);
    return t > 1700000000 ? (uint32_t)t : 0;          // not set yet, it counts from 1970
}
```

What uses the wall time there: the clock on the radar, the weather's hours and the hand on its dial, the night
backlight (the sun's height at home, once a minute), GET's `utc`. Everything else still runs on the board's own
clock, and nothing waits for the time:

- **No gate on HTTPS for the time.** A contributor's pull request made every request wait for NTP; it was taken
  out, because a network that blocks NTP would then never fetch [reasoned, in the review].
- With no time yet, `net_utc()` is 0 and each user says so: no clock, a day without night.

The board's own clock:

```c
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static uint32_t now_us(void) { return (uint32_t)esp_timer_get_time(); }     // differences only
```

- Elapsed time is always a subtraction of unsigned values: `now - t_stat >= 10000u`, or signed for a deadline,
  `(int32_t)(now - next_hop) >= 0`. Unsigned survives the wrap: 49.7 days for ms, 71.6 minutes for us
  [computed]. A signed deadline left set after it passed reads as ahead again 24.8 days later: clear it, see
  [tasks-and-watchdog.md](tasks-and-watchdog.md#clock-wraps).
- `0` often means "never", so a stored time is `now ? now : 1`.
- A plane's `seen` is our own ms at the request, adjusted by the server's own time in the answer (see
  esp32-data-sources), not the server's wall time.
- Histories have no dates: Thermo marks a power-on with a dashed line, "the board has no clock, so the length
  of the gap isn't invented".
- `uptime` and `ran` are seconds of this clock. A test like `now < UI_SCREEN_PICK_MS` would fire again after a wrap.

**TLS without a clock works** because `CONFIG_MBEDTLS_HAVE_TIME_DATE` is off by default: "valid from" and "valid
to" are ignored [docs, IDF Kconfig]. So an expired certificate is accepted, and the chain is checked only against
the bundle. This is still so in Flight Radar with SNTP on. If a project needs date checks it needs the time before
its first request, and a plan for networks where NTP is blocked.

If calendar time is needed in a record one day: the owlos-revival discussion proposed records that keep (boot
number, uptime) and get a timestamp when time is learnt. A proposal only.
