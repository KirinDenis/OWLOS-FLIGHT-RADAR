# Elecrow CrowPanel Advance 2.8" (build `crowadv28`)

Elecrow's "CrowPanel Advance 2.8 HMI ESP32 AI Display": an ESP32-S3 with an IPS ST7789, capacitive touch, a speaker,
a microphone and two USB-C. NOT the plain CrowPanel 2.8 HMI, which is a classic ESP32 with ILI9341V and XPT2046.

- Build: `crowadv28`, `RADAR_DISPLAY` 9 (`DISPLAY_CROWADV28_320x240`), target esp32s3. FusionEdge has a build of the
  same name.
- How sure: built 2026-11-04, not run on one here. Asked twice, the second time with its Amazon page: "ELECROW ESP32
  Display 320x240, 2.8" HMI Advanced ESP32-S3 IPS Touch Display, Support AI Speech Interaction". One asker reported
  0.4 resetting right after a WiFi scan on its screen ("Interrupt wdt timeout on CPU0"); 0.6 fixed WiFi scans
  restarting the S3 boards (see [esp32-s3](../chips/esp32-s3.md)). FusionEdge's build is built, not run either.

## Chip, flash, PSRAM, USB

ESP32-S3-WROOM-1 N16R8 ([esp32-s3](../chips/esp32-s3.md)), built with the 4 MB layout. Two USB-C (schematic J3, J4,
TYPEC-3.0-16P): one through a CH340K (U4) to UART0, one the S3's own. Either works for flashing and the console. App
1,232,816 bytes (built).

## Screen

ST7789 240x320 IPS. SCLK 42, MOSI 39, no MISO, CS 40, DC 41, reset by the board's own circuit, BL 38 (high, through a
transistor). `LCD_INVERT 1` (Elecrow's `invert = true`). BGR first, RGB behind BOOT (`rgb_order = false` is LovyanGFX's
BGR bit): `LCD_PROFILES 2`, `LCD_ORDER { 1, 0 }`. MADCTL 0xA0 (LovyanGFX rotation 3). SPI 40 MHz (Elecrow runs 80;
these pins go through the GPIO matrix). Panel: ../../../esp32-lcd-touch/references/panels/st7789.md.

## Touch

FT6336U at 0x38 on I2C SDA 15, SCL 16, INT 47, RST 48. The firmware asks the chip; INT is not read. Elecrow's example
says the touch of some panel batches is mirrored on X and of others on Y (the 5 cross alignment takes either).
../../../esp32-lcd-touch/references/touch/ft6336.md.

## Other parts

- No LED for the user. BOOT 0.
- Not driven by the radar: the speaker (NS4168, I2S LRCLK 11, data 12, BCLK 13, its control on 21 from V1.1: low switches
  the amplifier off), the microphone (9 and 10 behind a switch on 45), the buzzer on 8, the SD card (SCK 5, MISO 4,
  MOSI 6, CS 7), the wireless module socket. FusionEdge drives the speaker (mono) and the SD card on the HSPI host.
- GPIO 14 is a `TFT_PWR` net that Elecrow's examples never drive; neither does the build. A pin you do not know is a pin
  you do not touch.
- Same pins on V1.0, V1.1 (new amplifier) and V1.2 (new buttons).

## The V1.2 schematic's nets

