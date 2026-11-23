# M5Stack Tab5 (build `tab5`)

A 5" tablet on an ESP32-P4: a 720x1280 IPS panel standing on MIPI-DSI, capacitive touch, the WiFi on an ESP32-C6 beside
the P4, through Espressif's board package.

- Build: `tab5`, `RADAR_DISPLAY` 12 (`DISPLAY_TAB5_1280x720`), target esp32p4. Released in 0.8.
- How sure: brought in by parispitts (pull request #7), who runs it on a Tab5 with the ST7121 panel (board V1.0): the
  WiFi, planes, the map and the touch work. Not run here, nor on the two other panels the Tab5 is sold with.
  Experimental on the site.

## Chip, flash, PSRAM, USB

- ESP32-P4 ([esp32-p4](../chips/esp32-p4.md)), a v1.3 chip before production silicon: 16 MB flash, 32 MB PSRAM.
- WiFi through the on-board ESP32-C6 over SDIO with ESP-Hosted. The preset `CONFIG_ESP32P4_TAB5_C6_BOARD` puts the bus
  on GPIO 8 to 13 and the C6's reset on 15. The C6 keeps M5Stack's co-processor firmware, 1.4.1 as it reports itself,
  which a 3.x host talks to as it is.
- The console is the chip's own USB alone (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`).

## Screen

5" 720x1280 IPS standing; the picture lies, 1280x720. Three panel revisions exist: ILI9881C with a GT911, ST7123, and
ST7121 with its own touch. Espressif's board package (`espressif/m5stack_tab5`) tells them apart at power on and brings
up the one it finds, so board.h names no panel or touch pins, and the I2C bus is the package's. Bus:
../../../esp32-lcd-touch/references/bus-mipi-dsi.md.

- lcd_dsi.c: as on the RGB boards the chip reads a whole frame out of PSRAM all the time and a strip is only put in
  it; here each strip of the picture's rows is turned on its side into a column of the frame by the P4's PPA, and the
  driver keeps the cache straight.
- `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1`, no DMA2D: one frame, the strips copied in by the CPU or the PPA.
- The layout is the 5" boards' 9/4 as big (569x320: 1280 / 2.25 = 568.9, the half pixel past the edge clipped). The
  fonts are the 5" boards' 3/2 ones, so the writing is two thirds of its size; a 9/4 bake is to come.
- Strips of 12 rows, 60 strips: at 1280 wide the strip, its turned copy and the two cores' canvases share about 346 KB
  of RAM.
- The board's I2C bus and the switches on its IO expanders are brought up by `disp_board_up()` (the WiFi's power among
  them), before the WiFi starts.

## Touch

The package's `esp_lcd_touch`, whichever chip it found (`TOUCH_BSP`). It counts the standing glass's pixels, 720 x
1280: the long way is the picture's width. ../../../esp32-lcd-touch/references/touch/gt911.md for the ILI9881C
revision; the ST7121's own touch has no file here.

## Buttons and LEDs

No BOOT reaches the P4: the power button is the charger chip's, the other buttons go to its power chip and the C6
(`PIN_BUTTON -1`, `RADAR_HAS_BUTTON` 0). No LED.

## board.h

```c
#elif RADAR_DISPLAY == DISPLAY_TAB5_1280x720
/* M5Stack Tab5: ESP32-P4 (16 MB flash, 32 MB PSRAM), a 5" 720x1280 IPS panel standing on MIPI-DSI,
 * capacitive touch, Wi-Fi through an ESP32-C6 on SDIO (sdkconfig.defaults.esp32p4). three revisions
 * of the panel exist (ILI9881C with a GT911, ST7123, ST7121 with its own touch): Espressif's board
 * package (espressif/m5stack_tab5, main/idf_component.yml) tells them apart at power on and brings
 * up the one it finds, so no panel or touch pins are named here, and the I2C bus is its. no BOOT
 * button reaches the P4: the power button is the charger chip's. no LED. written on one with the
 * ST7121 (board V1.0), lcd_dsi.c */
#define DISP_LCD_DSI   1
#define DISP_STRIP    12        // 60 strips: at 1280 wide the strip, its turned copy and the two cores' canvases have to share 346 KB of RAM
#define DISP_HELPER   1
#define TOUCH_BSP      1        // the package's esp_lcd_touch, whichever chip it found
#define PIN_BUTTON    (-1)      // none
```

## Components

From the registry, only for esp32p4 (main/idf_component.yml, ../board-files.md): `espressif/m5stack_tab5` ^1.3.2,
`espressif/esp_lcd_touch` ^1.2.1 (a 2.x may change the calls touch.c makes), `espressif/esp_lvgl_port` ~2.8.0 (the
package wants it; 2.9.0 uses a DPI callback ESP-IDF 5.5.3 lacks), `espressif/esp_wifi_remote` ^1.6.5,
`espressif/esp_hosted` ^3.1.0 (knows this board as ESP32P4_TAB5_C6_BOARD). main/CMakeLists.txt adds `esp_driver_ppa`.

## sdkconfig and partitions

`sdkconfig.defaults` + `sdkconfig.defaults.esp32p4` (copy in ../board-files.md): rev below v3 and experimental
features (for the v1.3 chip), 16 MB flash at 80 MHz, PSRAM at 200 MHz, CPU 360, main task stack 8192, event task stack
4096, the console on USB-Serial-JTAG, one DPI buffer and no DMA2D, the C6 board preset. `partitions_16mb.csv`: app
4 MB, tiles 6 MB.

## Building

The Tab5 does not build on the local v5.5 tag (component manager, MissingKconfigError before compiling). CI's
release-v5.5 image builds it.

## To learn

Which panel a board has, does the flash from the site go through, does the WiFi join.
