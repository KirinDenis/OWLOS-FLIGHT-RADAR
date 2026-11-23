# Radial dials: the scope, round HUDs, the thermostat, the 24-hour weather dial

The instruments of this family are round. This file is how each round instrument is built: the radar scope, Air
Quality's HUDs, Thermo Relay's dial, and Flight Radar's 24-hour weather dial in colour and printed, with the column
that hugs it. Code: [code-scope.md](code-scope.md) (the scope, silhouettes, blips, the lobe),
[code-hud.md](code-hud.md) (HUDs, the thermostat dial). The weather dial is in Flight Radar's `core/ui_weather.c`.

How sure: (M) seen on a real board, (P) built and seen in the preview or read in the code, (R) reasoned.

## Start from the instrument's own shape

A screen that is merely correct reads as a finished engineering task. Before laying a screen out, look at what the
product already is and push that further: the radar is radial, so its weather became a 24-hour dial, and its printed
weather became that dial redrawn for ink, not rows and axes. A linear meteogram was started and dropped for that
reason, and the printed dial was taken at once by the person who looked at it (a verdict, not a measurement). Show a
picture early, in the preview.

## The radar scope, layer by layer

One pass over the disc's pixels, in whole numbers. Per row, the radius and angle of a pixel depend only on `|dx|`,
so they are worked out once for both halves. Then per pixel:

```
p = gfx_mix(ground, glass, s_glass[r])          // dark in the middle, lighter to the ring: 70 + 100 t^3
st = s_ring[r16]                                // range ring, rim, glow outside it and inside the ring
if ((phi >> 3) & 1) st += s_dash[r16]           // quarter rings, dashed by the angle
ticks: k = nearest of 72 lines; cov = 192 - distance to that line; length 6, 4 or 2 px; alpha 240, 150, 90
cross: nearest of 8 lines, alpha 36 along the compass, 20 on the diagonals
p = gfx_add(p, gfx_scale(prim, st))
if behind the beam: p = gfx_add(p, gfx_scale(prim, trail_lut[behind]))   // 90 degrees of afterglow, 80 * f^2
near the beam's edge: p = gfx_add(p, gfx_scale(focus, 190 * cov))        // exact distance to the beam's line
```

Tables: `s_ring`, `s_dash` by radius in 1/16 px, `s_glass` by radius, `s_phi8` the angle in 1/256 of 90 degrees for
`dx <= dy` only, a byte a pixel. The other eighth of the quadrant is its mirror, so the table is `RQ * (RQ + 1) / 2`
bytes: 11.9 KB at 480x320. The angle only picks which tick is near, the exact distance comes from that line's sine and
cosine (72 lines, rebuilt when the map turns), so a tick stays sharp at the rim.

A map under the radar lights with the beam like ground echoes: maps-on-a-small-screen.md.

Light by radius makes the depth. Rings are not `gfx_ring` calls: a table by radius makes them free in the same pass.

WiFi Radar has the simpler version when nothing turns: rings, dashes, ticks and cross baked into one byte a pixel of
one quadrant (`build_static`), the other three mirror it (90 degrees holds a whole number of every tick and dash).
Use that when the instrument never rotates. It took the static layers from 22 ms a frame to almost nothing (M). When
the map may turn, the bake cannot hold: the light of what never turns stays a table by radius, ticks and cross come
from a distance to their line.

Geometry (Flight Radar): `SR = GFX_H / 2 - 12` the rim, `RL = SR - 12` the compass letters in the band between rim
and ring, `PR = SR - 20` the range ring (where `range_km` lands), `RQ = SR + 6` the disc and its glow. Around it
(square screens): four corner brackets, a hatched bar top and bottom, top left the town and how the sky is (`ADSB.LOL
3S`, `NO WIFI` in the alarm colour), top right the look button and `NORTH UP` or `UP 090*`, bottom left the height
legend, bottom right the range and how the fingers work it (`DRAG: ZOOM`, `RIM: TURN`). Compass letters N E S W in the
smooth font (N in focus), numbers 30, 60.. in the pixel font, ring values on a diagonal (on a solid card when a map is
under them).

**The sweep.** The beam is pure time: `u->beam = (now % 6000) * 0.06` degrees, a turn in 6 s (WiFi Radar: 4 s). It
belongs to the radar: the weather dial has none (data-screens.md).

