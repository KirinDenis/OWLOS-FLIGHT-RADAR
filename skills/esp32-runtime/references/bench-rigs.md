# Debugging a board over its serial port

The bench rigs: small PowerShell scripts an agent runs against a real board on its USB cable, each one asking
the board one question over its console and printing only words that name nothing of the person. They are in
[../files/bench/](../files/bench/). They were made for OWLOS Flight Radar's console, and the way they work
carries to any firmware that speaks text lines over its serial port.

How sure: the four words of the collection's README. What a rig found on a board is [measured]: these rigs ran
on the bench boards (2.8" CYD, 3.5" ST7796, the round C3, the 4.2" e-paper S3). The files here are cleaned
copies: near-duplicates merged, every port, MAC, path and name made a parameter, a shared helper taken out,
and the put-back added where a rig changed a setting and left it. After cleaning they were checked by
PowerShell's parser and `bash -n` only, not run on a board again: that they behave as the originals did is
[reasoned].

The flashing side (chip and MAC check, backups, flashing a build or a site folder) is in esp32-flash-and-settings:
[chip-and-mac-check.md](../../esp32-flash-and-settings/references/chip-and-mac-check.md) and its
`files/rigs/`. The protocol from the site's side is in
[usb-console.md](../../esp32-flash-and-settings/references/usb-console.md). This file does not repeat them.

## How the agent talks to a board

### The port

- A COM number is not a board. Numbers move when boards are replugged, and a board was once blamed for another's
  fault that way [measured]. So `-Port` is required by every rig, every example port in the headers (COM5) is
  only an example, and the rigs that restart a board through esptool first check its MAC, and its chip when
  `-Chip` is given.
- The MAC comes from your own list of boards, kept outside every repository. A board that runs the firmware
  says its MAC itself: `OWLOS HELLO` is answered `OWLOS READY flight-radar <version> <build> <mac>` [docs, read
  in main.c's say_ready]. The rigs never print a MAC, only that it matched or not.
- One program on a port at a time. A browser tab with the site connected, a person's `idf.py monitor`, or
  esptool holds it, and the rig gets "Access is denied" [measured]. Read a person's monitor, never kill it.
- Opening the port can restart the board. With DTR and RTS set low before the open, the CH340 boards of the
  bench went on as they were [measured, the rigs' own notes]. With both up at the open and dropped 150 ms
  later, they restarted every time [measured]. An older note in usb-console.md records a CYD that restarted
  even so; when it matters, read `uptime` with `get-fields.ps1` before and after [reasoned]. Boards on the
  chip's own USB were not run with these rigs [reasoned that they may differ].
- Keep one long reader on a port rather than many short peeks: every open may restart a board and ends what it
  was doing [measured].

### The line protocol

One text line each way at 115200 baud, on the same line as the board's log. The board finds `OWLOS ` anywhere in
a line, because a restart leaves stray bytes in front of the first command; the rigs also send a newline first,
as the site does [docs, read in console.c]. What OWLOS Flight Radar 0.17 takes (`on_line()` in
`firmware/main/main.c`) [docs, read in the code]:

- `OWLOS HELLO`: answered `OWLOS READY flight-radar <version> <build> <mac>`, the MAC as 12 hex digits.
- `OWLOS GET`: answered by one line, `OWLOS CFG {json}`, every setting the board keeps plus `version`, `newest`,
  `uptime`, `ran` (how long the run before lasted), `reset` (why it last started) and `utc`. It holds the WiFi's
  name, the remembered networks, the home (`lat`, `lon`, `place`, `country`), its country `cc` and the watch
  list. A GET also makes the board tell a listening page its weather page again.
- `OWLOS SET {json}`: any fields, the rest stay. Answered `OWLOS OK` once the settings are taken, or `OWLOS
  ERR <why>`. Only a home off the Earth gets ERR ("home is not on Earth"). **A value out of range is skipped in
  silence and the board still says OK**: `backlight` 7, `temp_unit` 7, `wxpage` 5 all answer OK and change
  nothing. A rig reads the field back with GET to know.
- `OWLOS SKY ON` and `OWLOS SKY OFF`: a page listens. While it does (60 s from the last SKY ON) the board sends
  every answer of the plane servers as `OWLOS SKY BEGIN`, one `OWLOS PLANE {json}` a plane with its position,
  `OWLOS SKY END`; and says `OWLOS WXPAGE n` when the weather page turns, `OWLOS SEL hex` when a plane is
  picked on its glass, `OWLOS ALIGN n` while its touch waits for alignment. The planes give away the home's
  area [reasoned]: no rig prints them.
- `OWLOS PICK <hex>`: a plane picked on the page, 0 lets go. `OWLOS SKIP`: leave the touch alignment.
- E-paper builds only: `OWLOS KEY n` presses a key as if a finger did (1 up, 2 down, 3 ok, 4 ok held, 5 menu,
  6 menu held, 7 back, from `core/ui.h`). `OWLOS EPDTEST` prints six test bands. `OWLOS EPDLUT <index>
  <hex value>` changes one byte of the grey waveform in RAM; `OWLOS EPDLUT` followed by any word that is not a
  byte and a value puts the panel's own waveform back. `OWLOS PAPER n` changes the sheet's tones till a reset.
- A few more test lines exist in some builds (a kept test sky, a scroll test); read `on_line()` for them.

A trap found while cleaning the rigs: `OWLOS EPDLUT` with nothing after it is not a command at all, because the
board compares 13 characters, trailing space included. The scratch grey rig's `-Reset` sent exactly that, so it
never put the waveform back [docs, read in main.c; reasoned that the reset did nothing]. The clean rig sends
`OWLOS EPDLUT all`.

### Reading the log while it runs

The log and the answers come on the same line, so a rig reads every line and decides what it is. Nothing is
printed raw:

- The CFG line is never printed. A rig prints only the fields it names; `ssid`, `nets`, `lat`, `lon`, `place`,
  `country`, `cc`, `key` and `watch` are refused by name.
- The log itself names the network ("WiFi up, <address>, network ...", "WiFi started, network ..."), every
  request URL carries the home, and the line after a SET says the network and the home to four decimals [docs,
  read in main.c and net.c]. So a log line is printed only when it matches a known shape and is cut to its
  numbers: the 10 s frame line, the layer times, the second core, the map's line, the weather's line, the
  planes' status line, the WiFi's ups and downs without names, the e-paper's refreshes, the keys, the touch
  driver's start, the light. Anything else (an E or W line, a tap) only when it has no web address, no quoted
  text, no network, no home, no watch list, no IP address and no number with four decimals.
- Long logs go to a local file whole (without the CFG line), and are read only through `log-summary.sh`, which
  prints counts.
- Results are told by counts and booleans: "113 of 113 maps whole", "look changed after 0", "differ: none".

All of this lives in one helper, `bench-serial.ps1`, so every rig keeps the same rules. Adapting the rigs to
another firmware means changing three lists there (the private fields, the fields one SET can put back, the
crash words) and the shapes in `Get-KnownLine` [reasoned].

### Settings put back as found

A rig that changes a setting reads GET first and keeps the answer in memory. At the end, in a `finally` block,
`Restore-BoardCfg` asks GET again, sends one SET with every numeric field that moved (look, range, units, light,
the LED, the radar's options and the rest), asks GET a third time to see it took, and prints "put back as
found: look, range" or what did not go back. A private field that differs is named, never set. Because it is a
`finally` block, Ctrl+C puts things back too: PowerShell stops the rig at the next line, the serial read waits
200 ms at most, and the block runs. The helper writes those lines to the host, so they show while the rig
stops [reasoned].

Why it matters: the cycle rig used to leave every board at a 5 km range [measured, its own note], and the
scratch rigs for the units and the light set AUTO, full light and no night at their end, whatever the board had
before [docs, read in their code]. What is not a setting is put back too by the rigs that change it: the
weather page by `weather-page.ps1` and `weather-pages.ps1` (it is kept in RAM only and read through SKY ON and a
GET), the e-paper's grey waveform by `epaper-grey.ps1` (RAM only). `epaper-keys.ps1` puts back every setting but
not the weather page, which a restart brings back to the dial [docs, read in main.c]. The one rig that leaves its
change is `send-wifi.ps1`, because that is its job.

### Running a rig

- Run a rig in-process from PowerShell with `&`: `& .\cycle-looks.ps1 -Port COM5 -Out c.log`. Through
  `powershell -File` a JSON argument loses its quotes, and the board then answers OK to a SET that set nothing
  [measured with cfgrig.ps1]; an array like `-Looks 0,1,2` arrives as one string there [reasoned].
- Keep the rigs together: each loads `bench-serial.ps1` from its own folder.
- Windows PowerShell 5.1 is enough. esptool is needed only by `boot-listen.ps1`, `touch-boot.ps1` and the
  flashing rig: IDF's own Python by default, or `-Python`, or `python` with pip's esptool on PATH. The identity
  check reads esptool 4.10's words ("Chip is", "MAC:"); a newer esptool that words them otherwise makes it
  refuse, which is the safe side [reasoned].
- Several boards at once: one PowerShell window per board, each with its own port [measured: the soak ran on
  three boards at once].

## The order of a bench session

1. **Identify** each board by chip and MAC, never by its port (chip-and-mac-check.md), or by `OWLOS HELLO` for a
   board that runs the firmware.
2. **Back up** a board that holds anything of value: `backup-board.ps1`, the whole flash, in a folder no cleanup
   touches. A temp folder was cleaned once and took working files with it [measured].
3. **Save its settings**: `cfg-snapshot.ps1 -Out cfg-COM5-before.json`.
4. **Build and flash**: `release-build.cmd` for a build from nothing as CI makes it, then `flash-one.ps1`; or
   `flash-and-check.ps1` with a site folder, as the browser flasher writes it. Only after "Project build
   complete", and only when 3 of 3 parts say "Hash of data verified" [measured: a failed build leaves the old
   .bin and esptool flashes it happily].
5. **Compare the settings**: `cfg-snapshot.ps1 -Before cfg-COM5-before.json`. After a flash without erase,
   nothing but new fields may differ.
6. **Functional rigs**: `func-checks.ps1`, every console rig in turn.
7. **Soak**: `soak-board.ps1` on every bench board at once, before every beta.
8. **Read the soak**: `bash log-summary.sh` on each log. Restarts, maps whole, warnings, the lowest heap.

## The rigs

Each file's header says what it answers, how to run it, what it needs and what it changes. Here: the question,
the line to run, how to read what it prints, and what it found when that is known. COM5 and aa:bb:cc:dd:ee:ff
are examples throughout.

### bench-serial.ps1: the shared helper

Not a rig. Opens a port with or without a restart, sends a line with a newline first, reads lines without their
colours, knows the shapes of the firmware's lines (`Get-KnownLine`, by topic: console, frames, memory, map,
weather, planes, net, epaper, keys, touch, light, crash, warn), listens for some seconds (`Watch-Board`), reads
GET into an object (`Get-BoardCfg`), puts settings back (`Restore-BoardCfg`), reads the weather page through
SKY ON (`Get-BoardWxPage`), and checks a board's MAC and chip through esptool (`Test-BoardIdentity`).

### cfg-snapshot.ps1: are the settings the same as before?

```powershell
& .\cfg-snapshot.ps1 -Port COM5 -Out cfg-COM5-before.json
& .\cfg-snapshot.ps1 -Port COM5 -Before cfg-COM5-before.json -Settle 20
```

Saves GET's answer to a local file (it holds the WiFi and the home: keep it out of repositories and chats), or
compares with one and prints "N fields the same; differ: ...; only before: ...; only now: ...", names only. Left
out because they change by themselves: `uptime`, `ran`, `reset`, `utc`, `newest`, `version`, `cc`. The last is
the home's country, asked from OpenStreetMap at every start; after several quick restarts OpenStreetMap refuses
for 5 to 10 minutes and `cc` stays empty for that while [measured on the round board]. Changes nothing
[measured that it works].

Found: the beta round trip (beta, the live stable over it, the beta again, no erase) kept every setting, 44
fields the same on two CYDs, 2026-11-19 [measured].

### get-fields.ps1: what does it say for these fields, and nothing else?

```powershell
& .\get-fields.ps1 -Port COM5 -Names look,range,uptime,reset
& .\get-fields.ps1 -Port COM5 -Names led,look,alerts -LedTest
```

Prints `name=value` for each field, `(none)` for one this build does not have, `(private, not shown)` for the
private ones. `-LedTest` runs the LED's test once (`led_test`, nothing kept), for a person to look at the LED.
`uptime` before and after another rig tells whether that rig restarted the board [measured that it works, in the
0.16.1 functional pass].

### try-setting.ps1: does it take this value, draw with it, and not restart?

```powershell
& .\try-setting.ps1 -Port COM5 -Field alt_unit -Value 1 -Listen 60
& .\try-setting.ps1 -Port COM5 -Field names -Value 2 -Listen 30
```

The field before, the board's OK or ERR, the field after, then the console and any restart or crash while the
board draws with it (`-Topics` for more), then everything put back. "after" equal to "before" with an OK means
the value was out of range. Only numeric fields one SET can put back are taken. Merged from two rigs that did
this for the map names' style and the height's unit [measured that they worked].

### weather-units.ps1: are the weather's units taken, and a wrong one refused?

```powershell
& .\weather-units.ps1 -Port COM5
```

Sets F, mmHg, m/s and inches, then inHg and knots, then `temp_unit` 7, which does not exist, printing the four
units after each. Read: each step shows its values; after the 7, `temp_unit` is still 2. It prints whether the
home's country is known, not the letters. All four units are put back as found [measured that the steps work].

### weather-page.ps1: does the board tell a listening page its weather page, once per change?

```powershell
& .\weather-page.ps1 -Port COM5
```

SKY ON and a GET: the board names its page. SET 2: `OWLOS WXPAGE 2`. The same again: nothing more. GET: told
again. SET 5: OK and nothing more. Then back to the page it was on, and SKY OFF. A `WXPAGE` line where none is
due, or none where one is due, breaks the twin's sync [measured that the steps work]. Only WXPAGE, OK or ERR and
crashes are printed: never a plane.

### weather-pages.ps1: how fast is each weather page, with the panel and without?

```powershell
& .\weather-pages.ps1 -Port COM5 -WithBare -Each 20
```

Puts the board in the WEATHER look, turns pressure, temperature and the dial, each for `-Each` seconds, and with
`-WithBare` does it all with the radar's panel and without (`bare` 0 and 1). Prints the 10 s frame line with the
heap, ms a frame per layer, the second core, the weather's own line. Read the heap: a page that takes it down
step by step holds something it should give back. Look, panel and page go back as found [measured that it
works; numbers of this kind for the weather look are in SKILL.md, Memory per board].

### screen-light.ps1: does the light turn down, refuse a wrong level, survive a restart, follow the night?

```powershell
& .\screen-light.ps1 -Port COM5 -Step 2
```

Only on a board whose CFG has `backlight`. Sets the level, then 7 (skipped: the level must stay), restarts the
board (an RTS pulse) and reads the level again (kept in flash), then sets `night` and listens for "the sun at
home: down, the night's light" and "the screen's light: 25%". The night part needs a home and the time; the rig
says whether the time is known. Backlight and night go back as found [measured that the steps work].

### send-wifi.ps1: does the board join this network with no browser?

```powershell
& .\send-wifi.ps1 -Port COM5
```

**A person runs this one, in their own window. An agent never types, reads or sends a WiFi password.** It asks
for the name and the password at run time, the password hidden, sends them as the site does, and drops the
password at once; nothing of either is printed or written. Then a minute of: the board's OK, "WiFi up" without
its address or name, "WiFi lost" with what the reason means (15, 202, 204 the password looks wrong; 201, 210,
211 the network is not found) [docs, the site's own reading of those codes], the first maps, a new e-paper sheet.
The network stays on the board, first of the five it keeps: that is the rig's job [measured that it works].

### cycle-looks.ps1: does the map come back whole when looks and range change every few seconds?

```powershell
& .\cycle-looks.ps1 -Port COM5 -Out cycle-COM5.log -Minutes 20 -Looks 0,1,2,3
bash log-summary.sh cycle-COM5.log
```

Restarts the board at the open, waits for its first CFG line (nothing is changed before it came), then from 45 s
sends look after look every 5 s and a new range every round, the way the site sends them, then leaves the board
alone for `-Settle` minutes. The whole log goes to the file with each command marked RIG; look and range go back
at the end. Read it with `log-summary.sh`: maps whole of maps made, map memory warnings, restarts.

Found:
- A tester's "the map goes away": 227 commands, 150 of 150 maps whole on the CYD, the same on the version
  before, which showed it depended on their place, not the code [measured].
- The 3.5"'s square map now and then came up empty for 15 seconds: memory enough, but in pieces too small for
  the tile reader. With smaller pieces and strips of 6 rows instead of 8, 20 minutes of looks and ranges every
  5 s made 153 maps of 153 whole (0.16.1) [measured].
- The rig itself left every board at 5 km before the put-back was added [measured].

### looks-stress.ps1: does a running board survive its looks switched under it?

```powershell
& .\looks-stress.ps1 -Port COM5 -Seconds 120 -Every 3
```

Switches 3 0 3 1 3 2 3 0 1 2 3 0 3, the WEATHER look between each of the others, so the second core starts and
stops and the dial's tables are taken and given back every few seconds. Prints each switch, the second core's
start and stop with the heap, the frame line, any crash [measured that it works]. Read: no RESET OR CRASH, and
the heap at each "stops drawing" about where it was the time before [reasoned]. A board with one core prints no
second core lines [docs, read in main.c].

### look-timing.ps1: how fast does each look draw, and where does a frame go?

```powershell
& .\look-timing.ps1 -Port COM5 -Looks 0,1,2,3
& .\look-timing.ps1 -Port COM5 -Looks 3 -Restart -Wait 45
```

Sets each look, lets its first 10 s go (the switch is in them), then prints the 10 s log: fps, ms a frame, view,
planes, heap; ms a frame per layer (background, disc, compass, planes, panel, the rest, sending, touch); the
second core's wait and stack; internal RAM on PSRAM boards [measured that it works]. `-Restart -Wait 45` is the
WEATHER look after a fresh start with its forecast in. Read: `sending` near `W*H*16 / SPI clock` means the bus is
the limit (61 ms at 480x320 and 40 MHz) [reasoned from the arithmetic]; a layer that grows after a minute is state
filling up [reasoned].

### look-watch.ps1: what happens when the board goes into this look?

```powershell
& .\look-watch.ps1 -Port COM5 -Look 1 -Listen 30 -Out look1.txt
```

The look before and after, the e-paper's refreshes ("4-grey refresh, N ms (N px of grey were wrong)", "drawn on
top, N px, N ms, grey debt N"), the map's line, the keys, E and W lines that name nothing of the person, and every
line of a crash: Guru Meditation, Backtrace, PC, EXCVADDR, EXCCAUSE, rst:, task_wdt, ELF file SHA256. The
backtrace goes to addr2line with the ELF of that very build ([crashes.md](crashes.md)). Merged from a crash
catcher and the e-paper look rig [measured that they worked].

### map-range.ps1: at this range, does the map come whole, and how much heap is left?

```powershell
& .\map-range.ps1 -Port COM5 -Range 120 -Seconds 40
```

Sets the range, prints only the map's lines ("map: zoom <z>, <got> of <asked> tiles, made in <ms> ms, heap <n>,
at least <n> after the tiles came, <n> after reading them") and map memory warnings, then puts the board's own
range back and listens 20 s more [measured that it works]. Read: got equal to asked is a whole map; "at least ...
after reading them" is the budget the tile reader left [docs, read in main.c]. The same kind of change in a
loop, printing only counts, is how the keep-alive crash and its fix were proven: a tile that came broken must
drop the connection, or the next request crashes inside mbedTLS [measured].

### listen-board.ps1: what is a running board saying, without touching it?

```powershell
& .\listen-board.ps1 -Port COM5
& .\listen-board.ps1 -Port COM5 -Topics map,crash -Seconds 60
& .\listen-board.ps1 -Port COM5 -Topics keys,epaper,map,warn,crash -Seconds 240 -Out keys.txt
```

The known lines of the topics asked, with seconds. The third line is the recorder for a person pressing an
e-paper board's keys or touching a glass: what they did lands in a file that is safe to share. `-Restart` for a
listen from the boot. Merged from a frames listener, a map listener and the key recorder [measured that they
worked].

### log-board.ps1: the whole log for hours

```powershell
& .\log-board.ps1 -Port COM5 -Out long-COM5.log -Minutes 120
& .\log-board.ps1 -Port COM5 -Out long-COM5.log -Minutes 120 -Restart
```

Every line with the seconds into a local file, the CFG line left out. Without `-Restart` a board someone is using
goes on as it was. The file holds URLs and the network's name: read it only through `log-summary.sh`. Then read
the trend of the heap and of "at least", not only the crashes.

Found with a long logger like this one: Flight Radar 0.14 on three boards, two hours each, no restart, the heap
at fetch time flat; WiFi Radar on the 2.8", 2 hours, no reboot, heap 49 KB [measured].

### boot-listen.ps1: how long after a restart until it says this?

```powershell
& .\boot-listen.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff
& .\boot-listen.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff -Chip esp32c3 -Until 'map: zoom' -Seconds 60
```

Checks the MAC (and the chip), which restarts the board through esptool, then listens from its boot: the
weather, the network's statuses, the console, crashes, the look it came up in, and "'pattern' seen after N s" or
not seen. By default it waits for the weather's first answer [measured that it works]. The matched line is
printed only when it names nothing of the person.

### net-diag.ps1: what does the network do after a start, and when?

```powershell
& .\net-diag.ps1 -Port COM5 -Seconds 150
```

Restarts at the open, then prints with the board's own clock: each plane request (server, HTTP code, bytes, ms),
the site's version check, the map of boards' count, the weather, a watched plane's lookup (its code only), WiFi
started with how many networks kept, looked round, up, lost with its reason, on to the next network, asking for
the time, esp-tls and HTTP client failures with their code [measured that it works]. Read: mostly 200 at the
planes' pace; a 429 now and then is a server's limit (the soak of 2026-11-21 saw 4 to 15 of them a server in 18
minutes, beside 150 to 180 answers of 200) [measured]; a TLS failure next to "WiFi lost" is the network, not the
code [reasoned].

### get-repeat.ps1: does GET answer whole, again and again?

```powershell
& .\get-repeat.ps1 -Port COM5 -Times 10 -Gap 2
```

Restarts at the open, waits 35 s so GET meets a busy board, then asks GET `-Times` times: for each, whole or CUT,
its length, its version and reset fields only, and crash text found inside the line. Why: the CFG line grows with
every setting, and the console task that prints it has 4 KB of stack. On 2026-11-18 a GET overflowed it on the
2.8" and the 3.5" and restarted them; the fix moved the line's buffers to statics [measured, the note in main.c's
say_cfg]. Run it after adding a field.

### touch-boot.ps1: does the touch take a tap nobody made at the start?

```powershell
& .\touch-boot.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff -Times 10
```

Reads the look, then restarts the board `-Times` times through esptool (the MAC checked each time) and for each
prints what the touch driver says at its start and the look it came up in, with LOOK CHANGED when it moved. The
pass: "the look changed after 0". Nobody may touch the glass meanwhile.

Found: now and then after a restart the 3.5" went on to the next look by itself, as if its look button had been
touched [measured]. The fix in 0.16.1: a press counts once two reads in a row say it, and the glass is not read
in the first 1.5 s after power on [docs, CHANGES.md]. In the 0.16.1 functional pass this rig restarted three boards 13 times (5 on each
CYD, 3 on the round board) and every one came back in the look it had [measured]. On the PC, the old driver made
191 phantom presses in 400 idle samples, the new one 0 (testing.md) [measured on the PC]. Right after the fix, 10 of
10 restarts on the 3.5" and 5 of 5 on the CYD kept the look [measured]. Before it, 13 restarts had not shown the
fault once, so the fix stands on the mechanism and the PC test, not on a bench before and after: a rare fault
needs a rig on the PC that can make it happen.

### epaper-keys.ps1: what does an e-paper board do when its keys are pressed?

```powershell
& .\epaper-keys.ps1 -Port COM5 -Keys "5:10,1:8"
```

Each step is key:seconds. Prints the keys taken at once ("key <n>: look <n>, view <n>, range <r>"), the gathered
ones ("key <n> gathered", then after 2 s of quiet "keys taken after the quiet: changed; look <n>, range <r>,
page <n>", or "as it was"), every refresh, the map, warnings, crashes. Every setting goes back as found. This is how the gathered
presses and the frame hold of the 4.2" were checked with no finger [measured that it works; the behaviour itself
is in SKILL.md, The e-paper loop].

### epaper-grey.ps1: which waveform bytes give four greys the eye can tell apart?

```powershell
& .\epaper-grey.ps1 -Port COM5
& .\epaper-grey.ps1 -Port COM5 -Pokes "141:47,99:49"
```

Starts from the panel's own waveform, changes the bytes given (index in decimal, value in hex), prints the six
test bands (black, dark grey, light grey, white, dots 1/8, dots 1/2), which stay on the glass for 10 minutes, and
puts the panel's own waveform back [docs, read in epd.c]. A person photographs the glass; the values that look
right go into `LUT_4GRAY` in `epd.c` by hand. Read the board's lines: "grey waveform byte <i>: <old> -> <new>"
for each poke, "grey test: ...; <ms> ms" for the bands.

Found: as it came, the 4.2"'s greys were 220 and 50 of 255 and the light one could not be seen on the glass. A
frame more or less of white each (EPDLUT 99 49 and 141 47), the bands printed, and the photo showed four tones;
those values are the waveform the firmware ships, 2026-11-22 [measured, the note above LUT_4GRAY in epd.c].

### soak-board.ps1 and log-summary.sh: the soak before every beta

```powershell
& .\soak-board.ps1 -Port COM5 -LogDir soak -Cycle 15 -Idle 15
```
```bash
bash log-summary.sh soak/soak-COM5-cycle.log
bash log-summary.sh soak/soak-COM5-idle.log --tail 40
```

The soak is `cycle-looks.ps1` for `-Cycle` minutes, then the board left in one look for `-Idle` minutes with
`log-board.ps1`, every bench board at once in its own window, every setting put back at the end. The reader
prints counts only: restarts and crashes by kind, the frame rate's range and mean, the heap first, lowest and
last, maps made, whole and not whole (as got/of), the least heap after reading tiles, tile reads and maps with no
memory, the weather's answers and misses, the planes' answers by server and HTTP code, WiFi lost by reason and
up, the second core started and stopped, the rig's commands. `--tail N` adds the last N lines of harmless kinds,
filtered again. Merged from the soak reader, a map counter, a fetch counter and a filtered tail.

Read: 0 restarts after the first line (the start the rig made), every map whole when the board is left alone,
the lowest heap not falling from one soak to the next.

Found:
- 0.16.2: 0 restarts on three boards; idle, every map whole [measured].
- In a soak of 2026-11-21, while the looks cycled: the 2.8" made 113 maps of 113 whole; the 3.5" 102 of 111, with
  one tile read short of memory; the round C3 only 17 of 67, with 19 "no memory to read a tile in". Left alone,
  every map came whole on all three [measured]. The round board's WiFi on that bench is weak [measured], and its
  tile reader ran short of memory while the looks changed [measured]; how much of the 50 each cause explains was
  not split [reasoned].

### flash-and-check.ps1: after this flash, does it start, connect, and keep its settings?

```powershell
& .\cfg-snapshot.ps1 -Port COM5 -Out cfg-COM5-before.json
& .\flash-and-check.ps1 -Dir public\fw\st7796 -Port COM5 -Chip esp32 -Mac aa:bb:cc:dd:ee:ff -Before cfg-COM5-before.json
```

Flashes a site folder with `flash-manifest.ps1` from esp32-flash-and-settings (chip and MAC checked, the
manifest's parts at its offsets, no erase), stops unless every part verified, then runs `net-diag.ps1` on the
start and `cfg-snapshot.ps1 -Before`. The pass: every part verified, WiFi up, requests answered, "differ: none".
Run it for the beta, the live stable over it, and the beta again: that round trip is the 44 fields of 2026-11-19
[measured].

### func-checks.ps1: the functional pass

```powershell
& .\func-checks.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff -Dim -Boots 10 *> func-COM5.txt
```

Runs weather-units, weather-page, screen-light (with `-Dim`), weather-pages with the panel and without,
look-timing on all four looks, looks-stress for two minutes, touch-boot and the LED test, each after a `===`
line. Read it for OK where a value was valid, unchanged values where one was not, WXPAGE once per change, no
RESET OR CRASH outside the restarts the rigs made, "the look changed after 0", "put back as found" or "every
setting as it was found". It was 0.16.1's functional pass on three boards [measured].

### release-build.cmd: does it build from nothing as CI builds it?

```bat
release-build.cmd st7796 2 esp32
```

From the firmware repository's root: a fresh `firmware\build_rel_<disp>` and a fresh sdkconfig from the
defaults, no ccache, the CI's own `idf.py` line. It refuses a folder that is there already. Then flash it with
`flash-one.ps1`, or assemble the site from it for a release rehearsal. It was the build step of the 0.16.1
rehearsal on the bench's three boards [measured]. What a fresh build catches that a reused folder hides: a
default that only a fresh sdkconfig shows, and with the rehearsal's export from git, a file that is on disk but
not in git (testing.md) [reasoned].

## Left out, and why

- The flash, backup and build helpers of the scratch folders are already in esp32-flash-and-settings as
  `flash-one.ps1`, `flash-manifest.ps1`, `backup-board.ps1` and `build-some.ps1`.
- The release rehearsal's site assembler is `files/flight-radar/tools/assemble.sh` there; the scratch copy
  needed a private mirror server.
- A PC preview launcher and a one-line `idf.py size` wrapper: the repository's `tools/preview/BUILD.BAT` and
  `idf.py -B <build folder> size` do the same.

## Not known

- Boards on the chip's own USB (S3, C5, P4 without a CH340) were not run with these rigs; opening and the RTS
  restart may behave otherwise there [reasoned].
- PowerShell 7 on Linux or macOS has System.IO.Ports too, but these rigs were run on Windows only [reasoned].
- The cleaned files were not run on a board after cleaning (see the top).
