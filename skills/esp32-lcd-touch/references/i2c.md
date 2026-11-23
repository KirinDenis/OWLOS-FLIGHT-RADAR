# I2C: the lazy bus, the bit-banged bus, and the guard

The I2C bus that touch chips, IO expanders and sensors share. Sources: OWLOS Flight Radar
`firmware/main/i2c_bus.c`, `i2c_bus.h`; OWLOS Air Quality UI `firmware/main/i2c_bus.c`, `i2c_bus.h`, `sensor_hw.c`.
Sureness words as in [SKILL.md](../SKILL.md).

## The lazy I2C bus, on ESP-IDF's i2c_master

For the C3, the S3, the C5 and a touch chip on the classic ESP32 [measured on the C3: expander and CST816; probing
on the classic ESP32; on the S3 RGB boards that ran]. One bus a board, up on the first call, `.i2c_port = -1`
picks a free port, `glitch_ignore_cnt = 7`, internal pull-ups. Devices at 400 kHz unless board.h says
`I2C_BUS_HZ` (100 kHz on the plain CrowPanel 5.0's GT911). `i2c_bus_free` gives the pins back, only when no device
was added: the 3.5" uses it to hand GPIO 33 back to the XPT2046 after no GT911 answered. `i2c_bus_get` hands the
bus to a library that adds its own devices (chipguyhere's Waveshare 7/7B library, through its Arduino `Wire` shim,
on the same bus as the GT911).

Lazy, because a board asks for it at different moments: the round board for its expander in `disp_init`, the 3.5"
for a GT911 in `tp_init`, the CrowPanel Advance 5.0 for its board chip in `disp_init`.

```c
// the one I2C bus of a board: the round one's IO expander and touch chip, the 3.5" C's touch,
// the ES3C28P's and the CrowPanel Advance's touch
#pragma once
#include "board.h"
#ifdef PIN_I2C_SDA
#include "driver/i2c_master.h"
#ifdef __cplusplus
extern "C" {
#endif

// a device on the bus at 400 kHz, the bus comes up on the first call
i2c_master_dev_handle_t i2c_dev(uint8_t addr);
// 1 if something acks at addr
int i2c_probe(uint8_t addr);
// the bus itself, for a driver that adds its own devices (the Waveshare 7/7B's, lcd_wave7.cpp)
i2c_master_bus_handle_t i2c_bus_get(void);
// take the bus down and give the pins back (only when no device was added)
void i2c_bus_free(void);
#ifdef __cplusplus
}
#endif
#endif
```

```c
#include "i2c_bus.h"
#ifdef PIN_I2C_SDA
#include "esp_err.h"

#ifndef I2C_BUS_HZ
#define I2C_BUS_HZ 400000
#endif

static i2c_master_bus_handle_t s_bus;

static void bus_up(void)
{
    if (s_bus) return;
    i2c_master_bus_config_t cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
}

i2c_master_dev_handle_t i2c_dev(uint8_t addr)
{
    bus_up();
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_BUS_HZ,
    };
    i2c_master_dev_handle_t h = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &h));
    return h;
}

int i2c_probe(uint8_t addr)
{
    bus_up();
    return i2c_master_probe(s_bus, addr, 50) == ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get(void)
{
    bus_up();
    return s_bus;
}

void i2c_bus_free(void)
{
    if (!s_bus) return;
    i2c_del_master_bus(s_bus);
    s_bus = NULL;
}
#endif
```

Timeouts in this code: 20 ms a touch read, 50 ms a probe, 50 ms a board chip write, 100 ms the round board's
expander.

## The classic ESP32 trap: i2c_master spinning after a NACK

[measured] ESP-IDF 5.5's `i2c_master` can spin for ever after a NACK. In `s_i2c_send_commands`, i2c_master.c, an
`I2C_EVENT_NACK` waits for the transaction to finish with `while (i2c_ll_is_bus_busy(hal->dev)) { nop }` and no
limit: the transfer's own timeout does not reach it [docs, read in v5.5]. The chip at 0x40 on the Air Quality board
put it there, the task watchdog restarted the board, the same chip hung it again: a restart loop.

- `i2c_master_probe` goes through the same function, and an empty address is fine on the classic ESP32: the NACK,
  the stop, the bus is free [measured: the 3.5" R version probes 0x5D and 0x14 every boot]. What spins is a NACK
  with something still holding the bus.
- The GT911 path on the classic ESP32 uses the IDF driver and has not run on a C version: keep this in mind if one
  locks up [reasoned].
- The C3's `i2c_master` with the CST816 and the expander has shown no such hang [measured].
- So the Air Quality board bit-bangs its I2C, and guards each sensor in RTC memory.

## The bit-banged bus, Air Quality

For the classic ESP32 with sensors that misbehave [measured: it is what the Air Quality board runs].
~90 kHz (5 us half clock), lines
open drain with the pull-ups on. Why not the IDF driver: after a NACK it waits for a free bus with no limit
(`s_i2c_send_commands`), the chip at 0x40 put it there and the task watchdog restarted the board, again and
again. Here every wait has a limit, a NACK is just an error, and a slave that holds SDA gets nine clocks to
let go. An interrupt in the middle only makes a clock longer, the master owns the clock, so nothing needs
interrupts off. SCL is let go and then waited for: the CCS811 holds it low while it thinks, up to 25 ms.

```c
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
```

```c
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
```

## The guard in RTC memory

Without it a chip that holds the bus means a restart loop: watchdog, boot, same chip, hang, the disease of
the old OWLOS firmware. The sensor being talked to is written into RTC memory first, which
lives through a restart but not a power cut. After a watchdog or panic restart that sensor is left alone
until the power goes off, and the screen says why.

```c
#define GUARD_MAGIC 0x41514755u
static RTC_NOINIT_ATTR uint32_t r_magic, r_in, r_bad;

static void guard(int i) { r_in = (uint32_t)(i + 1); }
static void unguard(void) { r_in = 0; }

static void guard_boot(sens_t *s)
{
    esp_reset_reason_t why = esp_reset_reason();
    int crash = why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_WDT || why == ESP_RST_PANIC;
    if (r_magic != GUARD_MAGIC || why == ESP_RST_POWERON) {
        r_magic = GUARD_MAGIC;
        r_bad = 0;
    } else if (crash && r_in >= 1 && r_in <= SENSORS) {
        r_bad |= 1u << (r_in - 1);
    }
    r_in = 0;
    for (int i = 0; i < SENSORS; i++)
        if (r_bad & (1u << i)) {
            s->state[i] = SS_ERROR;
            snprintf(s->info[i], sizeof s->info[0], "HUNG I2C, OFF TILL POWER");
        }
}
```

Every read is wrapped: `guard(i); r = read(); unguard();`. A sensor with a bit in `r_bad` is not started
again. `RTC_NOINIT_ATTR` needs its own magic: it holds noise after power on.

The same memory counts restarts since the power came on, and brown-outs among them
(`esp_reset_reason() == ESP_RST_BROWNOUT`). RTC memory does not survive everything: on Flight Radar it was
lost by the USB line's EN reset, and its run record moved to NVS, once a minute.

