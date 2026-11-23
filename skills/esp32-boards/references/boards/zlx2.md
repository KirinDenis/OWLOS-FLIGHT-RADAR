# ZLX-ESP32-2, 2.8" ESP32-C5 (build `zlx2`)

A 2.8" ESP32-C5 board with a socket for an outside antenna, a resistive touch on pins of its own, a speaker amplifier,
a GPS socket and a battery input. Not the NM-CYD-C5: the same chips on other pins.

- Build: `zlx2`, `RADAR_DISPLAY` 19 (`DISPLAY_ZLX2_320x240`), target esp32c5. New in the 0.16.2 beta, so the site
  flashes it the beta only (flow.js `inStable`).
- How sure: built 2026-11-22, not run on one. Asked 2026-11-19 in GitHub issue #13 with the pins from the maker's
  manual; the asker offered to test. Their photo shows the board in its case lying down, on a firmware of its own.
- The NM-CYD-C5's build on this board holds this board's screen select (10) up as its SD card's, so the screen stays
  dark, and drives its screen's DC on this board's amplifier enable (24): never cross-flash them.

## Chip, flash, PSRAM, USB

- ESP32-C5-WROOM-1U-N8R8 ([esp32-c5](../chips/esp32-c5.md)): 8 MB flash, 8 MB PSRAM, a U.FL socket for an outside
  antenna. Built with the 4 MB layout. App 1,430,112 bytes (built).
- One USB-C, through a USB to serial chip on UART0 (11, 12, left out of the maker's list); which chip is not said. The
  C5's own USB pins, 13 and 14, are the backlight and the screen's DC here, so the console leaves the chip's USB alone
  (`NO_CHIP_USB`).

## Screen

ST7789 2.8" 240x320, no inversion. The maker's factory test (TFT_eSPI 2.5.43): BGR, rotation 1, that is MX|MV with the
BGR bit (0x68), SPI at 40 MHz. The build: `LCD_MADCTL 0x60`, BGR first and RGB behind BOOT (`LCD_ORDER { 1, 0 }`), so
0x68 goes out first; 40 MHz. Strips of 12 rows as on the NM-CYD-C5. Panel:
../../../esp32-lcd-touch/references/panels/st7789.md.

## Touch

XPT2046 on pins of its own, bit-banged as on the 2.8" CYD: CLK 4, DIN 5, DOUT 3, CS 23, pen IRQ 25. The maker's test
turns the IRQ's pull-up on, none on the board (`PIN_T_IRQ_PULLUP`). The maker's touch: 2 MHz, X and Y swapped, raw 220
to 3880 both ways; the 5 cross alignment finds the axes anyway.
../../../esp32-lcd-touch/references/touch/xpt2046.md.

## Other parts

- SD card CS 27, on the screen's bus, held up (`PIN_SD_CS`). Its MISO 2 is wired, but only the SD card answers there:
  the build leaves MISO off.
- Speaker amplifier SC8002B: enable 24 (high = on), PWM 8. Held off from the start of app_main (`PIN_AMP_EN`): a
  floating enable may hum, and the radar is silent.
- GPS socket for a GT-U7 on UART RX 0, TX 9. Battery on ADC 1 through 100k/100k. Green LED 26 (high = lit), not used.
  BOOT 28.

## Pins

LCD SCLK 6, MOSI 7, CS 10, DC 14, BL 13, reset on the chip's own. Touch 4, 5, 3, 23, 25. SD 27. Amplifier 24 and 8.
GPS 0, 9. Battery 1. LED 26. BOOT 28. The same in the issue and in the maker's pins.h.

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_ZLX2_320x240
/* ZLX-ESP32-2, 2.8": ESP32-C5-WROOM-1U N8R8 (a socket for an outside antenna), ST7789 240x320,
 * XPT2046 resistive. pins from its maker's pin list and factory test (qmaakdfq/ESP32-C5-Series-
 * Development-Board: GPIO_Pinout_CN_EN.md, pins.h, config.h), the same in GitHub issue #13. NOT run
 * on one here: a viewer asked for it and has one. not the NM-CYD-C5: the same chips on other pins.
 * one USB-C, through a USB to serial chip on UART0: the C5's own USB pins are the backlight and the
 * screen's DC here. the screen and the SD card share one SPI bus, the touch has pins of its own */
#define DISP_LCD_SPI   1
#define LCD_CHIP_ST7789 1
#define PIN_LCD_SCLK   6
#define PIN_LCD_MOSI   7
#define PIN_LCD_MISO  (-1)      // its 2 is wired, but only the SD card answers there
#define PIN_LCD_DC    14
#define PIN_LCD_CS    10
#define PIN_LCD_RST   (-1)      // on the chip's own reset
#define PIN_LCD_BL    13        // high = lit
#define PIN_SD_CS     27        // the SD slot on the same bus: held up, so a card in it keeps quiet
#define NO_CHIP_USB    1        // its pins, 13 and 14, are the backlight and the DC
#define LCD_SPI_HZ    (40 * 1000 * 1000)   // what the maker's factory test runs
/* the maker's landscape: TFT_eSPI's rotation 1, MX|MV. the other one is the screen turned over */
#define LCD_MADCTL    0x60
#define DISP_STRIP    12        // as the NM-CYD-C5, the same chip: 24 left no RAM for the AES
/* no inversion; BGR in the maker's setup, so BGR first, RGB behind BOOT in case */
#define LCD_PROFILES   2
#define LCD_ORDER      { 1, 0 }
#define TOUCH_XPT_BITBANG 1
#define PIN_T_CLK      4
#define PIN_T_MOSI     5
#define PIN_T_MISO     3
#define PIN_T_CS      23
#define PIN_T_IRQ     25
#define PIN_T_IRQ_PULLUP 1      // the maker's test turns the chip's pull-up on: none on the board
#define PIN_BUTTON    28        // BOOT
#define PIN_AMP_EN    24        // the speaker's amplifier (SC8002B), high = on: held off, the radar is silent
```

## sdkconfig and partitions

As [nmcydc5](nmcydc5.md): `sdkconfig.defaults` + `.esp32c5`, 4 MB on its 8 MB, `partitions.csv`.

## Sources

The maker's repository github.com/qmaakdfq/ESP32-C5-Series-Development-Board (GPIO_Pinout_CN_EN.md, factory tests with
their pins.h and config.h), GitHub issue #13 on OWLOS Flight Radar.

## To learn

Which landscape is upright (SET's FLIP turns it over), BGR as the maker says, the pen IRQ, whether 40 MHz is clean, the
planes over WiFi.
