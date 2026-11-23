# VRS standing data: routes by callsign (researched, not built)

Virtual Radar Server's standing data: aircraft routes by callsign and the airports, kept by the VRS project and mirrored by
adsb.lol. Looked at twice for "from where to where" without FR24: the free networks give a position, never a route. OWLOS Flight
Radar shows a route (ORIG > DEST) only from FR24 today.

How sure: [docs] and [measured] by fetching the files on the build PC, 2026-11-19. Nothing of it is in the firmware.

## What is there

- Licence CC0-1.0, both `adsblol/vrs-standing-data` and upstream `vradarserver/standing-data`: the file may be prepared and hosted
  by the project itself.
- `routes.csv`: 19 MB, 629k callsigns, ICAO airport codes (EGLL-KJFK), 34k with stops. NOT sorted by callsign.
- `airports.csv`: 2.5 MB, 34k rows with IATA codes and lat/lon.
- One callsign as JSON:

```
https://vrs-standing-data.adsb.lol/routes/<first 2 letters>/<CALLSIGN>.json
```

  About 0.8 KB: IATA codes and both airports' lat/lon. `Access-Control-Allow-Origin: *`, `max-age=600`, the data renewed hourly.

## What was found

- Routes are by callsign, and some are stale. Show one only if the plane is near the great circle between the two airports and
  heading toward the destination (adsb.lol checks plausibility too). [reasoned]
- An earlier contribution took routes from adsbdb (`adsbdb.md`): about 1 route in 5 did not fit the plane [measured]. VRS's data
  was noted then as no better; how that was checked is not recorded.

## The advice given, not built

- First: the per-callsign JSON for the picked plane only. Every board, nothing to install, one small request when a plane is
  picked.
- Later, for offline: the files on an SD card. On the 2.8" CYD the card slot's pins (5, 18, 19, 23) are free of the LCD and touch.
  FAT with 4096-byte sectors and a per-file cache takes about 9 KB while a file is open; 512-byte sectors would make it about 1 to
  2 KB. `routes.csv` would want sorting by callsign first, for a binary search. [reasoned]
