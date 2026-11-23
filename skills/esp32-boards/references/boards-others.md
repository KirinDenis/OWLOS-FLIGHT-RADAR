# Boards on sale that nobody asked for yet

From BOARDS-WANTED.md's list of 2026-11-03, with pins added on 2026-11-04 from
github.com/rzeldent/platformio-espressif32-sunton (one JSON per board, `extra_flags`). All of this is docs: never built,
never run, unless a line says it moved to boards/. The digits came through a fetch tool that summarises pages, so open
the raw JSON before a build, and check each board against a second source (its maker's wiki, Bruce's boards/ folder).

Ordered from the easiest port to the hardest, as in BOARDS-WANTED.

Common to the Sunton ESP32 boards in the JSONs (classic ESP32, 4 MB): LCD MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, no reset pin,
RGB LED 4, 16, 17, speaker 26, light sensor 34, SD CS 5, MOSI 23, SCLK 18, MISO 19. rzeldent runs them at 24 MHz, we run the
same pins at 40. A capacitive touch there is on SDA 33, SCL 32, RST 25, INT 21. A resistive touch is on the LCD bus, CS 33, INT 36,
except the 2.8" CYD, which has its own pins.

## Only pins: chips the firmware knows

- Guition JC2432W328C, the capacitive 2.8" CYD, looks the most common now. ESP32, ST7789 IPS by the JSON, RGB (ILI9341 by
  batch, says BOARDS-WANTED), BL 27, CST816S or CST820 at 0x15 on SDA 33, SCL 32. IPS means `LCD_INVERT 1` is the first
  guess. Flash 40 MHz DIO in the JSON.
- ESP32-2432S024R: 2.4" ILI9341, BGR, BL 27, XPT2046 on the LCD bus. Micro USB.
- ESP32-2432S024C: the same with CST820 at 0x15 (rzeldent's macro calls it CST816S; the README says CST820 is upward
  compatible). ESP32-2432S024N: no touch.
- ESP32-2432S032R: 3.2" ST7789 IPS, RGB, BL 27, XPT2046 on the LCD bus.
- ESP32-2432S032C: the same with GT911 at 0x5D. ESP32-2432S032N: no touch.
- Elecrow CrowPanel 2.8 and 2.4 ESP32 HMI, the plain ones: classic ESP32, ILI9341V, XPT2046 on the LCD bus, CS 33.
- Elecrow CrowPanel Advance 2.4: S3, Elecrow's examples are shared with the 2.8's ([crowadv28](boards/crowadv28.md)).
- ESP32-2424S012C: round 1.2", ESP32-C3, 4 MB, GC9A01 at 80 MHz, BGR, SCLK 6, MOSI 7, CS 10, DC 2, no MISO. Backlight is plain
  GPIO 3, no expander, so it is near the CrowPanel 1.28 without the hard part ([gc9a01](boards/gc9a01.md)). CST816S at 0x15
  on SDA 4, SCL 5, RST 1, INT 0.
- WT32-SC01: 3.5" ST7796, FT6336, ESP32 with PSRAM. Pins on its maker's pages.
- LilyGO T-Deck: S3, ST7789 320x240, keyboard. Bruce has boards/lilygo-t-deck.
- ESP32-4827S043R and N: the 4.3" 480x272 with a resistive XPT2046 on its own SPI, or no touch. The C is built
  ([esp4827s043](boards/esp4827s043.md)); the R would be the same pins with another touch.
