# Adding a board, step by step

As it was really done in OWLOS Flight Radar for boards 7 to 19, between 2026-11-03 and 2026-11-22. Two kinds of port:

- A board nobody here has (7 to 11, 13, 14, 19): researched from far away, built, flashed by someone who owns one. The
  way below is for those; for a board on the desk, skip what its owner would answer and look yourself.
- A board a contributor brings in, running on their own board (12 the Tab5, 15 to 17 from a fork, 18 the plain
  CrowPanel 5.0): reviewed, cut down to what belongs, registered. See "A board a contributor brings in" at the end.

Do not start from a shop title, and do not start until told: a new ask goes into BOARDS-WANTED.md first, with what is
known, and a reply goes back (esp32-project-playbook). The rule since 2026-11-02: list it, port it when the project's
maintainer says. A second ask for the same board is a good reason.

## What each board cost

To size the work. Each got cheaper because the last one's parts were made general: keep doing that, a new board should
mostly be a board.h block.

- 7 ES3C28P. New: FT6336 touch, WS2812 LED, the console on the chip's own USB, an S3 LCD sdkconfig. Reused: ILI9341 with
  the CYD's three profiles.
- 8 NM-CYD-C5. New: ST7789 as a chip with two profiles, the SD card held off the bus, the C5 target and its sdkconfig.
  Reused: XPT2046 on the bus, the WS2812, the console.
- 9 CrowPanel Advance 2.8. New: one switch, `LCD_INVERT`. Reused: ST7789, FT6336, the S3 sdkconfig: about 30 lines of
  board.h and a line in each list.
