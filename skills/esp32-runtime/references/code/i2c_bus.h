/* copied 2026-11-04 from OWLOS-AIR-QUALITY-UI firmware/main/i2c_bus.h, unchanged */
/* the board's I2C bus: CCS811, ADS1015, SI7021 (and a BMP280 if one answers).
 * bit-banged, not ESP-IDF's driver: after a NACK that driver waits for a free bus with no time
 * limit (i2c_master.c, s_i2c_send_commands), and the chip at 0x40 put it there - the task
 * watchdog restarted the board, again and again. here every wait has a limit, a NACK is just an
 * error, and a slave that holds SDA gets nine clocks to let go */
#pragma once
#include <stdint.h>
#include "board.h"

typedef uint8_t i2c_dev_t;      // a device is its 7 bit address, 0 = none

static inline i2c_dev_t i2c_dev(uint8_t addr) { return addr; }
int  i2c_probe(uint8_t addr);                                          // 1 if it acks its address
int  i2c_tx(i2c_dev_t d, const uint8_t *buf, int n);                   // 1 = every byte acked
int  i2c_rx(i2c_dev_t d, uint8_t *buf, int n);
int  i2c_txrx(i2c_dev_t d, const uint8_t *w, int nw, uint8_t *r, int nr);   // write, repeated start, read
// register read / write, 1 = ok
int  i2c_rd(i2c_dev_t d, uint8_t reg, uint8_t *buf, int n);
int  i2c_wr(i2c_dev_t d, uint8_t reg, const uint8_t *buf, int n);
void i2c_recover(void);                                                // nine clocks and a stop
