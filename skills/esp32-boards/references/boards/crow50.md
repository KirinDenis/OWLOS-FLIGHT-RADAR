# Elecrow CrowPanel 5.0" plain, DIS07050H (build `crow50`)

Elecrow's plain 5" HMI, not the Advance: an ESP32-S3 with an 800x480 panel on the RGB bus, GT911 touch, the backlight
on a GPIO and the panel's pixel clock on GPIO 0.

- Build: `crow50`, `RADAR_DISPLAY` 18 (`DISPLAY_CROW50_800x480`), target esp32s3. Also in build.sh.
- How sure: built. Experimental: the radar itself has not run on one. Its pins are the ones a P2000 firmware already
  runs on this board. Brought in by bertvm (pull request #11, a clean redo of #9, which had taken number 15 and
  carried unrelated changes), released in 0.15.

## Chip, flash, PSRAM, USB

ESP32-S3 with octal PSRAM ([esp32-s3](../chips/esp32-s3.md)); the module's flash size is not recorded, the build uses
the 4 MB layout. A CH340: GPIO 19 and 20, the S3's own USB pins, are the touch's I2C, so `NO_CHIP_USB`.

## Screen

800x480 on the 16-bit RGB bus, the 5" boards' layout. 10 MHz pixel clock. Rows: pulse 1, front 8, back 32; frames:
pulse 1, front 8, back 8; sync lines idle low (`LCD_RGB_SYNC_IDLE_LOW`). Backlight GPIO 2, high = lit (PWM, it dims).
RGB and BGR behind BOOT in the code, but BOOT cannot be pressed (below): the site or `SET screen` picks. Bus:
../../../esp32-lcd-touch/references/bus-rgb.md.

Data lines, lowest bit first: blue 8 3 46 9 1, green 5 6 7 15 16 4, red 45 48 47 21 14. HSYNC 39, VSYNC 41, DE 40,
PCLK 0. The Advance's 15 and 16 are green bits of the picture here.

## Touch

GT911 on I2C 19 and 20 at 100 kHz (`I2C_BUS_HZ`). This panel's GT911 wants a stop between the register and the read; a
repeated start comes back empty (`GT911_STOP`). A PCA9557 at 0x18 holds the touch's reset (P0) and interrupt (P1):
before asking for the touch the build runs the P2000 firmware's sequence (`TOUCH_PCA9557_BOOT`), see
../board-code.md. ../../../esp32-lcd-touch/references/touch/gt911.md.

## Buttons

BOOT is GPIO 0, the panel's clock: no button the firmware can read (`PIN_BUTTON -1`, `RADAR_HAS_BUTTON` 0, the site's
`noboot`). Do not press it while the screen runs.

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_CROW50_800x480
/* Elecrow CrowPanel 5.0" plain (DIS07050H), not the Advance. pins from the P2000 firmware that
 * already runs on this board: backlight is GPIO 2, high, and the touch is I2C on 19 and 20 with a
 * PCA9557 at 0x18. the Advance's 15 and 16 are green bits of the picture here */
#define DISP_LCD_RGB   1
#define LCD_RGB_DATA   8, 3, 46, 9, 1,  5, 6, 7, 15, 16, 4,  45, 48, 47, 21, 14   // blue, green, red, lowest bit first
#define PIN_LCD_HSYNC 39
#define PIN_LCD_VSYNC 41
#define PIN_LCD_DE    40
#define PIN_LCD_PCLK   0        // BOOT is the panel's clock: no button the firmware can read
#define LCD_RGB_HZ    (10 * 1000 * 1000)
#define LCD_RGB_H_PULSE 1
#define LCD_RGB_H_FRONT 8
#define LCD_RGB_H_BACK  32
#define LCD_RGB_V_PULSE 1
#define LCD_RGB_V_FRONT 8
#define LCD_RGB_V_BACK  8
#define LCD_RGB_SYNC_IDLE_LOW 1
#define PIN_LCD_BL     2        // high = lit
#define DISP_STRIP    12
#define DISP_HELPER   1
#define LCD_PROFILES   2
#define LCD_ORDER      { 0, 1 }
#define TOUCH_GT911    1
#define GT911_STOP     1        // this panel wants a stop between the register and the read
#define I2C_BUS_HZ     100000
#define TOUCH_PCA9557_BOOT 0x18 // the reset sequence the P2000 firmware runs before it asks the touch
#define PIN_I2C_SDA   19        // the S3's own USB pins: this board has a CH340, they are free
#define PIN_I2C_SCL   20
#define NO_CHIP_USB    1
#define PIN_BUTTON    (-1)
```

## sdkconfig and partitions

`sdkconfig.defaults` + `.esp32s3` + `.s3lcd` + `.s3rgb`: 4 MB, `partitions.csv`. App 1,261,680 bytes (built).

## How it came in

The author builds with ESP-IDF 6 in the VS Code extension. Kept from the pull request: the IDF 6 guards (the RGB panel
asked for RGB565 by name; `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` in the shared defaults, which a 5.5
build warns about and skips) and asking the time as soon as WiFi is up, on every board. The stats server's list of
builds had to be updated before it counted this board. See ../new-board-recipe.md.

## Sources

The P2000 firmware that runs on this board (via bertvm's pull request).
