# Elecrow CrowPanel Advance 5.0" (build `crowadv50`)

Elecrow's "CrowPanel Advance 5.0-HMI ESP32 AI Display", SKU DIS02050A: an 800x480 IPS on the S3's 16-bit RGB bus, with
the backlight and the touch's reset behind a chip of the board's own. Not the plain CrowPanel 5.0 (DIS07050H): that is
[crow50](crow50.md).

- Build: `crowadv50`, `RADAR_DISPLAY` 10 (`DISPLAY_CROWADV50_800x480`), target esp32s3. lcd_rgb.c
  and the scaled layout came with it.
- How sure: built 2026-11-05 with no board of this bus here, the picture seen only in the PC preview. The asker's V1.3
  ran it the same day (their logs and photo): picture and touch work. V1.0 to V1.2 not seen.
- Asked 2026-11-05 ("Any chance for a 5" board like an elecrow advanced 5"?"), the second ask for 800x480.

## Chip, flash, PSRAM, USB

ESP32-S3-WROOM-1 N16R8 ([esp32-s3](../chips/esp32-s3.md)), 16 MB flash, 8 MB octal PSRAM. One USB-C marked
USB-5V-IN, through a CH340K to UART0 (43, 44 by the silkscreen). BOOT GPIO 0.

## Screen

800x480 IPS, ST7262, 16-bit RGB, a frame of 768,000 bytes in PSRAM with bounce buffers as in Elecrow's IDF example.
Bus: ../../../esp32-lcd-touch/references/bus-rgb.md. Panel: ../../../esp32-lcd-touch/references/panels/st7262.md.

- Data lines, lowest bit first: blue 21 47 48 45 38, green 9 10 11 12 13 14, red 7 17 18 3 46. DE 42, VSYNC 41, HSYNC 40,
  PCLK 39. Porches 8, pulse 4, back 8 both ways, the clock idle high.
- Clock: 21 MHz in the V1.0 setup, 16 MHz in the V1.2 and V1.3 examples, Arduino and IDF: 16 here.
- RGB and BGR behind BOOT (the second profile trades the two groups of five data pins): `LCD_ORDER { 0, 1 }`.
- The layout: the 3.5"'s pages half as big again (`GFX_SCALED`, layout 534x320 on an 800x480 glass, fonts baked 1.5x
  in core/font_glass_*.c). The same code puts the picture on every 800x480 board.

## Backlight and the touch's reset, by board version

Printed on the board; nothing on the S3's pins (`LCD_BOARD_CHIP`, lcd_rgb.c):

- V1.0: a PCA9557 at 0x18, bit 1 the backlight, bit 2 the touch's reset.
- V1.1: an STC8H1K28 at 0x30 on the same I2C that takes one byte: 0x05 off to 0x10 brightest, 0x19 wakes the touch;
  0x10 must go first.
- V1.2 and V1.3: the same chip, 0 brightest to 245 off, 250 wakes the touch.
- Nothing tells V1.1 from V1.2, and 0x10 is brightest on one and nearly so on the other: the build sends 0x10 and has
  no dimming. V1.0 is told by its PCA9557 answering at 0x18.
- When the chip at 0x30 or the touch at 0x5D does not answer, Elecrow's demos send 250 to 0x30, hold GPIO 1 low for
  120 ms, let it go and wait 100 ms, for ever. The build tries five times, then goes on and says so in the log.

## Touch

GT911 at 0x5D on I2C SDA 15, SCL 16, polled; its INT net on GPIO 1, which only helps it wake (the reset is on the
board's chip). ../../../esp32-lcd-touch/references/touch/gt911.md.

## Other parts

The speaker, the microphone and the SD card share GPIO 4, 5, 6, 19, 20, picked by a switch (S1 S0) on the board; none
driven. A buzzer, an RTC at 0x51 on 15 and 16, a wireless socket: not driven.

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_CROWADV50_800x480
/* Elecrow CrowPanel Advance 5.0" HMI (DIS02050A), ESP32-S3-WROOM-1 N16R8, 800x480 IPS on the RGB bus
 * (ST7262), GT911 capacitive touch. pins from Elecrow's repository (Elecrow-RD/CrowPanel-Advance-5-
 * HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480): its LovyanGFX setups for V1.0 and V1.2, the
 * same but for the clock, 21 MHz and 16; 16 in the V1.2 and V1.3 examples, Arduino and IDF. a
 * viewer's V1.3 runs it (2026-11-05): picture, touch, 3 fps on the radar with one core. V1.0 to
 * V1.2 not seen yet. the backlight and the touch's reset are behind a chip of the board's own,
 * lcd_rgb.c. its speaker, microphone and SD card are on pins shared through a switch on the board,
 * none of them is driven here */
#define DISP_LCD_RGB   1
#define LCD_RGB_DATA   21, 47, 48, 45, 38,  9, 10, 11, 12, 13, 14,  7, 17, 18, 3, 46   // blue, green, red, lowest bit first
#define PIN_LCD_HSYNC 40
#define PIN_LCD_VSYNC 41
#define PIN_LCD_DE    42
#define PIN_LCD_PCLK  39
#define LCD_RGB_HZ    (16 * 1000 * 1000)
#define LCD_BOARD_CHIP 1
#define DISP_STRIP    12        // 16 left no room in the RAM for the second core's strip, on a V1.3
#define DISP_HELPER   1
#define LCD_PROFILES   2
#define LCD_ORDER      { 0, 1 }
#define TOUCH_GT911    1
#define PIN_I2C_SDA   15        // the touch, the board's own chip, a clock, the I2C socket
#define PIN_I2C_SCL   16
#define PIN_T_INT      1        // the touch's reset is on the board's own chip, this line only helps it wake
#define PIN_BUTTON     0        // BOOT
```

## sdkconfig and partitions

`sdkconfig.defaults` + `.esp32s3` + `.s3lcd` + `.s3rgb` (firmware/CMakeLists.txt, numbers 10, 11, 13, 14, 15, 16,
18): 4 MB, `partitions.csv`, PSRAM at 80 MHz, code run from PSRAM, TLS and WiFi buffers in PSRAM. App 1,257,408 bytes
(built). Elecrow's IDF example runs PSRAM at 80 MHz; the repository also carries an Arduino build at 120.

## Measured (the asker's V1.3)

- 2026-11-05: picture and touch work. 3 fps and 280 ms a frame on the radar (disc 151, panel 39, packing into the
  frame 52), 14 fps on SET. The second core never started: no room in internal RAM for its strip. Opening the WiFi
  page restarted the board: the event task's stack ran into the event queue's lock. That scan also found 0 networks,
  reason not known.
- The builds after it: strips 16 -> 12, TLS and WiFi buffers in PSRAM, a failed scan and the free RAM in the log.
- The asker's photo of the back: SKU DIS02050A, V1.3. Its silkscreen agrees: I2C on 15 and 16, UART0 on 43 and 44.

## Doubts

The picture is read from PSRAM all the time, so WiFi and flash writes (the map's tiles, the settings) can shake it.
XIP from PSRAM is meant to keep flash writes from stopping it; whether anything shakes shows only on a real board. The
V1.3 folder's hardware guide agrees with the pins and says a model compiled it: the code is the source, not the guide.

## Sources

Elecrow-RD/CrowPanel-Advance-5-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480: the LovyanGFX setups for V1.0 and
V1.2, the V1.2 and V1.3 examples.

## To learn

Does the list of networks come now, does it join, are the colours right, does the picture shake while the map loads.
