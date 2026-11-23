# Fonts and font baking (core/font.c, tools/FontBake)

Two kinds of font, the text functions, how a TTF becomes a C table, what each font costs, and the traps of text.
Code: [code-fonts.md](code-fonts.md) (font.h, font.c, FontBake). Flight Radar's newest font.c adds the turned
text and the soft letters described below.

How sure: (M) seen on a real board, (P) built and seen in the preview or read in the code, (R) reasoned.

## Two kinds, each with a job

**MICRO**: the 3x5 pixel font from the DOS game OWL FLY, a 4x6 cell, 1 bit, caps, digits and signs, integer scale.
Instrument text: ring numbers, degrees, status, labels, what a value is. 59 glyphs, 354 bytes: it costs nothing and
it gives the look. Lower case maps to caps, `*` draws a degree sign. No `"`, no `&`, nothing outside ASCII.

**Baked**: a TTF turned into 4-bit coverage bitmaps by `tools/FontBake` (C#, Windows). For names, which can be any
UTF-8, and for big numbers. Rows of `ceil(w/2)` bytes, high nibble first, glyphs sorted by code point for a binary
search:

```c
typedef struct {
    uint16_t cp;            /* Unicode code point */
    uint8_t  w, h;          /* ink box */
    int8_t   xo, yo;        /* offset from pen, yo is from line top */
    uint8_t  adv;           /* pen advance */
    uint8_t  pad;
    uint32_t off;           /* offset in bitmap. rows of ceil(w/2) bytes, high nibble first */
} glyph_t;
```

Coverage to alpha: `k = (a * (v * 17 + 1)) >> 8`, so 15 gives 256.

## FontBake

FontBake picks the em size so the monospace advance is a whole number of pixels: `em = adv / advance_em`, drawn with
GDI+ `AntiAliasGridFit` (hinted), white on black, the green channel is the coverage. Glyphs land on whole pixels and
stay crisp. Then the lines are tightened so the top of H, d and 0 sits right under the line top.

```
dotnet run --project tools/FontBake -- CascadiaMono.ttf SMALL  6 "32-126,160-383,1024-1119,8211-8212,8216-8217,8220-8221,8230" core/font_small.c sheet.png
dotnet run --project tools/FontBake -- CascadiaMono.ttf MEDIUM 8 "32-126,160-383,1024-1119,8211-8212,8216-8217,8220-8221,8230" core/font_medium.c
dotnet run --project tools/FontBake -- CascadiaMono.ttf LARGE 15 "32,37,43,45-58,66,68,77,100,109" core/font_large.c
dotnet run --project tools/FontBake -- CascadiaMono.ttf HUGE  24 "32,37,43,45-58" core/font_huge.c
```

The ranges are read back from the baked tables. Missing code points are skipped, so ask wide. `sheet.png` is a 4x
contact sheet to look at what was baked. C#, .NET 8: every converter here is C#, not Python.

`160-383` is Latin-1 and Latin Extended-A: Slovak, Czech, Polish, the accents of Europe. `1024-1119` is Cyrillic.
Those are in network names and town names, so the small and medium fonts carry them. Big fonts carry only digits and
a few signs: size is what costs.

## The fonts baked, and what they cost

Each line: font, source TTF and advance, em px, line height and ascent, glyphs, bytes in flash (bits plus 12 a
glyph) where known.

The LCD fonts (Cascadia Mono):

- FONT_SMALL: advance 6, 10.24 px, line 10, ascent 8, 422 glyphs, 13 964 B.
- FONT_MEDIUM: advance 8, 13.65 px, line 14, ascent 11, 422 glyphs, 21 362 B.
- FONT_LARGE: advance 15, 25.60 px, line 26, ascent 20, 22 glyphs (digits, `% + - . / :`, B D M d m), 2 550 B.
- FONT_HUGE: advance 24, 40.96 px, line 39, ascent 29, 17 glyphs, 4 116 B.

The scaled glass fonts (Cascadia Mono, half as big again, `font_glass_*.c`, for the layouts put on 800x480 and up,
P):

- FONT_TINY: advance 6, 10.24 px, line 10, ascent 8, 96 glyphs (ASCII and the degree sign). What the pixel font is
  written in at scale 1 on that glass.
- FONT_SMALL: advance 9, 15.36 px, line 16, ascent 12, 422 glyphs.
- FONT_MEDIUM: advance 12, 20.48 px, line 21, ascent 16, 422 glyphs.
- FONT_LARGE: advance 22, 37.55 px, line 38, ascent 29, 22 glyphs.
- FONT_HUGE: advance 36, 61.44 px, line 58, ascent 44, 17 glyphs.

On the Tab5 (9/4), the Waveshare 7B (15/8) and the JC8012 (5/2) the same 3/2 bakes are used, so the writing is
two thirds, four fifths and three fifths of its size there: a bake per ratio is still to come.

The e-paper fonts (DejaVu Sans Bold, read from across the room like printed plates):

- FONT_PAPER_S: advance 7, 10.06 px, line 11, ascent 9, 416 glyphs, 15 895 B.
- FONT_PAPER_M: advance 9, 12.93 px, line 14, ascent 11, 416 glyphs, 22 549 B.
- FONT_PAPER_L: advance 14, 20.12 px, line 21, ascent 16, 416 glyphs, 45 696 B.
- FONT_PAPER_XL: advance 24, 34.49 px, line 35, ascent 27, 416 glyphs, 118 078 B. Flight Radar's paper clock.
- FONT_PAPER_XXL: advance 44, 63.24 px, line 63, ascent 48, 15 glyphs (digits, minus, dot, M and ~), 10 450 B.
  The big temperature: it has no degree sign, so the degree is a `gfx_ring` or `paper_ring` after the digits.

The film fonts (tools/preview, Cascadia Mono, ASCII only, 95 glyphs): FONT_TITLE advance 16, 27.31 px, line 27;
FONT_BIG advance 30, 51.20 px, line 51 (films-and-stills.md).

Air Quality and Thermo Relay add `LABEL` (advance 10, 17.07 px, 420 glyphs), `VAL` (13, 22.19 px, ASCII + degree +
middle dot), `BIG` (20, 34.13 px), `HUD` (22, 37.55 px), `TEMP` (50, 85.33 px, digits and the degree sign). Air
Quality's `HUGE` is bigger than the radars': advance 34, 58.03 px. A big font is cheap when it holds only the
digits.

Fonts cost flash: the e-paper build with the paper weather and its XL and XXL fonts came to 1.41 MB of a 2 MB app
partition (M). Link a font only into the builds that draw with it: Flight Radar's paper fonts are not in the colour
builds.

Fonts: Cascadia Mono and DejaVu keep their own licences (OFL, the DejaVu licence): list them in THIRD-PARTY.md.

## Text functions

```c
int font_text(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a);   // returns the advance
int font_width(const font_t *f, const char *utf8);
int font_text_clip(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a, int maxw);
int font_text_down(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c);   // turned, read downwards
int micro_text(canvas_t *cv, int x, int y, const char *s, uint32_t c, int a, int scale);
static inline int micro_width(const char *s, int scale);
#define FONT_LINE_H(f) GFX_UNZ((int)(f)->line_h)   // a line's height and the baseline, in the layout's pixels
#define FONT_ASCENT(f) GFX_UNZ((int)(f)->ascent)
```

`y` is the line top, not the baseline. Off-strip text still measures: `hit = gfx_touches(...)` guards the pixels, the
pen still moves. So layout that uses `font_text`'s return value stays right in every strip.

`font_text_down` writes a line turned a quarter clockwise, read downwards, the letters' tops to the right: `x` is the
right edge of its line, the pen goes down from `y`, it returns how far down. It works in the glass's own pixels. It is
how the e-paper's key strip says what each key does in a strip 28 px wide (e-paper-design.md, M on the 4.2").

On scaled glass the pixel font at scale 1 and 2 is written in a smooth font of the same width instead (TINY and
MEDIUM), whose capitals stand where its letters stood, with `*` as a real degree sign: the pixel font has no half
pixels (P). Larger scales stay blocks.

## Long names fade out, they do not get "..."

At 6 px an ellipsis looks like an underscore, and an underscore can be a real part of a network name. So a cut name
fades over its last 12 px (`FADE`, `GFX_Z(12)` on scaled glass). `font_text_clip` measures first, and only a cut
string fades:

```c
if (!RADAR_EPAPER && fade && px > limit - FADE) k = k * (limit - px) / FADE;
```

E-paper cannot fade (4 greys), and at those sizes an ellipsis cannot pass for an underscore, so there it is whole
letters plus a real ellipsis, U+2026, which is why 8230 is in the ranges. `split2` in WiFi Radar's panel breaks a
long name over two lines by pixel width, UTF-8 safe. Bad bytes and emoji fall back to `?`.

## Letters on paper

`put_px` is the one place a letter's pixel is written. On glass it mixes. On a paper canvas marked `soft` it writes
`paper_soft(under, ink, k)`: the letter's edge a grey step between the ink and the paper (M, 4.2"). Off, the edge
rounds to the ink or the paper, which the black and white partial refresh can draw. Clocks and anything drawn on top
between full prints turn `soft` off for their call (anti-aliasing-and-soft-edges.md).

## Size for a value

- Numbers big, labels small: the value in a smooth font, its name in the pixel font or a small font above it.
- Pick the size by the room: "as big as the circle holds it" (one digit of the paper temperature in XXL, two or more
  in XL), the clock LARGE where it fits between two buttons, MEDIUM where not, the row's value MEDIUM or SMALL by its
  length. Write the code as "the first of these that fits", not as a size per board (layouts-by-screen-size.md).
- Short forms when long ones do not fit: `TUESDAY 18 NOVEMBER 2025` or `TUE 18 NOV 2025`, `FEELS LIKE` or `FEELS`.

## Traps of text

- **Ellipsis at 6 px (P).** A cut name ended in three dots that looked like an underscore. Fade the last 12 px.
- **The pixel font scaled up (P).** `N` at scale 2 reads as an arch. Compass letters use the smooth font. The pixel
  font has no lower case, no `"`, no `&`, no accents, no Cyrillic: a town's name or anything from outside goes in the
  smooth font. Scale 1 and 2 for labels; large scale only for a word meant to be read across the room (DEMO at scale
  4 or 5).
- **`strchr` finds the zero (P).** In the pixel font's sign table `strchr(signs, 0)` returns the terminator: check
  the char first (`p && ch`).
- **A sign lost at a strip edge (M).** On scaled boards the smooth slash stands a pixel over the capitals, and a strip
  starting there lost it ("WIND KM/H" on the 5"). `micro_text`'s strip test is widened a row each way.
- **Accents on capitals lost at a strip edge (R).** FontBake lifts everything so the top of H, d and 0 lands under
  the line top: an accent on a capital sticks out above the line. `draw_text` asks `gfx_touches(cv, y, y +
  f->line_h)`, so rows above `y` are drawn only in a strip that also holds part of the line. When a strip ends right
  above `y`, the accent is gone in that frame. If it matters, widen that test by the accent's height.
- **Emoji in a name (P).** A network name is any UTF-8. `glyph_or_fallback` gives `?` for what is not baked, and a bad
  byte gives `?` too. Never crash on a name, never trust its length in bytes: `split2` and the clip functions walk
  code points.
- **A bold letter's thin strokes went missing on paper (M).** With the paper's inks read at half, the middle of a W
  vanished. Black from a third of the pixel (90 of 255).
