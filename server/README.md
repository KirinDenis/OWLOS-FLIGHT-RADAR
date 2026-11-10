# RadarStats

How many OWLOS Flight Radar boards there are, for the map of boards on the site (`site/stats.js`): flashed
from the site, online now, on a cable to the site now, by board, by version and by country.

## What it keeps

- Counts only. A report says a build (`st7796`) and a version (`0.12`), nothing else.
- The address a report comes from is turned into a country by DB-IP's free IP to Country Lite and
  dropped. It is not written anywhere and not logged. It is the rate limiter's key for one minute, in
  memory only.
- Russia is not counted at all (`IgnoredCountries`).
- `data/counts.json` holds the flashes by build, version and country, and the page visits by day and
  country. Boards online and boards on a cable are the reports of the last few minutes, in memory.
- A board reports only when its owner allowed it, and a page only when its visitor did. A page visit is a
  number a day and a country, no cookie.

## The API

- `GET /api/stats`: the counts, as `site/stats.js` reads them (its first lines say the form).
- `POST /api/flash` `{"build":"st7796","version":"0.12"}`: the page, after a flash it made went through.
- `POST /api/alive` `{"build":"st7796","version":"0.12"}`: a board, every `AliveMinutes`. The reports in
  that long are the boards online now: nothing tells one board from another, so a board that reports
  twice in a window counts twice, one that missed one counts once less.
- `POST /api/cable` `{"n":2}`: a page with boards on its cable, every `CableMinutes`.
- `POST /api/visit`: a page shown.
- `GET /api/visits`: the visits by day and country, for us. It answers only on the server itself
  (`curl http://127.0.0.1:5088/api/visits`), and Caddy does not pass that path on.

A report with a build the service does not know, or a version that is not one, is answered 400. More
than `RequestsPerMinute` requests a minute from one address: 429. A body over 1 KB: 413. Only the
`AllowedOrigins` pages may ask from a browser.

## Build and run

.NET 8. The code style is the pilot's: no `var`, a body in braces always; the build stops on either.

```
dotnet build -c Release
dotnet bin/Release/net8.0/RadarStats.dll
```

It listens on `http://127.0.0.1:5088` only (`appsettings.json`, `Urls`): Caddy in front of it does TLS
and gives it the client's address in `X-Forwarded-For`, which it takes from the loopback alone.

The country table: DB-IP's "IP to Country Lite", the CSV, from https://db-ip.com/db/download/ip-to-country-lite
(CC BY 4.0: the site says "IP to country by DB-IP" where it shows the countries). Unzipped to
`data/dbip-country-lite.csv`. Without it every report counts as country `ZZ`: in the totals, on no
country of the map.

## On the server

`deploy/radar-stats.service` runs it under systemd, `deploy/Caddyfile` is the site block for Caddy. See
`deploy/README.md`.
