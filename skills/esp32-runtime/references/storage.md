# Storage: NVS and raw flash rings

How sure each fact is: the tags of SKILL.md. Whole files: `code/histstore.c` (the slot ring), `code/tiles.c` (the
log ring).

## Which store for what

- A few hundred bytes, changed by a person: NVS. Keys, versions, wear levelling for free.
- Secrets: NVS strings. Same, and never read back to the outside.
- A counter once a minute: NVS. Small enough, see the sum below.
- KBs, rewritten on a timer: a raw partition, a slot ring. NVS would copy it around; a ring is simple and even.
- Many files of any size: a raw partition, a log ring. A filesystem's work for 170 lines.
- A crash's core dump on a board nobody watches: a `coredump` partition (the 7" and 10.1" builds), see
  [crashes.md](crashes.md).

No filesystem anywhere in the ESP-IDF firmwares: nothing here needs names, folders or random updates. (FusionEdge,
an Arduino project, keeps its WiFi and stations in LittleFS. Its flasher's manifest writes the LittleFS image, so a
flash from its site resets them; keeping them is the radio's own web update. One more reason to keep settings out
of any image.)

What goes where in practice:

- Settings, one versioned blob: NVS, 3 s after the last change (holding + is one write, not twenty). A change from
  the site is saved at once.
- WiFi networks, passwords, keys: NVS strings and a blob, when set.
- Touch alignment, which screen profile: NVS, when set.
- How long this run has lasted: NVS u32, once a minute.
- Walk map and networks (WiFi Radar): raw partition, ring of 12 KB slots, every 3 min and 5 s after a turn.
- History (Thermo 12 KB slots, Air Quality 48 KB slots): raw partition, ring, every 10 min.
- Map tiles (Flight Radar): raw partition, log ring of 4 KB sectors, as they download.

## NVS rules

**Start-up**, always this block, and it is the only place that may erase:

```c
esp_err_t err = nvs_flash_init();
if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
}
ESP_ERROR_CHECK(err);
```

**One namespace per firmware**: `"flight"`, `"radarscan"`, `"thermo"`. Open, do it, commit, close. No handle kept.

**Settings are one struct in one blob with a version in its first field.** Reading checks the size and the version:

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

**A new version keeps the old settings.** A field added at the end means the old blob is a prefix of the new
struct. Read the old size over the defaults, and let the compiler guard the layout:

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
    /* older layouts that are not a prefix: their own struct, copied field by field */
    cfg_v2_t o;
    if (!load_blob(NS, "cfg", &o, sizeof o) || o.version != 2) return 0;
    fr_cfg_defaults(c);
    c->source = o.source;
    /* ... */
    return 1;
}
```

The same pattern for every versioned blob since. The LED's settings keep reading versions 3 and 2 over the
defaults, and a `_Static_assert` guards that a new event goes after the ones the old versions kept:

```c
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
    /* ... the old fields copied over ... */
}
```

`0.2 -> 0.3` flashed with no erase kept every setting [measured, on the 3.5", with a full flash backup restored
after]. The user's rule: an update must not cost a person their WiFi and home. Going back, an old firmware may not
read what a newer one saved: the WiFi stays, the rest may need sending again.

**A key of its own for each new thing.** That is why new things often stay out of the settings blob: then
`fr_cfg_t` and a flash back to an older firmware stay untouched. Flight Radar's keys in namespace `flight` on
2026-11-23 [from store.c]:

- `cfg`: the settings blob (`fr_cfg_t`, versioned, prefix reads).
- `ssid`, `pass`, `key`: strings. The first network and the FR24 key.
- `nets`: the other remembered networks, a blob.
- `watch`: the watch list, a string.
- `light`: the LED's settings, a versioned blob (v4 reads v3 and v2).
- `scope`: the radar look's options, a versioned blob (`SCOPE_OPT_VERSION`).
- `cal`: touch alignment. `screen`: which panel profile, u8.
- `help`, `bare`, `backlight`, `night`, `alerts`, `names`: one small value each.
- `units`: the weather's units, a blob.
- `ran`: how long the run lasted, u32, once a minute.

Measured round trip, 2026-11-19: a beta build, then the live stable flashed over it with no erase, then the beta
again: 44 fields the same on both CYDs. On the older stable a look it does not know (look 3, weather) was drawn as
the map, and the LED's newer blob read as defaults; back on the beta all of it returned [measured].

A small value is saved only when it changes. A page that sends every setting each time (the twin sends its whole
JSON) would otherwise write flash on every message.

**Secrets as strings, each in its key**, `ssid`, `pass`, `key`. They are not in the settings blob, so GET can dump
one and not the other, and a password never goes through the path that logs. The site is told only
`"key":true`, and of the networks only their names. The copy on the screen is wiped after use:
`memset(s_ui.wifi_pass, 0, sizeof s_ui.wifi_pass)`.

**Wait for the last change.** Holding + makes twenty changes, flash gets one write:

```c
if (cmd.cfg) { /* ... */ dirty = now ? now : 1; }
if (dirty && now - dirty > 3000u) {           // holding + is one flash write, not twenty
    store_save_cfg(&s_ui.cfg);
    dirty = 0;
}
```

Done in Flight Radar, Thermo Relay and Air Quality. WiFi Radar's few settings change seldom and go at once. A change
from the site's console is saved at once too: it comes once, on purpose.

**Do not save what is not real.** Air Quality: `s_cfg.sim = 0; // the demo never survives a restart: real air
first`. Made-up numbers never reach the history: the real history is saved first, the demo runs on a clean model,
and switching back loads the real one. Thermo Relay keeps its simulated probe on over a power cut, since the
person chose it, but it stops by itself the moment a real probe answers. A value learnt from the network (the
home's country from Nominatim) stays in RAM and is asked again at start.

**Take what a sibling firmware left.** Same chip, same panel, flashed with no erase, NVS survives:

```c
/* a board that ran the WiFi radar or the thermostat already knows its touch: same chip, same
 * panel. so without an alignment of its own it takes theirs */
