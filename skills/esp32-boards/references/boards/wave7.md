# Waveshare ESP32-S3-Touch-LCD-7 (build `wave7`)

Waveshare's 7" 800x480 on the S3's RGB bus, GT911 touch, an I2C IO expander for the panel's reset and supply, the
backlight and the touch's reset. Driven by chipguyhere's library with the moving layer over the frame.

- Build: `wave7`, `RADAR_DISPLAY` 15 (`DISPLAY_WAVE7_800x480`), target esp32s3. `RADAR_OVERLAY` 1. Also in build.sh
  (macOS and Linux). Released in 0.14.
- How sure: brought in by chipguyhere from the fork chipguyhere/OWLOS-FLIGHT-RADAR-7-7B and run on one there (README;
  FORK-NOTES.md item 6, 2026-11-11). board.h's line "the -7 not yet" is older than that. Not run here. Experimental on
  the site.
- Sister board: [wave7b](wave7b.md), the same library, another build.

## Chip, flash, PSRAM, USB

ESP32-S3-WROOM-1 N8R8 ([esp32-s3](../chips/esp32-s3.md)): 8 MB flash, 8 MB octal PSRAM. The partition table keeps the
4 MB layout's app and tiles and adds a core dump on flash at 0x400000 (`partitions_wave7.csv`): the board often runs
with nobody on its serial port, and `idf.py coredump-info` reads a crash afterwards.

## Screen

800x480 on the RGB bus, the 5" boards' layout at 3/2 (534x320). The panel, its pins and timing, and the expander belong
to chipguyhere's 7/7B library, vendored in `components/chipguy_lcd7_7b` (MIT) with a small Arduino shim (`Arduino.h`,
`Wire.h` as TwoWire over the firmware's own `i2c_master` bus, which the GT911 shares). The panel is fed from PSRAM through
two bounce buffers (10 rows, not the library's 20, for internal RAM) by the library's fill task, and the moving layer
(the planes, the sweep, the compass ring) is put over each chunk as it lands. Design and numbers: FORK-NOTES.md and
the README's "The moving layer"; bus: ../../../esp32-lcd-touch/references/bus-rgb.md.

The library detects the board at run time, but the layout is fixed at build time, so each board has its own build.
Flashing the wrong one leaves the screen dark and the log names the right one.

## Touch

GT911 on I2C SDA 8, SCL 9, shared with the expander and the I2C header. Its reset goes through the expander:
lcd_wave7.c does it before `tp_init`. ../../../esp32-lcd-touch/references/touch/gt911.md.

## Buttons

BOOT is GPIO 0, a panel data line: no button the firmware can read (`PIN_BUTTON -1`, the site's `noboot`).

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_WAVE7_800x480 || RADAR_DISPLAY == DISPLAY_WAVE7B_1024x600
/* Waveshare ESP32-S3-Touch-LCD-7 (800x480, ESP32-S3-WROOM-1 N8R8) and -7B (1024x600, N16R8). added by
 * chipguyhere, driven by chipguyhere's 7/7B library, vendored in components/chipguy_lcd7_7b, which owns
 * the panel, its pins and timing, and the board's I2C expander (panel reset and supply, backlight,
 * the GT911's reset). lcd_wave7.c. the panel is fed from the PSRAM through two bounce buffers by
 * the library's fill task, and the moving layer (ovl.c) is put over each chunk as it lands.
 * one library for both boards, but the layout is built for one glass: each has its own build, and
 * the wrong one says so in the log. a -7B ran it here (2026-11-11); the -7 not yet */
#define DISP_WAVE7     1
#define DISP_STRIP     4        // rows a strip of the background, drawn on the second core (main.c bg_task): 16 KB of RAM
#define TOUCH_GT911    1        // its reset goes through the expander: lcd_wave7.c does it before tp_init
#define PIN_I2C_SDA    8        // the expander, the touch, the I2C header
#define PIN_I2C_SCL    9
#define PIN_BUTTON   (-1)       // BOOT is GPIO0, a panel data line
```

## sdkconfig and partitions

`sdkconfig.defaults` + `.esp32s3` + `.s3lcd` + `.s3rgb` + `.wave7` (copy in ../board-files.md). What `.wave7` adds:
run-time stats (the library reads each core's idle share from them); plain malloc of 1 KB or more may go to PSRAM;
big static state in PSRAM (about 65 KB of internal RAM back); software AES (the hardware AES failed with TLS buffers in
PSRAM); 8 MB flash, `partitions_wave7.csv`, a core dump on flash; the task watchdog does not watch core 0's idle task
(a TLS handshake beside a page being drawn left it no turn, which is no hang). App about 1.34 MB (built).

A build folder made before `sdkconfig.defaults.wave7` existed kept the old settings and hung on a dark screen after
the WiFi started; lcd_wave7.c now refuses to build with them (`#error` on the old values).

## Measured (by chipguyhere, on the fork's board)

The -7 runs (2026-11-11): 22 to 23 fps on the demo sky, no late rows, 2.4 MB of PSRAM heap free. The README gives about
34 frames a second for the sweep on the 7.

## Sources

chipguyhere's 7/7B library and the fork; FORK-NOTES.md in OWLOS Flight Radar.
