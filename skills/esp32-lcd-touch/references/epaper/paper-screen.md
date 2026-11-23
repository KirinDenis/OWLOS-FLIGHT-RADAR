# What OWLOS Flight Radar puts on the paper, and when

How the radar's e-paper builds turn a picture into 4 greys and decide what to print: `core/paper.h`, `core/paper.c`,
`core/ui_paper.c`, the paper parts of `core/ui.c` and `firmware/main/main.c`. Runs on the CrowPanel 4.2" on our desk
[measured unless it says otherwise]. The driver underneath: [ssd1683](ssd1683.md). How the screens are designed
(the printed chart, the weather dial, fonts, tones) is esp32-ui-drawing's subject:
[e-paper-design](../../../esp32-ui-drawing/references/e-paper-design.md). This file keeps the parts that decide
refreshes and keys. Sureness words as in [SKILL.md](../../SKILL.md).

Why the paper has its own screens: the LCD screens printed in 1 bit were rejected (tiny, no greys). So 4 greys
with ViewOwl's waveforms, and drawing of its own for paper; the colour radar printed in greys stays only for the
looks that have no paper version.

## Two ways a picture comes to the paper

- **The colour looks**, drawn light on dark glass as on an LCD: paper is ink on white, so `paper_pack` prints the
  negative, the light by steps. Under 36 white, under 80 light grey, under 150 dark grey, from there black.
  Changed since the first version of these notes: it was "the nearest of the 4 greys". The nearest grey put the
  radar's dim words (light about 100) on the light grey, and the 4.2"'s light grey was all but white, so a third
  of the words were gone on the first run. Now the faint ground, its grid and the panels' shading stay white, the
  rings and quiet marks go light grey, the dim words dark grey, the rest black.
- **The paper's own screens** (`paper_inks` 1: the CLASSIC chart, the printed radar, the weather pages), drawn in
  four inks, one colour channel each: `P_BLACK` red, `P_DARK` green, `P_LIGHT` blue, `P_WHITE` nothing. `paper.c`
  reads which channel is over half (black from 90 of 255, a third: the thin strokes of a bold W went at half). An
  anti-aliased edge drawn over another ink so comes out as one of the two, never a grey that neither is: black
  over the light grey water is black or water. The edges are crisp, and what moves stays black and white, which
  the partial refresh can draw.

No dither at pack time on either way: dithering made text and thin lines fuzzy on the real panel, and dots that
differ from frame to frame spoil the partial refresh. A tone between two inks is a pattern drawn at draw time.

## The rules the screens follow

- **Grey only on what does not change** until the next full refresh: the sheet, the map, the bands. The planes,
  their trails and labels, the cards and the key strip are black on white. Partials since the paper screen: grey
  debt 0 on the sheet.
- **Two layers**: the sheet (printed with a full refresh when it is new) and over it what moves, drawn on top in
  black and white.
- **A new screen is printed whole**, its greys too (`paper_new_sheet`, which main.c turns into `disp_full_next()`):
  another view, look, weather page, range or tone style, or the map under it made again. Before this the colour
  looks' greys were missing for up to 3 minutes after a change.
- **A new screen waits for its map** (`paper_hold`): while it does, the firmware draws and pushes nothing, the old
  screen stays on the glass, and it looks again every 250 ms. Printed once with its map, not twice; after 8 s
  without one (no WiFi, no tiles kept) it is printed anyway. A white sheet drawn on top first was a long refresh for
  nothing.