int store_load_cal(touch_cal_t *cal)
{
    return load_blob(NS, "cal", cal, sizeof *cal) || load_blob("radarscan", "cal", cal, sizeof *cal) ||
           load_blob("thermo", "cal", cal, sizeof *cal);
}
```

The screen profile does the same with `radarscan`'s `screen`.

**A flasher that erases wipes all of it.** Updates use the four separate parts with "no" to erase. A merged bin is
the whole flash and wipes NVS on every update [measured]: esp32-flash-and-settings.

**The run record once a minute is fine in NVS** [computed]. A u32 write is one 32-byte entry, a 4 KB page holds
126, so 1440 writes a day fill about 11 pages. Over the 6 pages of the 24 KB partition that is 2 erases a page
a day, and 100 000 erases last over 100 years. Not at one a second: 86 400 a day is about 2 years.

## The slot ring

For one blob saved on a timer. WiFi Radar's `flashstore.c`, then cut down to `histstore.c` for one buffer. In use
[computed]:

- WiFi Radar: partition `radar` 512 KB, 12 KB slots, 42 slots, saved every 3 min and 5 s after a turn or a new
  walk. One round 2.1 h, each sector erased 11 times a day: 24 years at the 100 000 rating.
- Thermo Relay: `thermo` 256 KB, 12 KB slots, 21 slots, every 10 min. One round 3.5 h, 7 erases a day: 40 years.
- Air Quality: `aqhist` 512 KB, 48 KB slots, 10 slots, every 10 min. One round 1.7 h, 14 erases a day: 19 years.

Flash wear is in the sectors, so what counts is how often the writer comes back to the same one: more slots, later
return. A 12 KB slot is three 4 KB sectors. Size it just above the blob: Air Quality's history is about 44 KB in
48 KB slots.

```c
typedef struct { uint32_t magic, seq, len, crc; } slot_hdr_t;

/* data first, header last: if the power dies in the middle, this slot has no header and the
 * previous save is still the newest good one */
int hstore_save(const uint8_t *buf, int len)
{
    if (!s_part || len > SLOT_BYTES - (int)sizeof(slot_hdr_t)) return 0;
    slot_hdr_t h = { SLOT_MAGIC, s_seq + 1, (uint32_t)len, esp_rom_crc32_le(0, buf, (uint32_t)len) };
    size_t at = (size_t)s_next * SLOT_BYTES;
    int ok = esp_partition_erase_range(s_part, at, SLOT_BYTES) == ESP_OK &&
             esp_partition_write(s_part, at + sizeof h, buf, (size_t)len) == ESP_OK &&
             esp_partition_write(s_part, at, &h, sizeof h) == ESP_OK;
    if (ok) {
        s_seq++;
        s_next = (s_next + 1) % s_slots;
    }
    return ok;
}
```

Why each part:

- **Header last.** NOR flash erases to 0xFF and a slot with no magic is simply not a save. A cut in mid-write leaves
  the previous save as the newest. No journal needed.
- **Sequence number, not a position.** At boot read every header, the highest `seq` wins, the next write goes after it.
- **CRC, and fall back.** `hstore_load` tries the newest, on a bad CRC or wrong length the one before, and so on.
- **Length in the header, version inside the blob.** `store_unpack` checks magic, version and `sizeof` of each struct,
  so "state from another build is refused, not misread". A firmware change that alters the struct starts empty, a
  price paid gladly.
- **A magic per project.** Thermo's comment: `"THRM". not the radar's "SLOT": a board that ran it has those here`.
  The partition sits at the same address and an update without erase leaves the old bytes.