- Sunton ESP32-8048S043, S050, S070: 800x480 RGB, ST7262, S3, may take the JC8048W550 build ([jc8048w550](boards/jc8048w550.md)).
  Not tried, pins not checked. The 8048S050C's pins are the JC8048W550C's, read from both JSONs. The 8048S070 has
  versions 1.1 and 1.3 with colours on different GPIOs (rzeldent's README).

## A new touch chip, a power chip or an IO expander in the way

- Waveshare ESP32-S3-Touch-LCD-2.8: CST328. rzeldent has esp32-s3touchlcd2p8.json.
- Waveshare ESP32-C5-Touch-LCD-2.8: ST7789, reset and backlight behind a CH32V003 co-processor, CST3530 touch. Found
  while answering an ask for "the C5 board", model not named.
- M5Stack Core2 (AXP192), CoreS3 (AXP2101, AW9523). Bruce has both.
- Waveshare ESP32-S3-Touch-LCD-3.5: TCA9554, AXP2101.
- Elecrow CrowPanel 3.5: ILI9488, 18-bit pixels over SPI, see [ili9488-diy](boards/ili9488-diy.md).
- Elecrow CrowPanel Advance 7.0: ESP32-S3 N16R8, IPS, SC7277, a swappable radio socket; likely a chip of the board's own
  for the backlight as on the Advance 5.0, not checked. Not the plain 7.0 ([crow70](boards/crow70.md)).

## Another bus, esp_lcd has it

- LilyGO T-Display S3: 8-bit parallel, 170x320, very common.
- ESP32-2432S022C: classic ESP32, ST7789 on an 8-bit I80 bus: WR 4, RD 2, DC 16, CS 17, data 15, 13, 12, 14, 27, 25, 33, 32 (the
  JSON names them D8 to D15). Backlight GPIO 0, a strapping pin. CST816S at 0x15 on SDA 21, SCL 22. Speaker 26, SD 5, 23, 18, 19.
- Guition JC3248W535: 3.5" S3, 16 MB, PSRAM `qio_opi`, AXS15231B on QSPI: CLK 47, D0 21, D1 48, D2 40, D3 39, CS 45, TE 38, BL 1,
  40 MHz. Cheap and common. Some batches ignore the window commands: whole frames from PSRAM. The JSON for the N model
  names no touch. FusionEdge runs it: [jc3248w535](boards/jc3248w535.md).
- Guition JC4827W543C: 4.3" S3, PSRAM `qio_opi`, NV3041A on QSPI with the same pins, 32 MHz, BL 1. GT911 at 0x5D on SDA 8, SCL 4,
  RST 38, INT 3. SD 10, 11, 12, 13. The README says module N4R8. One of the boards a "4.3" Guition" ask could mean.
- ESP32-1732S019C and N: S3, 1.9" ST7789 170x320, GT911 or none (README table).

## RGB parallel from PSRAM

- Built: the CrowPanel Advance 5.0, the plain CrowPanel 5.0 and 7.0, the Guition JC8048W550C, the ESP32-4827S043C, the
  Waveshare 7 and 7B (boards/).
- ESP32-4848S040CIY1 and CIY3: 4.0" ST7701 parallel with 3-wire SPI, relays and a mains supply on the board.

## What else the sources hold

- rzeldent's JSON list, 2026-11-04: JC2432W328C, JC3248W535N, JC4827W543C, JC8048W550C, esp32-1732S019C/N, 2424S012C/N, 2432S022C/N,
  2432S024C/N/R, 2432S028R/Rv2/Rv3, 2432S032C/N/R, 3248S035C/R, 4827S043C/N/R, 4848S040CIY1/CIY3, 8048S043C/N/R, 8048S050C/N/R,
  8048S070C/N/R, 8048S550C, s3touchlcd2p8, s3touchlcd4p3, s3touchlcd7.
- Bruce's boards/ folder, 2026-11-04: CYD-2432S028, ES3C28P, ESP-General, ESP32-C5, ESP32-C5-tft, arduino-nesso-n1, elecrow,
  elecrow_advance_s3, lilygo-t-deck, lilygo-t-display-s3, lilygo-t-display-s3-pro, lilygo-t-display-ttgo, lilygo-t-embed-cc1101,
  lilygo-t-hmi, lilygo-t-lora-pager, lilygo-t-watch-s3, m5stack-cardputer, m5stack-core, m5stack-core2, m5stack-cores3,
  m5stack-cplus1_1, m5stack-cplus2, m5stack-dinmeter, m5stack-sticks3, marauder-mini, marauder-touch, nm-cyd-c5, phantom, reaper,
  smoochiee-board, xk404.
- chipguyhere's libraries (github.com/chipguyhere) cover other ESP32-S3 RGB and ESP32-P4 boards; the moving layer suits an
  S3 with an RGB panel or a P4 with a DPI or MIPI-DSI panel, not an SPI or QSPI panel, which keeps its own frame memory.
