# OpenStreetMap on the board: projection and tile processing

What OWLOS Flight Radar does with a map tile once it is in flash: the plan, the 4-bit texels, the tile ring, the inflater, the
names read from the pixels, the outlines, the looks. How a tile is fetched, and OSM's tile policy, are in `apis/osm-tiles.md`;
Nominatim is `apis/nominatim.md`.

From OWLOS-FLIGHT-RADAR: `firmware/main/tiles.c`, `core/map.c`, `map.h`, `png_decode.c`, `ui_scope.c`, `firmware/main/main.c`.
`code/flight_tiles.c`, `code/flight_map.c` and `code/png_decode.c` are the copies of version 0.4; the current code has moved on
where this file says so. The pipeline runs on the 2.8" and 3.5" CYD and the round C3. [measured] Tags as in SKILL.md.

## 1. Why it is built this way

A classic ESP32 has no PSRAM on these boards, a heap whose biggest block after WiFi is about 24 KB [measured], and a screen too
big to keep in RAM. A 256 x 256 RGB tile is 192 KB decoded. So:

- The PNG files stay in flash as they came, in a partition of their own. They are also the cache OSM's policy asks for, and they
  serve every range and look.
- A PNG is decoded row by row and is never whole in memory.
- What is kept in RAM is not a picture but a picture of WHAT IS THERE: 4 bits a texel, the class of ground (water, wood, road).
  The screen paints the classes in the theme's colours, so one map serves every theme and look.
- The texels are laid out in the scope's own projection, so planes land exactly over the ground.

The order of work, on the fetch task (`map_service`):

```
same home, scale and size, and all tiles in, or asked under 15 s ago -> return
a new picture that failed under 15 s ago -> return
map_plan()         which tiles, where each texel falls on them
tiles_fetch()      the missing ones, over one kept connection, nearest home first
same picture and nothing new kept -> return
a new range or turn with none of its tiles here yet -> keep the map shown, put the plan back to it, return
map_alloc()        texels in 8-row pieces, IRAM first (PSRAM on the boards that have it)
map_begin()        every texel M_NONE, not ready
for each tile not yet read: tiles_open, map_tile (png_decode into texels); PNG_BAD: tiles_drop, ask again
map_end()          the halos round the names, ready, gen++
```

- While a new picture is made the scope shows the glass alone.
- A retry for the same plan reads only the tiles still missing (`s_map_done`, a bit a tile) into the texels as they are. It
  used to wipe them and read every tile again: with the memory short just then, a map that showed went and stayed away till a
  restart. [measured, fixed in 0.10]
- A new range whose tiles have not come keeps the map shown, scaled by its own kpx. Wiped first, the map went from the screen
  and stayed away while the new tiles could not come. [measured, fixed in 0.14]
- The map's log line: zoom, tiles read of planned, the time to make it, the heap after the tiles came and after reading them.
  "no memory to read a tile in" logs the heap and its largest block.

## 2. Zoom, projection, the plan

```c
static double world(int z) { return 256.0 * (double)(1u << z); }      // the world's width in pixels

// Web Mercator: the pixel row of a latitude
static double merc_y(double lat, double w)
{
    double f = lat * PI_D / 180.0;
    return (1.0 - log(tan(f) + 1.0 / cos(f)) / PI_D) / 2.0 * w;
}

int map_zoom(float lat, float kpx)
{
    // a tile pixel at zoom z covers 40075 km * cos(lat) / (256 * 2^z)
    double c = cos(lat * PI_D / 180.0);
    if (c < 0.05) c = 0.05;
    int z = (int)floor(log2((double)kpx * 40075.017 * c / 256.0) + 0.5);
    return z < 2 ? 2 : z > 17 ? 17 : z;
}
```

