# Passive WiFi capture: the reference

From OWLOS-WIFI-RADAR: `firmware/main/radio.c`, `core/app.c`, `core/ui_views.c`. Whole in `code/wifi_radio.c` and
`code/wifi_app.c`. Runs on the 2.8" CYD, the 3.5" CYD, the round C3 and the 5.79" e-paper S3. [measured] Tags as in SKILL.md.

## 1. Listen only

The radio never transmits: no probe requests, no association. The driver is in promiscuous mode on one channel at a time.
Payloads are never looked at. That is a rule of the product, say it in the README.

```c
s_q = xQueueCreate(128, sizeof(sample_t));
ESP_ERROR_CHECK(esp_event_loop_create_default());
wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_wifi_init(&cfg));
ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
// EU channels 1-13. we only receive, so tx power limits don't matter
wifi_country_t country = { .cc = "<CC>", .schan = 1, .nchan = 13, .policy = WIFI_COUNTRY_POLICY_MANUAL };   // a code where 1-13 are allowed
ESP_ERROR_CHECK(esp_wifi_set_country(&country));
ESP_ERROR_CHECK(esp_wifi_start());

wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(rx_cb));
ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
```

The manual policy keeps channels 1 to 13 as set, whatever the driver would pick. Use a code that allows them where you are. The ESP32 hears 2.4 GHz
only.

The callback runs inside the WiFi task and must stay short. It only copies a `sample_t` into a queue (`xQueueSend(s_q, &s,
0)`, a full queue counts `s_dropped`). `radio_task`, core 0, priority 6, stack 4096, takes them under the lock, up to 64 at
a time, and does the hopping and `app_tick` every 50 ms.

## 2. What is read from a frame

802.11 header, offsets into `pkt->payload`:

- 0: frame control: `type = (p[0] >> 2) & 3`, `subtype = (p[0] >> 4) & 15`.
- 1: flags: `(p[1] & 3) == 2` is FromDS, the AP sending to a station.
- 10: addr2, the BSSID in a FromDS data frame.
- 16: addr3, the BSSID in a beacon or a probe response.
- 34: beacon: capability field, 2 bytes. Bit 0x0010 = privacy.
- 36: beacon: the tagged parameters begin.

`len = sig_len - 4`: `sig_len` includes the 4 byte FCS. Under 24 is nothing; a beacon under 36 is nothing.

The frames kept:

- Beacons and probe responses (management, subtype 8 and 5): name, channel, security, level.
- FromDS data frames: only BSSID and level. Busy APs send those far more often than beacons, so a near router reads faster
  and steadier. A data frame from an unknown AP is ignored until its beacon is heard: only a beacon has the name and the
  channel.

```c
static void rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *p = pkt->payload;
    int len = (int)pkt->rx_ctrl.sig_len - 4;       // sig_len includes the 4 byte FCS
    if (len < 24) return;
    int ftype = (p[0] >> 2) & 3, sub = (p[0] >> 4) & 15;
    sample_t s;
    s.rssi = (int8_t)pkt->rx_ctrl.rssi;
    s.ch_rx = (uint8_t)pkt->rx_ctrl.channel;
    if (type == WIFI_PKT_MGMT && ftype == 0 && (sub == 8 || sub == 5)) {
        if (len < 36) return;
        memcpy(s.bssid, p + 16, 6);                 /* addr3 = BSSID */
        s.kind = SMP_BEACON;
        parse_beacon(p, len, &s);
    } else if (type == WIFI_PKT_DATA && ftype == 2 && (p[1] & 3) == 2) {
        memcpy(s.bssid, p + 10, 6);                 /* FromDS: addr2 = BSSID */
        s.kind = SMP_DATA;
        s.ch_ap = 0; s.sec = 0; s.ssid_len = 0;
    } else {
        return;
    }
    if (xQueueSend(s_q, &s, 0) != pdTRUE) s_dropped++;
}
```

The tags of a beacon, walked as id, length, data:

- 0, SSID, up to 32 bytes: all zeros or empty means hidden. A probe response may have named it already.
- 3, DS parameter set: the AP's own channel, `d[0]`.
- 48, RSN: WPA2; WPA3 when an AKM suite 00-0F-AC with type 8, 9, 18, 24 or 25 is offered.
- 221, vendor `00 50 F2 01`: old WPA.

Security in order: RSN, then vendor WPA, then the capability's privacy bit (WEP), else open. The RSN walk: skip version and
group cipher (6 bytes), a count and 4-byte pairwise suites, then a count and 4-byte AKM suites.

