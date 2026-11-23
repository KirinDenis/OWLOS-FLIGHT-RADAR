# Real files

So a new repository can start from them with none of the old repos at hand. The `flight-radar/` copies were taken
again on 2026-11-23 from OWLOS Flight Radar as it stood then (0.16.2 beta); `web-flasher/` is as it was on
2026-11-04 (it has not changed since). The arrow says where each goes. LF line ends here: `BUILD.BAT` wants CRLF in
a repository, the `.gitattributes` copy does that. One change to a copy: the example home in `console.h`'s comment
is a neutral point.

## flight-radar/ (OWLOS Flight Radar: ESP-IDF, a build per board, channels, the archive)

The firmware side:

- `BUILD.BAT`, `build.sh`, `CMakeLists.txt` go to `firmware/`.
- `partitions.csv` (4 MB), `partitions_2mb.csv` (the round C3), `partitions_16mb.csv` (the P4 Tab5),
  `partitions_jc8012.csv` (16 MB and a core dump), `partitions_wave7.csv` (8 or 16 MB, the 4 MB layout and a core
  dump) go to `firmware/`.
- `console.c`, `console.h`, `store.c`, `store.h` go to `firmware/main/`.

The pipeline:

- `pages.yml` goes to `.github/workflows/pages.yml`.
- `make-manifests.sh`, `archive.sh` go to `.github/`.
- `archive-seed.txt` goes to `.github/archive-seed.txt` (in a new repo: the comment only).
- `gitattributes.txt` goes to `.gitattributes`.

The page (`site/` here and there):

- `site/flasher.js`: the own flasher on esptool-js: `detect()` and `flash()`. It imports `./vendor/esptool.js`
  (esptool-js as one file, Apache 2.0: put its bundle and licence there) and `./md5.js`.
- `site/md5.js`: MD5 for the chip's check of what it wrote.
- `site/link.js`: one board on one cable, the console's lines over Web Serial (`class Link`).
- `site/board-console.js`: the site console's board commands and `/boards` folders. It needs the shell core it
  plugs into (not copied here: `references/site-console.md` has its contract).

The tools (`tools/` here and there):

- `tools/assemble.sh`: the site as `pages.yml` puts it together, into `_public/`, from local builds; `--test` adds
  the fake board.
- `tools/fakeserial.js`: boards behind a pretend `navigator.serial`, for tests only.

To change for a new project: the app name (`owlos-flight-radar` in `pages.yml`, `make-manifests.sh`, `archive.sh`,
`assemble.sh`, and `project()` in `CMakeLists.txt`), `RADAR_DISPLAY`, `FLIGHT_VERSION`, `FLIGHT_BETA`, the namespace
`flight` in `store.c`, the word `flight-radar` after READY (and in `link.js`'s `hello()`), the boards in the matrix,
BUILD.BAT, `make-manifests.sh` and `assemble.sh`, the chip names in `flasher.js`.

`store.c` needs the project's `fr_cfg_t`, `light_cfg_t`, `scope_opt_t`, `nets_t`, `wx_units_t` and `touch_cal_t`:
read it as the pattern (`references/nvs-settings.md`). `console.c` needs `board.h` for `NO_CHIP_USB` and the board
names.

## web-flasher/ (ESP32 Web Flasher: Arduino, one manifest, the minimal template)

- `pages.yml` goes to `.github/workflows/pages.yml`.
- `index.html`, `manifest.json` go to `site/`.
- `firmware.ino`, `version.h` go to `firmware/`.
- `gitignore.txt` goes to `.gitignore`.

## rigs/ (working tools, cleaned of paths, ports and MACs)

- `flash-one.ps1` flashes a finished build folder to one board, only when the chip AND the MAC are the ones meant,
  no erase, 3 of 3 hashes counted. From the Flight Radar bench's newer flash helper.
- `backup-board.ps1` reads the whole flash into a file, MAC checked, never over a file that is there, size checked.
  From the bench's backup helper.
- `build-some.ps1` builds several boards one after the other, a line each: built, fresh, warnings, errors, size,
  time. From the bench's build helper.
- `flash-board.ps1` builds, checks chip and MAC, flashes, counts the hashes. From the project's `flash.ps1`; its
  MAC-only guard was replaced here by the chip and MAC check of `flash-one.ps1`.
- `flash-manifest.ps1` flashes a site folder as the page does, with backup and restore. From `flashpub.ps1` and
  `flashman.ps1`; the same new guard.
- `cfgrig.ps1` the settings console from PowerShell, values masked. Its `\n`-first was added to match the page;
  `-Mac` (send nothing unless READY names that MAC) and the longer list of fields a compare leaves out were added on
  2026-11-23.
- `check-site.sh` fetches every manifest and part of a site, checks bootloader offsets. Run on the live Flight Radar
  site: 61 fetched, 0 bad on 2026-11-04; 586 fetched, 0 bad on 2026-11-23 (13 versions and a beta).
- `fake-gh.sh` a stand-in for `gh run download` in archive tests. The `run download` branch of the fake that was
  used, cut out of it.

The `.ps1` files were cleaned for this skill and not run again after that (no board may be touched from here):
`flash-one.ps1`, `backup-board.ps1` and `build-some.ps1` are clean copies of helpers that ran on the bench, with
parameters where those had fixed paths; the C5 and P4 chip patterns in them were never run.
