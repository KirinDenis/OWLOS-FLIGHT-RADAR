# How a frame runs, and what makes it fast

The contract between the screen code and the firmware, the loop, sending strips, only what changed, two cores,
the timing log, the numbers measured per board, and the cures in the order they paid. Code:
[code-frame.md](code-frame.md). The renderer itself: [renderer-and-canvas.md](renderer-and-canvas.md). Tasks,
priorities and watchdogs in general: [esp32-runtime](../../esp32-runtime/SKILL.md). The SPI side of a strip:
[bus-spi-strips.md](../../esp32-lcd-touch/references/bus-spi-strips.md).

How sure: (M) measured on a real board, (P) built and seen in the preview or read in the code, (D) docs or a
contributor's notes, (R) reasoned.

## The contract: ui_frame once, ui_strip many

```c
void ui_frame(ui_t *u, uint32_t now);   // once a frame: time, layout, what the strips share
void ui_strip(ui_t *u, canvas_t *cv);   // once a strip: paint. READS ONLY
void ui_touch(ui_t *u, int down, int x, int y, int rx, int ry, uint32_t now, ui_cmd_t *cmd);
```

Three rules hold all four projects up:

1. **Everything shared is worked out in `ui_frame`.** Sorting, screen positions, label placement, trails in
   screen coordinates, plot columns, grid lines, the weather dial's stars, clouds and words. A strip only draws
   what falls in it. Flight Radar: trails walked per strip cost 62 ms a frame on the 3.5", once a frame 7 ms (M).
   The grid: 38 of 110 ms (M). The weather dial: forty strips each working the stars, clouds and words out again
   made the look three times slower (M).
2. **`ui_strip` writes nothing shared.** That is what lets a second core draw strips at the same time. Air
   Quality: "the state only changes between frames, when the painter is waiting". The weather's tables made in a
   strip also raced on two cores: both strips asked for them and both gave them back at the first frame of
   another look (M, fixed by moving the work into `weather_frame`).
3. **The UI only reads the data.** It works on a private copy (`u->snap`, taken under the lock once a frame) and
   asks for changes in `ui_cmd_t`, which the caller applies. Secrets never enter it: the screen only learns
   whether a password is set.

The core has no hardware in it: time comes in as `now`, the clock for profiling as a pointer `ui_clock_us`,
pixels leave through `canvas_t`. So the same files build on a PC (pc-preview-tool.md) and in a web page as WASM.

Check rule 2 by drawing: a PC rig drew every look and page whole, in strips, and in two halves cut at any row,
and compared. 0 pixels differed on the 2.8", 3.5", 5" and 4.3" layouts (P). Keep such a check for every new page.

## The loop (firmware/main/main.c, shortened)

```c
for (;;) {
    uint32_t now = now_ms();
    ui_cmd_t cmd; memset(&cmd, 0, sizeof cmd);
    int ev = button_poll(now);
    if (ev) ui_button(&s_ui, ev == BTN_LONG ? 2 : 1, &cmd);
    touch_sample(1);                                   // and the samples queued between strips
    for (int i = 0; i < s_nts; i++) ui_touch(&s_ui, s_ts[i].down, s_ts[i].x, s_ts[i].y,
                                             s_ts[i].rx, s_ts[i].ry, s_ts[i].t, &cmd);
    s_nts = 0;
    /* apply cmd: save settings 3 s after the last change, wake other tasks */
    lock(); memcpy(&s_ui.snap, &s_sky, sizeof s_sky); unlock();

    ui_frame(&s_ui, now);
    for (int sy0 = 0; sy0 < GFX_H; sy0 += DISP_STRIP) {
        canvas_t cv = { work, sy0, sy0 + DISP_STRIP <= GFX_H ? DISP_STRIP : GFX_H - sy0, 0 };
        ui_strip(&s_ui, &cv);
        disp_strip(&cv);                               // packs to 565, waits for the strip before, starts DMA
        touch_sample(0);                               // every 15 ms or so
    }
    disp_frame_end();
    if (light < 100) disp_backlight(light += 10);      // fades in over a real picture
    /* every 10 s: the log below */
    esp_task_wdt_reset();
    vTaskDelay(1);                                     // the idle task must run
}
```

