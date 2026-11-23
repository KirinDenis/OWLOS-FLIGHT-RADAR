---
name: esp32-data-sources
description: How small ESP32 boards get data from the web and from sensors, from the OWLOS firmwares (Flight Radar, WiFi Radar, Air Quality, Thermo Relay, the FusionEdge radio), with real code and measured facts. Use when an ESP32 firmware calls an HTTPS API on a small heap (TLS memory, certificate bundle, keep-alive, response size, JSON parsed without a library or streamed, rate limits and HTTP 429, caching, what to show when a source is down, privacy of the home point), or uses adsb.lol, adsb.fi, Flightradar24, OpenSky, Open-Meteo forecast and air quality, Nominatim, OpenStreetMap tiles in flash with its own PNG inflater, map names read from the tiles, radiation, NTP, a stats server, internet radio, Last.fm. Also for finding WiFi routers from RSSI and passive promiscuous capture, air sensors (CCS811, ADS1015, MQ135, MQ7, DHT22, HDC1080, BMP280, Zambretti forecast), a DS18B20 thermostat relay with safe states, and watchdog resets during TLS, I2C hangs and sensors that never answer.
---

# ESP32 data sources: how a small board learns about the world

Five firmwares, one habit: the outside world comes in through a small, portable model that knows nothing of hardware, says how
sure it is, and survives the source going away. This skill holds what was learnt in OWLOS Flight Radar, OWLOS WiFi Radar, OWLOS
Air Quality UI, OWLOS Thermo Relay and the FusionEdge radio port, in a form that works in a new repository with none of them at
hand. This file is the general part and the index; each service and each sensor has a file of its own.

Not here: screens, touch, flashing, settings over USB, drawing and animation. See `../esp32-lcd-touch/SKILL.md`,
`../esp32-ui-drawing/SKILL.md`, `../esp32-flash-and-settings/SKILL.md`, `../esp32-runtime/SKILL.md`, `../esp32-boards/SKILL.md`.

## How sure each fact is

Every fact carries one of these. Keep them when you copy a fact on, and never upgrade one without evidence.

- [measured]: seen on a real board here, or against the real server.
- [PC test]: the same `core/` code run on the PC against a simulated or recorded input, not on a board.
- [built]: in the firmware and compiled, no record of it running on a board. A board or path only built is "New: not run on one
  here yet".
- [docs]: read from a datasheet, a maker or a service's documentation. The date of reading is given where it matters.
- [reasoned]: worked out from the code or the numbers, not tried.

## The files

General references:

- [references/https-client.md](references/https-client.md): `net_get`, the kept connection, POST, the User-Agent,
  certificates, TLS memory on boards with and without PSRAM, timeouts.
- [references/flight-data.md](references/flight-data.md): the ADS-B Exchange v2 format, `plane_t`, the streaming reader, the
  server's clock, the fetch loop, ground planes, the guess between answers, trails, the watch list, squawks, silhouettes, the
  relay to the page.
- [references/maps.md](references/maps.md): zoom and projection, the 4-bit texels, the tile ring in flash, the inflater, names
  read from the tile pixels, outlines, the looks.
- [references/weather.md](references/weather.md): what the weather answers become: the model, the warnings, units by country,
  the sun worked out on the board, what stands in for a missing value.
- [references/air-quality.md](references/air-quality.md): the air monitor's bus, I2C by hand, the RTC guard, values with
  quality, history, the barometer forecast (Zambretti), power clues.
- [references/thermo-relay.md](references/thermo-relay.md): the thermostat: wiring, safe states, the tick, no-rise, the
  simulated room, the LED.
- [references/wifi-radar-math.md](references/wifi-radar-math.md): routers from RSSI: the solver, the 360 turn, Procrustes,
  doubt, the simulated flat.
- [references/wifi-capture.md](references/wifi-capture.md): promiscuous capture, frame offsets, tags, hopping, smoothing, the
  spectrum, the driver's scan.

