# ESP32-C5

One RISC-V core at 240 MHz, WiFi on 2.4 and 5 GHz, quad PSRAM on the boards here, a preview target on the ESP-IDF 5.5
tag. Two boards are built for it, both 2.8" yellow-board look-alikes; none has run here. Sureness words as in SKILL.md.

Raw numbers from ESP-IDF 5.5's own files (soc_caps.h, `io_mux_reg.h`, the GPIO docs page, the bootloader's Kconfig)
and Espressif's esptool docs.

Boards here on this chip: [nmcydc5](../boards/nmcydc5.md), [zlx2](../boards/zlx2.md). Found, not built: Waveshare
ESP32-C5-Touch-LCD-2.8 (../boards-others.md).

## The numbers

- CPU: 1 RISC-V core, 240 MHz. No FPU flag in 5.5's soc_caps.h.
- WiFi: 2.4 and 5 GHz. Nobody has joined a 5 GHz network under these firmwares yet; the README's "the ESP32 hears
  2.4 GHz networks only" was written for the other chips.
- Internal RAM: 384 KB, one region. Plus a 16 KB LP RAM that plain malloc can land in, and that is not DMA capable.
- GPIO: 0 to 28. No input-only pins.
- Flash and PSRAM pins: 16 to 22.
- PSRAM: quad. `N16R8` and `N8R8` read like the S3's names, but the PSRAM is quad here.
- UART0: TX 11, RX 12.
- Own USB: D- 13, D+ 14. USB-Serial-JTAG, no OTG. A board can use 13 and 14 as plain GPIO and give up the chip's USB:
  the ZLX-ESP32-2 has its backlight and its LCD's DC there.
- BOOT pin: 28. Must also hold for download: 27 high; 27 low with 28 low is invalid.
- Strapping pins: the IDF docs list 2, 7, 25, 27, 28. `io_mux_reg.h` also marks GPIO 26 "Boot Mode select (analog
  mode)" and its comment for GPIO 3 says boot mode too: the datasheet decides, it was not read here.
- Bootloader at 0x2000: the first two sectors are reserved for the key manager (bootloader Kconfig). Read from real
  builds (built).
- SPI: SPI2 only. IOMUX SCLK 6, MOSI 7, MISO 2, CS 10. One bus is why the NM-CYD-C5 puts screen, touch and SD on it.
- LCD peripherals: none, SPI only.
- I2C controllers: 2. LEDC channels: 6. RMT words a channel: 48.
- ESP Web Tools chipFamily: `ESP32-C5`.

## ESP-IDF

- On the v5.5 tag (version.cmake 5.5.0) `tools/idf_py_actions/constants.py` has esp32c5 in `PREVIEW_TARGETS`, not in
  `SUPPORTED_TARGETS`: `idf.py --preview ...` builds it. BUILD.BAT passes the flag for that target only, before the
  other arguments.
- CI's `espressif/idf:release-v5.5` image is a later 5.5, there it is supported and needs no flag.
- ESP Web Tools 10.2.1 depends on esptool-js ^0.5.7, which has an ESP32-C5 target with `CHIP_NAME "ESP32-C5"` and
  `BOOTLOADER_FLASH_OFFSET = 0x2000` (docs, read from the published packages). ESP Web Tools picks a build by
  `chipFamily === CHIP_NAME`, so the site should take a C5. The ESP32 Web Flasher's hand-written Arduino list stops at
  the C6, and "0x0 on all but classic" would be wrong for a C5.

## Memory: what a tester's board taught

The NM-CYD-C5 reports came from a tester's board (their logs), not from one here:

1. 0.6 from the site: "not enough memory for the screen". About 35 KB of internal RAM left, the 8 MB of PSRAM nearly
   empty: the build asked for PSRAM by name only, as on the S3.
2. 0.7: the map's picture, TLS and the WiFi's buffers moved to PSRAM (`MBEDTLS_EXTERNAL_MEM_ALLOC`,
   `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`). WiFi asks for PSRAM only when PSRAM is not optional, so `IGNORE_NOTFOUND` went off:
   a C5 without PSRAM does not start on this build. In that mode WiFi keeps static TX buffers, 16 by default at 1.6 KB
   each (26 KB): `CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=6`.
3. The site's build of 2026-11-16: the screen works, no planes. Every TLS read failed with
   `esp-aes: Failed to allocate memory` after the certificate was validated. With TLS in PSRAM the C5's AES-DMA writes
   into unaligned PSRAM through a bounce buffer of up to 1,600 bytes (`esp_aes_dma_core.c`) that must be DMA-capable
   internal RAM. The main SRAM was spent: 103.7 KB heap after static data, and 24-row strips took 61,440 bytes (two DMA
   strips and the work strip). Plain malloc still found the LP RAM, which is not DMA capable.
4. Fix: strips of 12 rows (30 KB back). Built, not run on a C5 since. If it still fails, the fallback is
   `CONFIG_MBEDTLS_HARDWARE_AES=n` (software AES on ROM tables, no RAM cost), as the Waveshare 7/7B do.

Why only the C5 among the SPI boards: the ESP32 and the C3 have no PSRAM, so TLS is in DMA-capable RAM and needs no
bounce (the ESP32's AES has no DMA at all); the S3 SPI boards keep TLS internal; the S3 RGB boards bounce too but have
more SRAM (reasoned from the code and the reports).

## Flash

QIO 80 MHz, CPU 240, PSRAM (`sdkconfig.defaults.esp32c5`, copy in ../board-files.md). Both boards have more flash than
4 MB and run the 4 MB layout as it is. The C5 apps are the largest here: 1,440,160 bytes (nmcydc5) and 1,430,112
(zlx2) in a 2 MB slot (built).

## USB

Some C5 boards have both a bridge and the chip's own USB (NM-CYD-C5: a CH340C to UART0 and the C5's own, two USB-C),
some only a bridge because 13 and 14 are used for other things (ZLX-ESP32-2). The console polls the chip's own USB
unless board.h says `NO_CHIP_USB`. The log goes to UART0 and to USB-Serial-JTAG by default
(`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`), deeper on the event task's stack: the C5 file sets it to 4096 as
the S3 LCD boards do.

## Strapping on the boards

The NM-CYD-C5 has MISO on 2, MOSI on 7, the backlight on 25, the LED on 27 (behind a level shifter) and BOOT on 28: all
by its maker. The ZLX-ESP32-2 has the touch's pen IRQ on 25, the SD card's CS on 27 and BOOT on 28. Fine on their
boards; a pull-up on one of them on a home-made board is how a C5 stops booting.
