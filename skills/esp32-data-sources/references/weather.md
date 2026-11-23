# The weather on the board: what the answers become

What OWLOS Flight Radar's WEATHER look (0.16.1 beta on) makes of its sources: the model, the warnings, the units, the sun,
and what stands in for a value nobody gives. The sources themselves: `apis/open-meteo-forecast.md`,
`apis/open-meteo-air-quality.md`, `apis/radiation.md`, `apis/ntp-pool.md`. The air sensors' barometer forecast (Zambretti)
is in `air-quality.md`.

From OWLOS-FLIGHT-RADAR: `core/weather.h`, `wx_parse.c`, `wx_alert.c`, `sun.c`, `ui_weather.c`, `firmware/main/main.c`,
README.md, CHANGES.md. Tags as in SKILL.md.

## 1. The model

`weather_t` is plain data. The firmware fills it from the answers, the PC preview and the site's twin from a sample or from
the same answers. The firmware keeps two, so the screen reads one while the next is filled.

- `at`: UTC of `hour[0]`, the hour now began. 0: no forecast yet.
- `updated`, `utc_min` (home's clock), `elev` (the forecast point's height).
- Now: temperature, feels like, kind, humidity, pressure, wind from, wind, gust, UV, the air index, fine dust.
- `gamma_nsv` and `gamma_km`: the background radiation at the nearest station and how far it is. Always 0 today: no source.
- `hour[24]`, `day[4]`, and the 24 hours before `hour[0]`: pressure, temperature, rain, kind. A past hour is known when its
  pressure is.
- Inside it is all degrees C, hPa at sea level, km/h and mm. The screen turns them into the person's units.

## 2. The warnings

`wx_alerts(w, mask, &a)`, `core/wx_alert.c`, over now and the next 12 hours (`WXA_AHEAD`). Each kind on at first, each one
the person's to switch off (SET, DATA, or the site; console bits 1 storm, 2 pressure, 4 air, 8 radiation).

- STORM: the strongest gust now or ahead, amber from 75 km/h, red from 100. Thunder ahead (or now) is amber at least.
- PRESSURE: the biggest change from now within the 12 hours, either way, amber from 6 hPa, red from 10. The change the body
  feels; said in weather words, no medicine.
- AIR: the worst European index now or ahead, amber from 60 (poor), red from 80 (very poor).
- RADIATION: the nearest station, amber from 300 nSv/h (0.3 microsievert an hour), red from 1000. Waits for a source.
- `wx_alert_worst` gives the worst, the first kind on a tie. A chip under the town's name says it, "+1" for each one more;
  a tap opens WEATHER. One that comes shows for a few seconds and lights the LED's WEATHER row, yellow and slow, second only
  to an emergency squawk.
- A PC test of 25 cases. [PC test] On the boards since 0.16.1. [measured]

## 3. Units, AUTO by home's country

SET, UNITS, each AUTO at first: temperature C or F; pressure hPa or inHg at sea level as forecasts give it, or mmHg at home's
height as a barometer there reads; wind km/h, m/s, mph or knots; rain mm or inches. AUTO goes by the two letters Nominatim
gives for home's country (`apis/nominatim.md`), lists in `ui_weather.c`:

- Fahrenheit: us lr bs bz ky pw fm mh pr gu vi as mp
- inHg: us pr gu vi as mp
- mmHg: by ua kz uz kg tj tm am az ge md
- mph: us gb lr pr gu vi as mp
- m/s: no se fi dk is ee lv lt by ua kz uz kg tj tm am az ge md jp kr cn
- inches of rain: us lr pr gu vi as mp
- everything else: C, hPa, km/h, mm

The conversions:

```c
// inHg, at the sea's level as the forecast gives it
inhg = hpa * 0.02953f;
// mmHg at home's height, the barometer's way: from the forecast point's elevation and the hour's temperature
const float h = (float)W.elev, k = 1.0f - 0.0065f * h / ((float)t10 * 0.1f + 273.15f + 0.0065f * h);
mmhg = (h > 0.0f && k > 0.5f ? hpa * powf(k, 5.257f) : hpa) * 0.750062f;
// a change of pressure in mmHg is scaled the same way: at home's height a little less than at the sea's
```

No extra request: the forecast's own `elevation` is the height. [measured: AUTO, a set unit, a restart and AUTO again checked on
three boards over the console]

## 4. The sun, worked out on the board

No sunrise or sunset is asked of any server. `sun_elevation(lat, lon, utc)` (`core/sun.c`) is the low precision formula of the
Astronomical Almanac, good to a hundredth of a degree for decades: days since J2000, the sun's mean longitude and anomaly, the
ecliptic longitude, right ascension and declination, sidereal time at Greenwich, then the height over home.

```c
float sun_elevation(float lat, float lon, uint32_t utc)
{
    const double d = ((double)utc - 946728000.0) / 86400.0;   // days since 2000-01-01 12:00 UTC (J2000)
    const double mean = fmod(280.460 + 0.9856474 * d, 360.0);
    const double anom = fmod(357.528 + 0.9856003 * d, 360.0) * RAD;
    const double ecl = (mean + 1.915 * sin(anom) + 0.020 * sin(2.0 * anom)) * RAD;
    const double tilt = (23.439 - 0.0000004 * d) * RAD;
    const double ra = atan2(cos(tilt) * sin(ecl), cos(ecl));
    const double dec = asin(sin(tilt) * sin(ecl));
    const double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
    const double hour = (gmst + lon) * RAD - ra;
    const double phi = lat * RAD;
    return (float)(asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(hour)) / RAD);
}
```

- Checked on the PC at the solstices and against a known day's sunrise and sunset. [PC test]
- Sunrise and sunset: the dial's table has the sun's height for each of its 360 degrees (4 minutes each). The first degree
  after now where it crosses the horizon is halved down 8 times, the sun worked out exactly against -0.833 degrees (its
  disc's upper edge), to some seconds. A few dozen calls a minute, not one a degree: the C3 has no floating point unit.
  None in the next day (the far north and south): none is shown.
- The night light (SET, VIEW, NIGHT): dark from 3 degrees under the horizon, about a quarter of an hour after sunset, till as
  long before sunrise; worked out once a minute, with half a degree of hysteresis (dark under -3.5, light again over -2.5) so
  a sun at the edge does not blink the screen. No time from the network: day. [built; the night itself not run on a board]
- The time comes from NTP: `apis/ntp-pool.md`.

## 5. What stands in when a value is missing

- No radiation source: the panel's RADIATION row and the dial's number show the sun's next rise or set (SUNRISE 07:05) in its
  place. README: "There is no open source of it for the whole world yet." [measured: on the boards]
- No air answer: the air rows are left out, the index stays `WX_NONE`.
- No day-before answer: the last answer's past moved on by the hours since.
- No new forecast: the last one read stays on the screen.
- No forecast yet: the look says why there is none instead of drawing a dial.
- The WEATHER look is drawn standing still: no beam or sweep goes round data the person reads. What moves is the weather
  itself (rain, wind, stars) and the pulsing dot at now.

## 6. On the site

The site's screen of a board and its demo draw WEATHER with the same code. The page asks Open-Meteo itself from the browser, only
while that look is up, every 30 minutes, a minute on after a miss. A page turned on the board turns on the site too (`OWLOS
WXPAGE n` over the console) and the other way round.
