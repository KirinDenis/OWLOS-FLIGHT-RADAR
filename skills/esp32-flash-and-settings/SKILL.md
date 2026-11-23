---
name: esp32-flash-and-settings
description: Getting firmware onto ESP32 boards and settings into them from a public web page. Use when flashing with esptool or idf.py (a build folder per board, flash_args, bootloader offsets by chip, partition tables), refusing to flash until the chip and MAC match, adding a browser flasher over Web Serial (ESP Web Tools manifests and chipFamily, or an own esptool-js flasher that detects the chip), building in GitHub Actions and publishing on GitHub Pages (ESP-IDF docker matrix, PlatformIO, a WebAssembly twin), versions from CHANGES.md, a beta channel beside the stable, keeping old versions flashable, .bin files and merged images, settings over USB serial (a line protocol, a site console with a command contract), settings in NVS through updates and downgrades, testing a flasher site with a fake Web Serial board, or when it fails - "not supported" in the flasher, settings lost after an update, 403 on a release, a busy port, a board left dark.
---

# ESP32: flashing and settings

How firmware gets onto a board and settings get into it, from a page anyone can open, in a family of ESP32
firmwares with a screen. Most of it comes from OWLOS Flight Radar (19 builds, 17 of them on its site, a stable and
a beta channel, its own esptool-js flasher, a console on the page). The rest from OWLOS WiFi Radar, the Thermo
Relay, ESP32 Web Flasher (the minimal template) and a fork of FusionEdge (the same road for a PlatformIO
project). Written 2026-11-04, brought up to date 2026-11-23. It works in a new repository with none of those at
hand: the real files are in `files/`, each topic in `references/`.

## The whole road

```
CHANGES.md "## 0.16.2 (beta)" ------------- the version and its channel, one line feeds everything
firmware/ ---- BUILD.BAT ---- esptool ----- a board on the desk, chip and MAC checked, hashes counted
    |
push to main (a pull request: built, not published)
    |
GitHub Actions: a job a board in espressif/idf:release-v5.5, and the screen code for WebAssembly
    |            fw-<board>, elf-<board>, web artifacts
    v
site job: page + manifests from flash_args + a beta beside the stable + older versions from the live site
    |            fw/<board>/   fw/<version>/<board>/   fw/versions.json   version.json
    v
GitHub Pages
    |
Chrome or Edge: the page asks the board (HELLO, or the chip's ROM loader), offers its cards, flashes the
    |           parts at their offsets (no to erase keeps NVS)
    |
the same page, Web Serial: OWLOS HELLO, SET {...}, GET, a live twin of the screen, a console
    |
NVS: versioned blobs, a key per new setting, strings apart, read back after every update
    |
the board on WiFi asks version.json twice a day: a NEW VERSION note (a beta board hears of betas), ABOUT, a QR
```

## The decisions, and why

- **Parts, not a merged image.** Bootloader, partition table and app at their offsets leave NVS alone, so an update
  keeps the settings. A merged image covers NVS: for a first install only.
- **Settings go over the cable, not in the firmware.** A firmware on a public page is the same file for everyone and
  cannot carry anyone's WiFi. The cable that flashed it is already there and goes nowhere else.
- **The page does both.** A person flashes and sets up without installing anything. The console is plain text at
  115200, so a script, a terminal or a model can do what the page does.
- **Ask the board before flashing it.** A board with our firmware names its build and MAC; any other answers its
  chip and MAC from the ROM loader. The page offers only the cards of that chip. On the bench, nothing writes
  until the chip and the MAC both match a private list.
- **A build and a card per board, not per chip.** Nobody can ask which screen hangs on the bus, so the person picks
  by a picture, and BOOT steps through a board's screen start-ups. Where one firmware fits every board of a chip,
  one manifest with a build per chipFamily lets ESP Web Tools pick by itself.
- **Offsets are read from `flash_args`, never typed.** Bootloader at 0x1000, 0x0 or 0x2000 by chip.
- **One version line.** The first `## ` of `CHANGES.md` is read by CMake, the workflow, the PC preview, the web twin
  and the page, so they cannot disagree. `version.json` has no build number, so a doc fix does not tell every board
  there is news.
- **Channels.** `(beta)` on that line puts the build beside the stable, not over it. `version` stays the stable for
  every board; only a beta build reads `beta`. A board new in the beta gets the beta.
- **No OTA.** One factory app, the rest of the flash for data. The board says a newer version exists and the person
  flashes.
- **Old versions live on the site itself, not in GitHub Releases.** The workflow's token may not tag commits that
  change `.github/workflows`. A run that cannot carry every old version forward stops before deploy. Same number
  replaces, new number keeps the old.
- **A key per new setting; a blob for what changes together; strings apart for secrets.** A blob migrates by reading
  its old size over the defaults. Keys and strings need no version, so the WiFi survives a downgrade, and that is
  how a person gets back.
- **The password goes in, never out.** GET does not carry it, the log does not name it, the rigs and the page's log
  mask it.
- **Every command says its effect.** The site's console declares for each command whether it reads, writes, deletes
  or changes the setup, so a model working in the page asks the person where it must.