- 10 CrowPanel Advance 5.0 and 11 JC8048W550, the same day. New: the RGB bus (lcd_rgb.c), a frame in PSRAM, the scaled
  layout (`GFX_SCALED`: the 3.5"'s pages 3/2 as big on 800x480, glass fonts), `.s3rgb`, the board's own backlight chip,
  GT911 as a touch of its own. Reused: every page.
- 12 Tab5 (a contributor). New: the P4 target, MIPI-DSI through Espressif's board package from the component registry,
  the PPA turning strips onto a standing panel, WiFi on a C6 over ESP-Hosted, `partitions_16mb.csv`, the console on
  USB-Serial-JTAG, a board with no BOOT.
- 13 ESP32-4827S043C. New: a layout of its own at 480x272 one to one (no code: every size switch in core/ already asks
  `GFX_H >= 320`), `LCD_RGB_BOUNCE`, its own timing. Reused: the JC8048W550's pins.
- 14 CrowPanel 7.0. New: `RADAR_HAS_BUTTON` (BOOT is the panel's clock), RGB timing switches, a PCA9557 reset only when
  the touch is silent. Reused: the 5" layout.
- 15, 16 Waveshare 7 and 7B, 17 JC8012 (a contributor's fork). New: the moving layer, two vendored libraries, an 8 MB and
  a 16 MB partition table with a core dump, `.wave7` and `.jc8012`, build.sh. All behind `RADAR_OVERLAY` and their own
  `DISP_*`.
- 18 CrowPanel 5.0 plain (a contributor). New: `GT911_STOP`, `I2C_BUS_HZ`, `TOUCH_PCA9557_BOOT`, sync idle low. Reused:
  lcd_rgb.c.
- 19 ZLX-ESP32-2. New: three switches, `PIN_AMP_EN`, `PIN_T_IRQ_PULLUP`, `NO_CHIP_USB` on a C5. Reused: the NM-CYD-C5's
  chip and screen settings, the 2.8" CYD's bit-banged touch. Pins only.

## 0. Is it a new board at all?

See research.md. Often it is not: the Elegoo E32R28T and the Bruce kits are "take the 2.8" CYD build", and the answer is
a card and a sentence. Then only BOARDS-WANTED.md, the README's board list and the site's text change, and no number is
spent.

What a build needs, and nothing less: the chip, the screen chip, the touch chip, every pin. Each with its source written
down.

A near build flashed on the unknown board is a cheap probe, when its owner is willing: the CrowPanel 7.0's owner flashed
the CrowPanel Advance 5" build and got WiFi and planes with a dark screen (chip, USB and firmware fine, only the screen's
pins differ); the ESP32-4827S043's owner flashed the 5" build and got a picture spilling over the glass (data, sync and
touch pins the same, size and timing not). Never the other way round when a pin could harm the board: the NM-CYD-C5's
build on a ZLX-ESP32-2 drives the screen's DC onto its amplifier enable.

## 1. The number and the name

Take the next free number and a short lower-case name. In Flight Radar: `DISPLAY_CROWADV28_320x240 9`, `crowadv28`.

The name is forever: it is the build folder, the `fw/<name>/` folder on the site, the CI matrix, the stats server's list,
and the key the site's version archive files older firmware under. Rename nothing after a release. Never reuse a number.

Two people can take the same number: a contributor's pull request took 15 for the plain CrowPanel 5.0 while a fork used
15 to 17 for its boards. The fork's numbers were kept (so its later changes come over clean) and the pull request was
redone as 18.

## 2. core/config.h

All by that number. Grep for the previous board's name and you find them.

```c
#define DISPLAY_CROWADV28_320x240 9  // Elecrow CrowPanel Advance 2.8": ESP32-S3, ST7789 IPS, FT6336 touch

#if RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || ... || RADAR_DISPLAY == DISPLAY_CROWADV28_320x240
#define GFX_W 320                    // 1. the size: which group it joins
#define GFX_H 240

#define RADAR_TOUCH_CAL (... || RADAR_DISPLAY == DISPLAY_CROWADV28_320x240)  // 2. has touch: 5 cross alignment

#define RADAR_HAS_LED (...)          // 3. only if it has one (the CrowPanel does not)
#define RADAR_LED_RED_PIN (...)      // only the CYDs: red on one of two pins

#elif RADAR_DISPLAY == DISPLAY_CROWADV28_320x240
#define RADAR_BOARD "2.8\" CrowPanel Advance, S3"    // 4. what ABOUT says
```

Since the bigger boards, also:

- A size no group has: 320x240, 480x320, 240x240, 480x272 and the scaled 800x480, 1024x600, 1280x720, 1280x800 are laid
  out. A bigger glass keeps a layout and scales it (`GFX_W`/`GFX_H` the layout, `GFX_PW`/`GFX_PH` the glass,
  `GFX_SCALE_NUM`/`GFX_SCALE_SHIFT` the factor, 3/2, 15/8, 9/4, 5/2); a layout width is rounded so its middle is the
  glass's middle (534 not 533). A smaller glass gets its own layout. Same-size boards reuse every screen, every shot and
  the PC preview, and tools/ needs no change.
- `RADAR_RGB_BUS` (a frame the chip reads out: its own byte order), `RADAR_OVERLAY`, `RADAR_OVERLAY_COLS`.
- `RADAR_HAS_BUTTON`: take the board out when BOOT is a panel line or does not reach the chip.
- `RADAR_HAS_DIM`: take it out when the light only goes on.
- Scaled boards join `RADAR_TOUCH_CAL` by `GFX_SCALED`.

## 3. firmware/main/board.h

One block. Copy the nearest board and change it. Write in the comment where each fact came from and what is not known:
the comment is what the next person reads when the board arrives. The blocks of all 19: boards/.

Choices made on purpose, for a board nobody here can see:

- Where sources disagree, carry both as profiles, and make the likely one the start (NM-CYD-C5: RGB first, BGR second;
  every RGB board: RGB first, BGR by trading the data pins).
- `LCD_SPI_HZ`: the slower of what is known to work, not the maker's top. Elecrow runs 80, the build asks 40. On a shared
  bus lower still (NM-CYD-C5 asks 30, one project saw stripes at 40). A speed-up can come later, a striped screen as a
  first impression cannot. An RGB clock: the lowest the maker's examples use that the 5" boards already run (CrowPanel
  7.0: 15 and 24 in two examples, 16 built).