`disp_init` runs inside the screen task so the SPI interrupt sits on that core, away from WiFi.
`CONFIG_FREERTOS_HZ=1000` makes `vTaskDelay(1)` a millisecond, not ten.

Backlight on before the first frame (R): a lit panel with nothing sent shows whatever is in its RAM. Backlight 0 at
init, then up by 10 after each frame until 100: it fades in over a real picture.

## Sending a strip

Two DMA buffers, fill one while the other goes out. First strip of a frame: set the window, RAMWR `0x2C`; the rest
RAMWRC `0x3C`, all three chips know it. Wait for the done callback before the next transfer: sending while one is
in flight hung for ever in `spi_device_acquire_bus` (M, a rare hang, a core dump showed it; light views draw
faster than the 40 MHz bus). Pack first, wait after, so packing overlaps the transfer:

```c
uint16_t *buf = s_buf[s_k];
s_k ^= 1;
gfx_pack565(cv, buf);   // this buffer went out two strips ago and we waited for that, so its free
if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;   // max 200 ms, count it, go on
```

Wire time is the floor: `bytes * 8 / Hz`. A whole frame on the wire:

- 320x240 at 40 MHz: 30.7 ms (R).
- 480x320 at 40 MHz: 61 ms (M).
- 480x320 ILI9486 on shift registers at 20 MHz: 123 ms (M).
- 240x240 C3 at 80 MHz: 11.5 ms (R).

More strips cost more transfers: the 3.5"'s strip at 6 rows instead of 8 took MAP from 12 to 10 fps and CLASSIC
from 11 to 9, RADAR the same (M). The cost per transfer is not understood. Smaller tile pieces with 8 rows is the
untried other way.

A panel that stands still (M, cause unknown): a 3.5" was found frozen after about a day, the code alive. Every 5 s
between frames re-send sleep out, pixel format, inversion, display on and MADCTL. Invisible on a healthy panel, a
glitched one comes back by itself.

The touch on the LCD bus (M): the 3.5" XPT2046 shares the screen's SPI. Asking while a strip is out collides. Call
`disp_wait_idle()` first: the touch is read only between transfers, at 2 MHz.

## Dirty regions: only what changed (Air Quality)

On the ILI9486 at 20 MHz a full frame cannot pass 8 fps. But a dashboard changes little. So after packing, each
strip is cut in 32 px columns, each column gets an FNV checksum over its packed pixels, and only runs of changed
columns are sent, each with its own window. Runs closer than 2 columns are merged, 4 runs at most. A still strip
sends nothing. It needs no bookkeeping in the page code at all: the pages redraw everything and the checksums find
out what moved. That is the point of the design.

- Checksums are per strip, so two cores packing two strips never meet.
- `disp_invalidate()` when the glass may not match.
- It only pays on a slow wire. On a 40 MHz panel a full send overlaps drawing anyway.
- Log `rows sent a frame` to see it work.

Code: `disp_pack`, `disp_send` in code-frame.md.

## Two painters on two cores

The classic ESP32 has two cores. The second is free when WiFi is idle or absent.

- **Flight Radar, 3.5", `DISP_HELPER`**: the helper task on core 0 draws the strip under the main core's strip and
  packs it into the second half of the same DMA buffer (`disp_pack_under`); both go in ONE transfer
  (`disp_strip2`), because the driver's tx_color acquires the bus and waits for the transfer before, so two
  separate sends keep the sender waiting (M). Its strip, stack (7168 B) and the DMA buffers' second halves are
  taken only for the looks that use it, between frames, and only with 48 KB left for TLS (`helper_set`). It wants
  pixels, DMA, stack and 48 KB free, about 87 KB on the 3.5", so after MAP or CLASSIC it often waits (M). On the
  square map the SPI is the limit (53 of 82 ms waiting on the bus, M), so the helper stays off there. On the 3.5" it
  runs in RADAR and WEATHER and their pages; its stack had 4 436 to 4 980 B of 7 168 free (M).
