# ILI9488 and XPT2046 wired to an ESP32-S3 by hand (no build)

A group member's own wiring, asked 2026-11-04 whether the radar can run bigger ("ILI9488 TFT's or a Guition ESP32-S3
JC8048W550", "it works great, just needs a bigger display size option"). Waiting: the project said keep it for later.

- Build: none. How sure: docs, from a small drawing, so not sure of every digit. The asker will rewire if asked.
- Which S3 board (flash, PSRAM), 3.5" or 4.0", and whether they would test: not known.
- FusionEdge's reference board is also an S3 with an ILI9488 module, on other pins: [s3-ili9488](s3-ili9488.md).

## What it is

480x320, the 3.5" layout's size, so the screens are drawn already. ILI9488 over SPI, XPT2046 touch on pins of its own.
Panel: ../../../esp32-lcd-touch/references/panels/ili9488.md. Chip: [esp32-s3](../chips/esp32-s3.md).

## Pins, as read from the drawing

3.3V, GND, CS 10, RST 15, D/C 9, SDI 11, SCK 12, BL 8. Touch: TCK 14, TCS 18, TDI 17, TDO 47. SDO and PEN not
connected. The SD card's wires go off the picture.

## New firmware needed

1. 18-bit pixels over SPI, 3 bytes a pixel, not 2: 460,800 bytes a frame, 92 ms at 40 MHz against 61 ms for 16-bit.
2. The screen's reset on a GPIO: lcd_spi.c only sends the software reset now.
3. XPT2046 with no pen IRQ pin, on its own pins on an S3 (`TOUCH_XPT_BITBANG` needs a build with no `PIN_T_IRQ`).

Elecrow's CrowPanel 3.5 has the same ILI9488 problem (../boards-others.md).

## To learn

The digits confirmed (send them back in text and ask for a yes), which S3 board, the screen's size, whether they would
test.