`kpx` is texels a km, `PR / range_km` (PR is the scope's radius in pixels): a texel is a screen pixel at the range it was made
for. The zoom is the one whose tile pixel comes nearest a texel. While a finger zooms or turns the map, the screen only
stretches and turns the same texels, and the map is made again when the range settles.

The plan, per texel row, in the projection of `geo_offset` (`flight-data.md` section 7):

```c
for (int v = 0; v < h; v++) {
    // sky.c's projection: north in km is the latitude, east is the longitude at the middle latitude
    double north = (hh - v - 0.5) / kpx;
    double la = lat + north / 110.574;
    double cm = cos((lat + la) * 0.5 * PI_D / 180.0);
    double s = ww / (360.0 * 111.320 * cm * kpx);           // world pixels a texel, east
    p->row[v] = (int32_t)floor(merc_y(la, ww));
    p->x0[v] = (int32_t)llround((xc + (0.5 - hw) * s - p->xorg) * 65536.0);
    p->dx[v] = (int32_t)llround(s * 65536.0);
}
```

- `xorg = floor(xc) - 4096`: the row starts are counted from a point near home, in 1/65536 pixel. Counted from the edge of the
  world they would not fit 32 bits at zoom 17.
- Tiles under the map: every other texel asks which tile it is on, the last row and column too. Nearest home first, at most
  `MAP_TILES_MAX` (16; 32 on the scaled big screens). `x` may run off the world's edge, `map_url` wraps it. A `y` past the poles
  has no tile.

## 3. The texels

```c
enum {
    M_LAND, M_WATER, M_WOOD, M_GREEN, M_URBAN, M_BUILDING, M_ROAD, M_MAJOR, M_MOTOR, M_RAIL,
    M_BORDER, M_TEXT, M_AERO,
    M_HALO,     /* not in the tiles: round the letters of a name (map_end). the dark looks paint it dark */
    M_SOFT,     /* a letter's soft edge: classic paints it as the letter, the dark looks dimmer */
    M_CLASSES,
    M_NONE = 15 /* nothing known there (yet) */
};
```

`map_class` matches an OSM colour to the nearest of the reference colours, in a weighted distance `2 dr^2 + 4 dg^2 + 3 db^2`
(the eye cares for green most). It is a match, not a lookup, because each tile has a palette of its own. The palette is classed
once a tile, then a pixel is one table read.

Reference colours of OSM's standard style (`REF` in `map.c`, about 50 entries; 44 in 0.4): land 242,239,233; farmland
238,240,213; water 170,211,223; forest 173,209,158; grass 205,235,176; residential 224,223,223; building 217,208,201; road
255,255,255; primary 252,214,164; motorway 232,146,162; rail 112,112,112; border 172,70,172; text 0,0,0; runway 187,187,204.
Added since: 85,85,85 and 128,95,60 as text (a letter's soft edge, a hill's brown name); 220,220,220 as town and 200,200,200 as
building, because the plain greys of a town were taken for an apron and lit up as runways. If OSM changes its style, this table
is what to redo.

Rows go to texels by nearest neighbour, never by mixing: the class of a pixel cannot be averaged anyway.

Which kind a texel keeps when its pixels say more than one (since 0.16.2): the zoom's pixel comes nearest a texel, so a texel is
up to 1.4 pixels. Read at one pixel, a road or a stream one pixel wide fell between the texels and came out dashed. Now each
texel keeps the kind that would be lost, by `KEEP`: halo and land 1, green 2, wood 3, town 4, building 5, road 6, water 7,
runway 8, rail 9, border 10, major road 11, motorway 12, soft edge 13, letter 14. The areas give way to the lines, everything to
the letters. [measured: roads and streams run whole on the boards]

4 bits a texel: 480 x 320 is 75 KB, the disc of the 2.8" a fraction of that. [measured] Run-length coding of the rows was tried
and does not help: the detail is too fine. [measured]

The memory trick: a 75 KB picture cannot sit in the heap next to TLS. So it is many small pieces (8 rows each), taken from IRAM
first, which neither TLS nor WiFi can use. Plain IRAM on the ESP32 takes 32-bit accesses only: a byte access faults. So every
texel is read and written through a 32-bit word:

```c
static void *iram_first(size_t bytes)
{
#if CONFIG_IDF_TARGET_ESP32
    return heap_caps_malloc(bytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
#elif CONFIG_SPIRAM
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);   // every board with PSRAM. none found: NULL, malloc's
#else
    (void)bytes;
    return NULL;                                       // the C3's RAM is one: malloc does it
#endif
}

static inline int map_at(const map_t *m, int u, int v)
{
    return (int)(m->row[v][u >> 3] >> ((u & 7) * 4)) & 15;
}

// writing, in tile_row
const int sh = (u & 7) * 4;                      // a word at a time: it may be in the IRAM
row[u >> 3] = (row[u >> 3] & ~(15u << sh)) | (uint32_t)k << sh;
```

`memset` is out for the same reason: `map_begin` fills with a loop of words. The PSRAM path: the 5" and the Tab5 have no room for
the 190 KB picture in RAM, the C5's strips and AES want the RAM. The `sdkconfig` lines that made room (TLS dynamic buffers, WiFi RX
5, WiFi IRAM options off, about 36 KB of IRAM back) are in `https-client.md`.

## 4. The tile partition

