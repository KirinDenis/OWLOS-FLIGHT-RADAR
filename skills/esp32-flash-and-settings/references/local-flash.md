# Local builds and flashing

Sources: OWLOS Flight Radar's `firmware/BUILD.BAT`, `firmware/build.sh`, `firmware/CMakeLists.txt`,
`firmware/partitions*.csv`, `firmware/sdkconfig.defaults*`, the build folders, and the working rigs of that
project. Real files: `../files/flight-radar/BUILD.BAT`, `build.sh`, `CMakeLists.txt`, `partitions*.csv`,
`../files/rigs/build-some.ps1`, `flash-one.ps1`, `flash-board.ps1`, `flash-manifest.ps1`.

Everything here is measured on boards unless it says otherwise. The build PC: ESP-IDF 5.5 tag, esptool 4.10
(shipped with it), Windows 10. Before any write to a board, the chip and MAC check: `chip-and-mac-check.md`.
PlatformIO builds have their own file: `platformio-pipeline.md`.

## BUILD.BAT: a build folder per board

One source tree makes a firmware per board. Usage:

```
BUILD.BAT                    the default board, build only
BUILD.BAT <port>             the default board, build, flash, open the monitor
BUILD.BAT st7796             another board
BUILD.BAT st7796 <port>      another board, flash it
```

Run it from PowerShell or cmd. From Git Bash `cmd //c BUILD.BAT` did not find idf.py. From a script:
`cmd /c "<repo>\firmware\BUILD.BAT st7796"`.

The file does these things in order, each for a reason:

- A table: build name to `NUM` (the `RADAR_DISPLAY` number) and `TARGET` (the chip). In the Flight Radar it
  has 19 builds now: ili9341, st7796, ili9486, gc9a01, es3c28p, nmcydc5, zlx2, crowadv28, crowadv50, crow50,
  jc8048w550, tab5, esp4827s043, crow70, wave7, wave7b, jc8012, epd400, epd792. Adding a board is one line
  here, one in the matrix of `pages.yml`, one in `make-manifests.sh`, one entry in the site's board list.
- `BDIR=build_<board>` and `SDK=build_<board>\sdkconfig`: each board has its own build folder and its own
  sdkconfig, passed with `-B` and `-DSDKCONFIG=`. WHY: switching a board would otherwise reconfigure and
  rebuild everything, and a chip change (`IDF_TARGET`) would throw the sdkconfig away. The default board keeps
  plain `build` and `firmware/sdkconfig` only because it always had them.
- `IDF_PATH` and `IDF_TOOLS_PATH` default to `%USERPROFILE%\esp\v5.5\esp-idf` and `%USERPROFILE%\.espressif`.
  IDF's Python goes first on PATH: the venv was made with IDF's own Python (`tools\idf-python\3.11.2`), not
  the system one.
- The moved-folder guard (next section).
- `--preview` for the C5 (the section after it).

`build.sh` is the same for macOS and Linux, for the five boards a contributor added (wave7, wave7b, crow70,
jc8012, crow50): `./build.sh wave7b [port]`. It looks for IDF in `$IDF_PATH`, then `~/esp/esp-idf`. Built by
its author on a Mac; not run here.

### The moved-folder cache trap

A build folder made where the project used to be keeps the old path in `CMakeCache.txt`, and CMake refuses
it. This came from moving a repo to a new name with the build folders inside. The guard:

```bat
set "HERE=%~dp0"
set "HERE=%HERE:\=/%"
if exist "%BDIR%\CMakeCache.txt" (
  findstr /i /c:"CMAKE_HOME_DIRECTORY:INTERNAL=%HERE:~0,-1%" "%BDIR%\CMakeCache.txt" >nul
  if errorlevel 1 (
    echo %BDIR% was made in another folder, starting it fresh
    rmdir /s /q "%BDIR%"
  )
)
```

`%HERE%` has forward slashes and a trailing one, hence the replace and `:~0,-1`.

### The preview flag

```bat
set PREVIEW=
if /i "%TARGET%"=="esp32c5" set PREVIEW=--preview
idf.py %PREVIEW% %ARGS% build
```

