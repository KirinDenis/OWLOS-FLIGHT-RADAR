# Boards people asked for

Asked for in the groups after the first posts. Each one needs its screen and touch chips and their
pins, and a board here to try or someone with one who tests a build. Built means a build of its own
on the site; none of the built ones here has been run on that board yet unless it says so.

## An ESP32-C5 board, model not named

- Asked: will it work on the C5 board?
- The chip is no problem: the NM-CYD-C5 below is built for it. ESP-IDF 5.5.0 still calls the C5 a
  preview target (BUILD.BAT passes --preview), the later 5.5 releases the site is built with do not.
- If it is the NM-CYD-C5, that build. Another C5 board found: Waveshare ESP32-C5-Touch-LCD-2.8, an
  ST7789 with its reset and backlight behind a CH32V003 co-processor and a CST3530 touch: not built.
- Missing: which board it is.

## NM-CYD-C5: built 2026-11-03, first run by a tester 2026-11-06

- A 2.8" board in the CYD's form: ESP32-C5-WROOM-1 N16R8 (16 MB flash, 8 MB quad PSRAM), ST7789
  320x240, XPT2046 resistive touch, two USB-C: one through a CH340C to UART0 (TX 11, RX 12), one the
  C5's own USB (D- 13, D+ 14).
- Its maker's repository RockBase-iot/NM-CYD-C5 is gone from GitHub; its forks keep it, with the
  schematic SCH_NM-CYD-C5-v1.0 (for one, github.com/michaelhenry/NM-CYD-C5, Documention/2-sch).
  The same pins in Bruce's port (github.com/BruceDevices/firmware, boards/nm-cyd-c5) and in other
  projects on it.
- Screen on SPI: SCLK 6, MOSI 7, MISO 2, CS 23, DC 24, reset on the chip's own, backlight 25 (high =
  lit). The touch on the same bus: CS 1, its pen interrupt on 3 by the schematic (most ports ask the chip
  instead). The SD card on the same bus too: CS 10, held up by the firmware. BOOT is GPIO 28.
- Built with: no inversion (every source agrees), landscape MADCTL 0xA0, RGB first and BGR as the
  second screen profile (the maker's own demos disagree), SPI at 30 MHz (one project saw stripes at
  40 on this shared bus). The console listens on both USB ports.
- First run on a real board, 0.6 from the site: the screen stopped with "not enough memory for the
  screen", about 35 KB of the chip's own RAM left and the 8 MB of PSRAM nearly empty. For 0.7 the map's
  picture, TLS and the WiFi's buffers went to the PSRAM. That build has not run on one yet.
- Not driven yet: the WS2812 LED on GPIO 27 (GRB), the AHT20 sensor on I2C 9/8, the speaker on 26.
- The one who asked has the board and offered to test a build. To learn from it: RGB or BGR, which
  landscape is upright, whether the touch's pen interrupt is there, whether 30 MHz stays clean.

## LCDWIKI ES3C28P: built 2026-11-03, not run on one yet

- In the asker's words: same form factor as the CYD, with more RAM. They flashed a basic ESP32-2432S
  from a Chromebook with no trouble, then this one could not be flashed: the site had no S3 build, and
  ESP Web Tools turns down a chip no build is for.
- 2.8", ESP32-S3R8 (8 MB octal PSRAM) with 16 MB flash, ILI9341V 240x320 IPS, FT6336G capacitive touch.
- Pins, from LCDWIKI's pin table and schematic (https://www.lcdwiki.com/2.8inch_ESP32-S3_Display) and
  the same in Bruce's port (boards/ES3C28P, said to be run on one): LCD CS 10, DC 46, SCLK 12, MOSI 11,
  MISO 13, backlight 45 (high = lit), reset on the chip's own; touch at 0x38 on I2C SDA 16, SCL 15,
  INT 17, RST 18. BOOT is GPIO 0.
