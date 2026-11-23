---
name: esp32-ui-drawing
description: Drawing, UX and animation on small ESP32 screens with no LVGL, as the OWLOS firmwares do it (Flight Radar, WiFi Radar and others). A software renderer that paints the scene in strips into a 32-bit buffer and packs dithered RGB565, anti-aliased lines, discs and polygons by distance, baked 4-bit fonts with Latin Extended and Cyrillic, themes where each colour has one meaning, radar scopes, round HUDs and a 24-hour weather dial, graphs and honest data screens, an OpenStreetMap ground on a small screen, 4-grey e-paper design (inks, halftones, soft letters, a key strip, partial refresh), touch gestures and hit areas, an on-screen keyboard, first-run flow, the RGB LED as UX, layouts from 240x240 round to 1280x800, and a PC preview that renders the same C to PNGs and films. Use when writing or changing the screen code of an ESP32 firmware on an SPI, RGB or MIPI panel or e-paper, when there is no RAM for a framebuffer, when frames are slow, or when a screen must look good, stay calm and say what it does not know.
---

# Drawing, UX and animation on small ESP32 screens, no LVGL

Four firmwares share one way of drawing: OWLOS Flight Radar (the newest, start there), OWLOS WiFi Radar, OWLOS Thermo
Relay and OWLOS Air Quality UI. There is no shared core: each repository has its own copy, and this skill is how the
next one gets it. A whole radar with a sweeping beam runs on a classic ESP32 with no PSRAM at 8 to 19 fps, with about
60 KB of RAM for all the drawing buffers (M). The same renderer was carried into another project's firmware
(FusionEdge) for its boards without PSRAM.

This file is the general part and the index. Every detail, number and code piece is in `references/`, one file per
topic, listed at the end.

The neighbours in this collection: [esp32-lcd-touch](../esp32-lcd-touch/SKILL.md) has panel init, buses (SPI strips,
RGB, MIPI-DSI), touch chips and e-paper panels. [esp32-boards](../esp32-boards/SKILL.md) has one file per board.
[esp32-runtime](../esp32-runtime/SKILL.md) has tasks, memory and watchdogs.
[esp32-data-sources](../esp32-data-sources/SKILL.md) has the services the screens show (planes, weather, map tiles).
[esp32-flash-and-settings](../esp32-flash-and-settings/SKILL.md) has flashing, the site and NVS. This skill starts at "a
strip of pixels can be sent" and "raw touch can be read".

## How sure each fact is

- **(M)** measured or seen on a real board here (or, where it says so, on a tester's board from its log)
- **(P)** built, and seen in the PC preview or read in the code, not run or measured on a board
- **(D)** read from a datasheet, a maker's example, a contributor's notes or docs
- **(R)** reasoned here, not measured

A board that is built and never run here is "New: not run on one here yet". Code in `references/code-*.md` is verbatim
from the repositories, named at each piece. Flight Radar's newer code (named in the topic files) wins over an older copy.

## How this family draws

1. **No framebuffer: strips.** A 320x240 RGB565 frame is 150 KB and the biggest free block after WiFi is about 110 KB.
   So the scene is drawn again for each strip of a few rows (6 to 24 on the radars) into a small 32-bit work buffer (8 bits a channel keeps
   alpha, light and dark gradients clean), then packed to 565 with a 4x4 ordered dither and sent by DMA while the next
   strip is drawn. Coordinates are always screen coordinates; a strip is only a window. The price: work inside a strip
   is multiplied by the strip count.
2. **`ui_frame` once, `ui_strip` many.** Everything shared (sorting, positions, labels, trails, plans of a dial) is
   worked out once a frame. A strip only reads, and draws what falls in it: that is what lets two cores draw strips of
   one frame. The UI works on a private copy of the data and asks for changes; secrets never enter it.
3. **The core has no hardware in it.** Time comes in as a number, pixels leave through a canvas. So the same C builds
   the firmware, the PC preview (PNG shots, films) and a WASM twin in the site's page.
4. **Coverage is distance.** Every smooth shape (lines, rings, discs, convex polygons, arcs, the scope's ticks, a graph's
   line) finds a pixel's distance to its edge and uses it as alpha: 1 px of soft edge, no supersampling. Per-pixel work
   is in whole numbers for the C3, which has no FPU.
5. **Light is added, paint is mixed.** Glows, beams, rims and halos go through a saturating add, so they never dull what
   is under them. Gradients and glass come from tables by row or by radius, built when the theme changes.
6. **One theme struct, colours by meaning.** Pages write `C_PRIM`, `C_FOCUS`, never a number. Five themes change how a
   colour looks, never what it says.
7. **Two kinds of text.** A 3x5 pixel font for instrument labels (354 bytes) and TTF fonts baked to 4-bit coverage for
   names (any UTF-8) and big numbers.
8. **Each surface its own drawing.** A round glass gets its own views, not a square with corners cut off. E-paper gets
   its own screens drawn in inks, not the LCD in greys. Bigger glass gets the same layout scaled, in glass pixels where
   pixels are walked by hand.
9. **The PC first.** Every page is seen in the preview before the first flash; shots prove a change changed nothing it
   should not.

## Design principles

These are what was kept, every time, on every screen. Most came from a person looking at a board.

1. **Start from the product's own shape.** Before laying a screen out, look at what the product already is (the radar
   is radial, so its weather is a 24-hour dial) and push that further. A layout that is merely correct, rows and axes
   by default, reads as a finished engineering task.