`tools/idf_py_actions/constants.py` of the 5.5 TAG lists esp32c5 in `PREVIEW_TARGETS`, so `idf.py` refuses it
without `--preview`. CI's `espressif/idf:release-v5.5` image is a later 5.5 and builds the C5 with no flag
(measured: the live site has C5 manifests). The flag goes BEFORE the other arguments and does nothing for
other chips.

### How the board and the chip reach the code

- `-DRADAR_DISPLAY=N` goes to `CMakeLists.txt`:
  `idf_build_set_property(COMPILE_DEFINITIONS "RADAR_DISPLAY=${RADAR_DISPLAY}" APPEND)`. `board.h` picks pins
  and drivers from it.
- `sdkconfig.defaults` is read for every board, `sdkconfig.defaults.<chip>` for that chip, by IDF itself.
- A board that differs from the rest on its chip sets its own list BEFORE `include(project.cmake)`. The
  Flight Radar has four such files now, each read after the ones before it:
  - `sdkconfig.defaults.s3lcd`: the S3 boards with an LCD (7, 9).
  - `s3lcd` then `sdkconfig.defaults.s3rgb`: the S3 boards with an RGB panel (10, 11, 13, 14, 15, 16, 18).
  - `s3lcd`, `s3rgb`, then `sdkconfig.defaults.wave7`: the Waveshare 7 and 7B (15, 16).
  - `sdkconfig.defaults.jc8012` after the P4 chip file: the Guition JC8012P4A1C (17).

```cmake
if(RADAR_DISPLAY EQUAL 7 OR RADAR_DISPLAY EQUAL 9)   # the S3 boards with an LCD
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.s3lcd")
elseif(...)                                           # the RGB boards: s3lcd, then s3rgb, then wave7 for 15 and 16
endif()
```

  The chip file is still read after each file of the list, the board's file comes after it and wins.
- These defaults are read ONLY when a build folder's sdkconfig is made. A changed default does nothing to an
  existing build. Delete `build*/sdkconfig` (and `firmware/sdkconfig`) to apply it. CI always starts clean, so
  a local build can differ from the site's for this reason alone.
- `sdkconfig` is in `.gitignore`. What matters is in the `sdkconfig.defaults*` files.

### Building several boards

`../files/rigs/build-some.ps1 -Repo <repo> -App <app> -Boards ili9341,st7796,gc9a01` builds them one after the
other and prints a line each: built (the words `Project build complete`), fresh (the `.bin` is newer than the
start of its build), warnings, errors, the app's size, the seconds. The full logs go to `build-<board>.log`.
"Fresh" is there because a failed build leaves the old `.bin` behind (see Verifying).

Measured on the build PC: the P4 board `jc8012` stopped at IDF configure with `MissingKconfigError` on the
local 5.5 tag, while CI's later 5.5 builds it (it is on the live site). A board that does not configure
locally is not a broken board: compare with CI first.

## esptool lines

IDF's Python, not the system one: `$py = "$env:USERPROFILE\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"`.

```
cd firmware\build_st7796
python -m esptool --chip esp32 -p <port> -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
python -m esptool --chip esp32 -p <port> -b 460800 read_flash 0 ALL backup.bin      # the whole flash
python -m esptool --chip esp32 -p <port> -b 460800 --after hard_reset write_flash 0 backup.bin
python -m esptool --chip esp32 -p <port> erase_flash                                # everything, settings too
python -m esptool -p <port> chip_id                                                 # no --chip: it finds the chip
```

- `@flash_args` is the file the build writes: one line of flash settings, then address and path of each part.
  Its paths are relative to the build folder, so run it there.
- 460800, not 921600: CH340 reads fail at 921600 (measured).
- A wrong `--chip` stops with a message and writes nothing. It once saved a board when a weaker MAC guard let
  a wrong port through (`chip-and-mac-check.md`).
- PlatformIO's esptool from pioarduino is version 5 and spells the commands with hyphens: `write-flash`,
  `merge-bin`, `image-info` (`platformio-pipeline.md`).

### The real flash_args, by chip

Read from the build folders (measured), and for the P4 from the live site's manifest, which CI makes from its
`flash_args`:

