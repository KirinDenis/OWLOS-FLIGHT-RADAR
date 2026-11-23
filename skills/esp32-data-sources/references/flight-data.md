# Flight data: the model, whatever the source

What every plane source shares in OWLOS Flight Radar: the ADS-B Exchange v2 format, `plane_t`, the reader that takes an
answer in pieces, the fetch loop, the guess between answers, trails, the watch list, squawks and silhouettes. Each source's
own URL, limits and failures are in `apis/` (adsb-lol.md, adsb-fi.md, fr24-api.md, opensky.md).

From OWLOS-FLIGHT-RADAR: `core/sky.c`, `sky.h`, `watch.c`, `light.c`, `ui_scope.c`, `firmware/main/main.c`, CHANGES.md.
`code/flight_sky.c`, `code/flight_watch.c`, `code/flight_light.c` and their headers are the copies of version 0.4. The
current `sky.c` has moved on (sections 5 and 9, the new fields): what is said here is the current code unless it names 0.4.
Tags as in SKILL.md.

## 1. Who is asked, in one view

- adsb.lol and adsb.fi: free, no key, the ADS-B Exchange v2 format, every 5 s. Server 0 and 1 of `FREE_URL`.
- Flightradar24: paid, a Bearer key, its own format, every 20 s.
- OpenSky: researched, not built.
- airplanes.live and adsb.one: removed, both answer 403 to strangers.
- The watch list: one lookup every 15 s on the free network the planes are NOT on.
- Numbers: HTTP timeout 9 s; a plane forgotten 30 s after the last word about it (60 s at FR24's pace); WiFi gone counts as
  gone after 20 s; on 429 the wait is multiplied by 3.

## 2. The ADS-B Exchange v2 format

Spoken by adsb.lol and adsb.fi, by the demo (`demo_json`) and by the board's relay to the page (section 13). One object a
plane, about 750 bytes to 1 KB each [measured]. The list is `ac` (adsb.fi's old v2 called it `aircraft`: the reader takes
either). The rest of each answer does not matter, except `now`, the server's clock (section 5).

Fields read (`adsbx_field`), and where they go:

- `hex`, string: `icao` (24 bit, the key). A `~` in front marks a non-ICAO address, skipped while reading the digits.
- `flight`, string: `flight[9]`. Padded with spaces: trim the end.
- `r`, string: `reg[10]`, the registration with its dash.
- `t`, string: `type[5]`, the ICAO type designator: B738, A320.
- `category`, string: `kind`. A1..A7, B1..B7, C0..C3, section 12.
- `lat`, `lon`, numbers: an object without both is dropped.
- `alt_baro`: a number in feet OR the string `"ground"`. The string is how a plane on the ground is told.
- `alt_geom`, number: `alt`, only if `alt_baro` gave nothing.
- `gs`, number: knots over the ground, `has_spd`.
- `track`, number: degrees true.
- `baro_rate`, number: `vrate`, feet a minute. `geom_rate` only if `baro_rate` was 0 or missing.
- `squawk`, string: `atoi`, the four octal digits read as a decimal number, so 7700 is 7700.
- `seen_pos`, number: the position's age in seconds; `seen = now - age`.
- What the plane says of itself, passed on when heard (since 0.6): `nav_altitude_mcp` (the height set on the autopilot; it
  comes as 32992 for 33000, so it is rounded to hundreds), `nav_heading`, `ias`, `mach`, `wd` and `ws` (the wind where it
  is). Measured in one answer of 83 planes round a busy airport on adsb.lol: `nav_altitude_mcp` 55 of them, `emergency` 47,
  `ias` and `mach` 46, `wd` and `ws` 41, `nav_heading` 21, `dbFlags` 4. [measured]
- Directions are kept as 1..360, so 0 can mean not heard.

## 3. plane_t, as kept

172 bytes now (`_Static_assert(sizeof(plane_t) <= 172)`: three copies of 48 live in the classic ESP32's RAM). The 0.4 copy in
`code/` is 160 bytes, computed from the layout.

```c
typedef struct {
    uint32_t icao;               /* 24-bit address, the key. 0 = free slot */
    char     flight[9];          /* callsign */
    char     reg[10];            /* registration */
    char     type[5];            /* ICAO type designator: B738, A320.. */
    char     orig[4], dest[4];   /* IATA airports, FR24 only */
    uint8_t  kind;
    uint8_t  ground;
    uint8_t  has_alt, has_spd;
    float    lat, lon;
    int32_t  alt;                /* feet, barometric */
    float    gs;                 /* knots */
    float    track;              /* degrees, true */
    int16_t  vrate;              /* feet a minute */
    uint16_t squawk;             /* as the four octal digits read in decimal: 7700 */
    int16_t  sel_fl;             /* the height set on the autopilot, hundreds of feet. 0: not heard */
    uint16_t sel_hdg;            /* the heading set on it, 1..360 */
    uint16_t ias;                /* indicated airspeed, knots */
    uint16_t mach;               /* thousandths */
    uint16_t wind_dir;           /* where the wind comes from, 1..360 */
    uint8_t  wind_kt;
    int8_t   gh;                 /* the nose's slide, degrees, in what was padding */
    uint32_t seen;               /* our ms clock at this position */
    uint32_t heard;              /* our ms clock at the last answer that had it */
    uint32_t first;              /* our ms clock when it first showed up, for the ring it gets */
    float    x, y;               /* km east and north of home at `seen` */
    float    dist, brg;          /* km and degrees from home */
    int16_t  tx[TRAIL_N], ty[TRAIL_N];   /* trail, 10 m units, oldest first */
    uint8_t  tn;
    int8_t   turn;               /* tenths of a degree a second, + to the right, from its last two positions */
    int8_t   gx, gy;             /* the slide onto this position, 25 m units */
} plane_t;
```

## 4. Reading an answer on a small heap

Why: a busy sky is 100 planes at about 1 KB each, more than a classic ESP32 has free after WiFi and TLS, and more than any
one block it has. At a 200 km range the board takes some 165 KB of planes every few seconds. So the answer never exists
whole. [measured: heap blocks about 24 KB after WiFi]

The way it goes (`sky_read`, whole in `code/flight_sky.c` as of 0.4):

1. `sky_read_begin` under the lock: copies home and the ground setting. Only those are read from the sky.
2. The download runs with no lock held. `net_get` hands each 1 KB piece to `sky_read`.
3. `sky_read` is a character machine: is it in a string, is the last character an escape, how deep in `{}[]`, what was the
   last string at depth 1. The list starts at a `[` at depth 1 after the right name. Each `{` at depth 2 opens a plane; its
   bytes are copied into `obj[2048]` until its `}`. Since 0.16.2 it reads the top level to the end, because `now` may come
   after the list; in 0.4 it stopped at the end of the list.
4. The one object is read in place by a small walker (`parse_plane`): key, colon, value, into a stack `plane_t`. Nothing is
   allocated. An object longer than 2 KB is skipped, not cut.
5. `keep` holds only the nearest SKY_MAX (48) of the answer, by squared distance in `got[]`.
6. `sky_read_end`, under the lock: dates the positions (section 5) and merges `got[]` into the sky. A short read keeps what
   was read. It returns -1 when the answer had no list, and the screen says STRANGE ANSWER.

```c
if (ch == '{' || ch == '[') {
    if (r->state == 0 && r->depth == 1 && ch == '[' && (!strcmp(r->key, want) || !strcmp(r->key, also))) r->state = 1;
    r->depth++;
    if (r->state == 1 && r->depth == 3 && ch == '{') { r->obj[0] = ch; r->objn = 1; r->too_long = 0; }   // a plane begins
} else if (ch == '}' || ch == ']') {
    r->depth--;
    if (r->state == 1 && r->depth == 2 && ch == '}') {                                                   // and ends
        plane_t p;
        r->obj[r->objn] = 0;                   // strtod reads up to something that is not a digit
        r->total++;
        if (!r->too_long && parse_plane(r, &p)) keep(r, &p);
    } else if (r->state == 1 && r->depth == 1) r->state = 2;   // the list is over
}
```

The reader's memory, about 10 KB, static (too much for a task's stack):

```c
#define SKY_OBJ 2048             /* the longest object kept, ADS-B Exchange writes ~1 KB */
typedef struct {
    int      fr24, fr24_pace;
    float    home_lat, home_lon;
    int      hide_ground;
    uint32_t now, asked;         /* the positions' time while it reads; when the request went out */
    uint32_t fix0;               /* FR24: the time of the answer's first position, s since 2000 */
    uint32_t made;               /* the free networks: the answer's "now", the server's ms (low 32 bits) */
    int      made_have;
    char     num[24];            /* "now"'s digits as they come */
    int      numn;
    int      state;              /* 0 looking for the list, 1 in it, 2 past it */
    int      depth, in_str, esc, keyn;
    char     key[12];            /* the last string at the top level: the name of what follows */
    char     obj[SKY_OBJ];
    int      objn, too_long;
    plane_t  got[SKY_MAX];
    int      n, total;
} sky_reader_t;
```

Details that matter:

- `strtod` reads until something that is not a number, and the end of the buffer is not that: put a 0 at the object's end.
- `\u` escapes in a name become `?`. Nothing the radar shows needs more than ASCII. The Nominatim reader (`json_str`) does turn
  `\uXXXX` into UTF-8: town names need it.
- `total` is how many the answer had before the cap, for the screen.
- A `sky_t` is 48 planes, about 8 KB. The firmware has three of that size: the sky, the reader's `got[]`, and the screen's copy
  (`memcpy` under the lock, once a frame). The screen never reads the sky itself.
- Answers from the demo go through the same reader: `demo_json` writes the same ADS-B Exchange text (12 planes in 6 KB). So
  the demo tests the parser, and the preview needs no network.
- The reader was checked over real answers fed in pieces of 1, 7, 512 and 4096 bytes. [measured]

## 5. When a position was true: the server's clock

Measured on 12 minutes of real sky round a busy airport, replayed through the board's own code (0.16.2):

- A request takes 2.5 to 4 s, and the server makes its answer somewhere inside that, never at the same point. Stamped with
  the moment the request went out (0.4 to 0.16.1), each new position came a hundred metres or so ahead or behind, and a plane
  sped up and slowed down with every answer. [measured on recorded answers]
- Now each answer is dated by the server's own clock. The free networks write `now`: in milliseconds AFTER the list on
  adsb.lol and adsb.fi v3, in seconds with a fraction BEFORE the list on adsb.fi's old v2 (`server_ms` takes both: a point
  or under 12 digits means seconds). FR24 dates each position (`timestamp`, ISO): the answer is placed at its newest one,
  each position as much older as its stamp says.
