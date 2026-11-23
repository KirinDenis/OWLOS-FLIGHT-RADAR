# Open-Meteo air quality

Open-Meteo's air quality API, no key: a model's forecast for an area, not a sensor. OWLOS Flight Radar's WEATHER look shows
the European air quality index now and by the hour, and fine dust now; its AIR warning comes from it (`../weather.md`).

How sure: run on the bench boards since 0.16.1 with the forecast. [measured] Answer sizes were not written down.

## URL shape

```
https://air-quality-api.open-meteo.com/v1/air-quality?latitude=<%.1f>&longitude=<%.1f>
  &current=european_aqi,pm2_5&hourly=european_aqi&timezone=auto&timeformat=unixtime&forecast_hours=24
```

Home rounded to 0.1 degree, as for the forecast. Asked only after a forecast that read, every 30 minutes (`wx_url_air`).

## Fields used

- `current.european_aqi`: the index now, 0 good and up. `current.pm2_5`: fine dust now, kept in tenths of a microgram a cubic
  metre.
- `hourly.time` (the first) and `hourly.european_aqi`, matched to the forecast's hours by time: the two answers may start apart.
- Not read: the air left at `WX_NONE` (255), and the screen leaves the air out.

The index's six steps, as the board names them (`aqi_level`, `aqi_word`): under 20 GOOD, under 40 FAIR, under 60 MODERATE,
under 80 POOR, under 100 VERY POOR, else EXTREME, drawn in the EEA's colours. These match the docs' bands. [docs, 2026-11-23]

## Size and parsing

The same reader as the forecast (`core/wx_parse.c`): held whole in the 4 KB buffer, `strstr` for the objects and fields,
`null` as NAN, an answer that filled the buffer not read.

## Limits and terms

Read 2026-11-23 from `open-meteo.com/en/docs/air-quality-api` and the terms page. [docs]

- The same free-tier limits as the forecast (non-commercial, under 10,000 calls a day for the application), counted together.
- `european_aqi` is the highest of five indices, PM2.5, PM10, NO2, O3 and SO2, each from hourly concentrations.
- Resolution: about 11 km (0.1 degree) over Europe, about 45 km (0.4 degree) for the world. Europe's forecast is renewed every
  24 hours, the world's every 12.
- All users must credit the data provider, CAMS ENSEMBLE, and Open-Meteo. The radar's README credits Open-Meteo and the EEA's
  index, not CAMS by name: a credit still to add. [reasoned]
- Commercial use needs their paid server (a `customer-` prefix on the host).

## What the screen says

- The air with a scale under it, best to worst, the step now lit.
- The AIR warning: amber from 60 (poor), red from 80 (very poor), over the next 12 hours.
- README: "The air is Open-Meteo's model for the home's area, not a sensor." Say the same in a new product.