```
ESP32       --flash_mode dio --flash_freq 80m --flash_size 4MB
            0x1000 bootloader/bootloader.bin
            0x10000 owlos-flight-radar.bin
            0x8000 partition_table/partition-table.bin
ESP32-C3    --flash_mode dio --flash_freq 80m --flash_size 2MB    0x0 bootloader (the rest the same)
ESP32-S3    --flash_mode dio --flash_freq 80m --flash_size 4MB    0x0 bootloader (the rest the same)
            8MB on the Waveshare 7 and 7B; 40m on the e-paper S3 boards
ESP32-C5    --flash_mode dio --flash_freq 80m --flash_size 4MB    0x2000 bootloader (the rest the same)
ESP32-P4    bootloader at 8192 (0x2000), app 65536, table 32768: the Tab5's manifest on the live site
```

`dio` with `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` is right: the image header says dio and the second stage
bootloader switches the flash to quad itself. Do not "fix" it.

### Flash speed, by board

- `sdkconfig.defaults` says 40 MHz for every board. The chip files for the ESP32, C3 and C5 and the S3 LCD
  boards' file set QIO at 80 MHz; the P4 file sets 80 MHz. The e-paper S3 boards stay at 40 MHz.
- Measured both ways: the WiFi Radar's CYD hung silently at 80 MHz, so that project stays at 40 MHz DIO. The
  Flight Radar later went QIO 80 MHz on its three bench boards, and it was the cheap big win (the 3.5" radar
  124 to 99 ms a frame). Try it per board.
- The comment in `CMakeLists.txt` that says "40 MHz, at 80 the CYD hangs" is older than
  `sdkconfig.defaults.esp32`, which wins.

### Verifying

Count, do not skim. esptool prints `Hash of data verified.` once per file: three files, three lines. The
rigs stop when the count is not 3. esptool checks an MD5 of what it wrote, so this proves the flash holds the
file, not that the file is the one you meant:

- A failed IDF build leaves the old `.bin` in place and esptool flashes it happily. Gate on the words
  `Project build complete` in the build's output and on the exit code, as `flash-board.ps1` does, or on a
  `.bin` newer than the build's start, as `build-some.ps1` does.
- `Project build complete` with an old time on the `.bin` means nothing changed. The scripts print size and
  time.

After flashing, the board restarts by `--after hard_reset`. Boards on the chip's own USB may need RST by hand.
An e-paper board printed nothing after flashing until it was reset: toggle RTS on an open port.

## Offsets by chip

Where the ROM looks for the second stage bootloader (`components/bootloader/Kconfig.projbuild`,
`BOOTLOADER_OFFSET_IN_FLASH`, read from IDF 5.5; the ones marked were also measured on real builds):

- 0x1000: ESP32 (measured), ESP32-S2.
- 0x0: ESP32-C3 (measured), ESP32-S3 (measured), C2, C6, H2, C61.
- 0x2000: ESP32-C5 (measured in `flash_args` and the published manifest), ESP32-P4 (measured in the published
  manifest), H4. The first two sectors are kept for the key manager.

Partition table 0x8000 and the app at 0x10000 here (set by `partitions.csv`, the table's own offset is the IDF
default). In a manifest they are decimal: 4096, 8192, 32768, 65536. JSON has no hex.

WHY the offsets are read and never typed: `make-manifests.sh` takes them from `flash_args`. A hand-typed
bootloader offset boots on one chip and not on the next, and the C5 brought a third value, 0x2000. More on
each chip: `../../esp32-boards/references/chips/esp32.md`, `esp32-c3.md`, `esp32-s3.md`, `esp32-c5.md`,
`esp32-p4.md` in the same folder.

## The partition tables