## 3. The level of a network

```c
// 2.4 GHz channels overlap, an AP on 6 is still heard (weaker) when tuned to 4.
// only frames on the AP's own channel give the real level
if (s->ch_rx != c->channel) return;
```

This one line matters more than any filter. Off its channel an AP reads about 14 dB a channel lower (the simulation's
number), and the map would be nonsense.

- `rssi` fast, about 1.5 s; `rssi_slow`, about 12 s. Each from `v += (x - v) * k`, `k = dt / (tau + dt)`, dt in ms + 1.
- `hist[60]`, a value a second: the mean of that second, or the smoothed level if heard in the last 5 s, else HIST_NONE.
- `app_trend`: least squares slope over the last 10 s, dB per 10 s, needs 6 values. This is the "warmer, colder" of the hunt.
- `rssi_peak`, `rssi_last`, `n`, `first_ms`, `last_ms`.
- MAX_CONTACTS 40. Full: the worst goes, score `rssi - age_ms * 0.01`, never the tracked target, and a new one weaker than
  that does not get in. Expiry after 45 s unheard, not during a turn.
- A time comparison is signed on purpose: a sample stamped just after `now` would look 49 days old.

```c
static inline int32_t since(uint32_t now, uint32_t t) { return (int32_t)(now - t); }
```

## 4. Channel hopping

`app_next_channel` only decides; the caller tunes, so it runs on the PC too.

```
tracking one network:  3 looks of 150 ms at its channel, then 1 other
turning:               only channels where something lives, 110 ms each
else:                  survey 1..13: 120 ms where networks are known, 60 ms where none
```

A full survey takes 0.8 to 1.6 s. Beacons come about every 102 ms, so a 60 ms look at an empty channel can miss one: a new network may take a few rounds to show. [read from the plan, not measured]

`esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE)`. The channel is written to `app_t` under the lock, the driver call after it.

## 5. The spectrum view

Drawn like WiFi analyser apps: one hump a network on its channel. Not a measurement of the band: the board has no spectrum
analyser, the hump is drawn from the network's level.

```c
// rough shape of a 20 MHz channel: flat top, falls off past ~1.4 channels each side
static float hump(float rssi, float d)
{
    float e = fabsf(d) - 1.4f;
    return e > 0 ? rssi - 40.0f * e * e : rssi;
}
static float spx(float ch) { return SP_L + 8.0f + (ch - 1.0f) * SP_PITCH; }            // channel -> x
static float spy(float dbm) { return SP_T + (-30.0f - dbm) / 65.0f * SP_H; }           // dBm -> y, -30 at the top
```

- 16 loudest at most, weakest drawn first so the loud ones end on top. Each from `channel - 2.3` to `channel + 2.3`,
  floor -95 dBm, grid every 10 dB.
- Tap a channel to see who sits on it (`spectrum_channel_at`), the others fade.
- The channel being tuned is lit on the axis. On e-paper the dB scale fits what is there, in 10 dB steps: with a fixed -30 to
  -95, everything real (-70 to -92) lay on the bottom.

## 6. The other way to know networks: a scan

OWLOS-FLIGHT-RADAR's WiFi page is not passive, it is the driver's scan (`firmware/main/net.c`). Three things found there:

- The driver will not scan while it is joining, and with a network set that is not there it joins every two seconds. So
  the join waits while it looks: `if (!s_up) esp_wifi_disconnect();` before `esp_wifi_scan_start`, a retry every 300 ms for
  8 s, an answer awaited 20 s, and a `esp_wifi_connect()` after.
- `esp_wifi_scan_get_ap_record` one at a time, into a short sorted list: no list of them all in the heap. `esp_wifi_clear_ap_list`
  after. Same name twice: the nearer one. Hidden ones are skipped.
- The reason code of a disconnect says what to show: AUTH_FAIL or a handshake timeout means the password, the rest means not
  found. The event task waits 2 s before trying again, the router gets a breath.

## Traps

- Levels too low, the map nonsense: frames from neighbouring channels counted. Only the AP's own channel. [design rule]
- Bins vanish in the turn: a silent contact expired. No expiry during a turn. [built]
- A contact looks 49 days old: an unsigned time difference. `(int32_t)(now - t)`. [built]
- The scan never starts (the Flight Radar's WiFi page): the driver is joining a network that is not there. Stop the join, scan,
  join again. [measured]
