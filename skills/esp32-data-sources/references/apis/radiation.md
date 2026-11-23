# Radiation: the source behind gamma_nsv (none yet)

OWLOS Flight Radar's weather model has a place for the background gamma radiation at the nearest monitoring station
(`weather_t.gamma_nsv`, nSv/h, and `gamma_km`, how far that station is), a RADIATION row, a number on the dial and a warning. No
source fills it. It is always 0, and the board shows the sun's next rise or set in its place (`../weather.md`).

How sure: the empty field and the stand-in are [measured] on the boards. The candidates below are [docs], read during the
research for 0.16.1. None was ever called from a board or a page.

## What the board would do with it

- Show it in microsieverts an hour, an ordinary background is a tenth or two: `%d.%02d` from nSv/h.
- Colour: amber from 300 nSv/h, red from 1000.
- The RADIATION warning: amber over 0.3 microsievert an hour at a station near home, red over 1. Switchable (SET, DATA,
  RADIATION ALERT; GAMMA on the round glass), on at first.

## Candidates looked at

- Germany's BfS ODL network: open JSON, the layer `odlinfo_odl_1h_latest` (hourly values, the latest). The licence was still to
  be confirmed. One country only.
- Safecast: CC0, but mostly readings from drives, often stale for a fixed point.
- EURDEP: a map of about 5500 stations across Europe, but no open API.
- No open source covers the whole world. The README says so to the user.

## The idea, not built

A server of the project's own would gather the open networks and give a board the nearest station's last reading, with its
distance. Not built; any such server must keep the privacy rules of `radarstats.md` (no home, a rounded point at most). [reasoned]