## Where everything is

References, one topic each:

- `references/local-flash.md`: BUILD.BAT and build.sh, a build folder per board, sdkconfig defaults per board, the
  preview flag, the moved-folder cache, building several boards, esptool lines, `flash_args` by chip, flash speed,
  verifying, offsets by chip, the partition tables (4 MB, 2 MB C3, 16 MB P4, core dump on flash), what a flash
  touches and leaves, what a killed flash leaves.
- `references/chip-and-mac-check.md`: why and how to refuse a write until chip and MAC match, a board that names
  itself, what is on a board without reading NVS, backups, ports that are not free, the bench rules.
- `references/web-flasher.md`: ESP Web Tools 10.2.1 from its source (slots, CSS, chips, each step of a click, the
  erase logic), the own esptool-js flasher (detecting the chip, writing with an MD5 check, the restart, a board that
  comes back on a new port), the manifest, one per board or one per chip, erase in the person's words.
- `references/usb-console.md`: the line protocol, `console.c` on UART0 and USB-Serial-JTAG, the JSON reader and its
  fixes, the handler, the page's `class Link`, the settings form, the traps as met, `cfgrig`, the minimal form, a
  firmware's own file protocol.
- `references/site-console.md`: the site's shell over the boards, `/boards/NAME/config`, the command contract and
  its effects.
- `references/nvs-settings.md`: `store.c`, blobs and keys, migration, the typedef of an old layout, adding a setting,
  the downgrade, many networks, between firmwares, settings in files instead.
- `references/ci-and-versions.md`: `pages.yml` line by line, the web job, `make-manifests.sh`, the version line and
  its six readers, the board's check, ABOUT and QR, crash backtraces, release rehearsal, job logs, Pages' 503s.
- `references/beta-and-stable.md`: the channels, end to end, the rules `archive.sh` keeps, the page's choice, the
  release routine, a contribution while a beta is out.
- `references/archive.md`: the archive of old versions, the story of the two failed runs, `archive.sh` block by
  block, the page's picker, how it was tested with no gh, jq, zip, Docker or WSL, FusionEdge's variant.
- `references/bin-files.md`: direct links per board, a merged image and why it is for first installs only,
  FusionEdge's checked one-file image and its file list on each card.
- `references/platformio-pipeline.md`: the same road for a PlatformIO firmware: PlatformIO on the build PC, CI with
  board option folders, addresses from metadata and the partition table, boot_app0, LittleFS in the manifest and
  what it costs, the no-PSRAM boards as far as flashing goes.
- `references/site.md`: the page since 0.8 (one CONNECT button, every board at once, what it remembers), the
  previous page's structure, cards, the boot section, checking a local copy.
- `references/template.md`: ESP32 Web Flasher, the minimal template for any ESP32 (Arduino, one manifest per chip,
  a terminal in the page).