- Its USB-C is the S3's own USB, no bridge chip: the console listens there too now.
- Built with: BGR and colours inverted (LCDWIKI's own start-up and both ports agree, the CYD's v2),
  landscape MADCTL 0x28 (Bruce's), the CYD's three profiles behind BOOT in case, SPI at 40 MHz.
- Not driven yet: the WS2812 LED on GPIO 42 (GRB), the ES8311 codec and the speaker (amp enable on 1,
  low = on), the SD card, the battery on 9.
- The one who asked was asked whether they would try a test build.

## Elecrow CrowPanel Advance 2.8": built 2026-11-04, not run on one yet

- Asked for twice, the second time with its Amazon page: "ELECROW ESP32 Display 320x240, 2.8" HMI
  Advanced ESP32-S3 IPS Touch Display, Support AI Speech Interaction". Elecrow's own name: CrowPanel
  Advance 2.8 HMI ESP32 AI Display. Not the plain CrowPanel 2.8 HMI (classic ESP32, ILI9341V, XPT2046).
- ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal PSRAM), ST7789 240x320 IPS, FT6336U capacitive touch,
  two USB-C: one through a CH340K to UART0, one the S3's own USB.
- Pins from Elecrow's repository (github.com/Elecrow-RD/CrowPanel-Advance-2.8-HMI-ESP32-AI-Display-320x240):
  its working LovyanGFX setup (example/V1.0/Arduino_Code/lesson-03) and the nets of the V1.2
  schematic agree. Screen on SPI: SCLK 42, MOSI 39, no MISO, CS 40, DC 41, reset by the board's own
  circuit, backlight 38 (high = lit). Touch at 0x38 on I2C SDA 15, SCL 16, INT 47, RST 48. BOOT is
  GPIO 0. The same pins on V1.0, V1.1 (new amplifier, its mute on 21) and V1.2 (new buttons).
