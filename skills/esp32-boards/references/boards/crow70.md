# Elecrow CrowPanel 7.0" plain, DIS08070H (build `crow70`)

Elecrow's plain 7" HMI: ESP32-S3-WROOM-1 N4R8, 800x480 TN on the RGB bus, GT911, a CH340C. Elecrow sells two 7" boards
and a shop title does not tell them apart: the CrowPanel Advance 7.0 is N16R8, IPS, SC7277, with a swappable radio
socket and likely a backlight chip as on the Advance 5.0 (not checked, not built).

- Build: `crow70`, `RADAR_DISPLAY` 14 (`DISPLAY_CROW70_800x480`), target esp32s3. Also in build.sh. Released in 0.11.
- How sure: built 2026-11-10, experimental, not run on one. Asked 2026-11-09 in the group ("ELECROW CrowPanel 7" ESP32
  Display Touchscreen 800x480 ... ESP32-S3 240MHz, 800x480 IPS Touchscreen"); the asker has one and tests it. Their
  photo of the back: "ESP32 Display-7 inch, SKU:DIS08070H", V3.0.
- 2026-11-10 the asker flashed the CrowPanel Advance 5" build on it: WiFi set up and planes on the site, the screen
  dark. As expected: other pins for the screen and the backlight. It proved the chip, the USB and the firmware's core
  on this board (measured by the asker).
- 2026-11-24 an owner ran 0.16.2 on it: "it works", with a photo of the WEATHER dial. **The circles are not round:
  about 7 mm wider than tall** (measured by the owner; "not a problem" to them).

## Pixels that are not square

The cause, reasoned, not measured here: a 7" 800x480 TN glass is about 154 x 86 mm, so a pixel is about 0.193 mm
wide and 0.179 mm tall, 7.7% wider than tall. A circle of about 440 px comes out about 6 mm wider than tall, as the
owner saw. Every round thing is stretched: the radar's scope, the weather dial, the rings; the map tiles too
(distances across differ from distances up and down).

- The 5" 800x480 boards, the Tab5 and the 10.1" have square pixels (reasoned from their glass sizes). wave7 (800x480
  7") is likely the same as this board; wave7b (1024x600 7") about 5% wide.
- Planned for 0.17, not built: a pixel aspect per board in board.h, and the round things drawn as ellipses (x scaled
  by about 0.93). The map's stretch is a separate question.
- The general point: never assume square pixels on a glass whose size you have not divided by its resolution.

## Chip, flash, PSRAM, USB

ESP32-S3-WROOM-1 N4R8 ([esp32-s3](../chips/esp32-s3.md)): 4 MB flash is enough, the S3 RGB builds use `partitions.csv`.
8 MB octal PSRAM. A CH340C; I2C on 19 and 20, the S3's USB pins (`NO_CHIP_USB`). Many shops call the panel IPS; Elecrow's
spec says TN.

## Screen

800x480 on the 16-bit RGB bus, the 5" boards' layout. Rows: pulse 48, back 40, front 40; frames: pulse 31, back 13,
front 1, the same in both of Elecrow's examples. The clock is 15 MHz in one example and 24 in the other: 16 here, as
the 5" boards run it out of PSRAM. An ESPHome setup said to work well uses 15 MHz and esp_lcd's own sync polarity.
Backlight GPIO 2, high = lit, an MT9201's enable (PWM, it dims). Bus: ../../../esp32-lcd-touch/references/bus-rgb.md.

Data lines, lowest bit first: blue 15 7 6 5 4, green 9 46 3 8 16 1, red 14 21 47 48 45. HSYNC 39, VSYNC 40, DE 41,
PCLK 0.

## Touch

GT911 on I2C 19 and 20. On V3.0 a PCA9557 at 0x18 holds the GT911's reset (IO0) and INT (IO1), as on the Advance 5.0
V1.0. An ESPHome draft says driving it at every start broke the touch, and an older Elecrow V3.0 example leaves it
alone; Elecrow's own examples do drive it. The build leaves it alone unless the touch is silent, then resets the touch
in Elecrow's order with INT low for 0x5D (`TOUCH_RESET_PCA9557`); IO2 and up, the panel's reset and power among them,
stay as they are. ../board-code.md, ../../../esp32-lcd-touch/references/touch/gt911.md.

## Buttons

BOOT is GPIO 0, the panel's clock: no button the firmware can read, and pressing it while the screen runs shorts the
clock (`PIN_BUTTON -1`, the site's `noboot`, no "PRESS BOOT" hints).

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_CROW70_800x480
/* Elecrow CrowPanel 7.0" HMI (DIS08070H), the plain one: ESP32-S3-WROOM-1 N4R8, 800x480 TN on the RGB
 * bus, GT911, a CH340C. pins and timing from Elecrow's repository (Elecrow-RD/CrowPanel-7.0-HMI-ESP32-
 * Display-800x480): its V3.0 examples, Arduino with LovyanGFX, and its hardware guide from the V3.0
 * schematic. a second source, an ESPHome setup said to work well (Home Assistant's forum, "Elecrow
 * CrowPanel 7" with ESPHome"): the same pins, 15 MHz, the sync polarity esp_lcd's own. the clock is
 * 15 MHz in one of Elecrow's examples and 24 in the other, the rows and frames round it the same in
 * both: 16 here, as the 5" boards run it out of the PSRAM. BOOT is on GPIO 0, the panel's clock: no
 * button the firmware can read, and pressing it while the screen runs shorts the clock. a PCA9557 at
 * 0x18 has the touch's reset (touch.c). an owner's V3.0 ran the CrowPanel Advance 5" build: WiFi and
 * planes, the screen dark. this one NOT run on one yet: experimental, its owner tests it */
#define DISP_LCD_RGB   1
#define LCD_RGB_DATA   15, 7, 6, 5, 4,  9, 46, 3, 8, 16, 1,  14, 21, 47, 48, 45   // blue, green, red, lowest bit first
#define PIN_LCD_HSYNC 39
#define PIN_LCD_VSYNC 40
#define PIN_LCD_DE    41
#define PIN_LCD_PCLK   0
#define LCD_RGB_HZ    (16 * 1000 * 1000)
#define LCD_RGB_H_PULSE 48
#define LCD_RGB_H_BACK  40
#define LCD_RGB_H_FRONT 40
#define LCD_RGB_V_PULSE 31
#define LCD_RGB_V_BACK  13
#define LCD_RGB_V_FRONT  1
#define PIN_LCD_BL     2        // high = lit, an MT9201's enable
#define DISP_STRIP    12        // as the 5" boards'
#define DISP_HELPER   1
#define LCD_PROFILES   2
#define LCD_ORDER      { 0, 1 }
#define TOUCH_GT911    1
#define TOUCH_RESET_PCA9557 0x18 // only when the GT911 does not answer: touch.c
#define PIN_I2C_SDA   19        // the S3's own USB pins: this board has a CH340C, they are free
#define PIN_I2C_SCL   20
#define NO_CHIP_USB    1
#define PIN_BUTTON   (-1)       // BOOT is the panel's clock, see above
```

## sdkconfig and partitions

`sdkconfig.defaults` + `.esp32s3` + `.s3lcd` + `.s3rgb`: 4 MB, `partitions.csv`. App 1,261,808 bytes (built).

## Sources

Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480: V3.0 examples (crowpanel-esp32-7.0.ino, Course/LVGL_Arduino7.0)
and its V3.0 hardware guide; Elecrow's wiki for the backlight on GPIO 2; ESPHome setups on Home Assistant's forum for
the same sync pins.