`partitions.csv`, 4 MB board: `tiles, data, 0x40, 0x210000, 0x1F0000` (1.9 MB after a 2 MB app). 2 MB board:
`tiles, data, 0x40, 0x160000, 0xA0000` (640 KB). The 16 MB boards (the Tab5, the JC8012): 6 MB. The Waveshare 7 and 7B keep the
4 MB layout's app and tiles.

A ring. A tile starts on a 4 KB sector with a 32 byte header, its PNG follows:

```c
#define SECTOR    4096u
#define MAGIC     0x544D534Fu          /* "OSMT" */
#define HEAD      32u
#define MAX_TILE  (160u * 1024u)       /* a tile bigger than this is not kept */
#define MAX_KEPT  128

typedef struct { uint32_t magic; int32_t z, x, y; uint32_t len, seq, spare[2]; } head_t;
```

- The header is written LAST. NOR flash can be written once after erasing, so the PNG goes in at offset 32 and the header after
  it. A download cut short, or a power cut, leaves no header, so no tile.
- Sectors are erased just before the writing reaches them, and any kept tile they touch is forgotten then.
- At power on the headers are read back. The newest `seq` says where to go on. A sector with no good header is stepped over.
- The index in RAM is 128 x 24 bytes. Full, the oldest goes.
- `tiles_drop` forgets a tile that will not read and clears its header's magic in flash (a write turns bits from 1 to 0 without
  an erase), so the scan at power on passes it by too.

The same "data first, header last" ring is in the WiFi Radar's save slots (`flashstore.c`) and in the air history.

## 5. The inflater

`png_decode.c`, after Mark Adler's puff.

