# Traps: symptom, cause, fix

Each was met in these projects unless it says "not met". How sure, after each title: measured (on a board, the
build PC or the live pipeline), docs (read from source or docs), reasoned (from the code).

## Building and flashing on the PC

**The old firmware is on the board after a "successful" flash.** (measured)
Cause: a failed IDF build leaves the old `.bin`, and esptool flashes it happily.
Fix: gate on `Project build complete` and the exit code, print the `.bin`'s size and time, or check it is newer
than the build's start. `flash-board.ps1`, `build-some.ps1`.

**The wrong board got flashed.** (measured, the risk)
Cause: COM numbers move, and two CYDs look the same.
Fix: `chip_id` with no `--chip`, compare the chip AND the MAC with your private list, refuse on any doubt. Back up
first: `read_flash`. `chip-and-mac-check.md`.

**A MAC guard let a wrong port through.** (measured)
Cause: an older, looser check in a bench rig.
Fix: the chip and the MAC both, read from esptool's own detection, exact compare, no answer is a refusal; then
`--chip` on the write so esptool checks too.

**esptool says "Access is denied" or "could not open port".** (measured)
Cause: the Flight Radar site open in a browser holds the ports it was allowed (it connects them by itself), or a
person's monitor, or a Python child of a killed shell.
Fix: find the holder. Only the person can close their tab or monitor; never kill another person's process.

**CMake refuses a build folder after the repo moved.** (measured)
Cause: `CMakeCache.txt` keeps the old path.
Fix: BUILD.BAT's findstr guard starts the folder fresh.

**A change in `sdkconfig.defaults` does nothing.** (measured)
Cause: defaults are read only when a build folder's sdkconfig is made.
Fix: delete `build*/sdkconfig` and `firmware/sdkconfig`. CI starts clean, so the site's build already differs from
yours.

**`idf.py` refuses esp32c5.** (measured)
Cause: on the 5.5 tag it is a preview target. CI's `release-v5.5` is later.
Fix: `--preview` for that target only.

**A P4 board stops at configure with `MissingKconfigError`.** (measured, the JC8012 on the local 5.5 tag)
Cause: the local IDF is older than CI's `release-v5.5`, where it builds.
Fix: compare with CI before calling the board broken; build that board in CI or on a later 5.5.

**idf.py is not found.** (measured)
Cause: `cmd //c BUILD.BAT` from Git Bash. Or the system Python first on PATH.
Fix: PowerShell, `cmd /c`. IDF's Python first on PATH.

**`pio run` fails with "MSys/Mingw is not supported".** (measured, PlatformIO 6.1.18)
Cause: started from Git Bash.
Fix: PowerShell, `cmd /c "pio run ... > log 2>&1"`, and `PLATFORMIO_CORE_DIR` of its own.

**An old PlatformIO project builds a firmware that does nothing.** (docs, read from its source)
Cause: the old OWLOS code checks for Arduino-ESP32 1.0.4 and a newer platform builds past the check.
Fix: pin the platform by exact version (`espressif32@1.12.3` there; a release URL for pioarduino).

**`read_flash` fails.** (measured)
Cause: 921600 is too fast for a CH340 read.
Fix: `-b 460800`.

