# adsb.lol

A community ADS-B network, free, no key. OWLOS Flight Radar's free server 0 (`ADSB.LOL`), for the planes round home and for
the watch list's lookups. Its answers are in the ADS-B Exchange v2 format: `../flight-data.md` section 2.

How sure: run on the boards against the real server since the first public version. [measured]

## URL shape

Planes round a point, radius in nautical miles:

```
https://api.adsb.lol/v2/point/<lat %.2f>/<lon %.2f>/<NM>
```

- `lat`, `lon`: home rounded to 0.01 degree (`vague()`), about a kilometre.
- `NM = ceil(ask_km / 1.852)`, capped at 250. `ask_km` is the range times 1.6 (the disc) or 2.4 (the square map).

One plane, for the watch list:

```
https://api.adsb.lol/v2/callsign/<id>
https://api.adsb.lol/v2/reg/<id>          note: reg, where adsb.fi says registration
https://api.adsb.lol/v2/hex/<id>
```

Headers: `Accept: application/json` and the board's own User-Agent (`../https-client.md`).

## Fields used

The list is `ac`. Every ADS-B Exchange v2 field of `../flight-data.md` section 2, and `now` at the top level: the server's
clock in milliseconds, written AFTER the list. The reader dates every position by it. [measured on recorded answers]

## Limits and terms

- Asked every 5 s. The code's comment says a request a second at most.
- Their docs page (`api.adsb.lol/docs`) is a script app: a plain fetch gets only its title (tried 2026-11-04 and
  2026-11-23). [docs]
- The API's README, read 2026-11-23: "Rate limits are dynamic based on the environment load"; 4xx answers mean the client
  does something wrong; an API key will be needed in future, obtained by feeding adsb.lol. It says it is a drop-in
  replacement for the ADS-B Exchange Rapid API. It does not state the data's licence. [docs]
- The project names the data as ODbL and credits adsb.lol in its README. Name it, link it.

## Size and parsing

- About 750 bytes to 1 KB a plane. A busy sky is 100 planes; at a 200 km range some 165 KB come every few seconds.
  [measured]
- Never held whole: `net_get` hands 1 KB pieces to the streaming reader `sky_read`, which keeps one plane's object at a time
  and the nearest 48. `../flight-data.md` section 4.
- A watch answer is about 750 bytes a plane, read by the same reader with no merge.

## Failures met

- HTTP 429 when the planes and the watch lookups both went to adsb.lol. Fix: lookups go to the other free network, a lookup is
  skipped in a round that got 429, and the wait is multiplied by 3. [measured]
- A comma list of callsigns or registrations returns only the first. `hex` alone takes several. So one plane a request.
  [measured]
- A registration without its dash finds nothing: OE-IJW finds it, OEIJW does not. Ask with the dash; drop dashes only when
  matching planes in hand. [measured]
- Callsigns are case-insensitive. [measured]
- A failed or strange answer moves the next request to adsb.fi by itself. [built]

## Routes

adsb.lol also serves the VRS standing data's routes by callsign: `vrs-standing-data.md`. Not used by the firmware.
