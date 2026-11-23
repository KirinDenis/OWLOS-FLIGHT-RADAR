# Crashes, hangs and the log

How sure each fact is: the tags of SKILL.md. What to do when a board reboots, stands still, or a stranger says
"it hung".

## First, what kind of "it hung"

1. **Look at the board.** "It hung" was the cable falling out, twice [measured].
2. **Is it still running?** Ask it. `OWLOS GET` gives `uptime`, `ran` and `reset`. If the console answers, the
   CPU is fine and the picture is the problem.
3. **Did it restart?** `uptime` small and `reset` not "power on": read `reset` and `ran`.
4. **Opening the port resets a CH340 board** even with DTR and RTS off [measured]. So the run you want is the one
   **before** this one: that is `ran` and `reset`, not `uptime`.
5. **Picture frozen, console alive:** the screen lost its mode, or the strip path is stuck. See the stories.
6. **Nothing answers:** a hang with the watchdog off, a stuck interrupt, or power. With the settings below
   it should not stay that way more than 10 s.
7. **The port says "Access is denied"**: something else holds it, most often a browser tab with the flasher site
   connected over Web Serial. Close that tab; do not kill a process you did not start.

For a problem you cannot make happen on demand, keep **one long background logger** on the port and let it run.
Short peeks reset the board each time and destroy the evidence.

## The settings

```text
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y     # print, reboot. a stuck board helps nobody
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y       # no flash partition needed
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
```

The dump goes out on the UART at the moment of the crash, so it exists only if something is listening. That is
fine on the desk and useless on a stranger's wall, where the reset record below takes over.

**A core dump on flash** keeps it for afterwards. The 7" and 10.1" builds of a contributor do this: a `coredump`
partition of 0x10000 (64 KB) and `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, read later with `idf.py coredump-info`.
Those boards often run with nobody on the serial port, and the watchdog crashes of their moving layer were found
this way [docs, FORK-NOTES.md]. It costs 64 KB of flash and a board with 8 MB or more has it to spare; on a 4 MB
board it would come out of the tiles partition [reasoned].

## A core dump, step by step

The board prints base64 between two marker lines, then reboots:

```text
================= CORE DUMP START =================
<base64 lines>
================= CORE DUMP END ===================
```

A dump is a copy of RAM: every task's stack, and whatever it holds. The WiFi password and an API key can be in it.
Treat a dump from a person's board like the password itself, and do not ask for one in public.

`idf.py monitor` decodes it by itself if it is the one on the port. From a saved log, by hand, as the C3's hang was
found [measured]:

```sh
# 1. keep the lines between the markers, without the markers, in dump.b64
# 2. turn it into an ELF core file (IDF's own tool, in the IDF python env)
python -m esp_coredump --chip esp32c3 info_corefile -t b64 -c dump.b64 -s core.elf app.elf
# 3. every task's stack
riscv32-esp-elf-gdb -batch -ex "add-symbol-file <tools>/esp-rom-elfs/<ver>/esp32c3_rev3_rom.elf" \
                    -ex "thread apply all bt" app.elf core.elf
```

The note on record has the tool names, `add-symbol-file` and `thread apply all bt`. The exact gdb line above was
put together from them, check it on first use. `app.elf` is the build's `build_<disp>/<project>.elf`. The ROM ELF
matters on the C3, where a lot of the stack passes through ROM. For an ESP32 use `--chip esp32`,
`xtensa-esp32-elf-gdb`, and the ESP32 ROM ELF. (The `python -m` here is IDF's own tool inside IDF's environment,
not a script of ours.)

What to look for: on a watchdog, the task named in `Task watchdog got triggered` and what each **other** task
was doing. A starved IDLE points to whoever held the core. Two dumps from the same bug, compared, are worth more
than one. The strip deadlock showed as the screen task sitting in `spi_device_acquire_bus`.

## A backtrace from a board flashed off the site

The ELF has to be of that very build. A local one does not match: the local IDF is the v5.5 tag, the CI's image is
`release-v5.5`, and the IDF patch level moves the addresses [measured in the repo's comment]. So CI keeps it:

```yaml
# not for the site: the ELF turns a crash backtrace from a board flashed off the
# site into file and line. a locally built ELF doesn't match
- uses: actions/upload-artifact@v4
  with:
    name: elf-${{ matrix.disp }}
    path: |
      firmware/build_${{ matrix.disp }}/owlos-flight-radar.elf
      firmware/build_${{ matrix.disp }}/sdkconfig
    retention-days: 90