2. **One colour, one meaning, in all five themes.** Height, or security, or heat. Never decoration. On a data screen the
   theme's colours carry everything and one accent is kept for warnings, only where a warning applies.
3. **Weight goes on what moves.** The boldest mark on an instrument must be the one that changes: the room's glowing
   dot on the thermostat, the hand at now on the weather dial. The eye reads a big still mark as decoration whatever the
   legend says.
4. **Grey only for what never changes.** On e-paper a law of the glass, on LCDs a good habit: rings, grid and labels of
   the instrument are `quiet`, what you read is `focus`.
5. **Doubt is drawn.** What the instrument does not know, it shows as not known: a hollow ring, a `~`, `---`, `DEMO` in
   big letters, a dashed line where the power came back. A confident picture of something unknown is the worst bug a
   screen can have.
6. **Numbers big, labels small; a number with a word.** The value in a smooth font, its name small above it. A state
   comes with its reason and the next step: `IDLE, on at 21.8`, `WARMER`, `FALLING`.
7. **The thing you set and the thing you measure never look alike.** The room huge in the middle, the target smaller,
   boxed, labelled TARGET.
8. **Light on dark, added, not painted.** Rims, beams and halos through `gfx_add`.
9. **Labels or numbered dots.** Where words do not fit between the marks, number the marks and put the words in a
   legend. A label that finds no free place is not drawn: never overlap.
10. **Say how the instrument works on the instrument.** `DRAG: ZOOM`, `RIM: TURN`, `TAP A CHANNEL TO SEE WHO SHARES IT`,
    in `quiet`, in a corner. On e-paper the key strip says what each key does now.
11. **Calm. Data screens stand still.** Everything that moves on its own moves slowly. On a screen people read for its
    numbers, decorative motion goes (no beam over the weather, no sweep over a graph); the weather itself may move
    (rain falling, a sun's rays turning). Every mark says what it is, and now looks the same on every page.
12. **What must be found at once gets its own colour, a dark edge, and goes on top**: the home pin in a theme colour
    nothing else uses.
13. **The golden section where proportions are free.** The weather dial's core is 0.382 of it, a graph's now stands at
    0.382 of its width, the words over a printed graph take 0.382 of the height.
14. **Reuse what was approved before inventing a variant.** The printed radar's map got invented faint tones and was
    nearly invisible; the CLASSIC sheet's approved tones fixed it.
15. **Text areas may follow a circle.** Beside a round instrument each row starts where the circle lets it.
16. **Never a dead button.** A button that cannot work says why (`NO FR24 KEY: SEND IT FROM THE SITE`); a key that does
    nothing here says nothing.

## The order of work for a new screen

1. **Look at what the product is.** Which instrument is this screen part of, which mark will move, what will a person
   read first, what is unknown or made up. Pick the form people already know (an analyser's humps, a clock, a
   barometer) so it needs no manual.
2. **Draw it in the preview first.** A scripted session that reaches the board's real state (trails grown, a day of
   weather), a shot per state. Show a picture early and let the picture answer questions.
