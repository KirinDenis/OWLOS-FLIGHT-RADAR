# Traps

Symptom, cause, fix. How sure each is: the tags of SKILL.md. Source in brackets: FR Flight Radar, WR WiFi Radar, TR
Thermo Relay, AQ Air Quality UI, FL Web Flasher, FE FusionEdge, FN a contributor's notes on the 7" and 10.1" boards
(FORK-NOTES.md in Flight Radar).

## Build and flash layout

- **The CYD hangs at random, silently** [measured, WR]. Cause: 80 MHz flash, never found; may be the same days'
  strip hang, not recorded. Fix: 40 MHz DIO is the safe start. See project-skeleton.md, Flash speed.
- **A comment says 40 MHz, the board runs 80** [computed, FR]. Cause: `sdkconfig.defaults` says 40M,
  `sdkconfig.defaults.esp32` comes later and wins; the CMake NOTE was copied from WR and is still in the file. Fix:
  trust `flash_args` and the build's sdkconfig, not the comment. Delete the note in a new repo.
- **`flash_args` says `dio` on a QIO build** [computed]. Normal: the bootloader switches to QIO itself [docs].
  Nothing to fix.
- **A new line in `sdkconfig.defaults` does nothing** [measured, FR]. Defaults are read only when `sdkconfig` is
  made. Delete `build*/sdkconfig`, build again. Same for a new board file in the `SDKCONFIG_DEFAULTS` chain.
- **Works here, differs on CI** [computed]. The local `sdkconfig` is older than the defaults, CI starts clean. When
  in doubt delete and rebuild, or rehearse with CI's line.
- **Yesterday's firmware on the board after a "successful" flash** [measured, FR]. A failed IDF build leaves the
  old `.bin`, esptool flashes it. Gate on `Project build complete`.
- **CMake refuses a build folder after a move** [measured, WR]. The cache keeps the old path. BUILD.BAT compares
  `CMAKE_HOME_DIRECTORY` and starts fresh.
- **`idf.py` fails on Python** [measured]. IDF's venv needs IDF's own Python first on PATH:
  `for /d %%P in ("%IDF_TOOLS_PATH%\tools\idf-python\*")` in BUILD.BAT.
- **`idf.py` not found from Git Bash `cmd //c`** [measured, TR]. Shell differences. Use PowerShell `cmd /c`.
- **PlatformIO stops mid-build with "MSys/Mingw is not supported"** [measured, FE]. Started from Git Bash. Run
  `cmd /c "pio run ... > log 2>&1"` from PowerShell.
- **Link clash on `touch_init`, `touch_read`** [measured, WR]. Same names inside `librtc.a`. Call them `tp_*`.
- **`esp32c5` refused on IDF 5.5.0** [measured, FR]. A preview target on the tag. `idf.py --preview`; CI's
  release-v5.5 needs no flag.
- **A P4 build stops at configure with `MissingKconfigError`** [measured, FR]. The component manager on the build
  PC; not the change at hand. CI builds it; do not chase it in a feature's review.
- **An ESP-IDF 6 line in shared defaults** [built, FR]. 5.5 warns that it does not know
  `CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` and skips it. Harmless; keep such lines commented as IDF 6.
- **The round C3 close to not fitting** [measured from the build, FR]. 96% of 1.31 MB, 56 KB left on 2026-11-23.
  Watch the size per build; take room from `tiles` when needed.
- **A ring reads another project's data** [measured, TR]. Same partition address, flashed with no erase. A magic
  per project: `SLOT`, `THRM`, `AQH1`, `OSMT`.
- **A new core file missing from one build** [reasoned, FR]. `core/` is listed three times: `core/CMakeLists.txt`,
  `tools/preview/BUILD.BAT`, `web/emcc.args`. Add it to all three.
- **The preview overwrote the README's shots** [measured, FR]. `tools/preview/BUILD.BAT` writes into tracked
  `shots/`. Copy what you need, restore the folder.

## Tasks and the watchdog

- **Reboot every minute or two, beside a busy router** [measured, WR]. 1.2 s of maths grew to 10 s under WiFi,
  IDLE0 starved. `map_yield` after every round.
