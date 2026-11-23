# ESP32-WROVER-E N16R8 with an ILI9488 (FusionEdge build `wrover-n16r8`)

A classic ESP32 module with PSRAM, wired by hand to an SPI ILI9488, an I2S DAC and two encoders. FusionEdge's
experimental classic-ESP32 profile.

- Build: FusionEdge's `wrover-n16r8` (esp32-wrover-n16r8 folder, its own platformio.ini). No Flight Radar build.
- How sure: docs for this skill. FusionEdge's README says the pinout was tested with an SPI ILI9488, an outside I2S DAC
  and two encoders; touch, SD, Bluetooth and the rest are off and untested.

## Chip, flash, PSRAM

Classic ESP32 ([esp32](../chips/esp32.md)), WROVER-E: 16 MB flash, 8 MB PSRAM. `board = esp-wrover-kit`, QIO, flash
40 MHz, CPU 240, `-DBOARD_HAS_PSRAM`, the 16 MB `partitions.csv`.

What its README says of the chip (docs):

- The classic ESP32 has much less internal DRAM than the S3 for this work. FusionEdge moves medium-sized general
  allocations to PSRAM here to keep internal RAM for WiFi, AsyncTCP, LittleFS locks and audio DMA; that stopped a
  WebUI file-serving crash.
- Of the 8 MB PSRAM, only about 4 MB is mapped into the heap Arduino uses.
- HTTPS streams may fail: a TLS handshake needs a large block of internal RAM in one piece.
- The WebUI can become very slow while audio and visualisation run.

## Pins

- ILI9488: DC 4, CS 5, RST none, SCK 18, MOSI 23 (MISO 19 off), backlight 14.
- PCM5102A DAC: DOUT 27, BCLK 26, LRC 25.
- Encoder 1 on 34, 36, 39 and encoder 2 on 33, 35, 32, internal pull-ups off: 34 to 39 are input-only with no pulls,
  they need outside resistors.
- IR 13.

## Sources

FusionEdge's esp32-wrover-n16r8 folder (README, myoptions.h, platformio.ini).
