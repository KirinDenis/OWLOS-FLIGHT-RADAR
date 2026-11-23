# E-paper design

How to design for 4-grey e-paper: what the glass allows, the two ways of drawing for it, tones, soft edges, the
refresh, keys instead of touch, and the screens that came out of it. Two projects taught it: OWLOS WiFi Radar on the
5.79" 792x272 (M), and OWLOS Flight Radar on the 4.2" 400x300 (M), where the newest rules come from. Code: WiFi
Radar's paper.c, ui_paper.c and the refresh policy in [code-paper.md](code-paper.md); Flight Radar's are in its
`core/paper.c`, `core/ui_paper.c` and the paper part of `core/ui_weather.c`. The panels, their waveforms and drivers:
[ssd1683.md](../../esp32-lcd-touch/references/epaper/ssd1683.md); the packing and the print decisions on the firmware
side: [paper-screen.md](../../esp32-lcd-touch/references/epaper/paper-screen.md); the keys and the dial:
[buttons-keys.md](../../esp32-lcd-touch/references/buttons-keys.md). The boards:
[epd400.md](../../esp32-boards/references/boards/epd400.md),
[epd792.md](../../esp32-boards/references/boards/epd792.md).

How sure: (M) seen on the real panel, (P) built and seen in the preview, (R) reasoned.

## What the glass allows

- **4 greys.** A tone in between is a pattern of two of them, as engraved maps and newspapers made one.
- **What changes is drawn on top with a partial refresh, which knows black and white only.** Pixels not touched keep
  their grey; a grey drawn by a partial is not there till the next full refresh. So grey only where nothing changes
  till the next full refresh; everything that changes is black on white.
- **No light of its own.** Glow prints as grey fuzz, dust specks as dirt, half-faded dots as noise, a grid as dots, a
  frozen beam means nothing, so the beam shows only while the 360 turn runs. On paper all of them are off (`if
  (RADAR_EPAPER)` at each). No easing either: a frame is seconds.
- **Slow.** A full 4-grey refresh is 3.1 to 3.3 s and flashes; a partial 1.86 s on the 5.79" (M), a pressed key's
  partial about 1.2 s on the 4.2" (M).
- **Less contrast than the preview.** On the glass the greys read weaker: text black, dark grey only for what is
  second, nothing read in light grey (M).

Rejected first: the LCD UI in 1 bit ("tiny, no greys", M). The drawing must be its own for these displays.

## Two ways of drawing for paper

1. **The LCD's light printed as its negative** (WiFi Radar; Flight Radar's colour looks on paper). The UI draws light on
   dark as usual, with one PAPER theme, and `paper_pack` prints the negative, so theme and shapes are shared. Below
   light 14 is clean white paper.
   - WiFi Radar: every pixel to the NEAREST of the panel's 4 greys, no dither. Dithered edges printed as fuzz, and
     dots that differ between frames spoil the partial refresh. Font AA stays: soft edges round to the two greys.
   - Flight Radar: by STEPS, not the nearest: light below 36 white, below 80 light grey, below 150 dark grey, the rest
     black. The nearest put the radar's dim words (light about 100) on the light grey, and the 4.2"'s light grey was all
     but white: a third of the words were gone on the first run (M). Now the faint ground, its grid and the panels'
     shading stay white, rings and quiet marks go light grey, dim words dark grey, the rest black. These steps were
     set for the old greys and not checked again after the 4.2"'s greys moved.
