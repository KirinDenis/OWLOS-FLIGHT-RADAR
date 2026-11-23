# OpenWeatherMap (in FusionEdge)

FusionEdge's weather line on its clock screen comes from OpenWeatherMap's current weather API. This is FusionEdge's code (from
yoRadio), read here, not written here. The project's own radar uses Open-Meteo instead (`open-meteo-forecast.md`).

How sure: read from the code (`src/core/timekeeper.cpp`). [docs] Not checked against the live service here.

## URL shape

Plain HTTP on port 80, a raw request over an `AsyncClient`, no TLS:

```
GET /data/2.5/weather?lat=<lat>&lon=<lon>&units=<units>&lang=<lang>&appid=<key> HTTP/1.1
Host: api.openweathermap.org
Connection: close
```

- `appid`: the person's own key, set in the web UI. No key, or weather switched off: nothing is asked.
- `units` and `lang` come from the build's language file: metric by default (C, hPa, km/h), imperial with `IMPERIALUNIT` and
  English.
- Every `weatherSyncInterval` minutes, 30 by default, on a sync task of 4 KB stack, core 0, priority 3.

## Fields used

Found in the body with `strstr`, no JSON library: `description`, `icon`, `temp`, `pressure`, `humidity`, `feels_like`, `speed`,
`deg`. Written into one line of up to 254 characters.

## Notes for a new design

- The key goes over plain HTTP in the query string: anyone on the path can read it. A new design should use HTTPS where the memory
  allows, or a key-less source. [reasoned]
- OpenWeatherMap's terms and limits were not read here.
