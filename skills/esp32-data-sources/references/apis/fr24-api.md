# Flightradar24 API

Flightradar24's paid API, with the person's own key. OWLOS Flight Radar's source when SET, DATA picks FR24 and a key is
stored. It gives what the free networks do not: where a flight comes from and goes to.

How sure: built and parsed in the PC preview and the site's twin. The notes do not record a run on a board with a real paid
key, and the 0.16.2 smoothing was not seen on FR24 on a real board. Treat the live path as [built].

## URL shape

A box round home, not a radius:

```
https://fr24api.flightradar24.com/api/live/flight-positions/full?bounds=<N>,<S>,<W>,<E>
Authorization: Bearer <key>
Accept-Version: v1
Accept: application/json
```

```c
static void fr24_url(const fr_cfg_t *c, char *url, int cap)
{
    float lat = vague(c->lat), lon = vague(c->lon);
    float r = ask_km(c), dlat = r / 110.574f, dlon = r / (111.320f * cosf(lat * 3.14159265f / 180.0f));
    snprintf(url, (size_t)cap, "https://fr24api.flightradar24.com/api/live/flight-positions/full?bounds=%.2f,%.2f,%.2f,%.2f",
             (double)(lat + dlat), (double)(lat - dlat), (double)(lon - dlon), (double)(lon + dlon));
}
```

Home goes out rounded to 0.01 degree first. The key never goes in the URL, so the URL may be logged.

## Fields used

The list is `data`, one object a plane (`fr24_field`):

- `hex`: `icao`.
- `timestamp`: an ISO time, when this position was fixed. Read as seconds since 2000 by a small `iso_seconds`; only how far
  apart two positions of one answer are counts. The answer is placed at its newest position, each other one as much older as
  its stamp says (`../flight-data.md` section 5).
- `callsign`: `flight`. `flight` is taken only if no `callsign` came first.
- `reg`: `reg`.
- `type`: `type`, and the kind from its prefix: FR24 gives no category (`../flight-data.md` section 12).
- `orig_iata`, `dest_iata`: `orig[4]`, `dest[4]`. The panel's "ORIG > DEST" comes only from here.
- `lat`, `lon`.
- `alt`: `alt`, `has_alt`, and `ground = alt <= 0`.
- `gspeed`: `gs`, knots.
- `track`, `vspeed` (`vrate`), `squawk`.

## Limits and terms

- Their API terms, credits counted per request. The board asks every 20 s (`FR24_EVERY_MS`), and on 429 waits 3 times as
  long.
- Answers come 20 s apart and a little more, and one that fails puts the next 20 s further off. So at FR24's pace a plane is
  flown on by a guess for up to 60 s and forgotten after 60 s, and the LOST option greys it after 32 s, not 12.
- The watch list's lookups still go to a free network while FR24 brings the planes.

## The key

- Up to 600 bytes in NVS (`secrets_t.key`, "they are long"), sent by the site over USB. The board's DATA row switches to FR24
  only when a key is stored, else it says `NO FR24 KEY: SEND IT FROM THE SITE`.
- Never shown and never sent back. The console's GET and the ABOUT page say only whether one is stored.
- `auth[640]` and `buffer_size_tx = 1024` were sized for FR24's tokens. A longer token would fail. [reasoned, not seen]

## Size and parsing

The same streaming reader as the free networks, told `fr24` so it looks for `data` and calls `fr24_field`. Answer sizes were not
written down.

## Failures met

- With a 20 s guess and answers 20 s apart and more, every plane stood still for a moment before each answer and then jumped.
  A replay at FR24's pace: 0.16.1 jumped up to 2.3 km, 0.16.2 slides at most 17 m a frame. [PC test]
- The site's twin froze FR24 planes 39 to 84 s a run until the relay said `FLIGHTRADAR24` and the twin took FR24's pace.
  [PC test]
- The screen says `FR24 HTTP <code>` or `FR24 NO ANSWER` in the alarm colour.
