# adsb.fi open data

A community ADS-B network, free, no key. OWLOS Flight Radar's free server 1 (`ADSB.FI`), for the planes round home and for
the watch list's lookups. Its answers are in the ADS-B Exchange v2 format: `../flight-data.md` section 2.

How sure: run on the boards against the real server. [measured]

## URL shape

Planes round a point:

```
https://opendata.adsb.fi/api/v3/lat/<lat %.2f>/lon/<lon %.2f>/dist/<NM>
```

- Home rounded to 0.01 degree. `NM = ceil(ask_km / 1.852)`, at most 250, as for adsb.lol.
- The old `https://opendata.adsb.fi/api/v2/lat/<lat>/lon/<lon>/dist/<NM>` is deprecated, kept for old clients, and has a
  different format from their other v2 endpoints: the list is called `aircraft`, and `now` is seconds with a fraction, BEFORE
  the list. Flight Radar moved to v3 in 0.5. [measured]

One plane, for the watch list, on v2:

```
https://opendata.adsb.fi/api/v2/callsign/<id>
https://opendata.adsb.fi/api/v2/registration/<id>
https://opendata.adsb.fi/api/v2/hex/<id>
```

- These are not deprecated. `/v3/hex` answers 400. [measured]
- The docs also list `/v2/icao/<hex>`, `/v2/sqk/<squawk>`, `/v2/mil`, and a feeder's `/v2/snapshot`. Not used. [docs,
  2026-11-23]

## Fields used

v3 has the shape of adsb.lol: `ac` first, then `msg`, `now`, `total`, `ctime`, `ptime`. `now` is the server's clock in
milliseconds, after the list. Every field of `../flight-data.md` section 2. The answers also carry `track_rate` and `roll` for
many planes; tried as the source of a plane's turn on recorded answers, no better than two fixes, so not read. [PC test on
recorded answers]

## Limits and terms

Read 2026-11-04 from `github.com/adsbfi/opendata`, the same on 2026-11-23. [docs]

- Public endpoints: 1 request a second. The feeder endpoint: 1 every 30 s.
- Answers 400, 401, 403, 404 and 429 count toward a temporary IP restriction. So a client must not hammer a wrong URL.
- Radius at most 250 NM.
- Personal, non-commercial use only. No licensing, selling, renting or leasing the data. Cite adsb.fi and link their home page.
  They may suspend the service or a user's access.

The board asks every 5 s.

## Certificates

adsb.fi sends its GTS Root R4 signed by an old GlobalSign root that is not in ESP-IDF's common bundle, while the same GTS root
is trusted on its own. ESP-IDF 6 builds need `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`; 5.5 warns that it does
not know the line and skips it. [built here on 5.5; run by bertvm on 6]

## Size and parsing

The same as adsb.lol: about 750 bytes to 1 KB a plane, streamed through `sky_read`. The move from v2 to v3 was one URL string:
the reader takes `ac` and `aircraft`.

## Checked 2026-11-04

- v2 and v3 both answered 200 with the same planes.
- The reader read both answers fed in pieces of 1, 7, 512 and 4096 bytes. [measured]
- A 2.8" CYD on adsb.fi only: seven fetches in a row, HTTP 200, heap steady. [measured]
- The v2 form is deprecated with no end date. If it ever stops, the firmware moves to the next server by itself
  (`next = (server + 1) % FREE_SERVERS`).

## Failures met

- The watch list by registration needs the dash, as on adsb.lol. A comma list returns only the first. [measured]
- `/v3/hex` answers 400 [measured], and a 400 counts toward their IP restriction [docs]. The watch list stays on v2.
