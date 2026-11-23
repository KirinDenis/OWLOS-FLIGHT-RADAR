# Waveshare ESP32-S3-Touch-LCD-7B, 1024x600 (build `wave7b`)

The 7B: the Waveshare 7's board with a 1024x600 panel and an N16R8 module, the same library and the same moving layer.
Everything shared with the 7 is in [wave7](wave7.md); this file holds what differs.

- Build: `wave7b`, `RADAR_DISPLAY` 16 (`DISPLAY_WAVE7B_1024x600`), target esp32s3. `RADAR_OVERLAY` 1. build.sh's
  default. Released in 0.14.
- How sure: brought in by chipguyhere, who runs it on one (README; the fork has a video of it on live traffic). Every
  number below is chipguyhere's, measured on that board (FORK-NOTES.md). Not run here. Experimental on the site.

## Chip, flash, PSRAM

ESP32-S3-WROOM-1 N16R8 ([esp32-s3](../chips/esp32-s3.md)): 16 MB flash, 8 MB octal PSRAM. The build uses 8 MB of flash
(`partitions_wave7.csv`), like the 7.

## Screen

1024x600 on the RGB bus. The layout is the 5" boards' 15/8 as big (547x320: 1024 / 1.875 = 546.1, the half pixel past
the edge clipped). The fonts are the 5" boards' 3/2 ones, so the writing is 4/5 of its size; a 15/8 font bake is to
come. Chamfered corners at 15/8 are drawn with lines through the pixels' middles so the cut corners meet the sides.

## board.h

Shared with the 7: see [wave7](wave7.md).

## Measured (chipguyhere, 2026-11-11 and 2026-11-14)

- Panel 32.7 Hz, 0 fills dropped, 0 desyncs; the README gives about 30 frames a second for the sweep on the 7B.
- Worst chunk, interrupt to overlay done: about 0.95 ms (deadline-bounded).
- The moving layer is made in about 62 ms on core 0, so it changes about 15 times a second; the background is redrawn
  whole every ~1.9 s on core 1.
- Internal RAM: about 8 KB free, 3 KB at the least (too tight). After moving the fetch, console and log stacks to PSRAM
  (safe with XIP from PSRAM): 45 KB free, 31 KB in one piece, 10 KB at the least through three tile downloads. With
  big state in PSRAM: about 24.8 KB free in steady state, 6.9 KB at the least.
- Plane fetches after the first take 11 to 19 s (not the fork's doing). The kept HTTPS connection is hardly ever reused,
  as the fetch task changes host between requests.

## Open

The screen goes dim on its own sometimes and does not come back (seen twice). Re-sending the backlight every 10 s does
not bring it back. The 10 s log says the expander took the write and its lines are right; the battery input reads 4.16
to 4.49 V with no battery fitted. If the lines stay right while it dims, suspect the power path: the 7B's charger, or
the USB port's current under a 7" backlight.

## Sources

chipguyhere's 7/7B library, the fork chipguyhere/OWLOS-FLIGHT-RADAR-7-7B, FORK-NOTES.md.
