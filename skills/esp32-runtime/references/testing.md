# Testing without a human

How sure each fact is: the tags of SKILL.md. The rule: a person clicking and reporting is a defect of the process.
Build the agent that runs the thing and reads the result. A person is asked only for what no tool can see: colours
on the glass, the LED, whether a touch feels right, where a key physically is.

Four rigs, from cheapest to dearest: the PC preview, the WebAssembly twin, the site against a fake board, the real
board on the bench. Push every check as far up that list as it can go.

## The PC preview

All of `core/` builds on the PC. Nothing else needs a board, so keep the logic there.

```bat
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% /DFLIGHT_VERSION=\"%VER%\" ^
   /Fobuild\%DISP%\ /Febuild\%DISP%\preview.exe ^
   preview.c ..\..\core\sky.c ..\..\core\demo.c ..\..\core\gfx.c ..\..\core\font.c ^
   ..\..\core\ui.c ... ..\..\core\map.c ..\..\core\png_decode.c ..\..\core\watch.c ..\..\core\light.c ^
   ..\..\core\paper.c ..\..\core\ui_paper.c %FONTS%
```

Visual Studio 2022's `vcvars64.bat`, one output folder per screen, then `preview.exe <out-dir>`. The sources are
named one by one, the same files as `core/CMakeLists.txt`. `tools/preview/BUILD.BAT <screen>` takes `ili9341`
(the default, shots to `shots/`), `st7796`, `ili9486`, `gc9a01`, `crowadv50`, `esp4827s043`, `epd400`, `epd792`
(shots to `shots/<screen>`); the e-paper ones come out in the glass's 4 greys, as `epd.c` gets them. The script
rewrites the tracked shots: copy what you need and restore the folder after, or a shot that the README shows
changes by accident. `tools/preview/preview.c` is a small driver:

```c
// the sky moves on: an answer every 5 s, like the firmware asks
static void fly(int seconds)
{
    for (int s = 0; s < seconds; s++) {
        t_ms += 1000u;
        if (t_ms % 5000u == 0) {
            int n = demo_json(&demo, HOME_LAT, HOME_LON, range_km(&ui.cfg), t_ms, json, sizeof json);
            static sky_reader_t rd;                  // in pieces, like the network hands it over
            sky_read_begin(&rd, &sky, 0, t_ms);
            for (int at = 0; at < n; at += 37) sky_read(&rd, json + at, n - at < 37 ? n - at : 37);
            if (sky_read_end(&rd, &sky) < 0) printf("parse failed\n");
            sky_forget(&sky, t_ms);
        }
    }
}
```

What this gives you:

- **The real parser**, fed the real format in 37-byte pieces, an odd size on purpose, so a token split across
  two reads is tested all the time.
- **The clock in your hand.** `t_ms` jumps: 30 minutes of sky in no time, a timeout on demand.
- **Every page as a PNG**, with the same dither and strip pass as the board, so the pixels are the panel's.
- **Touches and keys by script:** a `tap()` at x, y runs the same `ui_touch` as the finger, through a real press and
  lift, so gestures are tested as well. The e-paper shots drive `ui_key` the same way.
- **Screens by compile switch:** `BUILD.BAT st7796`, `gc9a01`. Layout breaks show on the size they break on.
- **The C3's path on a PC:** `GFX_SOFT_FLOAT` may be set from outside, to see the whole-number drawing.
- **The tile path:** `ground.inc` uses the same `map.c` and `png_decode.c` on files kept in `tools/preview/tiles`,
  fetched once by `curl` with a named user agent. The preview's home is the repository's default; check that
  pictures meant for the public show no other place.
- **A film:** `tour.c` plays a script of touches and pipes frames to ffmpeg.

**Keep a reference.** A speed-up must not change a pixel. Take the shots before, take them after, compare two
folders. The rig is a PowerShell one-pager with `System.Drawing` and `LockBits`: per file, "same" or how many
pixels differ and the largest step. It proved the grid and trail fixes changed nothing [measured]. The 320x240 set
was the reference while WiFi Radar's layout went size-relative. Later uses [measured on the PC]: the 5" scaled
layout left every other screen pixel-identical (135 shots); the inflater's smaller rows left 86 PNGs identical;
the map's new paint tables and names were compared side by side, old tree against new.

