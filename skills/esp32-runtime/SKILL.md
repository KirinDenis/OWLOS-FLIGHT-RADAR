---
name: esp32-runtime
description: Keeping ESP-IDF 5.5 firmware with a screen alive on ESP32, ESP32-C3, C5, S3 and P4 boards. Project layout (portable core/ with no hardware, board.h, one build per screen, sdkconfig.defaults chains, partitions, version from CHANGES.md, flash budget), FreeRTOS tasks, priorities, stacks, locks, the task watchdog, the second core drawing, heap and PSRAM per board with measured frame times, DMA strips, WiFi station and scan, HTTPS with the certificate bundle, TLS memory (esp-aes Failed to allocate memory), the fetch loop, SNTP, NVS and raw flash rings, core dumps, reset records, the 10 s log, the USB serial console, e-paper keys and frame hold, and testing without a person (PC preview, WebAssembly twin, fake Web Serial board, bench rigs). Use when writing CMake, sdkconfig or partitions, adding a task, a request or a flash write, when a board reboots, hangs, stands still, runs out of heap or fails TLS, when reading a core dump or the log, or when deciding what can be tested off the board.
---

# Keeping the firmware alive

What was learnt keeping these firmwares running: OWLOS Flight Radar (the newest and the largest: 19 builds, read it
first), OWLOS WiFi Radar, OWLOS Thermo Relay, OWLOS Air Quality UI, and the no-PSRAM port of FusionEdge, an internet
radio. All but FusionEdge are ESP-IDF 5.5, plain C, no Arduino, one screen drawn in strips. This file is the map: the
decisions, the numbers, the stories in a line. The copyable detail is in `references/`.

## Where to go

- [references/project-skeleton.md](references/project-skeleton.md): every CMake file, the sdkconfig chains per chip
  and board, partition tables, the build names, the flash speed question, the flash budget, the WebAssembly build.
- [references/tasks-and-watchdog.md](references/tasks-and-watchdog.md): all tasks, `app_main` order, locks, the
  queue, the watchdog stories with code, the second core, the USB console, the e-paper loop, clock wraps.
- [references/memory.md](references/memory.md): what is left on each board, allocators, strip arithmetic, TLS and
  the AES, PSRAM per board group, the inflater, FusionEdge's no-PSRAM traps.
- [references/network.md](references/network.md): station, several networks, reconnect, scan, promiscuous mode,
  HTTPS, keep-alive, the fetch loop and its timings, SNTP and time.
- [references/storage.md](references/storage.md): NVS rules and keys, versioned blobs, the slot ring, the tile ring,
  wear arithmetic.
- [references/crashes.md](references/crashes.md): core dumps (UART and flash), addr2line, reset records, RTC guards,
  the stories, the log line by line.
- [references/testing.md](references/testing.md): the PC preview, the WebAssembly twin, the site against a fake
  board, bench rigs, flashing as a regression test, what needs a person.
- [references/bench-rigs.md](references/bench-rigs.md): debugging a board over its serial port, the order of a
  bench session, and each rig of `files/bench/` (settings, looks, logs, soak, e-paper) with what it found.
- [references/traps.md](references/traps.md): about 120 traps: symptom, cause, fix.
- `references/code/`: whole files to copy, as they were on 2026-11-04: `net.c`, `tiles.c`, `histstore.c`,
  `png_decode.c` (the older 4 KB-piece version, see memory.md), `radio.c`, `i2c_bus.c`.

Other skills in this collection: [esp32-lcd-touch](../esp32-lcd-touch/SKILL.md) has the panels, the strip and DMA
driver and e-paper; [esp32-flash-and-settings](../esp32-flash-and-settings/SKILL.md) the flasher, the console
protocol and CI; [esp32-ui-drawing](../esp32-ui-drawing/SKILL.md) the renderer and the PC tools;
[esp32-data-sources](../esp32-data-sources/SKILL.md) the servers and their answers;
[esp32-boards](../esp32-boards/SKILL.md) the chips and pins; [esp32-project-playbook](../esp32-project-playbook/SKILL.md)
the way of working.

## How sure each fact is

Every number here and in the references carries one of these:

