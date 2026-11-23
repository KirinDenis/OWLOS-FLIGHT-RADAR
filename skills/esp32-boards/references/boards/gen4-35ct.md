# 4D Systems gen4-ESP32-35CT, 3.5" (FusionEdge build `gen4-35ct`)

4D Systems' 3.5" 320x480 ILI9488 board with a FocalTech capacitive touch and a microSD slot, on an ESP32-S3.

- Build: FusionEdge's `gen4-35ct` (gen4-ESP32-35CT_options). No Flight Radar build: it would need the ILI9488's 18-bit
  pixels ([ili9488-diy](ili9488-diy.md)).
- How sure: docs for this skill. FusionEdge's folder README credits a contributor who developed the support (built in
  FusionEdge's CI).

## Chip, flash, PSRAM

ESP32-S3, 16 MB flash, 8 MB OPI PSRAM ([esp32-s3](../chips/esp32-s3.md)).

## Screen and touch

- ILI9488 on SPI2: SCK 14, MOSI 13, CS 3, DC 21, RST 7, no MISO. Backlight 4. 20 MHz write, 16 MHz read, no inversion,
  LovyanGFX rotation 0 (landscape is applied by FusionEdge's driver). Panel:
  ../../../esp32-lcd-touch/references/panels/ili9488.md.
- FocalTech touch (`TS_MODEL_FT6X36`) at 0x38 on I2C port 0: SDA 10, SCL 9, RST 11, INT 8, 400 kHz, its own bus.
  ../../../esp32-lcd-touch/references/touch/ft6336.md.

## Other parts

microSD on SPI3 (HSPI): SCK 42, MISO 41, MOSI 2, CS 1, 4 MHz. An outside I2S DAC example on 38, 39, 40. Bluetooth, IR
and encoders left off: their pins depend on what is wired outside the board.

## Sources

FusionEdge's gen4-ESP32-35CT_options (README, myoptions.h). Check the pins against the exact board revision.