- `DISP_STRIP`: a heap decision. 24 at 320 wide with RAM to spare; 12 on a C5 (TLS's AES needs internal RAM) and on the RGB
  boards (the second core's strip); 6 on the 3.5" classic; 8 on paper.
- `PIN_LCD_RST (-1)` is fine: every start-up sends a software reset. A panel whose RST is on a GPIO needs code (the
  ILI9488 case).
- `PIN_LCD_BL`: LEDC PWM, high = lit. A board where low = lit, or the backlight is on an expander or a board chip, needs
  code (the round board, the CrowPanel Advance 5.0).
- An SD slot on the screen's bus: define `PIN_SD_CS`.
- Leave alone what the makers' examples never drive (`TFT_PWR` on GPIO 14 of the CrowPanel Advance 2.8). A pin you do not
  know is a pin you do not touch. But do hold off what would make noise: `PIN_AMP_EN` low from the start.
- Do not drive a pin that may be something else on a sister board: red LED 4 against the amplifier's enable.
- Boards with a WS2812: `PIN_LED_WS2812`, and the RGB defines at the bottom of board.h step aside by themselves.
- A board whose USB pins are used for something else: `NO_CHIP_USB`.
- A board package or a library that owns the pins: name none (the Tab5, the Waveshare 7/7B, the JC8012), say so in the
  comment.

Check every pin against the chip file (chips/): input-only pins, flash pins, octal PSRAM's 33 to 37, strapping pins, USB
pins if the board has the chip's own USB, GPIO 0 used as a panel line.

## 4. Driver switches, only if a chip or a behaviour is new

`lcd_spi.c`, `lcd_rgb.c`, `touch.c`, `led.c`, `main.c`, `console.c`. Rules:

- Prefer a board.h switch with a default that changes nothing (`#ifndef LCD_INVERT`, `#define LCD_INVERT 0`). The list of
  switches: board-code.md.
- After changing a shared driver, rebuild the other board that uses it and compare the .bin: the NM-CYD-C5 stayed
  byte-identical after `LCD_INVERT`. That is the cheapest regression test there is.
- A drawing change: draw every page on the PC preview before and after and compare pixel for pixel (135 shots stayed
  identical when the scaled layout came in; a halves check draws every page whole, in strips and cut at any row).
- A new touch chip that reads like an old one shares the code path (FT6336 through the CST816's).
- `TOUCH_RAW_SPAN` at the bottom of board.h for a new touch kind.
- Everything a contributed board brings that reaches other boards goes behind its own switch (`#if RADAR_OVERLAY`), with
  the old code in `#else`.

## 5. sdkconfig, only if needed

If its chip is already there and the chip's file fits, nothing. Otherwise a file for a kind of board, added in
firmware/CMakeLists.txt BEFORE `include(project.cmake)`:

```cmake
if(RADAR_DISPLAY EQUAL 7 OR RADAR_DISPLAY EQUAL 9)   # the S3 boards with an LCD
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.s3lcd")
elseif(RADAR_DISPLAY EQUAL 10 OR ...)                # and those with an RGB panel
    set(SDKCONFIG_DEFAULTS "...defaults;...s3lcd;...s3rgb")
    if(RADAR_DISPLAY EQUAL 15 OR RADAR_DISPLAY EQUAL 16)
        list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.wave7")
    endif()
endif()
```

Why here: `sdkconfig.defaults.esp32s3` was for the e-papers, an S3 LCD board wanted 4 MB and QIO 80. Name the file for
what it is, not the first board that needed it: `sdkconfig.defaults.es3c28p` was renamed `.s3lcd` when the second board
came. Files stack: `.s3rgb` on `.s3lcd`, `.wave7` on `.s3rgb`, `.jc8012` on the chip's `.esp32p4`.

A new chip: `sdkconfig.defaults.<target>` (found by IDF by name, no CMake). See SKILL.md for the order the files are read
in. A new partition table when the flash or a core dump asks for it (`partitions_16mb.csv`, `partitions_wave7.csv`,
`partitions_jc8012.csv`).

## 6. firmware/BUILD.BAT, and build.sh

Three lines in BUILD.BAT: the list in the comment, the mapping, the error message.

```bat
rem           crowadv28 (Elecrow CrowPanel Advance 2.8", ESP32-S3)
if /i "%DISP%"=="crowadv28" (set NUM=9& set TARGET=esp32s3)
  echo unknown display %DISP%, pick one of: ili9341 st7796 ... crowadv28 epd400 epd792
```

For a new preview target, the `--preview` line after it. A board whose contributor builds on macOS or Linux also goes in
build.sh's `case` (wave7, wave7b, crow70, jc8012, crow50).

## 7. Build it and read what came out

```powershell
cmd /c "<repo>\firmware\BUILD.BAT crowadv28 > %TEMP%\build.log 2>&1"
```

Then read, do not assume (all from real builds):

- The log ends in "Project build complete". A failed IDF build leaves the old .bin, and esptool flashes it happily.
- `build_crowadv28/sdkconfig`: `CONFIG_IDF_TARGET`, `CONFIG_ESPTOOLPY_FLASHSIZE`, `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`,
  `CONFIG_SPIRAM_MODE_OCT` or `_QUAD`, `CONFIG_PARTITION_TABLE_FILENAME`.
- `build_crowadv28/flash_args`: the bootloader's offset is right for the chip.
- Sizes: the .bin against the factory partition, warnings count.
- The other boards you touched: same size, or know why not. Build all of them: on 2026-11-14 the S3 builds did not compile
  after work on the P4 one, a hook was inside the wrong `#if`.

A folder made before you changed sdkconfig files keeps its old settings: delete `build_<name>/sdkconfig` and build again.
A board whose code depends on a new sdkconfig line can refuse such a folder with an `#error` (lcd_wave7.c).

A P4 board may not build on the local IDF (the Tab5's components, MissingKconfigError): CI's image builds it, and a pull
request into main runs every board's build without publishing.

Nothing can be tested on glass here, so test what can be: the code path on the PC preview (the same size has the same
pictures), and flash the driver change on a board of the same family that is on the desk.

## 8. CI and the manifest

`.github/workflows/pages.yml`, one matrix line:

```yaml
- { disp: crowadv28, num: 9, target: esp32s3 }
```

`.github/make-manifests.sh`, one case line. The name people read in the flasher, and the chipFamily:

```bash
crowadv28) name='2.8" Elecrow CrowPanel Advance (ESP32-S3, ST7789)'; family='ESP32-S3' ;;
```

The family must be exact, ESP Web Tools compares it with the chip's name and says "Your ESP32-S3 board is not supported"
otherwise. An unknown folder is skipped with a log line, not an error: a board with a matrix line and no case line just
has no manifest, and its card gives a 404. Offsets are never typed, they come from `flash_args`. The site stays working
when one board fails to build: `fail-fast: false`. A board not yet run on one says "experimental" in its manifest name.

A board of a new size also needs a web twin (the `web` job's list in pages.yml, `web/BUILD.BAT`).

## 9. The site and the server

- `site/flow.js`, `BOARDS`: one line (board-files.md has the fields). `noboot` for a board without a readable BOOT, `nvsfw`
  for one whose erase takes more than settings, `screens` for the profiles BOOT goes through.
- A board new in a beta: the site flashes it the beta only (`inStable`), with no row to pick the stable.
- `tools/assemble.sh`: its folder in the list, for a local look at the site.
- `server/appsettings.json`, `Stats.Builds`: the stats server reads it once at start, so a new board means a server
  update too, or it is not counted.
- The older page (`site/previous/index.html`) has cards of its own and is not touched for new boards. Its form, for a
  project that keeps cards in HTML:

```html
<article class="card">
  <img src="shots/02-live.png" alt="the radar on a 2.8 inch board" loading="lazy">
  <div class="body">
    <h3>2.8" CrowPanel Advance</h3>
    <div class="spec">Elecrow's 2.8" HMI, ESP32-S3, ST7789 320x240 IPS, capacitive touch. Either of its two
      USB-C ports works. New: not run on one here yet, tell us how it goes.</div>
    <a class="boot-tag" href="#boot">RED AND BLUE SWAPPED? PRESS BOOT</a>
    <esp-web-install-button class="flash" manifest="fw/crowadv28/manifest.json">
      ...
    </esp-web-install-button>
  </div>
</article>
```

  with the `#boot` block naming the board's profiles, and the lists of boards that have the LED. Nothing else: the
  version archive's code reads the manifest out of each button, and a board that came later says so for older versions.
- A picture the site shows needs its line in .gitignore's keep list (`shots/`), or CI does not have it.

One card, one chip: a card's manifest has one chipFamily. A board with two chips in its family (a CYD and a C3 clone) gets
two cards, or one manifest with a build per chipFamily, which ESP Web Tools picks from at connect. That second way is an
open idea, not built.

## 10. The words

- README.md: the boards list (say "New, not run on one here yet" or "Experimental", and where the pins came from), the
  build's name under "Build it yourself", the `screen` values of the USB console, the LED section if it has one. No
  tables, no emoji, no long dashes.
- BOARDS-WANTED.md: the section gets a "built <date>, not run on one yet" title, then: what it is, sources, pins, "Built
  with" (every guess and why), "Not driven", doubts, "To learn".
- CHANGES.md: a line in the new version, "experimental" for a board not run on one. A new number keeps the last version
  in the site's archive. A board that changes nothing for the others can say so ("there is no need to flash it again").
- A contributor: credit by their GitHub handle in README and CHANGES, and a copyright line in LICENSE (no email).
- core/config.h's and board.h's comments say the same as all that.

## 11. Before it goes out

- Flash at least one real board with a build from the same tree: the round C3 took it on 2026-11-03, UART console, GET
  and fetch fine. It does not test the new board, it tests that the shared code did not break. Keep each board's settings
  across it (read them before, compare every field after).
- Assemble the site locally: every card, manifest and part answers 200.
- The maintainer commits and pushes. Never commit, never offer to.

## 12. What to ask the person with the board

Ask few things, in the order they fail, with the way to fix each ready. One message:

1. Does the site see the board? Does the flash button connect, does it finish? (The manifest, the chip, the cable, the
   driver.) Native USB boards may need BOOT held while plugging in.
2. Is there a picture? Is the backlight on?
3. Are the colours right? Red and blue swapped, or a negative: BOOT in the first 8 seconds goes through the profiles.
   Which one was right?
4. Is it the right way up? SET has upside down. If the other one is right, the build's default changes by one XOR.
5. Does the touch work? The 5 crosses come first. No touch at all: what does the log say at start ("nothing at 0x38")?
6. Does the site read it? Step 2's READ WHAT IT HAS over each USB port. On a native port it is the first test of that
   path.
7. The LED, if it has one: SET, LED, TEST: red, green, blue, in that order?
8. Stripes or noise after a few minutes? Then the clock comes down. On an RGB board: does the picture shake while the
   map loads?
9. The log, when anything stops: the first owners' logs are what found the C5's memory (two rounds) and the S3 event
   stack.

Then write the answers into board.h's comment and BOARDS-WANTED.md, change "built" to "measured", and change the default
if the first guess was wrong. The README's "not run on one here yet" goes last, once someone has run it.

## How boards 10 to 19 were really ported

- 10 crowadv50 and 11 jc8048w550 (2026-11-05). Asked twice for 800x480. Built the same day with no board of this bus
  here: lcd_rgb.c from Elecrow's IDF example (frame in PSRAM, bounce buffers), the layout scaled rather than a new one,
  glass fonts baked 1.5x, every other screen checked pixel-identical. The asker's V1.3 ran it that day: picture and touch,
  3 fps, the second core never started, the WiFi page restarted the board. The next builds: strips 12, TLS and WiFi
  buffers in PSRAM, a 4 KB event stack. The JC8048W550 came from one JSON, its "red" low five read as blue.
- 12 tab5 (2026-11-08). A pull request by parispitts, who runs it on a Tab5 with the ST7121 panel. Espressif's board
  package does the panel and touch, so board.h names no pins. Its components come from the registry, gated to the P4.
  It never built on the local IDF; CI builds it.
- 13 esp4827s043 (2026-11-09). Asked as "older Guition 4.3" modules", narrowed by asking back: the model, then C or R,
  then a cross-flash that spilled over. Pins from rzeldent's JSON matched the 5" build's; its own timing and 8-row
  bounce buffers; its own one-to-one layout, checked on the PC preview.
- 14 crow70 (2026-11-10). A photo of the back named the plain 7.0 V3.0 against the Advance. Pins from two Elecrow
  examples and its hardware guide, a second source in an ESPHome setup. BOOT is the panel's clock: the board shows no
  BOOT hints. The PCA9557 is touched only when the touch is silent, because a reset at every start broke it for others.
- 15 wave7, 16 wave7b, 17 jc8012 (2026-11-16). chipguyhere's fork, 72 files and about 17,000 lines. Brought in through a
  pull request of the maintainer's own, squashed: Silead's GPL touch firmware had been in one fork commit and 7.4 MB of
  video in media/, neither may enter an MIT history; the touch firmware now comes from NVS, put there by the
  contributor's installer, and the video is linked. Every change that reached the other boards was gated behind
  `RADAR_OVERLAY` with the old code kept, except two kept for all: joining the strongest access point of a network, and
  the console finding "OWLOS " inside a line. The fork's numbers kept.
- 18 crow50 (2026-11-17 and 18). bertvm's first pull request (#9) added the board as 15 and also rewrote another board's
  code written while that board was misidentified, gated HTTPS on NTP (the 5.5 builds do not check certificate dates, so
  a network that blocks NTP would never fetch), added a second root CMakeLists and changed I2C for every board. The redo
  (#11) carried only the board and the IDF 6 guards, as 18. Merged as it was, then marked experimental, credited, and
  the stats server updated.
- 19 zlx2 (2026-11-22). GitHub issue #13 gave the pins from the maker's manual; they were checked against the maker's
  pins.h and factory test before a line was written. Pins only and three small switches. New in a beta, so beta-only on
  the site.
- 20 esp2432s032 (2026-11-25). A comment with a photo of the back gave the model; its owner had flashed the 3.5" build
  and got a garbled, flickering screen. The raw rzeldent JSONs showed every pin the 3.5"'s and only the screen another
  (ST7789 240x320), so the build is two existing ones put together: the 3.5"'s pins and touch with the 2.8" CYD's
  picture and memory, and the ST7789 of the CrowPanel Advance 2.8. No new code. A board that shares a sibling's pins is
  first checked for exactly this: which build already has its pins, which already has its screen. The next day the
  owner's box pointed to the maker's downloads: a 119 MB zip, read in the browser by HTTP ranges (its table of contents,
  then only the files wanted, nothing saved), its schematics viewed as pictures. They agreed on every pin and gave one
  change, the screen's SPI mode. The second source is worth that hour: it is where mode 3 and the GT911's pulled-up INT
  showed.

## A board a contributor brings in

- Read the whole change before taking any of it. List what reaches boards it was not written for (a keep-alive
  connection, a log ring, scaled drawing, timing diagnostics); gate each or decide to keep it for all, and test the ones
  kept on the desk's boards (the strongest-AP join: an A/B of 40 minutes on the weak round board changed nothing).
- Take out what was written against the wrong board, and what is an unrelated preference.
- Keep their copyright and credit: their handle in README and CHANGES, a LICENSE line, their name on the files they wrote.
- Licences: nothing GPL into an MIT tree, not even through history. Vendored libraries keep their own licence files.
- Their numbers can stay; a clash is solved by whoever is later.
- Their measurements are theirs: write "measured by <handle> on their board", never "measured" alone.
