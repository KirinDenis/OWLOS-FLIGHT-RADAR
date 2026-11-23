---
name: esp32-boards
description: ESP32 boards with a screen and the chips under them - classic ESP32, ESP32-S3, C3, C5 and P4 (cores, PSRAM quad or octal, native USB or CH340, strapping and BOOT pins, bootloader at 0x1000, 0x0 or 0x2000, RGB and MIPI-DSI panels, heap left with WiFi and TLS) - and one file per board with pins, board.h block, sdkconfig and how sure (2.8" CYD ESP32-2432S028R, 3.5" ESP32-3248S035, LCDWIKI E32R28T, E32R35T, ES3C28P, CrowPanel 1.28 round, e-paper 4.2 and 5.79, CrowPanel Advance 2.8 and 5.0, CrowPanel 5.0 and 7.0, NM-CYD-C5, ZLX-ESP32-2, Guition JC8048W550, JC8012P4A1C, JC3248W535, ESP32-4827S043, Waveshare S3-Touch-LCD-7 and 7B, M5Stack Tab5 and more), and the real way to port a board (board.h, config.h, per-board sdkconfig, BUILD.BAT, CI matrix, manifest, site). Use when asked "will it run on X", when adding or changing a board, picking a chip or a board, setting sdkconfig per chip, researching a board nobody has (schematics, LovyanGFX, board JSONs), or working out which board is plugged in before flashing.
---

# ESP32 boards: the chips, every board we know, how to port one

The general part and the index. Every chip and every board has a file of its own under references/. Learned in OWLOS
Flight Radar (the newest, 19 builds: start there), OWLOS WiFi Radar, OWLOS Thermo Relay, OWLOS Air Quality UI, ESP32 Web
Flasher and FusionEdge, their BOARDS-WANTED.md, FORK-NOTES.md and notes, plus ESP-IDF 5.5's own headers and docs.

Sibling skills: esp32-lcd-touch has the panels, buses and touch chips (start-ups, MADCTL, strips, DMA); esp32-flash-and-
settings the flasher, the site and CI; esp32-runtime tasks, memory and crashes; esp32-ui-drawing the screens;
esp32-project-playbook how to answer the person asking.

## How sure: four words

Every fact carries one of these, in the text or in a file's "How sure" line.

- **measured**: ran on that real board here, seen on the glass or in a log. When someone else ran it on their board (a
  tester, a contributor), the file says whose: "measured by chipguyhere on their board", "a tester's log".
- **built**: compiled for that board, never run on one.
- **docs**: read from a maker, a port or a datasheet, not built by us.
- **reasoned**: worked out from the above, not seen.

Say the same out loud: a built board is "New: not run on one here yet", or "Experimental", never "supported". Comments in
repos can be older than the facts (some board.h lines still say "NOT run on hardware"): the board files are newer.

## The chips

- [ESP32](references/chips/esp32.md): 2 cores, no PSRAM on the screen boards, a USB bridge only, input-only 34 to 39,
  strapping pin 12, SPI matrix limit 40 MHz. Bootloader 0x1000. The workhorse: strips, never a frame.
- [ESP32-S3](references/chips/esp32-s3.md): 2 cores, octal PSRAM on N16R8 (33 to 37 gone), its own USB on 19/20, an RGB
  LCD peripheral, PIE. Bootloader 0x0. Internal RAM is the limit on the RGB boards.
- [ESP32-C3](references/chips/esp32-c3.md): 1 core at 160 MHz, no FPU, no PSRAM, one SPI bus, the tightest flash.
  Priorities decide everything. Bootloader 0x0.
- [ESP32-C5](references/chips/esp32-c5.md): 1 core at 240 MHz, 5 GHz WiFi, quad PSRAM, one SPI bus, a preview target on
  the 5.5 tag. Bootloader 0x2000. TLS in PSRAM needs internal RAM for the AES.
- [ESP32-P4](references/chips/esp32-p4.md): 2 cores, 32 MB PSRAM, MIPI-DSI and the PPA, no radio: WiFi on a C6 over SDIO.
  Bootloader 0x2000. Built from the component registry.

Quick facts across them:

