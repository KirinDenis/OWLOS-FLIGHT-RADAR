# RadarStats: the map of boards

The project's own small server, in `server/` of the OWLOS-FLIGHT-RADAR repository. It counts boards for the map of boards on the
site: how many were flashed from the site, how many are online now, how many are on a cable to a page now, by board, by version
and by country. A board reports only when its owner allowed it; a page only when its visitor did.

How sure: deployed and counting since 0.12. [measured] The server is .NET 8.

## Where the address comes from

The firmware has no server address in it. The site's `version.json` (`version-json.md`) carries `"stats": "<https address>"`,
written by the site's CI from `server/public-url.txt`. Empty means nothing is asked or sent, by the boards and by the page. The
board takes it only if it starts with `https://` and has no space, quote, backslash, `?` or `#`; trailing slashes are cut. An
answer that filled the board's 160 byte buffer may be cut in the middle of it: then none.

## What a board sends

```
POST <stats>/api/alive
Content-Type: application/json
{"build":"st7796","version":"0.12"}
```

- Every 10 minutes (`ALIVE_EVERY_MS`), only when SET, DATA, COUNT ME is on (NVS `help`: 0 never asked, 1 yes, 2 no), the address
  is known and WiFi is up.
- Nothing else: no home, no WiFi name, no MAC, no id. The board's build name and its version.
- `net_post_json`, 4 s timeout: a server that is down holds the planes up this long, no more. The answer's body is not read; the
  status goes to the log.

## What a page sends

- `POST /api/flash` `{"build","version"}`: after a flash it made went through.
- `POST /api/cable` `{"n":2}`: how many boards are on its cable, every 5 minutes.
- `POST /api/visit`: a page shown. A number a day and a country, no cookie.
- `GET /api/stats`: the counts, as the site's `stats.js` reads them.

## What the server keeps

- Counts only. The address a report comes from is turned into a country by DB-IP's free IP to Country Lite (CC BY 4.0: the site
  credits it where it shows countries) and dropped. Not written anywhere, not logged. It is the rate limiter's key for one minute,
  in memory only. Without the country table every report counts as `ZZ`.
- One country is not counted at all (`IgnoredCountries`, the same refusal as the board's: `nominatim.md`).
- A file of flashes by build, version and country, and page visits by day and country. Boards online and on a cable are the
  reports of the last few minutes, in memory: nothing tells one board from another, so a board that reports twice in a window
  counts twice.
- Answers: 400 for a build it does not know or a version that is not one, 429 over 120 requests a minute from one address, 413 for
  a body over 1 KB. Only the site's own pages may ask from a browser (`AllowedOrigins`).
- It listens on the loopback only; Caddy in front does TLS and passes the client's address in `X-Forwarded-For`, taken from the
  loopback alone.

## Failures met

- The list of known builds is read once, at start. A board added to the repository needs the server's settings updated and the
  service restarted, or its reports are answered 400 and not counted. Seen when four new builds were missing. [measured]
- Caddy: the site block must use `handle` blocks. A bare `respond 404` beat `reverse_proxy` and answered 404 to the API.
  [measured]