- Built with: colours inverted and BGR (Elecrow's invert = true and rgb_order = false, which is
  LovyanGFX's BGR bit), RGB as the second screen profile; landscape MADCTL 0xA0 (LovyanGFX's rotation
  3, the one Elecrow's demos run); SPI at 40 MHz (Elecrow runs 80). The S3 LCD boards' sdkconfig.
- Doubts: a GT911 at 0x5D is started in one of Elecrow's sketches, left over from another board: the
  touch there is read through LovyanGFX's FT5x06 at 0x38, and the wiki says 0x38. If a board has no
  touch at all, that is the first thing to look at. GPIO 14 is a TFT_PWR net on the schematic, Elecrow's
  examples never drive it, so neither does this build.
- Not driven: no user LED on it; the speaker (NS4168, I2S 11 12 13), the microphone (9 and 10 behind a
  switch on 45), the buzzer on 8, the SD card (4 5 6 7), the wireless module socket.
- To learn from whoever tries it: colours right as it starts, which landscape is upright, the touch.

## Elegoo ESP32-32E 2.8" (LCDWIKI E32R28T)

- Asked: sold on Amazon as "ESP32 ILI9341", can it take the 2.8" ESP32-2432S028R build?
- It should, nothing to build: from https://www.lcdwiki.com/2.8inch_ESP32-32E_Display the screen
  (ILI9341V, CS 15, DC 2, SCLK 14, MOSI 13, backlight 21, reset on EN) and the touch (XPT2046 on its own
  pins, CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36) are on exactly the 2.8" CYD's pins. Colours and
  mirroring: BOOT in the first seconds goes through the screen variants. Not run on one here.
- One difference: the RGB LED's red is on IO22, not IO4 (green 16, blue 17 the same). IO4 on this board
  enables the speaker's amplifier, low is on. Now a setting: RED ON in SET's LED tab, `led_red` on the
  site, GPIO 4 by default.
- LCDWIKI's 3.5" twin, E32R35T, is the same story: every pin as the 3.5" build, red on IO22. The 3.5"
  here turned out to be one: red lit with GPIO 22 alone held low (2026-11-03).

## "Bruce CYD 2432S028" kits on AliExpress

- Asked 2026-11-04: a listing "ESP32 Bruce CYD 2432S028 ... NFC PN532 GNSS ATGM336H". It reads as the
  plain 2.8" CYD sold for the Bruce firmware, with add-on modules: the 2.8" CYD build, nothing to build.
  Not run on one here, and the listing does not say R (resistive) or C (capacitive): the build is for R.
- Bruce's own setup (boards/CYD-2432S028) has the PN532 on I2C 27/22, which the radar leaves alone, and
  the GPS on TX/RX, GPIO 1 and 3: the UART of the USB port. A GPS plugged in there talks over the
  flashing and over step 2, so it comes off for both. The radar uses neither module.

## A bigger screen on an ESP32-S3: the Guition built, the ILI9488 not

- Asked 2026-11-04 in the group: can it run on an ESP32-S3 with a larger TFT? Named: "ILI9488 TFT's or a
  Guition ESP32-S3 JC8048W550", and "it works great, just needs a bigger display size option". Which
  one the asker owns, and whether they would test, is not known yet.
- ILI9488, 480x320: the 3.5" CYD's picture size, so the screens are drawn already. The asker has one
  wired to an ESP32-S3 and will rewire it if we want other pins. From their drawing, a small picture,
  so not sure of every digit: 3.3V, GND, CS 10, RST 15, D/C 9, SDI 11, SCK 12, BL 8; XPT2046 touch TCK 14,
  TCS 18, TDI 17, TDO 47; SDO and PEN not connected; the SD card's wires go off the picture.
- New in the firmware for it: 18-bit pixels over SPI (3 bytes a pixel, not 2), the screen's reset on a
  GPIO (lcd_spi.c only sends the software reset now), the XPT2046 with no pen interrupt pin. Still to
  learn: the digits confirmed, which S3 board (flash, PSRAM), 3.5" or 4.0", whether they would test.
- Guition JC8048W550, 5", 800x480: built 2026-11-05, experimental, not run on one yet. ESP32-S3 N16R8,
  ST7262, CH340. The same code as the CrowPanel Advance 5.0 below, the pins from one source so far,
  JC8048W550C.json in rzeldent/platformio-espressif32-sunton, the same as its ESP32-8048S050C: HSYNC 39,
  VSYNC 41, DE 40, PCLK 42 at 16 MHz, porches 8, pulse 4, back 8 both ways; data lines lowest first 8 3
  46 9 1, 5 6 7 15 16 4, 45 48 47 21 14; backlight 2; GT911 on I2C SDA 19, SCL 20, reset 38, INT 18.
  That file names the low five red, ESPHome's page for the plain CrowPanel 5.0 has those same pins blue;
  the bus's low five are blue, so they go in as listed, with BGR behind BOOT in case. SDA and SCL are the
  S3's own USB pins, so the console stays off that USB here. Missing: a second source. One manual
  lists a version without touch.

## Guition 4.3": asked 2026-11-07; ESP32-4827S043 named 2026-11-08, its C built 2026-11-09

- Asked: will one of the binaries work with the older Guition 4.3" ESP32 modules? No 4.3" build exists.
- An 800x480 RGB one (Sunton ESP32-8048S043 and its kin) may take the JC8048W550 build, not tried and its
  pins not checked. The 480x272 JC4827W543 (QSPI NV3041A) needs a new build. Missing: the exact model.
- Asked 2026-11-08, maybe the same person: "my display is not one you have firmware for... The display
  is an ESP32-4827S043". Tried a couple of builds: no picture; the 5" one shows a picture that "spills
  over", as an 800x480 frame on a 480x272 glass would.
- The ESP32-4827S043: ESP32-S3, 4.3" 480x272 on the RGB bus, so lcd_rgb.c as on the 5" boards with its
  own pins and timing. Sold as C (capacitive, GT911), R (resistive, XPT2046) and N (no touch). New for
  the firmware: 272 rows, fewer than the 3.5" layout's 320, so the pages go smaller, not bigger as on
  the 5". Pins not looked up yet. Missing: which letter, flash and PSRAM, whether they would test.
- Answered 2026-11-09: no letter printed on the board, but it is a touch screen, so C or R. Would test a
  build. Has two: one ran openHASP before, the other is untouched. Asked for a photo of the back (touch
  chip, module label). Missing: C or R, flash and PSRAM.
- Then: "I believe the version I have is a capacitive type", not 100% sure.
- BUILT 2026-11-09 for the C, experimental, not run on one: esp4827s043, RADAR_DISPLAY 13. Pins and
  timing from rzeldent/platformio-espressif32-sunton esp32-4827S043C.json: the data, sync and touch
  pins are the JC8048W550's, which is why that build showed a picture on this board; 8 MHz, HSYNC
  4/43/8 and VSYNC 4/12/8 (pulse, back, front). That file also says N16R8, and the 5" build (octal
  PSRAM, frame in it) did show its picture here, so the S3 RGB settings stay as they are. A layout of
  its own at 480x272, one to one: every size switch in core/ asks GFX_H >= 320, so it takes the 2.8"'s
  letters and rows with a wider panel (PC preview shots/esp4827s043, all pages fit). Bounce buffers of 8
  rows: 272 does not divide by the 5"'s 10, and esp_lcd refuses a frame that does not.
