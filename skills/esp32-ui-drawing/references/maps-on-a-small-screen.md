# Maps on a small screen

How OpenStreetMap's ground is shown under an instrument on a few hundred pixels: what the board keeps of a tile,
how each look paints it, how names stay readable, and what was tried and dropped. This is the drawing side (Flight
Radar's `core/map.h`, `map.c`, `ground_paint` and the disc pass in `ui_scope.c`). Fetching tiles, the zoom and
projection, the tile partition and the PNG inflater are in
[osm-tiles.md](../../esp32-data-sources/references/apis/osm-tiles.md). The disc pass code is in
[code-scope.md](code-scope.md).

How sure: (M) seen on a real board, (P) built and seen in the preview or read in the code, (R) reasoned.

## The board keeps kinds, not colours

The tiles are pictures in Web Mercator; the scope has its own projection (kilometres east and north of home). So
the tiles are not shown: they are read once into a picture of the screen's own scale and projection, north up, a
texel a screen pixel at the range it was made for. The radar's disc only, or the whole screen for the square looks
(the round glass keeps its disc in every look). The screen turns and stretches it as the map turns and zooms, and
it is made again when the range settles or the look changes.

A texel keeps what is there, 4 bits:

```c
enum {
    M_LAND, M_WATER, M_WOOD, M_GREEN, M_URBAN, M_BUILDING, M_ROAD, M_MAJOR, M_MOTOR, M_RAIL,
    M_BORDER, M_TEXT, M_AERO,
    M_HALO,     /* not in the tiles: round the letters of a name */
    M_SOFT,     /* not in the tiles' colours: a letter's soft edge */
    M_CLASSES,
    M_NONE = 15 /* nothing known there (yet) */
};
```

The whole 480x320 screen is 75 KB at 4 bits. The picture is many pieces of 8 rows (`MAP_BAND`), read and written a
32-bit word at a time, so it may lie in the IRAM the code leaves free, which takes words only (P, M on the 3.5").
On scaled glass the texels are the glass's pixels (up to the glass's size).

A row is `w / 8` words, so the width must be a multiple of 8. On the 4.2" e-paper the disc map was 236 wide and the
last 4 texels of each band wrote past it: RADAR to MAP crashed (M). Now the disc's size is rounded up to 8 and the
allocator refuses any other width.

## From a tile pixel to a kind

The OSM standard style's colours do not change, but each tile has a palette of its own, so a colour is matched to
the nearest of a list (distance weighted 2, 4, 3 for red, green, blue: the eye cares for green most), not looked
up. The list, by kind (map.c `REF`):

- LAND: 242, 239, 233 (land), 238, 240, 213 (farmland), 245, 220, 186 (farmyard), 214, 217, 159 (heath), 245, 233,
  198 (sand), 238, 229, 220 (rock), 255, 241, 186 (beach), 255, 255, 229 (school grounds).
- WATER: 170, 211, 223 and its names 77, 128, 179.
- WOOD: 173, 209, 158 (forest), 157, 202, 138 (wood), 170, 203, 175 (cemetery).
- GREEN: 205, 235, 176 (grass), 200, 250, 204 (park), 223, 252, 226 (pitches), 201, 225, 191 (allotments), 174, 223,
  163 (orchard), 200, 215, 171 (scrub).
- URBAN: 224, 223, 223 (residential), 242, 218, 217 (commercial), 255, 214, 209 (retail), 235, 219, 232
  (industrial), 238, 238, 238 (parking), 223, 221, 206 (garages), 220, 220, 220 (a town's plain grey).
- BUILDING: 217, 208, 201, 196, 182, 171, 200, 200, 200.
- ROAD: 255, 255, 255 and its edge 153, 153, 153. MAJOR: 247, 250, 191, 252, 214, 164, 160, 107, 0, 112, 125, 5.
  MOTOR: 232, 146, 162, 249, 178, 156, 220, 42, 103, 200, 78, 47.
- RAIL: 112, 112, 112. BORDER: 172, 70, 172, 207, 155, 203. AERO: 187, 187, 204 (runway), 218, 218, 224 (apron).
- TEXT: 0, 0, 0, 51, 51, 51, 85, 85, 85 (a letter's soft edge), 128, 95, 60 (a hill's brown name).

The plain greys of a town were first matched to the runway's blue-grey and towns lit up as runways: the town greys
were added to URBAN and BUILDING (M).

A texel covers up to 1.4 tile pixels (the zoom whose pixels come nearest a texel). Read at one pixel, a road or a
river one pixel wide fell between the texels and came out dashed. So a texel keeps the kind that would be lost: the
areas give way to the lines, and everything to the letters of a name (`KEEP`, M).

## Names, copied pixel for pixel

The tiles write a town's name about 9 px high, soft-edged, for their own scale. Stretched or shrunk to the screen's
(up to 1.4 times either way) a stroke came out a pixel here and two there, an accent ran into its letter, and a name
with a hacek and an accented i lost them. So a word is copied pixel for pixel, and only where it stands is scaled
(M on the bench boards):

- A neutral grey (r, g and b within 12) under 100 is a letter's core. A grey under 170 with its colours within 40 is
  an edge, a letter's when a core is within 3 px (the thin stem of an i with an accent has no core of its own; a
  road's casing a few pixels on is no letter). A grey with no core near is a rail or a road's edge.
- Cores and their edges close together (5 px in a row, a row between) are a word. A word no taller than 16 and no
  wider than 180 with cores enough is stamped 1:1 at its scaled middle, or by the tile edge it touches, so the two
  halves of a split word meet. The rest goes the scaled way: a core a letter (M_TEXT), an edge beside one its soft
  edge (M_SOFT).
- It is read as the rows come, in little memory: a ring of 22 rows at 4 bits a pixel, 3.6 KB while a tile is read.
  From the heap that failed a third of the 2.8"'s maps in a soak (the heap in pieces round the PNG's window), so the
  firmware lends a buffer of its own that nothing uses at that moment (`map_scratch`, 3584 B) (M).
- Every texel round a letter (its eight neighbours) becomes `M_HALO`. The tiles write names dark with a light halo,
  made for a light map; the dark looks paint the halo as the glass itself and the letters light, so a name reads
  there too. Before, it was a hole with a glow round it (M).
- `M_SOFT`, a letter's soft edge, is painted as SET's MAP NAMES says on the dark looks: SOFT 110 (the default), THIN
  60, BOLD 215 (the old look, where light strokes ran as fat as the dark ones). Classic paints it as the letter.
  Painting the soft edge as bright as the letter made dark-look names swim (M). Cores alone broke "e" and "n".
- Names come only from the tiles, read on the board. No own name server, no Overpass or GeoNames: what cannot be read
  from the tiles is said plainly, not fetched elsewhere. (Overpass also refused clients that are not browsers.)

## How each look paints the kinds

`ground_paint` gives each kind a colour and a strength, and each kind on an outline another pair:

- **CLASSIC**: the map's own colours, painted as they are: land 242, 239, 233, water 170, 211, 223, wood 173, 209,
  158, green 205, 235, 176, urban 224, 223, 223, building 217, 208, 201, road 255, 255, 255, major 252, 214, 164,
  motor 232, 146, 162, rail 112, 112, 112, border 172, 70, 172, text 40, 40, 40, aero 187, 187, 204, halo 246, 245,
  241 (as the tiles have it), soft edge 40, 40, 40.
- **Theme looks (MAP, RADAR)**: strengths 0..255 added to the glass in `prim`, with motorway, runway, text and soft
  edges in `focus`, rail in `quiet`, border in `low`. Water is darker than land. Strengths now: land 26, water 0, wood
  44, green 34, urban 56, building 66, road 76, major 112, motor 125, rail 106, border 100, text 215, aero 140, halo
  0, soft 110 (by MAP NAMES).
- What changed: they were land 14, water 0, wood 34, green 24, urban 46, building 62, road 66, major 100, motor 120,
  rail 110, border 95, text 14, aero 140. Land and woods were one or two 565 steps above black and the screen shows
  little of its first few steps: the steps are now even to the eye, not in the numbers (M, "the map made easier to
  see"; it cost a few ms, frame-and-speed.md).
- Small roads get quieter the wider the range: 76 at 25 km and in, 44 from 150 km out, between in a line. From 25 km
  on the tiles have every lane in the country, and whole they were a net over the map.
- The RADAR look scales the strengths by 110/256 and lights them with the beam like ground echoes: brightest just
  behind the beam, then fading to a whisper (`echo_lut[i] = 110 + 146 * f^2 / 65536`, f the distance behind the
  beam).

## Outlines, worked out as the screen asks

The eye reads where things end more than what fills them, and on these screens a fill is a step or two above black.
A town or water texel is on an outline when a neighbour (left, right, up, down) is lower: open country 1, wood 2,
town 3, water 4; roads, rails, borders, runways and names 0, lines of their own.

- Dark looks: a bright rim inside water (110) and towns (96, 100). Classic: only the water's edge, a deeper blue 110,
  160, 196.
- Woods are not outlined: the country is woods in pieces, and every piece outlined was a ripple over the whole map;
  asking a forest's every texel for its neighbours also cost (M).
- Nothing is stored for them: `map_rim` returns `kind | MAP_RIM`, so the paint tables are 32 long and classic can leave
  them out.
- Tried and dropped: outlines baked into the texels as two new kinds (classic got dirty, names broke, a retry
  outlined again), water hatching (no lake in the cached tiles to judge it by).

## Over the map

- Text on the map stands on solid cards (alpha 255): plane labels, grid degrees, the OSM credit, the round's place
  card, the touch distance, the ring numbers when a map is under them. Half see-through cards mixed the letters with
  the ground (M).
- Every mark needs its own edge: on a light map a dark one (themes-and-palettes.md, the light look), the home pin a
  colour of its own with a dark edge.
- `(c) OpenStreetMap` wherever the map shows: its licence asks for it.

## The flat looks (MAP and CLASSIC)

The whole screen, north up, home in the middle. No rim, so no turning: `view_up` is 0 and a drag anywhere zooms.

- A grid of meridians and parallels a round number of minutes apart (`grid_step`, at least 0.42 of the range ring on
  screen), named at the edges on small cards. Worked out once a frame (`grid_frame`): per strip it cost 38 of 110 ms
  on the 3.5" (M).
- A scale bar of a round distance, 30 to 70 px long.
- Corners: `sq_box` lists four boxes the labels keep off.
- The panel slides in over the right side from the three dots, and by itself while a plane is picked.
- Planes off the square become darts at its edge, up to 2.4 x the range.
- A user asked for the flat map to turn like the radar: not built (it would need one up for the board, memory for the
  corners, and the degree grid drawn turned).

## On e-paper

The paper prints the map as a chart in inks and patterns, the areas judged by their neighbours, and only when it is
new: e-paper-design.md, "the sheet". On paper the flat MAP look is skipped: CLASSIC is the map there.

## Memory, in short

The map competes with TLS and the tile reader for the heap. What was learned on the drawing side: the weather's
tables once took the 1.5 KB the 3.5"'s tile reader needed (0 of 6 tiles in MAP every time), the strip went from 8 to
6 rows to give it back, the weather's tables are freed when its look goes, and a map for a screen that does not show
it is no news (M). The reader's own memory work is in osm-tiles.md.
