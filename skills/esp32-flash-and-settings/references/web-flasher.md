# The browser flasher over Web Serial

The browser flashes the board: no toolchain, no driver for native USB, Chrome or Edge on a computer or a
Chromebook. Two ways are in use here:

- **ESP Web Tools**, a ready web component with its own dialog. OWLOS WiFi Radar, ESP32 Web Flasher,
  FusionEdge's site and the Flight Radar's previous page (`site/previous/`) use it.
- **An own flasher on esptool-js**, no dialog: the Flight Radar's page since 0.8 (`site/flasher.js`). It asks
  the chip first and offers only the boards with that chip.

Both write the same parts at the same offsets from the same manifests. Settings over the same cable come
after: `usb-console.md`. The site around it: `site.md`. Files for esptool users: `bin-files.md`.

How sure: people flash from the published pages, the ESP32 CYDs most, one from a Chromebook. An agent cannot
press the browser's port picker, so local tests flash the manifest's parts with esptool instead
(`flash-manifest.ps1`) and the rest was read from the code. S3, C5 and P4 boards: built, flashed only as far as
their owners report.

## ESP Web Tools 10.2.1

The version every project here pins. Its code was read from unpkg (`dist/connect.js`, `dist/flash.js`,
`dist/install-button.js`, `dist/install-dialog.js`, `dist/const.d.ts`) on 2026-11-04, so "read from source"
below means exactly that version. Newer ones exist (10.4.0 by July 2026, 10.3.0 "refreshed flashing stack"):
pinned means a site that worked keeps working, and a move to a new one is a test, not a bump.

### On the page

```html
<script type="module" src="https://unpkg.com/esp-web-tools@10.2.1/dist/web/install-button.js?module"></script>

<esp-web-install-button class="flash" manifest="fw/ili9341/manifest.json">
  <button slot="activate">CONNECT &amp; FLASH</button>
  <span slot="unsupported" class="nope">This browser can't flash. Use Chrome or Edge on a computer or a Chromebook.</span>
  <span slot="not-allowed" class="nope">Flashing needs the page over https.</span>
</esp-web-install-button>
```

The three slots (read from source, `install-button.js`):

- `activate`: your own button. Without it the element makes one, styled by the CSS variables below.
- `unsupported`: shown when `"serial" in navigator` is false (Firefox, Safari, most phones).
- `not-allowed`: shown when `window.isSecureContext` is false. https and `http://localhost` are secure. A LAN
  address over http is not, and the button is replaced by this slot.

The element also gets the attribute `install-supported` or `install-unsupported`: CSS can hide a hint with
`esp-web-install-button[install-unsupported] ~ .tip`.