- To learn from the owner: a picture at all; colours (BOOT swaps RGB and BGR); the touch alignment
  and taps landing; shake while the map loads. If touch is dead it may be the R: then the XPT2046 on
  its own SPI is the next build.

## Elecrow CrowPanel Advance 5.0": built 2026-11-05, experimental, runs on a viewer's V1.3

- Asked 2026-11-05: "Any chance for a 5" board like an elecrow advanced 5"?" The second ask for 800x480,
  after the Guition above. Whether the asker has one, and would test, is not known yet.
- Elecrow's name: CrowPanel Advance 5.0-HMI ESP32 AI Display, DIS02050A. Not the plain CrowPanel 5.0
  (DIS07050H): that one is `crow50`, built from the pins a P2000 firmware already runs.
- ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal PSRAM), 800x480 IPS on a 16-bit RGB bus, ST7262, GT911
  capacitive touch at 0x5D, USB-C through a CH340K to UART0. BOOT is GPIO 0.
- Pins from Elecrow's repository
  (github.com/Elecrow-RD/CrowPanel-Advance-5-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480), its
  LovyanGFX setups for V1.0 and V1.2, the same but for the clock. Blue 21 47 48 45 38, green 9 10 11 12
  13 14, red 7 17 18 3 46, DE 42, VSYNC 41, HSYNC 40, PCLK 39. Porches 8, pulse 4, back 8 both ways, the
  clock idle high. 21 MHz on V1.0, 16 MHz in the V1.2 and V1.3 examples, Arduino and IDF. Touch on I2C
  SDA 15, SCL 16, polled; its INT net is on GPIO 1. The V1.3 folder's hardware guide agrees and says a
  model compiled it, so the code is the source here, not the guide.
- The backlight and the touch's reset are not on the S3's pins, and it goes by board version, printed
  on the board. V1.0: a PCA9557. V1.1: an STC8H1K28 at 0x30 on the same I2C, 0x05 off to 0x10 brightest,
  and 0x10 must go first. V1.2 and V1.3: the same chip, 0 brightest to 245 off. A guess to try: 0x10
  lights both V1.1 and V1.2 on. When 0x30 or 0x5D does not answer, Elecrow's examples send 250 to 0x30,
  hold GPIO 1 low for 120 ms, let it go and wait 100 ms.
- Built with: firmware/main/lcd_rgb.c, a frame of 768,000 bytes in PSRAM with bounce buffers as in
  Elecrow's IDF example, 16 MHz, RGB and BGR behind BOOT. V1.0 is told by its PCA9557 answering at 0x18;
  from V1.1 the chip at 0x30 gets 0x19 and 250 to wake the touch when it is silent, five times at most,
  and 0x10 for the light, so there is no dimming. Elecrow's demos wait for ever, this goes on and says
  so in the log.
