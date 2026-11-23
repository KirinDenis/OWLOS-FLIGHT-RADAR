# The renderer and the canvas (core/gfx.h, core/gfx.c)

How the picture is made: a strip of rows at a time, in a 32-bit work buffer, packed to dithered RGB565 for an
LCD or to 2-bit greys for e-paper. The whole code is in [code-gfx.md](code-gfx.md). How a frame runs over the
strips, and what made frames fast, is in [frame-and-speed.md](frame-and-speed.md). How the smooth shapes work is
in [anti-aliasing-and-soft-edges.md](anti-aliasing-and-soft-edges.md).

How sure: (M) measured or seen on a real board, (P) built and seen in the PC preview or read in the code, (D)
read from a maker, a contributor's notes or docs, (R) reasoned here.

## Why strips, and no framebuffer

A 320x240 RGB565 frame is 153 600 bytes. The biggest free block after WiFi on an ESP32 without PSRAM is about
110 KB (M). A 480x320 frame is 307 200 bytes. A framebuffer does not fit, and a 16-bit one would band in dark
gradients anyway. So the picture is never stored. The scene is drawn again for each strip of rows, into a small
32-bit buffer, then packed and sent while the next strip is drawn.

Strip heights in use:

- 320x240 ILI9341: `DISP_STRIP` 24 rows, a 30 720 B work buffer, two DMA buffers of 15 360 B, 10 strips a frame.
- 480x320 ST7796 and ILI9486: 8 rows, 15 360 B, two of 7 680 B. The map's picture needs the heap (12 rows
  without a map). Flight Radar later took the ST7796 to 6 rows: see frame-and-speed.md, the strip gave 7.7 KB
  back to the map's tile reader (M).
- 240x240 GC9A01 on a C3: 16 rows, 15 360 B, two of 7 680 B. A small heap, with room for the map and TLS.
- 792x272 e-paper: 8 rows, 25 344 B, no DMA buffers, a frame store. 24 rows was one 76 KB block, not there after
  WiFi (M).
- FusionEdge's screen for boards without PSRAM (it reuses this gfx and font, `src/lite`): 2 rows, 8 with PSRAM.
  It draws only the rows asked for (y0 to y1), a strip at a time (M, runs on a 3.5" ST7796 board and the 2.8" CYD).

Pick the height from the largest free block, not from the free heap. On 480 wide with no PSRAM it is 8 to 12 rows.

The price: everything is drawn once per strip, so work inside a strip is multiplied by the strip count. That one
fact drives most of frame-and-speed.md.

## The canvas

```c
typedef struct {
    uint32_t *px;   /* GFX_PW * h pixels, 0x00RRGGBB, row by row */
    int y0;         /* first screen row of the strip */
    int h;          /* rows in strip */
    int helper;     /* drawn by the second core (main.c): its time is not in ui_t.prof */
    int layer;      /* RADAR_OVERLAY builds: which layer it is drawn for (ui.h UI_LAYER_*), 0 everywhere else */
    int soft;       /* paper: the letters' edges in the panel's greys (font.c), for what is printed whole with
                       them. 0: edges round to the ink or the paper, what the black and white refresh can draw */
} canvas_t;

#define RGB(r, g, b) ((uint32_t)((((r) & 255) << 16) | (((g) & 255) << 8) | ((b) & 255)))

static inline uint32_t *gfx_row(const canvas_t *cv, int y)      // 0 if not in this strip
{
    int r = y - cv->y0;
    return (r >= 0 && r < cv->h) ? cv->px + r * GFX_PW : 0;
}
static inline int gfx_touches(const canvas_t *cv, int ya, int yb)   // any of the rows in this strip?
{
    return GFX_Z(yb) + GFX_SCALED >= cv->y0 && GFX_Z(ya) < cv->y0 + cv->h;
}
```

The `layer` and `soft` fields are newer than the first copy in code-gfx.md (Flight Radar wins: firmware code
that builds canvases with positional initializers got a trailing 0 for each). The old canvas had four fields.

Why 32 bits a pixel in the work buffer: 8 bits a channel keeps alpha, additive light and dark gradients clean,
and the dither at the end hides the step to 565. Coordinates are always SCREEN coordinates, the strip is only a
window, so page code never knows about strips except to skip work.

## Colour arithmetic, no floats

Alpha is 0..256 everywhere, 256 = opaque, so `>> 8` is exact. Red and blue go in one multiply.

```c
static inline uint32_t gfx_mix(uint32_t d, uint32_t s, uint32_t a)      // d toward s
{
    uint32_t ia = 256 - a;
    uint32_t rb = ((s & 0xFF00FFu) * a + (d & 0xFF00FFu) * ia) >> 8;
    uint32_t g  = ((s & 0x00FF00u) * a + (d & 0x00FF00u) * ia) >> 8;
    return (rb & 0xFF00FFu) | (g & 0x00FF00u);
}
static inline uint32_t gfx_scale(uint32_t c, uint32_t a)                // darker
{
    return ((((c & 0xFF00FFu) * a) >> 8) & 0xFF00FFu) | ((((c & 0x00FF00u) * a) >> 8) & 0x00FF00u);
}
static inline uint32_t gfx_add(uint32_t d, uint32_t s)   // saturating: light stops at 255, never wraps to black
{
    /* branchless: add low 7 bits of each channel, put the top bit back with xor,
     * then every channel that carried out gets filled with 0xFF */
    uint32_t t = (d & 0x7F7F7Fu) + (s & 0x7F7F7Fu);
    uint32_t sum = t ^ ((d ^ s) & 0x808080u);
    uint32_t ovf = ((d & s) | ((d | s) & t)) & 0x808080u;
    return sum | ((ovf >> 7) * 0xFFu);
}
```

