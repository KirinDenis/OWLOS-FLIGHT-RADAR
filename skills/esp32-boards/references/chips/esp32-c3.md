# ESP32-C3

One RISC-V core at 160 MHz, no floating point unit, no PSRAM, the tightest flash here. The chip under the round
CrowPanel 1.28". Sureness words as in SKILL.md. Raw numbers from ESP-IDF 5.5's own files and Espressif's esptool docs.

Boards here on this chip: [gc9a01](../boards/gc9a01.md). Others on sale: the round ESP32-2424S012C
(../boards-others.md). A board sold as "ESP32-2432S028" turned out to be a C3 clone: its ROM line said esp32c3, and it
took the round build.

## The numbers

- CPU: 1 RISC-V core, 160 MHz. No FPU flag in soc_caps.h.
- WiFi: 2.4 GHz only.
- Internal RAM: 384 KB, one region.
- GPIO: 0 to 21. No input-only pins.
- Flash pins: 12 to 17. No PSRAM.
- UART0: TX 21, RX 20.
- Own USB: D- 18, D+ 19. USB-Serial-JTAG, no OTG.
- BOOT pin: 9 (internal pull-up). Must also hold for download: 8 high; 8 low with 9 low is invalid.
- Strapping pins: 2, 8, 9.
- Bootloader at 0x0.
- SPI: SPI2 only. IOMUX SCLK 6, MOSI 7, MISO 2, CS 10. At 80 MHz or lower the GPIO matrix behaves as IOMUX (IDF
  docs). The round board runs its panel at 80 MHz (measured); its board.h calls that an IOMUX effect, the docs suggest
  that on this chip the panel and the wires matter more than the pins.
- LCD peripherals: none, SPI only.
- I2C controllers: 1. LEDC channels: 6. RMT words a channel: 48.
- ESP Web Tools chipFamily: `ESP32-C3`. ESP-IDF 5.5: supported.

## One core: priorities decide everything

`portNUM_PROCESSORS` is 1, the code pins everything to core 0. Measured on the round board: with the fetch task below
the screen task's priority it got under 1 ms a frame, map and TLS work starved IDLE, and the task watchdog reset the
board every ~29 s. Fix: the fetch task shares the screen's priority, they take turns a tick each.

```c
/* firmware/main/main.c, the two lines that make the same code right on one core and on two */
xTaskCreatePinnedToCore(fetch_task, "fetch", 12288, NULL, portNUM_PROCESSORS > 1 ? 4 : 5, &s_fetch, 0);
xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, portNUM_PROCESSORS > 1 ? 1 : 0);
```

A frame with network work in it runs long on one core: a weather page that decided "opened" from a gap over 400 ms
between frames restarted the round board on its first turn (fixed by watching the dial come up frame by frame,
2026-11-18). Hang diagnosis on it: esp32-runtime.

## No FPU

Float maths is library code. On the round board most of the frame went to the blips, ~8 fps with SPI at 80 MHz
(measured). The weather look's clouds took half its frame until `gfx_disc` got an integer path for the C3: the dial
went 3 to 4 fps -> 6 to 7 (measured, 0.16.1). The planes' float smoothing in 0.16.2 cost nothing measurable there: 5 to
7 fps beside the release build in the same sky.

## Memory and flash

- No PSRAM: no frame buffer, strips of 16 rows on the round board leave room for the map and TLS.
- Free heap measured on the round board: 31 KB under WiFi Radar; about 57 KB at fetch time in Flight Radar 0.14's two
  hour log.
- The round board has 4 MB of flash (measured) and runs the 2 MB layout (`partitions_2mb.csv`): app slot 0x150000 =
  1,376,256 bytes. The app grew from 1,213,392 bytes (2026-11-04) to 1,308,416 (2026-11-19, 95%, about 67 KB left).
  The tiles partition (640 KB) is where more room would come from (built).
- QIO 80 MHz flash (`sdkconfig.defaults.esp32c3`): the C3 runs most of its code from flash.

## USB

The C3's own USB on 18 and 19. The ESP32 Web Flasher's tests ran a C3 on its native USB (measured there). ESP-IDF 5.5
sends the log to UART0 and to USB-Serial-JTAG as a secondary output by default
(`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`, built), on the S3 and C5 as well. The round
CrowPanel uses 18 and 19 for its rotary encoder by Elecrow's wiki, so its USB-C is most likely a bridge to UART0: the
notes disagree, see ../boards/gc9a01.md. The port list's VID settles it on the desk (1A86 or 303A).

## Strapping

GPIO 2 is a strap and the round board's LCD DC: a panel input, no harm. 8 and 9 as above.

## sdkconfig

`sdkconfig.defaults.esp32c3` (copy in ../board-files.md): CPU 160, 2 MB, `partitions_2mb.csv`, QIO 80.

## WiFi on the round board

The round C3 heard fewer networks (10) than the ESP32 boards beside it (measured under WiFi Radar), and on one bench
about 9 joins in 10 ended in reason 201 while the classic boards on the same access point had no drops. An A/B with the
all-channel scan taken out changed nothing (265 drops against 315 in 40 minutes). The same board had joined that
morning: the radio or the place, not the firmware. A lower TX power is the untried next step.
