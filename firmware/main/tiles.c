/* the kept tiles, see tiles.h. the partition is a ring: a tile starts on a 4 KB sector with a
 * 32 byte header, its PNG follows. the header is written last, so a download cut short leaves
 * no tile. the sectors are erased just before the writing reaches them, and the tiles they held
 * are forgotten then. at power on the headers are read back: the newest one says where to go on */
#include "tiles.h"
#include "gfx.h"        // CORE_BIG
#include <stdio.h>
#include <string.h>
#include "net.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "tiles";

#define SECTOR    4096u
#define MAGIC     0x544D534Fu          /* "OSMT" */
#define HEAD      32u
#define MAX_TILE  (160u * 1024u)       /* a tile bigger than this is not kept */
#define MAX_KEPT  128

typedef struct { uint32_t magic; int32_t z, x, y; uint32_t len, seq, spare[2]; } head_t;
typedef struct { int16_t z; int32_t x, y; uint32_t off, len, seq; } kept_t;

static const esp_partition_t *s_part;
static CORE_BIG kept_t s_kept[MAX_KEPT];
static int      s_n;
static uint32_t s_wp, s_seq;           // where the next tile goes, and its number

static uint32_t up4k(uint32_t v) { return (v + SECTOR - 1) & ~(SECTOR - 1); }

static int find(const tile_id_t *t)
{
    for (int i = 0; i < s_n; i++)
        if (s_kept[i].z == t->z && s_kept[i].x == t->x && s_kept[i].y == t->y) return i;
    return -1;
}

static void forget(int i) { s_kept[i] = s_kept[--s_n]; }

static void keep(const head_t *h, uint32_t off)
{
    if (s_n == MAX_KEPT) {                             // full: the oldest goes
        int old = 0;
        for (int i = 1; i < s_n; i++) if (s_kept[i].seq < s_kept[old].seq) old = i;
        forget(old);
    }
    s_kept[s_n++] = (kept_t){ (int16_t)h->z, h->x, h->y, off, h->len, h->seq };
}

int tiles_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "tiles");
    if (!s_part) { ESP_LOGW(TAG, "no tiles partition: no map"); return 0; }
    uint32_t newest = 0, total = 0;
    for (uint32_t off = 0; off + HEAD <= s_part->size; ) {
        head_t h;
        if (esp_partition_read(s_part, off, &h, sizeof h) != ESP_OK) break;
        if (h.magic != MAGIC || !h.len || h.len > MAX_TILE || off + HEAD + h.len > s_part->size) { off += SECTOR; continue; }
        keep(&h, off);
        total += h.len;
        if (h.seq >= newest) { newest = h.seq; s_wp = up4k(off + HEAD + h.len); }
        off = up4k(off + HEAD + h.len);
    }
    s_seq = newest + 1;
    if (s_wp >= s_part->size) s_wp = 0;
    ESP_LOGI(TAG, "%d tiles kept, %u KB of %u KB", s_n, (unsigned)(total / 1024), (unsigned)(s_part->size / 1024));
    return 1;
}

int tiles_have(const tile_id_t *t) { return find(t) >= 0; }

void tiles_drop(const tile_id_t *t)
{
    int i = find(t);
    if (i < 0) return;
    /* its header's magic cleared in the flash (a write turns bits from 1 to 0 without an erase): the
     * scan at power on passes it by, so it is not kept again after a restart either */
    const uint32_t zero = 0;
    esp_partition_write(s_part, s_kept[i].off, &zero, sizeof zero);
    forget(i);
}

int tiles_open(const tile_id_t *t, tile_src_t *s)
{
    int i = find(t);
    if (i < 0) return 0;
    *s = (tile_src_t){ s_kept[i].off + HEAD, s_kept[i].len, 0 };
    return 1;
}

int tiles_read(void *src, uint8_t *buf, int cap)
{
    tile_src_t *s = src;
    uint32_t n = s->len - s->at;
    if (n > (uint32_t)cap) n = (uint32_t)cap;
    if (!n || esp_partition_read(s_part, s->off + s->at, buf, n) != ESP_OK) return 0;
    s->at += n;
    return (int)n;
}

/* --- writing one as it downloads --- */

typedef struct { uint32_t start, at, erased; int bad, big; } writer_t;   // big: past MAX_TILE

/* tiles too big to keep, the last few: not asked for again. one was downloaded whole every 15 s, said "came
 * broken" and held back every tile after it in the plan (the 2026-11-27 review) */
#define BIG_KEPT 8
static tile_id_t s_big[BIG_KEPT];
static int       s_nbig;

static int is_big(const tile_id_t *t)
{
    for (int i = 0; i < s_nbig && i < BIG_KEPT; i++)
        if (s_big[i].z == t->z && s_big[i].x == t->x && s_big[i].y == t->y) return 1;
    return 0;
}

// the room for a tile: from the write point, or from the start when the end is too near
static void w_begin(writer_t *w)
{
    if (s_wp + HEAD + MAX_TILE > s_part->size) s_wp = 0;
    *w = (writer_t){ s_wp, s_wp + HEAD, s_wp, 0, 0 };
}

