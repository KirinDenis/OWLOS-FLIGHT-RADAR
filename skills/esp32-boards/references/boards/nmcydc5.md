# NM-CYD-C5, 2.8" ESP32-C5 (build `nmcydc5`)

A 2.8" board in the CYD's form on an ESP32-C5, with two USB-C ports and screen, touch and SD card on one SPI bus.

- Build: `nmcydc5`, `RADAR_DISPLAY` 8 (`DISPLAY_NMCYDC5_320x240`), target esp32c5.
- How sure: built 2026-11-03. Not run here. A tester who asked for it ran site builds on theirs and sent logs: 0.6
  stopped with "not enough memory for the screen"; on 2026-11-16 the screen worked but every TLS read failed. The fix
  (12-row strips) is built, not run on one since.

## Chip, flash, PSRAM, USB

- ESP32-C5-WROOM-1 N16R8 ([esp32-c5](../chips/esp32-c5.md)): 16 MB flash, 8 MB quad PSRAM. One RISC-V core at 240 MHz,
  2.4 and 5 GHz WiFi. Built with the 4 MB layout.
- Two USB-C: one through a CH340C to UART0 (TX 11, RX 12), one the C5's own (D- 13, D+ 14). The site talks on either.
- The C5 app is the largest here: 1,440,160 bytes (built).

## Screen

2.8" ST7789 320x240, no inversion (every source agrees). MADCTL 0xA0. RGB in the maker's Arduino setup and most ports,
BGR in its MicroPython demo: two profiles, RGB first, BOOT switches (`LCD_PROFILES 2`, `LCD_ORDER { 0, 1 }`). Panel:
../../../esp32-lcd-touch/references/panels/st7789.md.

- SPI 30 MHz asked: the maker's demos run 20, one project saw stripes at 40 on this shared bus and runs 30, another
  runs 40. The driver takes the nearest it can make at or below.
- Strips of 12 rows: at 24 they took 60 KB of internal RAM and left none for the AES's DMA buffer, see the chip file.

## Touch

XPT2046 on the LCD's bus, CS 1, pen IRQ 3 by the schematic; most ports ask the chip instead. touch.c finds out: a pin
that reads low at start or misses a press is dropped. ../../../esp32-lcd-touch/references/touch/xpt2046.md.

## Buttons, LED, other parts

- BOOT 28.
- One WS2812C on 27 behind a level shifter, GRB (Bruce's order). Driven by led.c, not run.
- SD card on the same bus, CS 10, held high by the firmware (`PIN_SD_CS`), or a card in the slot answers too.
- Not driven: an AHT20 on I2C 9/8, the speaker on 26.
- Its MISO (2), MOSI (7), backlight (25), LED (27) and BOOT (28) are all C5 strapping pins, by the maker's design.

## Pins

Bus SCLK 6, MOSI 7, MISO 2; LCD CS 23, DC 24, reset on the chip's own, BL 25 (high = lit). Touch CS 1, IRQ 3. SD CS 10.
LED 27. BOOT 28.

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_NMCYDC5_320x240
/* NM-CYD-C5, 2.8" in the CYD's form: ESP32-C5-WROOM-1 N16R8, ST7789 320x240, XPT2046 resistive.
 * pins from its maker's schematic SCH_NM-CYD-C5-v1.0 (RockBase-iot/NM-CYD-C5, gone from GitHub,
 * kept in its forks), the same in Bruce's port (boards/nm-cyd-c5) and in other projects on it.
 * NOT run on one here: a viewer asked for it and has one. two USB-C: one through a CH340 to
 * UART0, one the C5's own USB, the site talks to it on either (console.c). the screen, the touch
 * and the SD card share one SPI bus */
#define DISP_LCD_SPI   1
#define LCD_CHIP_ST7789 1
#define PIN_LCD_SCLK   6
#define PIN_LCD_MOSI   7
#define PIN_LCD_MISO   2        // the touch reads it
#define PIN_LCD_DC    24
#define PIN_LCD_CS    23
#define PIN_LCD_RST   (-1)      // on the chip's own reset
#define PIN_LCD_BL    25        // high = lit
#define PIN_SD_CS     10        // the SD slot on the same bus: held up, so a card in it keeps quiet
/* the maker's demos run 20 MHz, one project saw stripes at 40 on this shared bus and runs 30,
 * another runs 40. 30 asked, the SPI driver takes the nearest it can make below */
#define LCD_SPI_HZ    (30 * 1000 * 1000)
#define LCD_MADCTL    0xA0      // MY|MV, RGB: the landscape most of them use, the other is the screen turned over
/* 20 strips. at 24 rows they took 60 KB of the chip's own RAM and left none for the AES: with TLS
 * in the PSRAM it decrypts through a buffer of up to 1.6 KB in that RAM, and every answer failed
 * ("esp-aes: Failed to allocate memory", a tester's log). 12 gives back 30 KB */
#define DISP_STRIP    12
/* no inversion, every source agrees. RGB or BGR they do not: RGB in the maker's Arduino setup and
 * in most ports, BGR in its MicroPython demo. both are in the build, RGB first, BOOT in the first
 * seconds switches (lcd_spi.c) */
#define LCD_PROFILES   2
#define LCD_ORDER      { 0, 1 }
#define TOUCH_XPT_SPI  1
#define PIN_T_CS       1
#define PIN_T_IRQ      3        // on the schematic; most ports ask the chip instead, touch.c finds out
#define PIN_BUTTON    28        // BOOT
#define PIN_LED_WS2812 27       // one WS2812C behind a level shifter, green red blue (Bruce's order)
```

## sdkconfig and partitions

`sdkconfig.defaults` + `.esp32c5`: QIO 80, CPU 240, PSRAM required (no `IGNORE_NOTFOUND`), TLS and WiFi buffers in
PSRAM, 6 static WiFi TX buffers, event task stack 4096. 4 MB, `partitions.csv`. The local v5.5 tag needs `--preview`.
Copy in ../board-files.md.

## Sources

The maker's schematic SCH_NM-CYD-C5-v1.0 in forks (RockBase-iot/NM-CYD-C5 is gone; github.com/michaelhenry/NM-CYD-C5,
Documention/2-sch), Bruce's boards/nm-cyd-c5.

## To learn

Whether 12-row strips let TLS through, RGB or BGR, which landscape is upright, whether the pen IRQ is there, whether
30 MHz stays clean.
