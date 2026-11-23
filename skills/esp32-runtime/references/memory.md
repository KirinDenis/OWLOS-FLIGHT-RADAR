# Memory

How sure each fact is: the tags of SKILL.md. The 2.8" CYD, the 3.5" ST7796 and the round C3 have no PSRAM, and the
Air Quality firmware uses none. The S3, C5 and P4 boards have it, see [PSRAM](#psram).

## Per board, what is left

### 2.8" CYD (classic ESP32, 320x240, no PSRAM)

- WiFi Radar, promiscuous, no TLS: 49 KB free, 2 h log, strips of 24 rows [measured].
- Flight Radar 0.6 with every radar option on: 77-79 ms a frame, least free heap 37 KB [measured].
- Flight Radar 0.14, two-hour log: heap at fetch time flat at about 68 KB, no leak, no restart [measured].
- Flight Radar 0.16.1: least heap while a map tile is read 17.9 KB; on the weather pages the heap dips to about
  25 KB during fetches [measured].
- Flight Radar 0.16.2: first free heap 34 264 bytes against 34 448 on 0.16.1 [measured].
- Flight Radar weather look with its new HUD (2026-11-23): 17-18 fps, 53-56 ms a frame, heap 28-46 KB, no crash in
  that run [measured; a short check, not a soak].
- The second core draws halves here (only its 7 KB stack), see [tasks-and-watchdog.md](tasks-and-watchdog.md).

### 3.5" ST7796 (classic ESP32, 480x320, no PSRAM): the edge of memory

The same chip as the 2.8" with twice the pixels. This board is where every memory lesson below was learnt.

- WiFi Radar: 24 KB free, strips of 12 rows, "or the heap runs out" [measured].
- Flight Radar, lowest since power on with TLS and the map: 10.5 KB; then 6.4 KB after 3.8 KB of new statics
  (fixed by sharing) [measured].
- 0.6 with every radar option on: 129-142 ms a frame with 18 planes [measured].
- 0.6, map look: about 51 KB free; a tile read failed with `heap 56884, largest 19456` and kept failing at every
  15 s retry. The window pieces went from 4 KB to 1 KB: 12 of 12 maps, least heap 10 KB [measured]. What broke
  the heap up was not found.
- 0.10: the map look dipped to 3-4 KB free and a TLS connection reused after a broken tile crashed the board
  (see [network.md](network.md)). The floor seen was 2.8 KB [measured].
- 0.13: the map stuck at "4 of 6 tiles" for 32 minutes, least heap 2.4 KB, no "no memory" line. The cause was not
  memory: the inflater took only 8-bit PNGs, and OpenStreetMap sends sea and plain tiles as 1, 2 or 4-bit palette
  PNGs. They failed fast and were kept for ever. Fixed by unpacking small depths [measured].
- 0.14: the square map takes 77 KB; about 43 KB stays free and the tile reader takes about 38 KB. 4 of 151 maps
  came empty with `no memory to read a tile in, heap 43440, largest 34816` and came back after 15 s; least free
  heap 880 bytes. Two-hour log: heap at fetch time flat at about 44 KB, no leak [measured].
- 0.16.1: window pieces of 512 bytes: 154 of 154 maps whole in a 20-minute cycle of looks and ranges, least heap
  while reading 1068 bytes [measured].
- 0.16.1: the weather took about 1.5 KB more of statics, and the square map failed every time (79 of 162 whole).
  The inflater's two rows sized to the tile at IHDR (2 KB less): 148 of 153, failing on fragments (41 KB free,
  largest 31.7 KB). Strips 8 -> 6 rows (7.7 KB back): 153 of 153, least heap while reading 6.3 KB. Cost: MAP
  12 -> 10 fps, CLASSIC 11 -> 9, from more SPI transfers; the per-transfer cost is not understood [measured].
- 0.16.2 weather look with its new HUD (2026-11-23): first 8 fps, 111 ms (disc 30, compass 12, panel 15), heap
  34-50 KB, helper stack 4436 free, no crash in 75 s; after the HUD was balanced 9-12 fps, 80-106 ms a frame, heap
  35-52 KB, no crash in that run [measured; short checks, not a soak].