`gfx_mix` is paint. `gfx_add` is light: glows, beams, rims and halos are added, so they never dull what is under
them. That difference is most of why these screens look like instruments and not like forms.

## Fills, gradients, alpha, hatch

- `gfx_fill`, `gfx_hline`, `gfx_vline` take alpha and go through one `plot`: 256 stores, 0 skips, the rest mixes.
  Prefer them to `gfx_line` for straight sides: `chamfer_box` draws its sides as whole pixels and only the two cut
  corners smooth (Flight Radar). It matters on a screen full of boxes.
- A vertical gradient is a table of one colour per row, built when the theme changes (`s_bgrow`). A gradient by
  radius is a table by radius (`s_glass`). Nothing computes a gradient per pixel per frame.
- `gfx_hatch`: diagonal stripes, `((x + y) & 3) < 2`, the caution-tape bar that fills the empty part of a header.
- Halftones and dithered gradients for e-paper are made at draw time: see e-paper-design.md.

## Packing: gfx_pack565

4x4 ordered dither from 8 bits to 565, then a byte swap, so DMA sends the buffer as it is. Keep this exact
routine: ordered dither means a pixel that did not change packs to the same bits, which is what makes
dirty-column checksums honest (frame-and-speed.md). Error diffusion would break that.

```c
CORE_HOT void gfx_pack565(const canvas_t *cv, uint16_t *out)
{
    /* NOTE: tried three lookup tables instead of this math, it was slower on the
     * board (10.8 ms a frame vs 9.7). dont go back to tables */
    static const CORE_DATA uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    for (int r = 0; r < cv->h; r++) {
        const uint32_t *row = cv->px + r * GFX_W;
        const uint8_t *bm = bayer[(cv->y0 + r) & 3];
        uint16_t *o = out + r * GFX_W;
        for (int x = 0; x < GFX_W; x++) {
            uint32_t p = row[x];
            unsigned d = bm[x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3;
            unsigned gg = (((p >> 8) & 255) + (d >> 2)) >> 2;
            unsigned bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            uint16_t v = (uint16_t)((rr << 11) | (gg << 5) | bb);
            o[x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}
```

The Bayer row comes from the SCREEN row `(cv->y0 + r) & 3`, so the pattern does not change with strip height. A
panel with no backlight control dims here: `gfx_bright`, one `gfx_scale` a pixel (code-gfx.md, Air Quality's
version). The RGB-bus panels read a frame from memory in their own byte order: low byte first there
(`RADAR_RGB_BUS`), and the PC preview unpacks both ways (P).

E-paper packs the same canvas with `paper_pack` into 2 bits a pixel: e-paper-design.md.

## Clipping

Every primitive clips to the strip and to `0..GFX_W`. That is the only clipping there is. For a vertical clip,
make a sub-canvas: a list scrolling under its header draws into

```c
canvas_t lc = { cv->px + (ya - cv->y0) * GFX_W, ya, yb - ya + 1, cv->helper };
```

and the header is drawn after it. Flight Radar now has it as a function that also keeps `layer` and `soft` and
works on scaled glass (P):

```c
static inline int gfx_sub(const canvas_t *cv, int ya, int yb, canvas_t *out);   // 0: none of those rows here
```

Text has its own right clip (fonts-and-font-baking.md).

Trap, rows scrolling over the header (P): primitives only clip to the strip. The sub-canvas for the list's body,
header drawn after it, is the cure, and the only vertical clip this renderer needs.

## Whole numbers, for the chip without an FPU

The ESP32 has a single-precision FPU. The ESP32-C3 has none: a float costs tens of cycles (M). So the per-pixel
work in Flight Radar's gfx.c is in 1/256 px whole numbers: floats only set a shape up, once a call.
`GFX_SOFT_FLOAT` is on for RISC-V without `__riscv_flen`, and the PC preview can force it to see the C3's way
drawn. On a chip with an FPU `gfx_sqrt` beats the integer radius walk, which is why the disc pass has both
(`#if GFX_SOFT_FLOAT`). `gfx_isqrt` is the whole number square root. The weather dial does the same: radii in
sixteenths of a glass pixel, angles in quarter degrees, an arc tangent table (P, and on the round C3 the dial
went from 3 to 4 fps to 6 to 7 after gfx_disc got an integer path, M).