- **[measured]**: seen on a real board here, in its log or on the glass. "Measured on the PC" means a PC rig.
- **[built]**: compiled for that board, never run on one here.
- **[computed]**: worked out from the code, or read from a built ELF, bin or sdkconfig; not observed on a board.
- **[docs]**: read in Espressif's documentation or the IDF 5.5 source, or in what a contributor or a tester reported
  from their own board (it says which). Read, not seen here.
- **[reasoned]**: worked out from the above, not checked.

Where a fact has no tag it is a design choice you can read in the code. A guess is written as one. Do not upgrade
a word without new evidence.

## The decisions, in short

1. **`core/` has no hardware.** Plain C, no `esp_*`, no FreeRTOS, so the same files build the firmware, a PC
   program and a WebAssembly page. It gets time as a `now` argument and everything else through hooks the firmware
   sets (`ui_clock_us`, `map_mem`, `map_scratch`, `map_yield`). If a change needs a FreeRTOS include in `core/`, it
   needs a hook instead.
2. **One build per screen, chosen at compile time** (`RADAR_DISPLAY`, `config.h`, `board.h`). No run-time pin
   assignment. The boards differ in flash, cores, RAM and PSRAM, and a compile switch costs nothing.
3. **Data on core 0, screen on core 1.** WiFi is pinned to core 0, so the fetch task sits with it and the screen
   gets core 1, its SPI interrupt too. One-core chips (C3, C5) give fetch and screen equal priority.
4. **Each loop gives the core back.** `vTaskDelay(1)` at the end of a frame, a yield hook inside long maths.
   `CONFIG_FREERTOS_HZ=1000` makes that 1 ms and not 10.
5. **The task watchdog panics at 10 s and the panic prints a core dump.** A hang becomes a reboot plus a file with
   the stack of every task. Silent freezes are the worst bug here; this turns them into evidence.
6. **Nothing big in one piece.** After WiFi the heap has many small holes. Strips, not a framebuffer. Streams, not
   whole answers. A 32 KB window in 512-byte pieces, not one block.
7. **The board works without the network.** No WiFi means a labelled demo, never a blank screen and never a reboot.
   A lost connection is retried for ever by the event handler; the board is not restarted for it.
8. **Flash writes are slow and limited.** Settings wait 3 s for the last change. Anything written often goes to a
   ring on a raw partition, data first, header last. Each new setting gets an NVS key of its own.
9. **Secrets go in, never out.** Not to the log, not to the screen, not to GET: GET says only whether a key is
   stored, and of the networks only their names. Home leaves the board rounded: 0.01 degree (about a kilometre)
   to the plane servers, 0.1 degree (about 10 km) to the weather.
10. **What the firmware does not know, it says so.** A saved network shows faded, an old reading shows `--.-`, a
    gap in the history is drawn as a gap, a clock not yet set is not drawn.
11. **Memory is the limit, not code.** Every new data source or screen is "fetch and draw" now. The question is
    always what it costs the smallest board in heap and flash, measured on the busiest look.

The OS question is closed: one repository per project, no OS, no shared core (esp32-project-playbook).

## Project layout

```text
core/                 portable C: an IDF component, and what MSVC and Emscripten build
  config.h            RADAR_DISPLAY -> GFX_W, GFX_H, RADAR_ROUND, RADAR_EPAPER, RADAR_HAS_TOUCH
  demo.c, sim.c       made-up data that goes through the real parser or model
firmware/
  CMakeLists.txt      EXTRA_COMPONENT_DIRS ../core, RADAR_DISPLAY, SDKCONFIG_DEFAULTS chains, version and beta
  main/board.h        pins and parts for each RADAR_DISPLAY: the only place pins live
  main/main.c         tasks, the lock, the fetch loop, the console's lines, app_main
  sdkconfig.defaults  + one file per chip, + one per board group (s3lcd, s3rgb, wave7, jc8012)
  partitions*.csv     4 MB, 2 MB (round C3), 16 MB, and two with a coredump partition
  BUILD.BAT           <screen> [port], own build folder and sdkconfig per screen
web/                  twin.c, BUILD.BAT, emcc.args: core/ in the browser
tools/preview/        core/ plus a script of touches: PNGs and a film
tools/fakeserial.js   a pretend Web Serial port with boards behind it
```

