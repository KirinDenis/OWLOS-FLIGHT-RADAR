# ESP32 Web Flasher: the minimal template for any ESP32

All of it copied in `../files/web-flasher/`. The smallest whole road from a push to a board on WiFi, for any
ESP32, S2, S3, C3 or C6: Arduino firmware built by GitHub Actions, published by Pages next to the page that
flashes it, a terminal in the page, WiFi over the cable. Why it exists: web flashing looks like magic to most
ESP32 people, and few know a branch can build firmware and serve its own flasher. It is the template to start any
ESP32 project's flasher from; the Flight Radar (`site.md`) and FusionEdge (`platformio-pipeline.md`) show where it
grows.

How sure:

- The steps of the workflow, run locally: the five chips built with arduino-cli, `public/` assembled by a copy of
  the workflow's last steps, flashed from the manifest on an ESP32 with a CH340 and an ESP32-C3 on its own USB,
  both restored after. Measured.
- **It has no git and is not on GitHub yet** (checked 2026-11-23: no `.git`). Its `pages.yml` has never run on
  Actions. The README's link to its Pages site will work after the owner pushes it. A video tutorial is planned.

The files and where they go:

- `pages.yml` goes to `.github/workflows/pages.yml`.
- `index.html`, `manifest.json` go to `site/`.
- `firmware.ino`, `version.h` go to `firmware/`.
- `gitignore.txt` goes to `.gitignore` (`build/`, `public/`, `firmware/build/`).

Make your own, in order:

1. Copy the repository (GitHub template, fork, or these files). Keep it public: Pages is free for public repos.
2. **Settings, Pages, Build and deployment, Source: GitHub Actions.** Once.
3. **Actions, Build and publish, Run workflow.** A fork asks first to allow workflows. Two jobs, `build` then
   `deploy`.
4. In `site/index.html` point the two repo links at yours.
5. Open the page in Chrome or Edge, CONNECT AND FLASH, pick the port, yes to erase the first time. Close the
   window.
6. CONNECT in the terminal, `help`. Fill in the WiFi, SEND TO THE BOARD. The terminal says the board's address.

What differs from the radars, and why:

- **Arduino, not IDF**: a beginner opens `firmware.ino` in the Arduino IDE. The core is pinned, `esp32:esp32@3.3.5`:
  the same build every time.
- **One manifest with a build per chip**: the firmware fits all boards, so ESP Web Tools picks. Paths are
  `fw/<chip>/...` from the manifest at the site's root.
- **Four parts, not three**: bootloader, `partitions.bin` at 0x8000, `boot_app0.bin` at 0xE000 (says "start the
  first app"), the app at 0x10000. The bootloader at 0x1000 on ESP32 and S2, 0 on the others. In the manifest by
  hand here, the core does not write a `flash_args`.
- `arduino-cli compile --build-path`, not `--output-dir`: on Windows `--output-dir` exits 1 from the core's
  boot_app0 export hook (measured).
- `CDCOnBoot=cdc` on chips with their own USB, and the sketch talks and listens on both `Serial` and `Serial0`: it
  cannot know which one is plugged in.
- **Not `merged.bin`**: Arduino's is the whole 4 MB flash, and each update would wipe the WiFi (`bin-files.md`).
- `version.h` is written by the workflow: `build N (sha)`. `@VERSION@` is replaced in `index.html` AND
  `manifest.json`.
- Plain words on the serial line, no JSON (`usb-console.md`, the minimal form).
- `ESP.getChipRevision()` 301 is v3.1: major times 100 plus minor.

To go on from it: another sketch in `firmware/` as `firmware.ino`, libraries by `arduino-cli lib install`, board
options after the chip (`esp32:esp32:esp32s3:CDCOnBoot=cdc`), fewer chips out of the loop and the manifest. For
ESP-IDF, take the radars' `pages.yml` (`ci-and-versions.md`). PlatformIO: `pio run`, files in `.pio/build/<env>/`,
addresses from `pio project metadata` and the partition table (`platformio-pipeline.md`). Page and publishing stay
the same whatever builds the files. A board per screen on the same chip: a manifest per board (`web-flasher.md`).

The terminal in the page, in its shortest form, and a restart with no button on the board:

```js
const port = await navigator.serial.requestPort();      // the browser's own list of ports
await port.open({ baudRate: 115200 });
// the auto-reset wiring of ESP32 boards: RTS low pulls EN, the reset, while DTR leaves GPIO0 alone
await port.setSignals({ dataTerminalReady: false, requestToSend: true });
await sleep(100);
await port.setSignals({ dataTerminalReady: false, requestToSend: false });
```

To open WITHOUT a restart, drop RTS first and then DTR, as the Flight Radar's page does (`usb-console.md`).

The read loop ends with an exception when a native USB board restarts, because its port goes away: say so and
offer CONNECT. `navigator.serial` fires `disconnect` for an unplugged board, and `connect` when one comes back on a
port the page was allowed.

Local test rig: `public/` assembled by the workflow's last steps, served on localhost, and
`../files/rigs/flash-manifest.ps1 -Family ESP32-C3` to flash one chip's build from the one manifest, MAC and chip
checked, `-Backup` and `-Restore` around it.
