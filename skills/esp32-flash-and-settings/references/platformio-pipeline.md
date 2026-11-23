# A PlatformIO project's road: FusionEdge

The same road as the Flight Radar's (Actions build, Pages, a browser flasher, older versions kept, WiFi over the
cable), for an Arduino firmware built with PlatformIO. Source: the FusionEdge fork with a browser flasher,
`github.com/KirinDenis/FusionEdge` (`.github/workflows/flasher.yml`, `.github/collect.sh`, `make-manifests.sh`,
`board-data.sh`, `archive.sh`, `site/index.html`, `site/wifi.js`), forked from `github.com/SimZs/FusionEdge`, an
ESP32 internet radio under GPL-3.0. Its maintainers agreed to the flasher. Its boards are described in
`../../esp32-boards/references/boards/` (one file each).

How sure: the pipeline runs on GitHub; on 2026-11-23 its live site served version 1.1.1 (build 32) for 7 boards,
each manifest with 5 parts and a zipped one-file image (measured: fetched from the site). The two boards without
PSRAM ran it on the bench (measured: the screen, MP3 streams, the touch on the 3.5"). The S3 boards: built, not
run here (no S3 board of theirs on the desk).

The code of this fork is GPL-3.0. Nothing of it goes into the MIT repositories of this family; the other way is
fine, and the Flight Radar's MIT drawing code is in the fork with its licence file.

## The build PC: PlatformIO from PowerShell

- `pio run` started from Git Bash fails in the middle of a build with "ERROR: MSys/Mingw is not supported"
  (measured, PlatformIO 6.1.18, pioarduino 55.03.37). Run it from PowerShell:
  `cmd /c "pio run -e <env> > build.log 2>&1"`.
- Set `PLATFORMIO_CORE_DIR` to a folder of its own, so someone's `~/.platformio` (with another project's pinned
  platform in it) is never touched. The first install there is about a gigabyte and several minutes (measured).
- pioarduino's esptool is its own, version 5.1.2, not the one on PyPI. It spells commands with hyphens:
  `write-flash`, `merge-bin`, `image-info`.
- `pio project metadata` has `flash_images` (bootloader, partitions, boot_app0) but no application offset
  (measured): the app's offset comes from the partition table (below).
- Pin the platform by its exact release, as the board's `platformio.ini` does:
  `platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip`
  (Arduino 3.3.7 on ESP-IDF 5.5.2). An old project shows why: the old OWLOS firmware needs `espressif32@1.12.3`
  (Arduino-ESP32 1.0.4), and its kernel checks `ARDUINO_ESP32_RELEASE_1_0_4`, so a newer core builds a firmware
  that does nothing (read from its source; the platform installed then was 6.12.0).

## flasher.yml

```yaml
strategy:
  fail-fast: false
  matrix:
    include:
      - { board: s3-ili9488,   options: "",                      chip: esp32s3 }
      - { board: jc3248w535,   options: AXS15231B_options,       chip: esp32s3 }
      - { board: cyd28,        options: ESP32-2432S028R_options, chip: esp32 }
      # ... 7 boards
steps:
  - uses: actions/checkout@v4
  - uses: actions/setup-python@v5
    with: { python-version: "3.12" }
  # the platform, the Arduino core and the toolchain are about a gigabyte: kept between runs
  - uses: actions/cache@v4
    with:
      path: ~/.platformio
      key: pio-${{ matrix.chip }}-${{ hashFiles('platformio.ini', '*/platformio.ini') }}
  - run: pip install platformio==6.1.18
  - name: Build
    run: |
      if [ -n "$OPTIONS" ]; then
        cp "$OPTIONS/myoptions.h" myoptions.h
        [ -f "$OPTIONS/platformio.ini" ] && cp "$OPTIONS/platformio.ini" platformio.ini
        # a 4 MB board takes part of data/ into its LittleFS image (data_dir = build-data)
        [ -f "$OPTIONS/data.txt" ] && bash .github/board-data.sh "$OPTIONS"
      fi
      ENV=$(sed -n 's/^\[env:\(.*\)\]/\1/p' platformio.ini | head -n 1)
      pio run -e "$ENV"
      pio run -e "$ENV" -t buildfs
      pio project metadata -e "$ENV" --json-output --json-output-path meta.json
```

- The project describes a board by a folder of options that its README tells people to copy over the root files
  by hand. The job does the same copy, in its own checkout only: nothing in the source tree changes.
- `ENV` is the first `[env:...]` of the `platformio.ini` in place.
- The 4 MB boards cannot hold all of `data/` in their filesystem: `data.txt` in the board's folder lists what
  goes in, one path a line, and `board-data.sh` fills `build-data/`, which that board's `platformio.ini` names as
  `data_dir`. The fonts go into the firmware there.
- The cache key by chip and by every `platformio.ini`: a board that changes its platform gets a new cache.
- The rest is the Flight Radar's shape: an `elf-<board>` artifact for 90 days, a site job with `!cancelled()`,
  `configure-pages`, `archive.sh`, a `site-fw` second copy, `deploy-pages` (`ci-and-versions.md`).
