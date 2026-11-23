# ESP32-S3

Two cores, PSRAM on nearly every screen board, its own USB, and an RGB parallel LCD peripheral: the chip under the
2.8" S3 boards, the e-papers and every 4.3" to 7" RGB panel here. Sureness words as in SKILL.md.

Raw numbers from ESP-IDF 5.5's own files (soc_caps.h, spi_pins.h, uart_pins.h, the GPIO docs page for the chip) and
Espressif's esptool docs: docs read from the source.

Boards here on this chip: [es3c28p](../boards/es3c28p.md), [crowadv28](../boards/crowadv28.md),
[epd400](../boards/epd400.md), [epd792](../boards/epd792.md), [crowadv50](../boards/crowadv50.md),
[crow50](../boards/crow50.md), [jc8048w550](../boards/jc8048w550.md), [esp4827s043](../boards/esp4827s043.md),
[crow70](../boards/crow70.md), [wave7](../boards/wave7.md), [wave7b](../boards/wave7b.md),
[ili9488-diy](../boards/ili9488-diy.md), FusionEdge's [s3-ili9488](../boards/s3-ili9488.md),
[jc3248w535](../boards/jc3248w535.md), [gen4-35ct](../boards/gen4-35ct.md).

## The numbers

- CPU: 2 Xtensa cores at 240 MHz, FPU flag set. 128-bit PIE vector instructions (used by the Waveshare 7/7B builds).
- WiFi: 2.4 GHz only.
- Internal RAM: 480 KB in one region by the IDF docs. A note in the radar's work says 512 KB of SRAM for the RGB
  boards: the chip has 512 KB, part of it can serve as cache (reasoned, not settled).
- GPIO: 0 to 48, holes at 22 to 25. No input-only pins.
- Flash and PSRAM pins: 26 to 32; with octal PSRAM also 33 to 37.
- PSRAM: quad or octal. `N16R8` modules here have octal.
- UART0: TX 43, RX 44.
- Own USB: D- 19, D+ 20. USB-Serial-JTAG and USB OTG.
- BOOT pin: 0. Must also hold for download: 46 low or floating.
- Strapping pins: 0, 3, 45, 46.
- Bootloader at 0x0.
- SPI hosts: SPI2 and SPI3. SPI2 IOMUX SCLK 12, MOSI 11, MISO 13, CS 10. Octal IOMUX 36 35 37 34.
- At 80 MHz or lower the GPIO matrix behaves the same as IOMUX (IDF docs).
- LCD peripherals: I80 and RGB.
- I2C controllers: 2. LEDC channels: 8. RMT words a channel: 48.
- ESP Web Tools chipFamily: `ESP32-S3`. ESP-IDF 5.5: supported.

## PSRAM

- `N16R8` means 16 MB flash, 8 MB PSRAM; on the S3 modules here the PSRAM is octal. `N8R8` (the e-papers, the
  Waveshare 7) and `N4R8` (the plain CrowPanel 7.0) the same width.
- With octal PSRAM GPIO 33 to 37 are taken, whatever a pin map says.
- Octal PSRAM and flash share the S3's memory bus and cache (FORK-NOTES.md): moving code back to flash does not
  relieve the PSRAM.