- **Flight Radar, 2.8" CYD, `DISP_HELPER_HALF`**: the second core draws the lower rows of this core's own strip,
  so it costs only its 7 KB stack. It starts 1.6 s after boot, before the WiFi, and never stops. The cut row moves
  a row a frame (6 to 18 of 24) toward the core that waited less. At 200 km with 48 planes: RADAR 9 to 12 fps,
  WEATHER 12 to 17, MAP 16 to 19, CLASSIC 13 to 15 (M). Only about 1.3x: core 0 also carries WiFi and the fetch.
- **Flight Radar, PSRAM boards**: pairs on every view and look (P, not run on one here).
- **Air Quality**: no network, so always on. Core 1 draws even strips, core 0 odd ones. Each painter packs what it
  drew (dithering and checksums are a third of the work) into its own two buffers; core 1 sends all of them in
  order.

Gain: about 1.25x to 1.3x, not 2x, because each core's strip gets slower when both run: they share the flash
cache and memory (M). Air Quality: about 120 ms of CPU across both cores in a 110 ms frame (M). Each painter needs
its own work buffer and scratch: nothing shared between the two. `ui_lap` skips the helper's strips: one clock, one
core. Plan for 1.25x, and use the second core only where the frame is CPU-bound.

Memory for two looks (M): a DMA buffer pair sized for the helper is memory the square map's texture needs. So
`helper_set` takes the strip, the stack and the second halves only for the look that uses them, and only when 48
KB stay free for TLS. Between frames only.

## Timing layers

Never guess where a frame goes. `ui_lap` adds the time since the last lap to `prof[layer]`, the page calls it at
layer boundaries, and the firmware logs it every 10 s:

```c
void ui_lap(ui_t *u, const canvas_t *cv, int layer)
{
    if (!ui_clock_us || cv->helper) return;
    uint32_t now = ui_clock_us();
    if (layer >= 0 && layer < (int)(sizeof u->prof / sizeof u->prof[0])) u->prof[layer] += now - s_lap;
    s_lap = now;
}
```

```
I (...) 10 fps, 98 ms a frame, view 0, 21 planes, heap 57344
I (...) ms a frame: background 6, disc 37, compass 3, planes 9, panel 14, the rest 1, sending 8, touch 1
I (...) the second core draws: waited for it 12 ms a frame, its stack 2100 bytes free
```

(The shape of the log. The numbers in this example are made up.) Air Quality shows fps and heap on its SENSORS
page too: the instrument reports on itself.

The WEATHER look reuses the radar's layer names: "disc" is the dial's pixels, "compass" the marks on it, "planes"
the frame's planning work (about 1 ms on the ESP32), "panel" the middle, corners and panel. So tell the looks apart
in the log before believing a speed (M: a board booted into another look than asked, twice).

## Measured, per board

- 2.8" CYD, ESP32, WiFi Radar: 15 to 19 fps. A 2 h log clean, heap 49 KB, 0 lost strips (M).
- 3.5" ST7796, ESP32, WiFi Radar: about 8 fps, 118 ms, heap 24 KB (M).
- Round GC9A01, C3, WiFi Radar: about 8 fps, heap 31 KB, most of the frame in the blips (M).
- 3.5", Flight Radar, RADAR look: 139 to 98 ms with flash QIO 80 plus the second core. The flash alone 124 to 99,
  two cores about 1.25x (M).
- 3.5", Flight Radar, MAP look: 110 to 81 ms. SPI-bound, 61 ms of it on the wire (M).
- 3.5", Flight Radar, the map made easier to see: MAP disc 45 to 50 ms, CLASSIC 46 to 51, RADAR 65 to 66 (M).
- 3.5", Flight Radar, WEATHER: dial 8 to 10 fps, graphs 9 to 11; with the column table (wx_hud) 9 to 12 fps, 80 to
  106 ms, heap 35 to 52 KB (M).
- 2.8" CYD, Flight Radar, WEATHER: dial 17 to 20 fps, graphs 20 to 25, heap down to about 25 KB during fetches;
  with the column table 17 to 18 fps, 53 to 56 ms, heap 28 to 46 KB (M).
