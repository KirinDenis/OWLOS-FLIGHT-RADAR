# OpenStreetMap standard tiles

OpenStreetMap's own raster tiles, 256 x 256 PNG. OWLOS Flight Radar downloads them itself, keeps them in a flash partition and
reads them into its map. What is done with a tile once it is in flash (the projection, the 4-bit texels, the inflater, the
names read from the pixels) is in `../maps.md`.

How sure: run on the 2.8" and 3.5" CYD and the round C3 since the first public version, soaked on the bench. [measured]

## URL shape

```
https://tile.openstreetmap.org/<z>/<x>/<y>.png
Accept: image/png
User-Agent: the board's own (../https-client.md)
```

```c
int map_url(const tile_id_t *t, char *buf, int cap)
{
    int32_t n = (int32_t)1 << t->z;
    if (t->y < 0 || t->y >= n) return 0;                       // past the poles: no such tile
    int32_t x = ((t->x % n) + n) % n;                           // round the world at 180 degrees
    snprintf(buf, (size_t)cap, "https://tile.openstreetmap.org/%d/%ld/%ld.png", t->z, (long)x, (long)t->y);
    return 1;
}
```

Zoom 2 to 17, the one whose tile pixel comes nearest a screen pixel at the range shown. At most 16 tiles a map (32 on the
scaled big screens), nearest home first.

## How they are fetched (`tiles_fetch`, `firmware/main/tiles.c`)

- Only the tiles the current view needs and that are not kept already.
- One connection for all the missing tiles of a round (`keep_alive_enable`), two tries a tile: the server may have closed the
  kept connection.
- Each tile goes from the HTTP client straight into flash in 1 KB pieces. It never exists in RAM. Only
  `esp_http_client_is_complete_data_received` and a good write make a tile: its 32 byte header is written last (`../maps.md`).
- Anything but a whole tile closes the connection: a tile cut short (TLS short of memory) left the connection without its
  buffers, and the next request written into it crashed the 3.5" (LoadProhibited, a memcpy to NULL inside mbedTLS). [measured,
  fixed in 0.10]
- A `set_url` that fails (no memory for the new address) makes a new client: the old one would ask for the last tile again,
  and that tile would be kept under this one's name for good.
- No answer twice, a tile cut short, 403 or 429: the round stops and the rest wait for the next round. Asked one by one, a
  server that said nothing cost 18 s a tile and held the planes back for minutes. [measured]
- A non-200 answer is flushed (`esp_http_client_flush_response`) and logged with its address.
- Missing tiles are asked again every 15 s. A new picture that could not be made (none of its tiles, or no memory for it) is
  asked again 15 s on, not every round: each try costs TLS handshakes, and on the 3.5" every 5 s of them broke the heap into
  pieces. [measured]

## What comes

- PNG, palette (8, 4, 2 or 1 bit), grey or RGB, never interlaced. Sea and plain land come as 1, 2 or 4 bit palette tiles. Up to
  0.13 the board read only 8 bit: those tiles were refused at once ("made in 1 ms"), kept for good, and the map kept holes over
  the sea and plain land. Fixed in 0.14; a kept tile that will not read is now dropped (its header's magic cleared in flash, so
  it is not kept again after a restart) and asked for again. [measured]
- A tile bigger than 160 KB is not kept (`MAX_TILE`).
- The colours of OSM's standard style do not change from tile to tile, but each tile has a palette of its own: colours are
  matched, not looked up (`../maps.md`).

## Over weak WiFi

Not-whole maps on the bench came from tiles that "came broken" over weak WiFi: at -84 dBm 3 of 112 maps, at -85 dBm 9 of 113, on
the round C3 at -92 dBm 34 of 52 with 7 WiFi drops. Idle, every map was whole. [measured]

## The tile usage policy, and where the board stands

Read 2026-11-04 from `operations.osmfoundation.org/policies/tiles/`. [docs]

- Only `https://tile.openstreetmap.org/{z}/{x}/{y}.png`: yes.
- Visible attribution on the map, not behind a toggle: `(c) OpenStreetMap` is drawn wherever the map shows (`attribution()`,
  `\xC2\xA9 OpenStreetMap`), on a solid card, kept out of the labels' way.
- A distinct, stable User-Agent naming the app, contact optional, no library default: `NET_USER_AGENT` on every request.
- Honour cache headers, or if you cannot read them keep a tile at least 7 days: tiles stay until the room is needed, usually far
  longer.
- Conditional requests to revalidate: NOT done. A kept tile is never asked again, so it can grow old. Harmless for a map, but a
  gap.
- Only tiles the user is viewing, modest look-ahead: `map_plan` asks only what the current view shows.
- No bulk download, no preseeding, no "save region for offline": none. Never add one. What was shown stays and works without
  WiFi: that is a cache. A "download my area" button is not.
- Make the tile URL configurable (recommended): NOT done, it is a constant in `map_url`.
- Traffic may be blocked without notice: so the map must fail soft. The scope works with no map.

The licence text: map data (c) OpenStreetMap contributors, ODbL, with a link on the site and in the README.

## In the browser

The site's radar page and the twin fetch their own tiles from the page, not through the board.