```

Then:

```sh
# from "Backtrace: 0x400e14ed:0x3ffb5030 0x400d0802:0x3ffb5050" take the first of each pair
xtensa-esp32-elf-addr2line -pfiaC -e owlos-flight-radar.elf 0x400e14ed 0x400d0802
riscv32-esp-elf-addr2line  -pfiaC -e owlos-flight-radar.elf <pc> ...        # C3, C5
```

- Check it is the right ELF: the boot log and the panic both print `ELF file SHA256:` [docs, `esp_app_desc.c` and
  `panic.c`]. Compare with `sha256sum` of the ELF: if the start differs, the file and line you get are fiction.
- On RISC-V chips a panic gives registers and a stack dump, and the backtrace is made by `idf.py monitor` with
  gdb [docs]. So on a C3 or C5 ask for the whole text, or the core dump.
- Artifacts last 90 days. The site keeps each version's firmware for good, the ELF only that long. For a report
  on an older version, rebuilding the ELF will not match. Keep the ELFs of each release if this matters.
- A tester's report with no ELF in reach (an open bug on a board nobody here has) needs a local build of that very
  commit and still may not match. Ask for the whole panic text and the lines before it.

## The reset record

Every firmware should know how it died. In Flight Radar:

```c
/* how the last run ended, for a board found standing still: did it start again (and why), or
 * did the picture stop with the board running? the uptime goes to NVS once a minute (store.h):
 * RTC memory lost it when the USB line's reset pulled EN, the site's port may do that. the boot
 * log and GET say it */
static uint32_t           s_ran_s;     // how long the run before this one was, s, to a minute
static uint32_t           s_ran_kept;  // the uptime last saved, s
static const char        *s_reset;     // why it ended

static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_SW:       return "restart";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default:               return "other";
    }
}
/* in app_main */
s_reset = reset_name(esp_reset_reason());
s_ran_s = store_load_ran();
store_save_ran(0);
ESP_LOGI(TAG, "started after: %s; the run before lasted %u s%s", s_reset, (unsigned)s_ran_s,
         s_ran_s ? "" : " (under a minute, or not known)");
/* in the 10 s block of the frame loop */
if (now / 1000u - s_ran_kept >= 60u) {     // a reset from here on knows how long this run was
    s_ran_kept = now / 1000u;
    store_save_ran(s_ran_kept);
}
```

The README tells a stranger the same: press READ WHAT IT HAS on the site, and "if opening the port restarted it,
the run before is the one that stood still".

A reset reason of `power on` with a short `ran` on a board nobody touched is power trouble, not software. A long
`ran` and `task watchdog` is a slow leak or a rare state. A crash every few minutes wants a dump.

## RTC memory: counters and guards

`RTC_NOINIT_ATTR` keeps a variable through a software, watchdog or brown-out restart, but not through a power cut,
and not reliably through an EN reset from the USB line [measured]. So: fine for "this boot went wrong, remember
it for the next one", not for a record that must survive being plugged into a computer. Always guard with a magic.

**Brown-out counter** (Air Quality `main.c`), kept because the MQ heaters may starve the supply:

```c
#define PWR_MAGIC 0x50575231u
static RTC_NOINIT_ATTR uint32_t r_pmagic, r_brown, r_rest;