- `sky_t.clk` keeps our ms clock less the server's, the least an answer ever took: the quickest answer is the truest. 1 ms is
  added an answer for the two clocks' drift, and it starts again when one is over a minute off (another server, a clock set
  again). Only the low 32 bits are kept: only differences count.
- Without a server clock, `seen` stays the moment the request went out, and `heard` is the moment the answer came, so a slow
  handshake does not age the planes.

Result per plane-minute, 0.16.1 against 0.16.2: steps back of a pixel 6.6 to 0.6; the worst frame 600 m off the way it flew
to 17 m; drawn 289 m from where it is on average to 54 m. At FR24's pace the worst frame went from 2.3 km to 17 m.
[measured: real answers recorded, replayed on the PC]

## 6. The fetch loop

```
for (;;) {
    home_find();          // first: the planes are asked round the new home
    copy cfg and secrets under the lock; sky_hide_ground(&s_sky, !c.ground);
    geocode();            // home's country before anything is asked round it (apis/nominatim.md)
    refused home -> no map, no planes, the screen says why
    map_service(&c);      // a new look or range shows its map before the planes come
    no WiFi set, or the one set gone over 20 s -> the demo sky, the screen says DEMO; look again every 1 s
    WiFi up -> build the URL; read as it comes; merge
               watch_sky, light_sky (the LED)
               fr24 ? every = 20000 : 5000; on 429 every *= 3
               if (status != 429) watch_lookup();
               update_check();   alive_report();   wx_service();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(every));   // a new setting cuts the wait short
}
```