**Prove a rule, not only a picture.** The second core may draw only because strips only read. A rig draws every
look and page whole, in strips, and in halves cut at every row, and compares: 0 pixels differed on the 2.8",
3.5", 5", 4.3" and round layouts, and a 1-pixel difference on two boards that use pairs was found this way
[measured on the PC]. A rule that can be checked by drawing twice should be.

**Small tests for logic.** Plain C next to the module, a `check(ok, "what")`, `printf`, a count of fails:

```c
static int fails;
static void check(int ok, const char *what) { printf("%s  %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; }

watch_set(&l, " ezy12ab,G-EUPT;; 4ca7b4  EZY12AB, #N123AB, ABCDEFGHIJKLMN");
watch_text(&l, t, sizeof t);
check(l.n == 5 && !strcmp(t, "EZY12AB, G-EUPT, 4CA7B4, N123AB, ABCDEFGHIJ"), "capitals, each once, odd signs dropped, long ones cut");
```

Tested so [measured on the PC]: the watch list, home parsing, the LED logic, the ground filter with a sky of 60
parked and 20 flying planes, the weather warnings (25 cases), the sun's height (the solstices and a day's sunrise
and sunset), the five-network list. The touch driver with stubs and a pretend chip: the old code made 191 phantom
presses in 400 idle samples, the new one 0, with a finger still seen 15 ms after it touched and a dead interrupt pin
still worked round. Those tests and the rigs below lived in scratch folders, not in a repo. In a new project keep
them in `tools/tests/`, so the next person can run them too. Their code is quoted here for that reason.

**Replay what really happened.** Smoothing the planes' motion was first tested on made-up paths and passed, and the
eye still saw the planes wobble. Recorded answers from a real server (270 answers 2 s apart over a busy airport),
replayed through the board's own code, showed the cause, and the numbers measured what the eye sees: steps back a
pixel a minute, the worst metres off the path in one frame, the nose's snaps [measured on the PC]. Measure the thing
a person complains about, on recorded real data, not only the thing that is easy to count.