`main/CMakeLists.txt` lists every driver and each file compiles to nothing unless `board.h` picks it. The version
is the first `## x` line of `CHANGES.md`, read by CMake, the preview, the twin and CI; `(beta)` on that line makes a
beta build. Every core file is listed three times (IDF, preview, twin): add it to all three.

**Flash speed.** WiFi Radar, Thermo Relay and Air Quality run DIO 40 MHz. Flight Radar runs QIO 80 on every LCD
board (the chip files win over the 40M line in `sdkconfig.defaults`; the CMake note saying the CYD hangs at 80 is
out of date) and DIO 40 on the e-paper S3 [computed]. QIO 80 took the 3.5" radar from 124 to 99 ms a frame, and the
bench boards have run hours at it with no restart [measured]. The 80 MHz hang WiFi Radar met was never explained.
For a new board: DIO 40 first, QIO 80 for speed, then a long log.

**Flash budget** [computed from the built bins, 2026-11-21 to 23]: the 4 MB layout gives the app 2 MB, and Flight
Radar's builds use 57% to 69% of it (the C5 and e-paper builds are the largest). The **round C3 on the 2 MB layout
has 1 319 968 of 1 376 256 bytes used: about 56 KB, 4%, left** [measured from the 2026-11-23 build]. It was 88% on
2026-11-04. There is no OTA: one `factory` app, updates through the browser flasher. When the C3 fills, the tiles
partition (640 KB) can give room. Detail: [project-skeleton.md](references/project-skeleton.md).

## Tasks

Flight Radar on a two-core chip, with IDF's own for scale (theirs are [docs]):

- wifi (IDF): core 0, priority 23. A promiscuous callback runs here.
- esp_timer (IDF): core 0, 22. sys_evt (IDF): core 0, 20, stack 2304 (4096 on S3 LCD and C5). tiT: any, 18.
- ui: core 1, 5, stack 8192. Touch, BOOT, keys, copy the sky, draw and send strips, the 10 s log. Watched.
- fetch: core 0, 4 (5 on one core), stack 12288. Every request, TLS, tiles, the map, geocoding, the weather.
- strips: core 0, 4, stack 7168. The second core's half of the drawing.
- console, console usb: any, 3, stack 4096. Lines from the site over USB.
- keys: 4, stack 2048. The e-paper boards' keys, every 10 ms.
- main: core 0, 1. `app_main`, returns when all is started. IDLE0, IDLE1: 0. The watchdog checks these.

The rule: the screen outranks the data on its own core, and nothing of ours outranks WiFi. On a one-core chip, fetch
below the screen got under 1 ms a frame, a map took over ten seconds, and the C3 reset every 29 s [measured].

Sharing, in the order to reach for it: one writer per struct where it can be done; one mutex held for a copy, never
for I/O (the screen copies the 7.7 KB sky each frame and draws from the copy; the download streams without the
lock, only the merge takes it); flags and a task notification for requests; a queue only where a driver callback
produces. `app_main` starts the console and says READY last, once everything it may touch exists. Detail and code:
[tasks-and-watchdog.md](references/tasks-and-watchdog.md).

## The watchdog

```text
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10         default is 5 and no panic [docs]
CONFIG_ESP_TASK_WDT_PANIC=y              a timeout is a crash: backtrace, core dump, reboot
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
```

`esp_task_wdt_add(NULL)` in the tasks whose loop must never stop (ui, sensors, control), after their slow start-up,
with `esp_task_wdt_reset()` once a loop. The fetch task is not added: one request may take its 9 s timeout.

The story that taught it: WiFi Radar's walk-map solver did 1.2 s of maths on the bench and 10 s next to a busy
router, on core 0 with WiFi. Idle starved, the watchdog fired, and the restored map brought the job straight back:
a reboot every minute or two [measured]. A yield hook after every round fixed it. **Work on core 0 is measured
beside WiFi, not alone.** Any loop that may pass about 100 ms needs a yield: `if ((i & 8191) == 0) { vTaskDelay(1);
esp_task_wdt_reset(); }`.

