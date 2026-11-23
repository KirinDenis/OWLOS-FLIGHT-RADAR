# The site: the page that flashes and sets up

The site is one static page on GitHub Pages, next to the firmware it flashes: plain HTML, CSS and module scripts,
no framework, no build. Source: OWLOS Flight Radar's `site/` (the page since 0.8: `index.html`, `flow.js`,
`flasher.js`, `link.js`, `board-console.js`; the page before it at `site/previous/index.html`), `tools/assemble.sh`.
The minimal one: `template.md`. FusionEdge's: `platformio-pipeline.md`.

Public text rules of the family: plain words, no emoji, no tables, no long dashes, an untested board says so. See
`../../esp32-project-playbook/SKILL.md`.

## The page since 0.8: one button, every board at once

How sure: live on the site and used by people; the parts below read from its code; the board path tested in a
copy with a fake board (`fake-board-testing.md`).

- **The top**: the name, a picture of how the planes reach the board, one **CONNECT A DEVICE** button, "Plug the
  board in with a USB cable. Chrome or Edge on a computer.", and "Try it without a board" (the twin's demo sky).
  Without Web Serial the button hides and the line says the page can only be looked round here.
- **The wizard** in three steps, each said in a few words: 1 "Asking the board", 2 which board ("We found your
  board: ..." or "Which one is yours?", the cards of that chip only, the Version row, the erase box, FLASH 0.15
  TO THE 2.8" CYD), 3 flashing with a bar ("Leave the cable in, it takes about a minute"). The flasher's steps:
  `web-flasher.md`.
- **Each board a window**: its live screen (the twin, in step with the board), and SETTINGS with its tabs (the
  WiFi, home and data source sent as SET; VIEW: look, colours, units, warnings; BOARD: what it is, its version,
  STABLE or BETA, FLASH). A PORT window beside it: what came along its cable, and a line to send.
- **The bar of boards**: a chip each, ALL, + ADD A BOARD.
- **The console**: CONSOLE at the top right (`site-console.md`).
- **The map of boards**: counts from the project's server, shown only when it answers, and a "Help us make it
  better" card that asks once whether this page and this board may be counted.
- **Footer**: `Firmware @VERSION@` (the stable's text), the repo, credits, and "Looking for the page you know, with
  every firmware version and the full guide? Previous page."

What the page remembers, in the browser only:

- `localStorage` `owlos.boards`: each board known, by MAC: its name, build, zoom and USB chip. They come back as
  they were on the next visit.
- The ports the browser allowed: `navigator.serial.getPorts()` at start, all asked at once ("one board that does
  not answer must not keep the others waiting 7 seconds each"), each found board shown. The browser's `connect`
  event brings a board plugged in later, and a check every 3 s brings back one whose cable came back.
- A port the page holds is busy for everything else on the PC, esptool too (`chip-and-mac-check.md`).

How it talks when something goes wrong: a title and a sentence, TRY AGAIN, and PORT for what came along the
cable. "Nothing came from that port: the cable carried not a single byte back. Take the cable out and put it
back." "That port does not answer as a Flight Radar: is it the right port?" "It keeps restarting: its USB gives
too little power." "Flashed, but the page could not reach it: press its reset button, or plug it in again."

The board list in `flow.js`, one entry a build, is what the cards, the chip filter and the windows read:

```js
nmcydc5: { name: '2.8" NM-CYD-C5', spec: 'ESP32-C5, 320x240, resistive touch', chip: 'c5', w: 320, h: 240, inch: 2.8,
           touch: 'resistive', twin: 'Twin1', shot: '02-live.png', screens: 'two' },
crow70:  { ..., spec: 'ESP32-S3, 800x480, capacitive touch, experimental', noboot: true },   // BOOT is the panel's clock
jc8012:  { ..., nvsfw: true },     // its touch firmware lives in NVS: the erase box says so
```

`chip` filters the cards, `twin` picks the screen size of the live screen, `screens` says how many screen
start-ups BOOT steps through, `noboot` hides the BOOT hints on a board whose BOOT pin is a panel line,
`experimental` in the spec says it has not run on one here.

## The previous page (0.2 to 0.7, kept at previous/)

The page before 0.8 stays a link away, with every version and the full guide, on ESP Web Tools. It moved to
`previous/` with `<base href="../">`, so its buttons get whole addresses (`web-flasher.md`). Its structure is still
a good one for a project with one board a card:

1. **Header**, what it is, in two lines.
2. **Hero**: the film of the board in a photo of the board. The screen's rectangle is placed in percent over the
   photo (`left: 16%; top: 10%; width: 71.5%; aspect-ratio: 3 / 2`), captions read from a `<track>` and written
   UNDER it.
3. **Step 1 / FLASH THE BOARD**: a box with the newest version, the older-version picker, a numbered list of what
   can go wrong in the order it does.
4. **The cards**, a grid, a card a board.
5. **The boot section**, right under the cards.
6. **Step 2 / SEND IT YOUR SETTINGS**: the form (`usb-console.md`), SEND and READ WHAT IT HAS, a status list,
   `<details>` with What the board says.
7. **Step 3 / ON THE SCREEN: WHERE TO TAP**: a picture per screen with arrows, what it does under it.
8. **VERSIONS**: `CHANGES.md` as text, id `versions` for links from step 1.
9. **Footer**: `Firmware @VERSION@`, the repo, credits.

A card:

```html
<div class="grid">
  <article class="card">
    <img src="shots/02-live.png" alt="the radar on a 2.8 inch board" loading="lazy">
    <div class="body">
      <h3>2.8" NM-CYD-C5</h3>
      <div class="spec">The CYD's form on an ESP32-C5, ST7789 320x240, resistive touch. Either of its two
        USB-C ports works. New: not run on one here yet, tell us how it goes.</div>
      <a class="boot-tag" href="#boot">RED AND BLUE SWAPPED? PRESS BOOT</a>
      <esp-web-install-button class="flash" manifest="fw/nmcydc5/manifest.json">
        <button slot="activate">CONNECT &amp; FLASH</button>
        <span slot="unsupported" class="nope">This browser can't flash. Use Chrome or Edge on a computer or a Chromebook.</span>
        <span slot="not-allowed" class="nope">Flashing needs the page over https.</span>
      </esp-web-install-button>
    </div>
  </article>
</div>
```

```css
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(250px, 1fr)); gap: 18px; margin-top: 18px; }
.card { display: flex; flex-direction: column; border: 1px solid var(--line); border-radius: 12px; background: var(--panel); overflow: hidden; }
.card img { width: 100%; height: 190px; object-fit: contain; display: block; background: #020611; }
.card .body { padding: 14px 16px 16px; display: flex; flex-direction: column; gap: 8px; flex: 1; }
.card .flash { margin-top: auto; }          /* the buttons of a row line up, whatever the text above */
.btn, .flash button {
  width: 100%; padding: 11px 14px; border: 1px solid var(--prim); border-radius: 8px; cursor: pointer;
  font: 700 15px "Cascadia Mono", Consolas, monospace; letter-spacing: 1px;
  color: var(--bg0); background: var(--prim);
}
[hidden] { display: none !important; }      /* or a display: grid of the same element wins over it */
```

The rules behind the cards, on both pages:

- A card a BOARD, named as it is sold, with its chip, screen and touch in the spec line. The person knows the
  board, not the screen chip.
- **An untested board says so**: "New: not run on one here yet, tell us how it goes", or "experimental". The
  site, the README, CHANGES and the manifest's name all say it. Never claim a run that did not happen.
- The picture is a real shot of the firmware on that size.
- On the old page the manifest path is `fw/<board>/manifest.json` exactly: the version picker's regex reads the
  board name from it.

## The boot section: the screen nobody can ask

The chip and USB can be told, the screen chip cannot (`web-flasher.md`). So the 2.8" CYD, sold with three screen
chips, starts on the commonest, and BOOT in the first 8 seconds steps through the others, kept in NVS:

```html
<section class="boot" id="boot">
  <div>
    <h3>2.8" BOARDS: WRONG PICTURE? PRESS BOOT</h3>
    <p>... sold with three different screen chips that look the same from outside, and the board can't tell which ...</p>
    <ol>
      <li>Power the board on, or press <b>RST</b> and let go.</li>
      <li>Within <b>8 seconds</b> press <b>BOOT</b> once, briefly. A card on the screen says which one is on and shows the time left.</li>
      <li>Every press starts the screen as the next one: <b>v2</b>, then <b>v1</b>, then <b>v3</b>, and round again.</li>
      <li>That's all: the board keeps it, through a reset and a power cut.</li>
    </ol>
    <p>Missed the 8 seconds? Press RST and start again. Don't hold BOOT while you press RST: that wakes the flasher and the screen stays dark.</p>
  </div>
  <a href="shots/guide/screen.png" target="_blank"><img src="shots/guide/screen.png" loading="lazy" alt="..."></a>
</section>
```

```css
.boot {
  margin-top: 18px; padding: 16px 18px; border: 2px solid var(--warn); border-radius: 12px;
  background: rgba(255, 184, 72, .07); display: grid; grid-template-columns: 1fr 1fr; gap: 16px 22px; align-items: center;
}
.boot-tag {
  display: block; padding: 8px 10px; border: 2px solid var(--warn); border-radius: 8px; text-align: center;
  color: var(--warn); font: 700 14px "Cascadia Mono", Consolas, monospace; letter-spacing: 1px; text-decoration: none;
}
```

It was tried as a select in the settings first. The owner found that confusing: the screen is wrong BEFORE anyone
reaches a form, so it is said loud, in the warn colour, under the boards. `SET {"screen":n}` stays for scripts,
and the new page has `screen V2` in the console and the screen chips in a board's window, with "More" linking to
`previous/#boot`.

The old page's step 1 list, in the order things go wrong: which browser; a cable that carries data; press, pick,
yes to erase; no port (driver) or no connect (hold BOOT); the crosses; a wrong picture; DEMO until it has WiFi;
close the flasher's window; an update is no to erase.

## Checking a local copy of the site

An agent cannot press the browser's port picker, but it can prove all the rest.

**1. Assemble it**: `bash tools/assemble.sh` (`../files/flight-radar/tools/assemble.sh`) puts the site together as
the workflow does, into `_public/`, from the local build folders:

- each board with a local build gets `fw/<board>/` with its four files; a board with no local build is left out;
  the default board's folder is `build`, not `build_ili9341`, in a working copy;
- the twins into `radar/`, `site/`, `CHANGES.md`, the shots;
- `version.json` with the version and the stats address, as `pages.yml` writes it;
- `make-manifests.sh` run on a copy with its `\r` taken out (a Windows checkout may have CRLF in it), with the text
  `<version>, local`, and `@VERSION@` replaced the same way in both pages;
- `tools/stats-sample.json`: made-up counts, for a look at the map of boards with `index.html?stats=sample`;
- `--test` adds the fake board: `_public/test.html` (`fake-board-testing.md`).

Nothing here is published, and `_public/` is in `.gitignore`: the assembled site must not land in a commit. The older versions and the beta are not in a local
copy; for those, mirror the live `fw/versions.json` with stub files (`ci-and-versions.md`, the rehearsal).

**2. Serve it**: `npx --yes http-server _public -p <port> -c-1 --silent`. `-c-1` turns caching off. `http://localhost`
is a secure context, so Web Serial and ESP Web Tools work there; `file://` and a LAN address do not, and the twin's
WebAssembly does not load from `file://`.

**3. Fetch everything**: `../files/rigs/check-site.sh http://localhost:<port>`. It reads the pages' `manifest="..."`
and `fw/versions.json` (the beta's line too), fetches each manifest and each part, checks bootloader offsets against
the chip, ends with a count. Measured on the live Flight Radar site on 2026-11-23: 586 fetched, 0 bad. Run it after
each deploy too, on the real address.

**4. Look at the page**: the cards of each chip, the windows, no sideways scroll at 375 px. On the old page: every
card shows a button (a `hidden` one means a `[hidden]` rule or a missing board in the list), the picker appears with
two versions, `?v=0.3` switches the manifests.

**5. The flash itself**: `flash-manifest.ps1` writes the same parts at the same offsets. Only the browser's port
picker stays untested by an agent: it needs a hand.
