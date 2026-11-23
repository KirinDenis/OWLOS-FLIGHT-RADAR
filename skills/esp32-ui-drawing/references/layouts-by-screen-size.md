# Layouts by screen size: from 240x240 round to 1280x800

One source for every screen: sizes come from macros, a new size is a line in config.h, not a new page. This file
is how each size is laid out and what each one taught. The pages themselves are in
[screens-catalog.md](screens-catalog.md). The boards behind each size are in
[esp32-boards](../../esp32-boards/SKILL.md) (one file per build, e.g.
[ili9341.md](../../esp32-boards/references/boards/ili9341.md)), their panels in
[esp32-lcd-touch](../../esp32-lcd-touch/SKILL.md).

How sure: (M) seen on a real board, (P) built and seen in the preview, (D) a contributor's or maker's notes, (R)
reasoned.

## One source, sizes from macros

`core/config.h`: `RADAR_DISPLAY` picks `GFX_W`, `GFX_H`, `RADAR_ROUND`, `RADAR_EPAPER`, `RADAR_HAS_TOUCH`,
`RADAR_HAS_LED`, and in Flight Radar also `RADAR_HAS_BUTTON` (a BOOT the firmware can read), `RADAR_HAS_DIM` (a
backlight that dims), `RADAR_RGB_BUS`, `GFX_SCALED` and `RADAR_OVERLAY`. Layout is macros from those, so a new
size is a new line, not a new page. When adding a size, keep the old ones pixel-identical in the preview shots:
compare before and after (pc-preview-tool.md).

- `#define LX(x) ((x) * GFX_W / 320)`: columns laid out on 320, spread on wider screens.
- `#define NEAR_N (6 + (GFX_H - 240) / NEAR_H)`: a taller screen lists more.
- `#define AY(small, tall) (GFX_H >= 320 ? (tall) : (small))`: a line's place on ABOUT, spaced out on the taller
  screen.