Web services, one file each:

- [adsb-lol.md](references/apis/adsb-lol.md): planes and watch lookups, free. [measured]
- [adsb-fi.md](references/apis/adsb-fi.md): planes (v3) and watch lookups (v2), free, errors count toward an IP block.
  [measured]
- [fr24-api.md](references/apis/fr24-api.md): Flightradar24, a paid key, routes. [built]
- [opensky.md](references/apis/opensky.md): OAuth2 and credits worked out, not built. [docs]
- [airplanes-live.md](references/apis/airplanes-live.md), [adsb-one.md](references/apis/adsb-one.md): dropped, both answer 403
  to strangers. [measured]
- [open-meteo-forecast.md](references/apis/open-meteo-forecast.md): now, 24 hours, 4 days, the day before. [measured]
- [open-meteo-air-quality.md](references/apis/open-meteo-air-quality.md): the European air quality index and fine dust.
  [measured]
- [radiation.md](references/apis/radiation.md): the place for the nearest station's gamma reading; no source yet, the
  candidates. [docs]
- [nominatim.md](references/apis/nominatim.md): home's name and country, the search typed on the board, a refused country.
  [measured]
- [osm-tiles.md](references/apis/osm-tiles.md): OpenStreetMap's tiles, fetched into flash; the tile policy point by point.
  [measured]
- [overpass.md](references/apis/overpass.md): tried for place names, refused; never for names. [measured]
- [vrs-standing-data.md](references/apis/vrs-standing-data.md), [adsbdb.md](references/apis/adsbdb.md): routes without FR24,
  researched and declined. [docs, measured]
- [ntp-pool.md](references/apis/ntp-pool.md): the time. [measured]
- [version-json.md](references/apis/version-json.md): the site's file that says a newer firmware exists. [measured]
- [radarstats.md](references/apis/radarstats.md): the project's own count of boards: what a board sends, what is kept.
  [measured]
- [internet-radio-streams.md](references/apis/internet-radio-streams.md): what plays on an ESP32 without PSRAM, and what TLS
  leaves. [measured]
- [openweathermap.md](references/apis/openweathermap.md), [lastfm.md](references/apis/lastfm.md),
  [musicbrainz.md](references/apis/musicbrainz.md), [cover-art-archive.md](references/apis/cover-art-archive.md): FusionEdge's
  own sources, read from its code. [docs]

Sensors, one file each: [ccs811.md](references/sensors/ccs811.md), [ads1015.md](references/sensors/ads1015.md),
[mq135.md](references/sensors/mq135.md), [mq7.md](references/sensors/mq7.md),
[photoresistor.md](references/sensors/photoresistor.md), [dht22.md](references/sensors/dht22.md),
[hdc1080.md](references/sensors/hdc1080.md), [bmp280.md](references/sensors/bmp280.md), [ds18b20.md](references/sensors/ds18b20.md).

Code copies, `references/code/`, verbatim, LF, MIT, renamed so they do not collide: `flight_sky.c/.h` (`core/sky`),
`flight_watch.c/.h` (`core/watch`), `flight_light.c/.h` (`core/light`), `flight_map.c/.h` (`core/map`), `flight_tiles.c/.h`
(`firmware/main/tiles`), `png_decode.c/.h`, `wifi_map.c/.h` (`core/map`), `wifi_app.c/.h` (`core/app`), `wifi_radio.c`
(`firmware/main/radio.c`), `aq_sensor_hw.c`, `aq_i2c_bus.c`, `aq_dht22.c`, `thermo.c/.h`, `thermo_relay.c`, `thermo_ds18b20.c`.
Their `#include`s use the original names: rename on the way into a project. `flight_tiles.c` wants `net.h` for `net_up` and the
User-Agent. The flight and map copies are Flight Radar 0.4; where the current code moved on, the reference files say what changed.

## The shape that works

