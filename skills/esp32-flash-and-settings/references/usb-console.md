# Settings over USB: the board's line protocol

A firmware on a public page is the same file for everyone, so it cannot carry anyone's WiFi. The board takes
its settings over the cable it was flashed with and keeps them in NVS. Text lines at 115200, so a script or any
terminal can do what the page does. The site's shell on top of these lines is in `site-console.md`.

Real files: `../files/flight-radar/console.c`, `console.h` (the board), `site/link.js` (the page since 0.8),
`../files/rigs/cfgrig.ps1` (the same from PowerShell). The handler `on_line()` is in the Flight Radar's
`firmware/main/main.c`; the 0.4 page's `class Board` lives on in its `site/previous/index.html`.

How sure: UART0 through a CH340 is measured on the bench boards with `cfgrig.ps1`, and by people on the live
site. The USB-Serial-JTAG path is built: boards with only that port are on the site (the ES3C28P, the Tab5),
but it was not measured on such a board here.

## The protocol

One line each way, the log runs on the same line, the page skips what does not start with `OWLOS `:

```
board  OWLOS READY flight-radar <version> <build> <mac>   at power on, and as the answer to OWLOS HELLO
page   OWLOS HELLO
page   OWLOS SET {"ssid":"..","pass":"..","lat":51.5,"lon":-0.1,"look":1}
board  OWLOS OK                               or OWLOS ERR <why>
page   OWLOS GET
board  OWLOS CFG {"ssid":"..","nets":[".."],"lat":..,"key":true,"version":"0.16.2","newest":"0.15",...}
board  OWLOS ALIGN 1                          its screen waits for the touch alignment, 0: no more
page   OWLOS SKIP                             leave the touch alignment, as holding BOOT does
```

- The word after READY is the firmware's name. The page tells "another OWLOS firmware is on this board" from
  it: "This board runs other OWLOS firmware (...). Flash the Flight Radar first, step 1."
- `<build>` and `<mac>` came in 0.8: the build name (`ili9341`, `tab5`) and the MAC as 12 hex digits, no colons.
  A page that knows several boards tells them apart by the MAC. An older board says only the version, and the
  page asks the person which board it is. The README of the Flight Radar still shows READY with the version
  only: the firmware is the truth.
- Every field of SET may be left out, what is left out stays as it is.
- **The password and the key go in, never out.** GET has no `pass` field and `key` is true or false. The log
  line after SET names the network only. Keep it that way when adding a secret.
- Unknown lines are ignored without an answer, as the log and reset junk come through there too.

The list of settings is in the README, so scripts can use them. The Flight Radar's, as an example of the form:

- GET: the network's name, `nets` (all it remembers, names only), home (`lat`, `lon`, `place`, `country`),
  `src`, whether a `key` is stored, `up`, `flip`, `theme`, `look`, `map`, `ground`, `watch`, the radar options
  `vectors`, `beam_step`, `clock`, `block`, `autopilot`, `lost`, `conflict`, `fl_lo`, `fl_hi`, `utc` (seconds
  since 1970, 0 not known), `led`, `led_level`, `led_hold`, `led_near`, `led_red`, `version`, `newest` (the
  site's, `""` not heard yet), `uptime` and `ran` in seconds, `reset`, `help` (counted on the map of boards: 0
  never asked, 1 yes, 2 no), `bare`, `alerts`, `names`, the weather units `temp_unit`, `pressure_unit`,
  `wind_unit`, `rain_unit`, `cc` (home's country, `""` not known yet), `backlight`, `night`, and `screen` on
  the boards sold with several screens.
- SET: any of `ssid`, `pass`, `forget` (a network out of the five kept), `key`, `lat`, `lon`, `place`,
  `country`, `src` (`free` or `fr24`), `up` (degrees), `flip`, `theme` (0 to 4), `look` (0 radar, 1 map, 2
  classic, 3 weather), `wxpage` (not kept), `map`, `ground`, the radar options (each 1 on, 0 off; `fl_lo`,
  `fl_hi` flight levels, 0 no limit), `watch` (`""` empties it), `led` (bits: 1 SOS, 2 in range, 4 take-off,
  8 landing, 16 any near, 32 no data, 64 weather), `led_near` (1, 2, 5, 10), `led_level` (0 to 2), `led_hold`
  (0 till touched, 1 10 s, 2 while it lasts), `led_red` (4 or 22), `led_test` (1: run it once), `help`,
  `bare`, `alerts` (bits: 1 storm, 2 pressure, 4 air, 8 radiation), `names` (0 to 2), the four units (each 0:
  home's country's own), `backlight` (0 100%, 1 50%, 2 25%, 3 10%), `night` (0 off, else a step), `screen` (0 v1,
  1 v2, 2 v3 on the 2.8" CYD; 0 RGB, 1 BGR on the boards sold with two screens).
- A new home without `place` and `country`: the board asks OpenStreetMap what it is called.
- A network sent goes first in the five it remembers.

`uptime`, `ran` and `reset` are there for "it hung" reports: how long it runs, how long the run before lasted,
what it started after. A person presses READ WHAT IT HAS and the answer tells whether it restarted by itself.

Lines for the page's live screen of the board (the twin, `fake-board-testing.md`):

```
page   OWLOS SKY ON                 every 30 s while the page shows the board; 60 s unheard and it stops
board  OWLOS SKY BEGIN <server>     then one OWLOS PLANE {json} a plane, in the networks' own words
board  OWLOS SKY END <count>
page   OWLOS SKY OFF                the page went away
page   OWLOS PICK <hex>             a plane tapped on the page, 0: let go
board  OWLOS SEL <hex>              a plane tapped on the board
board  OWLOS WXPAGE <n>             the weather page turned on the board; also once at SKY ON and after a GET
```

Neither side echoes what the other told it. The firmware has a few more lines for tests on the bench (a kept
test sky, a key as if pressed, the e-paper's grey tables): keep such lines out of the public list, or mark
them TEMP and take them out.

## The board side: console.c

Two ports, one handler:

```c
static console_line_fn s_fn;
static SemaphoreHandle_t s_one;                        // one line at a time, whichever port it came on

typedef struct { char line[1024]; int n; } line_t;

static void take(line_t *l, uint8_t ch)
{
    if (ch == '\r') return;
    if (ch == '\n') {
        l->line[l->n] = 0;
        /* from "OWLOS " on: a reset (opening the port does one) leaves a few stray bytes in the
         * UART, and glued to the front of the first line they hid its command */
        const char *at = strstr(l->line, "OWLOS ");
        if (l->n && s_fn) {
            xSemaphoreTake(s_one, portMAX_DELAY);
            s_fn(at ? at : l->line);
            xSemaphoreGive(s_one);
        }
        l->n = 0;
        return;
    }
    if (l->n < (int)sizeof l->line - 1) l->line[l->n++] = (char)ch;
    else l->n = 0;                                     // too long for anything we know: drop it
}
```

- A buffer per port (`static line_t l` in each task), the mutex around the handler only. Both tasks can feed
  bytes at once, the handler touches NVS and shared state.
- Empty lines are skipped (`if (l->n && ...`): this is what lets the page send a `\n` first.
- Finding `OWLOS ` inside the line came from a contributor (chipguyhere) in 0.14: stray bytes from a restart
  no longer hide the first command, even from a page that sends no `\n` first.
- 1023 bytes a line, more is dropped silently. The FR24 key is up to 590: watch the sum when adding fields.

**UART0**: install the driver with an RX buffer only. The log keeps writing as it always did:

```c
if (uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0) != ESP_OK) { ... }
xTaskCreate(uart_task, "console", 4096, NULL, 3, NULL);
...
if (uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(1000)) == 1) take(&l, ch);
```

- Only when the console is on a UART (`#if CONFIG_ESP_CONSOLE_UART`). On the Tab5 (P4) the console is the
  chip's USB alone (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`): UART0 was never started there, and the UART
  driver waiting for it to answer held the core till the watchdog bit (from the code's note; the Tab5 runs on
  the site; `../../esp32-boards/references/boards/tab5.md`).
- On the JC8012 and the Waveshare 7 the console task's stack goes in PSRAM
  (`xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`): no deadline, and with code run from PSRAM nothing in it runs
  with the cache off (chipguyhere).
- The console task has 4096 bytes and runs `say_cfg` on every GET; a bench note says little room is left
  (reasoned: check the stack before adding to GET).

**USB-Serial-JTAG** (the chip's own USB, S3, C3, C5, C6, P4): read the FIFO with the low-level functions and
**no driver**. Installing `usb_serial_jtag_driver_install` takes over the log's way out too.

```c
#define CHIP_USB (SOC_USB_SERIAL_JTAG_SUPPORTED && !NO_CHIP_USB)   // the Guition 5" uses its pins for I2C
#if CHIP_USB
#include "hal/usb_serial_jtag_ll.h"

/* no driver: it would take over the log's way out too. the chip holds the host back while its
 * 64 bytes are full, so looking every 10 ms loses nothing */
static void usb_task(void *arg)
{
    (void)arg;
    static line_t l;
    for (;;) {
        uint8_t buf[64];
        int n = usb_serial_jtag_ll_rxfifo_data_available() ? (int)usb_serial_jtag_ll_read_rxfifo(buf, sizeof buf) : 0;
        for (int i = 0; i < n; i++) take(&l, buf[i]);
        if (!n) vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif
```

A board that uses the chip's USB pins for something else sets `NO_CHIP_USB` in its board file, and the USB
task is not built: the S3 boards with an RGB panel and a CH340 (crow50, crow70, jc8048w550, esp4827s043: I2C
on GPIO 19 and 20, the S3's USB pins) and the ZLX-ESP32-2 (C5: its USB pins 13 and 14 are the backlight and
the screen's DC). Reading those pins as USB would fight the I2C or the screen (from `board.h`).

**The answers** go out with plain `printf`. On chips with USB-Serial-JTAG the IDF default
`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` sends stdout to that port as well as UART0 (it is in each
build's sdkconfig with nothing asking for it; read from `components/esp_system/Kconfig`, whose help says output
only). So nothing else is needed, and it is why the input must be done by hand. The console is the primary on
UART0, `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`. Boards with both USB ports should work on either (built, not run
here). The JC8012 sets UART0 as its console on purpose: its CH340 port is the one to upload and watch on.

`console_start(on_line)` and the first READY come **last** in `app_main`, see the traps below.

Task priority 3 is under the fetch task's 4. During a TLS handshake the console waits: **HELLO was answered
about 2.7 s late** (measured). The priorities stayed, the page retries, see below.

### A flat JSON reader, no library

`json_str()` and `json_num()` in `console.c`: find `"key":` in a flat object, skipping the inside of other
strings. `\uXXXX` goes back to UTF-8 (a letter written as its number), `\n \t \\ \"` are handled. Used for
SET, for `version.json` and for OpenStreetMap's answers. Fine for what we send, not a general parser. The
other way, `json_esc()` in `main.c` escapes `"` and `\` and turns control characters into `?`.

Two fixes it needed, both from the code:

- `\u` reads up to four hex digits and no further. A text cut short after `\u` (Nominatim's answer is cut at
  2 KB) was read past its end, and a short one like `\u12"` took the closing quote with it. `\u` with no digit
  is left out.
- `json_num` answers "not found" for NaN and infinity (`isfinite`): a NaN home passed every range check and was
  kept.

### The handler, in order

```c
static void on_line(const char *line)
{
    if (!strncmp(line, "OWLOS HELLO", 11)) { say_ready(); return; }
    if (!strncmp(line, "OWLOS GET", 9)) { say_cfg(); return; }
    if (!strcmp(line, "OWLOS SKIP")) { s_skip_align = 1; return; }
    /* SKY ON / OFF, PICK, and the test lines */
    if (strncmp(line, "OWLOS SET ", 10)) return;
    const char *j = line + 10;
    /* copy the current settings under the lock, overwrite what the line has */
    /* range-check each field: bad values are skipped, one that matters answers: */
    if (c.lat < -90 || c.lat > 90 || c.lon < -180 || c.lon > 180) { printf("OWLOS ERR home is not on Earth\n"); return; }
    store_save_secrets(&sec);
    store_save_cfg(&c);
    /* into the live state under the lock, then */
    if (wifi) net_reconnect(&sec);
    ESP_LOGI(TAG, "settings from the site: network \"%s\", ...", sec.ssid, ...);   /* never the password */
    printf("OWLOS OK\n");
    xTaskNotifyGive(s_fetch);                          // a new setting cuts the wait short
}

static void say_ready(void)
{
    uint8_t m[6] = { 0 };
#if CONFIG_IDF_TARGET_ESP32P4
    esp_read_mac(m, ESP_MAC_BASE);     // the WiFi's MAC is the C6's: the P4's own base one, as fixed to this board
#else
    esp_read_mac(m, ESP_MAC_WIFI_STA);
#endif
    printf("OWLOS READY flight-radar %s %s %02x%02x%02x%02x%02x%02x\n", FLIGHT_VERSION, board_name(), m[0], m[1], m[2], m[3], m[4], m[5]);
}
```

Save, then apply, then OK: OK means it is in flash.

## The page side since 0.8: class Link

`../files/flight-radar/site/link.js`. Web Serial, Chrome or Edge, https or localhost, and a click: only a person
can pick a port, but a port the browser allowed before comes back with `navigator.serial.getPorts()`.

- `open()`: `port.open({ baudRate: 115200, bufferSize: 65536 })`. A port that does not open says why:
  "Another tab or program may have it: the previous page, a serial monitor. Or its USB chip stopped answering:
  take the cable out and put it back".
- Then the signals: the browser opens a port with DTR and RTS both on. **RTS goes off first, then DTR**: on the
  usual two-transistor circuit EN is never pulled low that way, and on the chip's own USB the line state never
  passes through "reset". So opening does not restart the board. DTR first did, on every open, and on the
  chip's own USB under Windows, whose driver may not send an RTS change alone, it could hold the chip in reset:
  a dark screen until the cable came out. (Read from source: the fix and its note in `link.js`. The older
  measurement, that opening a port restarts a CYD even with DTR and RTS off, was made with both dropped at
  once, and by .NET's SerialPort, which opens with both up; it still holds for those.)
- `pump()`: a reader loop. `\r` and ANSI colours are stripped (IDF's log is coloured). A line that starts
  `OWLOS ` and is not a PLANE or SKY line goes to a queue of 64 for `expect`; every line goes to `onLine` for
  the log. A `BreakError`, `BufferOverrunError`, `FramingError` or `ParityError` breaks only the read: a new
  reader carries on. A restarting board is full of them; a CP210x reports them, a CH340 hardly ever. Any other
  error is the port going away.
- `send(line)`: a newline first, `\n` + line + `\n` (bytes garbled while the board restarts would otherwise
  glue onto the command). One writer at a time, chained; a write that failed fails alone. A write the cable has
  not taken in 2 s is aborted: a USB chip that stopped answering would otherwise hold the writer for ever, and a
  port with a writer on it can never close again.
- `hello(tries = 8)`: HELLO, then 800 ms for READY, up to eight times; returns `{ version, build, mac }`, or
  null for another firmware.
- `config()`: GET, 3 s for CFG, `JSON.parse` of it. So `json_esc` on the board has to be right for odd network
  names.
- `close()`: SKY OFF if the board was sending planes, every write done or dropped, the reader cancelled within
  1 s, the port closed within 2 s. One link per port (a `WeakMap`): a port left open by a try that failed is let
  go before the next try.

The page's own log of what it sent masks the secrets: `"pass"` and `"key"` values show as `***`.

## The settings form (the 0.4 page, now at previous/)

The same lines, from a `class Board` that opened the port at 115200, dropped DTR and RTS together
(`setSignals({ dataTerminalReady: false, requestToSend: false })`: "the board runs; on the CYD this may restart it,
it then says READY anyway"), and sent HELLO 14 times 800 ms apart, 11 s in all ("a board that just restarted needs
a second or two"). Its `connect()` said each failure in words:

- no `serial` in `navigator`: "This browser has no Web Serial. Use Chrome or Edge on a computer or a Chromebook."
- `NotFoundError`: "No port was picked."
- `InvalidStateError` or `NetworkError`: "The port is busy: close the flasher's window (or any serial monitor) and
  try again."
- anything else: "Could not open the port: <message>".
- no READY: the wrong port, or nothing flashed. READY of another name: other firmware.

A line starting `OWLOS ` went to the queue `expect` reads; the rest into a `<details>` called What the board says,
and to `onlog`. What it did with the answers is still the model for a settings form:

```js
b.onlog = line => {
  let m;
  if ((m = line.match(/WiFi up, ([\d.]+)/))) { up = true; /* On the WiFi as ... */ }
  else if ((m = line.match(/WiFi lost \(reason (\d+)\)/)) && !up && ++fails >= 3) finish('The board can\'t join the WiFi: ' + wifi_why(+m[1]) + '.', 'bad');
  else if ((m = line.match(/: HTTP (\d+), \d+ bytes in \d+ ms, (\d+) planes in it/))) { /* 200: done, N planes */ }
};
function wifi_why(code) {
  if ([15, 202, 204].includes(code)) return 'the password looks wrong';
  if ([201, 210, 211].includes(code)) return 'the network is not found: is it 2.4 GHz, and is the name exact?';
  return 'reason ' + code;
}
```

- After a SET with a new WiFi, `onlog` reads **the firmware's own log text** to tell the person how it went. So
  those `ESP_LOGI` lines are part of the protocol. Reword one and the page goes quiet. 40 s timeout.
- SET, then `expect(['OWLOS OK', 'OWLOS ERR'], 5000)`: no answer is "The board did not confirm the settings",
  ERR is "The board refused: <why>". Then GET, said back in words, then the WiFi watch above.
- One `busy` flag and a `finally` that closes the port: the port is held only during a job, so the flasher can
  have it in between.
- Fields left empty are NOT sent: "keep as it is" is a select option with value `""`, an empty WiFi keeps the
  WiFi set on the board's own screen. A list that must be emptied has a word: `none` becomes `""`.
- Lengths checked in **UTF-8 bytes** with `TextEncoder`, the board's arrays are bytes: SSID 32, password 8 to
  63, town 39, country 31. `clip()` cuts a found name to fit.
- Round the numbers: `Math.round(lat * 1e5) / 1e5`.
- The form has `autocomplete="off"`, the password is `type=password`. The page keeps nothing.
- Extra READYs from the HELLO retries stay in the queue and hurt nothing, `expect` looks by prefix.

## The traps here, as they were met

**A setting sent the moment READY came was lost.** `app_main` printed READY and started the console BEFORE the
fetch task existed. The site sends SET the moment it reads READY: the SET was lost (no console yet) or hit
`assert xTaskGenericNotify (xTaskToNotify)` and reset the board (SW_CPU_RESET on 0.2). Fix: `console_start` and
READY last in `app_main`, READY about 1.0 s after reset instead of 0.5. Rule: READY is a promise, say it when
everything it promises exists. ViewOwl's flasher met the same: settings sent early were lost, and it waits for
its READY.

**Junk from the reset glued onto the first command.** Opening the port resets a CYD; garbled bytes from the
restart sit in the board's line buffer, and `garbageOWLOS GET` is not a command. Measured with the rig: GET at
READY worked 2 of 6, with `\n` sent first 6 of 6. Both 0.2 and 0.3 skip empty lines, so the fix was on the page
alone. Since 0.14 the board also finds `OWLOS ` inside the line.

**HELLO answered about 3 s late.** During TLS, see above. The old page sent HELLO 14 times, 800 ms apart, 11 s
in all; the new one 8 times.

**Opening the port restarts the board**, with the lines dropped together. The CH340 line's EN reset also clears
RTC memory: the run record moved to NVS once a minute. The old page said "it may restart it, it then says READY
anyway". The new page drops RTS first (above).

**The flasher still holds the port.** ESP Web Tools closes it when its dialog closes. The busy error says so.

**A board on its own USB said hello before the port was open.** ESP32 Web Flasher: the page sends `status` after
connect. And such a board's port vanishes on each restart: the read loop ends with an error, the page says press
CONNECT again, or (the new page) finds it again by its MAC when the browser's `connect` event comes.

**A NaN home was kept**, and **a cut `\u` read past the end**: the JSON reader fixes above.

## A test rig with no browser

`../files/rigs/cfgrig.ps1` does what the page does from PowerShell: opens the port, HELLO till READY, sends
lines, keeps CFG in a file, prints only field NAMES that differ, masks every setting a person typed. `-Mac`
sends nothing unless READY names that MAC.

```powershell
& .\cfgrig.ps1 -Port <port> -Mac aa:bb:cc:dd:ee:ff -Send @('OWLOS GET') -SaveCfg before.json -Seconds 8
# flash the new version, no erase
& .\cfgrig.ps1 -Port <port> -Mac aa:bb:cc:dd:ee:ff -Send @('OWLOS GET') -CompareCfg before.json -Seconds 8
```

Run it in-process with `&`. Through `powershell -File` the JSON loses its quotes. The rule behind the masking:
the board holds a real person's WiFi and home, and the test output goes into a chat.

The compare leaves out what changes by itself: `uptime`, `ran`, `reset`, `version`, `newest` (empty after a
flash till the board asks the site again), `utc`, and `cc`. Home's country is asked from OpenStreetMap at every
start and kept in RAM only; after several quick restarts OpenStreetMap refuses for 5 to 10 minutes, and a
difference in `cc` alone right after flashing is that, not lost settings (measured on the round board).

## The minimal form

ESP32 Web Flasher does the same with three typed words and no JSON: `ssid NAME`, `pass PASSWORD`, `join`,
`forget`, `status`, in any terminal. The board echoes `pass ********`, always eight stars, not even the length
goes back. `../files/web-flasher/firmware.ino`, `index.html`. Take this form for a new project with few
settings, the OWLOS line form when a page must read the answers.

## A firmware's own file protocol (FusionEdge)

FusionEdge, an internet radio, keeps its WiFi list in a file on LittleFS, and its firmware already speaks a
serial file protocol of its own. Its flasher page sends the WiFi through that (`site/wifi.js` of the fork):

- 460800 baud, the firmware's own `Serial.begin`. Lines start `MRSPIFS|`: `HELLO` (answered
  `MRSPIFS|OK|HELLO`), `BEGIN`, `READ|0|/data/wifi.csv` (`READ_BEGIN`, `DATA` lines, `OK|READ_END`),
  `WRITE_BEGIN|0|<file>|<length>`, `WRITE_DATA|0|<base64>`, `WRITE_END`, then `REBOOT`.
- The file is `name<TAB>password` a line, five kept, a name kept to 29 bytes and a password to 39 by the
  firmware: the page checks those. The new network goes first, the others stay.
- After REBOOT the page waits up to 60 s for `Web UI: http://<address>/` or `Running in AP mode` and shows the
  address.
- The same rules as here: RTS off, then DTR off, at open; a newline before each line; the line errors of a
  restart are not the port going away. The name and password go to the board on the cable and nowhere else.

How sure: built and on that project's live page; not run end to end here. Use a firmware's own protocol when it
has one, rather than adding a second console.