Hot loops go to IRAM (`CORE_HOT = IRAM_ATTR`): a frame runs them ten to forty times over and every flash cache
miss costs microseconds. Small tables go to DRAM (`CORE_DATA`). `core/CMakeLists.txt` builds with
`-O2 -ffast-math`, and `sdkconfig` with `CONFIG_COMPILER_OPTIMIZATION_PERF=y`.

On the boards with the moving layer (below) `CORE_HOT` and `CORE_DATA` are empty: what is drawn there is not on
the panel's clock and the internal RAM is wanted. `CORE_BIG` puts big state that no interrupt touches into PSRAM
on those boards (`EXT_RAM_BSS_ATTR`), into RAM everywhere else (P).

## Layout pixels and glass pixels (GFX_SCALED)

The 5" and 7" boards (800x480, 1024x600) and the P4 boards (1280x720, 1280x800) do not get a new layout. The 3.5"'s
layout is kept and put on the bigger glass at a fixed ratio (P, then M on some of them, see
layouts-by-screen-size.md):

- `GFX_W`, `GFX_H` stay the layout's (534x320 on 800x480, a ratio of 3/2). `GFX_PW`, `GFX_PH` are the glass's.
- `GFX_Z(v)` a whole number of glass pixels across, `GFX_PX(v)` where a layout pixel's middle falls on the glass,
  `GFX_PL(v)` a length, `GFX_UNZ(v)` a glass width back in layout pixels, rounded up. On every other screen they
  are all `(v)`.
- What is drawn through the gfx calls is in layout pixels. A strip's rows and `gfx_row` are in glass pixels: code
  that walks pixels by hand (the disc pass, the map, the background, round boxes, the weather dial, the moon)
  works there, through `G_SR`, `G_PR`, `G_RQ`, `G_SCX`, `G_SCY` in ui_int.h.
- 534 and not 533: its middle is the glass's middle, and the half pixel past the edge is clipped. The same reason
  gives 547 on 1024x600 (15/8), 569 on 1280x720 (9/4), and 512 on 1280x800 (5/2, exact).
- Fonts are baked half as big again for it (`font_glass_*.c`), and the pixel font at scale 1 and 2 is written in
  a smooth font instead: fonts-and-font-baking.md.
- Every other screen compiles to the same picture: checked pixel for pixel on 2.8", 3.5" and round, 135 shots (P).

## The moving layer (RADAR_OVERLAY), by chipguyhere

On the Waveshare 7 and 7B and the Guition JC8012P4A1C the sweep turns at the panel's own frame rate: about 34
frames a second on the 7, 30 on the 7B, 52 on the JC8012 (D, from the contributor's measurements in the public
repo's README and FORK-NOTES.md; not run here). How the drawing side works:

- The frame in PSRAM holds only what stands still (glass, rings, map, panel, cards, pages). It is drawn by the
  same strip renderer with `cv->layer == UI_LAYER_BG`, in the background, as time allows: on the 7B a whole frame
  every 1.9 s (D). In that layer the disc pass leaves out the sweep and the ticks, and planes and compass letters
  are skipped (`LAYER_BG`, `LAYER_TOP` in ui_int.h).
- The moving layer (planes with trails, labels and glows, the compass ring's ticks and letters) is drawn with the
  same primitives onto a canvas of zeros. In these builds the top byte of a pixel is coverage: `gfx_mix` builds
  coverage as it builds colour, `gfx_add` leaves it as it was, and a colour drawn opaque sets it to 255
  (`GFX_ALPHA`, `GFX_OPAQUE`). So the canvas ends as premultiplied colour plus alpha.
- `firmware/main/ovl.c` turns that canvas into runs of 32-bit words (colour spread as 565 with 0..32 of
  see-through), runs of eight or more equal pixels as one word, three buffers (shown, ready, building).
- The sweep is not drawn by the UI at all: the panel's fill works it out per row from the time the panel frame
  started, so it turns smoothly even when the layer is late.
- The fill lays the layer over each chunk of the frame in the bounce buffer just before the panel scans it, with
  a deadline per row. An SPI or QSPI panel keeps its own frame memory and has no feed to lay anything onto, and
  the other ESP32 chips have no PIE: this suits an S3 with an RGB panel or a P4 with DPI or MIPI-DSI.
- The weather dial on these boards plans its tables in the background's first strip of each frame, from the
  background task's own copy of the UI (P).

Bus and panel side: [bus-rgb.md](../../esp32-lcd-touch/references/bus-rgb.md),
[bus-mipi-dsi.md](../../esp32-lcd-touch/references/bus-mipi-dsi.md). The whole design is in FORK-NOTES.md of
OWLOS Flight Radar.

## The renderer travels

There is no shared core between the projects: each repository has its own copy of gfx and font, and this skill
is how the next one gets it. Flight Radar's copy is the newest. FusionEdge (SimZs/FusionEdge, a GPL internet
radio) got a screen of this kind for its boards without PSRAM: Flight Radar's gfx and font (MIT), the five themes
with their roles, Cascadia Mono bakes and the pixel font for labels (M on two boards, see above). A person found
its touch on the 2.8" CYD hard to hit: taps missed the volume, a tap hid the buffer indicator (open).
