---
name: esp32-lcd-touch
description: How an ESP32 screen firmware on ESP-IDF 5.x in plain C drives its panel, touch, buttons and LEDs, with start-up tables and code from boards that ran. Use when bringing up or debugging an SPI LCD on esp_lcd panel IO (ILI9341, ST7789, ST7796, GC9A01, ILI9486, ILI9488), a QSPI AXS15231B, an ESP32-S3 RGB parallel panel (800x480, 1024x600, 480x272, ST7262, bounce buffers, frame in PSRAM), or an ESP32-P4 MIPI-DSI panel (M5Stack Tab5, Guition JC8012, JD9365, PPA rotation); strips and DMA, a hang in spi_device_acquire_bus, SPI clock, MADCTL, landscape, a mirrored, upside-down, negative or red-blue swapped picture, inversion, screen profiles for CYD v1 v2 v3; backlight on LEDC or an I2C expander; touch (XPT2046, GT911, FT6336, CST816, GSL3680), 5 cross and 9 point calibration, swapped axes; SSD1683 e-paper, 4 greys, full and partial refresh, grey debt; I2C hangs, BOOT button, keys, RGB LED, WS2812. Not for what gets drawn (fonts, layout, animation), flashing or settings.
---

# ESP32 screens and touch on ESP-IDF

What the OWLOS screen firmwares learned about the glass: OWLOS Flight Radar (the newest, 19 builds on SPI, RGB,
MIPI-DSI and e-paper boards), OWLOS WiFi Radar, OWLOS Thermo Relay, OWLOS Air Quality UI, and the display options of
FusionEdge (an internet radio firmware on some of the same boards). ESP-IDF 5.5, plain C, no Arduino, no LVGL. The
SPI, RGB and e-paper boards download nothing at build; the P4 boards fetch Espressif's components, and three boards
use chipguyhere's vendored libraries.

This file is the general part and the index. Code and per-chip details are in `references/`. Pins and whole boards
are in the sibling skill esp32-boards ([its SKILL.md](../esp32-boards/SKILL.md), one file per board under
`references/boards/`).

## How sure each fact is

Every fact carries one of these words:

- **measured**: ran on a board here, seen on the glass or in the log. "Measured, a tester's board" or "a
  contributor's board" when someone else ran it and reported what they saw.
- **built**: compiles and ships in a release, never run on that board.
- **docs**: read from a datasheet, a maker's files, another project's source, or ESP-IDF's sources (v5.5).
- **reasoned**: worked out, not tested.

Where the newest source (OWLOS Flight Radar) differs from older notes, the file says what changed.

## Index

Buses:

- [references/bus-spi-strips.md](references/bus-spi-strips.md): SPI panels with their own frame memory: why strips,
  the whole `lcd_spi.c` without its tables, the done gate and the hang it cures, RAMWRC, packing to 565, SPI clock
  per board and IOMUX, the second core (pairs and halves), an SD card or touch on the same bus.
- [references/bus-rgb.md](references/bus-rgb.md): the ESP32-S3 RGB parallel bus: `lcd_rgb.c`, bounce buffers,
  colour order by pins, timing per board (CrowPanel Advance 5.0, JC8048W550, ESP32-4827S043, CrowPanel 7.0 and 5.0,
  Waveshare 7 and 7B), the CrowPanel Advance's board chip, chipguyhere's 7/7B library, sdkconfig.
- [references/bus-mipi-dsi.md](references/bus-mipi-dsi.md): the ESP32-P4: the Tab5 through Espressif's board package
  (`lcd_dsi.c`, the PPA, its touch), the JC8012 through chipguyhere's library (JD9365 bring-up, raster band, GDMA,
  XIP from PSRAM).

Panels, one file per controller (start-up table, MADCTL per board, quirks, how sure):

- SPI: [ili9341](references/panels/ili9341.md), [st7789](references/panels/st7789.md),
  [st7796](references/panels/st7796.md), [gc9a01](references/panels/gc9a01.md),
  [ili9486](references/panels/ili9486.md) (raw SPI through shift registers, both drivers),
  [ili9488](references/panels/ili9488.md) (18-bit pixels, not built).
- QSPI: [axs15231b](references/panels/axs15231b.md).
- RGB: [st7262](references/panels/st7262.md). MIPI-DSI: [jd9365](references/panels/jd9365.md),
  [st7121](references/panels/st7121.md).

