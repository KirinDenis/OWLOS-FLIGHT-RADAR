# Testing the site with a fake board, and the twin

An agent cannot press the browser's port picker, cannot see a board's glass, and should not ask a person to click
and report. Two things let the page's whole board path run with no board and no person: a fake Web Serial port
that answers as the firmware does, and the twin, the board's own screen code built for the browser. Real files:
`../files/flight-radar/tools/fakeserial.js`, `tools/assemble.sh`. Source: OWLOS Flight Radar `tools/`, `web/`.

How sure: measured in the browser. The fake board and a test copy carried the site's tests for the networks list,
the board windows, the console's `set` and `config`, and the 0.16 weather controls; all passed. What a fake cannot
prove: the browser's real port picker, a real USB chip's timing, the glass.

## The fake port in the repository: tools/fakeserial.js

A plain script, loaded before the page's own, that puts a pretend `navigator.serial` in place. Four boards speak
the console's lines behind it. Never on the site: `bash tools/assemble.sh --test` writes `_public/test.html`, the
page with this script added in front of the twin.

```js
const BOARDS = [
  { mac: 'aa0000000002', build: 'ili9341', theme: 2 },
  { mac: 'aa0000000001', build: 'st7796', theme: 0 },
  { mac: 'aa0000000003', build: 'gc9a01', theme: 1 },
  { mac: 'aa0000000004', build: 'ili9486', theme: 0 },
];
```

What `FakePort` does, each one a behaviour of a real port the page must survive:

- `open()` makes new ReadableStream and WritableStream; opening twice throws `InvalidStateError`, as the browser
  does. It counts the opens: a test can check one try is one open.
- `getInfo()` says `usbVendorId: 0x1a86`, a CH340.
- `setSignals()` takes anything.
- `close()` throws while a stream is locked, as the browser's does: a page that forgot a reader or a writer cannot
  close its port, and the test sees it.
- The `ili9486` board breaks its read with a `FramingError` 30 ms after open, as a CP210x does when a board
  restarts: the page must start a new reader, not give up.
