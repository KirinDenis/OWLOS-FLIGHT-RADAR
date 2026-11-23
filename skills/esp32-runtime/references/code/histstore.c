/* copied 2026-11-04 from OWLOS-THERMO-RELAY firmware/main/histstore.c, unchanged */
// the radar's flash ring (flashstore.c), for one blob
#include "histstore.h"
#include <stdlib.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"

static const char *TAG = "hstore";

#define SLOT_BYTES (12 * 1024)
#define SLOT_MAGIC 0x4D524854u            // "THRM". not the radar's "SLOT": a board that ran it has those here

typedef struct { uint32_t magic, seq, len, crc; } slot_hdr_t;

static const esp_partition_t *s_part;
static int      s_slots, s_next;
static uint32_t s_seq;

int hstore_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "thermo");
    if (!s_part) { ESP_LOGW(TAG, "no \"thermo\" partition: the history is not kept"); return 0; }
    s_slots = (int)(s_part->size / SLOT_BYTES);
    int newest = -1;
    for (int i = 0; i < s_slots; i++) {
        slot_hdr_t h;
        if (esp_partition_read(s_part, (size_t)i * SLOT_BYTES, &h, sizeof h) != ESP_OK) continue;
        if (h.magic != SLOT_MAGIC || h.len > SLOT_BYTES - sizeof h) continue;
        if (newest < 0 || h.seq > s_seq) { s_seq = h.seq; newest = i; }
    }
    s_next = newest < 0 ? 0 : (newest + 1) % s_slots;
    ESP_LOGI(TAG, "%d slots of %d KB, newest save #%u", s_slots, SLOT_BYTES / 1024, (unsigned)s_seq);
    return 1;
}

// the newest slot with a good CRC; a broken one makes it try the one before
int hstore_load(uint8_t *buf, int len)
{
    if (!s_part) return 0;
    uint32_t below = UINT32_MAX;
    for (int tries = 0; tries < s_slots; tries++) {
        int best = -1;
        slot_hdr_t bh = { 0 };
        for (int i = 0; i < s_slots; i++) {
            slot_hdr_t h;
            if (esp_partition_read(s_part, (size_t)i * SLOT_BYTES, &h, sizeof h) != ESP_OK) continue;
            if (h.magic != SLOT_MAGIC || h.len > SLOT_BYTES - sizeof h || h.seq >= below) continue;
            if (best < 0 || h.seq > bh.seq) { best = i; bh = h; }
        }
        if (best < 0) return 0;
        below = bh.seq;
        if ((int)bh.len != len) continue;
        if (esp_partition_read(s_part, (size_t)best * SLOT_BYTES + sizeof bh, buf, bh.len) != ESP_OK) continue;
        if (esp_rom_crc32_le(0, buf, bh.len) != bh.crc) { ESP_LOGW(TAG, "save #%u is damaged", (unsigned)bh.seq); continue; }
        ESP_LOGI(TAG, "loaded save #%u", (unsigned)bh.seq);
        return 1;
    }
    return 0;
}

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
