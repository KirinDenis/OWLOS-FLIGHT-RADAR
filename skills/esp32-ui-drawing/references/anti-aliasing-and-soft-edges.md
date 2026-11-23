# Anti-aliasing and soft edges

Every smooth shape here works the same way, on an LCD and on paper: the coverage of a pixel is its distance to
the shape's edge. The primitives are in [code-gfx.md](code-gfx.md) (`gfx.c`), the paper ones in
[code-paper.md](code-paper.md) and in Flight Radar's `core/paper.c`. Canvas and colour maths:
[renderer-and-canvas.md](renderer-and-canvas.md).

How sure: (M) seen on a real board, (P) built and seen in the preview or read in the code, (R) reasoned.

## Coverage is distance

For a pixel, find its distance to the shape's edge: that distance is the coverage. 1 px of soft edge, no
supersampling.

```
cov = half_width + 0.5 - distance     // <= 0: nothing, >= 1: solid, between: the soft edge
plot(row, x, colour, alpha * cov)
```

## The primitives (gfx.c)

- **Line** (`gfx_line`): a capsule. Distance to the segment, round ends. Each row walks only the span the line can
  reach, or long steep lines would cost their whole bounding box. A line thinner than 1 px is drawn 1 px wide and
  fainter.
- **Ring, disc** (`gfx_ring`, `gfx_disc`): distance to the centre. The ring jumps over its hole. Air Quality's disc
  fills the inside of a row as a flat span, only the rim needs a root. Flight Radar's disc has a whole number path
  for the C3 (`#if GFX_SOFT_FLOAT`).
- **Polygon** (`gfx_tri`, `gfx_poly`): convex, up to 12 corners, either winding. Each edge is set up once as
  `ex*x + ey*y + e0` in 1/256 px, then steps by one add a pixel. Coverage is the smallest edge value + 0.5. Rows are
  pruned to the span where every edge leaves coverage. Non-convex shapes are a few convex ones overlapping: that is
  how the planes are made, and how a tapered needle is made (a four-corner polygon wide at the hub, 0.3 px at the
  tip).
- **Glow** (`gfx_glow`): additive, `(1 - d^2/r^2)^2`, no root at all.
- **Dot** (`gfx_dot`): a point shared over its 2x2 pixels by sub-pixel position: a trail that does not jitter, and
  the weather dial's small stars.
- **Arc** (`gfx_arc`): a polyline, one segment per 4 degrees, 0 = up, clockwise. Air Quality's `gfx_sector` is the
  better one: a true annular sector with soft ends, comet fade, dashes and a moving glint (code-gfx.md).

The fast root, because the ESP32 has no sqrt instruction and AA wants one per pixel. About 0.2 % off, plenty for AA:

```c
static inline float gfx_sqrt(float x)
{
    if (x <= 0.0f) return 0.0f;
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759dfu - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}
```

On the C3 the radius of a pixel is walked in whole numbers instead: the radius squared grows by `2a - 1` a pixel
along a row, and the root in 1/16 px is stepped up or down from the last one (the scope's disc pass and the weather
dial, P).

## Shapes drawn by a pass of their own

When a shape is too big or too many for primitives, it is drawn pixel by pixel in one pass with the same rule:

- The scope and the weather dial: one pass over the disc, the radius and angle of a pixel from tables, every ring a
  coverage by its distance in 1/16 px (radial-dials.md).
- The graph's line (`graph_curve`): one column of the glass at a time, the line's height between two planned
  points, as thick across as along wherever it leans (`slean`, 16 = flat), its glow and the fill under it, each
  pixel once, in whichever strip. No joins drawn twice (data-screens.md).
- The moon: a disc whose lit side is half an ellipse `cos(phase)` wide, its edge and the terminator both smoothed
  by distance.
- The barometer's band: an annulus drawn pixel by pixel, its colour by the angle, its edges soft.

## Ticks that stay sharp

The scope's tick marks are not lines. A pixel finds the nearest of 72 tick lines by its angle, then its exact
distance to that line from that line's sine and cosine: `cov = 192 - distance`. So a tick stays sharp at the rim
however the map turns (radial-dials.md).

## Soft edges on e-paper

Paper has 4 greys and a partial refresh that only knows black and white. Two rules came from the glass:

1. **What changes between full prints has crisp edges.** Flight Radar draws its paper screens in inks, one channel
   each, and `paper.c` reads which channel is over half (P_BLACK red, P_DARK green, P_LIGHT blue, P_WHITE 0;
   black already from 90 of 255, since a W's thin middle strokes are only a third covered and went missing at
   half). A soft edge between two inks rounds to one of the two, never to a third grey: black over the light grey
   water comes out black or water. So the gfx and font AA comes out crisp, and what moves stays black and white (M,
   4.2").
2. **What is printed whole may have soft edges in grey steps.** `canvas_t.soft` says so. Then a letter's edge, a
   line's edge or a round's edge is a step of grey between the ink and what is under it, by coverage:

```c
uint32_t paper_soft(uint32_t under, uint32_t ink, int k)   // k of 256 of the pixel covered
{
    static const uint32_t INK[4] = { P_WHITE, P_LIGHT, P_DARK, P_BLACK };
    const int u = paper_dark(under), i = paper_dark(ink);
    const int step = k >= 115 ? 3 : k >= 60 ? 2 : k >= 30 ? 1 : 0;   // thirds of the way from under to the ink
    return INK[u + (i - u) * step / 3];
}
```

The black reaches as far as a crisp edge's, the greys lie outside it: smoother, no thinner, never lighter than
what is there (M, 4.2"). `paper_line` (a line by its distance, round joints, the darker of two kept, so a polyline
bends without seams), `paper_disc` and `paper_ring` use it where the canvas is soft, and fall back to gfx where it
is not. The weather pages, the printed radar's scope and rim, and the key strip are soft; clocks, the pressed key's
third and everything drawn on top between prints are not (e-paper-design.md).

Where soft cannot step, use a pattern: there is no ink between the paper and the light grey, so the printed
radar's light band edge is a chequer of the two (M).

The WiFi Radar's paper (an older way, M on the 5.79"): every pixel goes to the nearest of the 4 greys, no dither.
Font AA stays: the soft edges round to the two greys, and that is the smoothing.

## Text

Baked fonts are 4-bit coverage: `k = (a * (v * 17 + 1)) >> 8`, so 15 gives 256. The same `put_px` mixes on glass
and steps on soft paper (fonts-and-font-baking.md).
