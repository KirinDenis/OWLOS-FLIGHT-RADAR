# ESP32-P4

Two RISC-V cores, 32 MB of PSRAM on both boards here, MIPI-DSI for big panels, a pixel engine (PPA), and no radio of
its own: the WiFi is an ESP32-C6 beside it, reached over SDIO. Two boards are built for it, both brought in by people
who run them: the M5Stack Tab5 and the Guition JC8012P4A1C. Nothing on this chip has run here; everything measured
below was measured by those contributors on their boards (FORK-NOTES.md and pull requests in OWLOS Flight Radar).
Sureness words as in SKILL.md.

Boards here on this chip: [tab5](../boards/tab5.md), [jc8012](../boards/jc8012.md).

## The numbers

- CPU: 2 RISC-V cores. The builds set 360 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360`), with
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` and `CONFIG_IDF_EXPERIMENTAL_FEATURES`: both boards' chips are v1.3, before
  production silicon (built). The lines do no harm on a newer chip, by their comment.
- PIE vector instructions (`esp.*`: `vld/vst.128`, `andq`, `vmul.u16`, `vadd.u16`, `vmin.u16`, `orq`); they take only
  registers x8 to x15 (FORK-NOTES.md, measured by chipguyhere).
- PSRAM: 32 MB on both boards, at 200 MHz (`CONFIG_SPIRAM_SPEED_200M`). Flash 16 MB on both.
- L2 cache shared by flash and PSRAM; the JC8012 build sets 128-byte lines (`CONFIG_CACHE_L2_CACHE_LINE_128B`).
- No WiFi or Bluetooth on the chip.
- Display: MIPI-DSI (DPI), the PPA (rotate, scale), an AXI GDMA.
- Bootloader at 0x2000 (docs: the bootloader Kconfig lists the C5, P4 and H4 as reserving two sectors). No P4 build here
  to read it from: the P4 builds come from CI.
- ESP Web Tools chipFamily: `ESP32-P4`.

## The WiFi is a C6

- `espressif/esp_wifi_remote` puts the `esp_wifi` calls through to the C6 over ESP-Hosted (`espressif/esp_hosted`).
  Versions pinned in main/idf_component.yml: esp_wifi_remote ^1.6.5 (1.6.3 and older refuse an esp_hosted 3.x),
  esp_hosted ^3.1.0, both only `if: "target == esp32p4"`, so nothing is fetched for the other chips.
- The SDIO wires differ by board and come from a preset: the Tab5 `CONFIG_ESP32P4_TAB5_C6_BOARD` (bus on GPIO 8 to 13,
  the C6's reset on 15); the JC8012 `CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD` (CLK 18, CMD 19, D0 to D3 14 to 17, the
  C6's reset on 54, as on Espressif's ESP32-P4-Function-EV-Board).
- The C6 keeps its shipped co-processor firmware. The Tab5's reports itself as 1.4.1 and answers a 3.x host; the
  JC8012's 2.1.10 logs "major version mismatch" against Hosted 3.1.0 and nothing failed so far.
- TLS handshakes on the JC8012 still fail sometimes (`-0x7280` after 20 to 30 s) with 110 KB free: not the RAM; the
  link (SDIO at 40 MHz) or the servers are what is left to look at.

## Memory

- Internal RAM on the JC8012 (measured by chipguyhere): static 105 KB of IRAM code, 46 KB `.data`, 89 KB `.bss`. The
  four bounce buffers 102 KB, the moving layer's band 80 KB. Before moving things out: about 75 KB free, a largest
  block of about 31 KB, and TLS handshakes failing. After moving mbedTLS, lwIP's buffers, ESP-Hosted's task stacks and
  the fetch and console stacks to PSRAM: about 133 KB free, 112 KB at the least.
- Hardware AES works with its buffers in PSRAM here, unlike the S3 and the C5 (measured by chipguyhere).
- The SDIO buffers stay internal (DMA).
- On the Tab5 the strips, their turned copy and the two cores' canvases share about 346 KB of RAM at 1280 wide
  (config.h comment): strips of 12 rows.
- XIP from PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM`) made the biggest single difference on the JC8012: a filled chunk
  ~150 -> ~103 us, the background frame 0.8 to 5 s -> 0.41 s. Flash runs at 40 MHz there and shares the L2 cache with
  the PSRAM. It also keeps flash writes from stopping the panel's feed, and makes PSRAM stacks safe. It costs about
  11 KB of internal RAM.
- Flash runs at 40 MHz on the JC8012 build although `sdkconfig.defaults.esp32p4` asks for 80: not looked into.

## The PPA and the cache

- The PPA turns each background strip into the standing frame (the panels stand, the picture lies). Its driver drops
  the cache over the whole 2 MB frame each time, with the core's interrupts held off: on the core that takes the
  panel's interrupts two chunks' interrupts then came as one (~40 times per 10 s) and later chunks were drawn into the
  wrong buffer. Fixed in the driver by reading the DMA's next list item instead of counting interrupts.
- Strips of 80 rows, drawn in PSRAM: an 8-row strip writes 16 bytes into each of 1280 frame rows, 100 turns a frame,
  and made the fill's fetches slow. A PPA turn measured at boot: ~12 ms at 16 rows, ~27 at 80, ~42 at 128, ~190 at
  160, ~925 for the whole picture.
- Frame buffers must sit on the 128-byte cache line (`heap_caps_aligned_calloc`): the PPA's output and any range
  `esp_cache_msync` takes need it.
- Driving the cache registers directly past `esp_cache_msync`'s lock gave stale lines on the glass, and once the SDIO
  link to the C6 timed out and restarted the board.

## PIE and FreeRTOS on ESP-IDF 5.5

ESP-IDF 5.5's FreeRTOS port (portasm.S `pie_save_regs` / `pie_restore_regs`) never saved q3 and swapped SAR and
FFT_BIT_WIDTH on restore, so a task using PIE that is preempted mid-kernel gets its vectors broken: thin 8-pixel slivers
on the glass. Fixed upstream in espressif/esp-idf `b25cb2906c7d` (v6.0 on, master) and `51d5738ad769` (release/v5.5
head, 2026-10-28; no 5.5.x tag has it yet). On a stock 5.5.x set `OVL_PLANNER_PIE` to 0 in ovl.c.

## Console and USB

- Tab5: the console is the chip's own USB-Serial-JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`): the board has no
  UART0 to speak of, and the UART driver waiting for it held core 0 till the watchdog bit (built, by its comment).
- JC8012: UART0 through the board's CH340 (`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`), its USB_UART port.

## Building

- The board package and the WiFi come from the component registry: `main/idf_component.yml`, each entry with
  `rules: - if: "target == esp32p4"`. The P4 build also requires `esp_driver_ppa` (main/CMakeLists.txt adds it only for
  that target).
- `espressif/esp_lvgl_port` is pinned to ~2.8.0: the Tab5's package pulls it into every P4 build, and 2.9.0 uses a DPI
  callback ESP-IDF 5.5.3 does not have.
- The Tab5 does not build on the local v5.5 tag (component manager, MissingKconfigError before compiling); CI's
  release-v5.5 image builds it.
- Copies of `sdkconfig.defaults.esp32p4` and `.jc8012`: ../board-files.md.