The free server choice: `next` is -1 at first (the setting `c.server`). After a failed or strange answer the next one is tried
next time; after a good one it stays there:

```c
if (!fr24 && n < 0) next = (server + 1) % FREE_SERVERS;   // that one failed: the next one next time
else if (!fr24) next = server;
if (fr24) every = FR24_EVERY_MS;
if (status == 429) every *= 3;            // too many requests: slow down
```

- The demo and the real sky never mix: on any switch `s_sky.n = 0` under the lock.
- What went wrong is said on the screen in the alarm colour: `ADSB.FI HTTP 429`, `FR24 NO ANSWER`, `STRANGE ANSWER`,
  `CONNECTING`. The LED's NO DATA lights while the WiFi set is not there or no answer comes.
- The URL is logged once each time it changes. No key is ever in it.

## 7. Home and privacy

- The plane servers and Nominatim's reverse lookup get `vague(lat)`, `vague(lon)`: 0.01 degree, about 1.1 km north to south.
  The weather gets 0.1 degree, about 10 km. [built, the URLs are logged]
- The exact point stays on the board and draws the scope.
- What the rounding does NOT cover, say so in a new project: a town or postcode typed for the search goes to Nominatim as
  typed, and the tile requests name the tiles round home. A tile at the higher zooms is a few km wide, so the tile server
  learns the neighbourhood, about what the rounding gives away.