Touch, one file per controller, and the alignment:

- [xpt2046](references/touch/xpt2046.md), [gt911](references/touch/gt911.md), [ft6336](references/touch/ft6336.md),
  [cst816](references/touch/cst816.md), [gsl3680](references/touch/gsl3680.md),
  [axs15231b](references/touch/axs15231b.md).
- [references/touch-alignment.md](references/touch-alignment.md): when to sample, the 5 cross least squares, the 9
  point quadratic, the stored map's check, flip, boards that skip the crosses.

E-paper:

- [references/epaper/ssd1683.md](references/epaper/ssd1683.md): the shared driver, the refresh decision, grey debt,
  the LUT format, the grey test.
- [crowpanel-42](references/epaper/crowpanel-42.md) (4.2", one SSD1683, its greys moved),
  [crowpanel-579](references/epaper/crowpanel-579.md) (5.79", two SSD1683, the broken glass),
  [paper-screen](references/epaper/paper-screen.md) (what the radar prints and when: inks, soft edges, the key
  strip, gathered keys, holding a frame).

Other parts: [references/i2c.md](references/i2c.md) (the lazy bus, the classic ESP32 NACK trap, bit-banged I2C, the
RTC guard), [references/buttons-keys.md](references/buttons-keys.md), [references/leds.md](references/leds.md).

## 1. The shape of a screen firmware

- One face for every screen, `display.h`: `disp_init`, `disp_backlight`, `disp_strip`, `disp_frame_end`,
  `disp_frame_ms`, `disp_full_next`, `disp_lost`, `disp_profiles`, `disp_profile`, `disp_profile_name`,
  `disp_flip`, `disp_wait_idle`; for the second core `disp_pack_under`, `disp_strip2`, `disp_rows`; on the moving
  layer boards `disp_bg_ready`, `disp_bg_begin`, `disp_frame_us`, `disp_log`; on the Tab5 `disp_board_up`; on
  e-paper `disp_grey_test`, `disp_lut_poke`.
- The screen task draws a frame strip by strip and hands each strip over. An SPI LCD sends it; an RGB or DSI panel
  copies it into its frame in PSRAM; an e-paper collects it and refreshes in `disp_frame_end`.
- One `board.h`, one block per board, picked by a compile define (`-DRADAR_DISPLAY=n`). Every driver file is
  always in `SRCS` and compiles to nothing unless its switch is set: `DISP_LCD_SPI`, `DISP_LCD_RGB`,
  `DISP_ILI9486`, `DISP_EPD`, `DISP_LCD_DSI`, `DISP_WAVE7`, `DISP_JC8012`; touch `TOUCH_XPT_BITBANG`,
  `TOUCH_XPT_SPI`, `TOUCH_CST816`, `TOUCH_FT6336`, `TOUCH_GT911`, `TOUCH_GT911_TOO`, `TOUCH_BSP`, `TOUCH_JC8012`.
  One tree builds all boards, and a build folder per board (`build_<name>`, its own sdkconfig) keeps switching
  cheap. Each block says where each fact came from and "NOT run on one here" until it has.
- `REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_rmt esp_timer
  nvs_flash` and the rest; `esp_driver_ppa` for the P4 only. A `main/idf_component.yml` exists only for the P4
  (each dependency under `if: "target == esp32p4"`); nothing is fetched for the other chips. Changed since the
  first version of these notes, which had no component file at all. Why no panel component on SPI: the panel
  commands are 20 lines of table each, a component would add a download to every CI build and hide the table you
  need to change.
- `CONFIG_FREERTOS_HZ=1000` (a 5 ms wait really is 5 ms), `CONFIG_COMPILER_OPTIMIZATION_PERF=y`, task watchdog
  10 s with panic and a core dump: that is how the SPI hang was found. The big RGB and P4 boards keep the core
  dump in flash.
- Name the touch functions `tp_*`: `touch_init` and `touch_read` clash with ESP-IDF's librtc.a (the ESP32's own
  touch pads) at link time.
- The layout and the glass: the 800x480, 1024x600 and 1280-wide boards draw a 320-row layout scaled (3/2, 15/8,
  9/4, 5/2); the drivers work in the glass's pixels (`GFX_PW`, `GFX_PH`). How scaling draws is esp32-ui-drawing's
  subject.
- Pixel byte order: SPI sends the high byte first, so `gfx_pack565` swaps; a frame the chip reads out of memory
  (RGB, DSI) wants the native order (`RADAR_RGB_BUS 1`).

## 2. The buses, in one screen

- **SPI with its own frame memory** (ILI9341, ST7789, ST7796, GC9A01): no PSRAM needed. Strips of 6 to 24 rows,
  two DMA buffers in internal RAM, one in flight while the next is drawn, a semaphore given by the done callback
  before anything else enters esp_lcd. 320x240 at 40 MHz is 30.7 ms on the wire, 480x320 61 ms.
- **SPI through shift registers** (the RPi LCD's ILI9486): every byte padded to 16 bits with its own CS pulse,
  raw `spi_master`, 20 MHz at most. **QSPI** (AXS15231B): four data lines, commands with a 0x02 prefix, a whole
  frame from PSRAM, paced.
- **S3 RGB parallel** (800x480, 1024x600, 480x272): no commands, no window; the chip reads a whole RGB565 frame
  from PSRAM all the time through bounce buffers in internal RAM; a strip is a copy into the frame; colour order is
  which pins carry blue.
- **P4 MIPI-DSI** (720x1280 and 800x1280 standing): a DPI frame in PSRAM, the strips turned into it by the PPA,
  the cache written back. **E-paper SSD1683**: bit-banged, 2 bits a pixel, a refresh of seconds (section 10).

## 3. MADCTL and colour order

Command 0x36, one byte. A panel is portrait in its RAM; landscape is an exchange of rows and columns.

- bit 7, 0x80 **MY** row order. bit 6, 0x40 **MX** column order. bit 5, 0x20 **MV** rows and columns exchanged.
  bit 3, 0x08 **BGR** colour order.
- bit 4, 0x10 **ML** and bit 2, 0x04 **MH**: the order the glass is refreshed in. They do not move a pixel. Left 0
  on every board here [docs].

How to work out landscape:

1. Landscape means MV set. Four values are left: MX and MY each on or off.
2. Two of the four are the two landscapes, 180 degrees apart. The other two are the same pictures mirrored. Which
   two depends on how the glass is wired to the chip, not on the chip's name.
3. MX alone or MY alone toggled is a mirror. Both toggled is a 180 degree turn. So `m ^ 0xC0` always gives the
   other landscape, and `m ^ 0x40` always gives a mirror of what you have.
4. BGR is independent: `m ^ 0x08`.

Two families of glass were met; the bits above 0x08 tell them, and 0x08 is the colour order on top:

- **MX equal to MY**, landscapes 0x20 and 0xE0; with BGR 0x28 and 0xE8. The 2.8" CYD v1 0xE8 [measured], v2 the
  same [built]; the 3.5" ST7796 0xE8 [measured]; the RPi LCD ILI9486 0x28 [measured]; the ES3C28P's ILI9341V 0x28
  [built]. TFT_eSPI's ILI9488 has the same pair [docs].
- **MX different from MY**, landscapes 0x60 and 0xA0; with BGR 0x68 and 0xA8. The ST7789s: CYD v3, NM-CYD-C5 and
  CrowPanel Advance 2.8" at 0xA0 (MY|MV) [built], the ZLX-ESP32-2 at 0x60, BGR first, so 0x68 [built]. The round
  GC9A01 is BGR, 0x68 (MV|MX|BGR); 0x28 came out mirrored [measured].

So a v3 CYD against v1 lost BGR and MX: 0xE8 to 0xA0. A new SPI panel is 0x28, 0xE8, 0x68 or 0xA8 with BGR, or
those without 0x08.

Other buses: on the RGB bus there is no MADCTL; colour order is a swap of the two 5-line groups and a flip is the
frame written turned. On the JC8012 the JD9365 gets MADCTL 0xC0 once as its base scan and the PPA turns the picture.

**Flip by XOR 0xC0.** A board standing on its head gets the profile's own MADCTL with MX and MY toggled, read out of
the running profile, never a global: v3's 0xA0 is not `LCD_MADCTL`. The touch map turns with it, so no new crosses
are needed ([touch-alignment](references/touch-alignment.md)).

## 4. Inversion

Not in MADCTL: 0x20 off, 0x21 on. Which one a panel wants is a property of the panel on that board, and it differs
on the same chip: ILI9341 v1 off [measured], v2 on [built]; ILI9341V on the ES3C28P on [built]; ST7789 on for IPS
(CrowPanel Advance, `LCD_INVERT 1`, and the CYD v3), off on the NM-CYD-C5 and the ZLX-ESP32-2 [built]; GC9A01 on
[measured]; ST7796 off [measured]; ILI9486 (A) off [measured]. The AXS15231B in FusionEdge: INVOFF sent, the pixels
XORed in software [docs]. FusionEdge keeps inversion as a stored user setting on every panel [docs]. The RGB and DSI
panels here take no inversion command.

## 5. Telling from the picture what is wrong

Change one thing at a time, in this order: light, any picture, way round, colours, speed.

- **Backlight on, flat grey or white, nothing ever drawn.** The panel took no command: pins, DC, CS, mode. On the
  RPi LCD: 8-bit commands; esp_lcd leaves that panel grey, it needs padded bytes.
- **Black, no backlight.** PWM pin, panel power pin, the expander or board chip not started, or another board's
  build: an owner's CrowPanel 7.0 on the Advance 5.0 build had WiFi and planes and a dark screen.
- **Dark, and the log names another build.** The Waveshare 7/7B library knows its board and refuses the other's
  glass. A dark screen that hangs after the WiFi starts there: a build folder's stale sdkconfig ([bus-rgb](references/bus-rgb.md)).
- **The picture spills over the glass.** A build for a bigger panel: the 5" build on a 480x272 board.
- **Text reads backwards.** Mirrored: `MADCTL ^ 0x40`. GC9A01 at 0x28 did this.
- **Upside down, text fine when you turn the board.** `MADCTL ^ 0xC0`.
- **Red and blue swapped.** `MADCTL ^ 0x08` on SPI, the other pin order on RGB. Green stays right, which is how to
  tell it from negative.
- **Negative.** Black shows white, dark themes look pale, every colour is its complement. Swap 0x20 and 0x21.
- **Light blue on white and upside down.** All three at once: a CYD v3 running the v1 start-up (a viewer's report,
  the reason v3 exists in the build).
- **Noise.** The power and gamma values of another revision: a panel on the wrong start-up shows noise, negative
  colours or an upside-down picture (WiFi Radar's README).
- **Snow.** Clock too fast for the wiring: 80 to 40 (a board.h note; the round here never did it).
- **Stripes on a shared bus.** The same, with more on the lines: 40 to 30 or 20 (another project's report on the
  NM-CYD-C5).
- **Slices moving sideways, rows bleeding down** on a fed panel (RGB, DSI): the fill late for its chunk
  ([bus-mipi-dsi](references/bus-mipi-dsi.md)).
- **Sheared, or the picture fills a part of the glass.** MV wrong for the window, or the window bigger than the
  panel [reasoned, not seen here].
- **All colours speckled wrong.** The two bytes of a pixel not swapped, or swapped where they should not be
  [reasoned, not seen here].
- **Picture frozen, logs still running.** The panel lost its state: section 7 [reasoned].
- **Picture frozen, logs stopped.** The task hung: a watchdog reboot and core dump follow within 10 s if the
  watchdog is set up. "It hung" twice here was the USB cable falling out.

A good test frame: words, a red square top left, green top right, blue bottom left, white on black. Read the boot
log line: chip, size, clock, strip rows.

## 6. Screen profiles

Why: the same name is sold with different screens on the same pins, and the board cannot tell. "ESP32-2432S028R" is
three boards: v1 micro USB ILI9341, v2 USB-C ILI9341 with other power, gamma and inversion, v3 ST7789. v1 and v2 are
the same chip, so no ID read could split them, and the 2.8" never reads the panel at all: its MISO stays off
strapping pin 12. So all start-ups are in one build, the person picks, and it is remembered. The code is in
[bus-spi-strips](references/bus-spi-strips.md) (`PROFILE_LIST`, `run_profile`, `disp_profile`).

- `LCD_PROFILES` and `LCD_ORDER` in board.h: the CYD and the ES3C28P three (`{ 1, 0, 2 }`, v2 first); the ST7789
  boards RGB and BGR (NM-CYD-C5 `{ 0, 1 }`, CrowPanel Advance 2.8" and ZLX-ESP32-2 `{ 1, 0 }`), because the makers'
  own demos disagree; the RGB-bus boards RGB and BGR as a pin swap (the panel is made again, between frames).
- Every table that can follow another starts with software reset 0x01: no pin is needed to change.
- The list order is frozen because boards save the index: v1 stays 0. The order BOOT walks is a separate list.
  Flight Radar starts as v2 because most boards sold are v2; WiFi Radar started as v1.
- Saved in NVS as a u8, key `screen`. Read before `disp_init`; a value out of range falls back to `ORDER[0]`.
- A short BOOT press moves on when `now < 8000 ms` after power on, or at any time on the touch alignment page. That
  page is the first thing after flashing, so it is where someone with the wrong start-up stands. A long press there
  still skips the alignment.
- **The card on the screen**: for those 8 seconds a box says `SCREEN WRONG? PRESS BOOT`, under it `NOW ILI9341 V2`,
  and a bar shrinks with the time left. Any other card waits until it is gone. A toast `SCREEN <name>` confirms a
  change. The alignment page carries the same two lines. Boards without a readable BOOT get it from the site only.
- `OWLOS SET {"screen":n}` over the console stays for scripts, but the flasher site has no select for it: it was
  confusing beside the settings. The site says it loud instead, with the sequence: press RST, then BOOT, never held
  together (held together it is download mode).
- After a profile change the touch alignment is not redone. A profile that mirrors an axis leaves the old map wrong
  until the next alignment [reasoned from the code, not seen].
- If makers disagree (RGB or BGR, inversion), ship both as profiles and ask the board's owner what they saw:
  colours right at start, which landscape is upright, touch, clean clock.

## 7. remind(): the SPI screen told again how it runs

A 3.5" was found standing still after about a day, the cause not known, and not whether the board behind it was
alive. One thing it could be: a panel that lost its state to a glitch on the line, and nothing would ever say so,
since nothing reads the panel. So every 5 seconds, between frames, under the done gate, it is told again (code in
[bus-spi-strips](references/bus-spi-strips.md)):

- Safe to repeat, nothing shows on a healthy screen [measured on the three boards here]: 0x11 sleep out (then
  5 ms), 0x3A pixel format, 0x20 or 0x21 inversion, 0x13 normal mode, 0x29 display on, 0x36 MADCTL.
- Not repeated: 0x01 reset (it blanks the glass and wants 120 ms), and the power and gamma tables: long, and the
  ST7796's sit behind an unlock. A panel that lost those needs a whole start-up, which remind does not do
  [reasoned].
- The values come out of the running profile, so the GC9A01's 0x3A 0x05 stays 0x05, and a table with no 0x13 sends
  none.
- Only between frames: any command ends a RAMWRC chain. The next frame sets the window anyway.
- How sure: it costs a few polling commands and 5 ms every 5 s and shows nothing. That it cures the stall is not
  proven; the stall was seen once. The run time and reset reason are kept (in NVS, once a minute) so that the next
  "it stood still" comes with facts.
- E-paper, the raw ILI9486 path and the RGB and DSI panels have no remind.

## 8. Backlight and panel power

- **On a PWM pin**: LEDC timer 0, channel 0, 5 kHz, 10 bits, duty 0 at start. Once frames are going the screen task
  steps the light 10 percent a frame, so the panel's start-up garbage is never seen and the screen fades in. Code in
  [bus-spi-strips](references/bus-spi-strips.md).
- **Dimming**: Flight Radar's BACKLIGHT setting has four steps, 100, 50, 25 and 10 percent, and a NIGHT level from
  3 degrees under the horizon to as long before sunrise, worked out on the board from its home and the network time,
  with half a degree of hysteresis, never brighter than BACKLIGHT [built]. Thermo Relay dims
  to 6 percent after a timeout and eases, and the first touch on a dim screen only wakes it.
- **Only on or off**: the round C3 (an I2C expander), the RPi LCD (wired to power: a night mode can only darken the
  palette), the CrowPanel Advance 5.0 (its board chip's brightness codes differ by version), the Waveshare 7 (on or
  off through its expander). The Waveshare 7B dims through its expander's inverted PWM, the Tab5 through its board
  package, the JC8012 through a boost converter's enable.
- **Behind an I2C expander or a board chip**, the expander comes up before the first LCD command, and the light goes
  on with the first frame: the round C3's PI4IOE5V6408 ([gc9a01](references/panels/gc9a01.md)), the CrowPanel
  Advance 5.0's PCA9557 or STC8H1K28, the Waveshare 7's CH422G and 7B's microcontroller
  ([bus-rgb](references/bus-rgb.md)).
- **Reset pin.** CYDs have LCD reset tied high or on EN, so software reset only; lcd_spi.c has no hardware reset.
  lcd_ili9486.c has one: low 10 ms, high, 120 ms before the first command. The JC8012's panel reset is held through
  the whole host bring-up ([jd9365](references/panels/jd9365.md)).
- **Panel power pin**: e-paper `EPD_PIN_PWR` has to be high. The CrowPanel Advance 2.8" has a TFT_PWR net that
  Elecrow's examples never drive, so neither does the build [built]. The Waveshare 7B's panel supply is an expander
  line.

## 9. Touch, the general part

- **Sample between the strips**, not once a frame: after every strip at most every 15 ms, into a queue of 24 the
  screen task reads at the top of the next frame. At 8 fps a swipe sampled once a frame came out as a tap
  [measured].
- **Deaf for 1.5 s after power on**, and a press held from the start counts only once it lifts: chips asked while
  the bus was settling said presses that were not there [built after phantom taps were measured].
- **Drivers return raw numbers**; one alignment does the swaps and mirrors for every kind: 5 crosses, least
  squares, worst cross over 18 px is TRY AGAIN; a 9 point quadratic where a resistive panel bends
  ([touch-alignment](references/touch-alignment.md)).
- **A stored alignment is checked at every boot** against the chip's raw span (`TOUCH_RAW_SPAN`: XPT2046 3700,
  CST816 240, FT6336 320, GT911 and the P4 boards the glass's width): a map from another chip is off by ten times.
- **A flip turns the map**, saved, no crosses. Holding a finger 4 s on an empty spot starts the alignment again,
  BOOT skips it: a board whose touch is wrong is never locked out of the page that fixes it.
- **Boards with a capacitive chip that counts the glass's pixels and a known layout** (Waveshare 7/7B, JC8012) skip
  the crosses: the map is the glass scaled to the layout.
- **The same chip on the same panel keeps its alignment across firmwares**: fall back to the sister firmwares' NVS
  namespaces when your own has none.

## 10. E-paper refresh basics

[ssd1683](references/epaper/ssd1683.md) has the driver. In short:

- SSD1683, bit-banged, 4 greys with a grey waveform (LUT). A full 4-grey refresh takes about 3 s and flashes: a new
  screen, every 10 minutes for ghosts, or when over 8000 pixels show a wrong grey, at most every 3 minutes for that.
- Between them a partial draws black and white on top (1.86 s on the 5.79", about 1.2 s on the 4.2"), no flash;
  unchanged pixels keep their grey, and if nothing changed the glass is not touched. A 2-bit model of what is on
  the glass is kept, in PSRAM.
- So grey goes on what never changes, and everything that changes is black on white
  ([paper-screen](references/epaper/paper-screen.md)).
- A new picture every 5 s; keys on a task of their own, because the screen task waits seconds in `wait_busy`
  (which feeds the watchdog and gives up after 10 s).
- The 5.79" is two controllers in a cascade: slave opcodes are the master's + 0x80, 8 px hide behind the seam, the
  picture comes out turned 180 degrees ([crowpanel-579](references/epaper/crowpanel-579.md)).
- The greys a waveform gives are a property of the panel: the 4.2"'s light grey was invisible until one frame per
  grey was moved ([crowpanel-42](references/epaper/crowpanel-42.md)). `OWLOS EPDTEST` prints bands to photograph.

## 11. I2C, buttons and LEDs in short

- I2C: one lazy bus a board on `i2c_master`, up on the first use, given back if nobody answered. On the classic
  ESP32, ESP-IDF 5.5's `i2c_master` can spin for ever after a NACK with the bus held: bit-bang there, and guard each
  sensor in RTC memory so a hung one is left alone after the watchdog restart ([i2c](references/i2c.md)).
- BOOT: polled from the screen task, long press 700 ms while held, short on release over 30 ms. GPIO 0 on the ESP32
  and S3, 9 on the C3, 28 on the C5, 35 on the JC8012; none readable on the Tab5, the plain CrowPanel 5.0 and 7.0
  and the Waveshare 7/7B ([buttons-keys](references/buttons-keys.md)).
- LEDs: the CYD's RGB LED on its own LEDC timer, inverted, red on GPIO 4 or 22 as a setting; a WS2812 on the RMT
  with `mem_block_symbols` from the SOC macro ([leds](references/leds.md)).

## 12. Chips on boards nobody ported yet

From BOARDS-WANTED.md's survey; pins and sources in esp32-boards. None built or run here.

- Chips the firmware knows: Guition JC2432W328C (ST7789 or ILI9341 by batch, CST816 or CST820), ESP32-2432S024
  (ILI9341, CST820 or XPT2046), ESP32-2432S032 (ST7789, GT911 or XPT2046), the plain Elecrow CrowPanel 2.8 and 2.4
  (ILI9341V, XPT2046), CrowPanel Advance 2.4 (S3), ESP32-2424S012C (round C3, GC9A01, CST816), WT32-SC01 (ST7796,
  FT6336), LilyGO T-Deck (ST7789, keyboard), Sunton ESP32-8048S043, S050, S070 (RGB 800x480).
- A new touch chip, power chip or expander in the way: Waveshare ESP32-S3-Touch-LCD-2.8 (CST328), Waveshare
  ESP32-C5-Touch-LCD-2.8 (ST7789, CST3530, reset and backlight on a CH32V003), M5Stack Core2 (AXP192) and CoreS3
  (AXP2101, AW9523), Waveshare ESP32-S3-Touch-LCD-3.5 (TCA9554, AXP2101), Elecrow CrowPanel 3.5 (ILI9488),
  CrowPanel Advance 7.0 (SC7277).
- Another bus that esp_lcd has: LilyGO T-Display S3 (8-bit parallel, 170x320), ESP32-2432S022C (8-bit parallel),
  Guition JC3248W535 (QSPI AXS15231B, in FusionEdge), JC4827W543 (QSPI NV3041A).

## 13. Traps, and where the fix is

- Screen task stuck for ever in `spi_device_acquire_bus`: a strip sent while the one before was in flight. The done
  gate ([bus-spi-strips](references/bus-spi-strips.md)).
- `disp_rows` freeing a buffer that DMA is reading: do it under the gate. Failing whole once sent the next strip
  to NULL: `s_rows` 0 and ask again.
- No memory for the strips: DMA buffers must be internal, and after WiFi the big blocks are gone. Lower strips. On
  the C5, 24-row strips starved the AES of internal RAM: "esp-aes: Failed to allocate memory".
- A frame a third slower on the shared bus: each touch read waited for a strip. The pen interrupt with its check
  ([xpt2046](references/touch/xpt2046.md)).
- Phantom taps after a restart: single noisy reads; two reads in a row and the deaf time. Taps from nowhere on
  resistive: the Z and edge checks. Swipes read as taps at low fps: sample between strips. The alignment never
  finishing at low fps: count 0.3 s of time, not samples.
- Bottom row 15 to 20 px off after a good alignment: the panel bends; the 9 point quadratic. A stored alignment
  from another touch kind, off by ten times: the raw span check. A flip without turning the map: taps land on the
  opposite side ([touch-alignment](references/touch-alignment.md)).
- GPIO 33 dead for the XPT2046 after looking for a GT911: `i2c_bus_free`. CST816 silent after a few seconds:
  0xFE = 1. FT6336 absent at boot or more than 2 fingers: 300 ms after reset, ignore them [built]. The plain
  CrowPanel 5.0's GT911 empty with a repeated start: a stop between ([gt911](references/touch/gt911.md)).
- Board reboots in a loop on the classic ESP32 with I2C sensors: `i2c_master` spinning after a NACK; bit-bang, the
  RTC guard ([i2c](references/i2c.md)).
- RPi LCD stays grey: 8-bit commands through shift registers; pad every byte. The padded byte goes in
  `tx_data[1]`. 0x66 gives wrong colours there: 0x55 ([ili9486](references/panels/ili9486.md)).
- Light blue on white, upside down: a CYD v3 on the v1 start-up; v1 table on a v2 panel: noise, negative. Profiles.
  A remind or flip that overwrites a profile's own value (v3's MADCTL 0xA0, the GC9A01's pixel format 0x05): read
  from the table. Picture still after about a day: remind(), and a record of run time and reset reason.
- Snow at 80 MHz, stripes at 40 on a shared bus: the clock. MISO on GPIO 12 of the 2.8": a strapping pin, keep
  `PIN_LCD_MISO -1`. An SD card on the screen's bus answering: hold its CS up before the bus starts.
- An RGB panel taken by esp_lcd only if the frame's rows divide by the bounce rows: 8 on the 480x272
  ([bus-rgb](references/bus-rgb.md)).
- BOOT pressed on a board where GPIO 0 is the pixel clock shorts the clock: no button there.
- A stale build folder sdkconfig on the Waveshare 7/7B or the JC8012: dark screen and a hang after WiFi; the build
  now refuses it.
- DMA reading PSRAM behind the cache (RGB fill, DSI frames): write back what the CPU wrote, on the cache line
  ([bus-mipi-dsi](references/bus-mipi-dsi.md)).
- `esp_lvgl_port` 2.9.0 stops every P4 build on ESP-IDF 5.5.3: pin `~2.8.0`.
- The GSL3680 touch dead on a fresh JC8012: its firmware is not in NVS; install it once
  ([gsl3680](references/touch/gsl3680.md)).
- E-paper refresh looks like a hang: seconds in `wait_busy` in a watched task; feed the watchdog, 10 s limit on
  BUSY. `EPD_PIN_PWR` low: nothing happens ([ssd1683](references/epaper/ssd1683.md)).
- Vertical garbage at the right of the 5.79": on the panel here, shattered glass. Look before debugging.
- Linker clash on `touch_init` or `touch_read`: ESP-IDF's librtc.a. Name them `tp_`.
- Red LED dark, or the speaker clicks: an LCDWIKI board, red on 22, GPIO 4 the amplifier ([leds](references/leds.md)).
- WS2812 flickers or shows old colours: the buffer changed under the RMT. `rmt_new_tx_channel` fails on one chip:
  `mem_block_symbols` hard coded. LED and backlight fight: different settings on one LEDC timer.
- Reading other people's setups: LovyanGFX's `rgb_order = false` is its BGR bit set; `invert = true` is 0x21;
  rotation numbers are per driver (LovyanGFX rotation 3 is MADCTL 0xA0 on the CrowPanel Advance 2.8"). A maker's
  sketch may start chips left over from another board: Elecrow's has a GT911 the CrowPanel Advance 2.8" does not
  use. A board file may name red what the bus has as blue.
- The name on the listing is not the board: a "2432S028" that was an ESP32-C3 clone took the round build; the 3.5"
  here is an LCDWIKI, not a Sunton; R and C versions differ in touch; a NM-CYD-C5 build on a ZLX-ESP32-2 leaves its
  screen dark. Read the ROM line for the chip, and ask which USB connector and which touch.
- Opening the serial port resets a CYD (CH340 auto reset), even with DTR and RTS off. Keep one long logger open
  instead of short peeks, or the run you look at is always a fresh boot. Straight after flashing the S3 prints
  nothing until a reset.

## 14. A new board, step by step

1. Pins from two sources: the maker's schematic, rzeldent/platformio-espressif32-sunton board JSONs,
   BruceDevices/firmware `boards/`, LCDWIKI's pin tables, a working ESPHome or other firmware's setup. Where is
   reset, where is the backlight, high or low lit, who else is on the bus (touch CS, SD CS), which USB, is GPIO 0 a
   panel line.
2. A block in `board.h`, with where each fact came from and "NOT run on one here" until it has.
3. SPI: start from the chip's table, the board's MADCTL from its maker's setup, 20 or 40 MHz. RGB: the pixel clock,
   the porches and polarities from the maker, bounce rows that divide the height, the PSRAM settings. DSI: the
   board package or the maker's library first.
4. The test frame of section 5. Read the boot log line: chip, size, clock, strip rows.
5. Light, then any picture, then way round, then colours, then speed.
6. If makers disagree (RGB or BGR, inversion), ship both as profiles with BOOT, and ask the owner of the board what
   they saw.
7. Touch: alignment on, log the raw numbers at the corners, set `TOUCH_RAW_SPAN`.
8. A long run with fps, `disp_lost` and heap in the log every 10 s. Two hours clean before calling it done.