CSS variables of the default button: `--esp-tools-button-color` (default #03a9f4),
`--esp-tools-button-text-color` (#fff), `--esp-tools-button-border-radius` (9999px). We slot our own button, so
plain CSS on `.flash button` styles it and these do nothing. The dialog reads `--mdc-theme-primary`,
`--mdc-theme-secondary`, `--text-color`, `--danger-color`, and `--md-sys-color-*` of Material Web.

`hidden` on the element needs `[hidden] { display: none !important; }`, because the page's own `display`
rules win over the attribute otherwise. The version picker hides buttons this way.

A page moved under a `<base>` (the Flight Radar's old page now lives at `previous/` with `<base href="../">`):
ESP Web Tools reads the manifest against the page's own address, not the base. Give the buttons the whole
address: `new URL(path, document.baseURI).href` (built into the live previous page).

Another project of the owner's, ViewOwl, vendors esp-web-tools in its own folder instead of loading it from
unpkg, themes the Material dialog by CSS, goes straight on to sending settings on `state-changed` with
`finished`, and waits up to 15 s for its firmware's READY line because settings sent early were lost (read
from its source).

### Which chips

`dist/const.d.ts`: `chipFamily` is one of `ESP32`, `ESP32-C2`, `ESP32-C3`, `ESP32-C5`, `ESP32-C6`, `ESP32-H2`,
`ESP32-S2`, `ESP32-S3`, `ESP8266`. C5 came in 10.2.0 with esptool-js 0.5.6 (its release notes); 10.2.1's
`package.json` asks for esptool-js ^0.5.7.

The name must match esptool-js's `CHIP_NAME` letter for letter: `ESP32-C3`, not `esp32c3`. esptool-js also
knows `ESP32-P4` and `ESP32-C61`. The compare is a plain string one, so they may work though the type does not
list them. The Flight Radar's previous page has `ESP32-P4` manifests (the Tab5, the JC8012); whether ESP Web
Tools 10.2.1 flashes them was not tried here.

### What happens on a click (read from source)

1. `connect(button)`: `navigator.serial.requestPort()`, the browser's own list. Nothing picked: a dialog with
   driver help and a try-again.
2. `port.open({ baudRate: 115200, bufferSize: 8192 })`. Busy port: a plain `alert` with the message.
3. `el.manifestPath = button.manifest || button.getAttribute("manifest")`. **The manifest is read at the
   click, not at load.** So `setAttribute('manifest', ...)` switches a button to another version or board at
   any time, and that is the whole of the version picker.
4. The dialog fetches the manifest ("Failed to download manifest" if not) and tries Improv Serial for 1 s. Our
   firmware does not speak Improv. Its bytes reach the console as junk that starts no OWLOS line.
5. Install pressed. With `new_install_prompt_erase: true` it asks whether to erase. **Without that line it
   erases by itself** on a device with no Improv ("Default is to erase a device that does not support Improv
   Serial"), so every update would wipe the settings. The line is not optional here.
6. A confirm screen says the manifest's `name` and `version`. That is why the manifest's version is the full
   text, `0.4, build 6 (bc203cf)`: the person sees what is about to go on.
7. `flash()`: esptool-js at 115200 both ways, `esploader.main()`, `flashId()`, then
   `chipFamily = esploader.chip.CHIP_NAME` and `manifest.builds.find(b => b.chipFamily === chipFamily)`. None
   found: **"Your ESP32-S3 board is not supported."** and a reset. Nothing was written.
8. Every part is fetched BEFORE anything is erased, each `new URL(part.path, manifestURL)`: paths are relative
   to the manifest, not to the page. That is why `fw/0.3/<board>/manifest.json` works unchanged with plain
   file names. A missing part fails here (`Downlading firmware ... failed: 404`, their spelling) and the board
   is untouched.
9. Yes to erase: `esploader.eraseFlash()`, the whole chip. Then `writeFlash` with
   `flashSize/flashMode/flashFreq: "keep"`, `compress: true`.
10. Reset: RTS up, 100 ms, `esploader.after()`.
11. It waits up to 10 s for Improv on the new firmware (`new_install_improv_wait_time`, seconds). A value of 0
    skips Improv both before and after. We have not set it: the waits are short and harmless, and 0 is the
    thing to try if they ever annoy.
12. The port stays open until the dialog closes: `closed` then `port.close()`. **Until the person closes the
    dialog, a settings step on the same page gets "port busy".** The page says so.

115200 only means a flash takes about a minute for a 1.1 MB app (an estimate from the baud rate and
compression, not timed).

The failed-connect message says "Try resetting your device or holding the BOOT button while clicking
INSTALL". On a board whose auto-reset does not reach the ROM loader, that is the way.

## An own flasher on esptool-js (the Flight Radar since 0.8)

`../files/flight-radar/site/flasher.js` and `md5.js`; the flow around it in the site's `flow.js`. esptool-js
is vendored as one file, `site/vendor/esptool.js`, with its Apache 2.0 licence beside it; the file does not say
its version. It knows ESP32-C5, C61, H2 and P4. The whole page is built from the code as it runs on the live
site; how sure each step is: read from source, and used by people on the live site.

Why: ESP Web Tools picks a build by chip alone and cannot tell the screen. The page wanted to ask the board
first, offer only the cards of its chip, keep its own look with no dialog, and flash a board that is already
in a window of the page with no second port picker.

### Asking a board who it is

1. A port with our firmware answers `OWLOS HELLO` with its version, build and MAC (`usb-console.md`): the page
   knows the board and needs no ROM loader. A Flight Radar before 0.8 answers without build and MAC: the page
   shows every board for the person to pick, and says the settings stay.
2. Anything else: `detect(port)` opens esptool-js's loader (`baudrate: 460800, romBaudrate: 115200`), reads
   `esp.chip.CHIP_NAME` and the MAC, restarts the chip into its own firmware and lets go of the port. The chip
   name maps to a family key (`ESP32: esp32`, `ESP32-S3: s3`, `ESP32-C3: c3`, `ESP32-C5: c5`, `ESP32-P4: p4`),
   and the page shows only the boards with that chip. One chip, one board: it is picked. Several: "The chip
   does not say which screen is on it, so pick yours", with "Not this one? Show all boards".
3. A chip that did not answer (`esp.main()` threw) is restarted into its own firmware all the same, or it
   stays dark in its bootloader until a restart, and its port is let go, or it stays busy.

The restart, as esptool does it, DTR down first so the BOOT pin is up when the chip wakes:

```js
async function restart(transport) {
  await transport.setDTR(false);
  await transport.setRTS(true);
  await wait(200);
  await transport.setRTS(false);
  await wait(100);
}
```

With DTR still down the chip wakes in its bootloader, the screen stays black and nothing answers. The
library's own `after()` asks the chip about its USB first, and on the C3 no answer came.

Letting go has time limits: `transport.disconnect()` within 3 s, `port.close()` within 2 s. A port that does not
let go must not hold the page for ever.

### Writing

```js
const fileArray = await parts(dir);        // the manifest and every part fetched first: a 404 touches nothing
const { transport, esp } = await loader(port, say);
await esp.writeFlash({
  fileArray, flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
  eraseAll: erase,                         // the checkbox: ticked by the person, never by the page
  compress: true,
  calculateMD5Hash: md5,                   // the chip's hash of what it wrote must match
  reportProgress(index, written, size) { /* one bar over all the parts' bytes */ },
});
await restart(transport);
```

- `dir` is the board's folder of the version: `fw/<board>` for the stable, `fw/<version>/<board>` for a beta or
  an older one (`beta-and-stable.md`).
- `md5.js` is a small MD5 of its own, so the page needs no library for the check.
- The erase box names what goes: "Erase the board first: its WiFi, settings and saved maps go too". On the
  JC8012 it adds "and its touch firmware: install that again after", because that firmware lives in NVS
  (`../../esp32-boards/references/boards/jc8012.md`).
- After the write the page waits 1.5 s and asks HELLO up to four times, 1.5 s apart. A board on the chip's own
  USB comes back as a new port: the browser's `connect` event brings it back, and the page finds it by its MAC.
- A board the page knows by MAC that now runs another build (flashed elsewhere): the page follows the board's
  build. Kept as it was, an update would flash the old build back (a bug found and fixed in the page).
- A board that says `Brownout detector` while it starts and restarts for ever: the page says "its USB gives too
  little power. Try another USB port, a shorter cable or a powered hub", not "no WiFi".

### The USB chip, before connecting

`port.getInfo()` gives `usbVendorId`: 0x1A86 is a WCH CH340, 0x10C4 a Silicon Labs CP210x, 0x303A Espressif's
own USB. The page names the cable by it, and for a board it already knows passes it as a filter to
`requestPort({ filters: [{ usbVendorId, usbProductId }] })`, so the browser's list shows only those ports. The
browser does not always give a page back the cable of a board plugged in before it started (seen with CH340
boards in Chrome): then the page asks with the filter.

## The manifest

What `make-manifests.sh` writes (real, from the live site):

```json
{
  "name": "OWLOS Flight Radar - 2.8\" NM-CYD-C5 (ESP32-C5, ST7789)",
  "version": "0.4, build 6 (bc203cf)",
  "new_install_prompt_erase": true,
  "builds": [ { "chipFamily": "ESP32-C5", "parts": [
    { "path": "bootloader.bin", "offset": 8192 },
    { "path": "owlos-flight-radar.bin", "offset": 65536 },
    { "path": "partition-table.bin", "offset": 32768 } ] } ]
}
```

- Offsets decimal. Order as in `flash_args`.
- `new_install_skip_erase` is the deprecated older name.
- `home_assistant_domain`, `funding_url` exist and are not used.
- The own flasher reads `builds[0].parts` and nothing else: the same file serves both.
- The name of an untested board says "experimental" (`make-manifests.sh`).

### One manifest per board, or one with a build per chip

- **A build per chipFamily in one manifest**: ESP Web Tools picks by itself, one button for everything.
  ESP32 Web Flasher does this, five chips in `site/manifest.json`. Right when one firmware fits every board of
  a chip.
- **A manifest per board**: needed as soon as two boards share a chip and differ in screen. `find` takes the
  first build of a family, so one manifest cannot hold two ESP32 builds. The Flight Radar's site has three ESP32
  boards and nine S3 ones.

**It can tell the chip and the USB bridge, never the screen chip.** The chip comes from the ROM loader. The
bridge comes from Web Serial's `port.getInfo()`. What screen hangs on the SPI bus nobody can ask. So the
person picks a card with a picture, and a wrong card on the right chip flashes fine and shows a wrong picture.
That is what BOOT cycling of the screen start-ups is for (`site.md`).

The case met in the field: an owner of an ESP32-S3 board pressed the card of an ESP32 board and got "Your
ESP32-S3 board is not supported". It was true then: the site had no S3 build. Reading the chip at connect and
offering only that chip's cards was the idea; the own flasher does it now.

## Erase, in the person's words

The page says, and it has held up:

- New board, or anything else on it: **yes to erase**.
- An update of the same firmware: **no to erase**. WiFi, home and everything in NVS stay (measured, 0.2 to 0.3
  on a real board, every setting compared; the bench boards kept every setting through each update since).
- Yes to erase also clears the map tiles and the touch calibration: the board asks for the crosses again.
- A firmware whose settings live in a filesystem image that the manifest writes (FusionEdge's station and WiFi
  lists) loses those on every install, erase or not: `platformio-pipeline.md`.

## An older version

The ESP Web Tools page: `pickVersion()` in `archive.md`, one `setAttribute` per card, no reload, which works
because of step 3. The own flasher: the folder it is given (`fwDir(build, version)`).
