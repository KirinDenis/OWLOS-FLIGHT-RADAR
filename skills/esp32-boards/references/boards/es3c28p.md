# LCDWIKI ES3C28P, 2.8" ESP32-S3 (build `es3c28p`)

The CYD's form with more RAM: an ESP32-S3 with PSRAM, an IPS ILI9341V and capacitive touch.

- Build: `es3c28p`, `RADAR_DISPLAY` 7 (`DISPLAY_ES3C28P_320x240`), target esp32s3.
- How sure: built 2026-11-03, not run on one. Asked for by someone who owns one; they were asked whether they would
  try a test build.
- It is the board that showed the site's rule: its owner flashed a basic 2432S from a Chromebook with no trouble, then
  this one was refused. ESP Web Tools turns down a chip that the card's manifest has no build for.

## Chip, flash, PSRAM, USB

ESP32-S3R8 ([esp32-s3](../chips/esp32-s3.md)), 8 MB octal PSRAM, 16 MB flash, built with the 4 MB layout. Its USB-C is
the S3's own, no bridge: the console polls USB-Serial-JTAG for it.

## Screen

2.8" ILI9341V 240x320 IPS. SPI 40 MHz (LCDWIKI's and Bruce's setups). MADCTL 0x28 (Bruce's landscape; the other one is
the screen turned over). BGR and inverted in LCDWIKI's start-up and in both ports: that is the CYD's v2, so the CYD's
three start-ups are in the build behind BOOT, v2 first (`LCD_PROFILES 3`, `LCD_ORDER { 1, 0, 2 }`). Panel:
../../../esp32-lcd-touch/references/panels/ili9341.md.

## Touch

FT6336G at 0x38 on I2C SDA 16, SCL 15, INT 17, RST 18. It goes through the CST816 read path: the same registers from
0x02. It counts screen pixels, span 320. Reset pulse 10 ms low, then 300 ms before it answers.
../../../esp32-lcd-touch/references/touch/ft6336.md.

## Buttons, LED, other parts

- BOOT 0.
- One WS2812B on GPIO 42, order GRB (Bruce's). Driven by led.c since 0.4 (BOARDS-WANTED's "not driven yet" is older).
- Not driven: the ES8311 codec and the speaker (amplifier enable on 1, low = on), the SD card, the battery on 9.

## Pins

LCD CS 10, DC 46, SCLK 12, MOSI 11, MISO 13, BL 45 (high = lit), reset on the chip's own. Touch SDA 16, SCL 15, INT 17,
RST 18. LED 42. DC 46 and BL 45 are S3 strapping pins, by the maker's design.

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_ES3C28P_320x240
/* LCDWIKI ES3C28P, 2.8": ESP32-S3 N16R8, ILI9341V 240x320 IPS, FT6336G capacitive touch. pins
 * from LCDWIKI's pin table and schematic (lcdwiki.com/2.8inch_ESP32-S3_Display), the same in
 * Bruce's port (BruceDevices/firmware, boards/ES3C28P, said to be run on one). NOT run on one
 * here: a viewer asked for it and has one. its USB-C is the S3's own USB, no bridge chip: the
 * site talks to it there (console.c) */
#define DISP_LCD_SPI   1
#define LCD_CHIP_ILI9341 1
#define PIN_LCD_SCLK  12
#define PIN_LCD_MOSI  11
#define PIN_LCD_MISO  13
#define PIN_LCD_DC    46
#define PIN_LCD_CS    10
#define PIN_LCD_RST   (-1)      // on the chip's own reset
#define PIN_LCD_BL    45        // high = lit
#define LCD_SPI_HZ    (40 * 1000 * 1000)   // what LCDWIKI's and Bruce's setups use
#define LCD_MADCTL    0x28      // MV|BGR: Bruce's landscape. the other one is the screen turned over
#define DISP_STRIP    24
/* BGR with the colours inverted, in LCDWIKI's own start-up and in both ports: the CYD's v2 is
 * that. the CYD's three start-ups stay in the build all the same, BOOT in the first seconds goes
 * through them as on the CYD, in case this panel is another */
#define LCD_PROFILES   3
#define LCD_ORDER      { 1, 0, 2 }
#define TOUCH_FT6336   1
#define FT6336_ADDR    0x38
#define PIN_I2C_SDA   16
#define PIN_I2C_SCL   15
#define PIN_T_RST     18
#define PIN_BUTTON     0        // BOOT
#define PIN_LED_WS2812 42       // one WS2812B, green red blue (Bruce's order)
```

## sdkconfig and partitions

`sdkconfig.defaults` + `.esp32s3` + `.s3lcd` (firmware/CMakeLists.txt, number 7): 4 MB, `partitions.csv`, QIO 80 MHz,
octal PSRAM by name, event task stack 4096. App 1,249,024 bytes (built). The `.s3lcd` file was called `.es3c28p` until
a second S3 LCD board came.

## Sources

LCDWIKI's pin table and schematic (lcdwiki.com/2.8inch_ESP32-S3_Display), Bruce's boards/ES3C28P, said to be run on
one. They agree.

## To learn

Colours as it starts, which landscape is upright, the touch, the console over the chip's own USB.
