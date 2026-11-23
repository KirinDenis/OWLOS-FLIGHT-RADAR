# Data screens: panels, lists, graphs, and honest numbers

Screens people read for their numbers: the panel beside the scope, lists, the spectrum, the hunt, the walk map,
the weather's graphs, and the rules that keep them honest. Code: [code-pages.md](code-pages.md) (lists, setup,
spectrum, hunt, walk map), [code-hud.md](code-hud.md) (Air Quality's plots). The weather's graphs are in Flight
Radar's `core/ui_weather.c`. Round instruments are in radial-dials.md.

How sure: (M) seen on a real board, (P) built and seen in the preview or read in the code, (R) reasoned.

## Data screens stand still

On a screen people read for its numbers, decorative motion goes: no rotating beam over the weather dial, no sweep band
running across a graph. What moves is the weather itself (rain falling from the clouds into the rain bars, drops in
the pictures, a sun's rays turning slowly). The radar look keeps its sweep: that is the instrument.

- Data screens start rich. Small cues that mean something stay: a pulsing now dot, a breathing warning box, falling
  rain. A short one-time draw-in of a graph is fine.
- **Every mark says what it is.** A stranger could not tell what faint vertical day bands on a graph were, where the
  air and radiation were (unlabelled arcs on the dial), or that the dial's loop was the temperature. Drop or label
  anything a stranger would ask "what is this?" about: the air and UV arcs went (the numbers say them), the day bands
  went, the loop got its numbers and a key.
- **Now looks the same on every page**: a hand on the dial, a line up to a dot on the graphs, the clock in focus.
- The look came from a person reading the weather on the board: "it looks great, but a person wants to see the
  important data here" (M). Then the same person asked for more animation where it is weather, a golden section, a
  palette and smooth curves.

## Honest design: doubt is drawn

What the instrument does not know, it shows as not known. A confident picture of something unknown is the worst bug a
screen can have.

- Direction unknown: a hollow ring at a fixed place on its signal ring, `BRG ---`, `?` on the dial.
- Direction roughly known: a solid dot with an arc as wide as the doubt, `BRG 120* +-20`.
- Position from the walk: a ring as wide as the error, `~6 M`, `? ~6 M` when unsolved.
- Left and right unknown: `L/R ?`, `UP = YOUR FIRST STEPS`.
- What "up" means: `UP = TURN START`, `UP = WALKING`, `UP = ?`.
- Distance from a signal: always with `~`, `rough, from the level`.
- Not heard lately: fades after 8 s to 25 %. After a restart, restored networks are faded until heard again.
- No reading: `--.-`, never the last value. `LOST 12 S AGO`, in grey.
- A plane with no height, speed or callsign: `---`, `SPEED ---`, `NO CALLSIGN`, a grey dot.
- Between two answers: dead reckoning, 20 s at most, forgotten after 30 s. On paper a guessed position is hollow.
- Made-up data: `DEMO` in big letters, `DEMO SKY`, `SIM PROBE`, `ROOM (SIMULATED)`, `DEMO, MADE-UP AIR` in the alarm
  colour.
- A board without a clock: a dashed line where the power came back, the gap's length is not invented.
- A forecast: `BAROMETER FORECAST, 12 H`, `NO WIND, NO SEASON IN IT`. The weather's source and its time in a corner:
  `OPEN-METEO 14:05`.
- Too little data: `TREND IN 3 H`, `THE MAP NEEDS 3 M OF WALK: 1.2`, `the change needs 2-3 hours`.
- No forecast yet, or no memory for the dial: the glass and why, `NO WIFI: NO FORECAST`, `ASKING FOR THE FORECAST`,
  `NO MEMORY FOR THE DIAL`.
- No source for a value at all (the radiation dose for most homes): the row shows something real instead (the sun's
  next rise or set) until a source exists, never an empty or made-up number.
- A sensor missing: what was tried, which part to plug in, on which pins.
- A board left on a table: no fake map, a check of how much of the change is explained by moving.
- An untested board: said in the README, built, not run here.

Three more that came from this:

- Nothing switches on a guess. A relay never runs on an old reading, and a simulated probe never reaches real history.
- A fault says what happened, what the device does now, what to check. Tapping the red word opens that page.
- The numbers stay honest under animation: the spring is for the eye, logic reads the raw value.

Traps of honesty (M unless said):

- **An old number shown as current** (Thermo Relay). No reading: `--.-`, say how old, grey it, and for a relay: off.
- **Dead reckoning for ever** (P). No more than 20 s on a guess, gone after 30 s without a word.
- **A map from a board on a table** (P). Noise can look like a walk: a check, and no map without 3 m of walk.
- **Inventing the gap.** The board has no clock: a dashed line where the power came back.
- **A demo that looks real.** It says so in a big word, goes through the real parser to be a fair test, never reaches
  the saved history, and a real probe that answers ends the simulation.

## Numbers and words

- **Numbers big, labels small.** The value in a smooth font, its name in the pixel font or a small font above it.
- **A number with a word.** `IDLE, on at 21.8`, `WARMER`, `FAIR AIR`, `OPEN A WINDOW`, `FALLING`, `STEADY`. A state
  comes with its reason and the next step. A hysteresis of 0.5 means the heater is off at 21.9 with the target at 22,
  and without a word that looks like a fault.
- **The thing you set and the thing you measure never look alike.** The room huge in the middle, the target smaller,
  boxed, labelled TARGET.
- **A scale bar in an agency's colours** for an index: the EEA's six steps for air, the WHO's five for UV, the step now
  lit and taller, always with the word (themes-and-palettes.md).
- **A unit as the person picked**, and AUTO by the home's country (Fahrenheit, inHg, mph and inches for the US, mph
  for Britain, m/s in the Nordic and Baltic lands and some others, mmHg at the home's height where people read it so).
  The pressure in hPa and inHg at sea level.
- **Short forms when long ones do not fit**, decided by measuring (fonts-and-font-baking.md).

## Panels and lists

- **The panel of nearest planes**: rows 15 px, as many as the screen height gives, a height dot, callsign, distance
  without its unit (the title has the unit). A tap on a row picks the same plane as a tap on the scope.
- **List with drag and sorting**: row 22 px, `list_scroll` follows the finger, clamped, no inertia. A pressed row
  lights only while the finger has not moved. The scroll bar shows only when rows do not fit. WiFi Radar sorts by
  SIGNAL, NAME or CH with tabs in the header. The sort is a small insertion sort in `ui_frame`, done only while the list
  is shown.
- **Sub-canvas** for the body, so rows slide under the header (renderer-and-canvas.md).
- **A column that hugs a circle**: beside a round instrument, each row starts where the circle lets it, not at one x
  for all. The text area does not have to be a square (radial-dials.md).
- The weather panel: rows of what and its value, the pixel font for the names on the narrow 2.8" panel; breathing
  boxes for warnings; the next days; the radar's three buttons where they always are.

## Spectrum, walk map, hunt (WiFi Radar)

Each borrows a form people know already, so it needs no manual:

- **Spectrum**: a WiFi analyser's humps. x by channel, y by dBm, the 16 loudest as humps with a flat top (`rssi - 40
  e^2` past 1.4 channels), weakest first, a soft fill and a line, names on the peaks. A tap on a channel dims the rest
  and lists who sits there.
- **Walk map**: a plan with a metre grid, fitted to the walk and the networks it trusts within 14 m, so one far AP
  cannot shrink it. The path has older parts fainter, averaged over 5 s, because the solved path jitters a metre and
  the person does not. The last point is not averaged: it is where you are. A trusted network: a ring as wide as its
  error. An unsolved one: hollow. Off screen: an arrow on the edge.
- **Hunt** (the target): one network, for walking with the board in hand. The level as big as the font goes, WARMER,
  COLDER or STEADY in words with an arrow (3 dB in 10 s: walking changes the level this much, fading rarely does, and
  the trend is a least squares slope, not two points), `~4 M`, a direction dial with `?` when unknown, closeness in
  segments from FAR to NEAR so it reads without numbers, a graph of the last minute.

Numbers and rules for each: screens-catalog.md. Code: code-pages.md.

## Graphs (Flight Radar's weather pages, colour)

The pressure and the temperature as graphs on one time axis, behind the dial a tap away (P, M on the boards):

- **One axis for both**: from 14 hours before the forecast's first hour to 23 after, so now stands at about 0.382 of
  the width, the golden section (`PG_PAST 14`). The scale's numbers in a column of their own on the left.
- **Monotone cubic lines** (Fritsch and Butland): a cubic between each two hours, its slope at each the harmonic mean
  of the two either side, so it never swings past the values it joins. No frost the forecast has not got.

  ```c
  if (l && r) m[i] = dl * dr > 0.0f ? 2.0f * dl * dr / (dl + dr) : 0.0f;   // else the one side's slope
  ```
- **Drawn per glass column in whole numbers** (`graph_curve`): the line's height there, as thick across as along
  wherever it leans, its glow, and under it the fill fading to the foot; each pixel once, no joins drawn twice. The
  line is planned point by point once a frame (200 points at most), not in the strips: the C3 works a float out
  slowly.
- **The past thinner and fainter** than the hours to come (line alpha 165 and 1.3 px against 256 and 1.9 px, no glow).
  The fill: 48 under the past, 80 under the pressure to come, 92 under the temperature in its colour.
- **Now**: a line up from the axis to a pulsing dot on the curve.
- **The weather's pictures on the line every 3 hours** (6 where 3 would touch): on the temperature they ride the line,
  the picture over the number over the dot. Past pictures stand still; the coming ones' rain falls, their sun's rays
  turn.
- **Rain falls from the line into its bars**: a bar an hour as tall as the square root of how much (`sqrt(mm / 4)` of
  the room, at least 2 px).
- **Sunrise and sunset** as half suns on the time's axis.
- **A graph draws itself in** from the left over 0.9 s when its page comes, once.
- The pressure's warning: its hours in the warning's colour, the line to it said as `-11 BY 01:00`.
- The footer: three numbers, or two lines (24 H AGO, NOW, BY hh:00 on the pressure; the rain's span and the strongest
  gust on the temperature).
- On the round glass the graphs stay inside the circle (layouts-by-screen-size.md).

What went: the sweep over the graphs, the falling highlights on the bars, the faint day bands, the pressure warning
band.

## Graphs printed (the 4.2" e-paper, M)

The same data, drawn for ink (e-paper-design.md):

- The golden section: the words over the graph take 0.382 of the height, the graph 0.618; now stands at 0.382 of the
  width; on the temperature the line takes 0.618 of the graph and the rain under it the rest.
- No frames: the paper is the ground. The tones are halftones: over the line white by day and tinted by night; under it
  light grey by day, mid by night, the hours before now a step lighter. Nothing darker than mid: a warning's hours a step
  darker made a black slab of a night.
- The line black 2.6 px, the past dark grey 1.6 px, soft-edged. A dotted grid (a dot every 4 px), its numbers left of
  the graph, every other one where they stand close.
- The weather's pictures in a row over the graph every 3 hours, as big as 3 hours allow (25 px at most).
- Numbers and the warning's label on white patches with no frame; the warning's worst hour ringed and said.
- Now: a dashed line up from NOW on the time's line, a dot on the curve. The hours every 6 by the home's clock; the
  number next to NOW gives way.
- The page's name in L at the top left, the clock where the first page has it, the value big in XXL with its unit and
  its way (`FALLING`, an arrow), and facts as rows with no boxes (`12 H AGO`, `BY 01:00`; `HIGH`, `LOW`, `RAIN 14:00-18:00`
  or `NONE`, `GUSTS`).
- Rain bars by the square root, the past light grey, snow white with an ink edge; the most rain labelled, or `NO
  RAIN`.

## The weather's pictures

Small pictures of the weather out of discs and lines, so they scale with the screen (`wx_pic`, `paper_pic`):

- Clear: a sun (a disc with a glow and 8 rays turning slowly) or the moon (a crescent, or by night in its real phase).
- Partly: a smaller sun or moon behind a cloud.
- Cloudy: a cloud. Rain, drizzle, snow: a cloud with three drops, dots or flakes falling and fading, each at its own
  time. Storm: a cloud and a bolt (in `low`, amber). Fog: three banks drifting a little.
- On paper: a sun is a ring with rays, the moon a crescent cut out of a disc with the ground's ink, clouds light grey
  with a black edge (white on a grey night), a storm's cloud dark, rain black strokes, snow three-stroke flakes. The
  ink's width goes with the size: fine on the small ones.

## Air Quality's plots and Thermo Relay's graph

- Air Quality: a plot worked out once a frame (`ui_graph.c`, code-hud.md), every channel a line, 1H 24H 7D; DETAIL one
  channel with its HUD, a big plot, a cursor by finger.
- Thermo Relay: 1 h, 6 h, 24 h. Relay columns, target and band, the room's curve with a glow under it, a dashed line
  where the power came back, a cursor by finger.