static void count_restarts(void)
{
    esp_reset_reason_t why = esp_reset_reason();
    if (r_pmagic != PWR_MAGIC || why == ESP_RST_POWERON) {
        r_pmagic = PWR_MAGIC;
        r_brown = r_rest = 0;
    } else {
        r_rest++;
        if (why == ESP_RST_BROWNOUT) r_brown++;
    }
    s_brownouts = r_brown;
    s_restarts = r_rest;
    ESP_LOGI(TAG, "reset reason %d, restarts since power on %u, of them brown-outs %u", (int)why, (unsigned)r_rest,
             (unsigned)r_brown);
}
```

`CONFIG_ESP_BROWNOUT_DET=y`: a brown-out resets cleanly, and with a relay on the board the relay starts OFF.

**The guard that breaks a restart loop** (Air Quality `sensor_hw.c`):

```c
/* the guard. a chip that holds the bus makes ESP-IDF's I2C driver wait forever, the task watchdog
 * restarts the board, and the same chip would hang it again: a restart loop, the disease of the
 * old OWLOS. so the sensor being talked to is written into RTC memory first, which lives through
 * a restart but not a power cut. after a watchdog or panic restart that sensor is left alone till
 * the power goes off, and the screen says why */
#define GUARD_MAGIC 0x41514755u
static RTC_NOINIT_ATTR uint32_t r_magic, r_in, r_bad;

static void guard(int i) { r_in = (uint32_t)(i + 1); }
static void unguard(void) { r_in = 0; }

static void guard_boot(sens_t *s)
{
    esp_reset_reason_t why = esp_reset_reason();
    int crash = why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_WDT || why == ESP_RST_PANIC;
    if (r_magic != GUARD_MAGIC || why == ESP_RST_POWERON) {
        r_magic = GUARD_MAGIC;
        r_bad = 0;
    } else if (crash && r_in >= 1 && r_in <= SENSORS) {
        r_bad |= 1u << (r_in - 1);
    }
    r_in = 0;
    /* each bad one: state SS_ERROR, "HUNG I2C, OFF TILL POWER" on the screen */
}
```

Wrap each risky call: `guard(SEN_CCS811); r = ccs_read(s); unguard();`. Any feature that can take the whole board
down can be fenced this way: a guard word before, cleared after, checked at boot, and a sentence on the screen.

The same disease in another form: FusionEdge's "smart start" played the last station again at boot, and a station
whose decoder could not get memory aborted the board each time. Anything a firmware replays at boot needs the
same fence, or a failure that is a state instead of an abort. See [memory.md](memory.md).

## The stories

**The strip that deadlocked.** Light views draw faster than a 40 MHz bus. Sending a strip while the one before was
in flight sometimes hung for ever. A core dump showed the screen task in `spi_device_acquire_bus`. Now it waits for
the done callback first, at most 200 ms, then counts the strip `lost` and goes on:

```c
if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;   // max 200 ms, then count it lost and go on
```

A limit on every wait turns a deadlock into a number in the log. In the 2 h run `lost` stayed 0 [measured].

**Touch on the screen's bus.** The 3.5" XPT2046 shares the LCD's SPI. It waits for the strip in flight before asking
for the bus (`disp_wait_idle`), because waiting in the driver's bus lock can hang. The pen interrupt then keeps it
off the bus when nobody touches: every read had been waiting for a strip and holding the next one back, a third
of the frame [measured]. A single noisy read of it at power-up later counted as a tap and moved the 3.5" one look
on after some restarts: now a press needs two reads in a row, and nothing is read for 1.5 s after start. 10 of 10
restarts kept the look after the fix [measured]. Detail in esp32-lcd-touch.

**A 3.5" found standing still after about a day.** The cause is not known. The panel may have lost its mode to a
glitch on the line. The fix is blind: every 5 s between frames the LCD is told again how it runs (sleep out, pixel
format, inversion, display on, MADCTL), which does nothing to a healthy panel:

```c
/* every few seconds, between frames, the screen is told again how it runs: awake, on, its pixel
 * format, inversion, its way round. it has all of that already and nothing shows; a screen
 * that lost them to a glitch on the line comes back by itself instead of standing still */