The 4 MB one, for most boards:

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x200000
tiles,    data, 0x40,    0x210000, 0x1F0000
```

- One `factory` app, no OTA slots: the board never updates itself, it only says a newer version exists. That
  keeps 2 MB for the app and 2 MB for map tiles on 4 MB boards.
- `partitions_2mb.csv` for the 2 MB round C3 (`gc9a01`): app 0x150000 at 0x10000, tiles 0xA0000 at 0x160000.
  Its `sdkconfig.defaults.esp32c3` sets the 2 MB flash and this table. The app slot is 1 376 256 bytes and
  the app of the current tree (0.16.2 and after) 1 319 968 (measured on the build PC): about 55 KB to spare. Watch it on each version.
- `partitions_16mb.csv` for the P4 boards (the Tab5): app 4 MB at 0x10000, tiles 6 MB at 0x410000.
- `partitions_jc8012.csv`: the 16 MB layout and a core dump partition of 64 KB at 0xA10000.
- `partitions_wave7.csv` for the Waveshare 7 (8 MB) and 7B (16 MB): the 4 MB layout's app and tiles, and a
  core dump partition of 64 KB at 0x400000.
- The S3 boards with 16 MB (ES3C28P, CrowPanel Advance) and the C5 boards use the 4 MB layout as it is:
  simpler, and the extra flash is simply not used.
- NVS stays at 0x9000 size 0x6000 for life and in every project of the family. It is what lets settings
  survive updates, and lets one firmware read another's touch calibration (`nvs-settings.md`).
- The boards themselves: `../../esp32-boards/references/boards/gc9a01.md`, `tab5.md`, `jc8012.md`, `wave7.md`
  and the rest in that folder.

A core dump on flash (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`) can be read afterwards with
`idf.py coredump-info` even when nobody was watching the serial port (docs, from the boards' contributor; not
read off a board here). The other boards send the dump over UART at the crash (`CONFIG_ESP_COREDUMP_ENABLE_TO_UART`):
see the crash backtraces in `ci-and-versions.md`.

## What a flash touches and what it leaves

`write_flash @flash_args` writes three ranges. Everything else stays as it was: NVS (0x9000 to 0xEFFF),
phy_init, the tiles partition, any data partition. That is the whole reason updates keep settings.

It also leaves what ANOTHER firmware wrote in the same place. The Thermo Relay's history partition sits at
0x190000 where a WiFi Radar left its own slots. Any raw data partition needs a magic number at the start of
each record ("THRM" there against the radar's "SLOT"), and code that treats a wrong magic as empty.

A board that ran a firmware with another partition table (an Arduino one, NVS 0x5000 and an otadata) gets a
new table with our parts, but its NVS and data hold the other firmware's bytes. Yes to erase, or put back a
whole-flash backup of our firmware first: two CYDs came back from an Arduino firmware that way and then took
an update with every setting kept (measured).

`erase_flash`, and yes to erase on the web page, remove everything: NVS, tiles, the lot. On the JC8012 that
includes its touch controller's firmware, which lives in NVS: install it again after.

PlatformIO note from the old OWLOS code: `pio run -t uploadfs` rewrites the filesystem partition, and settings
kept in files there go with it, touch calibration too. NVS does not have that problem.

## What a killed flash leaves

Not measured here on purpose. From the esptool 4.10 source and IDF's bootloader:

- esptool writes the files in the order given: bootloader, app, partition table. Each file is erased and
  written in its own range, then its hash is checked.
- Killed in the middle: what was done stays, the file in flight is partly written, the rest is old. The chip
  stays in the ROM loader or the stub until reset, so the screen looks dead and the port is silent.
- After a reset the second stage bootloader prints `image at 0x10000 has invalid magic byte` or
  `Checksum failed`, then `No bootable app partitions in the partition table`, and it goes round.
- It is not a brick: the ROM loader cannot be overwritten. Run the same flash again, BOOT held if the
  auto-reset does not get in.
- NVS was never in a written range, so the settings are still there after the repeat. The exception: yes to
  erase. The whole chip is erased first, so a page closed after that leaves nothing.
- A killed parent shell can leave the Python child alive and holding the port: "could not open port" on the
  next try. Find that one process and stop it, nothing else.

The same applies to a closed browser tab during a flash from the page.

## Local rehearsal of a site flash

`flash-manifest.ps1` flashes `public/fw/<board>/` exactly as the page does: the manifest's parts at the
manifest's offsets, `-Erase` for yes to erase, `-Backup` and `-Restore` for a whole-flash copy (chip and MAC
checked first). With it the 0.2 to 0.3 update without erase was checked on a real board: every setting kept,
and the board restored from its backup after. See `ci-and-versions.md`, release rehearsal.