**Home.** On the RADAR look a small ring under the planes, not a pin: the scope's own rings and cross say where home is,
and a pin turns a radar into a map (a user's letter, M). On the map looks a pin, its tip on home, over the planes, in
the theme's `home` colour with a dark edge (head radius, side and tip to head's middle a 5 12 13 triangle); a small
white ring got lost among white roads and white planes (a person's photo, M).

### Planes, back to front, farthest first so the nearest lies on top

1. Trail: 16 old positions, in 10 m units, older is fainter: alpha `40 + 120 * (t + 1) / (tn + 1)`, a `gfx_dot` at each
   point and a line at half that, a last line to the plane at 80. A point off the scope breaks the trail. Placed on the
   screen once a frame (frame-and-speed.md).
2. Picked: a line from home at 60, a line ahead showing where it will be in a minute, a glow.
3. Glow in the height colour. Phosphor: alpha `70 + 100 * (1 - since / 360)`, brighter just after the beam crossed.
4. Silhouette, turned by track (and by the scope's up), colour slightly toward `focus` (`gfx_mix(col, C_FOCUS, 60)`).
5. New: a ring opens from 5 to 21 px over 1.6 s.
6. Picked: four arcs of 40 degrees turning round it, once in 3 s.
7. Labels, on top of every plane: a dark card (solid over a map), a bar of the plane's colour, name, height, a thin line
   to the plane.

Outside the range but near: a dart past the ring, up to 1.6 x the range. Optional speed vectors (SET, RADAR): a
minute's flight at 50 km long, the same length at every zoom (`VECTOR_PX = PR / 1620`), clipped at the ring.

**Silhouettes.** Seen from above, nose up, in units of the size, a few convex parts each, so `gfx_poly` draws them with
one smooth edge. Ten kinds from the ADS-B emitter category: JET, HEAVY (the same at 11.5), BIZJET (T-tail, engines at
the tail), LIGHT (straight wing), FAST (delta), HELI (a rotor ring), GLIDER, DRONE (an X), BALLOON (a disc), GROUND (a
box). All at `size = GFX_H / 240`.

```c
static const part_t JET[] = {
    { 7, { 0, -1.0f, 0.10f, -0.82f, 0.10f, 0.80f, 0.05f, 0.98f, -0.05f, 0.98f, -0.10f, 0.80f, -0.10f, -0.82f } },
    { 4, { -0.08f, -0.22f, -0.98f, 0.26f, -0.98f, 0.38f, -0.08f, 0.12f } },   /* wings */
    { 4, { 0.08f, -0.22f, 0.98f, 0.26f, 0.98f, 0.38f, 0.08f, 0.12f } },
    { 4, { -0.06f, 0.64f, -0.40f, 0.88f, -0.40f, 0.98f, -0.06f, 0.86f } },    /* tail */
    { 4, { 0.06f, 0.64f, 0.40f, 0.88f, 0.40f, 0.98f, 0.06f, 0.86f } },
};
/* turned: */
xy[2 * k]     = x + (px * ch - py * sn) * s;
xy[2 * k + 1] = y + (px * sn + py * ch) * s;
```

The rest of the shapes: code-scope.md. The same `silhouette()` draws the small plane in each list row, and the printed
radar's planes and their shadows.

**Height colours.** `C_DIM` on the ground or unknown, `C_LOW` under 10 000 ft, `C_PRIM` up to 25 000, `C_FOCUS` above.
The legend sits in a corner: four dots, `GND <10K <25K HIGH` (themes-and-palettes.md).

**Blips (WiFi Radar).** Distance from the centre is signal, not metres (`rssi_frac`, -28 dBm near, -92 far, dashed rings
at -40 -55 -70 -85). A solid glowing dot: the direction is known, with an arc showing how unsure it is (`b->ang - b->sd
.. b->ang + b->sd`). A hollow ring: the direction is not known yet; it stays on its ring at a fixed place. An extra thin
ring: placed by the walk map. Brightness follows the beam like phosphor: `0.35 + 0.65 * f * f`. Not heard for 8 s: fades
over 30 s to 25 %.

**Labels.** Placed once a frame. The picked one first, then the nearest. Each tries six places round its plane and takes
the first that covers nothing: other labels, planes, ring numbers, corner text. Budget 6 on 320x240, 9 on 480x320, 4 on
round. On round every corner of the label must be on the glass. What gets no place gets no label: never overlap.

```c
const int cand[6][2] = { { 8, -9 }, { -8 - w, -9 }, { 5, -LABEL_H - 5 }, { 5, 6 }, { -5 - w, -LABEL_H - 5 }, { -5 - w, 6 } };
```

Trap, a plane's own box blocks its own label (P): the placer puts every plane in `used[]`, so the one being labelled
must be skipped or it finds no place. Compare the box, `o->x == sp->x - 6 && ...`. A plane's shadow on paper is not
exempt: labels keep off it.

## Round HUDs (Air Quality, core/ui_hud.c)

Built from `gfx_sector`, outside in: a bezel with a halo and a glint running round it, ticks on a 270 degree scale from
lower left over the top (`hud_deg(f) = -135 + 270 f`), thin zones in the level colours, the value as a comet arc (dim at
its start, bright at its head, on a glow, the head breathes), inner rings for more channels, a dashed ring turning the
other way, the number and the level in words in the centre. A warming sensor shows a dashed arc filling with the time.
No sensor: says so, and which part to plug in where. A bad value: the halo pulses. The barometer's needle is now, its
arc is where it came from in 3 hours. Geometry on 480x320: `HUD_R 102`, centres `HUD_AX 122` and `HUD_PX 358`, `HUD_Y
144` (screens-catalog.md).

## The thermostat dial (Thermo Relay)

A 270 degree scale, 20 C wide, that stays put while the target moves in its middle 60 % (the numbers do not slide under
the finger) and jumps only near an end. White notch and pointer = target, soft band = hysteresis, glowing dot with a
white heart = the room: the boldest mark, it is the one that moves. While the relay runs an orange or blue arc from the
room to where it will stop, with dots of light flowing along it at 12 degrees a second. In the middle the temperature:
integer part in FONT_TEMP (85 px), the degree sign up like a superscript, the decimal on the big digits' baseline.
Under it the trend or `steady`. Under the ring what the relay does and why, in two lines. Code: code-hud.md.

## The 24-hour weather dial, in colour (Flight Radar's WEATHER look)

A clock of the coming day on the scope: noon at the top, midnight at the bottom, the day clockwise, a hand at now. All
of it there at once, no beam: the data, not the show. What moves is the weather (animation.md). Measured speeds in
frame-and-speed.md.

Radii (layout pixels): sky ring from `SR - 27` to `SR - 3`, the temperature loop between `0.30 SR` and `SR - 46`, the
core `0.25 SR`.

The pass (`dial_pass`, a pixel at a time on the glass, whole numbers as the scope's: radii in 1/16 glass px, angles in
quarter degrees, an arc tangent table of 257 entries):

- The glass: dark in the middle, lighter out to the ring (`30 + 170 t^3`); the core darker still for the digits.
- The ring of sky: each degree in the colour the sky will have at that hour, from the sun's height worked out on the
  board for home (themes-and-palettes.md for the stops), soft-edged, alpha 242.
- The temperature: a loop in the colours of how warm (`temp_colour`), nearer the rim the warmer, filled faintly inside
  (34) with its line at 240. A flat day stays a ring, not a scribble: the scale spans at least 4 C.
- The sky's inner edge and the rim with its glow outside, added in `prim`.

The marks over it (planned once a frame into arrays, drawn by each strip they reach):

- Stars in the night part of the ring (60, the sun 8 degrees under), twinkling; a few brighter ones as discs.
- A tick every hour on the ring's inner edge, longer with its number (pixel font) every 6.
- Sunrise and sunset: a tick across the ring in `DAWN_C` where the sun crosses the horizon.
- Clouds: a cluster of five puffs an hour on the ring, as wide as the sky is covered (`band * (0.45 + 0.62 *
  cover)`), the middle puff biggest, a little in and out of line; an overcast's run into one fluffy band. Opaque:
  puffs over puffs would darken where they meet. Every cloud's edge first (the colour darker, 1.5 px wider), then every
  fill, so neighbours run into one band with no seams. Grey with rain. Kept inside the ring: an overcast does not run
  past the rim, which on the round screen is the glass's edge (P). These are the printed dial's clouds brought to colour:
  "the great HUD with the clouds from the black and white".
- Rain: a bar inside the ring at that hour (2.6 px, `3 + 8 * mm` long up to 16) and drops falling through the ring,
  slanted by the wind; snow drifting.
- Now: a hand from the core to the ring (a 4.5 px glow at 46 and a 1.7 px line at 240, in `focus`), the sun or the moon
  in its real phase on the ring at its tip.
- Every 3 hours: the wind as an arrow on the loop (amber at gusts of 50 or more), the temperature's number outside it
  in its colour.
- Wind now: streaks drifting in the glass.

The core: the sky's picture, the temperature in HUGE (LARGE on a small core) in its colour with the degree as a ring,
and under them the home's clock (not on boards with the column table, where the clock is the column's).

The dial's tables (sky and loop by the degree, glass by radius, arc tangent) are made once a minute and for each new
forecast, taken while the look is up and given back when it goes (P).

### Without the panel: numbers either side

The radar's three dots take the panel away and the dial stands in the middle. Now's numbers then stand either side,
four a side, a golden step apart (2.618 times an item's height), the longest words in the corners where the circle
leaves most room: on the left how it is to be out in (how it feels, the damp, the rain, the UV), on the right what the
air does (the wind, the pressure, the radiation or the sun's next rise or set, the air). What each is in the pixel font
over its value, the unit small after it, the air's and the UV's scale bar under (`scale_bar`: the step now lit and
taller). A warning SET leaves on stands in a chamfered box that breathes round its number (P, M on the CYD).

### With the panel's room: the column as one table (WX_HUD, M on the 3.5" and the 2.8")

On 480x320 and 320x240 (`WX_HUD` in ui_int.h lists the displays) the panel is replaced on the dial page by the printed
dial's column, in colour (`wx_hud` in ui_weather.c). It came from the request of the person looking at the boards: take what was great on colour (the
dawn, day and dusk, the rain's animation) and the great HUD with the clouds from the black and white; then "balance the
palette"; then "make it one picture": the clock and the date were lost, the rows good, two indicators to align, the
days to spread like the rows.

- No frame. Each row starts where the dial's circle lets it (`hud_x(y0, y1)`: the circle `SR + 6` across those rows,
  plus 6), its value against the right edge.
- The clock in FONT_LARGE centred between the look button and the three dots (MEDIUM if it does not fit there), the
  full date under it right-aligned (`TUESDAY 18 NOVEMBER 2025`, short `TUE 18 NOV 2025` where it does not fit), under the
  buttons' row so it does not run under the dots.
- The sky in words (MEDIUM), then rows: FEELS LIKE, HUMIDITY, AIR PM2.5 with its word (or UV), RADIATION (or the sun's
  next rise or set while there is no source). Values in `focus`, names in `dim`.
- Warnings SET leaves on, in their colour, no box: what (against the circle), then how much and when (against the right
  edge). The pressure's warning is the barometer's.
- The barometer and the wind rose side by side, their pictures standing on one line (the rose's base is the
  barometer's hub), their words on the same two rows under.
- The next days as a table at the foot over the buttons: the short day name against the circle, the sky's picture in a
  column, the high right-aligned in a column, the low against the right edge in `dim`. Rows worked out from the foot up,
  the indicators get what is between.
- HUE LIST SET buttons where the panel has them, the three dots over the corner.
- The palette: the theme's colours and one accent kept for warnings (themes-and-palettes.md).

The barometer (`hud_baro`): a half dial, its scale fixed as a wall barometer's (960 to 1060 hPa, 28.5 to 31.5 inHg, 80
mmHg round today's own); a band outside the arc from the theme's dim (a storm's end) to bright (a fair day's), soft-edged,
a pixel at a time; a tick every tenth, the ends and the middle long; the ends' numbers inside by the hub (only from a
radius of 26 up); a triangle outside where the forecast takes it (the warning's hour, or the furthest it goes), in the
warning's colour if warned; a thin hand where it stood a day ago; the needle now with a glow and a hub. Under the hub
the value, its unit small and its way (an arrow), then `-11 BY 01:00` or `STEADY` in the warning's colour or dim.

The rose (`hud_rose`): a petal for each eighth of the compass the next 24 hours blow from, as long as their share, in
`prim` and in amber or red only where it would warn (50 and 75 km/h); a ring, ticks, north a mark on the ring with no
letter over it; the needle now the way it blows (downwind) with a glow. Under it `SW 20-32` (gusts when 10 over the
wind) in MEDIUM and the unit in SMALL.

The 2.8" (`small = GFX_H < 300`): the same table with less in it. The clock MEDIUM when LARGE does not fit, the date
under the buttons' row and short, the names short (FEELS, AIR, the dose unit for radiation), the wind a row (no room for
the rose), the barometer alone in the column's middle at radius 20 without its end numbers, rows 2 px apart, no days (no
room, as the old 2.8" panel had none). One build per board already; the same code sizes itself.

## The 24-hour dial printed (the 4.2" e-paper, M)

The colour screens' dial redrawn for ink, the first weather page on paper (paper style 1). Inks and soft edges as in
e-paper-design.md.

Radii by the golden section, laid out on 300 rows and scaled (`DL_SC(v) = v * GFX_H / 300`): centre x 140, the sky's
outer edge 122 (the hours' numbers round it), its inner edge 93 (0.618 of the way out from the core), the core 47 (0.382
of the dial), the temperature from 55 (coldest) to 82 (warmest), the clouds' middle 108.

- The ground, a pixel at a time: the sky ring white by day, a tint in the twilight (sun between -0.833 and -6
  degrees), light grey by night; inside the ring the same over the loop and a step darker under it. So the night is a
  grey wedge from the core to the rim.
- The sun where it is highest each day: a ring with 8 rays in the ring, under the clouds.
- Clouds round the ring: three puffs an hour as big as the cover (rain means at least 80 %), drawn grown in dark grey
  first, the edge, then filled light grey (white on the night's grey).
- Rain out of the clouds: strokes falling inward, as many as it rains and as long as the square root of how much;
  drizzle and snow dotted along them; thunder a bolt in the ring. The most rain said on its cloud on a white patch
  (`6.0 MM`).
- The loops: yesterday's dotted dark grey and thin, the coming day's black 2.4 px on a white edge 5.2 px, a point a
  degree, soft.
- Every 3 hours by the home's clock: the wind outside the loop, an arrow the way it blows (north up), as long as it is
  strong, bold where it gusts to 50 km/h. The temperature every 6 hours and at the day's high and low, inside the loop
  on white (outside where the core is too near); a six hours' number next to an end gives way to the end's. A storm's
  worst gust inverted where it blows.
- The rim: a tick every hour, the hours every three as numbers (no tick beside a number: a tick before 18 read as
  "-18").
- The hand: a thin tapered needle (a polygon 3.6 px wide at the core, a point at the tip) from the core to the ticks,
  white-edged over the greys. A hand points, it does not shout. A dot where it crosses the loop. The hand is the boldest
  mark that moves.
- The core: a white disc with a ring; now as big as the circle holds it (one digit in XXL with a ring as its degree,
  more in XL), FEELS under it.
- The clock in XL from the top of the glass, right of the "12", its digits ending over the rim; the weekday after it in
  M (S if long), the date and the year under the day. It changes every minute on its own: crisp, drawn on top.
- The column right of the dial, its left edge the circle's (`dl_free_x`: the rim plus the 15, 18, 21 and 00 numbers,
  then 6 px): the sky in words, an aneroid barometer, the short row (the dose, the air or the UV) by the "18" where a
  row is shortest, the sun's next rise or set, the humidity, then the next days stepping out with the circle (a
  staircase: the day, its sky, its high, its low in dark grey, spread down to the glass's foot, 19 to 24 px apart).
- The barometer printed: a half dial of radius 42 under the sky's words, where the dial has curved away; scale fixed as
  above; a band outside dithered from dark (low) to white (high); ticks inward on white so they show on the dark end;
  the end numbers inside by the hub; the forecast's mark a white-edged triangle outside; a day ago a thin dark hand (the
  set hand an owner turns to remember a reading); now a needle as the dial's. Under the hub the value and its way, then
  `-11 BY 01:00`, inverted when red. The hands' spread is the change, the mark the change to come.
- The wind rose in the bottom right corner the days leave: petals by share, their tone by how hard (a dither), a ring,
  ticks, N, the needle now, `SW 20-32 KM/H` at the foot.
- Corners: the place top left, `OPEN-METEO hh:mm` bottom left (its licence asks for the name), and the loops' key:
  a solid line NEXT 24 H, a dotted one YESTERDAY.
- Key strip tabs on this page are only the signs (the words come when pressed).

On the board: one 4-grey print, then only the clock (37 px, debt 0); paging up and down prints and comes back (M).

## What the first take taught

The printed dial was first brought to the colour screens literally, through palette roles (themes-and-palettes.md), as
a sheet beside the dial. It was taken out: "not so literal". The colour dial kept its sky colours, stars, wind streaks,
rain and snow, sun and moon, and took from the printed one its puff clouds and its column. Bring what a design does well,
not its literal picture.