```

The run record is for the same report: a board found standing still, and the question whether it had restarted.

**The 3.5" that restarted at a big zoom.** A kept TLS connection was reused after a map tile came broken (TLS was
short of memory) and the next request crashed inside mbedTLS: `LoadProhibited`, a `memcpy` to NULL, 6 of 6 range
changes. Any tile that is not whole now drops the connection [measured]. See [network.md](network.md).

**The strip buffers that could not be made.** On the 3.5" a failed allocation of the paired strips left the
sender with a row count it had no buffer for. Now `disp_rows` sets 0 rows when it fails completely and the paired
send tries the allocation again [built, from a whole-code review].

**The e-paper that crashed on RADAR -> MAP.** A map 236 texels wide wrote past each band; now the width is rounded
up to 8 and `map_alloc` refuses anything else [measured]. See [memory.md](memory.md).

**The console that restarted the CYD.** One more small local in the function that builds the CFG line overflowed
the console task's 4 KB stack: 2 of 5 GETs restarted the board. Its buffers are static now [measured].

**The S3 that reset after a scan.** A tester's CrowPanel Advance 2.8" on 0.4: `looked round: 13 networks`, then
`Interrupt wdt timeout on CPU0`, again after a second flash. No S3 board here then, no ELF in CI then, cause not
found. The suspect: the event task's 2304-byte stack, with the scan list on it and the log going to two consoles on
an S3. The S3 LCD boards got a 4096-byte event task stack later for that two-port log [reasoned; never
confirmed on that board]. The workaround offered: WiFi from the site, no scan.

**The C3 every 29 seconds, the solver next to a router, the I2C driver, the watchdog on the 7" boards**:
[tasks-and-watchdog.md](tasks-and-watchdog.md).

**A SET at READY.** `assert xTaskGenericNotify (xTaskToNotify)`, or the SET lost, or on 0.2 a `SW_CPU_RESET`:
READY was printed before the fetch task existed. Now console and READY come last in `app_main`.

**Junk on the first line.** The console takes a command only when `OWLOS` starts the line, and bytes from the reset
at connect glue onto it. GET at READY worked 2 of 6 times, with a `\n` sent first 6 of 6 [measured]. The site
now sends `\n` before each line, and since 0.14 the console also finds `OWLOS ` anywhere in a line.

**The Tab5 that the UART driver stopped.** The board has no usable UART0; the UART driver waiting for it held core
0 till the watchdog bit. That build's console is the chip's USB alone [docs, the comment in the sdkconfig file].

**80 MHz flash**: [project-skeleton.md](project-skeleton.md#flash-speed).

**The e-paper S3 prints nothing after flashing** until reset: toggle RTS on an open port to see the boot [measured].

## The log, line by line

Flight Radar's frame loop, every 10 s (WiFi Radar's every 5 s, with the network list every 10):

```text
flight: <fps> fps, <ms> ms a frame, view <v>, <n> planes, heap <bytes>
flight: RAM <free> free, <block> in one piece, <low> at the least                  (PSRAM builds only)
flight: ms a frame: background <a>, disc <b>, compass <c>, planes <d>, panel <e>, the rest <f>, sending <g>, touch <h>
flight: the second core draws: waited for it <w> ms a frame, its stack <s> bytes free[, <r> rows of <n> this core's]
```

What each field is and what to do with it:

- fps: frames in the last 10 s.
- ms a frame: `ui_frame` plus all strips, without the 1 ms delay. Budget: 1000 / fps minus this is what was lost to
  others.
- view: the page. A number means little without it: pages differ by 2x. The look is not in the line: ask GET for
  `look` before you compare two speeds, and know that the "planes" layer on the weather look is its frame work.
- heap: free now, `MALLOC_CAP_8BIT`. A falling line over hours is a leak. On a PSRAM board it counts PSRAM: read
  the RAM line instead.
- background, disc, ...: sum of `ui_lap` per layer over strips. The biggest one is the next job.
- sending: packing to 565, plus waiting for the strip before. If it nears the wire time the bus is the limit.
- touch: reading the touch chip. Large on a shared bus.
- waited for it: core 1 idle, waiting for core 0. High means the strips are uneven. With halves, the cut row is
  printed too.
- its stack: `uxTaskGetStackHighWaterMark`. Below ~512 bytes, give it more.

The wire time is `W * H * 16 / SPI_HZ`: 480x320 at 40 MHz is 61 ms, 320x240 at 40 MHz is 31 ms [computed, and 61 ms
matches the 3.5"]. The map look at 81 ms is SPI-bound, the radar at 98 ms was the disc pass, about 1 us a pixel
[measured]. Layers on the second core are not timed: one clock, one core. So with the helper on, the layers add up
to less than the frame.

The layer clock in `core/`:

```c
uint32_t (*ui_clock_us)(void);       /* NULL: nothing is timed, the PC preview runs without */
void ui_lap(ui_t *u, const canvas_t *cv, int layer)
{
    if (!ui_clock_us || cv->helper) return;
    uint32_t now = ui_clock_us();
    if (layer >= 0 && layer < (int)(sizeof u->prof / sizeof u->prof[0])) u->prof[layer] += now - s_lap;
    s_lap = now;
}
```

Each fetch, from the fetch task:

```text
flight: ADSB.LOL: HTTP 200, <bytes> bytes in <ms> ms, <n> planes in it, <kept> kept, <list> on the list, heap <h>, at least <low> since power on
flight: map: zoom <z>, <r> of <n> tiles, made in <ms> ms, heap <h>, at least <a> after the tiles came, <b> after reading them
tiles: <n> tiles kept, <k> KB of <k> KB
```

- **`at least`** is `heap_caps_get_minimum_free_size`. It only falls. Under about 10 KB on a classic ESP32 stop
  adding things [measured as the level Flight Radar lives at; the 3.5" has gone far below it on its maps].
- **HTTP 0** with `-1 bytes`: no connection or a failed handshake. Look a few lines up for `esp-tls` or `esp-aes`.
- **`kept` of -2** means no good answer came, **-1** an answer with no list in it.
- `map: no memory to read a tile in, heap <h>, largest <l>`: the inflater's window could not find its pieces. Read
  `largest`: a big total with a small largest block is fragmentation.
- `WiFi lost (reason N), trying again`: every 2 s while it lasts. 201 no AP found, 202 auth fail, 15 the 4-way
  handshake timed out, 204 handshake timeout [docs]. The screen gets the same as NOT FOUND or PASSWORD.

WiFi Radar adds `dropped` (queue full in the promiscuous callback) and `lost` (strips with no done callback), and
`map solve %d ms` when a solve passes a second. Air Quality adds the rows sent a frame and core 1's share: drawing,
sending, waiting for core 0. Air Quality's page can show `fps, ms a frame, heap` on the glass itself, so a person
with no cable can read it.

The 7" and 10.1" builds add a `pacing:` line (the longest gap between ui passes and what the fetch task was doing,
lock waits, requests on a kept connection) and, once a minute, every internal RAM region and every task's unused
stack, with `P` for a stack in PSRAM [docs, FORK-NOTES.md]. Worth copying on any board where the picture stutters.

**The log itself can cost frames.** At 115200 baud the 10 s statistics, about 3 KB, held the screen task 0.3-0.5 s
every 10 s on the 7": a visible stutter. Those builds format each log line straight into a 64 KB ring buffer in
PSRAM, and a task at the lowest priority writes it out; a full buffer drops lines and counts them rather than wait.
`printf` (the console's `OWLOS` lines) and a crash's panic text still go straight out; lines still in the buffer
when the board resets are lost [docs, `logbuf.c`]. After it the longest gap between ui passes was 38 ms with an
empty sky. On a board without PSRAM keep the 10 s block short instead.

**Reading a slow frame:**

1. Sum the layers. If they fall short of `ms a frame`, the rest is pack and wait: `sending`.
2. A layer grew with time? Something fills up. Trails took 80 s after boot to reach 62 ms.
3. Does a PC profile agree? Only if the PC has reached the same state.
4. Change one thing, take the next 10 s line. Pixel-diff the shots to prove the picture stayed the same.

Secrets stay out of all of this: the URL line prints no key, and `settings from the site` prints the network's
name, the rounded home, and "FR24 key stored", never the key. But the log is not clean: "WiFi up" names the network
and the address, the map and Nominatim lines can name the home's town. When you show a log to anybody, mask the
network name, the home, the town and the IP. A rig that reads logs should print booleans and counts, not raw lines:
a filter once let a WiFi name and a home town through to a screen that was shared [measured].
