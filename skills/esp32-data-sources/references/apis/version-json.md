# The site's version.json: is there a newer firmware

A small file next to OWLOS Flight Radar's flasher page on GitHub Pages, written by the site's CI on every push. Boards ask it
to learn whether the site has a newer version, and where the stats server is.

How sure: asked by every board on WiFi since 0.3; the NEW VERSION note seen on the boards. [measured]

## URL and answer

The shape; the values are an example:

```
GET https://kirindenis.github.io/OWLOS-FLIGHT-RADAR/version.json
{"version":"0.15","beta":"0.16.2","stats":"https://<the stats server>"}
```

- `version`: the stable version, the one the site flashes unless the beta is picked. The versions come from CHANGES.md's `## `
  lines, a beta marked `(beta)`. Boards up to 0.14 read only this field, so it stays the stable one.
- `beta`: the newest beta, read by 0.15 and later. A beta build follows the betas while the site has one above its stable, then
  the stable, which is what a beta found good becomes. A stable build never hears of a beta.
- `stats`: where boards are counted, or empty (`radarstats.md`).

## How the board reads it

- Once WiFi is up, then every 12 hours; no answer, or not one it can read: again in 30 minutes (`update_check` in `main.c`).
- Into a 160 byte buffer, fields picked by name (`json_str`). A version counts only if it is digits and dots, at least one.
- Compared with its own (`ui_version_cmp`). Newer: a small NEW VERSION note on the screen that opens ABOUT, and the console's GET
  says `newest`. Nothing else changes: the board never updates itself.

## Terms

The project's own page on GitHub Pages; GitHub's usage limits apply. One small request every 12 hours a board.