3. **Plan once a frame, draw per strip.** Put every computed thing in the frame's plan; in the strip guard each element
   with `gfx_touches` before even formatting its text. Check: whole, in strips and in halves, 0 px differ.
4. **Lay it out with macros, not numbers per board.** Sizes from `GFX_W`, `GFX_H`, the panel's width, "the first of these
   that fits". Render every size and every theme; keep the old sizes pixel-identical.
5. **Make it honest.** What does it show when the data is missing, old, or made up? Say it on the screen.
6. **Make it calm.** Only what means something moves, by the clock, not by frames.
7. **Touch and keys.** Hit areas bigger than drawn, the nearest thing within a radius, a ring under the finger, a
   pressed state that goes out when the finger moves. On paper a key strip.
8. **On the board.** Flash, read the 10 s log (fps, ms per layer, heap), cure in the order that paid (frame-and-speed.md).
   Photograph e-paper: the glass has less contrast than the preview.
9. **Before calling it done**, go through it asking: which mark moves, and is it the boldest? Which colours mean
   something, and is it the same in all five themes? What does it show when the data is missing, old, or made up? What
   does a stranger with a new board do first? Does it say so? Would a stranger ask "what is this?" about any mark?

## Starting a new repository

```
core/            no hardware: builds on the PC, in ESP-IDF and as WASM
  config.h       which display: GFX_W, GFX_H, RADAR_ROUND, RADAR_EPAPER, RADAR_HAS_TOUCH, GFX_SCALED
  gfx.h gfx.c    code-gfx.md, as they are (Flight Radar's newest copy)
  font.h font.c  code-fonts.md, plus font_*.c from FontBake
  paper.h paper.c   if there is e-paper
  ui.h           ui_t, ui_cmd_t, views, settings struct
  ui_int.h       theme_t, C_* macros, layout macros, HIT_* codes, helpers
  ui.c           themes, background, helpers, touch, alignment, ui_frame, ui_strip
  ui_<page>.c    one file a page or two
firmware/main/
  board.h        pins per board
  display.h      disp_init, disp_strip, disp_frame_end, disp_backlight
  lcd_spi.c      strips by DMA
  touch.c        tp_read (not touch_read: that name clashes with librtc.a)
  main.c         the screen task: frame-and-speed.md
tools/preview/   BUILD.BAT, preview.c, png.inc
tools/FontBake/
```

Order that works:

1. `config.h`, gfx, font with MICRO only. Build the preview, get a PNG of a filled strip with a line and a disc.
2. FontBake SMALL and MEDIUM, with the ranges in fonts-and-font-baking.md.
3. A theme table, `draw_background`, `button`, `chamfer_box`, `toast`.
4. The first page in `ui_strip`, a scripted tap, a shot. All pages in the preview before the first flash.
5. On the board: `lcd_spi.c`, strips, the 10 s log. Read fps and layers.
6. Touch: samples between strips, 5 crosses, the ring under the finger.
7. Then look at the log, and cure in the order of frame-and-speed.md.

## Where things are

Topic files (read the one you need):

- [references/renderer-and-canvas.md](references/renderer-and-canvas.md): strips and their heights, the canvas, colour
  maths, fills and gradients, packing to 565, clipping, whole numbers for the C3, scaled glass (`GFX_SCALED`), the moving
  layer on RGB and MIPI boards.
- [references/frame-and-speed.md](references/frame-and-speed.md): `ui_frame` and `ui_strip`, the loop, sending strips by
  DMA, dirty columns, two painters on two cores, the timing log, fps measured per board, the cures in the order they paid.
- [references/anti-aliasing-and-soft-edges.md](references/anti-aliasing-and-soft-edges.md): coverage by distance, every
  primitive, the fast root, passes of their own, soft edges and inks on e-paper.
- [references/fonts-and-font-baking.md](references/fonts-and-font-baking.md): MICRO, baked fonts, FontBake and its
  ranges, every font and its cost, text functions, turned text, fade instead of "...", traps of text.
