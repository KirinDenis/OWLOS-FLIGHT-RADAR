# Tasks, locks, the watchdog, the loops

How sure each fact is: the tags of SKILL.md. Code is real, from the file named above it, trimmed where it says so.

## The tasks

IDF's own tasks, the same in all firmwares that use WiFi [docs]:

- wifi: priority 23, core 0. The driver; a promiscuous callback runs here.
- esp_timer: 22, core 0.
- sys_evt: 20, core 0, stack `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` (2304 by default, 4096 on the S3 LCD
  boards and the C5 in Flight Radar, because the log goes to two ports there and went past the stack's end).
  `on_event` in net.c runs here.
- tiT (lwIP): 18, any core, stack 3072.
- main: 1, core 0, stack 4096 (8192 on the P4). `app_main`, returns when all is started.
- FreeRTOS timers: 1.
- IDLE0, IDLE1: 0, stack 1536. The watchdog checks these.

**Flight Radar** (`firmware/main/main.c` and the driver files):

- ui: core 1 on a two-core chip, core 0 on one core. Priority 5, stack 8192. `esp_task_wdt_add`, `disp_init`
  runs here. Touch, BOOT, keys, copy the sky, draw and send the strips, the 10 s log.
- fetch: core 0. Priority 4, on one core 5. Stack 12288. Every request, the TLS handshake, map tiles and the
  map, geocoding, the weather, the version check, one at a time.
- strips: core 0, priority 4, stack 7168. The second core's drawing (below). Made and deleted at run time on
  the boards that pair strips; made once and kept on the 2.8".
- console: any core, priority 3, stack 4096. `uart_read_bytes` with a 1 s timeout.
- console usb: any core, priority 3, stack 4096. Chips with USB Serial JTAG, polls every 10 ms.
- keys: priority 4, stack 2048. The e-paper boards only: reads the keys every 10 ms.
- On the 7" and 10.1" boards of a contributor's moving layer: a log writer (priority 1), a background drawer
  (priority 1, stack in PSRAM), a layer builder (priority 3), a planner (priority 6), and the fetch and console
  stacks in PSRAM. See [The moving layer boards](#the-moving-layer-boards).

```c
xTaskCreatePinnedToCore(fetch_task, "fetch", 12288, NULL, portNUM_PROCESSORS > 1 ? 4 : 5, &s_fetch, 0);
xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, portNUM_PROCESSORS > 1 ? 1 : 0);
```

**WiFi Radar**: radio 0/6/4096 (channel hopping, drains the queue), mapper 0/2/6144 (once a second), ui 1/5/8192.
On the C3 all on core 0 with mapper and ui both at priority 1, radio at 6. (core/priority/stack.)

**Thermo Relay**: control 0/6/6144 (once a second), ui 1/5/8192.

**Air Quality UI**: sensors 0/6/6144 (once a second), ui 1/5/8192, painter 0/4/6144 (draws the odd strips).

Why it is shaped so:

- WiFi is pinned to core 0 [docs], so the data task sits there too and the screen has core 1 alone.
- `disp_init()` is called **inside `ui_task`**, not in `app_main`: an interrupt is assigned on the core that
  allocates it [docs]. The source says it: "init here so the SPI interrupt is on this core, away from wifi".
- The control and sensor tasks outrank the screen (6 against 5) but sit on the other core and run a few ms a
  second. On the thermostat the relay must never wait for a frame.
- The screen outranks the data on its own core, and nothing of ours outranks WiFi.
- A 12 KB fetch stack is what was chosen for TLS and has not failed. It is not a measured minimum. To find a real
  number, call `uxTaskGetStackHighWaterMark` in the 10 s log; the 7" builds print every task's unused stack once
  a minute.

## app_main, in this order

```c
void app_main(void)
{
#ifdef PIN_AMP_EN
    // a speaker's amplifier on the board: off from the start, its enable left floating may hum
    gpio_set_level(PIN_AMP_EN, 0);
    gpio_config_t amp = { .pin_bit_mask = 1ULL << PIN_AMP_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&amp);
#endif
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_reset = reset_name(esp_reset_reason());          // what ended the run before
    s_ran_s = store_load_ran();
    store_save_ran(0);
    ESP_LOGI(TAG, "started after: %s; the run before lasted %u s%s", s_reset, (unsigned)s_ran_s,
             s_ran_s ? "" : " (under a minute, or not known)");

    s_lock = xSemaphoreCreateMutex();
    if (!store_load_cfg(&s_cfg)) fr_cfg_defaults(&s_cfg);
    store_load_secrets(&s_sec);
    store_load_nets(&s_sec, &s_nets);                   // the five networks it remembers
    /* ... the watch list, the LED, the radar's options, backlight, night, alerts, units, names:
     * each from its own NVS key ... */
    s_demo_on = !s_sec.ssid[0];                         // no WiFi set: the demo from the first frame
    sky_init(&s_sky, s_cfg.lat, s_cfg.lon);
    demo_init(&s_demo, range_km(&s_cfg), esp_random());
    tiles_init();
    map_mem = iram_first;
    map_scratch = s_fetch_buf.map;                     // the fetch task's own buffer while it reads a tile
    net_start(&s_nets);                                // up even without settings: a SET then just connects
    xTaskCreatePinnedToCore(fetch_task, ...);
    xTaskCreatePinnedToCore(ui_task, ...);
    /* the console last, and READY with it: the site sends its SET the moment it reads READY. said
     * before, the SET was lost (no console yet) or notified a fetch task not made yet (an assert) */
    console_start(on_line);
    say_ready();                                       // OWLOS READY flight-radar <version> <build> <mac>
}
```

The rule: a thing that can be called from outside starts after everything it can call. Moving READY last took
it from about 0.5 s to about 1.0 s after reset [measured].

A board with something to switch does that first of all. Thermo Relay reads its settings, because the relay's
polarity is one, and the next line is `relay_init(s_cfg.relay_low); // OFF before anything else happens`. The
relay stays off till the question "what is on the relay" has its answer. A speaker amplifier on the board is held
off before NVS is even opened, for the same reason.

READY carries the build name and the chip's MAC, so a page with several boards on several cables knows which is
which: a port's number can change between plugs, a MAC does not. The P4 prints its base MAC, because its WiFi MAC
belongs to the ESP32-C6 beside it.

## Sharing data

One mutex, `s_lock`, for the sky, the settings, the watch list and the LED state.

```c
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
```

While a lock problem is hunted, the wait can name the holder (WiFi Radar, `main.c`):

```c
// take the shared lock, but if it takes more than 2 s print who holds it (then keep waiting)
static void lock_app(const char *who)
{
    while (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        TaskHandle_t h = xSemaphoreGetMutexHolder(s_lock);
        ESP_LOGW(TAG, "%s waits for the lock, held by %s", who, h ? pcTaskGetName(h) : "?");
    }
}
```

The 7" builds keep the same idea in the 10 s log: how many times the ui waited for the lock, the longest wait,
who held it and what the fetch task was doing. Measured there: the ui waited for the lock 0 times [docs, the
contributor's FORK-NOTES.md].

**Never hold it across I/O.** The patterns, each from real code:

Copy, let go, then do the slow thing:

```c
fr_cfg_t c;
lock();
c = s_cfg;
unlock();
store_save_cfg(&c);                       // flash write, outside

lock_app("mapper");                       // WiFi Radar
int n = store_collect(&s_app, s_nets);    // small copy under the lock, the slow flash write without it
xSemaphoreGive(s_lock);
int ok = fstore_save(&s_map, s_nets, n);
```

A download in three steps: begin under the lock, stream without it, merge under it.

```c
lock();
sky_read_begin(&rd, &s_sky, fr24, now_ms());   // only the home is taken from the sky here
unlock();
int len = net_get(url, fr24 ? key : NULL, to_reader, &rd, &status);
lock();
if (len > 0 && status == 200) n = sky_read_end(&rd, &s_sky);
unlock();
```

Two copies, the screen reads one while the next is filled (the weather in Flight Radar: `static weather_t s_wx[2]`
and `s_wx_cur`, the index swapped under the lock).

Work out on a copy, swap under the lock (Thermo's 24 h pretend day, Air Quality's demo):

```c
aq_t *t = heap_caps_malloc(sizeof *t, MALLOC_CAP_8BIT);    // worked out off the lock, swapped in under it
/* ... 26 hours of made-up air, with a yield every 4096 steps ... */
lock();
s_aq = *t;
unlock();
free(t);
```

The screen's snapshot, once a frame:

```c
lock();
memcpy(&s_ui.snap, &s_sky, sizeof s_sky);      // 7.7 KB
/* ... the small net state ... */
unlock();
```

After that the whole frame is drawn from `s_ui.snap`, and no drawing code knows a lock exists. That is also what
lets the PC preview and the browser twin run the same code.

**Requests between tasks** are flags plus a wake-up, not queues:

```c
static volatile int s_cfg_new;        // the site sent settings: the screen takes s_cfg
xTaskNotifyGive(s_fetch);             // a new setting cuts the wait short
ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(every));   // the fetch task's only sleep
```

A flag for a thing that has a value, a string for one with text (`s_home_ask`, under the lock). A notification
sent before the task exists is an assert, see the app_main rule.

**Settings have two writers** (screen and site) and one rule: the site's SET writes `s_cfg` under the lock
and sets `s_cfg_new`, the screen task takes it as the master. A change on the screen copies its own fields
back but takes `lat`, `lon`, `place`, `country` from `s_cfg`, because the fetch task may have found them.

**Critical sections** only for a few bytes touched from the event task and the screen: the scan list in net.c,
`portMUX_TYPE s_scan_mux`, around a `memcpy` of 560 bytes. A deadline word written by one task and cleared by
another uses a compare-and-swap, so a renewal in between is not lost (`page_there` below).

## The queue

WiFi Radar, the one queue for data in these firmwares. The promiscuous callback runs inside the WiFi task at
priority 23, so it may not block, not take the mutex, not allocate:

```c
s_q = xQueueCreate(128, sizeof(sample_t));          // 44 bytes each, 5.6 KB [computed]

static void rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)   // NOTE: runs inside the wifi task, keep it short
{
    /* ... parse the header into a sample_t on the stack ... */
    if (xQueueSend(s_q, &s, 0) != pdTRUE) s_dropped++;
}
```

The consumer takes up to 64 samples per hold of the lock, and waits at most 20 ms so channel hopping stays on
time. `dropped` is in the log: a count that grows means the consumer is too slow, not that a frame was lost
silently. Whole file: `code/radio.c`. The e-paper keys use a queue of 8 the same way: sent with no wait, a ninth
key while eight wait is dropped.

## The frame loop and the idle task

```c
for (;;) {
    /* input, commands, snapshot, draw and send the strips */
    esp_task_wdt_reset();
    vTaskDelay(1);                                // let the idle task run, the watchdog checks it
}
```

`vTaskDelay(1)` is one tick. With `CONFIG_FREERTOS_HZ=1000` that is 1 ms a frame; at the default 100 Hz it would
be 10 ms a frame, a tenth of the budget at 10 fps. The delay is what lets IDLE1 run at all, because `ui` is
ready all the time otherwise. **A task that never blocks starves everything below it on its core.**

## The task watchdog

Two different things are watched:

1. **Tasks you add** with `esp_task_wdt_add(NULL)`. They must call `esp_task_wdt_reset()` within 10 s.
2. **The idle tasks**, by `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPUx`. If higher tasks keep a core busy for 10 s,
   idle never runs and the watchdog fires, whichever task is to blame. The panic names IDLE, the core dump
   shows who ran.

Where to call `esp_task_wdt_add`: after the slow start-up. In `ui_task` after `disp_init`, the strip buffers
and the fonts; in the sensor task after `sensors_init`. In a task that calls it optionally, as a shared driver
does, ask first, or `esp_task_wdt_reset` logs an error for an unsubscribed task:

```c
// epd.c, waiting for the panel's BUSY line: a refresh takes seconds, its not a hang
vTaskDelay(pdMS_TO_TICKS(10));
if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
```

The comment in `ui_task` says what it is for: "watchdog on this task too: stuck 10 s = reboot + core dump that
shows where".

Not added: the fetch task, because one blocking request may last 9 s and DNS and a handshake may add to it. The
price: a fetch task stuck for ever is not detected. Open.

### WiFi Radar: the solver and core 0

- `mapper_task`, core 0, priority 2. Once a second `map_solve(&s_map, 4)`, four rounds. Every 20 s six fresh
  starts compete, about 180 rounds in a row: 1.2 to 1.7 s of maths at 160 snapshots and 16 networks [measured].
- Next to a busy router the WiFi task, priority 23 on the same core, took most of the time. The same maths
  stretched to 10 s [measured].
- For that long nothing below priority 2 ran. IDLE0 starved. The watchdog panicked and the board restarted.
- The walk map comes back from flash after a restart, so the job came back too. That is how a one-off pause
  became a board that rebooted "after a minute or two (full map)", again and again. (The return of the
  job is a reading of the code [reasoned]. The repeated reboot is measured.)
- The bench said 1.2 s. **The number that matters is the same work beside a busy radio.**

Fix: `map_yield`, set to `vTaskDelay(1)`, after every round. `core/` may not know FreeRTOS, so it is a hook:

```c
/* core/map.h */
extern void (*map_yield)(void);                    /* NULL = no-op */
/* core/map.c, in the solver's loop */
for (int i = 0; i < iters; i++) {
    path_step(m, mu);
    for (int k = 0; k < m->n; k++) ap_step(m, k, mu);
    recenter(m);
    if (map_yield) map_yield();                    /* let the rest of the core breathe */
}
/* firmware/main/main.c */
static void yield_1ms(void) { vTaskDelay(1); }
/* the solver's restarts are seconds of maths. without a pause the idle task on core 0
 * starved and the task watchdog rebooted the board after a minute or two (full map) */
map_yield = yield_1ms;
```

About 180 rounds at 1 ms each is 0.18 s added to a restart [computed]: nothing, against a reboot. And the log
line that keeps watch on it: `if (solve_ms > 1000) ESP_LOGI(TAG, "map solve %d ms ...")`.

On the C3 (one core, no FPU) the restarts took 6-10 s even with 4 networks, and with the mapper above the screen
the picture stood still all that time. So fewer and rarer, and equal priority:

```c
if (portNUM_PROCESSORS > 1) {
    xTaskCreatePinnedToCore(mapper_task, "mapper", 6144, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, 1);
} else {
    /* one core and no FPU (C3): the restarts took 6-10 s there, with the mapper above
     * the screen the picture stood still all that time. now the two share the core
     * (same priority, time sliced), and the restarts are fewer and once a minute */
    map_restarts = 3;
    map_restart_every = 60;
    xTaskCreatePinnedToCore(mapper_task, "mapper", 6144, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 1, NULL, 0);
}
```

### Flight Radar: the C3 and the fetch task

- One core. ui at 5, fetch at 4 (the two-core setting). The screen's only pause was `vTaskDelay(1)`.
- So the fetch task ran under 1 ms per frame. A TLS handshake and a map took over ten seconds of wall time, and
  while they lasted IDLE had no slot at all, since fetch was always ready above it.
- Result: a reset every ~29 s, since the map's 15 s retry came in [measured].
- Diagnosis: a UART core dump, decoded with gdb, `thread apply all bt`.
- Fix: equal priority on one core. The two take turns a tick each, the job ends in time, and the board keeps
  working while a map is made. A frame gets slower while a map is made, which is the honest cost.

```c
/* the TLS handshake runs on the fetch task, and the map is made there. one core (the C3): it
 * gets the screen's priority, the two take turns a tick each. under the screen it had less
 * than a millisecond a frame, a map took it over ten seconds and IDLE starved into the watchdog */
```

The same rule holds for the C5 builds, one core too. Flight Radar's `core/map.c` has no yield hook today. It lives
on the shortness of a map job and the equal priority. A bigger map or a slower chip would need one [reasoned].

### Air Quality: the I2C driver

ESP-IDF 5.5's `i2c_master` waits for a free bus with no time limit after a NACK on the classic ESP32 (in
`s_i2c_send_commands`). A chip at 0x40 put it there. The task watchdog restarted the board, the chip hung it
again [measured]: "a restart loop, the disease of the old OWLOS". Two fixes, both worth keeping:

- A bit-banged bus where every wait has a limit, a NACK is just an error, and nine clocks free a stuck slave:
  `code/i2c_bus.c`.
- A guard in RTC memory, see [crashes.md](crashes.md).

Flight Radar still uses `i2c_master` for touch chips, with 20 to 50 ms timeouts, on boards where it has not hung
so far. The 3.5" probes for a GT911 on two addresses at start-up and hears nothing on the R version [measured].
On a classic ESP32 with a sensor that may NACK in mid-transfer, take the bit-banged one.

### The moving layer boards

A contributor's 7" S3 and 10.1" P4 builds (the moving layer: the sweep and the planes laid over the picture while
the panel is fed) changed the loop and the watchdog for those boards only [docs, the contributor's FORK-NOTES.md
and the comments in `main.c`]:

- The ui loop waits for the next panel frame by a notification (at most 50 ms) instead of `vTaskDelay(1)`. A frame
  begun while the pass ran is let go, so the core is always free for part of a frame.
- Crashes found with the core dump on flash: each time the watchdog on core 0's idle task. The layer was built
  back to back, then a menu page drawn back to back, beside TLS handshakes in the fetch task. Fixed by pacing (the
  layer at most every 100 ms counted from the end of a build; a page drawn on core 0 leaves the core free for half
  its draw time, at least 50 ms).
- And these builds no longer watch core 0's idle task: a TLS handshake (seconds of elliptic-curve maths) beside a
  page left the idle task no turn, which is no hang, and the board was restarted for it. The ui task stays watched.
- A planner task at priority 6 (above the ui, below every network task) composes the heaviest chunks of the next
  frame ahead.

The lesson for any board: watching idle catches starvation, but a board that is busy by design for seconds needs
the watchdog on the task that must not stop, not on idle.

## The second core draws

`ui_strip` only reads what `ui_frame` set up: **drawing has no writes to shared state**. Keep it so or the second
core is a data race. Then a second task on core 0 can draw part of every frame. Two ways in Flight Radar:

- **Pairs.** Every other strip is the helper's, in a strip of its own, packed under this core's, and the two go
  out in one transfer (DMA buffers of two strips). The 3.5" and the PSRAM boards. Needed because esp_lcd's
  `tx_color` takes the bus and waits for the transfer before, so a double buffer alone did not overlap [measured].
- **Halves** (`DISP_HELPER_HALF`). The helper draws the lower rows of this core's own strip, into the same canvas,
  and the strip goes out as it always did. No strip and no DMA memory of its own, only its 7 KB stack. The 2.8"
  CYD, which has no room for pairs. It starts 1.6 s after boot, before the WiFi, and never stops. The cut row moves
  a row a frame toward who waited less (between a quarter and three quarters of the strip): the second core
  shares its core with WiFi and the fetch.

```c
// between frames: on when wanted and the memory is there (with room left for TLS), off otherwise
static void helper_set(int on)
{
    if (on == (s_helper != NULL)) return;
    // what it takes besides its stack: a strip of its own and the DMA buffers' second half. halves: none
    const size_t px = DISP_HELPER_HALF ? 0 : GFX_PW * DISP_STRIP * sizeof(uint32_t);
    const size_t dma = DISP_HELPER_HALF ? 0 : GFX_PW * DISP_STRIP * sizeof(uint16_t) * 2;
    if (!on) {
        s_help_stop = 1;
        xSemaphoreGive(s_help_go);
        xSemaphoreTake(s_help_done, portMAX_DELAY);   // out of its loop, it deletes itself
        s_helper = NULL;
        if (!DISP_HELPER_HALF) {
            heap_caps_free(s_help_cv.px);
            s_help_cv.px = NULL;
            disp_rows(DISP_STRIP);
        }
        return;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < px + dma + HELPER_STACK + 48 * 1024) return;
    /* ... semaphores, its strip, disp_rows(2 * DISP_STRIP), the task at priority 4 on core 0 ... */
}
```

Which looks get it: on the 2.8" and the PSRAM boards every look and page; on the 3.5" the radar and weather looks
and their pages only, because its square maps want the memory and are bound by the SPI bus anyway (53 of 82 ms
waiting on the 40 MHz bus) [measured]. With 8-row strips the 3.5"'s helper wanted about 87 KB free before it
started, so after MAP or CLASSIC it often waited for the radar [measured]; with 6-row strips it wants 7.7 KB less
[computed].

Numbers [measured]:

- 3.5" radar look: 139 -> 98 ms a frame, together with the flash speed. Two cores gave only about 1.25x,
  because each core's strip gets slower when both run.
- 3.5" weather look: 6 -> 9 fps (102-113 ms) with the second core.
- 2.8" CYD with halves, at 200 km with 48 planes: RADAR 9 -> 12 fps, WEATHER 12 -> 17, MAP 16 -> 19, CLASSIC
  13 -> 15. About 1.3x: core 0 also carries the WiFi and the fetch (165 KB of planes every few seconds at 200 km).
  A higher helper priority would trade fresh planes for frames; not tried.
- The helper's stack: 4980, later 4484 and 4436 of 7168 bytes free, as features grew.
- Air Quality's "two painters": ~120 ms of CPU across both cores for a frame of ~110 ms.

Layer timing skips the helper's strips: one clock, one core. A PC rig proves the rule that strips only read: draw
every look and page whole, in strips, and in halves cut at every row, and compare. 0 pixels differed on the
2.8", 3.5", 5" and 4.3" layouts; a 1 px difference on two boards that use pairs was found that way [measured on the
PC]. See [testing.md](testing.md).

## Slow start and long jobs: the yield by count

```c
for (int i = 0; i < 86400; i++) {
    thermo_tick(&t, &c, 1, sim_step(&s_sim, 1.0f, t.relay));
    if ((i & 8191) == 0) {                    // let the idle task breathe, and tell the watchdog we live
        vTaskDelay(1);
        esp_task_wdt_reset();
    }
}
```

Thermo Relay's and Air Quality's made-up day. On a screen firmware such a job lives on the data task.

## The console over USB

The site and any script talk to the board over the USB serial line at 115200 baud, a line each way. The protocol
(HELLO, READY, GET, SET, CFG and the rest) is in esp32-flash-and-settings. The runtime side, `console.c`:

- Two readers, one handler. UART0 through the UART driver (the log keeps writing there as it always did; this
  only adds the receiving side), and on chips with USB Serial JTAG (S3, C3, C5) the chip's own USB, read with the
  low-level calls `usb_serial_jtag_ll_rxfifo_data_available` and `usb_serial_jtag_ll_read_rxfifo`. No driver
  there: it would take over the log's way out too. The chip holds the host back while its 64 bytes are full, so
  looking every 10 ms loses nothing [docs, the comment; the native USB path is not run on a board here].
- A mutex lets one line in at a time, whichever port it came on. A line is up to 1024 characters; a longer one
  is dropped.
- A command is found from `OWLOS ` on, anywhere in the line: a reset (opening the port does one) leaves stray bytes
  that glue onto the front of the first line. Before this, GET right at READY worked 2 of 6 times; the site sending
  `\n` before each line made it 6 of 6 [measured]. Do both.
- A board whose chip USB pins are used for something else sets `NO_CHIP_USB`. A board with no usable UART0 (the
  P4 Tab5) sets `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, and the UART driver is not started at all: waiting for
  it held core 0 till the watchdog bit.
- The console task's 4 KB stack was full: `say_cfg` builds the whole CFG line on every GET, and one more small
  local overflowed it, 2 of 5 GETs restarted the CYD [measured]. Its buffers are static now, under the lock.
  Watch this when adding to the CFG line.
- The console runs at priority 3, below the fetch task: during a TLS handshake it answers about 2.7 s late
  [measured]. Harmless; do not read it as a hang.
- A SET is stored at once by the handler, unlike a change on the screen, which waits 3 s (see
  [storage.md](storage.md)).
- Lines meant for a page (the twin): `OWLOS SKY ON` from the page every 30 s; while it lasts each answer goes out as
  `OWLOS SKY BEGIN <server>`, a line `OWLOS PLANE {json}` a plane in the free networks' own words, and
  `OWLOS SKY END <total>`. `SKY_OUT_MS` 60 s runs it out by itself if the page goes. A line a plane, because a list
  in one piece is more than the 3.5" has to spare, and the log may fall between lines.

```c
/* 0: no page. a passed time is let go here (only if no SKY ON renewed it meanwhile): left as it was,
 * 24.8 days later the 32-bit milliseconds would wrap round to it and read it as ahead again, and
 * every answer would go out over the console for good */
static int page_there(void)
{
    uint32_t until = s_sky_until;
    if (until && (int32_t)(until - now_ms()) > 0) return 1;
    if (until) __atomic_compare_exchange_n(&s_sky_until, &until, 0u, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return 0;
}
```

- Test hooks on the console, each a line in `on_line`: `OWLOS KEY n` (an e-paper key as if pressed),
  `OWLOS PAPER n` (the sheet's tones till a reset), `OWLOS EPDTEST` and `OWLOS EPDLUT i v` (the e-paper's grey
  bands and its waveform bytes, RAM only), `OWLOS SKYSAVE` and `OWLOS SKYPLAY 1|0` (a sky kept in NVS and shown
  still, so two builds can be compared on one scene; the 7" builds). A throwaway hook for one test goes in a
  temporary build and out again, see [testing.md](testing.md).

## The e-paper loop

An e-paper picture takes a second or more and each full one flashes the glass, so the e-paper boards run the same
ui task with a different rhythm (`main.c` under `DISP_EPD`, `core/ui_paper.c`, `firmware/main/keys.c`). The
panel side is in esp32-lcd-touch.

**The keys** are read on a task of their own every 10 ms, because the ui task waits a second, or six, while the
glass changes, and a flick of the dial is far shorter:

- All keys low when pressed, on the chip's pull-up. A key counts once it reads the same for 30 ms.
- A key with a second meaning (the dial's press, MENU) counts on release, or as its long meaning after 700 ms
  held; the rest count when pressed.
- Events go into a queue of 8 with no wait. `keys_poll()` takes one a pass; boards without keys get 0.

**When a picture is made**, `paper_due()`:

```c
/* paper: a picture takes a second or more, and each one flashes the glass. one when a key or BOOT was
 * pressed, a quarter of a second after it so a few presses go in one, and else one every disp_frame_ms():
 * the planes' positions come every 5 s anyway. a picture with no pixel changed costs nothing (epd.c) */
static int paper_due(uint32_t now, int pressed)
{
    static uint32_t s_at, s_pressed_at;
    if (pressed) s_pressed_at = now ? now : 1;
    if (s_pressed_at && now - s_pressed_at >= 250u) {
        s_pressed_at = 0;
        s_at = now;
        return 1;
    }
    if (!s_at || now - s_at >= disp_frame_ms()) {
        s_at = now ? now : 1;
        return 1;
    }
    return 0;
}
```

Between pictures the loop does only its input and log work, resets the watchdog and sleeps 20 ms.

**Keys gathered.** UP and DOWN (the range, or the weather's page), MENU (the next look) and EXIT (the look before)
go through `ui_key_pend`: each press moves what is asked for and its third of the keys' strip prints it at once,
drawn on top. After `PEND_MS` (2000 ms) with no press, `ui_pend_take` applies it: one print for all the presses,
and none at all if the presses came back to where they started. OK and held keys print their third of the strip
black first (a partial refresh, about 1.2 s), then the key is taken and the screen is printed whole. Verified on
the 4.2": two presses further gave one print after the quiet, the map waited without partials; nearer then
further gave "as it was" and no print [measured].

**The frame hold.** A new screen (another look, page, range, or the map under it made again) is printed whole
with a full refresh, greys included. While it waits for its map, `paper_hold` is set and the firmware draws and
pushes nothing: the old screen stays on the glass, looked at again every 250 ms, for up to `MAP_WAIT_MS` (8 s)
with no WiFi and no tiles kept. A white sheet drawn on top first had been a long refresh for nothing.

```c
if (!paper_due(now, ev || key)) {              // the rest of the pass is the frame's: the log, the stats
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(20));
    continue;
}
ui_frame(&s_ui, now);
int hold = 0;
if (paper_new_sheet) { paper_new_sheet = 0; disp_full_next(); }   // printed whole, its greys too
hold = paper_hold;
if (hold) paper_due(now, 1);                   // looked at again in a quarter of a second
for (int sy0 = 0; !hold && sy0 < GFX_PH; sy0 += step) { /* the strips */ }
if (!hold) disp_frame_end();
```

Traps met [measured]: the console's key slot is single, so keys sent during a 6 s refresh overwrite each other;
`OWLOS KEY 7` (EXIT) is no longer harmless since EXIT steps the look back, so check the look with GET first.

## Clock wraps

- `uint32_t` milliseconds wrap at 49.7 days. Elapsed time by unsigned subtraction survives it.
- A deadline compared signed, `(int32_t)(until - now) > 0`, works for 24.8 days either way. A deadline left set
  after it passed reads as "ahead" again 24.8 days later. Flight Radar clears passed deadlines (`ui_until()` in
  `ui.h`, toast and menu times cleared in `ui_frame`, `page_there` above) [built, from a whole-code review].
- No board here has been up that long.
