# Themes and palettes: the catalog, and how one is built

Every colour of the four firmwares, how a theme is built, the night palette, the light look on a map, the paper
inks, the weather's colours, and the ground every page sits on. Values are `RGB(r, g, b)`, stored as `0x00RRGGBB`.
Status: the five themes are drawn on real panels (2.8" CYD, 3.5" CYD, round C3, Air Quality's ILI9486) and looked
at by a person (M). The 5.79" and 4.2" paper palettes are on the glass too (M).

How sure: (M) seen on a real board, (P) built and seen in the preview or read in the code, (R) reasoned.

## The rule

Every colour has ONE meaning, and the meaning is the same in all five themes. A theme changes what a colour looks
like, never what it says. So the code never writes a colour number in a page: it writes `C_PRIM`, `C_FOCUS`, and
those read through one pointer:

```c
typedef struct {
    const char *name;
    uint32_t bg0, bg1, glass, panel;   /* the ground, top and bottom, the disc's glass, panels */
    uint32_t prim, quiet, focus, dim;  /* the instrument, its quiet parts, what you read first, secondary text */
    uint32_t low, alarm, home;         /* the meanings of THIS project (Flight Radar's), see below */
} theme_t;

extern const theme_t UI_THEME[UI_THEMES];
extern const theme_t *TH;
#define C_BG0    (TH->bg0)
#define C_PRIM   (TH->prim)   /* and so on, one macro a field */

void ui_set_theme(ui_t *u, int theme)
{
    u->theme = ((theme % UI_THEMES) + UI_THEMES) % UI_THEMES;
    TH = &UI_THEME[u->theme];       // careful: TH is global, all the C_* colours read from it
    if (s_bg_theme != u->theme) { build_background(); s_bg_theme = u->theme; }
}
```

The background tables hold colours, so they are rebuilt when the theme changes, not per frame.

Trap, half a theme (R): `TH` is a global pointer and page code reads it through macros. Changing the theme in the
middle of a frame gives strips of two themes. Change it only in `ui_frame` or `ui_touch`, never in a strip, and
rebuild the background tables there.

Five themes: ARCTIC, PHOSPHOR, AMBER, CRIMSON, SYNTH. `UI_THEMES` is 5 in all four projects. HUE is a button on
the main page (a long press on the round glass of the WiFi Radar). The name shows as a toast, the choice is saved 3 s
later. Paper has one palette and no HUE.

## The five, the part all projects share

The eight shared fields are the same numbers in Flight Radar, WiFi Radar, Thermo Relay and Air Quality (and in
FusionEdge's screen, which took them).

- ARCTIC: bg0 3, 8, 18. bg1 9, 22, 46. glass 16, 42, 82. panel 8, 26, 54. prim 92, 225, 255. quiet 46, 130, 178.
  focus 234, 247, 255. dim 120, 160, 196.
- PHOSPHOR: bg0 2, 9, 4. bg1 6, 24, 10. glass 10, 46, 20. panel 6, 28, 12. prim 90, 255, 130. quiet 40, 150, 70.
  focus 225, 255, 230. dim 130, 190, 145.
- AMBER: bg0 10, 5, 0. bg1 26, 14, 2. glass 46, 26, 4. panel 28, 16, 2. prim 255, 180, 40. quiet 160, 100, 20.
  focus 255, 244, 214. dim 200, 160, 110.
- CRIMSON: bg0 10, 1, 4. bg1 26, 5, 10. glass 46, 10, 18. panel 28, 6, 12. prim 255, 80, 100. quiet 160, 40, 60.
  focus 255, 226, 230. dim 205, 140, 150.
- SYNTH: bg0 6, 2, 16. bg1 20, 6, 38. glass 34, 12, 64. panel 20, 8, 40. prim 205, 125, 255. quiet 110, 60, 170.
  focus 250, 238, 255. dim 170, 145, 205.

What the shared fields mean:

- `bg0`, `bg1`: the ground, darker at the top. The screen is never pure black: a tint of the theme's hue plus the
  vignette gives depth.
- `glass`: the scope's disc, mixed in by radius (dark in the middle, lighter to the ring).
- `panel`: panels and button fill, between bg1 and glass.
- `prim`: the instrument: rings, rim, scales, curves.
- `quiet`: the instrument, quieter: ticks, grid, frames, column heads.
- `focus`: what you read first: the numbers, titles, the picked thing, the beam's edge, now. Near white with the
  theme's tint.
- `dim`: secondary text, the names of values.

## The project's own meanings

Same field, same meaning, in all five themes.

### Flight Radar (`low`, `alarm`, `home`)

`low` planes under 10 000 ft, `alarm` emergency squawks and errors, `home` the pin on home and nothing else.

- low: ARCTIC 255, 184, 72. PHOSPHOR 255, 200, 70. AMBER 110, 215, 255. CRIMSON 255, 205, 90. SYNTH 255, 190, 80.
- alarm: ARCTIC 255, 80, 88. PHOSPHOR 255, 90, 80. AMBER 255, 70, 150. CRIMSON 255, 255, 255. SYNTH 255, 90, 130.
- home: magenta 255, 60, 220 in ARCTIC and PHOSPHOR, lime 130, 255, 60 in AMBER and SYNTH, cyan 60, 235, 255 in
  CRIMSON. A colour nothing else in that theme has, so it differs from theme to theme: red and pink are `alarm`
  somewhere. With a dark edge on every ground (P in all five themes; the reason was a person's photo, M).

Height says the colour, the same way in every theme:

```c
uint32_t alt_colour(const ui_t *u, const plane_t *p)
{
    if (emergency(p)) return ((u->now / 400u) & 1) ? C_ALARM : C_FOCUS;   // never goes dark, only changes
    if (p->ground || !p->has_alt) return C_DIM;
    if (p->alt < 10000) return C_LOW;
    if (p->alt < 25000) return C_PRIM;
    return C_FOCUS;
}
```

The legend's words follow the unit the person picked: `GND <10K <25K HIGH` in feet, `GND <3KM <7.6KM HIGH` in
metres; the bands stay at 10 000 and 25 000 ft (M).

### WiFi Radar (`open`, `weak`)

`prim` also means a protected network, `open` an open one, `weak` broken security (WEP, WPA1).

- open: ARCTIC 255, 184, 72. PHOSPHOR 255, 200, 70. AMBER 110, 215, 255. CRIMSON 255, 205, 90. SYNTH 255, 190, 80.
- weak: ARCTIC 255, 96, 96. PHOSPHOR 255, 90, 80. AMBER 255, 80, 70. CRIMSON 200, 140, 255. SYNTH 255, 90, 130.

### Thermo Relay (`heat`, `cool`, `alarm`)

Orange only ever means heating, blue only cooling, red only an alarm.

- heat: ARCTIC 255, 150, 56. PHOSPHOR 255, 170, 60. AMBER 255, 96, 48. CRIMSON 255, 150, 50. SYNTH 255, 160, 70.
- cool: ARCTIC 96, 140, 255. PHOSPHOR 90, 180, 255. AMBER 110, 215, 255. CRIMSON 110, 190, 255. SYNTH 90, 200, 255.
- alarm: ARCTIC 255, 72, 88. PHOSPHOR 255, 80, 80. AMBER 255, 60, 150. CRIMSON 255, 236, 80. SYNTH 255, 80, 110.

### Air Quality (`lv[4]`, `alarm`)

Four levels, good, fair, poor, bad, used for rings, zones and the words. Each line: good, fair, poor, bad, alarm.

- ARCTIC: 70, 235, 170. 255, 214, 70. 255, 140, 50. 255, 64, 84. Alarm 255, 72, 88.
- PHOSPHOR: 110, 220, 255. 255, 226, 80. 255, 150, 60. 255, 70, 70. Alarm 255, 80, 80.
- AMBER: 150, 235, 120. 250, 245, 140. 255, 110, 50. 255, 60, 130. Alarm 255, 60, 150.
- CRIMSON: 110, 200, 255. 255, 236, 80. 255, 160, 50. 255, 255, 255. Alarm 255, 236, 80.
- SYNTH: 90, 235, 200. 255, 220, 90. 255, 140, 70. 255, 70, 110. Alarm 255, 80, 110.

## Why a meaning changes its hue from theme to theme

The meaning must stand out from `prim`, and `prim` is the theme's own colour. So the hue moves:

- AMBER's instrument is orange, so "low" there is cyan, and in Thermo Relay "cool" keeps blue while "heat" is pushed
  to a redder orange (255, 96, 48).
- CRIMSON's instrument is red, so its alarm cannot be red: it is white in Flight Radar, yellow in Thermo Relay and
  Air Quality, and Air Quality's "bad" level is white.
- PHOSPHOR's instrument is green, so Air Quality's "good" there is blue, not green.

A check for any new meaning: put the colour next to `prim` and `focus` of all five themes. If it melts into one of
them, pick another hue for that theme and leave the meaning alone.

## Building a sixth theme

Ratios read off the five above (reasoned from the numbers, not a rule anybody measured):

- Pick the instrument colour `prim`, at full saturation, at least one channel at 255 (or near it).
- `quiet` = about 0.55 to 0.65 of `prim`.
- `bg0` = the hue at about 3 to 7 % brightness, `bg1` = about 2.5 x `bg0`, `glass` = about 1.8 x `bg1`, `panel` =
  between `bg1` and `glass`. Keep them low: the ground must stay dark enough for additive light to read on it.
- `focus` = the hue pushed nearly to white: the lowest channel around 215 to 235.
- `dim` = `prim` with the saturation taken out, about 65 % brightness.
- Render every screen with it in the PC preview before putting it on a board (`theme-N.png` shots).

## A balanced palette: the theme's colours and one accent

On a data screen the theme's own colours carry everything, and ONE accent is kept for warnings, used only where a
warning applies (M, the weather column on the 3.5" and the 2.8", asked for after a screen in many colours looked
busy):

- values `C_FOCUS`, the names of values `C_DIM`;
- a gauge's band from the theme's dim to its bright: `gfx_mix(gfx_mix(C_GLASS, C_PRIM, 90), C_FOCUS, f)`;
- a wind rose's petals `C_PRIM`, and amber or red only on a petal where the wind would warn;
- the warning's amber (`C_LOW`) or red (`C_ALARM`) and nothing else in that colour;
- pictures of the weather keep their own colours (sun yellow, rain blue): they are pictures, not marks.

## The weather's colours, one job each

Flight Radar's WEATHER look (radial-dials.md, data-screens.md) adds colours outside the themes, each with one job,
the same on the dial, the graphs and the panel:

- How warm: `temp_colour`, blended between stops: -15 C 160, 140, 255 (frost violet). -5 C 110, 168, 255. 0 C 165,
  212, 255 (ice). 8 C 214, 236, 242. 16 C 246, 236, 196 (a pale mild). 23 C 255, 192, 88 (amber). 30 C 255, 104,
  84 (hot red). The loop, the line, their numbers. No theme colour.
- Rain: `RAIN_C` 77, 166, 255: drops, bars, the words of rain. Falling drops 140, 200, 255.
- The sun: `SUN_C` 255, 205, 80. Where it rises and sets: `DAWN_C` 255, 150, 72, not the warning's amber.
- Now: the theme's focus (the hand, the line and dot, the clock).
- Warnings: amber and red (`alert_colour`: `C_LOW` or `C_ALARM`), and nothing else.
- The air and the UV in their agencies' scales, always with the word. EEA air index, good to extremely poor: 80,
  240, 230. 80, 204, 170. 240, 230, 65. 255, 80, 80. 170, 20, 60. 150, 60, 170. WHO UV, low to extreme: 90, 200, 90.
  247, 228, 0. 248, 89, 0. 216, 0, 29. 107, 73, 200.
- The sky by the sun's height: below -12 degrees 3, 6, 18; to 48, 30, 104 at -6; to 255, 132, 56 at 0; to 40, 122,
  222 at 10; to 70, 162, 246 at 50.
- Clouds: day 244, 248, 253, night 74, 86, 114; with rain day 168, 178, 196, night 62, 72, 96; their edge the same
  colour mixed 70 of 256 toward black. Pictures' clouds 226, 234, 246 by day, 160, 176, 200 by night. The moon 238,
  242, 250 lit.
- The instrument itself (rims, grid, frames, what a thing is) keeps the theme's quieter colours.

## Palette roles shared by paper and glass

Flight Radar's printed weather draws through roles, not colours: `K_PAPER`, `K_LIGHT`, `K_DARK`, `K_INK`, `K_ALARM`,
`K_ON_ALARM`, and `wx_tone(g, x, y)` for a tone between. On paper they are the inks (a red warning inverted: black
with white words). Written for glass too: `C_GLASS`, `gfx_mix(C_GLASS, C_PRIM, 80)`, `gfx_mix(C_GLASS, C_PRIM, 170)`,
`C_FOCUS`, `C_ALARM` with white words, a tone a blend, a chequer on paper a blend on glass (P). The glass version was
the first colour take of the printed dial, and was taken out: the colour screens got their own dial instead
(radial-dials.md). The paper output stayed pixel-identical through the change (P, checked against saved previews).

## The night palette (Air Quality)

Warm and dark, no blue at all, so the board can stay on in a bedroom:

```c
const theme_t UI_NIGHT = {
    "NIGHT",      RGB(0, 0, 0),    RGB(6, 1, 0),     RGB(16, 4, 0),    RGB(10, 2, 0),
                  RGB(210, 70, 22),  RGB(95, 28, 8),     RGB(255, 150, 80),  RGB(150, 70, 32),
                  { RGB(175, 140, 40), RGB(225, 150, 35), RGB(245, 95, 20), RGB(255, 35, 20) }, RGB(255, 45, 20),
};
```

It is not switched, it is blended. `u->night` runs 0..1 and each field of the day theme is mixed toward it, so the
palette slides over a few seconds, and `TH` points at a scratch copy:

```c
static void blend_theme(const theme_t *d, float n)
{
    const theme_t *k = &UI_NIGHT;
    s_mix.name = n > 0.5f ? k->name : d->name;
#define M(f) s_mix.f = mixc(d->f, k->f, n)
    M(bg0); M(bg1); M(glass); M(panel); M(prim); M(quiet); M(focus); M(dim); M(alarm);
    for (int i = 0; i < 4; i++) s_mix.lv[i] = mixc(d->lv[i], k->lv[i], n);
#undef M
    TH = &s_mix;
}
```

- The light sensor decides, with a dead band: to night below 6 %, back above 14 %, and with patience: 8 s into the
  night, 3 s back out. A hand passing the sensor changes nothing.
- `u->night += (goal - u->night) * clampf(dt * 0.8f, 0, 1)`: frame rate does not matter.
- On that panel the backlight is wired to power, so brightness is done in the pixels:
  `gfx_bright = 256 - (int)(u->night * 140.0f)` and `gfx_pack565` scales every pixel. Runs on the board, it adds one
  `gfx_scale` a pixel only while dimmed. The background tables are rebuilt only when the key
  `(int)(night * 1000) * 8 + theme` changes.

Flight Radar has night by the clock instead, on boards whose backlight dims: SET, VIEW, NIGHT (OFF, 50, 25 or 10 %)
while the sun is 3 degrees under the horizon at home, half a degree of hysteresis, never brighter than BACKLIGHT (P,
the sun's maths checked on the PC; the night itself not yet seen on a board).

## The light look on a map (Flight Radar CLASSIC)

The map in its own colours has a light ground, so the instrument's rules flip. Light things do not show on it, cut
corners belong to the dark looks, and every mark needs a dark edge:

```c
#define CL_PAPER RGB(250, 250, 247)
#define CL_RIM   RGB(176, 181, 190)
#define CL_INK   RGB(38, 44, 60)
#define CL_DIM   RGB(92, 99, 116)
#define CL_BLUE  RGB(32, 88, 186)
#define CL_ALARM RGB(196, 36, 44)
#define INK      RGB(16, 20, 30)      /* outlines on the light ground */
```

- A plane is first drawn 1.35 x in `INK` at alpha 200, then in its colour on top: a dark edge.
- Trails are mixed toward `INK`: `gfx_mix(col, INK, 110)`.
- Legend dots get a dark disc under them.
- Cards are `paper_card` (round corners, 1 px shadow), buttons `map_button` (a pressed one sinks into its shadow by 1
  px, light 212, 228, 250 when lit, 222, 227, 234 pressed).

Trap, light marks on a light map (P): silhouettes and legend dots vanished in the CLASSIC look until each got a dark
copy 1.35 x under it, and trails were mixed toward ink. The map's own colours, and its kinds in the theme looks, are
in maps-on-a-small-screen.md.

## The paper palette (e-paper)

One palette. Two ways to draw for paper:

- **As light on dark, printed as the negative** (WiFi Radar, and Flight Radar's colour looks printed): the UI draws
  as usual, `paper.c` prints the negative, so black in the theme is white paper and white is ink:

```c
{ "PAPER", RGB(0,0,0), RGB(0,0,0), INK_GL, INK_GL,
           INK_BK, INK_GD, INK_BK, INK_GD, INK_BK, INK_BK },
```

  The two greys are the ones the glass really shows, and drawing with exactly these gives a clean flat grey. Codes
  0..3 as seen (0 black, 255 white): 5.79" 792x272 0, 60, 140, 255; 4.2" 400x300 0, 50, 220, 255 with the panel's
  stock waveform (both measured in ViewOwl, M). Flight Radar changed the 4.2"'s grey waveform so its greys moved to
  the middle, 0, 95, 185, 255, read off a photo of test bands (M). Security colours make no sense in grey: the list
  says it in words.

- **In inks, one channel each** (Flight Radar's own paper screens): `P_WHITE` 0, 0, 0. `P_BLACK` 255, 0, 0. `P_DARK`
  0, 255, 0. `P_LIGHT` 0, 0, 255. `paper.c` reads which channel is over half, so an edge rounds to one of the two inks
  it lies between (anti-aliasing-and-soft-edges.md). Tones between are patterns of two inks (e-paper-design.md).

On the glass the greys look less contrasty than in the preview: text is black, dark grey only for what is second
(the days' lows), nothing read in light grey (M, 4.2").

## The ground

No page sits on flat black. `draw_background` gives a vertical gradient `bg0` to `bg1` (a colour a row, `s_bgrow`), a
vignette (only 56 px at each side get darker, `VIG_W`), a faint 12 px grid through the scope's centre with brighter
nodes, and about 128 specks of dust hashed from x, y so they never move. All from tables built when the theme
changes. Pages of text use the same ground without the grid: they read better on it. Chamfered boxes
(`chamfer_box`, cut corners top left and bottom right), corner brackets and hatched bars frame the instrument. The
light map gets the opposite: round cards with a soft shadow, because the instrument's cut corners belong to the dark
looks. On paper the dust is not drawn (it would print as dirt) and no LCD frames: the paper is the ground.

## Other colours kept out of themes

- QR plate: dark 10, 12, 20 on light 250, 250, 247, always, in every theme. A phone reads a light code on a dark
  screen less surely.
- The guide pictures on the site (tools/Guide): back 3, 8, 26, frame 29, 74, 120, card 11, 33, 66 at alpha 240, ink
  234, 247, 255, the arrows 255, 184, 72, a dark edge 3, 8, 26 at alpha 170 under them so they show on the light map
  too.