- BOOT pin: ESP32 0, S3 0, C3 9, C5 28; none reaches the P4 on the Tab5, 35 on the JC8012.
- UART0 TX/RX: ESP32 1/3, S3 43/44, C3 21/20, C5 11/12.
- Own USB D-/D+: S3 19/20, C3 18/19, C5 13/14, none on the classic ESP32.
- SPI2 IOMUX SCLK MOSI MISO CS: ESP32 14 13 12 15, S3 12 11 13 10, C3 and C5 6 7 2 10.
- Never type a bootloader offset: take it from the build's `flash_args`. 0x1000 classic (and S2); 0x0 S3, C3, C2, C6;
  0x2000 C5, P4, H4 (their first two sectors are reserved for the key manager). Partition table 0x8000, app 0x10000.
- USB ids for the port list (docs): CH340 `VID_1A86`, CP210x `VID_10C4`, Espressif's own USB `VID_303A`.

## The boards

Flight Radar's builds, by `RADAR_DISPLAY` number (core/config.h; WiFi Radar uses 1 to 6 the same way):

- 1 [ili9341](references/boards/ili9341.md): 2.8" CYD ESP32-2432S028R, ESP32, three panels behind BOOT. The v1 start-up
  measured (on an E32R28T), v2 and v3 built.
- 2 [st7796](references/boards/st7796.md): 3.5" ESP32-3248S035 R and C, ESP32, ST7796 480x320. Measured on an E32R35T;
  the C's GT911 built.
- 3 [ili9486](references/boards/ili9486.md): Waveshare RPi LCD (A) on the OWLOSAirQuality board, ESP32. Panel measured
  under Air Quality UI, the radar built.
- 4 [gc9a01](references/boards/gc9a01.md): Elecrow CrowPanel 1.28" round, C3. Measured.
- 5 [epd400](references/boards/epd400.md): Elecrow CrowPanel e-paper 4.2", S3. Measured.
- 6 [epd792](references/boards/epd792.md): Elecrow CrowPanel e-paper 5.79", S3. Measured under WiFi Radar.
- 7 [es3c28p](references/boards/es3c28p.md): LCDWIKI ES3C28P 2.8", S3. Built.
- 8 [nmcydc5](references/boards/nmcydc5.md): NM-CYD-C5 2.8", C5. Built; a tester's logs drove two fixes.
- 9 [crowadv28](references/boards/crowadv28.md): Elecrow CrowPanel Advance 2.8", S3. Built.
- 10 [crowadv50](references/boards/crowadv50.md): Elecrow CrowPanel Advance 5.0", S3, RGB 800x480. Built; a tester's V1.3
  shows a picture and touches.
- 11 [jc8048w550](references/boards/jc8048w550.md): Guition JC8048W550 5", S3, RGB 800x480. Built, experimental.
- 12 [tab5](references/boards/tab5.md): M5Stack Tab5 5", P4, MIPI-DSI 1280x720. Run by its contributor.
- 13 [esp4827s043](references/boards/esp4827s043.md): ESP32-4827S043C 4.3", S3, RGB 480x272. Built, experimental.
- 14 [crow70](references/boards/crow70.md): Elecrow CrowPanel 7.0" plain, S3, RGB 800x480. Built, experimental.
- 15 [wave7](references/boards/wave7.md): Waveshare ESP32-S3-Touch-LCD-7, RGB 800x480. Run by its contributor.
- 16 [wave7b](references/boards/wave7b.md): Waveshare ESP32-S3-Touch-LCD-7B, RGB 1024x600. Run by its contributor.
- 17 [jc8012](references/boards/jc8012.md): Guition JC8012P4A1C 10.1", P4, MIPI-DSI 1280x800. Run by its contributor.
- 18 [crow50](references/boards/crow50.md): Elecrow CrowPanel 5.0" plain, S3, RGB 800x480. Built, experimental.
- 19 [zlx2](references/boards/zlx2.md): ZLX-ESP32-2 2.8", C5. Built, experimental.

Boards without a build of their own:

- [e32r28t](references/boards/e32r28t.md): LCDWIKI E32R28T 2.8", takes `ili9341`. Measured: the desk's 2.8" boards.
- [e32r35t](references/boards/e32r35t.md): LCDWIKI E32R35T 3.5", takes `st7796`. Measured: the desk's 3.5" board.
- [bruce-cyd](references/boards/bruce-cyd.md): "Bruce CYD" kits with NFC and GPS modules, take `ili9341`. Docs.
- [ili9488-diy](references/boards/ili9488-diy.md): an ILI9488 and XPT2046 wired to an S3 by hand. Docs, waiting.

FusionEdge's boards (an internet radio, Arduino and PlatformIO; its builds `cyd28`, `e32r35t` and `crowadv28` run the
boards above):