- The version is `FW_VERSION` in `src/core/options.h`, the one the firmware shows; the text is
  `VER, build N (sha)`. The page gets `@VERSION@` and `@REPO@` replaced, so a fork's page links to the fork.

## collect.sh: the parts and their addresses, never typed

```bash
# the bootloader, the partition table and boot_app0, at the addresses PlatformIO itself writes them
images=$(grep -o '"flash_images": \[[^]]*\]' meta.json | grep -o '{[^}]*}' || true)
while read -r addr path; do
  path=${path//\\\\//}                       # a Windows path in JSON: C:\\x -> C:/x
  cp "$path" "$OUT/"
  echo "$addr $(basename "$path")" >> "$OUT/flash_args"
done < <(echo "$images" | sed -n 's/.*"offset": "\([^"]*\)", "path": "\([^"]*\)".*/\1 \2/p')

# the firmware and the LittleFS image go where upload and uploadfs put them: the first app partition
# (app0, the one boot_app0 starts), and the data partition of subtype spiffs or littlefs
table=$(sed -n 's/^board_build\.partitions *= *\([^ ;]*\).*/\1/p' platformio.ini | head -n 1)
app=$(awk -F, '$2 ~ /^ *app *$/ { gsub(/ /, "", $4); print $4; exit }' "$table")
echo "$app firmware.bin" >> "$OUT/flash_args"
fs=$(awk -F, '$2 ~ /^ *data *$/ && $3 ~ /^ *(spiffs|littlefs) *$/ { gsub(/ /, "", $4); print $4; exit }' "$table")
echo "$fs littlefs.bin" >> "$OUT/flash_args"
```

- It writes a `flash_args` of its own, `<address> <file>` a line, so `make-manifests.sh` and the archive work as
  in the IDF project.
- **boot_app0** at 0xE000 sets the OTA data to start app0, the slot the firmware goes to. A radio that updated
  itself over WiFi into app1 would otherwise go on starting the old firmware there. Any layout with an
  `otadata` partition needs it in the flasher's parts.
- Then the checks and the one-file image: `bin-files.md`.

A live manifest, the 2.8" CYD: bootloader at 4096, `partitions.bin` at 32768, `boot_app0.bin` at 57344,
`firmware.bin` at 65536, `littlefs.bin` at 3407872 (0x340000).

## The partition tables

```
# 16 MB, the PSRAM boards                      # 4 MB, the boards without PSRAM
nvs,      data, nvs,     0x9000,    0x5000     nvs,      data, nvs,     0x9000,    0x5000
otadata,  data, ota,     0xE000,    0x2000     otadata,  data, ota,     0xE000,    0x2000
app0,     app,  ota_0,   0x10000,   0x600000   app0,     app,  ota_0,   0x10000,   0x330000
app1,     app,  ota_1,   0x610000,  0x600000   spiffs,   data, spiffs,  0x340000,  0xC0000
spiffs,   data, spiffs,  0xC10000,  0x3D0000
coredump, data, coredump,0xFE0000,  0x20000
```

- NVS is 0x5000 here, not the 0x6000 of the IDF projects. A board that goes from one family to the other gets
  the other's table and NVS bytes it does not know: yes to erase, or a backup put back (`local-flash.md`).
- The 4 MB boards have one app slot with the fonts inside it and no second slot to update into: their firmware
  is updated from the flasher site or over USB, never over WiFi.

## The page: one INSTALL, and what it costs

The manifest names every part, the LittleFS image too. So INSTALL writes the firmware AND the radio's files: its
web pages, fonts, pictures, the station list and the WiFi list. About 6 MB go over the cable: several minutes.

- ESP Web Tools then asks whether to erase. Yes on a new board or one that had other firmware. No for an update:
  the radio's settings in NVS stay. The station list and the WiFi list still go back to the ones it came with,
  so the page says: export them from the radio's web page first, import them after.
- Updates of the firmware alone, the lists kept, go through the radio's own web page, Update, with the
  `firmware.bin` from the board's card (the boards with two app slots).
- An earlier page wrote a second manifest a board. Told the cost of each way, the owner chose one button and the
  erase question: `make-manifests.sh` writes `manifest.json` only. A Screen switch between two builds of a board
  went too: one build a board.
- WiFi after the flash goes over the same cable, through the firmware's own file protocol (`usb-console.md`).
  Without the cable the radio opens its own access point to be set up from a phone.

## The two boards without PSRAM, as far as flashing goes

The fork added two classic ESP32 boards with 4 MB and no PSRAM (the 2.8" CYD, which also runs LCDWIKI's E32R28T,
and LCDWIKI's 3.5" E32R35T). All of it sits behind macros set in the board's own `platformio.ini`
(`BOARD_NO_PSRAM`, `LITE_UI`, `AUDIO_INTERNAL_DAC`, `AUDIO_DAC_LEVEL`, `LITE_SCREEN_W`, `LITE_SCREEN_H`), so the
S3 builds stay the same code. That was proved, not assumed: the objects of an S3 build before and after were
compared with `objdump`, addresses masked, and came out the same (measured). Use that proof for any port that
must not touch the boards already shipped.

Pins, the screen, the sound and the memory of these boards: their board files in
`../../esp32-boards/references/boards/`.
