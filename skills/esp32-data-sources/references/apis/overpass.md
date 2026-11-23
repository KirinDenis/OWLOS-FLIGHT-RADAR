# Overpass API: tried, refused, never for names

OpenStreetMap's query server. Looked at once, for a list of towns round home with their names, to draw the map's labels in the
board's own font.

- From a non-browser client it refused the request: its answer says the service is meant for browsers and people. [measured, one
  try, not from a board]
- The decision that followed: the map's names come from the tile pixels on the board, and from nowhere else. No Overpass, no
  GeoNames or other place list, no server of the project's own serving place names. If something cannot be done from the tiles,
  say so plainly (`../maps.md` section 6).
- Places with opening hours from OSM, once a day, were talked over for a separate firmware and not built. Any such use must read
  Overpass's own usage policy first. [reasoned]