- Until a home is set the board sits at a fixed public demo home from the firmware's config.
- `geo_offset` is the flat projection the map shares. "Good to a fraction of a percent at 300 km":

```c
void geo_offset(float lat0, float lon0, float lat, float lon, float *east, float *north)
{
    float dlon = lon - lon0;
    if (dlon > 180.0f) dlon -= 360.0f;
    if (dlon < -180.0f) dlon += 360.0f;
    *east = dlon * 111.320f * cosf((lat0 + lat) * 0.5f * D2R);
    *north = (lat - lat0) * 110.574f;
}
```

The asked radius is a bit more than the scope, so planes on their way in show on the rim first: `range * 1.6` for the disc,
`* 2.4` for the square map, which reaches into its corners.

## 8. Planes on the ground

Asked for by users next to airports. SKY_MAX keeps the nearest 48, and parked planes are the nearest of all: with 60 parked and
20 flying, 0 flying were shown. [PC test] The fix is in the reader, because filtering after the cap is too late. Flashed and
checked on a 3.5" board. [measured]

```c
// parked, taxiing, or a car of the airport's own
static int on_ground(const plane_t *p) { return p->ground || p->kind == KIND_GROUND; }

static void keep(sky_reader_t *r, const plane_t *p)
{
    float x, y;
    geo_offset(r->home_lat, r->home_lon, p->lat, p->lon, &x, &y);
    float d = x * x + y * y;
    // one on the ground left out is kept only to say it has landed: every plane in the air goes
    // before it. 1e9 km squared is further than anywhere on Earth
    if (r->hide_ground && on_ground(p)) d += 1e9f;
    ...
}

// in sky_read_end
if (r->hide_ground && on_ground(&r->got[i])) drop(s, r->got[i].icao);   // landed: gone now, not SKY_FORGET later
else merge(s, &r->got[i], r->now);
```

Why not throw a ground plane away at once: a plane that landed in view would hang on the screen for 30 s as if still flying.
Keeping it, last in the queue, lets its first ground report remove it.

## 9. Between the answers: the guess

Planes are asked every 5 s (20 s on FR24) and the screen draws 10 or more frames a second: each frame a plane is flown on by a
guess. The 0.4 guess, in `code/flight_sky.c`:

```c
void plane_now(const plane_t *p, uint32_t now, float *x, float *y)
{
    float dt = (float)(int32_t)(now - p->seen) / 1000.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 20.0f) dt = 20.0f;                      // no further than 20 s on a guess
    float km = p->has_spd && !p->ground ? p->gs * 1.852f / 3600.0f * dt : 0.0f;
    *x = p->x + km * sinf(p->track * D2R);
    *y = p->y + km * cosf(p->track * D2R);
}
```

What changed in 0.16.2 (`plane_now(sky, plane, now, ...)` and `plane_track`):

- How long a guess goes: 20 s on the free networks (`SKY_GUESS`), 60 s at FR24's pace (`SKY_GUESS_FR24`), whose answers come
  20 s apart and a little more, and one that fails puts the next 20 s further off. With a 20 s cap every FR24 plane stood
  still for a moment before each answer and then jumped. A guess may also go back up to 20 s: the radar's beam asks where a
  plane was when it passed. Nothing on the ground moves.