- **After restore, say so.** WiFi Radar: restored networks draw faded and go in about 30 s unless heard again. A walk
  restored after a power cut joins the new one with a 30 s pretend gap. The heading is dropped, the board may
  face anywhere now.

Save from a task that can wait, on a copy, outside the lock. It also takes a while: the log says how long,
`saved to flash: ok, %d networks, %d walk seconds, %d ms`. The 2 h run on the CYD did 42 saves, none failed
[measured].

A malloc of the slot's size is the one thing that can fail at save time, and `fstore_save` then returns 0 and
logs FAILED. The next timer tries again. Air Quality saves straight from the model, which only that task writes,
so it needs no copy of its 44 KB.

Flash writes pause the caches on both cores, so code outside IRAM stops for the erase [docs]. That is why the
strip-done callback is `IRAM_ATTR` (`CONFIG_SPI_MASTER_ISR_IN_IRAM=y`). A 12 KB erase in a frame is a visible
hitch at worst. How big it is has not been measured. On boards that run their code from PSRAM
(`CONFIG_SPIRAM_XIP_FROM_PSRAM`, the S3 RGB and P4 builds) a flash write leaves the cache on, so an RGB panel's feed
does not stop for a tile kept or a setting saved [docs].

## The log ring: tiles

For many files of any size, downloaded once, oldest thrown out first. `code/tiles.c`, Flight Radar, 170 lines then,
about 210 now.

```text
partition "tiles", 0x1F0000 on 4 MB boards, 0xA0000 on the 2 MB layout, 0x600000 on 16 MB
[head 32 B][PNG ...][pad to 4 KB][head][PNG ...][pad] ... wraps
head: magic "OSMT", z, x, y, len, seq, spare[2]
```

- A tile starts on a 4 KB sector boundary, so erasing a sector can only lose whole tiles.
- **The header goes last** (`w_end`): a download cut short leaves no tile.
- Sectors are erased just before the writing reaches them, and the tiles in them are forgotten at that moment.
- `MAX_TILE` 160 KB is the room checked before starting, a bigger answer is dropped. The write point wraps to 0
  when the end is nearer than that.
- At boot `tiles_init` walks the partition header by header: `off += SECTOR` on an empty one, jump over a tile on a
  good one. The newest `seq` says where to go on. The index, 128 entries of 24 bytes, is the only RAM it keeps.
- It writes as it downloads, 1 KB at a time, so the file is never in RAM. `tiles_read` feeds the PNG decoder
  straight from flash.
- No CRC here: `esp_http_client_is_complete_data_received` is checked before the header goes on, and a tile can
  always be fetched again. A damaged one fails in the decoder and the map has a hole, not a crash.
- **A kept tile the decoder refuses is dropped**: its header magic is cleared in flash, so it is fetched again. A
  tile kept for ever that never reads (the 1, 2 and 4-bit PNGs before the decoder learnt them) left a map with
  holes for good, and a retry alone did not help: the same tile came back unreadable [measured].
- OSM's tile policy asks for this: the program names itself, only the tiles it shows, each kept at least a week.
  A range shown once comes back at once, even with no WiFi.

Wear: each sector is erased once per trip round the partition, and that depends on how much new map is viewed.
It stops when the tiles are all there. Not a concern [computed].

The same idea works for logs and series: one record per sector group, header last, a sequence number. The
owlos-revival notes sized one at 24 bytes a row, a month of minutes in 1 MB. A proposal, not built.

## A test sky in NVS

The 7" builds keep one sky in NVS on request (`OWLOS SKYSAVE`, namespace `skyplay`) and show it, still, instead of
the network after restarts too (`OWLOS SKYPLAY 1`, `0` to go back). Two builds can then be compared on the same
scene, repeatable to about 15 rows in 10 s of their measure [docs, FORK-NOTES.md]. Worth copying for any
performance work: the same input every run.

## Keeping more than one thing

Thermo's and Air Quality's histories come back only if the build matches. If a field in a saved struct changes:

- settings: bump the version and keep the prefix read,
- ring blobs: change `STORE_VERSION`, accept that the history starts again, say so in `CHANGES.md`.

Keep them different on purpose: a person's settings must survive, a graph may not.