static int w_put(writer_t *w, const void *data, uint32_t n)
{
    if (w->bad) return 0;
    if (w->at + n > w->start + HEAD + MAX_TILE) { w->bad = w->big = 1; return 0; }
    while (w->erased < w->at + n) {                    // the next sector, and the tiles in it go
        for (int i = s_n - 1; i >= 0; i--)
            if (s_kept[i].off < w->erased + SECTOR && s_kept[i].off + HEAD + s_kept[i].len > w->erased) forget(i);
        if (esp_partition_erase_range(s_part, w->erased, SECTOR) != ESP_OK) { w->bad = 1; return 0; }
        w->erased += SECTOR;
    }
    if (esp_partition_write(s_part, w->at, data, n) != ESP_OK) { w->bad = 1; return 0; }
    w->at += n;
    return 1;
}

static int w_end(writer_t *w, const tile_id_t *t)
{
    uint32_t len = w->at - w->start - HEAD;
    if (w->bad || len < 8) return 0;
    head_t h = { MAGIC, t->z, t->x, t->y, len, s_seq++, { 0, 0 } };
    if (esp_partition_write(s_part, w->start, &h, sizeof h) != ESP_OK) return 0;
    keep(&h, w->start);
    s_wp = up4k(w->at);
    if (s_wp >= s_part->size) s_wp = 0;
    return 1;
}

int tiles_fetch(const tile_id_t *t, int n)
{
    if (!s_part || !net_up()) return 0;
    static char url[96], buf[1024];
    esp_http_client_handle_t h = NULL;
    int got = 0, fresh = 0, stop = 0;
    for (int k = 0; k < n; k++) {
        if (tiles_have(&t[k])) { got++; continue; }
        if (stop || is_big(&t[k]) || !map_url(&t[k], url, sizeof url)) continue;   // after a stop the rest are counted, not asked for
        int status = 0, whole = 0, big = 0;
        for (int attempt = 0; attempt < 2; attempt++) {    // the server may have closed the kept connection
            if (h && esp_http_client_set_url(h, url) != ESP_OK) {
                /* the new address not taken (no memory for it): the client would ask for the last tile
                 * again, and that one would be kept under this tile's name for good. a new client */
                esp_http_client_close(h);
                esp_http_client_cleanup(h);
                h = NULL;
            }
            if (!h) {
                esp_http_client_config_t c = {
                    .url = url,
                    .crt_bundle_attach = esp_crt_bundle_attach,
                    .timeout_ms = 9000,
                    .user_agent = NET_USER_AGENT,
                    .buffer_size = 2048,
                    .buffer_size_tx = 1024,
                    .keep_alive_enable = true,
                };
                h = esp_http_client_init(&c);
                if (!h) return got;
                esp_http_client_set_header(h, "Accept", "image/png");
            }
            whole = 0;
            status = 0;
            writer_t w;
            if (esp_http_client_open(h, 0) == ESP_OK && esp_http_client_fetch_headers(h) >= 0) {
                status = esp_http_client_get_status_code(h);
                if (status == 200) {
                    w_begin(&w);
                    int r;
                    while (!w.bad && (r = esp_http_client_read(h, buf, sizeof buf)) > 0) w_put(&w, buf, (uint32_t)r);
                    whole = !w.bad && esp_http_client_is_complete_data_received(h) && w_end(&w, &t[k]);
                    big = w.big;
                    if (whole) { got++; fresh++; }
                    else if (big) {
                        s_big[s_nbig++ % BIG_KEPT] = t[k];
                        ESP_LOGW(TAG, "%s is over %u KB: left out", url, MAX_TILE / 1024u);
                    } else {
                        ESP_LOGW(TAG, "%s came broken", url);
                    }
                } else {
                    esp_http_client_flush_response(h, NULL);
                    ESP_LOGW(TAG, "%s: HTTP %d", url, status);
                }
            }
            /* anything but a whole tile: a new connection for the next one. a tile cut short (the
             * memory short for TLS) leaves TLS without its buffers, and the next request written into
             * it crashed the 3.5" (LoadProhibited, a memcpy to NULL) */
            if (!whole) {
                esp_http_client_close(h);
                esp_http_client_cleanup(h);
                h = NULL;
            }
            if (status) break;                              // no answer at all: once more, on the new one
        }
        if (!status || (status == 200 && !whole && !big) || status == 403 || status == 429) {
            /* no answer twice, a tile cut short, or the server says no: the rest wait for the next round.
             * asked one by one, a server that said nothing cost 18 s a tile and held the planes back for
             * minutes, and on the 3.5" each tile cut short (TLS short of memory) cost two handshakes more */
            ESP_LOGW(TAG, "a tile did not come (HTTP %d): the others wait for the next round", status);
            stop = 1;
        }
    }
    if (h) {
        esp_http_client_close(h);
        esp_http_client_cleanup(h);
    }
    if (fresh) ESP_LOGI(TAG, "%d new tiles, %d kept in all", fresh, s_n);
    return got;
}