- A turn: `turn`, tenths of a degree a second, from the last two positions (2 to 60 s apart; the same position again keeps
  the turn it had). Over 12.7 degrees a second no aeroplane turns: a track that came wrong, or not one flight, so no turn.
  The guess flies the curve for `ARC_S` 5 s and `TURN_MAX` 90 degrees at most, then straight on. A longer arc turned the
  noses more than the planes turned: a turn is often over by the time two positions show it.
- The slide: when the next position comes, a plane slides from where the guess had it onto it, instead of jumping
  (`gx`, `gy` in 25 m units, `gh` for the nose). Eased at both ends, `SKY_GLIDE` 2 s, or longer where two would be faster
  than the plane flies (`1.5 * distance / speed`), at most 12 s: a light one circling does not swim half a kilometre
  sideways.
- A plane whose guess ran out (it stood still on the screen) jumps. So does one more than about 3 km off the guess (127 units
  of 25 m): lost and found, not one turn.
- The LOST option greys a plane after 12 s without a fresh position, 32 s at FR24's pace: 12 greyed every FR24 plane between
  two answers.
- Measured on recorded real answers: the planes' own `track_rate` and `roll` (adsb.fi sends them for many planes) were tried
  as the turn and did no better than two fixes. [PC test on recorded answers]

The picked plane also draws where it will be in a minute: `km = gs * 1.852 / 60` along its track.

## 10. Trails

16 old positions a plane, kept as int16 in 10 m units (about +-327 km). A point goes in when the plane moved over 200 m. A jump
of over 30 km clears the trail: lost and found again, the old line would cross the map. A plane farther than 327 km from home
clears it too, since 0.14: a point there wrapped to the other side of the int16 and its line crossed the scope (250 NM is 463
km). Moving home clears all trails, they were measured from the old home.

```c
if (dx * dx + dy * dy > 900.0f) p->tn = 0;                          // jumped 30 km
else if (fabsf(p->x) > 327.0f || fabsf(p->y) > 327.0f) p->tn = 0;   // past what 10 m units hold in 16 bits
else if (dx * dx + dy * dy > 0.04f) {                               // moved more than 200 m
    if (p->tn == TRAIL_N) { memmove(tx, tx + 1, ..); memmove(ty, ty + 1, ..); p->tn--; }
    p->tx[p->tn] = (int16_t)lroundf(p->x * 100.0f);
    p->ty[p->tn] = (int16_t)lroundf(p->y * 100.0f);
    p->tn++;
}
```

`merge` copies the new plane over the old slot and puts the trail, `first` and the rest back after. When the list is full, a
nearer plane takes the farthest one's slot (`slot()`).

Drawing cost is part of the data design: the trails were once walked whole in every screen strip, 62 ms a frame on the 3.5"
once all trails were full, about 80 s after boot. Now `scope_frame` turns them into screen points once a frame
(`spot_t.tx8/ty8`), 7 ms. A PC profile that stops before the trails fill never sees this. [measured]

## 11. The watch list

Up to 8 ids, as callsign (EZY12AB), registration (G-EUPT) or ICAO hex (4CA7B4). State per plane: UNKNOWN, GONE, GROUND, AIR.
Kept in NVS as the typed string, not in the settings struct.

Two ways a plane is seen:

- In the sky round home: `watch_sky` after each answer. New in range: `<ID> IN RANGE`, and the screen picks it unless one is
  picked.
- Anywhere in the world: one lookup every 15 s, the one asked longest ago that is not in range. 8 planes: each looked at every
  2 minutes. Read with the planes' own `sky_reader_t`, with no merge. Asked of the OTHER free network than the planes', on FR24
  too. The URLs and what each server answers are in `apis/adsb-lol.md` and `apis/adsb-fi.md`. [measured]

Which kind an id is, by shape, asked in this order and learnt once found:

```c
static int kinds(const watch_t *w, uint8_t *k)
{
    if (strchr(w->id, '-')) { k[0] = WATCH_REG; return 1; }
    if (hex6(w->id)) { k[0] = WATCH_HEX; k[1] = WATCH_CALLSIGN; return 2; }   // ACA123 is a callsign too
    k[0] = WATCH_CALLSIGN;
    k[1] = WATCH_REG;                                                         // N123AB has no dash
    return 2;
}
```

