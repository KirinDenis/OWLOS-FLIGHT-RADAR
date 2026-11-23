# Settings in NVS that survive updates

Real files: `../files/flight-radar/store.c`, `store.h`. Source repo: OWLOS Flight Radar `firmware/main`.

How sure: 0.2 to 0.3 without erase kept every setting (measured on a real board, whole-flash backup, GET
compared field by field). v3 to v4 kept a home that was on the board (measured). Since then the bench boards
took many versions without erase and kept every setting, compared field by field the same way (measured, the
latest 0.16.2). The downgrades below follow from the code and the NVS source, and from what the CHANGES file tells
beta testers; they were not run as their own test.

## The layout

Namespace `"flight"`, one per project. Four kinds of thing:

- **A blob of plain settings with a version field** for what changes together: `"cfg"` (`fr_cfg_t`, what the
  screen shows and changes), `"light"` (`light_cfg_t`, the LED), `"scope"` (`scope_opt_t`, the radar's
  options, all off when none were kept), `"units"` (`wx_units_t`, four bytes, the weather's units).
- **Strings on their own keys**: `"ssid"`, `"pass"`, `"key"` (the secrets), `"watch"`.
- **The networks after the first**: blob `"nets"` (below).
- **Small values on their own keys**, each "kept by itself": `"cal"` (touch alignment blob), `"screen"` (u8),
  `"ran"` (u32), and one byte each for `"help"`, `"bare"`, `"backlight"`, `"night"`, `"alerts"`, `"names"`.

```c
typedef struct {
    uint16_t version;
    uint8_t  source, server, dist_unit, alt_unit, flip, theme, labels, look, map;
    int16_t  up;
    float    lat, lon;
    char     place[40];
    char     country[32];
    float    range;
    uint8_t  ground;       /* new in version 4 */
} fr_cfg_t;

#define FR_CFG_VERSION 4
```

(shortened from `core/ui.h`; the real one has a comment on each field.)

WHY a blob: one read, one write, one version, and the struct is the schema. WHY not everything in it: see the
downgrade.

## A new setting gets its own key

Since the LED (0.3) every new group of settings went to a key of its own instead of into `fr_cfg_t`: the LED's
blob, the radar options' (0.6), the networks (0.8), then `help`, `bare`, `backlight`, `night`, `alerts`,
`units`, `names`. `fr_cfg_t` has stayed at version 4 since the ground filter in 0.3.

- An older firmware flashed back finds everything it knows as it left it, and the new keys simply wait.
- No migration is needed: a key that is not there reads as the default.
- Reading checks the range and falls back to the default: `v <= WXA_ALL ? v : WXA_ALL` ("never set: all of
  them, as a fresh board has them"), `v < UI_BACKLIGHTS ? v : 0`, each unit against its own count.
- 0x6000 of NVS holds them easily (below).

```c
int store_load_alerts(void)
{
    uint8_t v = WXA_ALL;                               // never set: all of them, as a fresh board has them
    return load_blob(NS, "alerts", &v, sizeof v) && v <= WXA_ALL ? v : WXA_ALL;
}
```

Take this as the default for a new setting. A blob with a version is for a group that changes together.

## Reading

```c
static int load_blob(const char *ns, const char *key, void *p, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t got = len;
    esp_err_t e = nvs_get_blob(h, key, p, &got);
    nvs_close(h);
    return e == ESP_OK && got == len;
}
```

NVS has no "read the first N bytes". What `nvs_get_blob` does with a buffer of `len` and a stored blob of `S`:

- `S == len`: reads it, `got == len`, fine.
- `S < len`: reads `S` bytes and says so in `got`. `load_blob` answers 0, the buffer holds a part.
- `S > len`: **`ESP_ERR_NVS_INVALID_LENGTH`**, nothing read.

So a blob is read only at its exact size, and an old blob is found by asking for its old size.

## Migration: the old size over the defaults

Fields are only ever added at the end. So the old blob IS the first bytes of the new struct, and reading it over a
struct filled with defaults gives every new field its default:

```c
int store_load_cfg(fr_cfg_t *c)
{
    fr_cfg_t t;
    if (load_blob(NS, "cfg", &t, sizeof t) && t.version == FR_CFG_VERSION) {
        *c = t;
        return 1;
    }
    /* version 3 is this one up to `ground`: its 100 bytes are read over the defaults, and a board
     * that ran it keeps all of its settings */
    _Static_assert(offsetof(fr_cfg_t, ground) == 100, "version 3 settings are the first 100 bytes");
    fr_cfg_defaults(&t);
    if (load_blob(NS, "cfg", &t, offsetof(fr_cfg_t, ground)) && t.version == 3) {
        t.version = FR_CFG_VERSION;
        *c = t;
        return 1;
    }
    ...
}
```

- `offsetof`, not a typed-in 100 and not `sizeof` of an old struct: the size of what was stored is where the new
  field starts.
- `_Static_assert` pins it. Someone who moves a field gets a compile error instead of boards that silently lose
  their settings.
- `fr_cfg_defaults(&t)` comes AFTER the failed first read, which may have left part of a blob in `t`.
- The version field is checked after the read. Same size is not same meaning.
- Nothing is written back here. The new version lands in flash at the next save.

**Padding trap.** The stored size was `sizeof` of the old struct, with its tail padding. If the old struct ended
padded, the new field's offset can be smaller than what was stored and the read fails with `INVALID_LENGTH`. The
`_Static_assert` with the old size written as a number catches it: compute the old size once from a build of
the old version. Here 100 and, for the LED, 10 and 11.

**When a layout really changed**, keep the old struct as a typedef and copy field by field. Two cases met:

- `cfg` version 2 had no `look`, and `place` was 20 bytes:

```c
typedef struct {
    uint16_t version;
    uint8_t  source, server, dist_unit, alt_unit, flip, theme, labels;
    int16_t  up;
    float    lat, lon;
    char     place[20];
    float    range;
} cfg_v2_t;
...
    cfg_v2_t o;
    if (!load_blob(NS, "cfg", &o, sizeof o) || o.version != 2) return 0;
    fr_cfg_defaults(c);
    c->source = o.source;
    /* ...each field... */
    memcpy(c->place, o.place, sizeof o.place);
    c->place[sizeof o.place - 1] = 0;
    c->country[0] = 0;                                   // the board asks OSM for it
    return 1;
```

- The LED's version 4 added a seventh event, WEATHER, to the `on[]` array in the middle of the struct. Every
  field after the array moved, so "the old size over the defaults" no longer works. The old layout is a typedef,
  read at both old sizes, then copied over the defaults:

```c
/* the LED's settings as the versions before 4 kept them: six events, no WEATHER. version 2 ended
 * before `red`. read into this, then over the defaults: WEATHER on, as a fresh board has it */
typedef struct { uint8_t version, on[6], level, hold, near, red; } light_v3_t;
_Static_assert(sizeof(light_v3_t) == 11 && offsetof(light_v3_t, red) == 10, "version 3 LED settings are 11 bytes, 2 the first 10");
_Static_assert(LIGHT_WEATHER == 6, "a new LED event goes after the six that versions 2 and 3 kept");

void store_load_light(light_cfg_t *c)
{
    if (load_blob(NS, "light", c, sizeof *c) && c->version == LIGHT_CFG_VERSION) return;
    light_v3_t o;
    memset(&o, 0, sizeof o);
    o.red = LIGHT_RED_4;                               // version 2: the red one on GPIO 4
    const int v3 = load_blob(NS, "light", &o, sizeof o) && o.version == 3;
    const int v2 = !v3 && load_blob(NS, "light", &o, offsetof(light_v3_t, red)) && o.version == 2;
    light_defaults(c);
    if (!v3 && !v2) return;
    memcpy(c->on, o.on, sizeof o.on);
    c->level = o.level;
    c->hold = o.hold;
    c->near = o.near;
    c->red = v3 ? o.red : LIGHT_RED_4;
}
```

  Lesson: an array inside a struct is not "the end". A new element of it moves everything after it.

- The radar options' blob (`scope`) has no older version yet: a new `SCOPE_OPT_VERSION` without a branch for the
  old one would put every option back to off. Bump it with a branch, as above.

## Adding a setting, the checklist

1. Its own key, read with a range check and a default (the section above). Or, for a group that changes
   together, a field at the END of its struct, its default in `*_defaults()`, version + 1, and a new branch in
   the load function: `offsetof(new field)`, `_Static_assert`, old version. Keep the branches for every older
   version: boards skip versions.
2. `on_line()`: `json_num` or `json_str`, with a range check. `say_cfg()`: the field in CFG.
3. The page: a field, sent only when set ("keep as it is"); the console's `set` word and `config` line
   (`site-console.md`); the README's list.
4. Test: flash the old build with settings on it, GET to a file, flash the new one without erase, GET, compare:
   `cfgrig.ps1 -SaveCfg` then `-CompareCfg`.

A setting with its own life (a list, a calibration) gets its own key, and needs none of this.

## Strings apart: why the WiFi survives a downgrade

An older firmware flashed over a newer one (the archive and the beta make this a normal thing):

- An older firmware asks for its smaller `sizeof` and the stored blob is larger: `ESP_ERR_NVS_INVALID_LENGTH`,
  `load_blob` says no, `store_load_cfg` falls through and it starts on defaults. The screen's settings look
  reset.
- The strings are read with `nvs_get_str` by key, and no version or size touches them:

```c
static void load_str(nvs_handle_t h, const char *key, char *out, size_t cap)
{
    size_t len = cap;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) out[0] = 0;
}
```

  So the board still joins the WiFi, the way back to a working screen that matters most. This is the WHY of
  keeping secrets out of the blob: `char ssid[33]` inside `fr_cfg_t` would go with it.
- The newer blob stays in NVS until the old firmware saves. Then the old size is what is stored, and the newer
  firmware migrates from it when it comes back: settings changed after the downgrade win, the rest are as the
  older version knew them.

The page says this in one line: "An older version may not read all the settings a newer one saved: the WiFi
stays, the rest may need step 2 again."

Two downgrades as CHANGES tells them (docs, from the project's own notes to beta testers):

- Back to 0.15 from the 0.16.1 beta: a board left on WEATHER (look 3, which 0.15 does not know) shows MAP there,
  and the LED's settings start from their defaults (the LED's version 4 is a size 0.15 cannot read). Everything
  else stays, and flashed to 0.16.1 again the board has all of it back. The site shows a board before 0.16 its
  three looks, and maps a look 3 to MAP.
- A value an older version does not know stays in the blob: read every enum with a fallback, so the old firmware
  draws something it knows.

## Many WiFi networks, and the downgrade

Since 0.8 the board remembers five networks, the one set last first, and joins whichever it finds. The first
stays in `"ssid"` and `"pass"`, where every version looks for it; the other four are one blob, `"nets"`, the
empty places in it too:

```c
void store_load_nets(const secrets_t *s, nets_t *l)
{
    memset(l, 0, sizeof *l);
    if (!s->ssid[0]) return;
    memcpy(l->n[0].ssid, s->ssid, sizeof l->n[0].ssid);
    memcpy(l->n[0].pass, s->pass, sizeof l->n[0].pass);
    load_blob(NS, "nets", &l->n[1], sizeof l->n - sizeof l->n[0]);
    l->count = 1;
    while (l->count < NETS_KEPT && l->n[l->count].ssid[0]) l->count++;
}
```

A board that goes back to a version before 0.8 keeps its first network. GET says the names only (`nets`), SET
`{"forget":"name"}` takes one out.

## Between firmwares

Same flash, same NVS, other namespaces. A board that ran the WiFi Radar or the Thermo Relay knows its touch
alignment and which screen it has, so the Flight Radar takes those if it has none of its own:

```c
int store_load_cal(touch_cal_t *cal)
{
    return load_blob(NS, "cal", cal, sizeof *cal) || load_blob("radarscan", "cal", cal, sizeof *cal) ||
           load_blob("thermo", "cal", cal, sizeof *cal);
}
```

The person flashed with no to erase and does not press the crosses again. It needs the same `touch_cal_t` in all
projects and the same NVS offset.

The screen is asked the same way, by the KEY in each namespace in turn. The first version asked the WiFi Radar's
namespace only when the Flight Radar's own could not be opened; but the own one exists from the first start (the
run record is saved every minute), so the fallback was never reached (reasoned from the code, fixed there):

```c
static const char *const FROM[] = { NS, "radarscan" };
for (int i = 0; i < 2; i++) {
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(FROM[i], NVS_READONLY, &h) != ESP_OK) continue;
    esp_err_t e = nvs_get_u8(h, "screen", &v);
    nvs_close(h);
    if (e == ESP_OK) return v;
}
```

## Start-up

```c
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
}
ESP_ERROR_CHECK(err);
```

The stock lines. They do erase settings on those two errors: `NEW_VERSION_FOUND` means the NVS format of a newer
IDF was found by an older one, so an old firmware built on a much older IDF could wipe a board. Not met: every
version here is IDF 5.5.

## Saving

- `store_save_ran()` once a minute: how long this run has lasted, so the next start can say how long the run
  before lasted, whatever ended it. It was in RTC memory first, and the CH340's EN reset cleared it. NVS is a
  log, so one small value takes a 32 byte entry, and 6 pages of 126 entries turn over about twice a day: wear is
  no worry against 100 000 erases a page (an estimate from NVS's layout, not measured).
- Changes on the screen are "use it now, save it a little later", not one write per finger move. A drag to zoom
  would write hundreds of times.
- A setting from the console is saved at once, in the handler, before OK.
- 0x6000 of NVS is 6 pages. A 600 byte key, the blobs of about 100 bytes, four networks of 98 bytes and the
  one-byte keys fit with room (reasoned from the sizes).

## Settings in files instead

Settings kept in a filesystem partition go with anything that writes that partition:

- The old OWLOS code kept them in SPIFFS: `pio run -t uploadfs` wiped every setting along with the touch
  calibration.
- FusionEdge keeps its station list and WiFi list on LittleFS, and its flasher's manifest writes the LittleFS
  image: every install from the page puts both lists back to the shipped ones, erase or not. Its own few NVS
  settings (`"daclevel"` in namespace `"lite"`, the screen's level) survive (`platformio-pipeline.md`).

And never in the source: a WiFi password in a config header ends up in the public history. Seen in an older
project: a station password committed in a config header, still in git history after it was taken out. That is
the other reason settings go over the cable.