- **C3 resets every ~29 s** [measured, FR]. One core, fetch at 4 below ui at 5: under 1 ms a frame, a map over
  10 s, idle starved. Same priority on one core (the C5 too).
- **C3 picture stands still 6-10 s** [measured, WR]. Solver above the screen, no FPU. Equal priority, 3 restarts a
  minute apart.
- **The 7" restarts with many planes on the radar** [docs, FN]. The moving layer built back to back and a page
  drawn back to back beside TLS handshakes: core 0's idle starved. Pacing, and the watchdog on the ui task instead
  of core 0's idle on those builds.
- **`assert xTaskGenericNotify (xTaskToNotify)`** [measured, FR]. A SET arrived before the fetch task existed.
  Console and READY last in `app_main`.
- **A SET right after READY is lost, or `SW_CPU_RESET` on 0.2** [measured, FR]. Same cause, same fix.
- **Console answers about 2.7 s late** [measured, FR]. Console priority 3 below fetch 4 during a handshake.
  Harmless. Do not read it as a hang.
- **GET restarts the board, sometimes** [measured, FR]. The console task's 4 KB stack overflowed when the CFG
  line grew by one local: 2 of 5 GETs. Static buffers under the lock; watch it when adding to CFG.
- **The UART driver holds core 0 till the watchdog** [docs, FR]. A board with no usable UART0 (the Tab5). Console
  on the chip's USB alone, the UART driver not started.
- **`esp_task_wdt_reset` logs an error** [docs]. The task was never added. Ask first:
  `esp_task_wdt_status(NULL) == ESP_OK`.
- **Watchdog during an e-paper refresh** [computed, WR]. A refresh takes seconds. Reset it in the BUSY wait.
- **A long `core/` loop trips the watchdog** [measured, WR]. `core/` may not call FreeRTOS. A function-pointer hook
  the firmware sets.
- **Two cores gave only 1.25x** [measured, FR]. Each core's strip gets slower when both run, and core 0 carries the
  WiFi. Expect that. It helps only when the CPU is the limit.
- **The second core races the first** [computed, FR]. A strip pass that writes shared state (the weather's table
  work once raced on two cores at closing). `ui_strip` only reads, all writes in `ui_frame`; prove it with the
  halves rig.
- **A stuck fetch task is not caught** [computed, FR]. Not added to the watchdog, a request may last 9 s. Open.
  Watch the age of the last answer.
- **`vTaskDelay(1)` costs 10 ms** [docs]. Default 100 Hz tick. `CONFIG_FREERTOS_HZ=1000`.
- **A swipe comes out as a tap** [measured, WR]. Touch read once a frame at 8 fps gives 1 or 2 points. Sample
  between strips, every 15 ms, queue the samples.
- **The 3.5" comes up one look on after a restart** [measured, FR]. One noisy XPT2046 read at power-up became a
  press, and a press with a release is a tap. Two reads in a row for a press, nothing read for 1.5 s after start.
- **Keys sent on the console during an e-paper refresh are lost** [measured, FR]. One key slot, overwritten during
  a 6 s refresh. Send one, wait for its log line.
- **A signed deadline fires again 24.8 days later** [built, FR]. `(int32_t)(until - now)` reads a passed deadline as
  ahead after the wrap. Clear deadlines when they pass; a compare-and-swap where another task renews them.

## Screen path, as far as it is a runtime matter

- **Screen stops for ever; core dump in `spi_device_acquire_bus`** [measured, WR]. A strip sent while the one before
  was in flight. Wait for the done callback, 200 ms limit, count `lost`.
- **Touch costs a third of the frame on the 3.5"** [measured, FR]. XPT2046 on the LCD's bus: each read waits for a
  strip and holds the next one back. `disp_wait_idle()` first, and the pen interrupt keeps it off the bus when
  nobody touches.
- **Sender waits though DMA is double-buffered** [measured, FR]. esp_lcd's `tx_color` takes the bus and waits for
  the transfer before. Two strips in one transfer on the helper path.