Matching against planes in hand ignores dashes and case (`same_id`). Only the question to the server needs the dash.

Take-off and landing are not read from anywhere, they are state changes:

```c
static int heard(watch_t *w, const plane_t *p, int near, uint32_t now, char *say, int cap)
{
    int was = w->state, came = near && !w->near;
    w->state = p->ground ? WATCH_GROUND : WATCH_AIR;
    w->heard = now;
    w->near = (uint8_t)near;
    if (came) { snprintf(say, cap, "%s IN RANGE", w->id); return WATCH_NEWS_RANGE; }
    if (w->state == WATCH_AIR && was != WATCH_AIR) { snprintf(say, cap, "%s IS FLYING", w->id); return WATCH_NEWS_UP; }
    if (w->state == WATCH_GROUND && was == WATCH_AIR) { snprintf(say, cap, "%s LANDED", w->id); return WATCH_NEWS_DOWN; }
    return WATCH_NEWS_NONE;
}
```

- Not heard is not landed. A flight over the ocean drops out of reach for a while, so a plane is GONE only after 30 minutes
  unheard (`WATCH_GONE_MS`), and it does not say IS FLYING again when it comes back.
- The limits, read from this code: a plane first seen in the air says IS FLYING, it may have been flying for hours. One that
  lands and switches its transponder off before a lookup catches it on the ground never says LANDED, it goes GONE without a
  word. With 8 planes a lookup comes round every 2 minutes, so a short ground report can be missed.
- The news ranks RANGE over UP over DOWN (`WATCH_NEWS_*`), for the LED and the toast.
- After a 429 the lookup is skipped that round; after any other non-200 the same one is asked again next time.
- Portable and PC-tested (`watch.c`), then run on a board against the real networks. [measured]

## 12. Emergency squawks and silhouettes

```c
sos |= p->squawk == 7500 || p->squawk == 7600 || p->squawk == 7700;   // hijack, radio, emergency
```

- The plane's colour blinks: `((now / 400) & 1) ? C_ALARM : C_FOCUS`, in `alt_colour`, ahead of the height colours.
- The panel says `7500 HIJACK`, `7600 RADIO LOST`, `7700 EMERGENCY`; the list shows the code where the kind would be.
- The LED, where there is one, blinks red: 120 ms lit of every 250 ms, first in the order SOS, WEATHER, IN RANGE, TAKE-OFF,
  LANDING, NEAR, NO DATA. Any touch puts it out. A squawk checks only the planes in the sky round home.
- All in `core/light.c`, whole in `code/flight_light.c` (0.4, before the WEATHER row). The firmware says what happened
  (`light_event`, `light_hold`, `light_sky`) and asks each frame what to show. [built, PC-tested; checked by eye with SET,
  LED, TEST]

ADS-B emitter category to kind (`kind_of_category`):

- A1 light: LIGHT. A2 small: BIZJET. A3, A4 large: JET. A5 heavy: HEAVY. A6 high performance: FAST. A7 rotorcraft: HELI.
- B1 glider: GLIDER. B2 lighter than air: BALLOON. B4 ultralight: LIGHT. B6 unmanned: DRONE. Any other B: LIGHT.
- Any C: GROUND. None: JET.

FR24 has no category, so the ICAO type decides (`kind_of_type`), by prefix:

- HEAVY: B74 B77 B78 B76 A33 A34 A35 A38 IL9 MD1 C5 C17 A40
- HELI: EC20 EC25 EC30 EC35 EC45 EC55 EC75 AS32 AS35 AS50 AS55 AS65 R22 R44 R66 H160 B06 B407 B412 B429 A109 A119 A139 A169
  A189 S76 S92 MI8 NH90
- LIGHT: C1x and C2x (C150, C172, C182), P28, SR2, DA4
- anything else JET

The shapes, seen from above, nose up, a few convex polygons each, turned by the track, in `ui_scope.c`:

```c
typedef struct { int8_t n; float xy[14]; } part_t;
static const part_t JET[] = {
    { 7, { 0, -1.0f, 0.10f, -0.82f, 0.10f, 0.80f, 0.05f, 0.98f, -0.05f, 0.98f, -0.10f, 0.80f, -0.10f, -0.82f } },
    { 4, { -0.08f, -0.22f, -0.98f, 0.26f, -0.98f, 0.38f, -0.08f, 0.12f } },
    { 4, { 0.08f, -0.22f, 0.98f, 0.26f, 0.98f, 0.38f, 0.08f, 0.12f } },
    { 4, { -0.06f, 0.64f, -0.40f, 0.88f, -0.40f, 0.98f, -0.06f, 0.86f } },
    { 4, { 0.06f, 0.64f, 0.40f, 0.88f, 0.40f, 0.98f, 0.06f, 0.86f } },
};
static const part_t BIZ[] = {
    { 6, { 0, -1.0f, 0.08f, -0.8f, 0.08f, 0.85f, 0, 0.98f, -0.08f, 0.85f, -0.08f, -0.8f } },
    { 4, { -0.07f, -0.05f, -0.80f, 0.28f, -0.80f, 0.38f, -0.07f, 0.22f } },
    { 4, { 0.07f, -0.05f, 0.80f, 0.28f, 0.80f, 0.38f, 0.07f, 0.22f } },
    { 4, { -0.08f, 0.40f, -0.22f, 0.40f, -0.22f, 0.62f, -0.08f, 0.62f } },   /* engines at the tail */
    { 4, { 0.08f, 0.40f, 0.22f, 0.40f, 0.22f, 0.62f, 0.08f, 0.62f } },
    { 4, { -0.38f, 0.86f, 0.38f, 0.86f, 0.38f, 0.96f, -0.38f, 0.96f } },      /* T-tail */
};
static const part_t LIGHT[] = {
    { 5, { 0, -0.95f, 0.10f, -0.78f, 0.08f, 0.92f, -0.08f, 0.92f, -0.10f, -0.78f } },
    { 4, { -1.0f, -0.40f, 1.0f, -0.40f, 1.0f, -0.18f, -1.0f, -0.18f } },      /* straight wing */
    { 4, { -0.38f, 0.72f, 0.38f, 0.72f, 0.38f, 0.88f, -0.38f, 0.88f } },
};
static const part_t FAST[] = {
    { 6, { 0, -1.0f, 0.09f, -0.7f, 0.09f, 0.95f, 0, 1.0f, -0.09f, 0.95f, -0.09f, -0.7f } },
    { 3, { 0.05f, -0.45f, 0.05f, 0.70f, 0.78f, 0.75f } },                     /* delta */
    { 3, { -0.05f, -0.45f, -0.05f, 0.70f, -0.78f, 0.75f } },
};
static const part_t HELI[] = {
    { 7, { 0, -0.55f, 0.22f, -0.38f, 0.24f, 0.10f, 0.10f, 0.30f, -0.10f, 0.30f, -0.24f, 0.10f, -0.22f, -0.38f } },
    { 4, { -0.05f, 0.25f, 0.05f, 0.25f, 0.04f, 1.0f, -0.04f, 1.0f } },        /* tail boom */
    { 4, { -0.20f, 0.88f, 0.20f, 0.88f, 0.20f, 0.96f, -0.20f, 0.96f } },
};
static const part_t GLIDER[] = {
    { 5, { 0, -0.85f, 0.06f, -0.7f, 0.04f, 0.95f, -0.04f, 0.95f, -0.06f, -0.7f } },
    { 4, { -1.0f, -0.32f, 1.0f, -0.32f, 1.0f, -0.20f, -1.0f, -0.20f } },
    { 4, { -0.28f, 0.84f, 0.28f, 0.84f, 0.28f, 0.92f, -0.28f, 0.92f } },
};
static const part_t DRONE[] = {
    { 4, { -0.75f, -0.65f, -0.65f, -0.75f, 0.75f, 0.65f, 0.65f, 0.75f } },
    { 4, { 0.65f, -0.75f, 0.75f, -0.65f, -0.65f, 0.75f, -0.75f, 0.65f } },
};
static const part_t GROUND[] = {
    { 4, { -0.5f, -0.8f, 0.5f, -0.8f, 0.5f, 0.8f, -0.5f, 0.8f } },
};
/* size in px, times the screen's plane size: JET 9, LIGHT 6.5, BIZJET 7.5, HEAVY 11.5 (JET's parts),
 * FAST 8, HELI 7 plus a ring for the rotor, GLIDER 8, BALLOON a disc of 5, DRONE 5, GROUND 4 */
```