- [references/themes-and-palettes.md](references/themes-and-palettes.md): the five themes, each project's meanings, a
  sixth theme, a balanced palette, the weather's colours, night, the light map look, the paper inks, the ground.
- [references/animation.md](references/animation.md): time not frames, smoothing, easing, springs, pulses, the weather's
  motion, what must stay calm, the 360 turn.
- [references/layouts-by-screen-size.md](references/layouts-by-screen-size.md): sizes from macros, 320x240, 480x320,
  480x272, 240x240 round, e-paper, 800x480 to 1280x800 scaled, small things' sizes.
- [references/touch-ux-and-controls.md](references/touch-ux-and-controls.md): the touch state machine and its numbers, hit
  areas, gestures, keyboard, SET rows, toasts, BOOT, traps of touch.
- [references/first-run-flow.md](references/first-run-flow.md): power on, alignment, the screen-revision card, the demo
  sky, WiFi, the new-version note, ABOUT and its QR.
- [references/rgb-led-as-ux.md](references/rgb-led-as-ux.md): LED events, priorities, blinks, driver facts.
- [references/maps-on-a-small-screen.md](references/maps-on-a-small-screen.md): kinds not colours, class matching, names
  copied pixel for pixel, painting per look, outlines, solid cards, the flat looks.
- [references/e-paper-design.md](references/e-paper-design.md): 4 greys, inks, tones and dither, soft edges, the refresh
  policy, the key strip, WiFi Radar's and Flight Radar's paper screens, altitude shadows, the printed weather.
- [references/radial-dials.md](references/radial-dials.md): the radar scope layer by layer, planes and silhouettes, round
  HUDs, the thermostat dial, the 24-hour weather dial in colour with its column table, and printed.
- [references/data-screens.md](references/data-screens.md): data screens stand still, honest design, numbers and words,
  panels and lists, spectrum, hunt, walk map, graphs in colour and printed, weather pictures.
- [references/screens-catalog.md](references/screens-catalog.md): every page of the four projects with its numbers.
- [references/pc-preview-tool.md](references/pc-preview-tool.md): tools/preview, the session script, shots, proving a
  change, the WASM twin, tools/Guide, QrBake, agents in the loop.
- [references/films-and-stills.md](references/films-and-stills.md): the tour film, the finger, captions, a promo, first
  frame not black, how a longer film is assembled.

Code copies, verbatim (linked from the topic files):

- [code-gfx.md](references/code-gfx.md): the whole renderer. [code-frame.md](references/code-frame.md): the strip loop,
  DMA, two cores, dirty columns, timing. [code-fonts.md](references/code-fonts.md): font.c, the 3x5 table, FontBake.
- [code-scope.md](references/code-scope.md): the scope, silhouettes, beam, labels, the 360 lobe.
  [code-pages.md](references/code-pages.md): list, setup, spectrum, hunt, walk map, round views, ABOUT.
  [code-hud.md](references/code-hud.md): round HUDs, the thermostat dial, spring, night, page slide, plots.
- [code-paper.md](references/code-paper.md): WiFi Radar's e-paper packing, refresh policy and screens.
  [code-touch.md](references/code-touch.md): the touch state machine, alignment, keyboard, hit areas.
  [code-light.md](references/code-light.md): the LED. [code-tools.md](references/code-tools.md): preview, PNG writer,
  tour film, GIF, Guide, QrBake.

## Traps to read before anything else

- Work redone in every strip: a frame 62 ms slower once trails filled (frame-and-speed.md).
- Sending a strip while the one before is in flight: a hang for ever in `spi_device_acquire_bus` (frame-and-speed.md).
- Touch read once a frame: a swipe at 8 fps came out as a tap (touch-ux-and-controls.md).
- Springs stepped with a 200 ms frame: the numbers jumped (animation.md).
- A dot that circles where the direction is unknown: movement that is not there (animation.md).
- An ellipsis at 6 px reads as an underscore: fade instead (fonts-and-font-baking.md).
- Dithered text, or a grey that changes, on e-paper; a tint nobody can see (e-paper-design.md).
- A PC profile that never reached the board's real state (pc-preview-tool.md).
- A map picture whose width is not a multiple of 8 texels: a crash (maps-on-a-small-screen.md).