- **A 3.5" stands still after about a day** [measured, cause not known]. Maybe a glitch on the line. Re-send the
  mode every 5 s. Unproven.
- **A job takes 62 ms a frame, only after 80 s** [measured, FR]. Trails walked in each of 40 strips, full only then.
  Move it to once a frame.
- **Grid costs 38 of 110 ms** [measured, FR]. Same. Once a frame.
- **Light views hang more than heavy ones** [measured, WR]. Drawing outruns the bus. Same as the deadlock.
- **Fewer strip rows, fewer fps on SPI-bound looks** [measured, FR]. 8 -> 6 rows: 3.5" MAP 12 -> 10 fps. Pay it only
  for memory, and say so.
- **The log makes the picture stutter** [docs, FN]. The 10 s statistics, about 3 KB at 115200 baud, held the ui
  0.3-0.5 s. A log ring in PSRAM written by a low-priority task, or a shorter 10 s block.
- **An e-paper drew a white sheet before the new screen** [measured, FR]. Drawn on top while the map was still
  coming. Hold the old screen (`paper_hold`) till the new one can be printed whole, 8 s at most.

## Memory

- **76 KB malloc fails after WiFi** [measured, WR]. 792 x 24 x 4 in one piece. Strips of 8 rows on the wide e-paper.
- **Inflater can't start** [measured, FR]. ROM tinfl wants 32 KB in one piece, largest block ~24 KB. Own inflater,
  window in small pieces.
- **The map stays empty, `no memory to read a tile in`, heap 56 KB, largest 19 KB** [measured, FR]. A fragmented
  heap and 4 KB window pieces. 1 KB pieces, later 512 bytes.
- **Map tiles kept for ever but never drawn** [measured, FR]. 1, 2 and 4-bit PNGs refused by the decoder. Unpack
  small depths; drop a kept tile the decoder refuses.
- **75 KB map doesn't fit** [measured, FR]. One block, or room TLS needs. 8-row bands, IRAM or PSRAM first, word
  access only.
- **The map's last texels write past a band** [measured, FR]. A width that is not a multiple of 8 (236 on the 4.2").
  Round up to 8; `map_alloc` refuses anything else.
- **An 8-bit write to IRAM crashes** [docs]. IRAM takes aligned 32-bit access only. Read and write words.
- **Minimum heap 10.5 -> 6.4 KB from 3.8 KB of code** [measured, FR]. New static buffers. Share them between jobs
  that never overlap.
- **A third of the 2.8"'s maps empty after a 3.6 KB malloc per tile** [measured, FR]. The window could not find its
  pieces beside it. Lend a static buffer of a job that never overlaps (a union).
- **A new feature's statics break an old feature** [measured, FR]. The weather's 1.5 KB left the 3.5"'s tile reader
  short: 79 of 162 maps. Re-run the memory-hungry cycle after every feature; on the 3.5" the strip went 8 -> 6.