- **Soft edges only where printed whole**: `canvas_t.soft` lets letters and lines step their edges through the
  greys (`paper_soft`: a third, two thirds of the way from what is under to the ink, by how much of the pixel is
  covered: 30, 60, 115 of 256). It is on for what is printed whole (the weather pages, the radar's scope, the key
  strip at rest) and off for anything drawn on top (clocks, a pressed key's third): a partial cannot make grey.
- **Gradients** by `paper_tone`: an ordered 4x4 dither between the two inks either side of a lightness. The glass
  blurs a 1 px pattern into the tone between.
- **Anything that changes every frame for no reason costs pixels**: the weather dial keys off a quarter-hour now;
  a continuous "now" made the loop's points shift and sent 120 to 180 px every 5 s.

## The keys' strip at the right edge

- `PAPER_KEYS_W` 28 px at the right edge of the screen as drawn, light grey, three equal thirds, from the top:
  MENU, the dial, EXIT (which key is MENU is a guess, [crowpanel-42](crowpanel-42.md)). Everything else on the
  paper keeps left of it.
- In each third, turned to read downwards, what pressing it does now; the dial's third has a line per direction
  with its sign. A key that does nothing here says nothing.
- A press: its third prints black (drawn on top, about 1.2 s), and the press is taken in the pass after that
  picture; then the screen is printed whole. Keys pressed meanwhile go in with it, one print for them all.
- **Gathered keys**: the dial (range, or the weather's page), MENU (the next look) and EXIT with no plane picked (the
  look before) are not taken at once. Each press moves what is asked for, its third says it white on black, and
  after `PEND_MS` 2000 ms with no press it is taken: one print for all of them. Asked back to where it was:
  nothing is printed. OK and held keys keep the black-third-then-full-print way. Verified on the board: two steps
  further gave one print after the quiet; nearer then further gave "as it was" and no print.

```c
/* paper: the dial (the range, or the weather's page), MENU and EXIT (the look after, the look before) are not taken
 * at once: each press moves what is asked for, its third of the strip says it, and after PEND_MS with no press the
 * firmware takes it, one print for them all; asked back to where it was, nothing. ui_key_pend: 1 gathered (a
 * different kind before it taken first, into cmd), 0 a key for ui_key_wait. ui_pend_take: 1 it changed the screen */
enum { PEND_NONE, PEND_RANGE, PEND_PAGE, PEND_LOOK };
#define PEND_MS 2000u
static inline int ui_pend_due(const ui_t *u, uint32_t now) { return u->pend_kind && now - u->pend_at >= PEND_MS; }

int ui_key_pend(ui_t *u, int key, uint32_t now, ui_cmd_t *cmd)
{
    if (u->view != VIEW_MAIN) return 0;
    const int wx = u->cfg.look == LOOK_WEATHER;
    int kind = PEND_NONE;
    if (key == KEY_UP || key == KEY_DOWN) kind = wx ? PEND_PAGE : PEND_RANGE;
    else if (key == KEY_MENU || (key == KEY_BACK && !sel_plane(u))) kind = PEND_LOOK;
    if (kind == PEND_NONE) return 0;
    if (u->pend_kind && (u->pend_kind != kind || (kind == PEND_LOOK && u->pend_key != key))) ui_pend_take(u, cmd);
    if (u->pend_kind != kind) {                        // gathering starts from what is shown
        u->pend_kind = (uint8_t)kind;
        u->pend_range = u->cfg.range;
        u->pend_page = u->wx_page;
        u->pend_look = u->cfg.look;
    }
    const int d = key == KEY_UP ? -1 : 1;
    if (kind == PEND_RANGE) u->pend_range = range_from(u->pend_range, d);
    else if (kind == PEND_PAGE) u->pend_page = (uint8_t)((u->pend_page + WX_PAGES + d) % WX_PAGES);
    else u->pend_look = (uint8_t)(key == KEY_MENU ? ui_look_after(u->pend_look) : ui_look_before(u->pend_look));
    u->pend_key = (uint8_t)key;
    u->pend_at = now ? now : 1;
    u->key_at = 0;
    return 1;
}
```

In the screen task (main.c, e-paper builds):

```c
/* paper: a key pressed prints its third of the keys' strip black first, drawn on top in a second; the pass
 * after that picture takes it and the screen is printed whole. keys pressed meanwhile go in with it, one print
 * for them all */
if (s_key_n && s_key_shown) {
    for (int i = 0; i < s_key_n; i++) ui_key(&s_ui, s_key_wait[i], &cmd);
    s_key_n = 0;
    s_key_shown = 0;
    disp_full_next();
    paper_due(now, 1);
}
/* the dial and MENU are gathered: each press says what it asks for in its third at once (drawn on top), and
 * after PEND_MS with none it is taken, one print for them all; asked back to where it was, nothing */
if (ui_pend_due(&s_ui, now)) {
    ui_pend_take(&s_ui, &cmd);
    paper_due(now, 1);
}
if (key && ui_key_pend(&s_ui, key, now, &cmd)) {
    paper_due(now, 1);
} else if (key) {
    if (s_key_n < (int)sizeof s_key_wait) s_key_wait[s_key_n++] = (uint8_t)key;
    s_key_shown = 0;
    ui_key_wait(&s_ui, key);
}
```

And before the strips of a frame:

```c
int hold = 0;                                  // paper: a new screen waits for its map, the old one stays
if (paper_new_sheet) {                         // a new map on the paper's sheet: printed whole, its greys too
    paper_new_sheet = 0;
    disp_full_next();
}
hold = paper_hold;
if (hold) paper_due(now, 1);                   // looked at again in a quarter of a second
for (int sy0 = 0; !hold && sy0 < GFX_PH; sy0 += step) {
    /* the strips */
}
```

The keys themselves are read on a task of their own every 10 ms into a queue: the screen task waits seconds while
the glass changes ([buttons-keys](../buttons-keys.md)). The console's test key slot is single: keys sent during a
6 s refresh overwrite each other.

## paper.h and paper.c

```c
/* paper - the picture for e-paper, 4 greys, 2 bits a pixel.
 * two ways a picture comes here (paper_inks says which):
 *  - the radar's pages, drawn as light on dark glass: e-paper is ink on white, so this prints
 *    the negative, the light by steps (paper.c) as white, light grey, dark grey or black
 *  - the paper's own main screen (ui_paper.c), drawn in the inks below, one channel each */
#pragma once
#include "gfx.h"

#define PAPER_STRIDE ((GFX_W + 3) / 4)      // bytes a row, 4 pixels a byte

/* one strip of 0x00RRGGBB light -> fb, which points at the first byte of the whole
 * frame. row cv->y0 + r goes to fb + (cv->y0 + r) * PAPER_STRIDE.
 * leftmost pixel in the top 2 bits. code 3 = white, 2 = light grey, 1 = dark grey,
 * 0 = black, the same as ViewOwl's mono4gray frames */
void paper_pack(const canvas_t *cv, uint8_t *fb);

extern int paper_inks;        /* 1: this frame is drawn in the P_* inks, 0: the radar's light, printed as its negative */
extern int paper_new_sheet;   /* 1: the sheet is new: print it with a full refresh, its greys too */
extern int paper_hold;        /* 1: a new screen waits for its map: draw nothing, the glass keeps the old screen */
extern int paper_style;       /* the sheet's tones: 0 the 4 inks and black and white patterns, 1 more tones */

/* the inks: a channel each, and paper.c reads which is over half */
#define P_WHITE RGB(0, 0, 0)
#define P_BLACK RGB(255, 0, 0)
#define P_DARK  RGB(0, 255, 0)
#define P_LIGHT RGB(0, 0, 255)

uint32_t paper_soft(uint32_t under, uint32_t ink, int k);
uint32_t paper_tone(float g, int x, int y);
void paper_line(canvas_t *cv, float x0, float y0, float x1, float y1, float w, uint32_t ink);
void paper_disc(canvas_t *cv, float cx, float cy, float r, uint32_t ink);
void paper_ring(canvas_t *cv, float cx, float cy, float r, float w, uint32_t ink);

/* the keys' strip at the right edge (ui_paper.c), a third of the height for each key */
#define PAPER_KEYS_W 28

// how the panel really shows the 4 codes, 0..255, for the preview
extern const uint8_t PAPER_LEVEL[4];

/* the two greys of the panel. the 4.2"'s were 50 and 220 (ViewOwl measured them), its grey
 * waveform now puts them nearer the middle (epd.c LUT_4GRAY): these two are read off a photo */
#if RADAR_DISPLAY == DISPLAY_EPD_400x300
#define PAPER_DARK  95
#define PAPER_LIGHT 185
#else
#define PAPER_DARK  60
#define PAPER_LIGHT 140
#endif
#define INK_GD  RGB(255 - PAPER_DARK, 255 - PAPER_DARK, 255 - PAPER_DARK)      // ViewOwl's GD, dark grey
#define INK_GL  RGB(255 - PAPER_LIGHT, 255 - PAPER_LIGHT, 255 - PAPER_LIGHT)   // ViewOwl's GL, light grey
#define INK_BK  RGB(255, 255, 255)                                             // black ink
```

```c
#include "paper.h"
#include <math.h>
#include <stdlib.h>

const uint8_t PAPER_LEVEL[4] = { 0, PAPER_DARK, PAPER_LIGHT, 255 };

/* how much ink a pixel gets goes by steps, no dither. steps, not the nearest grey: the nearest one put the
 * radar's dim words, light 100 or so, on the light grey, and the 4.2"'s light grey is all but white, so a
 * third of the words were gone (the first run on one) */
#define INK_WHITE 36        // below: white
#define INK_LIGHT 80        // below: light grey
#define INK_DARK  150       // below: dark grey, from here black

static uint8_t s_code[256];         // ink -> code: 3 white, 2 light grey, 1 dark grey, 0 black
static int     s_built;

static void build(void)
{
    for (int ink = 0; ink < 256; ink++)
        s_code[ink] = (uint8_t)(ink < INK_WHITE ? 3 : ink < INK_LIGHT ? 2 : ink < INK_DARK ? 1 : 0);
    s_built = 1;
}

int paper_inks, paper_new_sheet, paper_hold;

/* a pixel in the paper's inks (paper.h P_*): black from a third of it, the greys from half. black a
 * little before half: the bold letters' thin strokes (the middle of a W) are a third covered here and
 * there, and went at half */
static inline uint8_t ink_code(uint32_t q)
{
    return (uint8_t)((q >> 16 & 255) >= 90 ? 0 : (q >> 8 & 255) >= 128 ? 1 : (q & 255) >= 128 ? 2 : 3);
}

/* an edge a step of grey between the ink and what is under it, by how much of the pixel it covers:
 * the black as far as a crisp edge's (black from a third, above), the greys outside it: smoother, no
 * thinner. never lighter than what is there */
static int paper_dark(uint32_t q) { return (q >> 16 & 255) >= 90 ? 3 : (q >> 8 & 255) >= 128 ? 2 : (q & 255) >= 128 ? 1 : 0; }

uint32_t paper_soft(uint32_t under, uint32_t ink, int k)
{
    static const uint32_t INK[4] = { P_WHITE, P_LIGHT, P_DARK, P_BLACK };
    const int u = paper_dark(under), i = paper_dark(ink);
    const int step = k >= 115 ? 3 : k >= 60 ? 2 : k >= 30 ? 1 : 0;   // thirds of the way from under to the ink
    return INK[u + (i - u) * step / 3];
}

/* a lightness g (0 black .. 1 white) as one of the inks: an ordered dither between the two either side of it, for
 * gradients. the glass blurs the 1 px pattern into the tone between */
uint32_t paper_tone(float g, int x, int y)
{
    static const uint32_t INK[4] = { P_BLACK, P_DARK, P_LIGHT, P_WHITE };
    static const float LV[4] = { 0.0f, PAPER_DARK / 255.0f, PAPER_LIGHT / 255.0f, 1.0f };
    static const uint8_t BAYER[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
    int i = 0;
    while (i < 2 && g > LV[i + 1]) i++;
    const float t = (g - LV[i]) / (LV[i + 1] - LV[i]);
    return t * 16.0f > (float)BAYER[y & 3][x & 3] + 0.5f ? INK[i + 1] : INK[i];
}

/* paper_line, paper_disc, paper_ring: soft-edged where the canvas is (cv->soft), each pixel by its distance from
 * the line or the circle through paper_soft; else gfx_line, gfx_disc, gfx_ring as on an LCD */

void paper_pack(const canvas_t *cv, uint8_t *fb)
{
    if (!s_built) build();
    const int inks = paper_inks;
    for (int r = 0; r < cv->h; r++) {
        const uint32_t *row = cv->px + r * GFX_W;
        uint8_t *out = fb + (cv->y0 + r) * PAPER_STRIDE;
        uint8_t acc = 0;
        for (int x = 0; x < GFX_W; x++) {
            uint32_t q = row[x];
            int l = (int)((((q >> 16) & 255) * 77 + ((q >> 8) & 255) * 150 + (q & 255) * 29) >> 8);
            acc = (uint8_t)(acc << 2 | (inks ? ink_code(q) : s_code[l]));   // negative: light is ink
            if ((x & 3) == 3) { out[x >> 2] = acc; acc = 0; }
        }
        if (GFX_W & 3) {                                     // pad the last byte with white
            int left = 4 - (GFX_W & 3);
            out[GFX_W >> 2] = (uint8_t)(acc << (2 * left) | ((1 << (2 * left)) - 1));
        }
    }
}
```

The older paper.c (WiFi Radar, and the radar before the paper screen) mapped every pixel to the nearest of the 4
measured greys, with light under 14 kept white; esp32-ui-drawing keeps that copy.

## Tones beyond four (`paper_style` 1, the default)

- Tones from 1-px chequers of two inks: TINT (white and light, about 220), MID (light and dark, about 140), DEEP
  (dark and black, about 47). Woods are MID with a black tree every 4x4, parks light, water flat dark, towns a
  black hatch on light, near buildings DEEP, major roads DEEP. `OWLOS PAPER 0|1` on the console switches it in RAM
  and reprints the sheet.
- The first try (a tint, sparse dots) differed in 7 to 11 thousand of 120 thousand pixels and could not be seen on
  the glass. A step has to be a whole grey.