IO38_LED_BK, IO39_TFT_SDA, IO40_TFT_CS, IO41_TFT_RS, IO42_TFT_SCK, IO15_SDA, IO16_SCL, IO47_TP_INT, IO48_TP_RST,
IO14_TFT_PWR, IO8_BEEP, IO21_NS_CTRL, IO11_I2S_LRCLK, IO12_I2S_SDIN, IO13_I2S_BCLK, IO4_SD_MISO, IO5_SD_SCK, IO6_SD_MOSI,
IO7_SD_CS, IO9_MIC_CLK, IO10_MIC_SD, IO45_SPISW_ITCHING, IO3_W_MOSI, IO46_W_CS, IO9_W_MISO, IO10_W_CLK. 117 nets in all,
among them USB_D+ and USB_D-; parts U1 ESP32-S3-WROOM-1, U4 CH340K, J3 and J4 TYPEC-3.0-16P-SMD, J1 HOLDER-MICROSD-9P,
U7 NS4168, FPC1 TFT-2.4/2.8LCD-SPI-10P. How they were read: ../research.md.

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_CROWADV28_320x240
/* Elecrow CrowPanel Advance 2.8" HMI, ESP32-S3-WROOM-1 N16R8, ST7789 240x320 IPS, FT6336U
 * capacitive touch. pins from Elecrow's repository (Elecrow-RD/CrowPanel-Advance-2.8-HMI-ESP32-AI-
 * Display-320x240): its LovyanGFX setup in example/V1.0/Arduino_Code/lesson-03, checked against
 * the nets of its V1.2 schematic, the same from V1.0 to V1.2. NOT run on one here: two viewers
 * asked for it. two USB-C: one through a CH340K to UART0, one the S3's own USB, the site talks
 * to it on either (console.c). the screen has its bus to itself, the SD card is on other pins */
#define DISP_LCD_SPI   1
#define LCD_CHIP_ST7789 1
#define PIN_LCD_SCLK  42
#define PIN_LCD_MOSI  39
#define PIN_LCD_MISO  (-1)      // not wired, the screen is only written
#define PIN_LCD_DC    41
#define PIN_LCD_CS    40
#define PIN_LCD_RST   (-1)      // a reset circuit of its own on the board
#define PIN_LCD_BL    38        // high = lit, through a transistor
/* Elecrow runs 80 MHz; these pins go through the GPIO matrix, 40 is what the other S3 runs */
#define LCD_SPI_HZ    (40 * 1000 * 1000)
#define LCD_MADCTL    0xA0      // MY|MV: Elecrow's landscape (LovyanGFX rotation 3), the other is the screen turned over
#define DISP_STRIP    24
/* an IPS panel: colours inverted, as Elecrow's setup has it (invert = true). BGR there too
 * (rgb_order = false is LovyanGFX's BGR bit), so BGR first, RGB behind BOOT in case */
#define LCD_INVERT     1
#define LCD_PROFILES   2
#define LCD_ORDER      { 1, 0 }
#define TOUCH_FT6336   1
#define FT6336_ADDR    0x38
#define PIN_I2C_SDA   15        // the touch and the I2C socket on the side
#define PIN_I2C_SCL   16
#define PIN_T_RST     48        // TP_RST on the schematic; its TP_INT is 47, touch.c asks the chip instead
#define PIN_BUTTON     0        // BOOT
```

## sdkconfig and partitions

`sdkconfig.defaults` + `.esp32s3` + `.s3lcd`: 4 MB, `partitions.csv`, QIO 80, octal PSRAM by name, event task stack
4096.

FusionEdge's `crowadv28`: Arduino (pioarduino 55.03.37, Arduino 3.3.7 on IDF 5.5.2), its root platformio.ini for an
N16R8 (`qio_opi`, 16 MB, two OTA slots), LovyanGFX rotation 3, touch at 400 kHz, LED strip and built-in LED off
(GPIO 48 is the touch's reset, 38 the backlight).

## Doubts

A GT911 at 0x5D is started in one of Elecrow's sketches, a leftover from another board, and an AI-written hardware
guide in the repository repeats it. The sketch reads touch through LovyanGFX's `Touch_FT5x06` at 0x38, and the wiki
says 0x38. If a board has no touch at all, this is the first thing to look at. Bruce has a boards/elecrow_advance_s3
folder, not compared.

## Sources

Elecrow-RD/CrowPanel-Advance-2.8-HMI-ESP32-AI-Display-320x240: example/V1.0/Arduino_Code/lesson-03 (LovyanGFX setup)
and "Eagle_SCH&PCB/1.2".

## To learn

Colours right as it starts, which landscape is upright, the touch, the console over each USB port.