A board busy by design for seconds (the 7" moving-layer boards, with TLS handshakes beside heavy drawing) watches the
ui task instead of core 0's idle task, and paces its work [docs, the contributor's notes].

## The second core

`ui_strip` only reads what `ui_frame` set up, so a second task on core 0 can draw part of every frame. Pairs (every
other strip, two strips in one transfer) on the 3.5" and the PSRAM boards; halves (the lower rows of this core's own
strip, only a 7 KB stack) on the 2.8". It starts only with 48 KB to spare for TLS. Gains [measured]: 3.5" radar 139 ->
98 ms with the flash speed, weather 6 -> 9 fps; 2.8" at 200 km with 48 planes, radar 9 -> 12 fps, weather 12 -> 17.
About 1.25x to 1.3x, because core 0 also carries WiFi. A PC rig proves the read-only rule by drawing every page whole,
in strips and in halves, and comparing pixels.

## Memory per board

The largest single block matters more than the sum: about 24 KB with everything running on a classic ESP32
[measured]. So a 76 KB strip, the ROM inflater's 32 KB window and a 75 KB map in one piece all fail.

- **2.8" CYD** (classic ESP32, no PSRAM): two-hour log at 0.14, heap at fetch time flat at about 68 KB; 0.16.2 starts
  with 34 KB free. Weather look 2026-11-23: 17-18 fps, 53-56 ms a frame, heap 28-46 KB, no crash in that run
  [measured]. Strips of 24 rows, the second core in halves.
- **3.5" ST7796 480x320** (classic ESP32, no PSRAM), the edge of memory: the square map is 77 KB, the tile reader
  about 38 KB, least free heap seen 880 bytes. Every lesson below came from it: window pieces 4 KB -> 1 KB -> 512 B,
  strips 8 -> 6 rows when the weather came (153 of 153 maps whole after). Weather look 2026-11-23: 9-12 fps, 80-106
  ms a frame, heap 35-52 KB, no crash in that run [measured]. A feature that holds a TLS connection open for good
  does not fit here.
- **Round C3** (one core, no FPU, no PSRAM): flash is its limit (4% left). Two-hour log at 0.14: heap at fetch time
  about 57 KB; lowest in the 0.16.2 soak 11.2 KB [measured]. Strips of 16 rows.