- How the builds ask for it, three ways, one a kind of board (copies in ../board-files.md):
  - SPI LCD boards and e-papers (`sdkconfig.defaults.esp32s3`): `SPIRAM_MODE_OCT`, `IGNORE_NOTFOUND`, `USE_CAPS_ALLOC`:
    used only when asked by name, so a board without it behaves like the ones without (built; the 5.79" e-paper runs
    it, measured).
  - RGB boards (`.s3rgb`): PSRAM at 80 MHz, code and read-only data copied into PSRAM and run from there
    (`SPIRAM_XIP_FROM_PSRAM`), TLS and the WiFi's buffers in PSRAM, `IGNORE_NOTFOUND` off: the frame lives there.
  - Waveshare 7/7B (`.wave7` on top): plain malloc of 1 KB or more may go to PSRAM, big static state in PSRAM
    (`SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, about 65 KB of internal RAM back), software AES.
- With XIP from PSRAM a flash write leaves the cache on, so task stacks in PSRAM are safe and a flash write (a map tile,
  the settings) no longer stops the panel's feed (FORK-NOTES.md, measured on the 7B).

## The RGB bus

- A whole frame lives in PSRAM and the chip reads it out all the time: 800x480x2 = 768,000 bytes. It goes out through
  two small bounce buffers in internal RAM (esp_lcd's bounce buffers), which ride out what else the PSRAM is busy with
  (lcd_rgb.c, after Elecrow's IDF example).
- esp_lcd refuses a panel whose rows do not divide by the bounce buffer's rows: 272 % 10, hence 8 on the 480x272 board.
- `LCD_RGB_ISR_IRAM_SAFE`, `LCD_RGB_RESTART_IN_VSYNC` (the picture finds its place again at the next VSYNC when a frame
  cannot keep up), data cache line 64 B: all in `.s3rgb`.
- ESP-IDF 6.0 names the pixel format (`in_color_format = LCD_COLOR_FMT_RGB565`), 5.5 still has `bits_per_pixel`.
- Bus details and timing: ../../../esp32-lcd-touch/references/bus-rgb.md.

## Internal RAM is the limit

- An e-paper S3 with WiFi up had no 76 KB block (792 x 24 rows x 4 bytes): its strips went to 8 rows (measured).
- CrowPanel Advance 5.0 on a tester's V1.3: the second core never started, no room in internal RAM for its strip.
  Strips went 16 -> 12 rows and TLS and the WiFi's buffers moved to PSRAM.
- Waveshare 7B (FORK-NOTES.md): with 1024-wide bounce buffers about 8 KB free, 3 KB at the least, once 183 bytes while
  tiles came in. Task stacks (fetch 12 KB, console 4 KB, log 3 KB) and big state moved to PSRAM: 45 KB free, 31 KB in
  one piece, 10 KB at the least.
- Hardware AES with TLS buffers in PSRAM copies through a DMA buffer it takes from internal RAM: "esp-aes: Failed to
  allocate memory" and the request failed on the 7B. `CONFIG_MBEDTLS_HARDWARE_AES=n` there (measured by chipguyhere).
  The C5 hit the same message, see esp32-c5.md; the P4 does not.

## USB

- Own USB on 19 and 20. Several boards have a CH340 instead and use 19 and 20 as I2C for the touch (JC8048W550,
  ESP32-4827S043, CrowPanel 5.0 plain, CrowPanel 7.0): the console must not poll the chip's own USB there
  (`NO_CHIP_USB` in board.h, built).
- Some boards have both (CrowPanel Advance 2.8: two USB-C), some only the chip's own (ES3C28P).
- The log goes to UART0 and to USB-Serial-JTAG as a secondary output by default
  (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`, built). Logging to two ports is deeper on the event task's stack:
  it ran past the end on an S3, so the S3 LCD boards set `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096`.
- Espressif's own USB id: `VID_303A` (docs). No driver to install on any OS, but the port disappears when the chip
  resets, after flashing too, and a board that started before the port opened said its hello to nobody.
- The notes record no S3 board talking to Flight Radar over its own USB here: that path (console.c polling
  USB-Serial-JTAG) is built, and reports from owners of such boards decide it.

## Strapping and BOOT

- GPIO 0 low = download, GPIO 46 low or floating then; 46 is ignored in a normal boot.
- GPIO 45 sets the flash voltage (high = 1.8 V; a third-party source, read the datasheet before relying on it).
- GPIO 3 picks the JTAG source only when an eFuse says so.
- Makers put these to use: ES3C28P DC 46, backlight 45; the e-papers CS 45, DC 46; RGB boards use 3, 45, 46 as data
  lines. A pull-up on one of them, on a home-made board, is how an S3 stops booting.
- GPIO 0 as a panel line: the plain CrowPanel 5.0 and 7.0 have the panel's pixel clock on GPIO 0, the Waveshare 7/7B a
  panel data line. Their BOOT button cannot be read while the screen runs, and pressing it on the CrowPanels shorts the
  clock (board.h, `RADAR_HAS_BUTTON`).

## Pins you cannot have

19 and 20 if you want the chip's USB, 26 to 32, and 33 to 37 with octal PSRAM. A board that puts I2C on 19 and 20 has
a bridge chip and no native USB.

## SPI

The S3 SPI LCD builds ask 40 MHz: LCDWIKI's setup runs 40, Elecrow's runs 80, 80 not tried here. The SPI LCD and
e-paper builds leave PSRAM at the default 40 MHz; the RGB builds run it at 80. The bus takes IOMUX
only when all its pins are IOMUX pins; CS is not counted in that check (`bus_uses_iomux_pins`, esp_driver_spi).

## The WiFi scan trap

A CrowPanel Advance 2.8 on 0.4 reset right after a WiFi scan on its screen ("Interrupt wdt timeout on CPU0"), a
tester's report. The CrowPanel Advance 5.0 restarted on opening the WiFi page: the event task's stack ran into the
event queue's lock (firmware/main/net.c). 0.6 says looking for WiFi networks on the screen no longer restarts the S3
boards; the event stack of 4096 is part of that fix.

## sdkconfig

`sdkconfig.defaults.esp32s3` (the e-papers): 4 MB layout since 2026-11-22 (the app passed 1.2 MB, ViewOwl's 2 MB left
it 1.3), CPU 240, octal PSRAM by name. `.s3lcd` on top for the SPI LCD boards, `.s3rgb` on top of that for the RGB
boards, `.wave7` on top of that for the Waveshare 7/7B. Copies in ../board-files.md. A 16 MB board runs the 4 MB layout
as it is, on purpose: one layout, the map's tiles get the same 2 MB.

App sizes (built, local builds): SPI LCD boards 1.23 to 1.25 MB, RGB boards about 1.23 to 1.26 MB, Waveshare 7/7B about
1.34 MB, the 4.2" e-paper with its paper screens 1.44 MB, all in a 2 MB slot.