- The screens: the 3.5"'s layout half as big again (config.h GFX_SCALED, layout 534x320, glass 800x480).
  core/gfx.c turns the layout's numbers into the glass's pixels, the disc and the map are drawn in the
  glass's own, the fonts are baked half as big again (core/font_glass_*.c). tools/preview takes it:
  BUILD.BAT crowadv50.
- Doubt: on this bus the picture is read from PSRAM all the time, so WiFi and flash writes (the map's
  tiles, the settings) can shake it. Whether they do here shows only on a real board. Elecrow's IDF
  example runs PSRAM at 80 MHz; the repository also carries an Arduino build with it at 120.
- No board here, and nothing here has this bus: the picture was seen in tools/preview, the driver only
  compiled. Once it runs, the other 800x480 boards below are mostly pins.
- Speed is a guess too: a frame has 2.5 times the pixels of the 3.5"'s. The second core draws in the radar
  look only, as on the 3.5"; the maps may want it too.
- Not driven: the speaker, the microphone and the SD card (GPIO 4 5 6 19 20, shared, picked by a switch
  on the board), the buzzer, the RTC, the wireless socket.
- The asker has one and sent a photo of its back, 2026-11-05: SKU DIS02050A, V1.3, the newest version, one
  USB-C marked USB-5V-IN. Its silkscreen agrees with the build: I2C on 15 and 16 (the RTC at 0x51 on
  them), UART0 on 43 and 44. The switch S1 S0 picks the microphone, speaker, wireless or SD card only.
- First run on it, 2026-11-05: the picture and the touch work, 3 fps and 280 ms a frame on the radar
  (disc 151, panel 39, packing into the frame 52), 14 fps on SET. The second core never started: no
  room in the RAM for its strip. Opening the WiFi page restarted the board: the event task's stack ran
  into the event queue's lock (firmware/main/net.c says how). That scan also found 0 networks, reason
  not known; the build after it logs a failed scan and the free RAM, and keeps the WiFi's buffers in
  the PSRAM.
- To learn from the asker: does the list of networks come now, does it join, are the colours right, does
  the picture shake while the map loads.

## M5Stack Tab5: built 2026-11-08, experimental, runs on its author's

- Not asked for: Paris Pitts brought it in, pull request #7, and runs it on a Tab5 with the ST7121
  panel (board V1.0): the WiFi, planes, the map and the touch.
- ESP32-P4 (16 MB flash, 32 MB PSRAM), 720x1280 IPS on MIPI-DSI, the WiFi on an ESP32-C6 over SDIO with
  ESP-Hosted. Espressif's board package (espressif/m5stack_tab5) tells the three panels apart at power
  on: ILI9881C with a GT911, ST7123, ST7121 with its own touch. The other two not seen.
- No BOOT reaches the P4; the console is the chip's own USB. The fonts are the 5" boards' 3/2 ones, a 9/4
  bake is to come.
- To learn from anyone with one: which panel it has, does the flash from the site go through, does the
  WiFi join.

## Elecrow CrowPanel 7": asked 2026-11-09, built 2026-11-10 (crow70, 0.11), not run yet

- Asked in the group: "ELECROW CrowPanel 7" ESP32 Display Touchscreen 800x480 ... ESP32-S3 240MHz,
  800x480 IPS Touchscreen", would it be suitable. The asker has one.
- Elecrow sells two 7" boards, and a shop title does not tell them apart. The plain CrowPanel 7.0:
  ESP32-S3-WROOM-1 N4R8, a TN panel by Elecrow's spec (many shops call it IPS), its pins on the S3. The
  CrowPanel Advance 7.0: N16R8, IPS, SC7277, a swappable radio socket; likely a chip for the backlight
  as on the Advance 5.0, not checked.