- [s3-ili9488](references/boards/s3-ili9488.md): ESP32-S3 DevKitC-1 N16R8 with an ILI9488 module, its primary target.
- [jc3248w535](references/boards/jc3248w535.md): Guition JC3248W535 3.5", S3, AXS15231B on QSPI.
- [gen4-35ct](references/boards/gen4-35ct.md): 4D Systems gen4-ESP32-35CT 3.5", S3, ILI9488.
- [wrover-n16r8](references/boards/wrover-n16r8.md): ESP32-WROVER-E N16R8 with an ILI9488, classic ESP32 with PSRAM.

Shared references:

- [boards-others.md](references/boards-others.md): boards on sale nobody asked for, easiest to hardest, with pins.
- [new-board-recipe.md](references/new-board-recipe.md): the port, step by step, as really done for boards 7 to 19, and
  what to ask the person with the board.
- [research.md](references/research.md): finding out about a board from far away: trust order, Eagle, LovyanGFX, JSONs.
- [board-code.md](references/board-code.md): the code that differs per board: every board.h switch, profiles, BOOT,
  expanders, touch probing, LEDs, the USB console.
- [board-files.md](references/board-files.md): the shared files: board.h head and tail, config.h, CMake, BUILD.BAT,
  build.sh, every sdkconfig and partition file, manifests, CI matrix, where else a board is named, other projects.

## Picking a chip and a board for a new project

- The 2.8" yellow board (the CYD, or LCDWIKI's E32R28T) is the safe pick: cheap and the most tested here. A classic
  ESP32 with no PSRAM draws in strips and has about 25 to 70 KB of heap with WiFi and TLS.