- Why our own: the ESP32 has an inflater in ROM (miniz's tinfl), but it wants its 32 KB window in ONE piece, and after WiFi starts
  the heap has nothing that big. [measured]
- Ours keeps the 32 KB look-back window in pieces. 0.4 had 8 of 4 KB (that is `code/png_decode.c`, 331 lines):

```c
#define WIN_PIECE  4096u                      /* the window in pieces the heap always has */
#define WIN_PIECES (32768u / WIN_PIECE)

static void put(st_t *s, int b)
{
    s->win[(s->out / WIN_PIECE) % WIN_PIECES][s->out % WIN_PIECE] = (uint8_t)b;
    s->out++;
    ...
}
```

- The pieces got smaller twice. 0.6: 1 KB, after the 3.5" stopped making maps until a restart with "heap 56884, largest 19456":
  a broken-up heap failed for good. 12 of 12 maps with looks switched every 8 s after it. 0.16.1: 512 bytes (64 pieces), and the
  two rows made as long as the tile's, not the widest picture's (2 KB less): on the 3.5" 154 of 154 maps whole in a 20 minute
  cycle, against 147 of 151 before; the CYD 153 of 153. [measured]
- In 0.4 it took about 39 KB while it ran, the largest piece 5 KB, and gave all of it back. On the 3.5" the reader takes about
  38 KB and the least free heap while reading was 1068 bytes, on the CYD 17.9 KB. [measured]
- 8-bit palette, grey, grey+alpha, RGB, RGBA. Since 0.14 also 1, 2 and 4 bit palettes, which OSM sends for sea and plain land.
  Not interlaced (map tiles never are). `PNG_MAX_W` 256.
- Each byte that comes out of the inflater goes to the row filter; each full row goes to a callback. IDAT runs on from one chunk
  into the next without a seam. Rows done: it stops, the zlib checksum is not read.
- A bit at a time, plain and slow on purpose: a map is made rarely. A map on the 3.5" took about 0.8 s at zoom 13 for 6 tiles,
  1.1 to 1.6 s with the names read word by word. [measured]
- Returns PNG_OK, PNG_BAD, or PNG_NO_MEMORY. The caller logs heap and largest block on NO_MEMORY.

```c
typedef int  (*png_read_fn)(void *ctx, uint8_t *buf, int cap);          /* the next bytes, 0 at the end */
typedef int  (*png_head_fn)(void *ctx, const png_info_t *info);         /* before the first row: 0 stops */
typedef void (*png_row_fn)(void *ctx, const png_info_t *info, int y, const uint8_t *row);
int png_decode(png_read_fn rd, void *rctx, png_head_fn head, png_row_fn row, void *ctx);
```

## 6. The names: read from the tiles, nowhere else

The map's town and village names come from the tile pixels, on the board. No server of our own, no Overpass, no GeoNames or other
place list. Overpass was tried for a list of places and refuses non-browser clients (`apis/overpass.md`); a server of our own
that serves place names was rejected as not proper. If something cannot be done from the tiles, say so plainly. [measured, and
the project's rule]

How a letter is found (`map.c`, since 0.16.2), because OpenStreetMap's colours do not change:

- A grey (r, g and b within 12) under 100 is a letter's core (`MK_CORE`).
- A grey under 133 (`MK_DARK`; rails are 112) or under 170 (`MK_LIGHT`), or a tinted one with its colours within 40 under 170
  (`MK_TINT`: the halo under a name is half see-through, the ground shows in the soft edge), is an edge. It is a letter's when a
  core is within `NEAR` 3 pixels: the thin stem of an i with an accent has no core of its own, and a road's casing a few pixels
  on is no letter. A grey with no core near is a rail or a road's edge.
- Cores and their edges close together (`GAP` 5 pixels in a row, a row between) are a word. A word no taller than 16 and no wider
  than 180 pixels, with 2 cores or more and a core in every 6 of its pixels, is copied pixel for pixel and stands by its middle,
  scaled as everything else is. A word that touches a tile's edge stands by that edge, so the two halves of a name split between
  tiles meet. The rest goes the scaled way: a core a letter (`M_TEXT`), an edge beside one its soft edge (`M_SOFT`).
- Why copied: the tiles write a town's name some 9 pixels high for their own scale. Stretched or shrunk to the screen's (up to 1.4
  times either way) a stroke came out a pixel here and two there, an accent ran into its letter, and a name with a hacek read
  wrong.
- In little memory: the marks of the last 22 rows, 4 bits a pixel, a ring. 3.6 KB while a tile is read (`MAP_SCRATCH`), lent by
  the firmware from the Nominatim buffers it already had, not from the heap: from the heap, the 2.8" could not read a third of its
  maps in a soak ("no memory to read a tile", heap 44.8 KB, largest block 21 KB). After the loan: 113 of 113 maps whole on the
  2.8". [measured]
- `map_end` makes every texel touching a letter (the eight about it) `M_HALO`, painted dark on the dark looks under light letters:
  the tiles write dark letters in a light halo, made for a light map, and on the dark glass a name was a hole with a glow round it.
- SET, VIEW, MAP NAMES sets how bright the soft edge is on the dark looks: SOFT half (the default), THIN a quarter, BOLD as bright.
  Light letters on dark glass look fatter than dark ones on paper.

## 7. The outlines

Water and towns get a bright edge inside them on the dark looks: the eye reads where things end more than what fills them, and on
these screens a fill is a step or two above black. Worked out as the screen asks, nothing kept:

```c
#define MAP_RIM 16
static inline int map_rim(const map_t *m, int u, int v)
{
    static const uint8_t UP[16] = { [M_LAND] = 1, [M_GREEN] = 1, [M_WOOD] = 2, [M_URBAN] = 3, [M_BUILDING] = 3, [M_WATER] = 4 };
    const int k = map_at(m, u, v), up = UP[k];
    if (up < 3) return k;
    int n;
    if (u > 0 && (n = UP[map_at(m, u - 1, v)]) && n < up) return k | MAP_RIM;
    if (u + 1 < m->w && (n = UP[map_at(m, u + 1, v)]) && n < up) return k | MAP_RIM;
    if (v > 0 && (n = UP[map_at(m, u, v - 1)]) && n < up) return k | MAP_RIM;
    if (v + 1 < m->h && (n = UP[map_at(m, u, v + 1)]) && n < up) return k | MAP_RIM;
    return k;
}
```

- Woods have none: the country is woods in pieces, and every piece outlined rippled over the whole map; asking a forest's
  neighbours also cost every texel of it. Roads, rails, borders, runways and names have none: they are lines, and a name's halo
  would be outlined.
- The classic look outlines only the water, a deeper blue.
- Tried and dropped: outlines baked into the texels as two more kinds (classic got dirty, names broke, a retry outlined again);
  water hatching.
- Cost on the 3.5": MAP 45 to 50 ms a frame, CLASSIC 46 to 51, RADAR 65 to 66. [measured]

## 8. The looks

One map, several ways to paint it. One button goes round them: RADAR, MAP, CLASSIC, and WEATHER (no map).

- RADAR: the disc with its fixed panel. The ground in theme colours, lit by the beam like land echoes, dim off the beam. Planes
  glow, brighter just after the beam.
- MAP: square, whole screen, home in the middle, north up, grid and scale. Theme colours, steady. Planes glow.
- CLASSIC: the same in OSM's own colours. A dark edge round a silhouette: light does not show on light ground.
- The round glass keeps its disc in all looks. The texture is sized per look: `ui_ground_size` gives the disc `2 PR` or the whole
  screen. The panel in the square looks slides in over the map.

The strength of each class in the theme looks (`LEV` in `ui_scope.c`, out of 255), since 0.16.2: land 26, water 0, wood 44, green
34, town 56, building 66, road 76, major 112, motorway 125, rail 106, border 100, letters 215, runway 140, halo 0, soft edge 110.
In 0.4 land was 14 and woods 34, one or two steps of 32 above black. Small roads are quieter the wider the range: 76 up to 25 km,
down to 44 from 150 km, because from 25 km out the tiles have every lane in the country.

The radar's ground off the beam is `LEV * 110 / 256`, and behind the beam `LEV * echo` with `echo = 110 + 146 * f^2 >> 16` over
90 degrees of afterglow, so it fades.

The OSM colours of the classic look (0.4):

```c
static const uint8_t CLASSIC[M_CLASSES][3] = {
    { 242, 239, 233 }, { 170, 211, 223 }, { 173, 209, 158 }, { 205, 235, 176 }, { 224, 223, 223 },
    { 217, 208, 201 }, { 255, 255, 255 }, { 252, 214, 164 }, { 232, 146, 162 }, { 112, 112, 112 },
    { 172, 70, 172 },  { 40, 40, 40 },    { 187, 187, 204 },
};
```

Which texel lies under a screen pixel, in 16.16 fixed point, with the map turned by `up` and stretched by `gk`:

```c
gk  = m->kpx / k_px(u);                      // made at one range, shown at another
gdu = lroundf(cosf(up) * gk * 65536.0f);     // a step of one pixel along x
gdv = lroundf(sinf(up) * gk * 65536.0f);
for (x = ga; x <= gb; x++, gu += gdu, gv += gdv) {
    uint32_t iu = gu >> 16, iv = gv >> 16;
    if (iu < mw && iv < mh) ground_row[x - xa] = (m->row[iv][iu >> 3] >> ((iu & 7) * 4)) & 15;
}
```

The look-up runs in a loop of its own before the pixel loop: the pixel loop has no registers left. The disc pass is made twice by
an always-inline function, once with `map == NULL`, because a loop that may meet the map is slower by a sixth even with no map in
it. [measured] Drawing and speed belong to the drawing skill; this is the part the data shapes.

The map is shown only when it fits what is on the screen, or nothing is:

```c
const map_t *m = u->map;
if (!m || !m->ready || !m->tiles || !u->cfg.map || m->lat != u->cfg.lat || m->lon != u->cfg.lon) return NULL;
ui_ground_size(&u->cfg, &w, &h, &disc);
return m->w == w && m->h == h ? m : NULL;          // the look just changed: its map is still being made
```

The cards under writing on the map (plane labels, the degrees, the attribution, the ring's numbers) are solid since 0.16.2: the
brighter map showed through the letters.

## Traps

- `tinfl` out of memory with plenty free: it wants 32 KB in one block, the heap has about 24 KB. Own inflater, the window in
  pieces. [measured]
- No room for the map beside TLS: 75 KB and TLS peaks. IRAM first in word-only pieces, mbedTLS dynamic buffers, WiFi RX 5, WiFi
  IRAM options off. [measured]
- Crash writing a byte to the map: IRAM takes words only. Read and write 32-bit words, no `memset`. [design rule]
- Reset every 29 s on the C3: one core, the fetch task below the screen had under 1 ms a frame, TLS and map work starved IDLE.
  The fetch task shares the screen's priority there. [measured]
- Min free heap fell to 6.4 KB: a second set of static buffers for the search. One `s_osm_url`, `s_osm` for both. [measured]
- The map stops coming until a restart, "no memory to read a tile in" with plenty free: the heap broken into pieces smaller than
  the window's. Smaller window pieces. [measured]
- The map keeps holes over the sea: 1, 2 and 4 bit palette tiles refused and kept. Read them; drop a tile that will not read.
  [measured]
- A new feature's static buffers broke the 3.5"'s square map (0 of 6 tiles every time): the tile reader had about 1 KB to spare.
  Run a 20 minute cycle of looks and ranges after any memory change, and count maps whole. [measured]
- A half tile after a power cut: cut download. Header last, only a complete body makes a tile. [built]
- The kept connection fails: server closed it. One more try on a new connection. [built]
- Row RLE to save the texture: detail too fine, it does not help. Do not repeat it. [measured]
- Names eaten on the dark looks, accents gone: letters read one pixel at a time and scaled. Copy words pixel for pixel. [measured]
- Roads and streams dashed: a texel read at one pixel of up to 1.4. Keep the kind that would be lost. [measured]
- New sdkconfig defaults do nothing: an old `build*/sdkconfig`. Delete it. [measured]