- 800x480, so the 5" boards' layout as it is; a new build for the pins and the clocks. 4 MB of flash is
  enough: the S3 RGB builds use partitions.csv. Missing: which one (the back: SKU, version), would they test.
- The asker's photo of the back, 2026-11-09: "ESP32 Display-7 inch, SKU:DIS08070H", V3.0, so the plain
  7.0, not the Advance. The backlight on GPIO 2 by Elecrow's wiki. On V3.0 a PCA9557 at 0x18 holds the
  GT911's reset and INT, as on the Advance 5.0 V1.0 (lcd_rgb.c has that code); an ESPHome draft says
  driving it broke the touch there, Elecrow's own examples do drive it. I2C on 19 and 20, the S3's USB
  pins, as on the Guition. RGB pins and clocks: from Elecrow's 7.0 examples and a second source.
- 2026-11-10 the asker flashed the CrowPanel Advance 5" build on it: the WiFi set up and the planes on
  the site, the screen dark. As expected: other pins for the screen and the backlight. They will test
  a build of its own.
- The build, crow70: the pins from Elecrow's V3.0 examples (crowpanel-esp32-7.0.ino, Course/LVGL_Arduino7.0)
  and its V3.0 hardware guide; ESPHome's setups have the same sync pins. Rows and frames 48/40/40 and
  31/13/1 in both examples, the clock 15 MHz in one and 24 in the other, 16 here. The PCA9557 is left
  alone unless the touch is silent, then the touch's reset as Elecrow does it. GPIO 0 is the panel's
  clock and BOOT: no button read.

## Others on sale, nobody asked yet

Looked up 2026-11-03 to see what else people build things like this on. Pins are in the board JSONs
of github.com/rzeldent/platformio-espressif32-sunton, in Bruce's boards/ and the makers' wikis;
check each against a second source before a build. From the easiest port to the hardest:

- Pins only, chips the firmware knows: Guition JC2432W328C (the capacitive 2.8" CYD, ESP32, ST7789 or
  ILI9341 by batch, CST816/CST820 at 0x15 on SDA 33 SCL 32, backlight 27, looks the most common now);
  ESP32-2432S024 C/R/N (2.4", ILI9341, CST820 or XPT2046); ESP32-2432S032 C/R/N (3.2", ST7789, GT911 or
  XPT2046); Elecrow CrowPanel 2.8 and 2.4 ESP32 HMI, the plain ones (ILI9341V, XPT2046 on the LCD
  bus, CS 33); the CrowPanel Advance 2.4 (S3, Elecrow's examples are shared with the 2.8's);
  ESP32-2424S012C (round, ESP32-C3, GC9A01 and CST816, near the CrowPanel 1.28); WT32-SC01 (3.5"
  ST7796, FT6336, ESP32 with PSRAM); LilyGO T-Deck (S3, ST7789 320x240, keyboard).
- A new touch chip, a power chip or an IO expander in the way: Waveshare ESP32-S3-Touch-LCD-2.8
  (CST328), Waveshare ESP32-C5-Touch-LCD-2.8 (CST3530, reset and backlight on a CH32V003), M5Stack
  Core2 (AXP192) and CoreS3 (AXP2101, AW9523), Waveshare ESP32-S3-Touch-LCD-3.5 (TCA9554, AXP2101),
  Elecrow CrowPanel 3.5 (ILI9488 takes 18-bit pixels over SPI).
- Another bus, esp_lcd has it: LilyGO T-Display S3 (8-bit parallel, 170x320, very common),
  ESP32-2432S022C (8-bit parallel), Guition JC3248W535 (3.5" QSPI AXS15231B, cheap and common, some
  batches ignore the window commands: whole frames from PSRAM), JC4827W543 (QSPI NV3041A).
- RGB parallel 800x480 from PSRAM: Sunton ESP32-8048S043/050/070, JC8048W550C. 480x272: ESP32-4827S043
  (its C built 2026-11-09, above).