2. **In inks** (Flight Radar's own paper screens: CLASSIC, RADAR, the weather). Drawn in `P_WHITE`, `P_LIGHT`,
   `P_DARK`, `P_BLACK`, a colour channel each, and `paper.c` reads which channel is over half (black from 90 of 255).
   `paper_inks` says per frame which way it is. A soft edge rounds to one of the two inks it lies between, never to a
   grey neither is: the gfx and font AA come out crisp, and what moves over a grey map stays black and white, which the
   partial refresh can draw (M, every partial since had grey debt 0).

The paper screens are the drawing's own, not the colour radar in greys. Start a paper screen in inks.

## The panel's greys

- 5.79" 792x272: 0, 60, 140, 255 (measured in ViewOwl, M).
- 4.2" 400x300 with its stock waveform: 0, 50, 220, 255 (ViewOwl, M): the light grey was all but invisible. Flight
  Radar changed the grey waveform (the panel file has how) and photographed test bands: 0, 95, 185, 255, four clear
  tones (M). `PAPER_DARK`, `PAPER_LIGHT` in paper.h.
- Drawing with exactly these gives a clean flat grey. The preview shows the panel's real greys.

## Tones between the greys

A 1-pixel pattern of two inks reads as its mean on the glass, and takes a partial refresh well: what passes over it
leaves white and black, the same tone (M).

- Chequers of two inks (Flight Radar, 4.2" greys): TINT white and light, 220. MID light and dark, 140. DEEP dark and
  black, 47. HALF black and white. Hatch: lines down to the right, a quarter of ink (`((x - y) & 3) == 0`). Dash: 4 on,
  4 off along the diagonal. WOOD: MID with a black tree every 4 x 4. TOWN: the hatch on light grey.
- **A step must be a whole grey.** A tint (220) or sparse dots are invisible on the glass: the first try at more tones
  differed from the old sheet in only 7 to 11 thousand of 120 thousand pixels and nobody could see it. The tones that
  worked are a step apart (M).
- **Gradients by ordered dither** (`paper_tone(g, x, y)`, g 0 black to 1 white): a 4x4 Bayer dither between the two
  inks either side of g. The glass blurs the 1 px pattern into the tone between. Used for a band that fades: the
  printed radar's rim band (mid grey at the edge fading out over 10 px, eased), the barometer's storm-to-fair band, the
  wind rose's petals by strength (M).
- WiFi Radar's halftone fills, like a printed graph: a grey the panel lacks is made of its two nearest greys in an 8x8
  Bayer pattern at draw time, so `paper.c` only ever sees the panel's own levels (`halftone()`, code-paper.md). Only
  fills get the pattern, text and lines stay clean (M).
- What changed against the older rule "no dither": dither and patterns are fine in fills and bands that are printed
  whole and do not change between full prints. Text, lines and anything drawn on top stay undithered.

## Soft edges on what is printed whole

`canvas_t.soft` marks a layer printed whole with a full refresh: its letters, lines and rounds get edges in grey
steps (`paper_soft`, `paper_line`, `paper_disc`, `paper_ring`). Off for whatever is drawn on top between prints:
clocks, a pressed key's third, planes and labels. Details: anti-aliasing-and-soft-edges.md (M).

## The refresh policy

- **Full 4-grey refresh** (it flashes) for a new screen, every 10 min, or when the grey debt passes 8000 px, at most
  every 3 min. The driver keeps a 2-bit model of the glass in PSRAM to count that debt: how many pixels show a grey
  that is wrong (code-paper.md).
- **Partial** for the rest: a new picture every 5 s, and if no pixel changed the glass is not touched.
- The soft edges of changing digits alone make 2000 to 4000 debt a minute: they do not matter, they turn crisp (M).
- Labels over light grey woods add 150 to 300 px of debt a frame; the full refresh by debt follows, 3 min apart (M).
- **A new screen is printed whole once** (Flight Radar `screen_news`): another look, view, weather page, range or
  sheet style. A new screen waits for its map up to 8 s and is printed once with it, not twice. While it waits, the
  firmware draws and sends nothing (`paper_hold`): the old screen stays on the glass. A white sheet drawn on top
  first was a long refresh for nothing (M). Without this, a look's greys were missing for minutes.
- The sheet's map made again (another range) is news; a map made for a screen that does not show it is not.
- The weather is printed whole when its forecast changes and when its time step turns (the dial each quarter hour,
  the other pages each hour, `weather_stamp`). Between, only the clock changes: 37 px a minute, debt 0 (M).
- A range just turned keeps the old map till the new one is made: printed stretched it would be a wrong map.

## Keys instead of touch

Paper boards have keys: MENU, EXIT and a dial (up, press, down). How they show:

- **The keys' strip** at the edge they are on (Flight Radar, `PAPER_KEYS_W` 28 px, light grey): three equal thirds,
  from the top a key, the dial, a key, each third in front of its key. In each, turned a quarter to read downwards
  (`font_text_down`), what pressing it does NOW: the next look's name, NEARER, FURTHER, PLANE or NEXT, LET GO, the
  weather page before or after; held actions on a second line (`HOLD: ABOUT`, `HOLD: WATCH`); the dial's three lines
  9 px apart, each with its sign (a triangle up, a dot, a triangle down). A key that does nothing here says nothing
  (M, 4.2"). Every paper screen keeps left of the strip.
- Which physical key is which is not in any doc found (the maker's wiki, pinouts): the keys are on the right edge of
  the screen as drawn, top to bottom a key, the dial, a key; which is MENU and which EXIT is still a guess (MENU top).
- **A press shows at once**: the next picture prints that key's third black with white words (a partial), then the
  key is taken and the screen printed whole.
- **Presses gathered** (UP, DOWN, MENU, and EXIT with nothing picked): each press moves the target, its third shows the
  target in white on black (`50 KM`, the page's name, the look's name), and after 2 s of quiet one print. Back where it
  was: no print at all. So turning the dial twice is one long refresh, not two (M).
- WiFi Radar (5.79"): MENU next screen, hold MENU the 360 turn, BACK on the main screen the next network, elsewhere
  back. The dial: up and down pick, press opens, hold starts the turn. `key_hints` draws what the keys do where the
  buttons would be (M).

## WiFi Radar's paper screens (5.79", M)

- **Few things, big.** Six networks on cards, read from a metre, not a table of twenty. Names 20 px, numbers 34 px,
  the target's level 63 px. DejaVu Sans Bold.
- **Grey only where nothing ever changes** (bands, shadows, the radar's ring). What changes on the grey header sits
  in a white box.
- **Numbered cards and dots instead of labels.** The radar has no words: each dot carries the number of its card, in
  a black badge, hollow when the direction is unknown. A legend reads better than words squeezed between dots. Two
  routers in one box sit on the same spot, so a badge that would cover another moves aside (`spread_badges`) with a
  thin line back to its real place.
- **Scales fit the data**: `db_range`, 10 dB steps. With a fixed -30..-95 dB scale everything lay on the bottom.
- The wide panel gets two columns of cards and a three-part hunt screen (`WIDE = GFX_W >= 2 * GFX_H`).
- Screens: `paper_main` (radar with numbered badges and 6 cards), `paper_target` (the hunt, level in FONT_PAPER_XXL 63
  px), `paper_list` (pages, two columns on the wide one), `paper_spectrum` (humps, halftone under them, `CROWDED CH 6:
  5`), `paper_map` (numbered like the cards, legend with distances). The 4.2" version of them is built and
  preview-checked only (P).

## Flight Radar's paper screens (4.2", M)

Two layers on every one: the sheet, printed whole in greys and patterns, and over it what moves, black on white.

### The sheet (CLASSIC, the printed chart)

The map's kinds as a printed chart. The hierarchy: motorways, runways and names black; main roads DEEP (darker than the
woods they cross); small roads dotted (HALF) only near (25 km and in), further out a light net or nothing; rails and
borders dashed; fields white paper. Areas go by their neighbours, as a printed map has only areas, no specks: the
tiles have soft edges and thin things the texels cut in pieces, which printed as dust (`around()` counts the kinds of
the eight texels about one; off the sheet counts).

- Style 1, the default, each kind a step apart: water flat DARK where 4 or more of 8 neighbours are water (and its
  creeks near, 10 km and in); woods WOOD where 6 or more are wood, LIGHT at 4 or 5, paper else; parks and meadows
  LIGHT where 6 or more; towns TOWN (hatch on light) where 6 or more of 8 are town, their buildings DEEP near; elsewhere
  paper.
- Style 0, the sheet before: water LIGHT in the middle (all 8), DARK as a bank (4 or more), a dotted creek near; woods
  LIGHT where 6 or more (the old charts tinted forests, and the hills are woods, so the tint shows the country's
  shape); towns hatched, buildings HALF near; parks paper. The water's light grey has a dark bank, the woods' none: the
  two do not mix.
- Far out the tiles' towns are specks between the fields and their "buildings" are road casings: those stay paper
  (measured per class with a paper-only debug build).
- The person who looked at the glass picked the bolder tones of style 1 (M). The console can switch styles (`OWLOS
  PAPER 0|1`, RAM only).

### Over the sheet

- Range rings dashed (a dash of 3 degrees every 6) at the range and half, their numbers on white.
- Home: a circle with a dot on a white disc, the chart's sign for where you stand.
- Planes black, with a white silhouette 1.5 x under each, 10 % bigger than on the colour screens (no glow to find them
  by). Off the sheet: a dart at its edge.
- Where it was: 16 dots, the older the smaller. Which way and how fast: a line a minute's flight at 50 km long, the
  same length at every range (`PR / 1620` px a knot).
- A guessed position (no answer for 12 s, 32 s on FR24) is hollow. The picked plane gets a ring.
- Labels: white cards, black words; the picked one and emergencies inverted; a watched plane's card framed; a line to
  the plane when the card stands off. Placed once a frame, picked first, then emergencies, then the nearest, 8 at most,
  each where it covers nothing (cards, tabs, rings' numbers, home, planes and their shadows).
- Cards: home top left (the place, how the sky is, `MAP COMING` or `MAP NEEDS WIFI`), the picked plane bottom left (its
  name in L, type, height, speed, distance, the squawk's meaning or the route), the OSM chip, a framed toast.

### The radar, printed

The radar look redrawn for ink, the scope where the radar's own is (so the scope's planning places the planes):

- Inside the scope the CLASSIC sheet's own tones, turned and scaled as the radar's disc. A first try invented fainter
  tones: the map was almost invisible, worse than the colour look printed. Reuse what was already approved before
  inventing a variant (M).
- Round it a band, a dithered gradient from mid grey at the rim fading to paper over 10 px; where soft cannot step
  from paper to light grey, a chequer.
- The rim solid, a tick every 10 degrees, every 30 longer with the bearing (`030`), N E S W bold in M, turned as the
  scope is.
- **Height as shadows**: each plane casts a chequer copy of itself down and to the right, as far off as it is high (2
  px plus 8 at 40 000 ft; on the ground none; unknown as high), on its own white ground (the map's halftones hid it).
  It is drawn as a marker colour that is then turned into dots row by row (a strip has no neighbour rows to look at).
  Filling the silhouette's inside by height was tried first: the wings are 2 px and it read as a broken plane. Labels
  keep off the shadows (M).
- Right of the scope, a column whose left edge follows the circle (`rd_free_x`: past the ticks and the bearings' words):
  the count `N PLANES IN x KM` in XL over the top right; the height key as swatches (a square and its shadow off by the
  class, 0 to 3 px: HIGH, <25K, <10K, GND, or the metric words); then the nearest planes, a name and under it the
  height and how far, as many as the glass holds, picked and emergencies inverted. Labels on the scope keep left of the
  column.
- The place top left, the feed and the OSM credit bottom left.
- Planes and the list are drawn on top every 5 s: 3 to 5 thousand px a picture (M).

### The weather, printed

All three weather pages on the 4.2" are the paper's own (`PAPER_WX`); the 5.79" keeps the colour look printed. The
24-hour dial printed (style 1, the first page) is in radial-dials.md. The graphs printed are in data-screens.md. Style
0 keeps a card of words: now big on the left (a picture, the temperature in XXL with its degree a ring, the sky in
words, how it feels), the clock in XL right of it with the weekday, the date and the year (short forms when a 2-digit
temperature leaves no room), two columns of three rows (wind, pressure with its way, humidity; rain, sunrise or
radiation, air or UV), the worst warning (red inverted, amber framed), the next day in 8 columns of 3 hours with the
nights light grey, and the next 4 days with the lows dark grey.

Rules the printed weather taught (M):

- Halftones and soft lines; no LCD frames (chamfered boxes) on ink: the paper is the ground.
- Fluffy clouds, never black: a cloud is light grey with a black edge (white on a grey night), 3 or 4 puffs on a base
  with round ends (`pcloud`). The colour screens' three discs on a box read on paper as a blob.
- The golden section for the layout (data-screens.md, radial-dials.md).
- Labels and numbers on white patches with no frame.

### What is not done

Other looks than CLASSIC, RADAR and WEATHER still go the colour way on paper. The MAP look is skipped on paper (CLASSIC
is the map there). Weather warnings and the new-version note on the paper's own screens are not done.

## Traps of e-paper

- **The LCD UI in 1 bit (M).** Tiny, no greys. Rejected on sight.
- **Dithered text (M).** Text and thin lines printed as fuzz, dots that differ between frames spoil the partial. No
  dither on text and lines.
- **Everything lay on the bottom (M).** A fixed dB scale; fit it in 10 dB steps.
- **Grey changes need a full refresh (M).** Grey only for what does not change; a white box for what does, even on a
  grey band; count the debt.
- **What only light can do (M).** Glow, dust, half-faded dots, a grid, a frozen beam: off on paper.
- **Dim words gone on a near-white grey (M).** Map light to inks by steps, not to the nearest grey.
- **A tint nobody can see (M).** Steps a whole grey apart.
- **A moving hand through greys (M).** The dial's now rounded to a quarter hour, the page printed each quarter.
- **A key sent while a refresh runs (M).** On the console the key slot is single: keys sent during a 6 s refresh
  overwrite each other.
