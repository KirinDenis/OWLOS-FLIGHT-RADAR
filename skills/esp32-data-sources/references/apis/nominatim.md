# Nominatim (OpenStreetMap geocoding)

OpenStreetMap's own geocoder. OWLOS Flight Radar asks it two things: what home is called and which country it is in (reverse,
once a home), and where a typed town or postcode is (search, only when a person presses FIND). The site does the same from the
browser.

How sure: both run on the boards against the real server. [measured]

## URL shape

The reverse lookup, with home rounded to 0.01 degree, the same point the plane servers get:

```
https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=10&accept-language=en&lat=<%.2f>&lon=<%.2f>
```

- `accept-language=en` because the board's font is Latin first. `zoom=10` is town level.

The search, home typed on the screen (square touch boards; the round board and the RPi LCD take home from the site):

```
https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&addressdetails=1&accept-language=en&q=<url-encoded text>
```

- The text is encoded by `url_words`: letters, digits, `-` and `.` as they are, everything else as `%XX`.
- The site's search, in the browser: the same without `addressdetails`, `encodeURIComponent` for the text.

## Fields used

- The place: the first of `city, town, village, municipality, hamlet, county, state` that is there; for a search with none of
  them, `name`.
- `country`: the country's name, for the screen and for the postcode rule below.
- `country_code`: two small letters (`json_str(buf, "country_code", ...)`). Kept in RAM only (`s_home_cc`), asked again after
  each power on. It chooses the weather's AUTO units (`../weather.md`), and the refusal below.
- Search: `lat` and `lon` come as STRINGS in this answer: `strtof` after `json_str`.

```c
static const char *const PLACE_KIND[] = { "city", "town", "village", "municipality", "hamlet", "county", "state" };
for (int i = 0; i < N && !place[0]; i++) json_str(buf, PLACE_KIND[i], place, sizeof place);
json_str(buf, "country", country, sizeof country);
json_str(buf, "country_code", code, sizeof code);
```

`json_str` turns `\uXXXX` into UTF-8: town names need it.

## Size and parsing

- An answer is 600 to 700 bytes [measured], held whole in a 2 KB buffer, fields picked by name.
- Memory: the reverse lookup and the search share one `s_osm_url[420]` and `s_osm[2048]`. A search with 3.8 KB of static
  buffers of its own had dropped the lowest free heap from 10.5 to 6.4 KB on the ESP32. [measured] Both run on the fetch task,
  one at a time. Since 0.16.2 the same bytes are also the map reader's working rows (`MAP_SCRATCH`, 3.6 KB, a union), used
  only while a tile is read, never at the same time.

## The rules the board keeps

- The reverse lookup runs once a home, after power on too, before anything is asked round it. A home it did not answer about
  is asked again five minutes on. A `static` pair `asked_lat`, `asked_lon` stops a retry loop for the same point. The names
  are saved to NVS only when a new one came.
- Search runs only on FIND. At most two searches, 1100 ms apart. No search while typing.
- `48.9 18.1` or `48.9, 18.1` is taken as a point: no WiFi needed, the name is asked later by the reverse lookup.
- Text with a digit in it is tried first as `<text>, <home's country>`, then plain. A bare postcode is ambiguous worldwide:
  10115 came back as Santo Domingo, and a viewbox bias did not fix it. [measured]
- A name is never given a country: a capital's name with another country appended found a house of that name in that country.
  [measured]
- Full UK postcodes (EH1 1AA) are often missing; the outward code (EH1) works. [measured]
- The answer's first hit, a town's centre, becomes home.
- What the screen says: `HOME: <place>` (or `HOME FOUND` when that is wider than the line), `NO WIFI: TYPE LAT LON`,
  `NO ANSWER: TRY AGAIN`, `NOT FOUND: TRY ANOTHER`.
- The site: a typed town goes through search, no town means reverse from the numbers, with the same digit rule and the same
  1100 ms wait.

## A refused country

- The board asks for no planes and no map at a home whose `country_code` is on its refusal list (`ru`), and the screen says
  NOT AVAILABLE IN RUSIA (spelled so on purpose). The stats server does not count that country either. The project's choice,
  since 0.12. [measured]
- OpenStreetMap puts Crimea and the occupied towns in `ua`: asked on 2026-11-10 for seven of them, all `ua`; the towns on
  checked beside them came back as expected (Narva `ee`, Ivangorod and Belgorod `ru`). [measured]
- Nominatim silent: the board goes on as ever. A refusal never holds anyone else up.

## Limits and terms

Read 2026-11-04 from `operations.osmfoundation.org/policies/nominatim/`. [docs]

- At most 1 request a second, counted for the application as a whole, all users together.
- A custom User-Agent or a valid Referer that identifies the application. Library defaults are refused.
- Display attribution. The data is ODbL.
- Periodic requests from apps count as bulk geocoding and are strongly discouraged.
- Forbidden: client-side autocomplete, systematic queries (grids of reverse lookups, all postcodes), scraping details.
- Apps should cache, and must be able to be switched off without a software update.

Gaps: there is no remote switch. It is a promise the firmware cannot keep without a server of its own. A new product with many
users should think about that first, and about the 1 request a second shared by ALL boards.

## Failures met

- After several quick restarts in a row Nominatim refused the board, and the country stayed empty for 5 to 10 minutes (the
  retry is every 5). A settings diff that shows only the country code empty right after flashing is this, not lost settings.
  [measured]