- `b.stuck`: a USB chip that takes nothing; its writes end only when aborted (the page's 2 s write limit).
- `b.mute`: a cable that carries nothing back, as a CH340 sometimes does until it is plugged in again.
- `unplug()` and `plug()`: the reader gets a `NetworkError`, and the browser's `disconnect` and `connect` events go
  out with the port as their target.

What the fake board answers:

- `OWLOS HELLO`: `OWLOS READY flight-radar 0.8 <build> <mac>`.
- `OWLOS GET`: `OWLOS CFG {...}` with the fields that version has (made-up network and home).
- `OWLOS SET {...}`: keeps `screen` and `flip`, answers `OWLOS OK`; a line that is not JSON is let pass, as a board
  does with a line typed wrong.
- `OWLOS SKY ON`: `OWLOS ALIGN 1` from the round board (it waits for its touch alignment), 0 from the others, then
  `SKY BEGIN TEST`, three `OWLOS PLANE {...}` lines, `SKY END 3`, again every 15 s. `OWLOS SKIP`: `OWLOS ALIGN 0`.

The handles a test reaches:

- `test.html?allow=0,1`: the ports this browser "allowed before", given back at start by `getPorts()`.
- `fakePorts[i].b.mute = true`, `.stuck = true`: break a board.
- `fakeNext(i)`: which port the next `requestPort()` gives.
- `fakeUnplug(i)`, `fakePlug(i)`: the cable out and in.

## A shim in a test copy, for the newest firmware

For a feature newer than `fakeserial.js` knows, the tests ran in a COPY of the site (the page's scripts and the
twins in a folder of their own, served on a local port), with a small `test-shim.js` loaded before the twin scripts
and `flow.js`. Never put such a shim in the repository.

- It defines `navigator.serial` with `requestPort` returning a FakePort: an EventTarget with `open` (new streams),
  `close`, `setSignals`, `getInfo`, `connected: true`. Behind it a FakeBoard answers as the current `main.c`:
  `OWLOS READY flight-radar 0.16.1 ili9341 a0b1c2d3e4f5` (the MAC 12 hex digits, no colons), GET with the fields
  that version has (for 0.16: `alerts`, `temp_unit`, `pressure_unit`, `wind_unit`, `rain_unit`, `cc`), SET merged
  into its CFG and answered OK, SKY ON.
- `getPorts` returns `[]`, so nothing connects at load. Then click CONNECT A DEVICE and the fake board attaches.
- Modes by the page's query:
  - `?fake=old` answers `OWLOS READY flight-radar 0.7` (no build, no MAC): the site shows the board picker with
    its STABLE and BETA chips.
  - `?fake=015` is a 0.15 board flashed back from the beta: no weather fields, look 3 kept. The page must show
    such a board its three looks and no weather chips.
- Typing in the console with no real terminal: type the line, then press Return as a key (a `\n` inside the typed
  text is not Enter).

## The hidden browser pane

A test browser that is not on screen behaves otherwise, and the page broke on it before a visitor ever would:

- With a 0x0 viewport the page's scene computed NaN and threw inside its start, which stopped `flow.js`'s top
  level: no demo, no come-back of known boards, no frames. Real visitors never hit it (the canvas is at least 300
  px high). Set the window to a real size, 1280x860, BEFORE loading.
- A hidden tab gets about one animation frame a second, and none while `document.hidden`: shim
  `requestAnimationFrame` to `setTimeout(100)` while hidden, or no frame runs and the twin never fetches its
  weather. The twin page has `twinFrame()` to step one frame by hand.
- The twin needs a slow drag (pointer events over about a second) to scroll a list.

## Afterwards

- Take the fake MAC out of `localStorage` `owlos.boards`. Do it from the radar page (`/radar/`), which never writes
  that key back; from the main page, the page writes it again.
- Put the window size back, stop the local server.

## The twin: the board's screen code in the browser

`web/` builds `core/` (the radar's drawing, pages, touch and data parsing, the same files as the firmware and the
PC preview) for WebAssembly with Emscripten: `web/BUILD.BAT` locally, the `web` job in CI, both from
`web/emcc.args`. Five twins, one a screen size: 800x480 (the 5" layout, with the glass's own letters), 320x240,
480x320, the round one, 480x272. The page's canvas is the screen, its pointer the finger. It fetches the map's
tiles and the weather itself, with the questions the board asks, and reads the answers with the board's own reader.

What it is for:

- **The demo with no board**: a demo sky until a board is connected. A visitor sees the real screen before buying
  anything; a test sees every page draw.
- **The live screen of a board on the cable**: the board sends its planes as `OWLOS PLANE {json}` lines in the
  free networks' own words, so the twin reads them with the same `sky_read` the board uses and no CORS question
  comes up. A tap on either side goes to the other (`OWLOS SET`, `OWLOS PICK`; back: `OWLOS CFG`, `OWLOS SEL`,
  `OWLOS WXPAGE`). State is kept in step, not lockstep, and neither side echoes.
- **A test of the page's board path with no board**: the twin plus a fake port is the whole page.
- **A test of a board nobody here has**: the twin draws any screen size, so a 7" or a 10" layout is seen without
  the panel. It shows what the code thinks, not the glass: colours, mirroring and a panel's timing need the real
  board (reasoned).

Its hooks, on the radar page (`web/index.html`, at `/radar/` on the site):

- `twinFrame()`: step one frame (a hidden tab gets one a second).
- `twinLink()`: make a Link to feed it a board's lines. `live` and `finger` are script globals, so a test can set
  `live = twinLink()`, override its `send` to catch what the twin would send to a board, and tap through `finger`.

Why the twin and not pixels over the cable: 115200 baud is about 11 KB/s, and one 320x240 frame is 150 KB, 13 s.
So both sides draw, and only inputs travel: touches, plane lists, the time (reasoned, the design note).

Serve it over http: a page opened from a file cannot load the WebAssembly.

## Settings with no browser

`cfgrig.ps1` speaks the same lines to a real board from PowerShell, with every typed value masked
(`usb-console.md`). With a fake board for the page and the rig for the board, an agent tests both ends of the
cable with no person in the loop; only the port picker and the glass are left for a hand and an eye.
