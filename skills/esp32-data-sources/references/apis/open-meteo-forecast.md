# Open-Meteo forecast

Open-Meteo's weather forecast API, no key. OWLOS Flight Radar's WEATHER look (since 0.16.1 beta): now, the next 24 hours an
hour at a time, the next 4 days, and the day before for the pressure and temperature graphs. The same API serves two of the
board's three weather questions; the third is `open-meteo-air-quality.md`. What the board makes of the answers (the warnings,
the units, the sun) is in `../weather.md`.

How sure: run on the bench boards (the 2.8" CYD, the 3.5", the round C3) since 0.16.1, the log line `weather: ... past read`
seen on the CYD. [measured] The parser runs in the PC preview and the site's twin too.

## URL shape

The forecast, for home rounded to 0.1 degree (about 10 km):

```
https://api.open-meteo.com/v1/forecast?latitude=<%.1f>&longitude=<%.1f>
  &current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,pressure_msl,
           wind_speed_10m,wind_direction_10m,wind_gusts_10m,uv_index
  &hourly=temperature_2m,weather_code,cloud_cover,precipitation,pressure_msl,wind_speed_10m,
          wind_direction_10m,wind_gusts_10m,uv_index
  &daily=weather_code,temperature_2m_max,temperature_2m_min
  &timezone=auto&timeformat=unixtime&forecast_hours=24&forecast_days=5
```

The day before, its own short question:

```
https://api.open-meteo.com/v1/forecast?latitude=<%.1f>&longitude=<%.1f>
  &hourly=pressure_msl,temperature_2m,weather_code,precipitation
  &timezone=auto&timeformat=unixtime&past_hours=24&forecast_hours=1
```

- `timeformat=unixtime`: times as seconds since 1970, no date strings to parse.
- `timezone=auto`: the answer carries `utc_offset_seconds`, so the board shows home's clock with no time zone table.
- A separate question for the past, because `past_hours` on the forecast brings the past of every hourly field and would not
  fit the 4 KB buffer.
- Built by `wx_url_forecast` and `wx_url_past` in `core/wx_parse.c`, shared by the firmware, the preview and the twin.

## Fields used

- Top level: `utc_offset_seconds` (home's clock, kept in minutes), `elevation` (the forecast point's height, kept when between
  -500 and 9000 m: it turns sea-level pressure into the barometer's at home).
- `current`: `time`, `temperature_2m`, `apparent_temperature` (else the temperature), `relative_humidity_2m`, `weather_code`,
  `pressure_msl`, `wind_direction_10m`, `wind_speed_10m`, `wind_gusts_10m`, `uv_index`.
- `hourly`, 24 of each: `time` (only the first is read), `temperature_2m` (all 24 must come, else the answer is not taken),
  `weather_code`, `cloud_cover`, `precipitation`, `pressure_msl` (a missing hour takes now's), `wind_direction_10m`,
  `wind_speed_10m`, `wind_gusts_10m`, `uv_index`.
- `daily`, 5 of each: `time`, `weather_code`, `temperature_2m_max`, `temperature_2m_min`. Today is the first, so days 2 to 5
  are kept. The weekday is worked out from the first day's midnight and the offset: 1970-01-01 was a Thursday.
- The past: `pressure_msl`, `temperature_2m`, `weather_code`, `precipitation`, matched to the forecast's hours by time, not by
  index. A pressure outside 800 to 1200 hPa is refused.

Kept in `weather_t` (`core/weather.h`) as small integers: temperatures in tenths of a degree C, pressure in tenths of hPa at
sea level, wind in km/h, rain in tenths of a mm, UV in tenths, the wind's direction in 2 degree steps.

WMO weather codes to the board's eight kinds (`kind_of`):

- 0, 1 clear; 2 partly cloudy; 3 cloudy; 45, 48 fog; 51 to 57 drizzle; 61 to 67 and 80 to 82 rain; 71 to 77, 85, 86 snow;
  95 and up a storm (thunder). Anything else, or none, cloudy.

## Size and parsing

- The forecast's answer is about 2.6 KB; the day before about 2 KB (0.7 KB when it asked only the pressure). [measured, from
  the log and the code's comment]
- Held whole in one 4 KB buffer (`malloc` for the round, freed after). An answer that filled the buffer is not read: it may be
  cut. [built]
- No JSON library. Open-Meteo's answer is flat objects of numbers and arrays of numbers, so a few lines do:
  - `object(json, "hourly")` finds `"hourly":{` with `strstr`.
  - `field(obj, name, tail)` finds `"name":` inside that object, before its first `}`.
  - `number` reads one with `strtof`; a `null` is "not given".
  - `array` reads up to n numbers; a `null` becomes NAN, which each field then treats as missing.
  - Times are read with `strtoul`, never as floats: a float has not the digits for seconds since 1970.

```c
// an array of numbers, up to n of them, a null as NAN: how many came
static int array(const char *obj, const char *name, float *out, int n)
{
    const char *p = field(obj, name, "[");
    if (!p) return 0;
    int i = 0;
    while (i < n && *p && *p != ']') {
        if (*p == 'n') {
            out[i++] = NAN;
            p += 4;
        } else {
            char *e;
            out[i++] = strtof(p, &e);
            if (e == p) break;
            p = e;
        }
        if (*p == ',') p++;
    }
    return i;
}
```

## When it is asked

- Every 30 minutes, a minute on after an answer that did not read, at once for a new home (`wx_service` in `main.c`).
- Only after the planes' question, on the same fetch task, one request at a time.
- The forecast first. Only a forecast that read is followed by the air and the past.
- Two `weather_t`: the screen reads one while the next is filled, then the index flips under the lock.
- The day before not read: the last answer's past, moved on by the hours between (`wx_past_from`), its own past where that
  reaches, then its forecast's hours, which are past by now.
- The site asks the same three questions from the browser, only while its WEATHER look is up, every 30 minutes, 60 s after a
  miss.

So a board makes 2 forecast calls and 1 air call each half hour: 144 calls a day. [reasoned]

## Limits and terms

Read 2026-11-23 from `open-meteo.com/en/terms`. [docs]

- The free API is for non-commercial use only.
- Under 10,000 calls a day, 5,000 an hour, 600 a minute (300,000 a month in their table).
- Limits count per IP address and per application. They may block an application or address that misuses the service,
  without notice.
- No key for the free API.
- The data is CC BY 4.0. The WEATHER look names Open-Meteo in its corner, and the README credits it.

## Failures met

- A weather buffer of about 1.5 KB more in static memory broke the 3.5"'s square map (0 of 6 tiles, every time): the tile
  reader had only about 1 KB to spare. Fixed in the tile reader and the screen's strips. [measured, `../maps.md`]
- `weather: HTTP <code>, <n> bytes, not read` in the log; the screen keeps the last answer it read.