- `int dh = GFX_H - 240;`: extra height goes to the graph.
- `plane_size() = GFX_H / 240`. `demo_box` scale 5 at 320 high, 4 at 240.
- `#define PANEL_WIDE (PW >= 150)`: the panel's letters go with its width, not with a board. The 2.8"'s 91 px take
  the small ones and the pixel font for names of values; the wide panels (171 to 260 px: the 3.5", the RPi LCD, the
  4.3", the 5", the 7", the Tab5) take the next size up and more in a row.
- Every size switch in core/ that goes by height is `GFX_H >= 320` or `GFX_H < 300`: a screen under 320 rows takes
  the 2.8"'s choices.
- Prefer "the first of these that fits" to a size per board: a clock in LARGE if it fits between the look button
  and the dots, else MEDIUM; a date in full if it fits, else short; a value in MEDIUM if the row holds it, else
  SMALL. Then each board's build sizes itself from the same code (M on the 3.5" and 2.8" weather column).

## 320x240 and 480x320

Scope left, panel right, buttons low in the panel. The 2.8" CYD is the design size. 480x320 gets the same layout with
room: more rows, a bigger scope, more labels. Geometry from the height (Flight Radar's ui_int.h):

```c
#define SR   (GFX_H / 2 - 12)   /* the rim, ticks go in from it. 320: 108, 480: 148 */
#define SCX  (SR + 6)           /* 320: 114, 480: 154 */
#define SCY  (GFX_H / 2)
#define PX   (2 * SCX)          /* panel. 320: 228, 480: 308 */
#define PW   (GFX_W - PX - 1)   /* 320: 91, 480: 171 */
#define BTN_Y (GFX_H - 38)
#define BTN_H 32
#define BTN_W ((PW - 13) / 3)
```

- Flight Radar's flat maps take the whole screen and the panel slides over its right side from three dots, and by
  itself while a plane is picked.
- The radar's three dots take its panel away: the scope moves to the middle of the screen, a picked plane brings the
  panel back while it is picked. The WEATHER look keeps the same dots: without the panel its dial stands in the
  middle with now's numbers either side.
- The look button top right of the scope or of the square map (`LOOK_W` 62, 10 narrower in the radar's corner),
  the dots 24 wide (18 on the 2.8": its title needs the room).
- The weather's column (`WX_HUD`) is built for 480x320 and 320x240 (radial-dials.md): the 2.8" gets the same table
  with less in it (M on both).

## 480x272 (the 4.3" ESP32-4827S043)

A layout of its own, one pixel one pixel, no scaling. It takes the 2.8"'s choices where they go by the height (the
pages, the keyboard, the gear), as everything under 320 rows does. Its panel, 219 wide, takes the wide panels'
letters, in rows a little shorter. All the preview shots fit (P). On the RGB bus, 272 rows did not divide by the 5"'s
10-row bounce, and esp_lcd refuses a frame that does not: see
[bus-rgb.md](../../esp32-lcd-touch/references/bus-rgb.md).

## 240x240 round

The corners are not there, and the bezel eats a few pixels. Rules:

- Nothing in corners. Everything inside radius `GFX_W / 2 - 6`. Scope `SR = GFX_W / 2 - 12`. The disc is the whole
  screen, `PX = GFX_W`, `PW = 0`, `SCX = GFX_W / 2`.
- Pages keep to the square inside the circle: `PG_L 36`, `PG_R GFX_W - 36`. `HEAD_H 38`: the title sits low, where
  the glass is wide enough. Rows 32 px, no descriptions in setup rows.
- Text in a list is as wide as the glass is at that row. The narrower end of the row decides:

  ```c
  static int chord(int y, int margin)        // half width of the glass at row y, minus a margin
  {
      int dy = y - CY;
      int h2 = SAFE * SAFE - dy * dy;
      return h2 > 0 ? (int)sqrtf((float)h2) - margin : 0;
  }
  ```
- Labels: every corner on the glass, or it does not go there.
- Touch crosses on the diagonals at `0.2 * GFX_W`, not in corners.
- No panel: what it says comes in a card under home, or a row of buttons after a tap on nothing (5 s), or a note
  box.
- Navigation with no buttons (WiFi Radar): four views in a ring, swipe left for the next, a swipe is a mostly
  sideways move about a third of the glass long, the **top edge of the glass is back**, tap a dot to pick, tap the
  empty radar to open, hold to change colours.
- A closeness bar becomes an arc: 26 segments on 260 degrees round the edge, the number in the middle.
- No keyboard: too small. WiFi comes from the site over USB, and the demo card says so.
- Round views are their own functions (`round_main`, `round_target`, `round_list`): a circle is not a square with
  corners cut off.
- The weather's graphs keep inside the circle: the frame `SCX - 86` to `SCX + 86`, `SCY - 60` to `SCY + 62`, the
  title where the glass is wide enough, the two words of a title in the middle, 6 pictures not 12 and bigger, the
  glass's rim drawn as the dial has it (P, M at 7 to 9 fps).
- The dial's clouds are kept inside the sky ring, so an overcast does not run past the rim, which is the edge of
  the glass on the round (P).

## E-paper: 400x300 and 792x272

Its own screens, not the LCD's printed (e-paper-design.md). For the layout:

- `WIDE = GFX_W >= 2 * GFX_H`: the wide panel gets two columns of cards and a three-part hunt screen (WiFi Radar).
- Flight Radar keeps everything left of the keys' strip (`PAPER_KEYS_W` 28 px at the right edge).
- The printed weather dial is laid out for the 4.2" (300 high) and scales by `DL_SC(v) = v * GFX_H / 300`.
- The 5.79" has no map in Flight Radar: the map's texels stop at 480 wide (`MAP_W_MAX`) and 792 is more (P). It
  keeps the colour look printed for the weather; the printed weather is the 4.2"'s.

## Bigger glass: the same layout, scaled (GFX_SCALED)

The 5", 7" and 10.1" boards do not get a new layout. The 3.5"'s layout is put on the bigger glass at a fixed ratio,
everything half as big again or more, so the pages, the touch and every number in them go on as they are:

- 800x480 (CrowPanel Advance 5.0, Guition JC8048W550, CrowPanel 5.0 and 7.0, Waveshare 7): layout 534x320, 3/2.
- 1024x600 (Waveshare 7B): layout 547x320, 15/8.
- 1280x720 (M5Stack Tab5, its panel stands at 720x1280 and the driver turns it): layout 569x320, 9/4.
- 1280x800 (Guition JC8012P4A1C, its panel stands at 800x1280, the driver turns it): layout 512x320, 5/2 exactly.

How it is done: renderer-and-canvas.md (`GFX_Z`, `GFX_PX`, glass fonts). Status: the CrowPanel Advance 5.0 ran on a
tester's board (M, 3 fps on the radar at first); the Waveshare 7, 7B and the JC8012 run with chipguyhere's moving
layer (D); the rest are built only: "New: not run on one here yet".

## Sizes of small things

- A key of the on-screen keyboard: 32 x 35 px on 320x240, 48 x 53 on 480x320, drawn 4 px smaller (`KB_CW = GFX_W /
  10`, `KB_RH = (BAR_Y - 4 - KB_Y0) / 4`).
- A WiFi network row: 38 px from 320 rows up, 30 under.
- QR modules: 4 px on 320x240, 5 on 480x320, 3 on the round.
- The panel's nearest planes: rows 15 px. A list row 22 px. A setup row 40 px (32 on the round).
- Plane label budget: 6 on 320x240, 9 on 480x320, 4 on the round.
- Hit areas: touch-ux-and-controls.md.