- 0.16.2 soak: 0 restarts, every map whole when idle; not-whole maps came from tiles broken over weak WiFi
  (-85 dBm, 9 of 113) [measured].
- The 3.5" classic ESP32 bottoms out near 5 KB on the busiest looks. A feature that keeps a TLS connection open
  for good (a live stream: 20-40 KB) does not fit it, and belongs to PSRAM boards only [reasoned from the above].

### Round C3 (ESP32-C3, 240x240, one core, no PSRAM, 2 MB layout)

- Flash first: the app partition is 96% full, 56 288 bytes left on 2026-11-23 [measured from the build]. See
  [project-skeleton.md](project-skeleton.md#flash-budget).
- WiFi Radar: 31 KB free [measured].
- Flight Radar 0.6 with every radar option on: 154-161 ms a frame, least free heap 26 KB [measured].
- Flight Radar 0.14, two-hour log: heap at fetch time flat at about 57 KB, no leak [measured].
- Flight Radar 0.16.2 soak: lowest heap 11.2 KB (5.7 KB on the build before the map-word change), but 7 WiFi
  drops in that run, so not a clean comparison [measured]. Its WiFi on the bench is weak, see
  [network.md](network.md).
- Strips of 16 rows, 30 KB [computed].

### Other classic ESP32 firmware

- Air Quality UI, 480x320, no WiFi, two painters: 98 KB, before a 29 KB table was tried and dropped [measured].
- FusionEdge, an internet radio (Arduino on PlatformIO), ported to the no-PSRAM CYD boards with a strip renderer
  from Flight Radar: with an MP3 stream playing the largest free block was about 50 KB on the 3.5" and 55 KB on the
  2.8"; with its web page serving while streaming about 20 KB free [measured]. Its traps are below.

### PSRAM boards

- 5.79" e-paper, S3R8: two 54 KB pictures and a 2-bit model of the glass in PSRAM [measured].
- NM-CYD-C5: the first run by a tester stopped with "not enough memory for the screen": about 35 KB of internal
  RAM left, 8 MB of PSRAM nearly empty [docs, a tester's report]. The second report, after the fixes below, had
  the screen working and no planes: `esp-aes: Failed to allocate memory` on every TLS read [docs, a tester's
  report]. See [The C5 and the AES](#the-c5-and-the-aes).
- The 7" S3 RGB boards of a contributor: internal RAM about 8 KB left, 3 KB at the least before the fetch, console
  and log stacks went to PSRAM; after, 45 KB free, 31 KB in one piece, 10 KB at the least through three tile
  downloads. 2.4 MB of PSRAM heap free on the 7" [docs, the contributor's FORK-NOTES.md].
- The 10.1" P4 board: about 75 KB of internal RAM free, 48 KB at the least, largest block about 31 KB, and TLS
  handshakes failing. With mbedTLS, lwIP's buffers, ESP-Hosted's stacks and the fetch and console stacks in PSRAM:
  about 133 KB free, 112 KB at the least. Handshakes still fail now and then with 110 KB free, so that is not the
  RAM [docs, FORK-NOTES.md].
- The Tab5 and the 5" boards keep the map's 190 KB picture in PSRAM: their internal RAM has no room for it.

## The largest block and the lowest point

**Largest free block.** Two figures from two moments: about 110 KB "after WiFi" in the early renderer (comment in
`gfx.h`, written with WiFi started and little else) and about 24 KB in Flight Radar with everything running (the
note on the map) [measured]. Plan on the second. Log both:

```c
ESP_LOGE(TAG, "not enough memory for the screen: RAM %u free, %u in one piece, heap %u",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
```

The lowest point is `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)`, and it is the number that counts: free
heap at a quiet moment says nothing about the middle of a handshake. Flight Radar prints it after each fetch
(`at least %u since power on`) and around map making (`at least %u after the tiles came, %u after reading them`).
On a PSRAM build the 10 s log adds the internal RAM alone:

```c
#if CONFIG_SPIRAM
// the heap above counts the PSRAM too: what the WiFi, the second core and the AES under TLS want is this
ESP_LOGI(TAG, "RAM %u free, %u in one piece, %u at the least", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
#endif
```

**What is taken before the heap starts** [computed, `xtensa-esp32-elf-size -A` and `nm -S --size-sort` on the
2026-11-03 ELFs]:

- `.dram0.data`: 17 624 bytes on the 2.8" ESP32, 17 608 on the 3.5" ESP32, 12 060 on the round C3.
- `.dram0.bss`: 81 088, 83 408, 82 408.
- `.iram0.text`: 93 323, 94 163, 91 748.

The biggest statics in Flight Radar then: `s_ui` 13.5 KB, the fetch task's `sky_reader_t` 9.8 KB, `s_sky` 7.8 KB,
the map plan 4 KB, the tile index 3 KB, the Nominatim buffers 2.4 KB. WiFi's own `g_cnxMgr` is 3.9 KB. On the 10.1"
P4 the static side was 105 KB of IRAM code, 46 KB `.data`, 89 KB `.bss` [docs, FORK-NOTES.md].

Task stacks come from the heap at start: fetch 12 KB, ui 8 KB, console 4 KB, plus the helper's 7 KB while it runs.

## Which allocator for what

- A buffer SPI reads by DMA: `heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)` (`lcd_spi.c`).
- The strip the renderer draws on: `heap_caps_malloc(n, MALLOC_CAP_INTERNAL)` (`main.c`): never let it land in
  slow memory.
- A word-only picture, classic ESP32: `heap_caps_malloc(n, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT)` (`iram_first`).
- Big and slow, on a board that has it: `heap_caps_malloc(n, MALLOC_CAP_SPIRAM)` or with `| MALLOC_CAP_8BIT`
  (`iram_first` on PSRAM boards, `epd.c`), with a fallback to internal.
- Big state no interrupt touches, on a board that lets `.bss` go to PSRAM: a static marked `CORE_BIG`.
- Anything else: `malloc`, which is internal on all these builds except where `CONFIG_SPIRAM_USE_MALLOC` is on (the
  7" and 10.1" boards: there plain malloc of 1 KB and more may go to PSRAM, and what must be internal says so).

`MALLOC_CAP_8BIT` in a statistic counts all byte-addressable heaps. With PSRAM on it includes PSRAM [docs], so on
an S3 the "free heap" in the log and the 48 KB test in `helper_set` look fine while internal RAM is short. That is
what happened on the C5: the log said plenty, the strips and the AES had none. Use `MALLOC_CAP_INTERNAL` there.
Flight Radar's 10 s log and its "not enough memory" line do that now [built; the C5 report came first].

## Strips: height is a heap decision

A full 320x240 RGB565 frame is 150 KB. It never existed in the SPI firmwares. The picture is drawn in 0x00RRGGBB
into one strip, dithered to 565 into a DMA buffer, and sent while the next strip is drawn.

`work = W * rows * 4`, `dma = 2 * W * rows * 2` [computed]:

- 2.8", any firmware: 320 wide, 24 rows, 10 strips: 30 720 + 30 720 = 60 KB.
- 3.5", WiFi Radar: 480, 12 rows, 27 strips: 23 040 + 23 040 = 45 KB.
- 3.5", Flight Radar before the weather: 480, 8 rows, 40 strips: 15 360 + 15 360 = 30 KB.
- 3.5", Flight Radar since 0.16.1: 480, 6 rows, 54 strips: 11 520 + 11 520 = 22.5 KB; with the helper's pairs,
  twice that.
- 3.5" with the helper and 8-row strips: 8 + 8 rows, 20 pairs: 30 720 + 30 720 = 60 KB.
- Round C3: 240, 16 rows, 15 strips: 15 360 + 15 360 = 30 KB.
- NM-CYD-C5 and ZLX-ESP32-2: 320, 12 rows: 15 360 + 15 360 = 30 KB. At 24 rows they were 61 440 bytes and left no
  internal RAM for the AES under TLS.
- 5.79" e-paper: 792, 8 rows, 34 strips: 25 344, no DMA: 25 KB.
- The RGB, DSI and moving-layer boards have their own numbers in board.h (12 rows on the 5" and the Tab5, 16 on
  the 4.3", 4 rows of background on the 7", 80 rows in PSRAM on the 10.1").

The e-paper had 24 rows at first: 792 x 24 x 4 is **76 032 bytes in one piece**, and after WiFi no such block
exists on the S3 either, because the strip is internal RAM [measured]. Hence 8 rows: "paper is slow anyway".

Fewer rows cost CPU. Whatever a strip pass repeats multiplies by the strip count, 40 on the 3.5" then:

- trails walked whole in every strip: 62 ms a frame once all were full, 7 ms when moved to once a frame [measured].
- the square map's grid redone per strip: 38 of 110 ms, moved to once a frame [measured].
- more strips mean more SPI transfers: 8 -> 6 rows cost the 3.5"'s maps 2 fps [measured].

So after lowering `DISP_STRIP`, move everything per-frame out of the strip pass. Each primitive clips to the
strip, but a loop over 48 planes with 16 trail points runs 40 times before any clipping helps. Detail in
esp32-ui-drawing.

## WiFi and TLS settings that made room

In `sdkconfig.defaults`:

```text
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y            # TX and RX buffers exist only while used
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y  # config freed after the handshake
CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y      # the CA freed after the handshake
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=5     # default 10, 1.6 KB each: 8 KB back
CONFIG_ESP_WIFI_RX_BA_WIN=5
# CONFIG_ESP_WIFI_IRAM_OPT is not set      # about 36 KB of IRAM comes free
# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set
```

With these the map fits beside TLS on the 3.5" [measured]. The defaults stay: `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN`
16384, `OUT` 4096. The cost of the IRAM lines is "a little slower to receive", not measured, and no fetch got
slower in a way anyone noticed.

**What TLS needs.** There is no measured peak on record. What is on record: the code keeps 48 KB in reserve
before it lets the second core take memory, and the lowest points seen with all of it running were 10 KB and less.
So a handshake fits, with not much to spare. It also runs on the caller's stack: the fetch task has 12 KB.

**When it fails**, in order:

1. mbedTLS cannot allocate, `esp-tls` logs the error (`MBEDTLS_ERR_SSL_ALLOC_FAILED` is -0x7F00 [docs]).
2. `esp_http_client_open` fails. `net_get` returns -1 with `*status` 0.
3. The fetch task sets `SKY_HTTP` and a note, `ADSB.LOL NO ANSWER`, and the screen shows it.
4. `next` moves to the other server for the next round. The sky fades by `SKY_FORGET`, 30 s.
5. Nothing crashes and nothing restarts.

Steps 2 to 5 are the code. A TLS read failing for memory was seen on the C5 (below), and it failed politely there:
no planes, no crash. Test it on a new board with a throwaway build that holds the memory.

Everything else has the same form: `map_alloc` returns 0 and the log says `no memory for %d x %d texels`, the
scope shows the glass alone. `png_decode` returns `PNG_NO_MEMORY`, the log prints heap and largest block, the map
keeps its other tiles and tries again in 15 s. The screen's own allocation is the one fatal case: it logs the sizes
and ends the task, so a board that cannot draw still answers on the console.

### The C5 and the AES

The story of the NM-CYD-C5, which no board here has run [reasoned from the code and two tester reports]:

1. 0.6: plain malloc and TLS in internal RAM, 24-row strips: about 35 KB left, the screen stopped.
2. 0.7: the map's picture to PSRAM (`iram_first`), TLS to PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), the WiFi's
   buffers to PSRAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`), `IGNORE_NOTFOUND` off (the WiFi asks for PSRAM only
   when PSRAM is not optional), `STATIC_TX_BUFFER_NUM=6` (PSRAM-mode WiFi keeps its send buffers static: 16 by
   default is 26 KB).
3. The tester's next report: every TLS read failed with `esp-aes: Failed to allocate memory` after the certificate
   was checked. With TLS in PSRAM, the C5's AES by DMA copies unaligned PSRAM data through a bounce buffer of up to
   1600 bytes, which must be DMA-capable internal RAM (`esp_aes_dma_core.c`) [docs]. The main SRAM was spent:
   103.7 KB of heap after the statics, and 24-row strips took 61 440 bytes of it. Plain malloc still succeeded from
   the C5's 16 KB of LP RAM, which is not DMA-capable, so the logs looked fine.
4. The fix: 12-row strips (30 KB back) and the internal RAM in the log [built, not run on a C5 here]. The fallback,
   if it still fails: `CONFIG_MBEDTLS_HARDWARE_AES=n`, AES from ROM tables in software, no RAM cost. The 7" boards
   met the same error and use that line [docs, FORK-NOTES.md].

Why only some chips meet it: the ESP32 and the C3 have no PSRAM, so TLS is in DMA-capable RAM and needs no bounce,
and the ESP32's AES has no DMA at all; the S3 LCD boards keep TLS internal; the S3 RGB boards bounce too but have
512 KB of SRAM; on the 10.1" P4 the hardware AES worked with its buffers in PSRAM [reasoned, and docs for the P4].

## Big things in small pieces

**Answers.** A busy sky is about 100 planes at about 1 KB each. `sky_read` takes pieces as they come, holds one
plane's object (`SKY_OBJ` 2048) and keeps the nearest `SKY_MAX` 48. An array with a limit, chosen by what can
be shown, and a rule for who is dropped.

**The map.** 4 bits a texel, so the whole 480x320 screen is 75 KB [computed] (77 KB with its plan, as measured on
the 3.5"). It is kept as bands of 8 rows, 1 920 bytes each at that width, `uint32_t *row[320]` pointing into them:

```c
int map_alloc(map_t *m, int w, int h, int disc)
{
    /* ... w must be a multiple of 8: a row is w / 8 words ... */
    const int words = w / 8;
    for (int v0 = 0; v0 < h; v0 += MAP_BAND) {
        int rows = h - v0 < MAP_BAND ? h - v0 : MAP_BAND;
        size_t bytes = (size_t)rows * words * 4;
        uint32_t *p = map_mem ? map_mem(bytes) : NULL;       // IRAM or PSRAM first
        if (!p) p = malloc(bytes);
        if (!p) { map_free(m); return 0; }
        m->band[v0 / MAP_BAND] = p;
        for (int r = 0; r < rows; r++) m->row[v0 + r] = p + r * words;
    }
```

A width that was not a multiple of 8 crashed the 4.2" e-paper: its disc map was 236 wide, rows of `w / 8` words,
and the last 4 texels of each row wrote past the band; RADAR -> MAP then crashed in `map_free`. Now the size is
rounded up to 8 and `map_alloc` refuses anything else [measured].

```c
static void *iram_first(size_t bytes)
{
#if CONFIG_IDF_TARGET_ESP32
    /* the IRAM the code leaves free. it takes 32 bit words only, map.c reads and writes nothing
     * smaller, and TLS and the WiFi cannot use it at all */
    return heap_caps_malloc(bytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT);
#elif CONFIG_SPIRAM
    /* every board with PSRAM: the 5" and the Tab5 have no room for its 190 KB in the RAM, the C5's
     * strips and AES want the RAM. no PSRAM found after all: NULL, and map.c takes malloc's */
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#else
    (void)bytes;
    return NULL;                                       // the C3's RAM is one: malloc does it
#endif
}
```

Every access is a word, `row[v][u >> 3] >> ((u & 7) * 4) & 15`. A byte access to IRAM is an exception [docs]. Row
run-length coding was measured and does not help: the detail is too fine. The `map_mem` hook keeps `core/` free
of IDF, and on the PC it is NULL.

**The inflater.** PNG tiles need deflate with a 32 KB look-back window. The ESP32 has miniz's tinfl in ROM, but it
wants the 32 KB in one piece, and after WiFi the largest block is about 24 KB [measured]. So `core/png_decode.c`
is our own, after Mark Adler's puff: plain, a bit at a time, and the window is many small pieces:

```c
#define WIN_PIECE  512u                      /* 4096 at first, 1024 in 0.6, 512 in 0.16.1 */
#define WIN_PIECES (32768u / WIN_PIECE)
uint8_t *win[WIN_PIECES];                    /* what came out, the last 32 KB */

static void put(st_t *s, int b)
{
    s->win[(s->out / WIN_PIECE) % WIN_PIECES][s->out % WIN_PIECE] = (uint8_t)b;
    s->out++;
    /* ... a finished row goes to the row callback ... */
}
/* a copy from the window, byte by byte: it may overlap what it makes */
uint32_t at = s->out - dist;
put(s, s->win[(at / WIN_PIECE) % WIN_PIECES][at % WIN_PIECE]);
```

It takes about 39 KB while it runs and gives it all back; the largest single piece was 5 KB with 4 KB pieces, about
5.3 KB in a later review. The picture is never whole: two rows and the window. Since 0.16.1 the two rows are
allocated at IHDR as long as the tile's rows (2 KB less than the longest possible). 8-bit palette, grey, RGB,
RGBA, and since 0.14 1, 2 and 4-bit grey and palette (unpacked after the packed row), not interlaced. Each step
down in piece size was a measured fix on the 3.5" (above). The copy in `code/png_decode.c` is the older one: 4 KB
pieces and 8-bit depths only. Take the current file from OWLOS Flight Radar's `core/`, or at least set the piece
size (one define) and add the small depths, or sea tiles fail. The same trick works for any algorithm that wants a big ring: the ring does
not have to be contiguous, only addressable.

**Statics shared by jobs that never overlap:**

```c
/* Nominatim's question and answer, for geocode() and the home search: both run on the fetch task,
 * one at a time. and the same bytes are map.c's working rows while map_service reads a tile, on the
 * same task, never at the same time: from the heap, beside the PNG's 32 KB window, they cost the
 * 2.8" a third of its maps (map_scratch, core/map.h) */
static CORE_BIG union {
    struct { char url[420], body[2048]; } osm;
    uint8_t  map[MAP_SCRATCH];                   /* 3584 */
    uint32_t align;
} s_fetch_buf;
```

- The 3.8 KB of separate Nominatim buffers took the minimum from 10.5 to 6.4 KB [measured]. One pair fixed it.
- The map's word finder needed 3.6 KB a tile. Allocated per tile, 33 of 114 maps on the 2.8" came empty (`no memory
  to read a tile`, heap 44.8 KB, largest 21 KB: the window could not find its pieces). Lent from the union (1.1 KB
  more static): 113 of 113 [measured].
- Same in `net_get`: `static char buf[1024]; // one piece; the fetch task is the only caller`, and the console's
  CFG buffers. A static in a function with one caller costs no stack and no heap churn, and it also says in the
  code that only one task may use it.

**Big struct, small stack:** `static sky_reader_t r; // 10 KB, too much for a task's stack`.

**Tables are a trade.** In Air Quality an angle table took a HUD from 62 to 60 ms and cost 29 KB of the 98 KB left,
so it stays off [measured]. WiFi Radar's polar table for one quadrant is one byte an entry, because two "would
cost 31 KB of ram, we dont have that". Flight Radar halved it again: an eighth of a turn, the other eighth is its
mirror, "and the heap keeps the rest for the map". Two copies of the 48-plane list would be about 17 KB on the
classic ESP32, which is why spreading the kept planes over the screen was not done [computed]. Measure the gain
before paying.

## PSRAM

```text
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y        # S3R8 and N16R8: octal. leave it out on the C5, its PSRAM is quad
CONFIG_SPIRAM_IGNORE_NOTFOUND=y # a board without it still boots (the e-paper S3 file)
CONFIG_SPIRAM_USE_CAPS_ALLOC=y  # plain malloc stays internal
```

Why `USE_CAPS_ALLOC` and not `USE_MALLOC` by default: the same code must behave the same on the boards without
PSRAM, and PSRAM is slower and useless for the SPI DMA buffers here. So it is asked for by name, with a fallback:

```c
int psram = 1;
s_frame = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
s_shown = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!s_frame || !s_shown) {                   // no PSRAM on this board: internal RAM then
    psram = 0;
    if (!s_frame) s_frame = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_8BIT);
    if (!s_shown) s_shown = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_8BIT);
}
```

What changed since these notes were first written (2026-11-04): then the S3 and C5 LCD builds had PSRAM on and
nothing asked for it, TLS stayed internal everywhere, and `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` was known but not
tried. The C5's first run by a tester showed that unused PSRAM does not help a board short of internal RAM, and the
groups below are the answer. What each PSRAM board group does now (Flight Radar):

- E-paper S3: two 54 KB pictures and the 2-bit model of the glass [measured]. `IGNORE_NOTFOUND` on. Whether a
  board with no PSRAM runs this build is [built] only.
- S3 LCD (ES3C28P, CrowPanel Advance 2.8"): PSRAM on; the map's picture goes there through `iram_first`. TLS stays
  internal.
- S3 RGB (the 5", 4.3" and 7" panels): the frame and the map in PSRAM; TLS and the WiFi's buffers too
  (`MBEDTLS_EXTERNAL_MEM_ALLOC`, `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`); code run from PSRAM (`XIP_FROM_PSRAM`), so a
  flash write no longer stops the panel's feed. `IGNORE_NOTFOUND` off.
- C5: as the S3 RGB group for TLS, the WiFi and the map; strips of 12 rows. See the AES story above.
- P4: the frame, the map, mbedTLS, lwIP and ESP-Hosted's stacks; with XIP from PSRAM a flash write leaves the cache
  on, so task stacks in PSRAM are safe there (`SPI_FLASH_CACHE_NO_DISABLE` in `spi_flash_os_func_app.c`) [docs,
  FORK-NOTES.md]. The SDIO buffers stay internal (DMA).
- The 7" and 10.1" boards also move big statics with `CORE_BIG` and allow `.bss` in PSRAM: about 65 KB of internal
  RAM back on the 7" [docs].

Rules that came out of it:

- PSRAM does not make internal RAM bigger. The strips, the DMA buffers, interrupt-touched state and the AES bounce
  buffer stay internal. A board with 8 MB can still run out where it counts.
- On a PSRAM board, log `MALLOC_CAP_INTERNAL`, never only the total.
- Keeping TLS internal (`USE_CAPS_ALLOC` alone) is the simple choice while internal RAM lasts; moving it out
  needs the AES question answered for that chip.

## FusionEdge on boards with no PSRAM: an Arduino radio's traps

FusionEdge is an ESP32-S3 internet radio on Arduino and PlatformIO, written for PSRAM. Its port to the classic
ESP32 CYD boards met these [measured on the 2.8" and 3.5" CYD]:

- Memory cuts under one build switch, `BOARD_NO_PSRAM`, so the S3 builds stay the same code (proved by a masked
  `objdump` diff of the objects): audio ring 10 KB, out buffer 4096 then 2304 (one MP3 frame), header 2 KB, no
  FLAC, Opus or Vorbis, no VU or FFT buffers, `ps_malloc` -> `malloc`.
- AAC does not play without PSRAM: faad2's `NeAACDecOpen` fails.
- A boot loop: an HTTPS AAC station left about 5 KB after TLS, `make_unique<AACDecoder>` threw, the board aborted,
  and its "smart start" played the same station again at boot. Fixed with no AAC decoder on these boards and every
  decoder made with `new (std::nothrow)`: out of memory is a station that does not play, not a crash. HTTPS streams
  still cannot play there; allocating the decoder before the TLS connect is the untried idea.
- The radio's web page hung while streaming: its vendored AsyncWebServer ignored `write()` returning 0 (lwIP
  `ERR_MEM`): headers were dropped and the content counted as sent. The fix keeps the refused piece in a buffer of
  its own and sends it first next time, with no new allocation (a `vector` insert there aborted the board), caps
  chunks at 2 MSS, and serves the page's files one at a time. After: 48 of 48 files in 8 loads on the 2.8", 54 of
  54 on the 3.5" with no stall in its log.
- A mono WAV overflowed the out buffer once it was cut to one MP3 frame: the WAV decoder is capped to it.

The general lessons: on a no-PSRAM board every allocation after TLS can fail, so make it fail into a state, not an
exception; and a network library that assumes writes always succeed breaks first when memory is short.
