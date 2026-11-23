# OpenSky Network: researched, not built

Status: researched 2026-11-03, nothing of it is in the firmware. Numbers re-read from the OpenSky docs on 2026-11-04
(`openskynetwork.github.io/opensky-api/rest.html` and the `openskynetwork/opensky-api` README). [docs] No request was ever
made from a board.

## Getting access

- Log in, Account page, create an API client, download `credentials.json`. The file carries `clientId` and `clientSecret`
  (the field names are from the research of 2026-11-03; the public README only names the file).
- Username and password no longer work: basic authentication ended 2026-03-18. OAuth2 client credentials only.
- Token: POST `https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token`, form fields
  `grant_type=client_credentials`, `client_id`, `client_secret`.
- A token lasts 30 minutes. A 401 means it has lapsed: ask for a new one and try again. Send it as
  `Authorization: Bearer <token>`.

## Credits a day

- Anonymous: 400 credits a day, 10 s time resolution, the newest state only (`time` is ignored).
- Standard user: 4000 a day, 5 s, up to 1 hour back.
- Active feeder (30% monthly uptime or more): 8000 a day, 5 s, up to 1 hour back.
- The docs also list a licensed tier: 14400 credits, refilled hourly.

Cost of one `/states/all`, by the area of the box in square degrees:

- up to 25, or a serial-only query: 1 credit
- 25 to 100: 2
- 100 to 400: 3
- over 400, or the whole world: 4

The sum a design has to do (computed here, not measured): `interval_s = 86400 * cost / credits`.

- 400 credits a day: 216 s at cost 1, 432 s at cost 2, 648 s at cost 3.
- 4000: 21.6 s, 43.2 s, 64.8 s.
- 8000: 10.8 s, 21.6 s, 32.4 s.

So anonymous is no live radar at all, and a standard account runs about as often as FR24's 20 s, if the box stays at cost 1.
The box is `area = (2 * r / 110.574) * (2 * r / (111.320 * cos(lat)))`. At 49 degrees north:

- r 40 km (25 km range x 1.6): 0.8 square degrees, cost 1
- r 160 km (100 km range x 1.6): 12.7, cost 1
- r about 225 km: 25, the edge of cost 1
- r 320 km (200 km range x 1.6): 50.7, cost 2
- r 463 km (the 250 NM cap): 106, cost 3

Read the balance from the answer's `X-Rate-Limit-Remaining`. After 429 wait what `X-Rate-Limit-Retry-After-Seconds` says. A daily
budget wants a slower interval worked out from the balance, not a fixed one.

## The request and the answer

The shape; the values are made up:

```
GET https://opensky-network.org/api/states/all?lamin=..&lomin=..&lamax=..&lomax=..&extended=1
{"time":1762254000,"states":[["4b1805","SWR123  ","Switzerland",t,t,lon,lat,alt,false,v,trk,vr,null,geo,"1000",false,0,6], ..]}
```

Each state is an ARRAY in a fixed order, not an object:

- 0 icao24, a hex string
- 1 callsign, 8 characters, can be null, padded
- 2 origin_country
- 3 time_position, unix seconds, can be null
- 4 last_contact, unix seconds
- 5 longitude, degrees
- 6 latitude, degrees
- 7 baro_altitude, METRES, can be null
- 8 on_ground, true or false
- 9 velocity, METRES A SECOND
- 10 true_track, degrees
- 11 vertical_rate, METRES A SECOND
- 12 sensors, null without a sensor filter
- 13 geo_altitude, metres
- 14 squawk, a string, can be null
- 15 spi
- 16 position_source
- 17 category, an integer, only with `extended=1`

Category numbers, to this firmware's kinds:

- 0, 1 none: JET
- 2 light: LIGHT
- 3 small: BIZJET
- 4, 5 large, high vortex: JET
- 6 heavy: HEAVY
- 7 high performance: FAST
- 8 rotorcraft: HELI
- 9 glider: GLIDER
- 10 lighter than air: BALLOON
- 11, 12 parachutist, ultralight: LIGHT
- 14 unmanned: DRONE
- 16, 17 surface vehicles: GROUND

## What building it would take

Read from the existing code, not tried:

- `net_get` does GET only. The token wants a POST with a form body (`esp_http_client_set_method`, `set_post_field`,
  `Content-Type: application/x-www-form-urlencoded`), on the fetch task, once in 30 minutes.
- `sky_read` starts a plane at a `{` at depth 3. OpenSky's plane is a `[` at depth 3 and ends with `]` at depth 2. The `want`
  name becomes `states`, and a new `opensky_field(p, index, value)` counts commas instead of reading keys.
- Convert units on the way in: `alt_ft = m * 3.28084`, `gs_kn = ms * 1.94384`, `vrate_fpm = ms * 196.85`.
- No registration and no type in the answer: the panel has less to say, and the watch list's `reg` kind cannot match.
- The age: `seen = now - (time - time_position) * 1000` from the answer's own two stamps.
- `"states":null` is what to expect for an empty box (not stated by the docs): today's reader would call that a STRANGE ANSWER
  and change server. Treat a null list as an empty one.
- Size the Bearer buffers from a real token first. `auth[640]` and `buffer_size_tx = 1024` were sized for FR24; a Keycloak JWT
  may be longer.
- `clientId` and `clientSecret` go where the FR24 key goes: NVS, sent by the site over USB, never shown, never in a repository.
  The site can read `credentials.json` in the browser and send the two fields.
- A home rounded to 0.01 degree works here too: `vague()` before the box.