- Round C3, Flight Radar, WEATHER: dial 6 fps (the disc 51 to 55 ms after the beam went), graphs 7 to 9 (M).
- ILI9486 480x320, Air Quality: about 8 fps, about 110 ms, two painters, dirty columns (M).
- 5.79" e-paper, S3, WiFi Radar: full refresh 3.1 to 3.3 s, partial 1.86 s, a new picture every 5 s (M).
- CrowPanel Advance 5.0 (800x480 RGB bus, scaled layout), Flight Radar, first run on a tester's board: 3 fps and 280
  ms a frame on the radar (disc 151, panel 39, packing into the frame 52), 14 fps on SET. The second core never
  started: no room for its strip (M, from the tester's log).
- Waveshare 7, 7B, JC8012 with the moving layer: see renderer-and-canvas.md (D).
- Boards built and never run here have no numbers. Say "New: not run on one here yet".

## What eats the frame, and the cures in the order they paid

1. Work repeated per strip: move it to `ui_frame` (62 to 7 ms). The symptom: a frame takes 62 ms more about 80 s
   after boot (once trails are full), or a look is far slower than its pixels. The square map's grid was the same (38
   of 110 ms on the 3.5", and the C3 worked it out in soft floats). The cure is a `*_frame` function once a frame
   that stores the result (screen coordinates in 1/8 px, row ranges), and strips only draw what `gfx_touches`. Every
   expensive draw call gets a `gfx_touches` guard too: letters not looked up for nothing.
2. Skip what is not in the strip: `gfx_touches` before each element, even before formatting its text.
3. A disc walked per pixel costs about 1 us a pixel (M). Tables by radius, one angle table of an eighth of a turn,
   one pass for glass, rings, ticks, cross and beam. Per-pixel static layers cost 22 ms until baked (M).
4. Per-pixel vignette: 14 ms (M). Now a colour per row and only 56 px at each side get darker.
5. Flash speed: QIO 80 MHz, code and fonts are read from it. 124 to 99 ms (M). All three local boards booted with
   it. But the older WiFi Radar notes say 80 MHz flash made the 2.8" CYD hang randomly, and that repo stays at 40.
   Nobody found out why they differ (DIO against QIO, or another cause). Try QIO 80, run it for hours before
   shipping, keep 40 ready.
6. Trig: a sine table and a polynomial atan for HUDs ("most of a frame" before, M). `sinf` once a frame or once an
   element, never a pixel. The compass reuses the tick table's sine and cosine.
7. Tables are not always faster: `gfx_pack565` got slower with them (9.7 to 10.8 ms), Air Quality's angle table
   gained 2 ms (62 to 60 ms) for 29 KB of the 98 KB left, so it stayed off (`GFX_ANGLE_TABLE`, M). Tables mean
   memory reads through the cache.
8. A second core: about 1.25x, last, because it costs memory and care.
9. Fewer bytes on a slow wire: dirty columns.
10. Specialise the hot loop: `ALWAYS_INLINE` with constant flags. A loop that may meet the map runs out of
    registers and was slower by a sixth even with no map in it (M). The row's ground is looked up in a loop of its
    own before the pixel loop.
11. Give memory back between looks: the weather dial's tables are taken while the look is up and freed when it
    goes, because the 3.5"'s map wants every byte (P). The weather once took about 1.5 KB the tile reader needed
    and broke the 3.5"'s square map (0 of 6 tiles every time, M); fixed on the map side and by the shorter strip.

## Traps of the frame

- **A PC profile that lies (M).** The preview said all is fine, the board was slow: the PC run had no full trails,
  the slow state only comes after 80 s of flying. Make the run reach the device's state first. Air Quality's
  `--prof` runs a whole simulated day first. Only proportions mean anything on a PC, never absolute numbers.
- **The screen task starves the others on one core (M).** C3: below the screen task's priority the fetch task had
  under 1 ms a frame, map and TLS work starved IDLE into the task watchdog, a reset every 29 s. Same priority for
  fetch there, and `vTaskDelay(1)` at the end of every frame so IDLE runs. WiFi Radar met the same with a solver on
  core 0: it gives the core back after every round.
- **A big strip block (M).** 792 x 24 rows x 4 bytes is one 76 KB block, not there after WiFi: 8 rows.
- **A "look came up" rule by time (M).** The weather dial took a gap of over 400 ms between frames for "the dial
  just opened" and restarted its draw-in; a C3 frame with network work runs longer. Tell it by the frames: the
  dial was not up last frame.
