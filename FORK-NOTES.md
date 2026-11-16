# Fork notes: Waveshare 7 / 7B and the moving layer

Engineering notes by Michael Caldwell-Waller (chipguyhere), written in the fork
[chipguyhere/OWLOS-FLIGHT-RADAR-7-7B](https://github.com/chipguyhere/OWLOS-FLIGHT-RADAR-7-7B) of Denis
Kirin's [OWLOS Flight Radar](https://kirindenis.github.io/OWLOS-FLIGHT-RADAR/) and brought in with it.
"This fork" below is that fork. The README's section The moving layer is the summary. This file holds
the design, the measurements and what is still open.

## Builds

| Build | `RADAR_DISPLAY` | Board | Glass | Layout (`GFX_W`×`GFX_H`) | Scale |
| --- | --- | --- | --- | --- | --- |
| `wave7` | 15 | Waveshare ESP32-S3-Touch-LCD-7 (N8R8) | 800×480 | 534×320 | 3/2 |
| `wave7b` | 16 | Waveshare ESP32-S3-Touch-LCD-7B (N16R8) | 1024×600 | 547×320 | 15/8 |
| `jc8012` | 17 | Guition JC8012P4A1C (ESP32-P4, 16 MB flash, 32 MB PSRAM) | 1280×800 lying on an 800×1280 panel | 512×320 | 5/2 |

The library detects the board at run time, but the layout is fixed at compile time, so each board
has its own build. Flashing the wrong one leaves the screen dark and the log names the right one.
Both use the CYD's 4 MB partition table (`partitions.csv`).

```sh
firmware/build.sh wave7b                              # build
firmware/build.sh wave7b /dev/cu.wchusbserial...       # build, flash, monitor
```

The 7B's text uses the 5" boards' 3/2 fonts at a 15/8 scale, so it is 4/5 of its intended size (the
Tab5 has the same compromise). A 15/8 font bake is still to come.

## Where the code is

| Path | What |
| --- | --- |
| `firmware/components/chipguy_lcd7_7b/` | chipguyhere's 7/7B board + display libraries, vendored. `shim/` supplies `Arduino.h` and `Wire.h` (TwoWire over the firmware's own `i2c_master` bus, which the GT911 shares) |
| `firmware/main/lcd_wave7_panel.cpp` | the library as a few C calls |
| `firmware/main/lcd_wave7.c` | `display.h` for these boards: the background in the frame that is not showing, flipped in when whole |
| `firmware/main/ovl.c`, `ovl.h` | the moving layer. Board-independent |
| `firmware/main/main.c` | `frame_layered()`: one pass of the ui loop per panel frame |
| `core/ui_scope.c` | `ui_layer` (BG / TOP), `main_top()`, `ticks_top()`, `ui_top_info()` |
| `core/gfx.h` | `GFX_ALPHA`: on these builds a pixel's top byte carries its coverage |
| `firmware/sdkconfig.defaults.wave7` | run-time stats (the library reads them), large plain `malloc`s to PSRAM |

Every change to shared code is behind `RADAR_OVERLAY` or `DISP_WAVE7`. The other boards compile as
before.

## The design

**Background.** Everything that stands still: the glass, rings, cross, map, panel, cards and pages. It
is drawn by the existing strip renderer with `ui_layer = UI_LAYER_BG`, into the PSRAM framebuffer that
is not being shown. It is flipped in at the next panel frame once its last strip is in, using the
library's two framebuffers. In BG mode the disc pass leaves out the sweep and the ticks, and the
planes and compass letters are skipped.

**Moving layer.** The planes (with trails, labels, glows), the compass ring's ticks and its
letters/numbers. Once per panel frame the ui task draws them with the existing primitives
(`ui_top_strip`) onto a canvas of zeros. In these builds `gfx_mix` carries coverage in the top byte
and `gfx_add` leaves it alone, so the canvas ends up holding premultiplied colour plus alpha. `ovl.c`
scans it and stores runs of 32-bit words in PSRAM. Each word is the colour spread as 565
(`0x07E0F81F`) with how much shows through (0..32) in the spare bits 5..10. Runs of eight or more
identical pixels (label cards) are stored as one word. There are three layer buffers (shown, ready,
building), and the fill takes the newest ready one at each panel frame.

**The sweep.** Not drawn by the ui task. The fill works out the trail and the bright edge for every
row from the time the panel frame started, so it turns smoothly even when the layer is late. The
trail is 32 angular steps. For each row, the x where each step's edge crosses it comes from a per-frame
tangent table, and the step's constant colour is added with a two-pixels-a-word saturating 565 add.

**Compositing.** In the vendored driver, `setOverlay()` (fork addition) gives the fill task a
callback. After the library's GDMA copy of a chunk is queued, the fill task follows the copy down the
chunk. The GDMA writes back each inbound descriptor as it completes, so `m2mLanded()` knows how many
bytes are there. Each row is composited as soon as the copy has passed it (bg + sweep + layer).
**Every row has a deadline**: the panel starts on the buffer one bounce period after its interrupt
and scans a row every period/rows, less 8 µs. A row that cannot start by its deadline is skipped and
left as the copy made it. Drawn late, it would land in the buffer after the panel had passed it, or
on top of the chunk 20 rows further down that the copy is already putting there. That was the
"bleeding downward" seen before the deadlines existed.

**Prepare, then apply.** Each row's work is in two halves. The part that needs no pixels (the
sweep's spans and edge, and fetching the row's layer words out of PSRAM into internal RAM) runs
while the copy is still on its way. The part that needs the pixels runs once the row has landed.

**The next copy starts early.** Between rows the fill looks at its queue. If the next chunk's
interrupt has already come, its copy is started at once instead of after this chunk's overlay. This
took the rows that were late waiting for the copy from ~11,000 to ~100 per 10 s.