Turning: `x' = x + (px * cos h - py * sin h) * s`, `y' = y + (px * sin h + py * cos h) * s`.

Colour says height, in every theme (`alt_colour`): an emergency blinks; on the ground or no height: dim; under 10000 ft: the
warm colour; under 25000: the theme's colour; above: white. A climb or a descent of 200 ft a minute or more draws an arrow in
the panel. With heights in metres the legend reads <3KM and <7.6KM; the colour bands stay at 10 000 and 25 000 ft.

## 13. The planes relayed to the page

The site's radar page runs the same `core/` in WebAssembly. It never asks the plane servers itself: while the page says
`OWLOS SKY ON` (every half minute), each answer goes out over the USB console, one line a plane in the free networks' own
words, so the page's `sky_read` takes them as they are and no CORS question comes up:

```
OWLOS SKY BEGIN <server>
OWLOS PLANE {"hex":"4b1805","flight":"...","r":"...","t":"...","category":"A3","lat":..,"lon":..,"alt_baro":..,"gs":..,
             "track":..,"baro_rate":..,"squawk":"7000","seen_pos":..,"nav_altitude_mcp":..,"nav_heading":..,"ias":..,
             "mach":..,"wd":..,"ws":..}
OWLOS SKY END <total>
```

- A line a plane: a list in one piece is more than the 3.5" has to spare, and the log may fall between lines.
- A page that went away stops it by itself: the relay runs 60 s past the last `SKY ON`. A passed time is let go, or 24.8 days
  later the 32-bit milliseconds wrap round and every answer goes out for good.
- A relay of FR24 must say so (`OWLOS SKY BEGIN FLIGHTRADAR24`): the page then flies its planes at FR24's pace, or they froze
  39 to 84 s a run. [PC test]
- The page fetches its own map tiles and its own weather from the browser.

## 14. Adding a source to this design

1. Add its URL builder, and round home with `vague()` first.
2. Add its field function next to `adsbx_field`, one line a field, in its own units converted to feet, knots, ft/min.
3. If the list is not `ac`, `aircraft` or `data`, teach `sky_read` the name. Mind whether a plane is an object or an array.
4. Date its positions: a server clock in the answer, or each position's own stamp (section 5).
5. Decide `every` from its limit, and back off on 429. Find out what its error answers cost (adsb.fi counts them).
6. Say what the source needs said: name, link, licence, on the screen or on the site.
7. Write its test answer into the preview before asking the real server.

## Traps

- Next to an airport no flying plane shows: parked planes are the nearest and took all 48 places. Filter while reading,
  ground planes last in the queue. [PC test], fixed [measured]
- A plane on the ground parses as height 0 or is lost: `alt_baro` is the string `"ground"`. Read number and string. [built]
- A callsign does not match: `flight` is padded with spaces. Trim the end. [built]
- Numbers run on into the next bytes: `strtod` on an unended buffer. Put a 0 at the object's end. [built]
- Planes look older after a slow fetch: position stamped at the answer. `seen` from the request or the server's clock,
  `heard` from the answer. [built]
- Planes surge and crawl backwards with each answer: positions stamped at the request, while the server answers somewhere
  in a 2.5 to 4 s request. Date by the server's clock. [measured on recorded answers]
- On FR24 planes stand still, then jump: a 20 s guess against answers 20 s apart and more. Guess 60 s at that pace. [PC test]
- A line across the whole map: a plane lost and found far away. Clear the trail on a 30 km jump. [built]
- A trail across the scope from a far plane: int16 10 m units wrap past 327 km. Clear it there. [built]
- Trails from the old home: trail is in km from home. Clear trails on `sky_home`. [built]
- Fast at boot, 62 ms slower a frame after 80 s: trails walked in every strip, and a PC profile stopped before they filled.
  Once a frame into screen points: 7 ms. [measured]
- Demo planes among real ones: two producers of one list. Clear the sky on every switch. [built]
- A test that measures only jumps on made-up paths missed a wobble the eye saw at once. Measure what the eye sees (the nose,
  sideways motion, pixel steps back) on recorded real answers. [measured]