**A profile on the PC gives proportions only** (Air Quality's `preview --prof`). And only if the PC reaches the
device's state: trails fill 80 s after boot, so a profile of a fresh sky hides the 62 ms.

## Data with a known truth

- `core/demo.c` pretends to be a free ADS-B network: 12 invented flights, callsigns `OWL..`, the same JSON. Used by
  the firmware, the preview and the twin. Proves the parser, trails, the whole screen with no WiFi.
- `core/sim.c` (Air Quality) is a flat that breathes: people, a window, cooking, the sun, a weather front. Firmware
  demo and preview. Proves the air model, a day of graphs in a moment.
- `core/sim.c` (Thermo) is a room with a heater or a fridge, a probe a minute late, 1/16 C steps, noise. Firmware
  and preview. Proves hysteresis, rest times, faults, with a real relay if you want.
- `tools/preview/walk.c` (WiFi Radar) is a 10 x 7 m flat with walls, neighbours, one router a floor up, your own body
  in the way. Preview. Proves the walk map's error against the truth.

The demo goes through the same door as the real thing, so it cannot drift from it: `demo_json` writes what the
server would send. It also makes the firmware a product with no setup: no WiFi, DEMO on the screen. Invented data
is labelled, always. On the firmware it never goes to flash and never drives real hardware alone: Thermo's
simulated probe stops the moment a real one answers.

`walk.c` prints, against what it knows to be true:

```text
walk: <m> m in <s> s, then a 360 turn
walk solved: <n> snapshots, path rms <m> m after the best rigid fit, spread <m> m, ...
  motion explained <r>, map ACTIVE
  store: <bytes> bytes, read back identical (T <t>, <n> networks)
  <name> pos err <m> m  sd <m>  bearing err <deg> ...
  bearing rms <deg> deg over <n> placed networks
```

In that flat: turn 5-11 degrees, path about 1.2 m, trusted bearings 10-30 degrees [computed in the simulator]. A real
flat is worse, because of reflections. It has a `still` mode too: a board on a table must not draw a fake map, and the
check on how much of the change is explained by moving is tested there. It also does a store round-trip. A test
that a method fails when it should is worth as much as one that it works.

Make the simulator as nasty as the world: weak beacons get lost, the body shades, neighbours are behind walls.

## The WebAssembly twin

`web/twin.c` is a thin wrapper round `core/` as it is, built with Emscripten (`web/BUILD.BAT` and `web/emcc.args`,
see [project-skeleton.md](project-skeleton.md#web-the-same-core-for-webassembly)). The page's canvas is the screen,
its pointer the finger. It is the radar in a web page for people, and for testing it is the board's own drawing
code running where an agent can reach it.

How it is wired:

- `twin_init`, then `twin_frame(now, utc, down, x, y)` returns a pointer to RGBA bytes the page puts on a canvas.
  Strips of 48 rows, drawn into a whole frame buffer (a browser has the memory).
- A demo sky till a board is connected. Then the board's planes: the page says `OWLOS SKY ON` every 30 s, and the
  board sends each answer as lines in the free networks' own JSON, which the twin reads with the board's own
  `sky_read` (`twin_sky_begin`, `twin_sky_plane`, `twin_sky_end`). The same parser on both sides, and no CORS
  question: the board fetched it.
- Measured on the round C3: 5 answers in 32 s, 173 lines, all JSON, about 281 bytes a line [measured].
- The page fetches the map's tiles and the weather itself, with the questions the board would ask
  (`twin_map_url`, `twin_wx_url`), and hands the answers to the board's own readers (`twin_map_tile`,
  `twin_weather`).
- **State sync, not lockstep.** Page to board: `OWLOS SET` (look, range, units and the rest) and `OWLOS PICK
  <hex>`. Board to page: a finger's change on the board says `OWLOS CFG`, a new pick `OWLOS SEL <hex>`, a turned
  weather page `OWLOS WXPAGE n`. Neither side echoes what it was told. Tried both ways against a real board on its
  cable: works [measured].
- Pixels cannot go over the cable: 115200 baud is about 11 KB/s and one 320x240 frame is 150 KB, 13 s [computed].
  So both sides draw and only inputs travel.
- The twin shows what the code thinks, not the glass: colours, mirroring and a dead panel need the board or a
  camera.
- One build per screen size (800x480, 320x240, 480x320, round, 480x272), each with its fonts, so the site shows a
  connected board's own layout.

What it is good for:

- A test of any page, look or setting with no board: step frames, tap, read pixels or the state back, all from a
  script in a browser.
- A test of the site's whole flow with the fake board below, the twin following it.
- Remote support later: the person's page shows what their board's code thinks. The plane list gives away the
  home's area, and the password must never enter the stream: view only by default, control only on the owner's yes.

Traps of a browser that an agent drives [measured]:

- A hidden browser pane runs about 1 frame a second and slows timers. Step frames yourself (`window.twinFrame`),
  and tap without awaiting between press and lift, or a press reads as a long one.
- Scrolling a page inside the twin needs a slow drag (pointer events over about a second).
- A 0x0 viewport broke the site's own scene before the twin ever ran: set the window size before loading the page.

## The site against a fake board

The flasher site's board path (attach, SET and GET, the board's tabs, the twin per board) can be tested with no
board and no person.

**In the repository:** `tools/fakeserial.js` defines a pretend `navigator.serial` with four boards behind it, each
with a build and a made-up MAC, speaking the console's lines: HELLO to READY with build and MAC, GET to a CFG,
SET to OK, SKY ON to a few planes every 15 s. It can play the failures real cables make: `mute` (a cable that
carries nothing back, as a CH340 sometimes does until replugged), `stuck` (a USB chip that takes nothing, writes end
only when aborted), a framing error after open (as a CP210x shows when a board restarts), unplug and plug with the
browser's disconnect and connect events, and `getPorts` giving back only ports allowed before. `bash
tools/assemble.sh --test` builds the site as CI does and adds `test.html` with the fake loaded first. Never on the
site itself.

**A shim of your own** for a new firmware version: a classic script loaded before the page's own, with a
`FakeBoard` that answers as `main.c` does. What mattered [measured]:

- `getPorts` returns nothing at load, so nothing connects by itself; then the test clicks the connect button and the
  fake attaches.
- Modes by the page's query string: an old board that says only `OWLOS READY flight-radar 0.7` (no build, no MAC),
  so the site must show its board picker; a board one version back, without the newest fields in its CFG.
- The CFG must carry every field the site's version checks, or the site takes it for an older board.
- Work in a copy of the site served on its own port; the shim never goes in the repository.
- Afterwards take the fake MAC out of the page's local storage, reset the window size, stop the server.

## The bench: flashing as a regression test

For a change that touches the board. A rig, not a person. The rules behind a flash script:

```powershell
$out = cmd /c "firmware\BUILD.BAT $Disp" 2>&1
$code = $LASTEXITCODE
$warn = @($out | Select-String -Pattern "warning:|error:")
$done = $out | Select-String "Project build complete"
if ($code -ne 0 -or -not $done -or -not (Test-Path $bin) -or $warn.Count -gt 0) { "NOT FLASHED: the build did not finish"; exit 1 }
$id = & $py -m esptool --chip $Chip -p $Port chip_id 2>&1 | Select-String "MAC:" | Select-Object -First 1
if ($id -notmatch [regex]::Escape($Mac)) { "NOT FLASHED: $Port is $id, not $Mac"; exit 1 }
Push-Location "firmware\$dir"
$f = & $py -m esptool --chip $Chip -p $Port -b 460800 --before default_reset --after hard_reset write_flash "@flash_args" 2>&1
Pop-Location
$ok = @($f | Select-String "Hash of data verified").Count
"flashed $Port ($Mac): $ok of 3 verified"
if ($ok -ne 3) { exit 1 }
```

(`$py -m esptool` is esptool from IDF's own environment, not a script of ours.)

- **Gate on the words** "Project build complete". A failed IDF build leaves the old `.bin`, and esptool flashes it
  happily [measured].
- **Warnings stop it too.** The rig treats a compiler warning like an error.
- **Check the chip and the MAC.** Port numbers move when boards are replugged: a board was blamed for another's
  fault once that way [measured]. An older guard that checked less let a wrong port through, and only esptool's
  chip check stopped it. Identify a port by MAC every time; for a MAC you do not know, ask `OWLOS HELLO` first: the
  board says its version, build and MAC. Never hard-code a port number in a skill.
- **Count 3 of 3** `Hash of data verified`: bootloader, partition table, app.
- **460800**, not 921600: CH340 reads fail at the higher speed [measured].
- **Back up before you write** a board with something of value: `read_flash` of the whole chip, restored after.
  Keep the backups in a folder that lasts: scratch and temp folders get cleaned. A small rig that reads only the
  partition table and the app's header names what runs on a board without touching its NVS.
- **The port is busy** while a browser tab has the site connected over Web Serial: esptool says "Access is denied".
  Ask for the tab to be closed; it is the person's.

**Then ask the board.** A settings rig talks to it the way the site does: open with DTR and RTS up, drop both,
`OWLOS HELLO` every 0.8 s till READY (and refuse to go on unless READY shows the expected MAC), send the lines,
then:

- save `OWLOS CFG` to a file and print **only which field names differ** from an earlier one. Values are the
  user's: home and network names never reach the screen.
- print the fetch summaries, OK or ERR, and any `rst:`, `Guru`, `panic`, `abort`, `WDT` line.
- check results by booleans, never by printing a log line: the request URL holds the home.

Run from PowerShell with `& script -Send @(...)` in the same process: through `powershell -File` the JSON loses its
quotes, and the board still answers `OWLOS OK` to a SET that set nothing [measured].

The regression pass after a settings change:

1. Flash the old version. Set something on every field. GET, save.
2. Flash the new one with no erase. GET, compare. Nothing but new fields may differ.
3. Reset the board, GET again: it survived NVS, not just RAM.
4. Change one thing over SET and over the screen, GET.
5. For a beta: also flash the live stable back over it and the beta again, and compare (44 fields the same on two
   CYDs, 2026-11-19 [measured]).

**Rigs over the console, no finger.** Each one a short script, each putting back what it changed [measured that
they work]:

- Cycle the looks every 5 s and change the range every round, then leave the board alone for minutes: 227 commands,
  150 of 150 maps whole on the CYD, and the same on the version before, which is how a tester's "the map goes away"
  was shown to depend on their place, not the code.
- Range changes in a loop, printing only counts (maps whole, tiles broken, crashes): how the keep-alive crash and
  its fix were proven.
- Restart by esptool after a MAC check and wait for a given log line (the weather's first answer).
- Turn the weather pages, the e-paper keys (`OWLOS KEY n`), the panel on and off, a unit, and put each back as found.
- A soak: the cycle for 15 minutes, then 15 minutes idle, on all bench boards at once, with a reader that counts
  restarts, maps whole, warnings and the lowest heap. 0.16.2: 0 restarts on three boards; idle, every map whole.

**A throwaway hook.** Some paths need an input you do not have: a board with no touch for the keyboard's search, a
newer version on the site, a night that is hours away. Add a console line or a shifted clock for it in a temporary
build, test, remove it. `UPDATE_URL` pointed at a local server with a test `version.json` saying 0.4 proved the NEW
VERSION note [measured]. A server option set with no console word was tested with a copy that hard-coded it. Save
the file before you start and diff after: it is too easy to ship.

**A release rehearsal** (Flight Radar 0.3, again for 0.16.1): export the would-be commit with a temporary
`GIT_INDEX_FILE` and `git -c core.autocrlf=false archive`, build it fresh with the CI's `idf.py` line and a new
sdkconfig, assemble the site by the workflow's steps (older versions from a mirror of the live `versions.json` with
small stub files, real-shaped where a script reads them), flash the manifest's parts at the manifest's offsets as
the browser flasher would, then run the update test above. Catches a file that is on disk but not in git, and a
default that only a fresh sdkconfig shows.

**A real run.** Start a long logger and walk away. For WiFi Radar on the 2.8": 2 hours, no reboot, 0 lost strips,
heap 49 KB, 42 flash saves. For Flight Radar 0.14 on three boards: two hours each, no restart, heap at fetch time
flat. Then read the log for the trend of heap and `at least`, not only for crashes.

**Reading the person's monitor.** When `idf.py monitor` is open in a terminal, the port is busy. Read the terminal,
never kill it, never ask for a paste.

**The WiFi password** of the bench is the person's. A rig that sends WiFi asks for it at run time and prints
nothing of it; an agent never types or sends it.

## What needs a person

- Colours and mirroring on an unknown panel. "Light blue on white and upside down" was a v3 CYD on the v1 start-up.
- An LED's colour and pin. For a pin hunt, hold one pin low for minutes and ask yes or no. A blink count was too hard
  to read [measured].
- Touch feel and hit zones: the look button was hard to hit, and got a wider zone.
- Where a key physically is, when no document says: the e-paper's keys were placed on a guess till a photo of the
  board settled the edge.
- The glass against the preview: an e-paper's real greys and contrast differ from the PNG. Show the bands, ask for a
  photo, then move the numbers.
- Boards no one here has. Say so on the site, ask the owner of the board for what to look at, and list it: "New: not
  run on one here yet". A tester's report is evidence of their board, and is written as theirs.

## What was verified where

The list is in SKILL.md, **What was run on what**. The honesty rule: a build no one here has run is written down as
not run, in `board.h`, in the README and on the site, and it stays so until someone with the board reports. WiFi
Radar's `ili9486` and `epd400` builds were written so, and every Flight Radar board that came from a request. The
console's read path on the chip's own USB (S3, C5, P4) is not confirmed on a board here: the LCD boards on the bench
talk through a CH340.
