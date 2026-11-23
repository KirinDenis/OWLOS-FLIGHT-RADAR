# For an AI agent working on OWLOS Flight Radar

This file is for the AI agent a person brings to this repository: Claude Code (its `CLAUDE.md` points here),
Codex, Copilot, Cursor or any other. Read it before you change anything.

## Start with the skills

OWLOS Flight Radar is an Open Skills project. The folder `skills/` holds what we learned building it, in the
Agent Skills format: a folder a skill, its `SKILL.md` first, the details in its `references/`. Every fact there
says how sure it is: measured on a real board, built but not run on one, read from docs, or reasoned. Believe a
measured fact, check the others.

- `skills/README.md`: the index.
- `esp32-project-playbook`: start here. The order of work, public text, people's requests, the release routine.
- `esp32-boards`: the chips and every board, one file each, and how to port a new one.
- `esp32-lcd-touch`: the panels, the buses, the touch chips and the e-papers, one file each.
- `esp32-ui-drawing`: the renderer, fonts, themes, layouts, the e-paper design, the dials, the preview.
- `esp32-runtime`: tasks, watchdogs, memory, WiFi and TLS, crashes, testing without a person.
- `esp32-flash-and-settings`: flashing, the browser flasher, the settings over USB, CI, versions.
- `esp32-data-sources`: the web services and sensors, one file each.

Read the skill your task needs before you open the code. When your work teaches something new, say it in your
pull request: it goes back into the skills.

## The rules

1. Work in your own fork or branch. Pull requests are welcome.
2. Build with ESP-IDF 5.5: `firmware/BUILD.BAT <screen>` on Windows, `firmware/build.sh <screen>` on macOS and
   Linux for the boards it knows. The screens are listed in the README's Build it yourself.
3. Test without a board first. `tools/preview` draws every page on the PC into `shots/`: compare the shots
   before and after your change. `web/` builds the same `core/` for the browser.
4. Flash only the board you mean. Ask it its chip and MAC (`esptool chip_id`) and compare before you write. A
   wrong build leaves the screen dark. Do not erase the flash: the person's settings live in NVS.
5. Keep the settings compatible. `OWLOS GET` and `OWLOS SET` over USB, and the settings stored on the board,
   must still work for older and newer versions. A new field gets a default. A changed stored block gets a new
   version number and the old one is still read (as `SCOPE_OPT_VERSION` in `core/ui.h`).
6. Mind the memory. The classic ESP32 boards have no PSRAM. The round ESP32-C3 has 2 MB of flash and its app
   has little room left. Measure the heap and the frame time on the board before and after.
7. Secrets stay in the board. The WiFi password and the Flightradar24 key are never printed, logged or
   committed. No home point, town, network name or LAN address in code, shots, logs or text.
8. Public text (README, CHANGES, the site) is plain English: no emoji, no tables, no long dashes. A board that
   was built but never run is "New: not run on one here yet", never "supported".
9. The version is the first `## ` line of `CHANGES.md`. `(beta)` on that line makes it a beta.

## Where things are

- `core/`: plain C shared by the firmware, the PC preview and the browser: the sky, the map, the drawing, the
  fonts, the pages, the touch.
- `firmware/`: the boards, the screens, the touch chips, WiFi and HTTPS, the settings, the USB console.
- `site/`: the page that flashes the boards, its console, the map of boards.
- `web/`: the radar in a web page, built with Emscripten.
- `tools/`: the PC preview, the font and map bakers, the guide arrows.
- `server/`: RadarStats, the counts behind the map of boards.
- `BOARDS-WANTED.md`: boards people asked for. `CHANGES.md`: every version.
