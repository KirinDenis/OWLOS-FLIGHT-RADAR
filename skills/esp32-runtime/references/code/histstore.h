/* copied 2026-11-04 from OWLOS-THERMO-RELAY firmware/main/histstore.h, unchanged */
#pragma once
#include <stdint.h>

/* the 24 h history in the ESP32's own flash: partition "thermo" (256 KB), a ring of 12 KB
 * slots. each save goes to the next slot, so a sector is erased once per round: with a save
 * every 10 minutes that is a few times a day, flash is rated 100k erases = decades */
int hstore_init(void);
int hstore_load(uint8_t *buf, int len);          // newest good save of exactly len bytes, 1 = found
int hstore_save(const uint8_t *buf, int len);