- `references/fake-board-testing.md`: `fakeserial.js`, a shim in a test copy, the hidden browser pane, the twin
  (the board's screen code in WebAssembly) as a test with no board.
- `references/traps.md`: every trap: symptom, cause, fix, how sure.

Files, see `files/README.md`: `files/flight-radar/` (ESP-IDF: the build, the workflow, the archive, the console,
NVS, the page's flasher and serial link, the fake board), `files/web-flasher/` (Arduino, the template),
`files/rigs/` (flash, back up, build several, the settings rig, the site check, a fake gh).

Other skills: the chips (`../esp32-boards/references/chips/`), each board (`../esp32-boards/references/boards/`),
public text and the owner's working rules (`../esp32-project-playbook/SKILL.md`).

## A new repository, in order

**The smallest thing (one firmware for all boards of a chip, few settings):** copy `files/web-flasher/`, follow
`references/template.md`. Three parts of the road are then done: Actions build, Pages, the flash button.

**A firmware with a screen, on ESP-IDF:**

1. `firmware/CMakeLists.txt`, `partitions.csv`, `sdkconfig.defaults*`, `BUILD.BAT`. Keep NVS at 0x9000, size
   0x6000.
2. `CHANGES.md` with a `## 0.1`, the version block of `CMakeLists.txt` (with the beta word).
3. `.gitattributes` (`*.sh`, `*.yml` LF, `*.BAT` CRLF).
4. A build on the desk, a flash after the chip and MAC check: `flash-one.ps1`, a backup first with
   `backup-board.ps1`.
5. `store.c` with a version field in each blob from day one, secrets on their own keys, a key per later setting.
6. `console.c`, `on_line()`, READY with the build and the MAC as the LAST thing in `app_main`. Check it with
   `cfgrig.ps1`.
7. `.github/make-manifests.sh`, `archive.sh` with an empty seed file, `workflows/pages.yml`. Set the app's name in
   all three.
8. The page: either cards with `<esp-web-install-button manifest="fw/<board>/manifest.json">` and
   `new_install_prompt_erase` in the manifests, or the own flasher (`flasher.js`, `link.js`, `md5.js`) with a board
   list by chip. Settings over `class Link`. `@VERSION@` in the page.
9. **Settings, Pages, Source: GitHub Actions.** Push. `check-site.sh https://you.github.io/repo`.
10. The board's `update_check()`, the note, ABOUT, a QR baked for the site's address.
11. A fake board for the page's tests (`fakeserial.js`, `assemble.sh --test`).

**A PlatformIO firmware:** the same, with `references/platformio-pipeline.md` for the build and the addresses.

**Adding a board** touches four places: BUILD.BAT's table, the matrix in `pages.yml`, the `case` in
`make-manifests.sh`, the site's board list (a card, its chip, its screen size). Its chip decides the `chipFamily`,
the build decides the offsets. Say "experimental" or "not run on one here yet" until it has run.

**A new version:** a `## ` section at the top of `CHANGES.md`, a higher number, push. A push with the same number
replaces that version's firmware on the site. **A beta:** the same with `(beta)` on the line. **A release:** take
`(beta)` off. `references/beta-and-stable.md`.

## The traps that cost the most

Full list in `references/traps.md`.

- `new_install_prompt_erase` left out: ESP Web Tools erases by itself, and every update wipes the settings.
- "Your ESP32-S3 board is not supported": a card with no build for that chip, not a broken board.
- 403 "Resource not accessible by integration" on POST /releases: the token cannot tag commits that touch
  workflows. A fake `gh` passed all of it. Read the REST docs for the permission first.
- A failed build leaves the old `.bin`, and esptool flashes it. Gate on `Project build complete`, count 3 of 3
  `Hash of data verified`.
- COM numbers move, and a loose guard once let a wrong port through. Chip AND MAC first, a backup first.
- READY said before the system is up: the page's SET is lost or resets the board.
- Opening the port resets a CYD when DTR and RTS drop together, and the junk glues onto the first command. Drop RTS
  first, send `\n` first, and let the board look for `OWLOS ` inside the line.
- A port is held by the flasher's dialog, a monitor, or the site in a browser tab: esptool says "Access is denied".
- The USB-Serial-JTAG driver takes the log's way out. Read the FIFO with the LL functions.
- A field added in the middle of a settings struct, or an array that grew, loses every board's settings. Add at the
  end with `offsetof` and `_Static_assert`, keep the old layout as a typedef, or give the setting its own key.
- A beta with no channel reaches every board. `version` in `version.json` stays the stable, for ever.
- Settings kept in a filesystem image go with every install that writes that image.

## How sure

Marked in each file with the same words: measured, built, docs, reasoned. In short:

- **Measured on boards**: esptool flashing and verifying, 460800 on a CH340, the chip and MAC check, the console
  over UART0 on ESP32 and C3 boards through a CH340, the settings surviving every update without erase on the bench
  (0.2 to 0.3 first, 0.16.2 last), READY and `\n` fixes (2 of 6 to 6 of 6), HELLO about 2.7 s late, `version.json`
  asked by a board, the NEW VERSION note, the QR read back, PlatformIO from Git Bash failing.
- **Measured on the live pipelines**: on 2026-11-04 the Flight Radar site had 3 versions, 7 boards in the newest,
  61 files all answering 200. On 2026-11-23: the stable 0.15 with 16 boards, 13 versions kept, the beta 0.16.2
  with 17 boards, 586 files, 0 bad; the P4 manifests with the bootloader at 8192. Runs 4 and 5 failed on releases
  as told. `Cache-Control: max-age=600`. Job logs 403 without auth. FusionEdge's site: 1.1.1 for 7 boards, each
  with a checked one-file image.
- **Measured in the browser**: the page's board path, the console and the channel choice against a fake board.
- **Built, not run on a board here**: the S3, C5 and P4 builds of the Flight Radar (on the site, marked
  experimental where nobody has reported a run), the console over USB-Serial-JTAG on a board with only that port,
  FusionEdge's S3 builds, the chip patterns for C5 and P4 in the rigs.
- **Docs, read from source**: ESP Web Tools 10.2.1's flow, the own flasher's steps, IDF's offsets and targets, NVS's
  read rules, the REST docs on Create a release, what a killed flash leaves, the RTS-first open.
- **Not done yet**: ESP32 Web Flasher is not on GitHub, its workflow never ran there. The Flight Radar's page has
  no list of .bin files and no merged image. The rigs were cleaned for this skill and not run again after.

## Ideas talked about, not built

- OTA self-update with signing, rollback and staged rollout: it needs two app slots, so the data partition shrinks.
- Opt-in crash reports from the page: CI already keeps an ELF per build to decode them.
- A portal for many firmwares: a build key from the firmware's files, components, board file, sdkconfig and IDF
  version, so only what changed is rebuilt and a new version reuses unchanged builds; after a flash, the page reads
  the board's log for about 10 s and says took off, crashed (boot loop, panic) or not seen (silent, or a native USB
  board on a new port), and a brownout is the cable, not the firmware.
- Detection in tiers before any reset: the USB chip from `getInfo()`, then HELLO if our firmware is there, then the
  MAC remembered in the browser, and only then the ROM loader, which restarts the board.
