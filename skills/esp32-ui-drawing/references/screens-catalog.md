# The screens, project by project

A catalog of every page, what is on it, its numbers, and where the code is. Use it to borrow a page. Status: the
pages run on the boards named; the e-paper 4.2" under WiFi Radar and the ILI9486 under WiFi Radar are preview-only
(the ILI9486 runs under Air Quality, the 4.2" under Flight Radar). How each part is built: radial-dials.md,
data-screens.md, e-paper-design.md, touch-ux-and-controls.md.

Layout constants are macros from the screen size, so one source serves all sizes. A number below with "320:" is what
the macro comes to at 320x240 (layouts-by-screen-size.md).

## Flight Radar (core/ui_scope.c, ui_pages.c, ui_wifi.c, ui_about.c, ui_weather.c, ui_paper.c)

Boards: 2.8" CYD 320x240 and its kin, 3.5" CYD 480x320, round 240x240, RPi LCD 480x320 without touch, 4.3" 480x272,
the 5", 7" and 10.1" scaled ones, the 4.2" and 5.79" e-paper. Views: `VIEW_MAIN, VIEW_LIST, VIEW_SETUP, VIEW_CAL,
VIEW_WIFI, VIEW_KEYS, VIEW_ABOUT`. Looks on MAIN: RADAR, MAP, CLASSIC, WEATHER (a button top right, the second in the
round's menu row; paper skips MAP).

### MAIN, the RADAR look

Left the scope, right the panel. Geometry (`ui_int.h`):

```c
#define SR   (GFX_H / 2 - 12)   /* the rim, ticks go in from it. 320: 108, 480: 148 */
#define SCX  (SR + 6)           /* 320: 114, 480: 154 */
#define SCY  (GFX_H / 2)
#define PX   (2 * SCX)          /* panel. 320: 228, 480: 308 */
#define PW   (GFX_W - PX - 1)   /* 320: 91, 480: 171 */
#define RL   (SR - 12)          /* compass letters, the band between rim and ring */
#define PR   (SR - 20)          /* the range ring: range_km lands here */
#define RQ   (SR + 6)           /* the disc and its glow fit in this radius */
#define TX   (PX + 6)
#define TX2  (PX + PW - 5)
#define BTN_Y (GFX_H - 38)
#define BTN_H 32
#define BTN_W ((PW - 13) / 3)
```

What is on the scope, back to front: background, glass by radius, range ring and rim with their glow, ticks every 5
degrees (long every 30, medium every 10), dashed quarter rings, the cross along the compass and fainter diagonals, the
beam's 90 degree afterglow and its bright edge, compass letters N E S W (smooth font, N in focus) and numbers 30, 60..
(pixel font), ring values on a diagonal, home (a small ring and dot), trails, planes, label cards, `(c) OpenStreetMap`
when a map shows, the gesture marks.

Around it: four corner brackets, a hatched bar top and bottom, top left the town and how the sky is (`ADSB.LOL 3S`, `NO
WIFI` in the alarm colour), top right the look button and `NORTH UP` or `UP 090*`, bottom left the height legend (four
dots: GND, <10K, <25K, HIGH, or <3KM, <7.6KM in metres), bottom right the range and how the fingers work the scope (`DRAG:
ZOOM`, `RIM: TURN`). A gear under the scope opens SET's RADAR tab; three dots in the panel's corner take the panel away.

Panel, nothing picked (`panel_overview`): title `OWLOS FLIGHTS` with hatch, the count in FONT_LARGE with `AIRCRAFT` and
`IN 50 KM`, the country, the point, `NEAREST`, then rows 15 px high, as many as fit (`NEAR_N = 6 + (GFX_H - 240) /
15`): height dot, callsign clipped at 44 px, distance. Buttons HUE LIST SET. No planes: `SKY IS EMPTY` or `WAITING`.

Panel, a plane picked (`panel_plane`): `TRACKING` + kind, callsign in FONT_MEDIUM, type and registration, route if known
(FR24, `ORIG > DEST`), height big with its unit above, up or down triangle with the rate, or `LEVEL`, speed and track,
distance and bearing, squawk (words in the alarm colour for 7500 7600 7700). Buttons NEXT WATCH X, WATCH lit while it is
on the list. `NO CALLSIGN`, `ON GROUND`, `NO DATA`, `SPEED ---` stand in for what is not known.

RADAR options (SET, RADAR tab, all off by default): speed vectors, the beam stepping planes as it crosses them, a clock,
the flight's block and autopilot lines in the label, lost planes dimmed, a conflict mark, lowest and highest heights shown
(P, M on the bench boards).

### MAIN, the MAP and CLASSIC looks

The whole screen, north up, home in the middle, a pin on home. No rim, so no turning: `view_up` is 0 and a drag anywhere
zooms. Grid of meridians and parallels a round number of minutes apart, named at the edges on small cards. Scale bar of a
round distance 30 to 70 px long. Corners: `sq_box` lists four boxes the labels keep off. The panel slides in over the
right side from the three dots, and by itself while a plane is picked (`panel_shown`). Planes off the square become darts
at its edge up to 2.4 x the range. CLASSIC is the map in its own colours with the light look's rules
(maps-on-a-small-screen.md).

### MAIN, the WEATHER look

The next 24 hours on the radar's own instrument (radial-dials.md, data-screens.md). Three pages: `24 HOURS` (the dial),
`PRESSURE`, `TEMPERATURE` (graphs). A tap on the dial or a graph turns the page (on the round, a tap turns it and shows
the menu row), BOOT turns it on every board, the console can set it. The page is not kept: the look comes on its dial.

- Beside the dial: the weather panel (the radar's frame, title `OWLOS WEATHER`, rows, breathing warning boxes, NEXT DAYS,
  HUE LIST SET), or on 480x320 and 320x240 the column table (`wx_hud`), or with the panel away now's numbers either side
  of the dial.
- Warnings: a chip in the corner of every look (the new-version note moves under it), a tap opens WEATHER, a 5 s toast
  when one comes; on the round above the card under home; in WEATHER the panel's boxes or the column's lines.
- No forecast yet: the glass and why (`NO WIFI: NO FORECAST`, `ASKING FOR THE FORECAST`, `NO MEMORY FOR THE DIAL`).

### Round glass

The disc is the whole screen, `PX = GFX_W`, `PW = 0`, `SCX = GFX_W / 2`, `SR = GFX_W / 2 - 12`.

- A tap on nothing brings a row of four buttons under home (`MENU_W 44`, HUE, the look, LIST, SET), 5 s
  (`ROUND_MENU_MS`), tap again hides it.
- A tap on a plane: `round_card`, 150 x 56 under home, a tap on the card goes to the next one.
- Otherwise `round_note`: town and country, the point, the count, the status.
- `round_demo`: DEMO at scale 3, why, `SEND WIFI FROM THE SITE`.

### LIST

Header `FLIGHTS` + `12 IN 50 KM`, column heads (not on round), rows 22 px: silhouette (it turns by track here too),
callsign FONT_MEDIUM, type, height in its colour, speed, distance + compass word. Columns laid out on 320 and spread:
`#define LX(x) ((x) * GFX_W / 320)`. Bar: `ZOOM 50 KM`, `BACK`. A tap picks and goes back to the scope. Empty: `NOTHING
IN RANGE`.

### SETUP

Tabs on the bottom bar: VIEW, DATA, UNITS, RADAR, LED (where there is one), BACK. Row table with label, one line of what
it does, kind, tab (touch-ux-and-controls.md). Rows 40 px (32 on round, no description there).

### WIFI, KEYS

Networks strongest first, four signal bars, tag `JOINED`, `SET`, `OPEN`, a lock mark. `LOOK AGAIN`, `BACK`. Tap: keyboard
(`code-touch.md`), or joined at once if open. Round and no-touch boards send WiFi from the site instead: the demo card
says so. The same keyboard types a new home (`FIND`) and the watch list.

### ABOUT

QR on a light plate (4 px a module on 320x240, 5 on 480x320, 3 on the round), version in FONT_LARGE, what the site said,
the board, whether a key is stored, how to update when there is news, the address for who would rather type it
(first-run-flow.md).

### CAL

One cross at a time with a pulsing ring, text in the half the cross is not in, `POINT 2 OF 5`, `SCREEN WRONG? PRESS
BOOT` on boards sold with different screens.

### Paper (the 4.2" and 5.79" e-paper)

CLASSIC as a printed chart, RADAR printed, the WEATHER pages printed (the dial, the two graphs, or the card of words),
the keys' strip on every one (e-paper-design.md). Other views go the colour way, printed as the negative.

## WiFi Radar (core/ui.c, ui_radar.c, ui_views.c, ui_round.c, ui_paper.c)

Views: `VIEW_MAIN, VIEW_RADAR, VIEW_LIST, VIEW_SPECTRUM, VIEW_TARGET, VIEW_MAP, VIEW_CAL`. A swipe goes round them
(`SWIPE_VIEWS`), the header goes back.

### MAIN

Scope left, panel right. Distance from centre is signal, not metres: `rssi_frac`, -28 dBm near, -92 far, dashed rings at
-40 -55 -70 -85. Colour is security. Panel: count, `N OPEN`, `LOUDEST` rows 18 px with a colour bar, name, level, trend
arrow, a level bar under it, then the small spectrum, buttons 360 MAP HUE. Target picked: the name on up to two lines
(`split2`, UTF-8 safe), BSSID, level, WARMER, COLDER or STEADY with dB per 10 s, distance, channel and security, `BRG 123*
+-20` or `BRG ---`, a sparkline of the last minute, proximity in 13 segments.

Corner text says what up means: `UP = TURN START`, `UP = WALKING`, `UP = MAP UP`, `UP = ?`.

### RADAR

The same disc, full height: `r = GFX_H / 2 - 5`. Tap a dot: target. Tap empty: back.

### LIST

Rows 22 px, colour bar, name, a line of `CH 6 WPA2 ~4 M BRG 120`, level bar 64 px, trend arrow, level. Sort keys sit in
the header as tabs: SIGNAL, NAME, CH at `x >= GFX_W - 120`, 40 px each, the active one underlined. Scroll bar on the right
edge. Drag scrolls, tap makes it the target and opens the hunt.

### SPECTRUM

Like a WiFi analyser app. x by channel, y by dBm, -30 at the top. Each network a hump:

```c
static float hump(float rssi, float d)        // d in channels from its centre
{
    float e = fabsf(d) - 1.4f;
    return e > 0 ? rssi - 40.0f * e * e : rssi;
}
```

The 16 loudest, weakest first so loud ones lie on top, a soft fill of vlines at alpha 26 and a line at 220; a picked
channel dims the others to 70 and 10. Names on the peaks, moved up 11 px at a time when they overlap, given up after 4
tries, at most 10. A tap picks a channel: the line under the plot says who sits there. `TUNED CH 06` on the right of the
header.

### TARGET, the hunt

One network, everything big, for walking with the board in hand. FONT_HUGE level, WARMER / COLDER / STEADY in words and
an arrow (`TREND_DB 3` in 10 s), distance `~4 M`, a direction dial with `?` when there is no bearing, closeness in
segments from FAR to NEAR (the last quarter in focus), a big sparkline with dB lines, buttons 360 TURN, WALK MAP, NEXT,
CLEAR. Round: closeness is 26 segments on a 260 degree arc round the edge, the number in the middle.

Trend is a least squares slope over the last 10 s, not two points. Distance is `powf(10, (-38 - rssi) / 28)` and always
carries a `~`.

### MAP, the walk map

World in metres, fitted (`map_fit`: the walk plus trusted networks within 14 m, 4 to 60 px a metre, so one far AP cannot
shrink the map). Metre grid (1, 2 or 5 m by zoom, every 5th stronger). Path with older parts fainter, averaged over 5 s
because the solved path jitters about a metre and the person does not; the last point is not averaged, it is where you
are. A trusted network: ring of its error, glow, dot, name and `~6 M`. An unsolved one: hollow ring, `? ~6 M`. Off screen:
an arrow on the edge. You: an arrow if the heading is known, else a dot. Scale bar. `UP = YOUR FIRST STEPS L/R ?` until a
turn fixes the mirror. Not enough walk: a card `WALK SLOWLY THROUGH EVERY ROOM`, `THE MAP NEEDS 3 M OF WALK: 1.2`.

### The 360 turn

Countdown 3 s with steps in the panel (`STAND IN ONE SPOT`...), run 24 s, result 3.5 s (animation.md).

### E-paper screens

`paper_main`, `paper_target`, `paper_list`, `paper_spectrum`, `paper_map` (e-paper-design.md, code-paper.md).

## Thermo Relay (core/ui_main.c, ui_graph.c, ui_pages.c)

320x240 only, fixed numbers:

```c
#define DCX 104   /* dial centre */      #define DCY 101    #define DR 88
#define PX  212   /* target panel */     #define PW  105
#define PLUS_Y 20   #define KEY_H 44     #define SETV_Y 66  #define MINUS_Y 102
#define INFO_Y 150  /* relay box */      #define BAR_Y 206  #define BAR_H 31  #define HEAD_H 18
```

- MAIN: the dial (radial-dials.md). Right: TARGET with + and -, the relay box with the last hour drawn small. Bar: MODE,
  GRAPH, SETUP.
- MODE: three rows with an icon drawn from primitives (power, flame, snowflake), a name, two lines of what it does. Also
  the first question after power on: `WHAT IS ON THE RELAY?`.
- GRAPH: 1 h, 6 h, 24 h (data-screens.md).
- ALARM help: what happened, what the relay does now, what to check, RESET ALARM where needed.
- SETUP: rows like Flight Radar's.

## Air Quality (core/ui_home.c, ui_hud.c, ui_meteo.c, ui_graph.c, ui_pages.c)

480x320 only:

```c
#define TAB_Y 292  #define TAB_H 28  #define HEAD_H 22  #define BODY_Y1 (TAB_Y - 2)
#define HUD_R 102  #define HUD_AX 122  #define HUD_PX 358  #define HUD_Y 144
#define CARD_Y 254 #define CARD_H 34
```

- Tab bar along the bottom (AIR, METEO, GRAPHS, SENSORS, SETUP) with a marker that slides from tab to tab, a dot on a tab
  that needs looking at. Pages slide sideways in 280 ms.
- AIR (home): two big HUDs, air on the left, pressure on the right, three cards under them (temperature with dew point,
  humidity, light), each with a mini ring, value, a word and an arrow. A `DEMO, MADE-UP AIR` pill in the alarm colour
  whenever the air is simulated.
- METEO: the big barometer, the forecast (Zambretti, with a weather icon drawn from discs and triangles, `BAROMETER
  FORECAST, 12 H`, `NO WIND, NO SEASON IN IT`), how fast it moves, the last day. With no barometer: `NO BAROMETER`, what
  was tried, what to plug in and on which pins.
- GRAPHS: every channel a line, 1H 24H 7D. DETAIL: one channel, its HUD, a big plot, a cursor by finger.
- SENSORS: a card each: state, what it reads, reads ok and failed. The last card is the board: version, `N fps, N ms a
  frame, heap N KB`, night state. The instrument reports on itself.
- SETUP: palette with its four colours shown, units, offsets, `SAVED BY ITSELF` in the header.

What a HUD is made of, outside in: radial-dials.md. Code: `code-hud.md`.