1. **One fetch task, one request at a time.** Every web question of the radar runs blocking on one task (core 0, priority 4, 12 KB
   stack; on a one-core chip the screen's priority). One set of buffers serves all sources, and the order is fixed: home, map,
   planes, watch, version, stats, weather. A new setting cuts the wait short. [measured]
2. **The model has no hardware in it.** `core/` is plain C, built by ESP-IDF, by MSVC for the PC preview and by Emscripten for the
   site. The firmware feeds it; the preview feeds it a simulated world where the truth is known. A demo writes the same JSON the
   real server sends, so the demo tests the parser too.
3. **Download without the lock, merge under it.** The screen draws from a copy taken once a frame and never reads the live data.
4. **Every value carries age and quality.** Never show an old reading as now. A plane is forgotten after 30 s, an air channel turns
   LOST after 15 s, a thermostat shows `--.-`. A power cut is a mark on the graph whose length nobody invents.
5. **What is not known is drawn as not known.** A hollow ring, `L/R ?`, `eCO2 ESTIMATE`, no ppm from an MQ, a forecast that says it
   is a barometer's, the sunrise where a radiation reading would be. Rather no answer than a confident wrong one.
6. **A source going away is a normal state.** The other server, a demo sky, a retry, a page that says what to check. A sensor is
   ABSENT, STARTING, OK or ERROR, never a crash. A hung chip gets left alone till power off.
7. **Be a polite client.** Name yourself in the User-Agent. Ask slower than the limit, back off on 429, cache, keep to what the
   person is looking at, say whose data it is.
8. **Privacy at the edge.** Round home before it leaves the board. Secrets stay in NVS, never on the screen, never in a log, never
   sent back.
9. **Heavy work shares the core.** A long solve or a TLS handshake below the screen's priority starves IDLE into the task
   watchdog. Yield every round; on a one-core chip give the worker the screen's priority.
10. **Saved means saved whole.** Data first, header last, in a ring. A cut write leaves the previous good one.

## TLS on a small board

Full reference: `references/https-client.md`.

- The common root bundle (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN`) covers every server here. adsb.fi needs
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` on ESP-IDF 6. [measured; the IDF 6 line run by the contributor]
- A classic ESP32 has about 24 KB in its biggest block after WiFi. mbedTLS dynamic buffers, 5 WiFi RX buffers and the WiFi's IRAM
  options off are what made room for a map beside TLS. [measured]
- One connection a request on boards without PSRAM: a kept one holds TLS's memory between requests. A kept connection on the big
  PSRAM boards, where a handshake every 5 s kept a core busy. [built, run by the contributor]
- Anything but a whole answer closes the connection: a broken one reused crashed the board. [measured]
- With TLS in PSRAM the hardware AES needs an internal DMA bounce buffer; when internal RAM is short, `esp-aes: Failed to allocate
  memory`. Smaller screen strips, or `CONFIG_MBEDTLS_HARDWARE_AES=n`. [measured from a tester's log]
- HTTPS does not wait for NTP: certificate dates are not checked on these builds, and a network that blocks NTP would never
  fetch. [reasoned]
- An HTTPS audio stream on an ESP32 without PSRAM: TLS leaves about 5 KB, too little for a decoder. A live stream (WebSocket,
  lightning, AIS) keeps TLS open for good, 20 to 40 KB: a firmware for PSRAM boards only. [measured, reasoned]

## How big an answer is

- A plane in the ADS-B Exchange format: 750 bytes to 1 KB. A busy sky 100 planes; at a 200 km range 165 KB every few seconds.
- A watch lookup: about 750 bytes a plane.
- Open-Meteo: the forecast about 2.6 KB, the day before about 2 KB; the air fits the same 4 KB buffer.
- Nominatim: 600 to 700 bytes.
- `version.json`: under 160 bytes.
- An OSM tile: kept up to 160 KB; a 256 x 256 RGB tile would be 192 KB decoded, so it is never decoded whole.
- A VRS route by callsign: about 0.8 KB.

## Three ways to read an answer in little memory

1. **Stream it** when it can be bigger than the biggest heap block: a character machine (string, escape, depth, last key) copies one
   object at a time into a fixed buffer, reads it in place, keeps the nearest N. The planes, `references/flight-data.md` section 4.
2. **Hold it whole** when it is small and bounded: a fixed buffer, fields found by name with `strstr`. Open-Meteo, Nominatim,
   `version.json`. An answer that filled the buffer may be cut: refuse it.
3. **Send it straight to flash** when it is a file: a map tile goes from the HTTP client into a flash ring in 1 KB pieces and is
   decoded later, row by row, by an inflater whose 32 KB window is in small pieces. `references/maps.md`.

Whichever way:

- Put a 0 at the end before `strtod`: the end of a buffer does not stop it.
- Know the types: `alt_baro` is a number or `"ground"`, Nominatim's `lat` is a string, `squawk` is a string of octal digits.
- `null` in a number array is "not given" (NAN), not 0.
- Read seconds since 1970 as integers: a float has not the digits.
- Combine two answers by time, never by index: the air and the forecast may start at different hours.
- Date positions by the server's clock when the answer gives one: a request takes 2.5 to 4 s and the answer is made somewhere
  inside it. [measured on recorded answers]

## Rate limits and pacing

- adsb.lol: every 5 s. Limits "dynamic"; a key will be needed in future. adsb.fi: every 5 s, 1 a second at most, and error answers
  count toward a temporary IP block. FR24: every 20 s, credits. On 429 the wait is multiplied by 3.
- The watch list: one lookup every 15 s, on the free network the planes are NOT on: both on one gave 429.
- Open-Meteo: 3 calls each 30 minutes, a minute on after a miss, at once for a new home. The free tier: non-commercial, under 10,000
  a day for the whole application.
- Nominatim: the reverse lookup once a home (again in 5 minutes if unanswered); the search only on FIND, two at most, 1100 ms apart.
  1 a second for the whole application, all boards together.
- OSM tiles: only what the view shows, kept in flash, missing ones asked again every 15 s; a round stops at the first silent
  server, 403 or 429.
- `version.json` every 12 hours (30 minutes after a miss); the stats report every 10 minutes, opt-in; NTP once, then it keeps itself.
- Work out a daily budget from credits when a service counts them: `interval = 86400 * cost / credits` (`references/apis/opensky.md`).

## Caching

- Tiles in a flash ring, as they came: the cache OSM's policy asks for, and the map works without WiFi at ranges it has shown.
- Home's name in NVS, asked once a home. Home's country in RAM, asked after each power on.
- Two weather answers: the screen reads one while the next is filled. The day before, when its answer fails, is carried over from
  the last answer.
- The last good answer stays on screen, dated, until it is too old to stand for now.
- A server's `Cache-Control` and `Expires` are followed where the firmware can read them (FusionEdge's Last.fm cache). The radar's
  tiles do not revalidate: a gap, said in `references/apis/osm-tiles.md`.

## When a source is down

- Say which source and what it answered, in the alarm colour: `ADSB.FI HTTP 429`, `FR24 NO ANSWER`, `STRANGE ANSWER`,
  `CONNECTING`.
- Fail over: a failed free server hands the next request to the other one.
- No WiFi set, or the one set gone for 20 s: a demo sky that says DEMO in big letters, answered like a server every 5 s through the
  same parser. Demo and real never mix.
- The map fails soft to the bare glass; a new range whose tiles do not come keeps the map shown.
- The weather keeps its last answer; a missing air answer leaves the air rows out; a missing radiation reading shows the next
  sunrise or sunset in its place.
- The LED's NO DATA row, amber and steady, while the WiFi set is not there or no answer comes.
- A sensor that never answered says so on its page, with what to check and what to plug in instead.

## Privacy at the edge

- Planes and Nominatim's reverse lookup get home rounded to 0.01 degree (about 1 km); the weather 0.1 degree (about 10 km). The
  scope keeps the exact point.
- What rounding does not cover: a town typed for the search goes as typed, and tile requests name the tiles round home (a few km).
  Say so in a new project.
- Keys and passwords in NVS, never shown, never logged, never sent back; the console says only whether one is stored. The URL
  never carries a key, so it may be logged.
- The stats report carries a build name and a version, nothing else; the server keeps a country, never the address.
- FusionEdge sends only Last.fm's API key; never a shared secret in firmware. Its OpenWeatherMap key travels over plain HTTP: avoid
  that in a new design.

## What never to call

- No server for the map's names: no Overpass (it refuses non-browser clients), no GeoNames or other place list, no server of the
  project's own. Names come from the tile pixels on the board, or not at all (`references/maps.md` section 6).
- No bulk tile download, no "save my area", no other tile URL than the policy's. No Nominatim autocomplete, no periodic or grid
  geocoding.
- No source that refuses strangers: airplanes.live and adsb.one answer 403.
- No wrong URLs at adsb.fi in a loop: 400 and 404 count toward its IP block (`/v3/hex` answers 400).
- OpenSky anonymous is no live radar: 400 credits a day is one box every 216 s.
- No planes and no map at a home in a refused country (`ru`, the project's choice since 0.12); the stats server does not count it.
- No HTTPS session the memory cannot hold: on an ESP32 without PSRAM an HTTPS stream or a second TLS session beside one is out.

## Sensors: the general part

Full references: `references/air-quality.md`, `references/thermo-relay.md`, `references/sensors/`.

- Ask each sensor at its known address from the board file. Autoscan was tried once and rejected.
- Read the id registers. The README's SI7021 was an HDC1080, its ADS1115 an ADS1015. Never trust the old notes. [measured]
- Bit-bang I2C with a limit on every wait: ESP-IDF 5.5's `i2c_master` spins forever after a NACK on the classic ESP32, and the
  watchdog made a restart loop of it. An RTC-memory guard marks the sensor that was being talked to when the board crashed, and
  leaves it alone till power off. [measured]
- Three failed reads in a row is ERROR; a missing or failed sensor is asked again every 10 s.
- A median of 3 (5 for light) against single wild reads; a quality with an age on every channel.
- No units a sensor cannot give: no ppm from an MQ without a calibration gas, eCO2 labelled an estimate.
- A simulated sensor never drives anything while a real reading exists, and its numbers never go to flash.
- Timing-critical one-wire reads (DHT22, DS18B20) run with interrupts off for microseconds to 5 ms. With WiFi on the same core,
  use RMT instead. [not tried]

## The areas in short

- **Flight data**: adsb.lol or adsb.fi every 5 s, FR24 every 20 s, the answer streamed, the nearest 48 kept, ground planes
  filtered while reading, positions dated by the server's clock, flown on by a guess with turns and glides between answers. A
  watch list of 8 with take-off and landing as state changes. `references/flight-data.md`.
- **Maps**: OSM tiles into a flash ring, decoded row by row into 4-bit kinds in the scope's own projection, IRAM first in word-only
  pieces, names copied word by word from the pixels. `references/maps.md`, `references/apis/osm-tiles.md`.
- **Weather**: Open-Meteo forecast, air and the day before every 30 minutes; warnings for storm, pressure, air and radiation;
  units by home's country; the sun worked out on the board. `references/weather.md`.
- **WiFi**: trilateration while moving, not triangulation. No compass, no accelerometer; a walk's map is known only up to rotation
  and mirror, and the 360 turn's heading is just time. Passive capture on the AP's own channel only. `references/wifi-radar-math.md`,
  `references/wifi-capture.md`.
- **Air**: seven boards with CCS811, ADS1015 with MQ135, MQ7 and a photoresistor, HDC1080, DHT22, and a BMP280 that never spoke;
  no network. `references/air-quality.md`.
- **Relay**: a DS18B20 thermostat on a CYD with no WiFi on purpose; nothing switches on a guess, every path falls to relay OFF.
  `references/thermo-relay.md`.

## Traps across sources

Symptom, cause, fix. The rest are in each file's own Traps.

- Next to an airport no flying plane shows: parked planes took all 48 places. Filter while reading. [measured]
- HTTP 429 from adsb.lol: planes and watch lookups on one network. Lookups on the other one. [measured]
- Planes surge and crawl backwards with every answer: positions stamped at the request. Date them by the server's clock.
  [measured on recorded answers]
- Reset every 29 s on the C3: the fetch task below the screen starved IDLE during TLS and map work. Share the screen's priority.
  [measured]
- The board reboots during a long solve next to a busy router: IDLE starved. `vTaskDelay(1)` every round. [measured]
- A board crashed after a tile came broken: the kept TLS connection was reused without its buffers. Drop it on anything but a
  whole answer. [measured]
- `tinfl` out of memory with plenty free: it wants 32 KB in one block. Own inflater, the window in small pieces. [measured]
- The map keeps holes over the sea: 1, 2 and 4 bit tiles refused. Read them; drop and re-ask a tile that will not read. [measured]
- A new feature's 1.5 KB of static buffers broke the 3.5"'s map: the tile reader had about 1 KB to spare. Soak looks and ranges for
  20 minutes after any memory change. [measured]
- 10115 finds Santo Domingo: a bare postcode. Try `<text>, <home's country>` first. [measured]
- `esp-aes: Failed to allocate memory` on a PSRAM board: AES bounce buffer with no internal DMA RAM. Smaller strips or software AES.
  [measured from a tester's log]
- Watchdog restart loop at an I2C chip: `i2c_master` waits forever after a NACK. Bit-banged I2C and the RTC guard. [measured]
- CCS811 never has data: DATA_READY read from the wrong byte. Read STATUS on its own first. [measured]
- New sdkconfig defaults do nothing: an old `build*/sdkconfig`. Delete it. [measured]

## Talked over, not built

Ideas met in the work, none called from a board: METAR (aviationweather.gov) and CAPE, SondeHub balloons, ISS passes, Germany's
Autobahn API, Finland's Digitraffic trains, AIS ships, DWD, Blitzortung lightning (its terms to be read first), places with
opening hours from OSM, worldwide traffic with the person's own key. Live streams want a PSRAM-only firmware. A server of the
project's own that gathers open radiation networks. Read each one's terms before the first request.

## Where it came from

- OWLOS-FLIGHT-RADAR (github.com/KirinDenis/OWLOS-FLIGHT-RADAR), 0.4 for the code copies, 0.16.2 beta for the rest: `core/`
  (`sky.c`, `watch.c`, `map.c`, `png_decode.c`, `light.c`, `wx_parse.c`, `wx_alert.c`, `sun.c`, `ui_scope.c`, `ui_weather.c`),
  `firmware/main/` (`main.c`, `net.c`, `tiles.c`), `sdkconfig.defaults*`, `server/`, `site/flow.js`, README, CHANGES.
  Contributions credited there: chipguyhere (the kept connection, the PSRAM boards), bertvm (ESP-IDF 6, NTP at WiFi up).
- OWLOS-WIFI-RADAR: `core/map.c`, `app.c`, `store.c`, `firmware/main/radio.c`, `flashstore.c`, `tools/preview/walk.c`, README.
- OWLOS-AIR-QUALITY-UI: `core/aq.c`, `aq.h`, `sensors.h`, `firmware/main/sensor_hw.c`, `i2c_bus.c`, `dht22.c`.
- OWLOS-THERMO-RELAY: `core/thermo.c`, `sim.c`, `firmware/main/main.c`, `relay.c`, `ds18b20.c`, README.
- FusionEdge (the project's fork of SimZs/FusionEdge): `src/core/timekeeper.cpp`, `coverart.cpp`, the no-PSRAM port, README.
- The project's working notes for what was measured, and when.
- Service docs, read on the dates given in each `apis/` file.