**Cores.** Core 1 is the panel's: the bounce interrupt and the library's fill task (started from a
short task on core 1 before the WiFi, as the library's `lv_setup` does) with the compositing, and
under it, at the lowest priority, the **background task**. That task draws from its own copy of the
ui state (`ui_t`, 14 KB, in PSRAM), taken when it is ready for another frame. Its 4-row strip
buffer is in internal RAM, its stack in PSRAM. Core 0 runs the ui loop (the moving layer only)
beside the WiFi. The layer being drawn is a field of the canvas, not a global, because both cores
draw at once.

## Changes to the vendored library

All are marked "fork addition". Candidates to upstream:

- `setOverlay(fn, ctx)`, `_overlayRows()`, and the `g_ovl*` counters.
- `M2MXfer::nd` and `m2mLanded()`: the progress of the newest m2m chain, from the GDMA's write-back.
- Bounce rows built at 10, not 20 (`LCD7_7B_BOUNCE_ROWS`, a compile definition in the component),
  for internal RAM.

## Measured, -7B, 2026-11-11

Demo sky (no WiFi set), 12 planes, radar look, latest build:

| | |
| --- | --- |
| Panel | 32.7 Hz, 0 fills dropped, 0 desyncs |
| Worst chunk, interrupt → overlay done | ~0.95 ms (deadline-bounded) |
| Rows without the sweep (skipped as late) | 0 per 10 s in the latest runs (live data, few planes) |
| Rows with the sweep but not the layer | 0 per 10 s in the latest runs; the sweep goes first, the layer only if its words fit before the row's deadline |
| Of those, waiting on the copy | ~100 per 10 s |
| Fill: sweep / layer composite | ~3.6–4.1 ms (trail on alternate rows, `OVL_SWEEP_EVERY 2`) / ~3.5–3.8 ms per panel frame |
| Layer words | ~14.5k a frame, with the DEMO card's box a hole (~28k without) |
| ui loop (core 0) | layer made in ~62 ms, so it changes at ~15 fps |
| Background (core 1) | a whole frame every ~1.9 s |
| Internal RAM left | ~8 KB, **~3 KB at the least**: too tight |

How it got there, for the record: a CPU copy of every chunk took the whole core (thousands of late
chunks). Waiting for each chunk's whole DMA before compositing made copy + composite longer than a
period. libm calls in the fill (running from PSRAM under XIP, behind the busy DMA) cost a third of
the sweep. The ui on core 1 beside a fill that spins on the DMA starved. Before the per-row
deadlines, late rows bled into the next chunk sharing the buffer. Before the early start, the next
chunk's copy waited for this chunk's overlay.

## Checked on the -7B again, 2026-11-14

After the JC8012 work, against the published build (fc04749), live data, 22-31 planes:
- **The S3 builds did not compile**: the planner's runway hooks (`ovl.c`) were inside `#if RADAR_OVERLAY_COLS`, and
  `g_busy` (busy.h) was defined in `lcd_jc8012.c` alone. Both moved; all three builds compile.
- **Unchanged**: 32.7 Hz, 0 fills dropped, 0 desyncs, ~13-16 fps, late rows and worst chunk alike. Plane fetches after
  the first take 11-19 s on both builds (not the fork's doing). The kept connection (`net.c`) is hardly ever reused
  here, as the fetch task changes host between requests; its TLS buffers are in the PSRAM.
- **Internal RAM** was 3.7 KB down (the log task's stack), and while map tiles came in it fell to 183 bytes at the
  least. Now the `fetch` (12 KB), `console` (4 KB) and `log` (3 KB) stacks are in the PSRAM on these builds too (XIP
  from PSRAM: a flash write leaves the cache on): 45 KB free, 31 KB in one piece, 10 KB at the least through three
  tile downloads. The once-a-minute memory report (regions, stacks) runs on these builds as well.
- Internal stacks still mostly unused, if more is wanted: `ui` ~4.9 KB of 8, `esp_timer` ~3.1 KB, `console usb`
  ~3.2 KB of 4, `sys_evt` ~2.8 KB, `planner` ~3 KB of 4.

## Open

1. **The layer is not frame-locked yet** (15 of 32.7 Hz). Making it costs ~62 ms on core 0. Likely
   costs: every band re-walks every plane and all 72 ticks; the scan of the bounded box; code
   running from PSRAM. Next: per-frame lists of what touches which band (ticks especially), and
   profiling `planes()` per band.
2. **The background redraws every ~1.9 s** on core 1, below the fill. Settings pages have no moving
   layer, so they show at that rate too.
3. **~3% of overlay rows are skipped**, mostly in the middle of the screen, where the disc is
   widest and the labels gather. They are CPU-bound. The trail is now on alternate rows
   (`OVL_SWEEP_EVERY` in ovl.c, 1 for every row): its cost fell from ~6 to ~3.8 ms. The layer
   composite (~6.5 ms, ~28k words) is now the larger half. Next: fewer layer words (glows are the
   bulk after labels), and the S3's 128-bit PIE for the flat runs and the sweep spans (as in the
   library's `RasterBouncingBalls`).
9. **XIP and the PSRAM bus.** With XIP, code and rodata misses come from PSRAM, but the fill's hot
   path is in IRAM. Flash and octal PSRAM share the S3's memory bus and cache, so moving code back to
   flash would not relieve it. An SD card for the tiles would only matter if flash writes stalled
   the panel, which XIP already prevents. The late rows measure as CPU-bound, not bus-bound.
10. **The screen goes dim on its own sometimes and does not come back** (the 7B, seen twice). Re-sending
    the backlight every 10 s does not bring it back. The 10 s log now says whether the expander took
    the write, the expander's lines (backlight, panel reset, panel supply), and the battery-input ADC:
    first readings all acknowledged, lines as they should be, battery input moving between 4.16 and
    4.49 V with no battery fitted. If the lines stay right while it dims, suspect the power path (the
    7B's charger, or the USB port's current under a 7" backlight).
12. **Popups are holes in the moving layer.** `ui_top_info()` lists the boxes the app draws over the
    scope (the DEMO card, a toast, the update note), and neither the sweep nor the layer is put in
    them. They are drawn after the planes and the sweep in the original too. Their 5-px chamfered
    corners are not cut out.
13. **The sweep comes first.** Each row has a deadline from the library (`g_ovlRowDeadline`): the
    sweep is always applied, the layer only if its words fit, about 30 cycles a word.
11. **Chamfered corners at 15/8.** The scaled `gfx_hline`/`gfx_vline` weight each glass row and
    each end pixel by how much the layout pixel covers it. At scales other than 3/2, `chamfer_box`
    draws all six sides with `gfx_line` through the pixels' middles, so the cut corners meet the sides
    (checked in a magnified render of `gfx.c` built on the Mac). The 3/2 boards draw boxes as before.
14. **Prepare-ahead.** While a row's copy has not landed, the fill prepares up to two rows ahead
    (`LCD7_7B_OVL_PREP_AHEAD`, a three-row ring in ovl.c; layer words are pulled into the cache, not
    copied). Per panel frame it was spending ~4.7 ms preparing, ~4 ms applying and ~10 ms spinning on
    the copy. Late rows now come in bursts in ~70–130 chunks of ~20,000 per 10 s, most likely rows
    of dense label text (~1,000 words). Next: the S3's 128-bit PIE for the per-word composite.
15. **RAM.** The moving layer's band is 6 rows (24 KB). Big state no interrupt touches is in PSRAM
    on these builds (`CORE_BIG` in core/gfx.h, `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`): `s_ui`,
    the sky and its reader, the map and its plan, the ring tables, the background's gradient and
    vignette, the grid. Internal RAM: ~24.8 KB free in steady state, 6.9 KB at the least (was 611
    bytes). Less was gained than those symbols' ~65 KB; not looked into yet.
16. **PIE composite.** Layer runs of 16 pixels or more are stored as two 16-bit arrays (colour,
    see-through amount), lined up with the panel row on 16-byte boundaries. A PIE kernel built only
    from instructions `RasterBouncingBalls` already runs on the S3 composites them 8 pixels at a time,
    bit-exact with the C version. A self-test at start compares the two on 4,096 random pixels and
    falls back to C if they differ (it passes on the 7B). The layer's composite fell from ~4 to
    ~0.7 ms a frame.
17. **Nothing to overlay, nothing to wait for.** The overlay answers `ovl_want()` per chunk; a chunk
    with no layer words and no sweep in its rows is copied as the library normally does, without
    following the copy. Off the radar page (settings, keyboard) the fill no longer holds core 1, and
    the background task that draws those pages gets it.
18. **The sweep's bright edge as spans.** Near 3 and 9 o'clock the edge runs along a row (hundreds
    of pixels). It was worked out a pixel at a time, at most 96 a row; now its coverage is stepped by
    adding, and pixels of one brightness become one span (applied two pixels a word).
20. **The busiest rows, pre-rendered.** When a layer is made (core 0, at most every 100 ms), each row's
    cost to the fill is estimated (~300 cycles a run + ~60 a stored word, measured). Rows over
    `OVL_BUSY_CYCLES` (~17 µs), whose covered span is no more than twice their stored words, get that
    span made whole: the shown background with the layer on it, in a PSRAM row cache (up to
    `OVL_BUSY_ROWS` 192 a layer). The fill copies the span into internal RAM during prepare and onto
    the row once it lands, then adds the sweep, which on these rows lies over the planes. Whole rows
    were tried first and were far worse (a 2 KB PSRAM read each; ~55,000 late rows per 10 s). With
    48 planes: ~150–170 rows a layer, ~190–250 rows without the sweep and ~120–220 without the layer
    per 10 s, from 1,400–3,900 and 4,500–9,800.
21. **Crashes, found with the core dump on flash.** The watchdog, each time core 0's idle task: the
    layer built back to back, then the menu page drawn back to back, beside TLS handshakes in the fetch
    task. Fixed by pacing (the layer at most every 100 ms counted from the end of a build; a page
    drawn on core 0 leaves the core free for half its draw time, at least 50 ms). These builds no
    longer have the watchdog watch core 0's idle task; the ui task stays watched. `chamfer_box` fills
    its six-sided shape row by row: the smooth polygon fill made a keyboard page take seconds.
22. **The heaviest chunks of the next frame, composed beforehand on core 0.** A planner task on core 0
    (priority 6: above the ui task, below every network task), woken at each panel frame's start, plans
    the next frame: its layer, and where its beam will be (both latched by the fill at that frame's
    start, so composed and live chunks agree). It estimates every chunk's cost to the fill from the
    internal RAM only: the sweep from its spans, the layer from a per-row cost table made with the
    layer. It composes the 8 heaviest over `PRE_CYCLES` (`PRE_PICK`), in scan order, into 64-byte-aligned
    PSRAM chunk buffers: the background's rows, the sweep, and the layer too where that chunk's layer
    is heavy (many planes; `PRE_LAYER_CYCLES`), else the fill adds the layer. The library's new `src`
    hook copies such a chunk from its buffer instead of the framebuffer; a chunk composed whole is a
    plain copy, the core yielded. Frozen 48-plane test sky, per 10 s: rows without the sweep 40–82
    without this, 1–10 with it. Lessons on the way: core 0 reading every layer word every frame to
    estimate costs, composing ~20 chunks a frame, and running the planner from the ui loop (it missed
    two frames in three) each made the panel later, by PSRAM traffic or by the plan not being there.
23. **A test sky.** `OWLOS SKYSAVE` / `OWLOS SKYPLAY 1` / `OWLOS SKYPLAY 0` on the serial console keep
    a live sky in NVS and show it, still, instead of the network: builds can be compared on the same
    scene (repeatable to about ±15 rows per 10 s).
19. **Networking.** TLS uses software AES on these builds: the hardware AES took a DMA buffer from
    internal RAM for data in PSRAM, which failed ("esp-aes: Failed to allocate memory"). Wi-Fi modem
    sleep is off on these boards. On every board, Wi-Fi now scans all channels and joins the
    strongest access point of the network (`WIFI_CONNECT_AP_BY_SIGNAL`): ESP-IDF's default joined the
    first it heard, here one at -85 dBm with another at -47 dBm a few metres away. This last one is a
    general fix the original project may want.
4. **The radar's ground echo is gone.** The original lit the map's land and roads brighter just
   behind the beam, per map class. The fill has no map classes, so the trail is the same colour over
   all of it.
5. **The trail has 32 steps and no dither**, so its fade can band slightly.
6. **The -7 runs** (2026-11-11): 22-23 fps on the demo sky, no late rows, 2.4 MB of PSRAM heap free. A build
   folder made before `sdkconfig.defaults.wave7` existed kept the old settings and hung on a dark screen;
   lcd_wave7.c now refuses to build with them.
7. **RAM is tight**: 64 KB of strip buffer left no room for the ui task's stack. The strip/band
   buffer is shared (12 rows, 48 KB).
8. XIP from PSRAM is on (upstream's `s3rgb` defaults). Map tiles and settings are written to flash.
   Check the late/dropped counters while tiles are being cached.

24. **The compass ring follows a finger.** (`beam_step`, a plane moving only when the beam goes over it,
    was on by default here for a while; it is off again, as the original author wants it. A board with
    options already saved keeps its own setting.) While a finger turns the compass on the rim, the moving layer is the ring
    alone (`ring_only` in `ui_top_t`) and is made again every pass instead of every ~270 ms, so the ring
    turns at the panel's pace; the planes leave the layer meanwhile and come back, turned, once the
    finger lets go. The background (map, cross) catches up at its own pace (~2 s a frame on the -7B).

## The Guition JC8012P4A1C (ESP32-P4)

Build 17, `firmware/build.sh jc8012` (target esp32p4; `sdkconfig.defaults.jc8012` after the Tab5's
`sdkconfig.defaults.esp32p4`, partitions `partitions_jc8012.csv`). Written and measured 2026-11-13 on a
board with a v1.3 P4.

### Touch firmware

The GSL3680 touch controller runs from RAM and needs ~18 KB of firmware uploaded at every power-up.
That firmware is Silead's and GPL-2.0, so it is not in this MIT tree: the vendored driver
(`components/chipguy_jc8012/gsl3680_touch.cpp`) reads it from NVS (namespace `jc8012`, key
`gsl3680fw`), checks its SHA-256 and uploads it. It is put there once per board by the
`InstallFirmware` example of chipguyhere's `chipguy_ESP32-P4-C6-firmware-installer` Arduino
library. Without it the radar runs and touch is off; the log says `no touch: touch firmware not
installed in NVS`.

The order is: installer first, then this firmware. The installer is an Arduino sketch, whose
partition table puts NVS at 0x9000-0xDFFF (20 KB) and otadata at 0xE000; `partitions_jc8012.csv`
has NVS at 0x9000-0xEFFF (24 KB). Measured 2026-11-15: after the installer, `idf.py flash` of this
build kept the blob - NVS accepted the Arduino otadata in its sixth page without the "no free
pages" error that would make `app_main` erase it - and touch came up.

### Where the code is

| Path | What |
| --- | --- |
| `firmware/components/chipguy_jc8012/` | chipguyhere's JC8012P4A1C display (1.0.0) and board libraries, vendored, with the 7/7B shim plus `analogWrite`, `getCpuFrequencyMhz`, `rgbLedWrite`. Not the terminal or LVGL glue. Touch is the library's MIT GSL3680 driver, which loads the controller's firmware from NVS (see below) |
| `firmware/main/lcd_jc8012_panel.cpp` | the library as C calls; the touch turned to the lying picture |
| `firmware/main/lcd_jc8012.c` | `display.h`, the PPA turning the strips, the raster fill, the GDMA copier, the helper on core 0 |
| `firmware/main/ovl.c` | `RADAR_OVERLAY_COLS`: the layer by columns. The S3 builds compile as before |

### The design

- **The frame stands.** Each background strip (80 picture rows, drawn in the PSRAM) is turned by the PPA, 270° (90° when
  flipped), into the hidden 800×1280 frame: the picture's row y becomes the panel's column 799 − y.
  The frame is shown from a panel frame's start, chosen by the fill (below), so the panel's DMA and
  the fill change frames together.
- **The panel's rows are the picture's columns.** The whole panel is the library's raster band: 80
  chunks of 16 panel rows (16 picture columns). A chunk the moving layer touches this frame is copied
  from the frame into its bounce buffer and composited; a chunk it does not touch is read by the panel
  straight from the frame (fork addition to the driver, `setRasterDirect`).
- **The layer by columns.** The ui task still draws the layer in bands of picture rows. Each band is
  read down its columns into records (x, then runs along y); when the layer is whole the records are
  sorted into columns, so the fill finds a column's words in one piece. The run words are the S3's,
  with y where they had x. The sweep is worked out with x and y swapped: the beam then turns the other
  way, at 270° − its angle.
- **The copy is the GDMA's, a chunk ahead.** Reading 25.6 KB out of the PSRAM takes ~180 µs whatever
  does it (CPU, GDMA, both at once: measured), and a chunk has ~235 µs. So while a chunk is composited,
  the AXI GDMA fetches the next chunk's background straight into that chunk's bounce buffer.
- **Two cores composite.** Core 1 does the first 8 lines of a chunk, a top-priority helper on core 0
  the other 8.
- **Lead and a safety net.** The library keeps 4 bounce buffers and draws chunk c when c − 3 has been
  read (fork addition), so each fill has two chunk times. A fill that starts with the panel already on
  the chunk before reads that chunk from the frame this time, without the layer, rather than showing a
  stale buffer: the chunk four before, 64 columns off ("slices moving sideways").

### Changes to the vendored library

All marked "fork addition". Candidates to upstream:

- The framebuffers allocated on the 128-byte cache line (`heap_caps_aligned_calloc`): the PPA's output
  and any range `esp_cache_msync` need it; plain `calloc` only happened to be aligned.
- `chipguy_lcd_dpi_panel_raster_direct()` / `setRasterDirect()`: a chunk read from the framebuffer
  instead of its bounce buffer, patched into every framebuffer's DMA list in place.
- `setRasterBuffers(4)`: four bounce buffers, chunk c drawn when c − 3 has been read; `rasterBuffer(c)`.
- `rasterChunksRead()`: how far the panel's DMA has got, so the callback knows how much time is left.
- `setRasterDirect` before the band is in use (2026-11-14): the bit is kept instead of dropped.

### Measured, 2026-11-13

Demo sky, 12 planes:

| | |
| --- | --- |
| Panel | 52.0 Hz, 0 underruns |
| Chunks a frame | ~47 filled, ~32 straight from the frame |
| A filled chunk | ~103 µs on core 1 (copy overlapped, half the layer), of ~235 |
| Chunks given up / finished late | ~25 and ~16 per 10 s (of ~25,000 filled) |
| Background | a whole frame every ~0.41 s |
| Internal RAM | ~93 KB free in steady state, ~28 KB at the least (TLS) |

How it got there: the CPU copying each chunk out of the PSRAM took ~400 µs (slices visibly late).
ESP-IDF's async memcpy cost ~165 µs a call to set up (new link lists, the source written back from the
cache); the GDMA driven directly with lists made once costs ~14 µs. The frame's first chunk was ~1.7 ms
late every frame: ~130 `tanf`/`fmodf` calls from flash in the beam's set-up, now turned step by step
in IRAM. **XIP from PSRAM** made the biggest single difference (filled chunks ~150 → ~103 µs, background
frame 0.8–5 s → 0.41 s): flash runs at 40 MHz here and shares the L2 cache with the PSRAM. It also keeps
flash writes (map tiles, settings) from stopping the fill. It costs ~11 KB of internal RAM; the
background strip went from 16 rows to 8 (80 → 40 KB) to fit.

### Heavy traffic: the layer's words by DMA, joined runs, PIE (2026-11-13)

On a frozen 32-plane sky over China (`OWLOS SKYSAVE` / `SKYPLAY 1`; ~60,000 layer words a frame), the
fill lost the overlay in ~1,650 chunks per 10 s. CPU-bound: ~33 cycles a pixel word in the fill. Three steps:

- **The words come with the chunk.** The layer is kept by columns, so a chunk's 16 lines are one piece
  of it: the GDMA fetches it into one of two 16 KB internal buffers in the same transfer as the background
  (`ovl_chunk_words` / `ovl_chunk_local`). Given up: ~1,650 → ~690 per 10 s.
- **Runs joined across the bands.** The ui task draws in 16-row bands, so no column run was longer than 16
  pixels. `ovl_build_end` now joins a run onto the one before where they meet (`s_cols`, then laid out).
  On its own no faster, but it makes the runs long enough for PIE.
- **PIE on the P4.** Runs of 16 pixels or more are laid out as the S3's two 16-bit arrays, in the panel's
  memory order (header bit 15: which way round, so a layer made before a flip is skipped). The kernel is the
  S3's steps in the P4's `esp.*` instructions (`vld/vst.128`, `andq`, `vmul.u16` with SAR, `vadd.u16`,
  `vmin.u16` to hold each field to its top, `orq`); they take only registers x8–x15, hence the `a0`–`a4`
  register variables. Bit-exact with the C one at start. Given up: ~690 → ~25–37 per 10 s; finished late
  ~20–26; worst chunk ~0.54 ms.

**The background in 80-row strips.** What was left came from the fill's fetches being slow: ~1.5% of them
with nothing else going on, ~4% while a background strip went through the PPA, ~3% while core 0 made a
layer, ~9% with both (measured). An 8-row strip turned by the PPA writes 16 bytes into each of the frame's
1280 rows, 100 times a background frame, and the PPA's driver drops the cache over the whole 2 MB each
time. The strips are now 80 rows (`DISP_STRIP`), drawn in the PSRAM (the canvas falls back there when the
internal RAM has no room in one piece; the background has no deadline): 160 bytes a row, 10 turns a
frame. On the 32-plane sky, flipped: chunks given up and chunks finished after the panel began on them
went from ~400 and ~300 per 10 s to **0 and 0**. (The library's own "late" count stays in the
thousands: with four buffers it counts any moment the fill has a second chunk waiting, even two chunks
ahead of the panel. The fill's two counts are the ones that show on the glass.)

### Chunks drawn for the wrong place (2026-11-13)

Seen on the glass: several chunks in a row shown whole but one or more chunks over, till the next frame.
The driver knew which chunk was done by counting the DMA's chunk interrupts. The PPA's driver drops the
cache over the 2 MB it wrote round (a turned strip spans all 1280 rows) with the core's interrupts held off;
on the core that takes the panel's interrupts, two chunks' interrupts then came as one (~40 times per 10 s),
and every later chunk of that frame was drawn into the buffer meant for another. Fixed in the driver: the
DMA's next list item (normally the chunk just done + 2, measured) says which chunks are done; merged ones
are reported too (`raster_merged`, in the 10 s log). The job queue is 16 deep, not 8: after such a stall the
jobs come in a burst, and a dropped one left a stale buffer on the glass.

Tried and dropped on the way: turning the strips on the CPU instead of the PPA (cache traffic made up to
~1,500 chunks a 10 s late), pacing it to the columns read straight from the frame (no better), and the PPA
on the first core (its stall then held up the other half of each chunk; a half the helper has not begun is
now done by the fill itself). Lateness on the 32-plane sky still varies a lot between runs (~0 to ~700 chunks
given up per 10 s, always without the overlay for that frame rather than shown wrong): the PPA's stall is
still the likely cause. The background is made in 80-row strips.

The moving layer itself changes only ~4 times a second on that sky (~120 ms to make: ~45 drawing the planes
in bands, ~55 reading the bands into columns, ~25 sorting), so the planes step visibly now that beam step is
off. Core 0 is ~38% idle.

### Internal RAM and the pages (2026-11-14)

**Where the internal RAM goes.** Static: 105 KB of IRAM code, 46 KB `.data`, 89 KB `.bss` (`ovl.c`'s tables
~70 KB of it). Heap: the four bounce buffers 102 KB, the moving layer's band (`s_top`) 80 KB, the layer
words fetched with each chunk 32 KB, the time-critical task stacks (ui, planner, fill2, raster, ppa).
Before: ~75 KB free while running, 48 KB at the least, the largest free block ~31 KB, and TLS handshakes
failing. Moved to the PSRAM (`sdkconfig.defaults.jc8012`, `main.c`, `console.c`): mbedTLS
(`MBEDTLS_EXTERNAL_MEM_ALLOC`), lwIP's buffers (`SPIRAM_TRY_ALLOCATE_WIFI_LWIP`), ESP-Hosted's task
stacks (`ESP_HOSTED_DFLT_TASK_FROM_SPIRAM`), the fetch and console stacks. With XIP from PSRAM a flash
write leaves the cache on (`SPI_FLASH_CACHE_NO_DISABLE` in `spi_flash_os_func_app.c`), so PSRAM stacks are
safe. Now ~133 KB free, ~112 KB at the least. Hardware AES works with its buffers in the PSRAM here
(unlike the S3). The SDIO buffers stay internal (DMA). Once a minute the log prints each internal region
and each task's unused stack (P: in the PSRAM). Not done: `SPI_FLASH_PLACE_FUNCTIONS_IN_IRAM` can only be
turned off with flash auto-suspend; the touch's 2 KB config table would need `const` through the vendored
driver.

**The handshakes still fail sometimes** (ADSB.LOL, `-0x7280` after 20-30 s) with 110 KB free, and ADSB.FI
can take 8-10 s for 18 KB: not the RAM. The WiFi link (C6 firmware 2.1.10 against Hosted 3.1.0, SDIO at
40 MHz) or the servers are what is left to look at.

**The strips are turned by the CPU, not the PPA.** `JC_CPU_TURN` in `lcd_jc8012.c` is 1 ("TEMP A/B", left on
from an earlier session; the build of 2026-11-13 had it too): `cpu_turn` writes a column at a time, syncs the
cache once a column (1,280 a strip) and paces itself to the panel (`layer_pace`). Measured at boot: ~12 ms at
16 rows, ~27 at 80, ~42 at 128, then ~190 ms at 160 and ~925 ms for the whole picture. The whole picture as
one strip also made the radar's background slower (~1.5 → ~2.1 s). Which of the two the radar's background
should use is still the open A/B.

**Pages (settings, keyboard, lists).** Before: any pass of the ui loop on a page drew it whole (the rest
between pages never applied, since `build` is always set on a page) and sent all of it: ~630 ms a page, a key
up to ~1.5 s. Now (`page_draw` in main.c):
- a page is drawn only when the ui's state has changed (`page_changed`: `ui_t` without the clock, cut to the
  cursor's 500 ms blink, and without `spot`/`order`, which the radar remakes every pass), on a finger down or
  up, or once a second; otherwise a look every 30 ms costs a 14 KB compare;
- it is drawn in 16-row bands in internal RAM (`s_top`, idle on a page), each band summed; the bands whose sum
  changed are copied to a PSRAM stage and, once all are drawn, turned one after another straight into the frame
  that is showing (no second frame to keep, no flip): `disp_page_turn`, the CPU turn without the per-column
  cache sync or the pacing, then one `esp_cache_msync` over the frame (`disp_page_flush`). Turned as each was
  drawn, a key reached the glass in pieces (seen as 5-6 steps, then as stale lines);
- tried and dropped: the cache's registers driven directly (`cache_ll_*`, per column or all at once), past
  `esp_cache_msync`'s lock: stale lines on the glass, and once the SDIO link to the C6 timed out and restarted
  the board;
- the touch is read between bands: the library believes a lift after four empty reads, and read once a pass
  (a pass ~130 ms while typing) a release took ~0.5 s to register;
- the first pass of a page is drawn whole into the frame not showing and flipped in.
Keyboard, typing: finger down or up to the glass ~130 ms (measured before the staging and the touch reads between bands); idle,
only the cursor's blink (~4 bands twice a second). Still to do: draw only the bands a change can touch, not the
whole page to find them.

### Scrolling pages by gathering (2026-11-14)

The setup, the networks and the plane list scroll by the fill, not by drawing. Measured first
(`JC_GATHER_TEST` in lcd_jc8012.c, off now): the AXI GDMA gathers a chunk of 48 pieces (each panel row a fixed
start from the frame, a span of the list, a fixed end) **right at any pixel offset, source and destination**,
in ~151 µs against ~159 for a plain chunk copy (190 / 181 with the panel also reading the PSRAM). ESP-IDF sets no
PSRAM alignment for the AXI GDMA (the burst rule is the AHB one's). Mounting the 48 pieces with
`gdma_link_mount_buffers` costs ~55 µs, so the lists are mounted once and only each item's address and owner
bit are written per chunk (through the lists' non-cached view, `gdma_link.c`'s item layout mirrored).
- At rest, the page's viewport (`ui_scroll_info`, the rows the page clips its list to with `gfx_sub`) is drawn
  for the list's whole height, a viewport a pass, turned and dithered into a PSRAM buffer in the panel's order
  (`disp_scroll_put`, a column at a time: a row at a time cost ~840 ms a list). Drawn again when what the list
  shows changes (the ui without clock, finger, scroll, toast).
- A drag (`t_down && t_moved`) with the list ready: `disp_scroll_to(GFX_Z(scroll))`; the fill takes the offset
  once a panel frame and gathers every chunk into its bounce buffer, the next one while this one is used
  (`prefetch_next`). Nothing is drawn meanwhile. The setup's scroll bar columns are left to the frame.
- The finger lifts: the page is drawn at rest into the frame not showing, and the gather stops with the panel
  frame that shows it.
- Two things held the ui task to ~2 passes a second at first: the fill wakes it from `ovl.c`'s `frame_latch`,
  which the gather path skipped (now `ovl_frame`), and the empty moving layer built every pass is paced by
  `layer_pace`, which waits for chunks read straight from the frame: none are while scrolling (~4.7 s in 10).
- `OWLOS SCROLLTEST` on the console: the setup dragged by a pretend finger (slowly up, quickly down), with a
  count of ui passes and the longest gap. Now ~47 passes a second while dragging, the longest gap ~115 ms
  (the page drawn once when the finger comes down, before it counts as a drag); 0 chunks gathered late.
- Any page, a finger that is only resting or creeping (under the 14-pixel slop): `touch_rects` sees that only
  the finger changed and redraws a stripe where it came down (whatever lights there) and the ring's box, by the
  keyboard's rectangle path. Before, each such pass drew the whole page (~150 ms), and a slow start looked like
  the old scrolling.
- Anything a page changes beyond that (a value, a tab, a toast) is drawn whole into the frame not showing and
  flipped in. Written into the frame showing, a pressed button showed cache-stale pieces.
- The ring under the finger is drawn into the list when a drag starts (`sc_ring`): what is under a dragging
  finger stays under it, so the ring goes with it. Taken out again when the gather stops.
- A change of the finger alone is drawn as rectangles, but all of a change's bands are drawn first and kept in
  the PSRAM, then turned one after another and the cache written back once (more than 25 bands: the whole page
  into the frame not showing instead). Sent band by band, a full-width stripe took tens of ms and the panel
  showed the pressed button half new for a few frames.
- The list stays usable for a drag while only what it says is out of date (the setup shows the network's
  state, which changes as the WiFi joins); it is drawn again over itself between drags, and the page is drawn
  at rest when the finger lifts. Only another page or another length makes it unusable.
- `OWLOS SCROLLTEST` now starts with the finger resting 0.5 s and creeping under the slop: 198 ui passes with the
  finger down, the longest gap 96 ms, 0 chunks gathered late.
- Not yet: the setup's scroll thumb following during a drag, the plane list while its data changes (each change
  draws the list again, ~300 ms).
- Momentum (2026-11-14, not yet built or tried): `ui_touch` keeps the finger's speed from the samples that
  moved; a lift within 150 ms of the last move at ≥ 0.25 px/ms sets `u->fling`, and `ui_frame` steps it
  (speed decays by e every 325 ms, stops at either end or under 0.02 px/ms). `sc_tick` counts a fling as a
  drag, so the fill keeps gathering till it rests. A finger on a coasting list stops it and is not a tap
  (`t_caught`). Portable: the other boards get it too, drawn as their pages are.

### Radar buttons waited for the background (2026-11-14, not yet built or tried)

SET on the radar took seconds: the radar's buttons are in the background, and with `JC_CPU_TURN` a background
frame is ~1.5-2 s. A press waited for the frame under way, then a frame of its own; the release (SET opens the
setup) waited for the frame under way again, since `draw_here` needs `!s_bg_busy`. Now:
- the background frame can be given up (`bg_cancel`: between strips, and every 16 columns of `cpu_turn` through
  `disp_bg_stop_fn`): on any page, on a radar button pressed or let go, on any lift on the radar;
- a radar button pressed or let go (`ui_pressed`) is drawn at once as a stripe ±24 layout rows into the frame
  showing (`rects_to_glass`, factored out of `page_draw`), and the next background starts from that state;
- the touch is also read between the moving layer's bands (a heavy sky's layer outlasts a panel frame, and a
  lift needs 4 empty reads). The 10 s log has a `touch:` line with the counts.

### Orientation, the network, the log (2026-11-14)

- **Normal is camera up.** `disp_flip` inverts the setting's sense (setting 0 = `s_flip` 1), and `jc_touch`
  maps the touch for camera-up; the setting's "flipped" is the way this build first drew it.
- **Fixed: the panel's first 3 chunks (a strip ~8 mm wide, the right edge camera up, the left before) showed
  their bounce buffers' leftovers**, i.e. the last chunks with the moving layer in them, for good. At start
  the raster band is still pending (put in at the first frame's end) while chunks 0-2 are filled;
  `chipguy_lcd_dpi_panel_raster_direct` checked the chunk against the band in use (0 chunks) and dropped the
  call, and the fill, which keeps its own `s_direct`, never asked again. The library now keeps the bit always
  and patches only lists that have the band (a pending band's lists are rebuilt from the bits). Candidate to
  upstream. Found with a TEMP line in the 10 s log (`TEMP first chunks`: per chunk the fill's flag, the
  library's bit, and where each frame's list item reads from), left in for now.
- **The network does not block the ui.** Measured (`pacing:` in the 10 s log): the ui task waited for `s_lock`
  0 times. `net_get` keeps one connection open (`esp_http_client_perform`, the body through `ON_DATA`; the
  open/read calls cannot send a second request on a connection): a plane request 0.2-1.1 s instead of 5-10 s.
- **The 10 s statistics held the ui ~0.3-0.5 s** (~3 KB at 115200 baud). `logbuf.c`: ESP_LOGx formatted
  into a 64 KB ring buffer in the PSRAM, written out by a priority-1 task; full: lines dropped and counted.
  After: the longest gap between ui passes 38 ms with an empty sky. With 7 planes on the radar, ~30 gaps over
  100 ms (up to ~250 ms) per 10 s with the fetch task idle: the radar pass itself, still to find.

### The moving layer's build in its own task (2026-11-14)

Measured with 48 planes (per-pass timing, `pass ms` / `layer builds` in the 10 s log):
- `layer_pace` as it was (wait till the panel and the next 5 chunks are read straight from the frame, 25 ms
  cap per call, ~140 calls a build) never found such a stretch on a busy sky: a build waited up to 3.4 s, the
  radar ran at 1 fps, and chunks were as late as without it (20-208 vs 85 per 10 s, `OWLOS NOPACE`).
- Unpaced, a build took ~650 ms inline in the ui task: touch and buttons waited that long.
- Now `builder_task` (core 1, priority 3, under the ui's 5) builds from a copy of the ui into its own band in
  the PSRAM; the ui hands it a copy when it is free, and a page coming up cancels a build under way. The
  ui runs ~49 passes a second (longest gap ~50 ms), builds come ~2.6 a second (~300 ms: draw 115, pack 105,
  end 37), and late chunks fell to 0-13 per 10 s, paced or not.
- `layer_pace` now waits only while one of the next 6 chunks is hot: `ovl_chunk_hot`, the planner's chunks
  over `PRE_CYCLES` it did not compose (the runners-up; the user's idea). It hits the 30 ms per build cap on
  a busy sky and showed no measurable gain over unpaced: lower the cap, or keep it for when it matters.
- The console: opening the port resets the board and leaves stray bytes in the UART, which hid the first
  line's command; `console.c` now takes the line from "OWLOS " on.
- Next: the build itself (draw and pack, ~110 ms each with 48 planes; each 16-row band draws every plane in it).

### Keeping off the fill's runway; PIE and the planner (2026-11-14)

48 planes, ~30 heavy chunks a frame, the planner composing 10 of them (`TEMP planner` in the log):
- `TEMP busy` (busy.h): each task sets a bit while it works; the fill counts them over all chunks and the late
  ones. Late chunks are nearly all hot (the planner's runners-up, composed by the fill itself, ~450-550 us of a
  234 us slot) and finished late, not given up. The planner was over-represented (69 % of chunks, ~91 % of late).
- `PRE_PICK` 16: ~3,000 late per 10 s (the PSRAM traffic; the user's point). Back to 10.
- PIE in the planner: the "8-pixel slivers" were ESP-IDF 5.5's FreeRTOS port (portasm.S `pie_save_regs` /
  `pie_restore_regs`): q3 never saved, SAR and FFT_BIT_WIDTH swapped on restore, so the helper preempting the
  planner mid-kernel broke it. Fixed upstream: espressif/esp-idf `b25cb2906c7d` (v6.0 on, master) and
  `51d5738ad769` (release/v5.5 head, 2026-10-28; no v5.5.x tag yet, v5.5.6 still has it). Our tree (a9de5ec5b9)
  predates it. Tried here: the scheduler held around each planner PIE call: ~2,000 late per 10 s (the helper's
  wakes held back). So the planner stays without PIE till the IDF has the fix.
- **The runway** (the user's point: the fill gets ahead in the cheap stretch before a hot run, and work there
  spends that slack): `runway_clear` in lcd_jc8012.c, no hot chunk among the next NBUF + 6 chunks the panel
  reads. The build, the background and now the planner (`ovl_planner_quiet`, a tick at a time, at most 4 ms a
  frame, only while the chunk can still be ready) wait for it. Late: 148-402 → 3-31 per 10 s; the planner still
  composes its 10 a frame. A first try spun the planner in 20 us steps: it starved core 0 (the log stopped).
- **The IDF fix, then PIE in the planner.** `51d5738ad769` cherry-picked into `~/espidf5.5/esp-idf` on a local
  branch `pie-context-fix` (from release/v5.5 at a9de5ec5b9; `git checkout release/v5.5` undoes it). With it the
  planner uses PIE unguarded (`OVL_PLANNER_PIE` 1 in ovl.c; set 0 to build on an IDF without the fix): late
  3-31 → 0-11 per 10 s, the planner busy at ~56 % of chunks instead of ~64 %. To check on the glass: no white
  8-pixel slivers in planes.

### Cheaper hot chunks: the layer's paths (2026-11-14)

`TEMP layer path` in the log: the fill's (and helper's) layer time by path in `words_apply`, 48 planes, per 10 s.
Before: PIE runs 12.6 cycles a pixel (~11 M px), their C edges 47, solid runs 19.7 (~9 M px), per-pixel words 37
(~4.6 M px, ~4.5 px a run).
- `pie_solid`: translucent solid runs of 16+ through PIE (colour and amount loaded with no step): 19.7 → ~16.5.
- `pie_blend2` / `pie_solid2` (used when they pass the start-up check against `pix`): SAR set to 5 once a block
  instead of 9 writes; each field scaled where it lies (blue and green fit 16 bits as they are; red's
  ((r<<11)*u)>>5 = (r*u)<<6 at most 63,488, masked to 0xF800), blue and green held by vmin, red by
  `esp.vcmp.lt.u16` + or (the wrapped sum is below what was added). PIE 12.8 → 7.7 cycles a pixel, solid
  → ~14.3; late 2-4 per 10 s; the planner busy at ~50 % of chunks.
- Padded runs (`pad2_*` in ovl.c, by columns only): a long run is kept in whole 8-pixel blocks on the 16-byte
  grid, its ends the identity (colour 0, shows 32), so the fill puts it on in one kernel call and the C edges
  are gone. The long runs' time (kernel + edges) ~0.41 → ~0.29 s per 10 s; layers ~7 % more words (largest
  ~81,000 of `OVL_CAP` 98,304; "too full" once at start, as before). Late 0-10 per 10 s.
- `PIE_MIN_COLS` 8 (was 16) and `OVL_CAP` 128 K words for the JC8012 (512 KB a layer; the S3 builds keep 96 K):
  per-pixel words ~447 → ~287 ms per 10 s, PIE runs ~287 → ~417 (the short runs' padding), the layer's time
  about even (~1.03 → ~1.01 s); layer words ~37,000 → ~44,000 a frame, the largest ~97,000 (over the old
  cap). Late 0-2 per 10 s. Kept for now; 12 or 16 would trade back words for a little fill time.
- Left: per-pixel words (~42 cycles a pixel, runs under 8, ~3.4 px each, ~140 cycles a run) and solid runs
  (~15). The TEMP path counters themselves cost a little on every run.

### Open

0. **The flip** works with the layer by columns (checked 2026-11-13).
1. **Wi-Fi works** with the C6's own firmware (2.1.10) against ESP-Hosted 3.1.0, which logs "major
   version mismatch"; nothing failed so far.
2. **Touch** works (tapping a plane, turning the ring, the buttons: checked 2026-11-13). The flip and BOOT
   not yet tried.
3. **The text is 3/5 of its size** (the 3/2 fonts at 5/2), as on the Tab5. A 5/2 font bake is to come.
4. **The sweep in PIE.** The layer's long runs use the P4's PIE now; the sweep's spans (a constant added
   along a line) are still C, ~3–4 ms a frame on the heavy sky.
5. **`esp_lvgl_port` is pinned to 2.8.x** for P4 builds (`main/idf_component.yml`): 2.9.0 uses a DPI
   callback ESP-IDF 5.5.3 lacks, and the Tab5's package pulls it into every P4 build.
6. Flash runs at 40 MHz on this build although `sdkconfig.defaults.esp32p4` asks for 80: not looked into.
