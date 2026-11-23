/* copied 2026-11-04 from OWLOS-AIR-QUALITY-UI firmware/main/i2c_bus.c, unchanged */
/*
 * I2C by hand on SDA 21 / SCL 22, ~90 kHz. both lines open drain with the pull-ups on (the board
 * has its own too). SCL is let go and then waited for: the CCS811 holds it low while it thinks
 * (clock stretching), at most STRETCH_US. an interrupt in the middle only makes a clock longer,
 * the master owns the clock, so nothing here needs interrupts off
 */
#include "i2c_bus.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#define HALF_US     5           // half a clock
#define STRETCH_US  25000       // the longest a slave may hold SCL

static int s_up, s_err;

static inline void sda(int v) { gpio_set_level(PIN_I2C_SDA, v); }   // 1 = let go, the pull-up lifts it
static inline void scl(int v) { gpio_set_level(PIN_I2C_SCL, v); }
static inline int  sda_in(void) { return gpio_get_level(PIN_I2C_SDA); }
static inline void half(void) { esp_rom_delay_us(HALF_US); }

// let SCL go and wait till it really is high
static int scl_up(void)
{
    scl(1);
    if (gpio_get_level(PIN_I2C_SCL)) return 1;
    const int64_t t0 = esp_timer_get_time();
    while (!gpio_get_level(PIN_I2C_SCL))
        if (esp_timer_get_time() - t0 > STRETCH_US) { s_err = 1; return 0; }
    return 1;
}

static void start(void)            // also the repeated start: SDA falls while SCL is high
{
    sda(1);
    half();
    scl_up();
    half();
    sda(0);
    half();
    scl(0);
}

static void stop(void)
{
    sda(0);
    half();
    scl_up();
    half();
    sda(1);
    half();
}

static int put(uint8_t b)          // 1 = the slave acked
{
    for (int i = 7; i >= 0; i--) {
        sda((b >> i) & 1);
        half();
        if (!scl_up()) return 0;
        half();
        scl(0);
    }
    sda(1);                        // let go for the ack
    half();
    if (!scl_up()) return 0;
    int ack = !sda_in();
    half();
    scl(0);
    return ack;
}

static uint8_t get(int ack)        // ack = more bytes wanted
{
    uint8_t b = 0;
    sda(1);
    for (int i = 0; i < 8; i++) {
        half();
        if (!scl_up()) return 0;
        b = (uint8_t)(b << 1 | sda_in());
        half();
        scl(0);
    }
    sda(ack ? 0 : 1);
    half();
    scl_up();
    half();
    scl(0);
    sda(1);
    return b;
}

void i2c_recover(void)
{
    sda(1);
    for (int i = 0; i < 9 && !sda_in(); i++) {
        scl(0);
        half();
        scl_up();
        half();
    }
    stop();
}

static void bus_up(void)
{
    if (s_up) return;
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&c);
    sda(1);
    scl(1);
    s_up = 1;
    i2c_recover();                 // a slave cut off mid byte by the last reset may still hold SDA
}

int i2c_txrx(i2c_dev_t d, const uint8_t *w, int nw, uint8_t *r, int nr)
{
    bus_up();
    s_err = 0;
    int ok = d != 0;
    if (ok && (nw || !nr)) {
        start();
        ok = put((uint8_t)(d << 1));
        for (int i = 0; ok && i < nw; i++) ok = put(w[i]);
    }
    if (ok && nr) {
        start();
        ok = put((uint8_t)(d << 1 | 1));
        for (int i = 0; ok && i < nr; i++) r[i] = get(i < nr - 1);
    }
    stop();
    if (s_err || !sda_in()) i2c_recover();                     // somebody still holds a line
    return ok && !s_err;
}

int i2c_probe(uint8_t addr) { return i2c_txrx(addr, NULL, 0, NULL, 0); }
int i2c_tx(i2c_dev_t d, const uint8_t *buf, int n) { return i2c_txrx(d, buf, n, NULL, 0); }
int i2c_rx(i2c_dev_t d, uint8_t *buf, int n) { return i2c_txrx(d, NULL, 0, buf, n); }
int i2c_rd(i2c_dev_t d, uint8_t reg, uint8_t *buf, int n) { return i2c_txrx(d, &reg, 1, buf, n); }

int i2c_wr(i2c_dev_t d, uint8_t reg, const uint8_t *buf, int n)
{
    uint8_t b[8];
    if (n > 7) return 0;
    b[0] = reg;
    for (int i = 0; i < n; i++) b[1 + i] = buf[i];
    return i2c_txrx(d, b, n + 1, NULL, 0);
}