- A 480x320 screen on a classic ESP32 costs real work to fit (the 3.5" runs at the edge of its memory with a map).
- A frame buffer, a bigger glass, or anything that keeps TLS open for good: an S3 with PSRAM. The RGB boards are where
  internal RAM, not PSRAM, runs out. Of the S3 boards only the e-paper has run here.
- A round glass: the C3 CrowPanel, measured; one core, so mind task priorities.
- 5 GHz WiFi: the C5. Its two boards are built, not run here.
- A tablet-sized glass: a P4 with MIPI-DSI. Its WiFi is a C6 and its builds depend on the component registry.

## How a board is described: compile time, one number

- One number per board (`RADAR_DISPLAY` in core/config.h), passed by the build. Pins and parts in one file,
  firmware/main/board.h. Nothing else knows a pin.
- One build per board, its own build folder and sdkconfig. The project decided against pin assignment at run time: the
  board is described at build time and the board file is the truth. Settings stay flexible, pins do not.
- The same number sets the picture size, so `core/` draws the same on the PC preview and in the browser twin.
- Every driver file is always in the build and compiles to nothing unless board.h picks it, so a new board touches no
  list of sources. A board's library or registry component is gated by target or number the same way.

Three things cannot be known at build time, and each got its own answer:

- Which of the 2.8" screens: one name, three panels, sellers do not say. Profiles in one build; a person picks with BOOT
  in the first 8 s, saved in NVS. On the S3 and C5 SPI boards and every RGB board: RGB or BGR, the same way.
- 3.5" R or C touch: one build for both. Ask for a GT911 on I2C, give the pins back if nobody answers.
- Red LED on 4 or 22: LCDWIKI's boards look like the CYD, and their 4 is the amplifier's enable. A setting, default 4,
  never both pins.

An open idea, not built: a "custom board", screen chip, touch and pins picked on the site and kept in NVS, one build per
chip, for people who wire their own. A short known list to pick from at run time is fine. A general I2C scan is not:
tried in old OWLOS and rejected.

## Adding a board, short form

Full version with real snippets and how boards 10 to 19 were really done: references/new-board-recipe.md. Not before the
maintainer says: a new ask first goes into BOARDS-WANTED.md.

1. Research it (below). No chip, screen chip, touch chip and all pins from a trusted source, no build. Maybe an existing
   build already fits.
2. Next free number, a short name. The name is forever: build folder, `fw/<name>/`, CI, the stats server, the archive.
3. core/config.h: number, size group (or a scaled layout), has-touch, has-LED, has-button, can-dim, the name for ABOUT.
4. firmware/main/board.h: one block, copied from the nearest board, with sources and doubts in the comment.
5. A driver switch only if something is new, with a default that changes nothing. Then rebuild the other boards that
   share it and compare the .bin.
6. sdkconfig, only if the chip's file does not fit (below).
7. firmware/BUILD.BAT: comment, mapping line, error list; build.sh for a contributor on macOS or Linux.
8. Build. Read `build_<name>/sdkconfig`, `flash_args`, the size against the partition. Build every other board too.
9. .github/workflows/pages.yml matrix line, .github/make-manifests.sh name and chipFamily; a web twin for a new size.
10. site/flow.js `BOARDS` line; tools/assemble.sh; the stats server's build list.
11. README boards list, build list, `screen` values. BOARDS-WANTED section. CHANGES line, "experimental" until run.
12. Flash a board on the desk with the same tree as a regression test. The maintainer pushes.
13. Ask the person with the board, in this order: does the site see it, is there a picture, are the colours right, which
    landscape is up, does the touch work, does the site read it, the LED, stripes. Write the answers back, and change
    built to measured.

## sdkconfig per board

How ESP-IDF reads defaults (docs: build-system.rst; used in Flight Radar's firmware/CMakeLists.txt, built):

- `SDKCONFIG_DEFAULTS` must be set BEFORE `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`. After it, it is ignored.
- Order: `sdkconfig.defaults`, then `sdkconfig.defaults.<target>`, then the next file in the list. A later file wins. For
  `sdkconfig.defaults;sdkconfig.defaults.s3lcd` on an S3 that is defaults, `.esp32s3`, `.s3lcd`: the target file comes
  right after the file that brings it in.
- A name with no path is taken from the project folder. Flight Radar gives full paths, `${CMAKE_CURRENT_LIST_DIR}/...`.
- All of them are read ONLY when the build folder's sdkconfig is made. Change a defaults file and an existing build keeps
  the old values: delete `build_<name>/sdkconfig`. Delete `firmware/sdkconfig` for the 2.8" CYD: BUILD.BAT keeps that one
  in the project root, as it always had.
- A file for a kind of board, not for a board: `.es3c28p` became `.s3lcd` when the second S3 LCD board came. They stack:
  `.s3lcd`, then `.s3rgb` for the RGB boards, then `.wave7` for the Waveshare 7/7B; `.jc8012` after the P4's chip file.

```cmake
# firmware/CMakeLists.txt, before include(project.cmake)
if(RADAR_DISPLAY EQUAL 7 OR RADAR_DISPLAY EQUAL 9)   # the S3 boards with an LCD
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.s3lcd")
endif()
```

Where the settings stand now (copies in board-files.md):

- Classic: QIO 80, 4 MB, `partitions.csv`, CPU 240, no PSRAM.
- C3 round: QIO 80, 2 MB, `partitions_2mb.csv`, CPU 160.
- S3 e-paper: default flash mode and speed, 4 MB (since 2026-11-22; 2 MB before), CPU 240, octal PSRAM by name.
- S3 SPI LCD: QIO 80, 4 MB, octal PSRAM by name, event task stack 4096.
- S3 RGB: as S3 LCD, PSRAM at 80 MHz, code run from PSRAM, TLS and WiFi buffers in PSRAM, PSRAM required.
- Waveshare 7/7B: as S3 RGB, 8 MB with a core dump, big state in PSRAM, software AES.
- C5: QIO 80, 4 MB, CPU 240, quad PSRAM required, TLS and WiFi buffers in PSRAM.
- P4: 16 MB, PSRAM at 200 MHz, CPU 360 on the v1.3 chips, 16 MB layouts.

`flash_args` says dio for all of them, and that is right (chips/esp32.md). 8 and 16 MB S3 and C5 boards run the 4 MB
layout on purpose: one layout, the map's tiles get the same 2 MB. Older projects stay on DIO 40.

ESP-IDF versions:

- Local ESP-IDF is the v5.5 tag (`git_revision "v5.5"`, version 5.5.0): esp32c5 is a preview target there, so BUILD.BAT
  passes `idf.py --preview` for that target only, before the other arguments. The Tab5 does not build on it.
- CI's `espressif/idf:release-v5.5` is a later 5.5: the C5 is supported there and the P4 builds.
- Set the chip with `-DIDF_TARGET=`, the board with `-DRADAR_DISPLAY=`, and `-DSDKCONFIG=<build folder>/sdkconfig` so
  boards do not share one.
- ESP-IDF 6 (a contributor builds with it): the RGB panel is asked for RGB565 by name (`in_color_format`), and
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` trusts a cross-signed root; a 5.5 build warns that it does not
  know that line and skips it. Guard such code with `ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)`.
- ESP-IDF 5.5's FreeRTOS port breaks a PIE task that is preempted (the P4's thin slivers): chips/esp32-p4.md.

## Researching a board with no board on the desk, short form

Full version: references/research.md.

1. Find out what it really is. A shop title is noise. A shop's option can be only an add-on module. The SKU and the
   version printed on the back decide. A 2432S028 with no letter: R or C unknown. Even boards on the desk can be
   misnamed: the "CYDs" here were LCDWIKI boards.
2. Trust order: working example code (a maker's factory test, a firmware already running on the board), then schematic
   nets, then generated docs, then shop text. Follow which code path actually runs: the Elecrow repo's AI-written hardware
   guide says GT911 because of a leftover touch.h, while the sketch reads touch through LovyanGFX `Touch_FT5x06` at 0x38.
3. Eagle .sch files are XML: fetch raw from GitHub, list `<net name=` and `<part`. The PowerShell is in research.md, no
   Python, no Eagle.
4. LovyanGFX to raw: `rgb_order = false` means the BGR bit (MADCTL 0x08) is set, `invert = true` means INVON,
   `offset_rotation` 3 gives MV|MY|ML = 0xB0, the table is in Panel_LCD.hpp.
5. Sources that gave pins: makers' wikis and repos, Bruce firmware boards/, rzeldent/platformio-espressif32-sunton board
   JSONs, lcdwiki.com, forks when a maker's repo is gone, ESPHome setups, GitHub issues checked against the maker's files.
6. Read what an add-on takes: Bruce's CYD setup puts a GPS on TX/RX (GPIO 1 and 3), the same UART as the USB bridge.
7. Where sources disagree, carry both as profiles. Take the slower clock. Write down the doubt.
8. A near build flashed by a willing owner is a probe: what comes up tells which parts are the same.

## Which board is on the desk

Before any flash. Never assume a port: numbers move when a cable moves, two boards can swap them in a day, and a stale
number flashes the wrong board.

1. List the ports with their USB ids: on Windows
   `Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM' } | Select-Object Name, DeviceID`. The VID says
   which kind of USB, not which board.
2. Ask the chip, by its MAC. IDF 5.5's esptool is 4.10:

   ```powershell
   python -m esptool --port <port> chip_id     # chip, revision, features, crystal, MAC
   python -m esptool --port <port> flash_id    # flash maker and size
   ```

   Keep your own MAC to board list somewhere private. Never in a repo, a skill or a post. A board with our firmware says
   its build and MAC to `OWLOS HELLO` (`OWLOS READY flight-radar <version> <build> <mac>`): ask before flashing an unknown
   MAC.
3. Check the chip against what you think it is. The label lies: a "2432S028" with a C3 on it took the round build.
4. Only then flash. If what is on it matters, read it off first: `read_flash 0 ALL <file>` (a CH340 fails reads at 921600,
   use 460800). Ask before flashing a board that is not plainly the one in the task. A port that says "Access is denied"
   is held by a browser tab with Web Serial: only the person at that browser can let it go.
5. A flash writes three parts here: count "Hash of data verified." three times, and only then it is done. Gate on the build
   too: a failed IDF build leaves the old .bin and esptool flashes it happily, so go on only after "Project build
   complete".
6. Opening the port resets a board with a bridge, even with DTR and RTS off. A long logger beats short peeks. The 5.79"
   e-paper printed nothing after flashing until RTS was toggled.
7. Keep the board's settings across a flash: read them before (`OWLOS GET`, compare field names, print no values) and
   compare after.

More on esptool, ports and the browser flasher: esp32-flash-and-settings.

## BOOT

Every board here reads BOOT as a user button when it can (button.c: pull-up on, low = pressed, polled from the screen
task, under 30 ms is bounce, 700 ms is a long press). Holding BOOT while pressing RST wakes the flasher and the screen
stays dark: say so wherever a feature asks for a BOOT press. No BOOT the firmware can read on the Tab5 (it does not reach
the P4), the plain CrowPanel 5.0 and 7.0 (GPIO 0 is the panel's clock: pressing it while the screen runs shorts the
clock) and the Waveshare 7/7B (a panel data line).

## Traps

- Site: "Your ESP32-S3 board is not supported": a card's manifest has one chipFamily, the chip has no build there. The
  right card, or a build for that board.
- 2.8" shows light blue on white, upside down: a v3 on the v1 start-up. BOOT in the first 8 s.
- Wrong colours or mirrored on any CYD: the wrong profile, the same fix. First suspect for any "looks wrong" report.
- Screen stays dark after holding BOOT at RST: that is download mode.
- Red never lights on a "CYD": an LCDWIKI board, red on 22. Never drive 4 there: it is the amplifier's enable.
- A GPS kit breaks flashing: the module is on TX/RX. Take it off to flash and to set up.
- C3 resets every ~29 s, only with the map: one core, the fetch task starved IDLE. Fetch at the screen's priority.
- A new classic board hangs silently, no log: flash at 80 MHz hung a 2.8" once, cause not found. Try DIO 40.
- sdkconfig change does nothing: defaults are read only when the sdkconfig is made. Delete the build's sdkconfig. A board
  that needs the new lines can refuse an old folder with an `#error` (the Waveshare 7 hung on a dark screen without).
- `SDKCONFIG_DEFAULTS` ignored: set after include(project.cmake).
- C5 build refused locally: a preview target on the v5.5 tag. `idf.py --preview`.
- CMake refuses a build folder: copied from another path, the old path in its cache. BUILD.BAT starts such a folder fresh.
- Flashed, but nothing changed: a failed build, the old .bin flashed. Gate on "Project build complete".
- First command lost, or a reset at READY: junk from the reset glues to it; a SET before the task exists. Send "\n"
  first, start the console last, take the line from "OWLOS " on.
- No port after flashing a native USB board: the chip's USB restarts with the chip. Connect again, BOOT held while
  plugging if it will not.
- A board with PSRAM runs as if it had none: by design where PSRAM is asked for by name (`IGNORE_NOTFOUND`,
  `USE_CAPS_ALLOC`). Check the total if a feature needs it. On the C5, RGB and P4 builds PSRAM is required.
- `esp-aes: Failed to allocate memory`, every TLS answer fails: TLS buffers in PSRAM and no internal DMA RAM for the AES's
  bounce buffer (C5, S3). Smaller strips, or `CONFIG_MBEDTLS_HARDWARE_AES=n`.
- "not enough memory for the screen" on a board with 8 MB of PSRAM: the strips are in internal RAM and the WiFi and TLS
  took it. Move TLS, WiFi buffers and big pictures to PSRAM.
- GPIO 33 to 37 dead on an S3: octal PSRAM owns them.
- The chip's own USB silent on a board: its pins are used for something else (`NO_CHIP_USB`).
- esp_lcd refuses an RGB panel: the frame's rows do not divide by the bounce buffer's (272 % 10).
- Screen and touch hang on a shared bus: a transfer while a strip is in flight. Wait for the strip first.
- SD card corrupts a shared bus: its CS floats. `PIN_SD_CS` up before the bus starts.
- A near build on a board with the same chips: the NM-CYD-C5 build on a ZLX-ESP32-2 holds its screen select up and drives
  its amplifier enable. Check the pins, not the chips.
- ILI9486 stays grey: esp_lcd's 8-bit commands. Every byte padded to 16 bits with its own CS pulse.
- `i2c_master` never returns on a classic ESP32: it spun after a NACK (Air Quality UI). Bit-banged I2C there.
- CST816 stops answering: it dozes after idle seconds. Register 0xFE = 1.
- A GT911 read comes back empty: some panels want a stop between the register and the read (`GT911_STOP`).
- Link error on `touch_init`: a clash inside librtc.a. Call them `tp_*`.
- A docs page says GT911, the touch is silent: a leftover in the maker's repo. Follow the running code.
- The JC8012's touch dead after flashing from the site with erase: its touch firmware lives in NVS. Install it again.
- Old data read back as garbage after a port: a partition at an address another firmware used. A magic at the start of
  each record (Thermo Relay's "THRM" at 0x190000, where the radar left "SLOT").

## Not known yet

- The chip's own USB path under Flight Radar: no S3 or C5 board here has been recorded talking over it.
- Whether the C5 boards run now: the 12-row strips that should let TLS through are built, not run on one.
- Whether the RGB boards' picture shakes while the map's tiles are written: XIP from PSRAM is meant to stop it.
- Whether the round CrowPanel has a CH340 or the C3's own USB: the notes disagree (references/boards/gc9a01.md).
- The red LED's pin on the desk's 2.8" boards: the radar's setting says 4, LCDWIKI's page for the E32R28T says 22.
- The C5's full strapping table, the S3's flash voltage strap: read from secondary sources, see the chip files.
- Why the JC8012's flash runs at 40 MHz when 80 is asked, and why the Waveshare 7B dims by itself.