- **Stack overflow from a 10 KB struct** [computed, FR]. A big local. `static` in the one task that uses it.
- **Row run-length coding saves nothing** [measured, FR]. Map detail too fine. Do not try it again.
- **29 KB table for 2 ms** [measured, AQ]. A table that pays little. Leave it off.
- **"Heap is fine" while internal RAM is short** [measured from a tester's report, FR]. `MALLOC_CAP_8BIT` counts
  PSRAM. Log `MALLOC_CAP_INTERNAL` on PSRAM boards.
- **8 MB PSRAM and still out of memory** [docs, FR]. `USE_CAPS_ALLOC`: TLS, WiFi, malloc stay internal. Ask for PSRAM
  by name, move TLS and the WiFi's buffers out where the chip allows.
- **`esp-aes: Failed to allocate memory`, no planes** [docs, a tester's report and FN]. TLS in PSRAM; the hardware
  AES needs a DMA bounce buffer of up to 1600 bytes in internal RAM. Fewer strip rows, or
  `CONFIG_MBEDTLS_HARDWARE_AES=n`.
- **The screen stops, "not enough memory for the screen", PSRAM empty** [docs, a tester's report, FR]. A C5 with
  24-row strips and TLS internal. Map, TLS and WiFi buffers to PSRAM, 12-row strips.
- **Helper starves TLS** [computed, FR]. The second core's buffers took the heap. It starts only with 48 KB to spare.
- **Boot loop on one radio station** [measured, FE]. A decoder made with `make_unique` threw for want of memory
  after TLS, and the station replays at boot. `new (std::nothrow)`, no decoder that cannot fit.
- **A web page hangs while streaming** [measured, FE]. AsyncWebServer ignored `write()` returning 0 (lwIP ERR_MEM).
  Keep the refused piece and send it first next time, no new allocation, chunks of 2 MSS, files one at a time.

## WiFi and HTTP

- **Nodes fail after a day and a half** [measured in old OWLOS]. DHCP dropped, IPs lost, chaos in the network code.
  Plain single-owner network code. Not proven cured, soak it.
- **Scan never starts** [measured, FR]. The driver won't scan while joining, and a missing network joins every 2 s.
  Disconnect first if not up, retry, resume after.
- **Password page says nothing useful** [measured, FR]. The disconnect reason was not read. Map `AUTH_FAIL`, 4-way
  and handshake timeouts to PASSWORD, the rest to NOT FOUND.
- **32-letter SSID loses a letter** [computed, FR]. `snprintf` wants room for the zero. `memcpy` with `strnlen`: the
  field needs no zero when full.
- **The screen says DEMO while the router boots** [measured, FR]. No grace time. 20 s of CONNECTING first.
- **The board joins the far access point of a network** [docs, FN]. ESP-IDF's default takes the first it hears.
  `WIFI_ALL_CHANNEL_SCAN` and `WIFI_CONNECT_AP_BY_SIGNAL`.
- **A board behind a big panel loses the network** [docs, FN]. Modem sleep missed DNS answers at -77 to -85 dBm.
  `WIFI_PS_NONE` on boards that run from a wall supply.
- **The round C3 has planes 40% of the time** [measured, FR]. Weak radio on that bench: 9 joins in 10 end in reason
  201. Not the firmware (an A/B changed nothing). Say so; a lower TX power is the untried next step.
- **HTTP 429 from adsb.lol** [measured, FR]. Planes and watch lookups on one server. Lookups go to the other server;
  429 waits 3x.
- **Two servers answer 403 to anyone not let in** [measured, FR]. airplanes.live, adsb.one closed. Keep a list of
  two, move on after a failure.
- **Answer too big for RAM** [computed, FR]. 100 planes at about 1 KB. Stream to a parser, keep one object.
- **A tile is cut short** [computed, FR]. Connection dropped. `esp_http_client_is_complete_data_received` before the
  header goes on.
- **The board crashes on the request after a broken tile** [measured, FR]. A kept TLS connection reused after a
  short read, `LoadProhibited` in mbedTLS. Drop the connection after any tile that is not whole.
- **Next request reads the old body** [docs]. A non-200 on keep-alive not drained. `esp_http_client_flush_response`.
- **Only `perform` reuses a connection** [docs, FN]. The open and read calls never send a second request line. Use
  `perform` with `ON_DATA` for a kept connection.
- **HTTPS "works" with an expired certificate** [docs]. `MBEDTLS_HAVE_TIME_DATE` off by default. Know it. If dates
  must count, the time must come first, and NTP may be blocked.
- **No requests at all on some networks** [reasoned, a reviewed pull request]. HTTPS gated on NTP; a network that
  blocks NTP never fetches. No gate: start SNTP when WiFi is up and let each user of the time wait for it.
- **Planes surge and crawl backwards** [measured, FR]. Positions timed from the start of a 2.5-4 s request. Use the
  server's own time in the answer (esp32-data-sources).
- **The home's country stays empty for minutes after flashing** [measured, FR]. Nominatim refuses after several
  quick restarts; it is asked again every 5 minutes. Not lost settings.
- **Round C3 hears 10 networks where the ESP32s hear more** [measured, WR]. Not known. Say so, do not tune around it.
- **Promiscuous frames lost** [computed, WR]. Callback too slow, queue full. Parse little, queue with 0 wait, count
  `dropped`.

## Storage

- **Holding + wears the flash** [computed]. A write a step. Save 3 s after the last change.
- **Power cut in mid-save** [computed, WR]. A half-written record. Data first, header last, CRC, fall back to the one
  before.
- **History from an older build misread** [computed, WR]. Struct changed. Header with version and sizes, refused not
  misread.
- **A version bump loses a person's settings** [measured, FR]. Blob size check fails. Prefix read over defaults,
  `_Static_assert` on `offsetof`.
- **A flash back to the stable loses new settings** [measured, FR]. The old firmware does not know them. A key of its
  own per new feature; the old one reads defaults and the new one finds them again.
- **Run record gone after plugging in** [measured, FR]. RTC memory lost on EN from the USB line. NVS, once a minute.
- **Demo numbers in the real history** [computed, AQ]. Demo on the same path. Save real first, demo never saves,
  `s_cfg.sim = 0` at boot.
- **Update wipes all settings** [measured, FL]. A merged bin is the whole flash. Four parts, answer no to erase.
- **`uploadfs` wiped settings** [old OWLOS]. One image for everything. Keep settings out of any image (FusionEdge's
  LittleFS image does the same today).

## Crashes and diagnosis

- **"It hung"** [measured, twice]. The cable fell out. Look first.
- **Each log peek restarts the board** [measured, WR]. CH340 auto-reset on open, even with DTR and RTS off. One long
  logger, read `ran` and `reset`.
- **addr2line gives nonsense** [measured, WR]. Local ELF, board flashed from the site. CI's ELF artifact, compare
  `ELF file SHA256`.
- **Old report, ELF gone** [computed, FR]. Artifacts last 90 days. Keep release ELFs if it matters.
- **A crash nobody saw** [docs, FN]. The UART dump exists only if something listened. A `coredump` partition where
  the flash allows.
- **I2C hang after a NACK, restart loop** [measured, AQ]. `i2c_master` waits for ever on the classic ESP32 in 5.5.
  Bit-banged bus, RTC guard.
- **Restart loop on any bad peripheral** [measured, AQ]. Same fault at each boot. Guard word in RTC, skip it till
  power off, say so on the screen.
- **Brown-out mistaken for a software bug** [measured, AQ]. Weak supply, MQ heaters. Count `ESP_RST_BROWNOUT` in
  RTC, show it.
- **Bytes before the first command eat it** [measured, FR]. Junk from the reset glues to `OWLOS`. Send `\n` first:
  2 of 6 became 6 of 6. And find `OWLOS ` anywhere in the line.
- **S3 e-paper silent after flashing** [measured, WR]. It prints nothing until reset. Toggle RTS on an open port.
- **Port busy** [measured]. The person's `idf.py monitor` is on it, or a browser tab holds it over Web Serial
  ("Access is denied"). Read the terminal, ask for the tab to be closed; never kill what you did not start.
- **The wrong board flashed** [measured, FR]. Port numbers moved after a replug. Check chip and MAC every time.
- **Backup read fails** [measured, FL]. CH340 at 921600. 460800.
- **PC profile says all is well** [measured, FR]. It never reached the device's state. Run it long enough for trails
  and history to fill.
- **A speed compared across two looks** [measured, FR]. The 10 s line names the view, not the look. GET `look`
  first.
- **JSON loses its quotes in the rig** [measured, FR]. `powershell -File`. Call the script in the same process.
- **A speed-up breaks a pixel unnoticed**. No reference. Pixel diff of the shot folders.
- **A smoothing test passes and the eye still sees a wobble** [measured, FR]. Made-up paths, and the wrong measure.
  Replay recorded real answers; measure what the eye sees.
- **Number springs explode at 8 fps** [measured, AQ]. A big frame dt in the integration. Integrate in 15 ms
  substeps.
- **A log shown to someone holds the network and the town** [measured, FR]. "WiFi up" names the network and the
  address; map and geocode lines can name the town. Rigs print booleans and counts; mask before sharing.