- **PSRAM boards** (S3, C5, P4): PSRAM does not make internal RAM bigger. Strips, DMA buffers, interrupt state and
  the AES bounce buffer stay internal. The C5 stopped twice for internal RAM with its PSRAM nearly empty: first the
  screen (35 KB left), then every TLS read (`esp-aes: Failed to allocate memory`, the hardware AES needs a DMA
  bounce buffer of up to 1600 bytes in internal RAM when TLS is in PSRAM) [docs, a tester's reports]. Fixed by
  12-row strips [built]; `CONFIG_MBEDTLS_HARDWARE_AES=n` is the fallback the 7" boards use. Log `MALLOC_CAP_INTERNAL`
  on these boards, never only the total.

Rules that came out of it:

- **Strip height is set by the heap.** `GFX_W * rows * 4` of work plus `GFX_W * rows * 2` twice for DMA. Fewer rows
  cost CPU: anything a strip pass repeats multiplies by the strip count (trails walked per strip: 62 ms a frame; once
  a frame: 7 ms) [measured].
- **DMA buffers** are `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, the work strip `MALLOC_CAP_INTERNAL`.
- **IRAM as a heap** on the classic ESP32: `MALLOC_CAP_EXEC | MALLOC_CAP_32BIT`, word access only, about 36 KB more
  with the WiFi IRAM options off. The map lives there; on PSRAM boards in PSRAM.
- **TLS:** dynamic buffers, the two `DYNAMIC_FREE` options, WiFi static RX buffers 5 and BA window 5. When TLS cannot
  get memory the request fails, the screen says `NO ANSWER`, nothing crashes.
- **Statics count too.** 3.8 KB of new statics took the minimum from 10.5 to 6.4 KB; a 3.6 KB malloc per tile
  emptied a third of the 2.8"'s maps. Jobs that never overlap share one buffer (a union) [measured].
- **Re-run the memory-hungry cycle after every feature.** The weather's 1.5 KB of statics broke the 3.5"'s maps.

All of it, the allocators and the PSRAM groups: [memory.md](references/memory.md).

## WiFi, TLS, time

Old OWLOS stopped in 2021 because of its network: DHCP dropped, IPs were lost, nodes failed after a day and a half,
and the watchdog did not save them. So this part is plain. One fetch task makes every request, in turn. The driver
reconnects from the event handler for ever with a 2 s pause, going round up to five remembered networks. The board
does not reboot for a network problem. `WIFI_STORAGE_RAM`: the settings live in our own NVS.

- `net_get()` opens, reads headers, streams the body to a sink in 1 KB pieces: `timeout_ms 9000`, the certificate
  bundle, a named user agent. The parser takes one object at a time.
- Join the strongest access point of a network (`WIFI_CONNECT_AP_BY_SIGNAL`, all-channel scan) [docs].
- No WiFi set, or the one set gone for over 20 s (CONNECTING till then): the demo sky, DEMO in big letters, and why.
- **Scan:** the driver refuses to scan while joining, so `net_scan_poll` disconnects first if not up, retries each
  300 ms, gives up after 8 s, and the join resumes after.
- **Fixed back-off**: free networks every 5 s, FR24 every 20 s, a 429 multiplies the wait by 3, a failed answer moves
  to the next server, tiles retry after 15 s, the weather every 30 min, the version check 12 h after an answer.
- **Keep-alive** for many small files; and a tile that came broken drops the connection, or the next request crashes
  inside mbedTLS [measured].
- **Time**: Flight Radar starts SNTP once WiFi is up; nothing waits for it, and HTTPS is never gated on it (a
  network that blocks NTP would never fetch). Certificate dates are not checked: `MBEDTLS_HAVE_TIME_DATE` is off by
  default [docs]. Everything else runs on `esp_timer_get_time()` as `uint32_t` ms, compared by subtraction.
- **Promiscuous** (WiFi Radar): `WIFI_MODE_NULL`, never transmits. `references/code/radio.c`.

Not known: no multi-day WiFi soak is recorded. The longest clean runs are hours. Do not write that the old
instability is solved. Detail: [network.md](references/network.md).

## Storage

- Settings: one versioned blob in NVS, saved 3 s after the last change; a new version reads the old one as a prefix
  over the defaults, guarded by `_Static_assert(offsetof(...))`.
- Each new feature's settings: an NVS key of its own, so a flash back to an older version keeps the rest.
- Secrets: NVS strings, never read back out. Touch alignment, screen profile: NVS. The run's length: NVS, once a
  minute.
- Rings on raw partitions for what is written often: each save to the next slot, header with magic, sequence,
  length and CRC written **after** the data; at boot the highest good sequence wins. A magic per project, because a
  board flashed without erase still holds the last project's slots.
- Map tiles: a log ring of 4 KB sectors, header last, read straight from flash into the decoder.

Wear sums and code: [storage.md](references/storage.md).

## When it crashes

```text
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y
CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y      (or _TO_FLASH with a coredump partition, for boards nobody watches)
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
```

- `app_main` logs `esp_reset_reason()` and how long the run before lasted, kept in NVS once a minute; GET gives
  `uptime`, `ran`, `reset`. **RTC memory is not enough**: it was lost when the USB line's reset pulled EN [measured].
- Opening the port resets a CH340 board [measured]: keep one long logger, not short peeks.
- A board flashed from the site needs the ELF of that build: CI keeps `elf-<disp>` for 90 days. Check the
  `ELF file SHA256` line.
- A restart loop needs a guard: a word in `RTC_NOINIT_ATTR` memory says which risky thing was running, and after a
  watchdog restart that thing is left alone till the power goes off.

**The 10 s log** is the instrument: fps, ms a frame, view, planes, heap; ms a frame per layer plus `sending` and
`touch`; the second core's wait and stack; on PSRAM boards the internal RAM; and per fetch bytes, ms and `at least
<n> since power on`. `sending` close to `W*H*16 / SPI Hz` means the bus is the limit (61 ms at 480x320, 40 MHz);
a layer that grows after a minute is state filling up; the `at least` number is the budget. The log itself costs
frames at 115200 baud, and it names the network, the address and the town: mask before sharing. Detail:
[crashes.md](references/crashes.md).

## The USB console

The site and scripts talk to the board at 115200 baud, a line each way (`OWLOS HELLO`, `READY <version> <build>
<mac>`, `GET`, `CFG {...}`, `SET {...}`). The runtime side: UART0 through the driver and, on chips with USB Serial
JTAG, the chip's own USB read with the low-level FIFO calls (a driver would take the log's way out too); one mutex,
one line at a time; a command found from `OWLOS ` anywhere in the line, because a reset leaves junk; priority 3, so it
answers late during a handshake; its 4 KB stack overflowed once when the CFG line grew [measured]. Protocol:
esp32-flash-and-settings. Runtime detail: [tasks-and-watchdog.md](references/tasks-and-watchdog.md#the-console-over-usb).

## The e-paper loop

A picture takes a second or more and flashes the glass, so the e-paper boards draw only when something asks: a key
(a quarter of a second after it, so presses go in one), or every frame period. Keys are read on their own task every
10 ms. Range and look keys are **gathered**: each press shows its target at once in its third of the keys' strip,
and after 2 s of quiet one print applies them all, or none if they came back to where they started. A new screen
**holds** the old one on the glass while it waits for its map (8 s at most) and is then printed whole once
[measured on the 4.2"]. Code: [tasks-and-watchdog.md](references/tasks-and-watchdog.md#the-e-paper-loop).

## Testing without a person

- **The PC preview:** `core/` builds with MSVC; `tools/preview/BUILD.BAT <screen>` runs a script of touches and keys
  and writes PNGs with the panel's own dithering, e-paper in its 4 greys. Prove a speed-up changed nothing with a
  pixel diff of two shot folders.
- **The WebAssembly twin:** `web/twin.c` with `web/BUILD.BAT` and `web/emcc.args` builds the same `core/` C for the
  browser. A demo sky alone, or the board's planes over Web Serial as lines of the free networks' JSON, read by the
  board's own parser; settings, picks and pages kept in step both ways. It shows what the code thinks, not the glass.
- **The fake board:** `tools/fakeserial.js` puts pretend boards behind `navigator.serial`, with the failures real
  cables make (mute, stuck, framing error, unplug); `assemble.sh --test` builds a test page.
- **Data with a known truth:** `core/demo.c` writes what the server would send, in 37-byte pieces through the real
  parser; simulators with a known answer; recorded real answers replayed through the board's code.
- **The bench:** flash only after `Project build complete`, check chip and MAC, count 3 of 3 `Hash of data
  verified`, then GET and compare field names only. Rigs drive looks, ranges and keys over the console and print
  counts, never raw lines. A soak before every beta.

Detail: [testing.md](references/testing.md).

## What was run on what

- WiFi Radar, 2.8" CYD: 15-19 fps, 2 h log, no reboot, 0 lost strips, heap 49 KB, 42 flash saves [measured].
- WiFi Radar, 3.5" ST7796: 8 fps, 118 ms a frame, heap 24 KB [measured].
- WiFi Radar, round C3: 8 fps, heap 31 KB, hears fewer networks (10) than the ESP32 boards [measured].
- WiFi Radar, 5.79" e-paper S3: full refresh about 3 s, partial 1.86 s [measured].
- WiFi Radar, RPi LCD, 4.2" e-paper: [built] and preview only.
- Flight Radar, 2.8" CYD (two units over time): on real WiFi, two-hour logs, the 0.16.2 soak with 0 restarts
  [measured].
- Flight Radar, 3.5" ST7796 (two units over time): radar 98 ms and map 81 ms a frame (2026-11-01), the weather
  numbers above, 0.16.2 soak 0 restarts; 0.2 -> 0.3 without erase kept every setting [measured].
- Flight Radar, round C3: console, GET, fetch fine; two-hour logs; 0.16.2 soak 0 restarts, weak WiFi on that
  bench [measured].
- Flight Radar, 4.2" e-paper S3: the paper screens, keys gathered, the frame hold, no crash [measured].
- Flight Radar, NM-CYD-C5: run by a tester; the fixes after the second report [built].
- Flight Radar, the 7" Waveshare boards and the 10.1" P4: run by their contributor [docs, FORK-NOTES.md].
- Flight Radar, every other build (RPi LCD, ES3C28P, CrowPanel Advance 2.8" and 5", Guition 5", 4.3", CrowPanel
  5" and 7", Tab5, ZLX-ESP32-2, 5.79" e-paper): [built] here, some run by testers on their boards; the board files
  in esp32-boards say which.
- Air Quality UI, its own board, ILI9486: about 110 ms a frame, 8 fps, ILI9486 at 20 MHz [measured].
- Thermo Relay, 2.8" CYD: flashed; no frame or heap numbers on record.
- FusionEdge no-PSRAM port, 2.8" and 3.5" CYD: MP3 streams play, its web page serves [measured].

## Checklist before a firmware is called done

- [ ] Task watchdog 10 s with panic, idle checks on (or the busy task watched instead), core dump to UART or
  flash, ELF kept by CI.
- [ ] `esp_task_wdt_add` in every loop that must not stop, a yield in every loop that may run long.
- [ ] `app_main` starts the console and says READY last, when all it may touch exists.
- [ ] The log prints fps, ms a frame, layer times, heap and minimum heap (internal RAM on PSRAM boards), reset
  reason, last run.
- [ ] Lowest free heap seen with the worst load and the busiest look, TLS included. Largest block known.
- [ ] Every malloc that can fail has a path that does not crash, and says so in the log.
- [ ] No WiFi, wrong password, no server: each tested, each shown on the screen in words.
- [ ] Settings blob has a version, a version bump keeps the old fields; new features get their own keys.
- [ ] Anything saved often is in a ring, header last, with a magic of its own.
- [ ] Secrets checked out of the log and the GET answer; logs masked before they are shown.
- [ ] Update without erase tried on a board holding the previous version's settings, and back again.
- [ ] Flash budget of the smallest layout checked: a change in size is a question.
- [ ] A long log on the real board on real WiFi, and how long it ran written down.

## The traps that cost the most

- Board reboots every minute or two beside a busy router: maths on core 0 starves idle. A yield hook every round.
- C3 resets every ~29 s: fetch below the screen on one core. Equal priority.
- Screen stops for ever, no reboot at first sight: next strip sent while one was in flight. Wait for the done
  callback, 200 ms limit, count `lost`.
- Reboot loop with one I2C chip: `i2c_master` spins after a NACK on the classic ESP32. Bit-banged bus, RTC guard.
- `assert xTaskGenericNotify` or a lost SET after READY: READY said before the task existed. Console last.
- 76 KB or 32 KB malloc fails after WiFi: the heap has only small blocks. Smaller pieces.
- `esp-aes: Failed to allocate memory` on a PSRAM board: no internal DMA RAM left for the AES bounce buffer. Fewer
  strip rows, or software AES.
- The board crashes on the request after a broken tile: a kept TLS connection reused. Drop it.
- New line in sdkconfig.defaults does nothing: `sdkconfig` already exists. Delete `build*/sdkconfig`.
- Old firmware flashed after a failed build: the old `.bin` is still there. Gate on `Project build complete`.
- A 3.5" stood still after about a day: not known, a glitch is suspected. The LCD re-sends its mode every 5 s.
- "It hung", twice: the cable fell out. Look before you debug.

The rest: [traps.md](references/traps.md).

## Open

- Why 80 MHz flash hung WiFi Radar on the CYD, and whether it still would.
- What really stopped the 3.5" after a day. The re-send was added blind and proves nothing.
- A multi-day WiFi soak with join, DHCP and TLS, with the network dropped on purpose.
- A stuck fetch task is not caught by any watchdog; the screen would only show planes fading away.
- `ms` counters are 32 bits and wrap at 49.7 days. Comparisons by subtraction survive it and passed deadlines are
  cleared now, but the run record and `now < 8000` tests do not, and no board has been up that long.
- The C5's 12-row fix has not run on a C5 here; if the AES still fails, software AES.
- The round C3's weak WiFi on the bench: a lower TX power is untried.
- Why fewer strip rows cost more per SPI transfer on the 3.5" than the arithmetic says.
- The round C3's flash: about 56 KB left. The next feature for it must say what it costs.