**The board hangs silently at 80 MHz flash, or runs fine.** (measured both ways)
Cause: the WiFi Radar's CYD hung at 80 MHz, so it stays 40 MHz DIO. Later the Flight Radar went QIO 80 MHz on all
three bench boards, and it was the cheap big win (3.5" radar 124 to 99 ms a frame).
Fix: try it per board. The comment in `CMakeLists.txt` that says 40 is older than `sdkconfig.defaults.esp32`, which
wins.

**Data of another firmware is read as ours.** (measured)
Cause: an update without erase keeps every byte outside the three parts. The thermostat's history sits where the
radar left "SLOT" slots.
Fix: a magic in each record of a raw partition, a wrong magic reads as empty.

**Arduino build exits 1 on Windows.** (measured)
Cause: `arduino-cli compile --output-dir` and the core's boot_app0 export hook.
Fix: `--build-path`.

**The port is busy.** (measured)
Cause: `idf.py monitor`, a Serial Monitor, another tab, the flasher's dialog, the site holding its ports, or a
Python child left by a killed shell.
Fix: one program at a time. Find the holder, stop only that. Never another person's monitor.

**The board restarts each time you look at it.** (measured)
Cause: opening the port pulses EN through the CH340, with DTR and RTS dropped together too.
Fix: one long reader. Whatever must outlive it goes in NVS, RTC memory is cleared too. From a page: drop RTS first,
then DTR (docs, the site's fix).

**A settings compare right after a flash says `cc` differs.** (measured)
Cause: home's country is asked from OpenStreetMap at start and kept in RAM; after several quick restarts it refuses
for minutes.
Fix: leave `cc`, `newest`, `utc` and the run's times out of the compare.

**Backups and rigs gone.** (measured)
Cause: they lived in a temp folder that a cleanup emptied.
Fix: backups in a folder no cleanup touches, never in a repository.

**A board from another firmware family keeps odd settings.** (measured)
Cause: our partition table replaces theirs, but their NVS (0x5000 in an Arduino table) and data stay.
Fix: yes to erase, or put back a whole-flash backup of our own firmware first.

## The browser flasher

**"Your ESP32-S3 board is not supported."** (measured)
Cause: the card's manifest has no build for that chip. ESP Web Tools picks only by `chipFamily`.
Fix: a build and card for that board, or the card of the right chip. Nothing was written. The own flasher asks the
chip first and shows only its cards.

**Every update wipes the settings.** (docs)
Cause: no `new_install_prompt_erase` in the manifest: it erases by itself on a board without Improv.
Fix: `"new_install_prompt_erase": true`, and say "no to erase" on the page.

**Every install puts back the station list and WiFi list.** (measured, FusionEdge)
Cause: the manifest writes the LittleFS image, where those lists live.
Fix: say it on the page (export, then import), or keep settings in NVS. The firmware-only update path is the
device's own web Update.

**Step 2 says the port is busy right after a flash.** (docs)
Cause: ESP Web Tools holds the port until its dialog closes.
Fix: the page tells people to close the window. The error says it too.

**The button is gone, a line about https shows.** (docs)
Cause: not a secure context, a LAN address over http.
Fix: https or `http://localhost`.

**No port in the list.** (docs)
Cause: a cable that only charges, or no CH340 / CP210x driver.
Fix: another cable, the driver. A board on its own USB needs none.

**"Failed to initialize."** (docs)
Cause: the auto-reset did not reach the ROM loader. Common on S2 and S3.
Fix: hold BOOT while plugging in, or hold BOOT and press RST.

**A GPS or anything else on TX and RX of the USB UART talks over the flashing.** (reasoned, from a board asked for)
Cause: GPIO 1 and 3 are the USB port's UART.
Fix: unplug it for flashing and for settings.

**A part answers 404 after moving a manifest.** (docs)
Cause: part paths resolve against the manifest's URL, not the page's.
Fix: plain file names next to the manifest. `check-site.sh`.

**A page under a `<base>` cannot find its manifests.** (docs, built into the live page)
Cause: ESP Web Tools reads the manifest against the page's own address, not the base.
Fix: whole addresses, `new URL(path, document.baseURI).href`.

**The board does not boot, the bootloader offset was typed by hand.** (reasoned)
Cause: 0x1000, 0x0, 0x2000 by chip.
Fix: only ever from `flash_args`.

**A merged image lost the settings.** (reasoned; the Arduino case measured)
Cause: it covers 0x9000 to 0xEFFF with padding.
Fix: parts for updates. Merged for first installs only.

**A `hidden` card button stays visible.** (docs)
Cause: a `display` rule beats the attribute.
Fix: `[hidden] { display: none !important; }`.

**After a failed connect the board's screen stays black.** (docs, the site's flasher)
Cause: the tries to reach the ROM loader left the chip in it.
Fix: restart it into its own firmware (DTR down first, then an RTS pulse), and let go of the port, or it stays
busy.

**The C3 does not come back after esptool-js flashed it.** (docs, the site's flasher)
Cause: the library's `after()` asks the chip about its USB first, and on the C3 no answer came.
Fix: the page's own restart.

**After flashing, the page cannot find a board on its own USB.** (docs, the site's flasher)
Cause: such a board comes back as a new port.
Fix: wait for the browser's `connect` event and find the board by its MAC.

**An update flashed the old build back.** (docs, a bug fixed in the page)
Cause: the page kept the build it knew for that MAC; the board had been flashed with another build elsewhere.
Fix: take the build from the board's READY every time.

**"No WiFi" on a board that keeps restarting.** (docs, the site)
Cause: a brownout loop: the USB port gives too little power, most often when the WiFi starts.
Fix: watch for `Brownout detector` in its log and say "another USB port, a shorter cable or a powered hub".

**Erasing the JC8012 kills its touch.** (docs, from its contributor)
Cause: its touch controller's firmware lives in NVS.
Fix: the erase box says so; install the touch firmware again after.

## CI and Pages

**Deploy: "Get Pages site failed".** (docs)
Cause: Pages' source is not GitHub Actions.
Fix: Settings, Pages, Source: GitHub Actions.

**`403 Resource not accessible by integration` on POST /releases, with `contents: write`.** (measured, runs 4 and 5)
Cause: the GITHUB_TOKEN may not make a tag on a commit that changes `.github/workflows` against the default branch.
Fix: no releases. The archive lives on the site (`archive.md`). An own empty commit does not help.

**A local fake passed, GitHub refused.** (measured)
Cause: a fake proves your logic, not GitHub's rules.
Fix: read the REST docs' permission text before designing on an endpoint.

**Red run, and no way to read why.** (measured)
Cause: job logs need auth: 403 "Must have admin rights to Repository". The public API has jobs and steps only.
Fix: the step's name from the API, its text from a signed-in person, or `gh` with a token.

**The build number is always 1.** (reasoned)
Cause: a shallow checkout.
Fix: `fetch-depth: 0` in the job that counts.

**Git errors inside the docker build.** (docs)
Cause: root in the container, files owned by another user.
Fix: `git config --global --add safe.directory '*'` first.

**A job loses its checkout rights after adding `permissions:`.** (docs)
Cause: a job's block replaces the top one.
Fix: `contents: read` again in the job.

**`$'\r': command not found` in a shell step.** (reasoned)
Cause: CRLF in a `.sh` from Windows.
Fix: `.gitattributes`: `*.sh text eol=lf`, `*.yml text eol=lf`, `*.BAT text eol=crlf`. A local run on a Windows
checkout strips `\r` first, as `assemble.sh` does.

**A stale file from the live site.** (measured, the header)
Cause: the CDN's `Cache-Control: max-age=600`.
Fix: `?run=<run id>` on each fetch from the site.

**A run stops on one 503 from Pages.** (docs, the workflow's note)
Cause: Pages answers 503 now and then, and a run asks it for hundreds of files.
Fix: `curl --retry 4 --retry-delay 5`; an answer that stays bad still stops the run.

**A pull request publishes, or cancels main's run.** (avoided by design)
Cause: the site job on every event, one concurrency group for all.
Fix: `if: github.event_name != 'pull_request'` on the site job, `group: pages-${{ github.ref }}`.

**The radar page did not build, and the flasher did not go out.** (avoided by design)
Cause: the site job needs the web job's artifact.
Fix: `continue-on-error: true` on that download: the flasher goes out without the radar page.

**A version is missing from the archive.** (reasoned, 0.7)
Cause: its run never published: never pushed alone, or cancelled by the next push.
Fix: let a version's run finish before pushing the next number.

**The site job stops: "a new version needs a higher number".** (measured, by test)
Cause: `CHANGES.md` is lower than the site's newest. On purpose.
Fix: a higher number. The site stayed as it was.

**The site job stops on a file of an older version.** (measured, by test)
Cause: a part of the archive answered other than 200. On purpose.
Fix: find out why, the site stayed as it was. `site-fw` has a copy.

**The seed's download fails.** (docs)
Cause: artifacts last 90 days.
Fix: get the seeds onto the site in time. Once there, the seed line is passed over.

**A site taken down and brought back has lost its archive.** (reasoned, not met)
Cause: 404 means none yet.
Fix: restore from the `site-fw` artifact.

**A board that failed to build has lost its card's firmware.** (reasoned, not met)
Cause: `!cancelled()` publishes what built, and a rebuild of the same number has no copy of the failed board: the
card's manifest is 404, and that board is out of the version's list.
Fix: push a fix, the next good run brings it back. If that matters more than a working site for the rest, gate the
site job on all boards.

**A new board has no manifest.** (reasoned)
Cause: the `case` in `make-manifests.sh` skips unknown folders.
Fix: four places per board: BUILD.BAT, matrix, `make-manifests.sh`, the site's board list.

**A crash address decodes to nonsense.** (docs)
Cause: a local ELF. CI's IDF is another build.
Fix: `elf-<board>` of the same run, 90 days.

**The archive grows past what Pages holds.** (reasoned, not met)
Cause: every version and board kept; about 170 MB at 13 versions, a Pages site may be 1 GB, and every run fetches and
deploys it all.
Fix: watch the size; a store of its own when it nears the limit.

## Versions and channels

**The new number does not reach a build folder.** (reasoned)
Cause: CMake does not watch `CHANGES.md` unless told.
Fix: `CMAKE_CONFIGURE_DEPENDS`.

**0.10 is "older" than 0.9.** (reasoned)
Cause: comparing as text.
Fix: number by number, `sort -V` in shell, `localeCompare(..., { numeric: true })` in JS.

**Every board says NEW VERSION after a doc fix.** (reasoned, avoided)
Cause: a build number in `version.json`.
Fix: `version.json` holds the plain version, the build number only the manifests and the page.

**A beta reaches every board as a plain update.** (reasoned, avoided)
Cause: no channels: a new number at the top of CHANGES.md is the newest for everyone.
Fix: `(beta)` on the line, a stable in `version`, the beta in `beta`, a stable build that never reads `beta`
(`beta-and-stable.md`).

**Old boards stop hearing of new versions.** (reasoned, avoided)
Cause: `version` in `version.json` turned into something else; boards up to 0.14 read only it, digits and dots.
Fix: `version` stays the stable's plain number for ever. New fields go beside it.

**A contributor's PR takes the beta's number.** (measured, once)
Cause: the PR opened CHANGES.md with the number the beta had.
Fix: merge it as that stable number and move the beta up, or ask for another number. Check every PR's number.

**ABOUT drops a sign.** (measured)
Cause: the micro pixel font has no `"` and no `&`.
Fix: other words.

## Settings over USB

**A setting sent right at READY is lost, or the board resets.** (measured)
Cause: READY and the console came before the fetch task existed: `assert xTaskGenericNotify`.
Fix: `console_start` and READY last in `app_main`.

**GET at READY works 2 times of 6.** (measured)
Cause: garbled bytes from the restart glue onto the command.
Fix: the page sends `\n` first, the board skips empty lines. 6 of 6. Since 0.14 the board also finds `OWLOS ` inside
the line.

**The board answers about 3 s late.** (measured)
Cause: console priority 3 under fetch 4 during a TLS handshake.
Fix: HELLO again and again, 800 ms apart. Timeouts of seconds, not hundreds of ms.

**The log stopped after adding USB input.** (docs)
Cause: `usb_serial_jtag_driver_install` takes over the way out.
Fix: the LL functions, polled, no driver.

**The P4 board hangs at start, the watchdog bites.** (docs, the code's note; the Tab5)
Cause: the UART driver waited for a UART0 that the board does not use: its console is the chip's USB.
Fix: start the UART console only `#if CONFIG_ESP_CONSOLE_UART`.

**I2C or the screen breaks on a board whose USB pins are wired to them.** (docs, from `board.h`)
Cause: the console polls the chip's USB on pins used for something else.
Fix: `NO_CHIP_USB` in that board's file.

**A SET does nothing and no answer comes.** (reasoned)
Cause: the line is over 1023 bytes, dropped silently. Or the line does not start with `OWLOS`.
Fix: watch the sum of field lengths. `\n` first.

**A home of NaN is kept.** (docs, a fix in the code)
Cause: NaN passes every `<` and `>` range check.
Fix: `json_num` refuses what is not finite.

**A name cut after `\u` reads past the end.** (docs, a fix in the code)
Cause: Nominatim's answer cut at 2 KB, and `\u` read four characters whatever they were.
Fix: up to four hex digits and no further.

**The page stops saying how the WiFi went.** (reasoned)
Cause: someone reworded an `ESP_LOGI` the page matches.
Fix: those three log lines are protocol. Say so beside them.

**The rig's JSON arrives without quotes.** (measured)
Cause: `powershell -File`.
Fix: run it in-process, `& script -Send @(...)`.

**A password in a chat or a log.** (rule)
Cause: printing CFG in a test.
Fix: GET never carries it, the log names the network only, the rig masks typed values and prints field names, the
page's own log shows `***` for `pass` and `key`.

**A stuck USB chip holds the page's port for ever.** (docs, the site's link.js)
Cause: a write the cable never takes keeps the writer, and a port with a writer cannot close.
Fix: abort a write not taken in 2 s.

## NVS

**After an update all settings are back to defaults.** (reasoned, the reason for all this)
Cause: `sizeof` grew, and `load_blob` wants the exact size.
Fix: fields at the end, a branch per old version with `offsetof` and `_Static_assert`; or a key per new setting.

**A field added to an array inside the struct lost the fields after it.** (reasoned, avoided: the LED's version 4)
Cause: the array is not the end; everything after it moved.
Fix: the old layout as a typedef, read at its old sizes, copied over the defaults.

**After a downgrade the screen's settings are defaults, the WiFi is fine.** (docs)
Cause: `ESP_ERR_NVS_INVALID_LENGTH` on the larger blob. Not a bug.
Fix: say it on the page. Strings stay on their own keys for this, and the first network stays in `ssid`/`pass`.

**The migration fails with INVALID_LENGTH though the size looks right.** (reasoned, not met)
Cause: tail padding in the old struct.
Fix: `_Static_assert` with the old size as a number.

**A new field holds garbage after migration.** (reasoned, avoided)
Cause: the failed full read left part of a blob in the struct.
Fix: defaults AFTER that read, then the old size over them.

**A fallback to another firmware's namespace never happens.** (reasoned, fixed)
Cause: it was tried only when the own namespace could not be opened, but the own one exists from the first start.
Fix: ask each namespace for the KEY in turn.

**Run records vanish on reset.** (measured)
Cause: RTC memory is lost on the USB line's EN reset.
Fix: NVS, once a minute.

**A WiFi password in the public history.** (seen in an older project)
Cause: a station password in a config header, committed.
Fix: settings over the cable, never in the source; a password once pushed is changed, not just deleted.
