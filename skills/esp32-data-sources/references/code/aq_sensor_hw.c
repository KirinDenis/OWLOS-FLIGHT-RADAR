/*
 * the sensors of the OWLOS Air Quality board, read once a second by the sensor task.
 *   CCS811 (0x5A)  eCO2 and TVOC. it estimates eCO2 from the VOCs it smells, there is no CO2
 *                  cell in it. a result every second (drive mode 1), told the temperature and
 *                  humidity from the DHT22 once a minute. the first 20 minutes it warms up
 *   BMP280 (0x76)  pressure (and its own temperature, which the board heats)
 *   ADS1115 (0x48) volts of the MQ135, the MQ7 and the photoresistor, +-6.144 V range like the
 *                  old firmware (the MQ modules may give up to 5 V)
 *   DHT22 (IO32)   temperature and humidity, every 3 s (it wants 2 s between reads)
 * every sensor has a state the screen shows: absent, starting, ok, error. one that stops
 * answering is asked again every 10 s, so a loose wire that comes back comes back on screen too
 */
#include "sensor_hw.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "dht22.h"
#include "i2c_bus.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensors";

#define RETRY_MS   10000u       // a missing or failed sensor is asked again this often
#define FAILS_MAX  3            // reads in a row that fail before a sensor counts as gone
#define CCS_WARM_S 1200         // the CCS811's first 20 minutes are not to be trusted

static i2c_dev_t s_ccs, s_bmp, s_ads;
static uint32_t s_retry[SENSORS], s_since[SENSORS];
static int      s_fails[SENSORS];
static uint32_t s_dht_last, s_env_last;
static float    s_dht_t, s_dht_rh;
static int      s_dht_have;

/* ---------- CCS811 ---------- */

static int ccs_start(sens_t *s)
{
    if (!s_ccs) s_ccs = i2c_dev(ADDR_CCS811);
    i2c_wr(s_ccs, 0xFF, (const uint8_t[]){ 0x11, 0xE5, 0x72, 0x8A }, 4);   // software reset
    vTaskDelay(pdMS_TO_TICKS(100));                                // Adafruit's pauses, the old firmware ran on them
    uint8_t id = 0, hw = 0, st = 0, fw[2] = { 0 };
    if (!i2c_rd(s_ccs, 0x20, &id, 1) || id != 0x81) {
        snprintf(s->info[SEN_CCS811], sizeof s->info[0], "ID %02X, NOT A CCS811", id);
        return 0;
    }
    i2c_rd(s_ccs, 0x21, &hw, 1);
    i2c_rd(s_ccs, 0x24, fw, 2);
    if (!i2c_rd(s_ccs, 0x00, &st, 1) || !(st & 0x10)) {          // APP_VALID
        snprintf(s->info[SEN_CCS811], sizeof s->info[0], "NO APP FIRMWARE");
        return 0;
    }
    const uint8_t start = 0xF4;                                    // APP_START: the address alone
    i2c_tx(s_ccs, &start, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!i2c_rd(s_ccs, 0x00, &st, 1) || !(st & 0x80)) {          // FW_MODE: the app runs
        snprintf(s->info[SEN_CCS811], sizeof s->info[0], "APP DID NOT START");
        return 0;
    }
    i2c_wr(s_ccs, 0x01, (const uint8_t[]){ 0x10 }, 1);           // MEAS_MODE: a result every second
    snprintf(s->info[SEN_CCS811], sizeof s->info[0], "HW %02X, APP %d.%d.%d", hw, fw[0] >> 4, fw[0] & 15, fw[1]);
    return 1;
}

// 1 new result, 0 nothing new yet, -1 the bus failed, -2 the sensor reports an error
static int ccs_read(sens_t *s)
{
    uint8_t st = 0, b[8];
    /* STATUS on its own first, then the result: the status byte inside ALG_RESULT_DATA never
     * showed DATA_READY on this board (seen in the log: STATUS 90 every time), the STATUS
     * register is what Adafruit's library asks, and the old firmware ran on that */
    if (!i2c_rd(s_ccs, 0x00, &st, 1)) return -1;
    if (st & 0x01) {
        uint8_t e = 0;
        i2c_rd(s_ccs, 0xE0, &e, 1);                                // reading ERROR_ID clears it
        s->ccs_error = e;
        return -2;
    }
    if (!(st & 0x08)) return 0;                                    // DATA_READY
    if (!i2c_rd(s_ccs, 0x02, b, 8)) return -1;                   // ALG_RESULT_DATA
    s->eco2 = (float)((b[0] << 8) | b[1]);
    s->tvoc = (float)((b[2] << 8) | b[3]);
    // RAW_DATA: 6 bits of current (uA), 10 bits of voltage over its sensing layer (1.65 V full)
    s->ccs_ua = (float)(b[6] >> 2);
    s->ccs_v = (float)(((b[6] & 3) << 8) | b[7]) * (1.65f / 1023.0f);
    return 1;
}

// ENV_DATA: humidity and temperature in 1/512, temperature counted from -25 C
static void ccs_env(float t, float rh)
{
    uint16_t h = (uint16_t)(rh * 512.0f + 0.5f), c = (uint16_t)((t + 25.0f) * 512.0f + 0.5f);
    i2c_wr(s_ccs, 0x05, (const uint8_t[]){ (uint8_t)(h >> 8), (uint8_t)h, (uint8_t)(c >> 8), (uint8_t)c }, 4);
}

/* ---------- BMP280 ---------- */

static struct { uint16_t T1; int16_t T2, T3; uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9; } C;
static uint8_t s_bmp_addr = ADDR_BMP280;                           // 0x76 or 0x77, start() finds which

static int bmp_start(sens_t *s)
{
    if (!s_bmp) s_bmp = i2c_dev(s_bmp_addr);
    uint8_t id = 0, k[24];
    if (!i2c_rd(s_bmp, 0xD0, &id, 1) || (id != 0x58 && id != 0x60)) {
        snprintf(s->info[SEN_BMP280], sizeof s->info[0], "ID %02X, NOT A BMP280", id);
        return 0;
    }
    i2c_wr(s_bmp, 0xE0, (const uint8_t[]){ 0xB6 }, 1);           // reset
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!i2c_rd(s_bmp, 0x88, k, 24)) return 0;                    // the factory calibration
#define U16(i) ((uint16_t)(k[i] | k[(i) + 1] << 8))
#define S16(i) ((int16_t)U16(i))
    C.T1 = U16(0); C.T2 = S16(2); C.T3 = S16(4);
    C.P1 = U16(6); C.P2 = S16(8); C.P3 = S16(10); C.P4 = S16(12); C.P5 = S16(14);
    C.P6 = S16(16); C.P7 = S16(18); C.P8 = S16(20); C.P9 = S16(22);
#undef U16
#undef S16
    i2c_wr(s_bmp, 0xF5, (const uint8_t[]){ 0x90 }, 1);           // standby 500 ms, IIR filter x16
    i2c_wr(s_bmp, 0xF4, (const uint8_t[]){ 0x57 }, 1);           // T x2, P x16, normal mode
    snprintf(s->info[SEN_BMP280], sizeof s->info[0], id == 0x58 ? "BMP280" : "BME280, HUMIDITY UNUSED");
    return 1;
}

// the datasheet's integer compensation. 1 new result, 0 not measured yet, -1 bus
static int bmp_read(sens_t *s)
{
    uint8_t b[6];
    if (!i2c_rd(s_bmp, 0xF7, b, 6)) return -1;
    int32_t ap = (int32_t)b[0] << 12 | (int32_t)b[1] << 4 | b[2] >> 4;
    int32_t at = (int32_t)b[3] << 12 | (int32_t)b[4] << 4 | b[5] >> 4;
    if (at == 0x80000 || ap == 0x80000) return 0;
    int32_t v1 = ((((at >> 3) - ((int32_t)C.T1 << 1))) * (int32_t)C.T2) >> 11;
    int32_t v2 = (((((at >> 4) - (int32_t)C.T1) * ((at >> 4) - (int32_t)C.T1)) >> 12) * (int32_t)C.T3) >> 14;
    int32_t fine = v1 + v2;
    s->bmp_t = (float)((fine * 5 + 128) >> 8) * 0.01f;
    int64_t p1 = (int64_t)fine - 128000;
    int64_t p2 = p1 * p1 * (int64_t)C.P6;
    p2 += (p1 * (int64_t)C.P5) << 17;
    p2 += (int64_t)C.P4 << 35;
    p1 = ((p1 * p1 * (int64_t)C.P3) >> 8) + ((p1 * (int64_t)C.P2) << 12);
    p1 = ((((int64_t)1) << 47) + p1) * (int64_t)C.P1 >> 33;
    if (p1 == 0) return 0;
    int64_t p = 1048576 - ap;
    p = (((p << 31) - p2) * 3125) / p1;
    p1 = ((int64_t)C.P9 * (p >> 13) * (p >> 13)) >> 25;
    p2 = ((int64_t)C.P8 * p) >> 19;
    p = ((p + p1 + p2) >> 8) + ((int64_t)C.P7 << 4);
    s->pres = (float)p / 256.0f / 100.0f;                          // Q24.8 Pa -> hPa
    return 1;
}

/* ---------- ADS1115 ---------- */

static int s_ads_lsb_seen, s_ads_reads;

static int ads_start(sens_t *s)
{
    if (!s_ads) s_ads = i2c_dev(ADDR_ADS1115);
    uint8_t c[2];
    if (!i2c_rd(s_ads, 0x01, c, 2)) return 0;
    snprintf(s->info[SEN_ADS1115], sizeof s->info[0], "ADS1X15");
    s_ads_lsb_seen = s_ads_reads = 0;
    return 1;
}

static int ads_channel(int ch, float *v)
{
    // OS start, MUX AINch vs GND, PGA +-6.144 V, single shot, 128 SPS, comparator off
    uint16_t cfg = (uint16_t)(0x8000 | (4 + ch) << 12 | 0x0000 | 0x0100 | 0x0080 | 0x0003);
    if (!i2c_wr(s_ads, 0x01, (const uint8_t[]){ (uint8_t)(cfg >> 8), (uint8_t)cfg }, 2)) return 0;
    uint8_t b[2] = { 0 };
    for (int i = 0; i < 6; i++) {                                  // ~8 ms at 128 SPS
        vTaskDelay(pdMS_TO_TICKS(4));
        if (!i2c_rd(s_ads, 0x01, b, 2)) return 0;
        if (b[0] & 0x80) break;                                    // OS = 1: conversion done
    }
    if (!(b[0] & 0x80) || !i2c_rd(s_ads, 0x00, b, 2)) return 0;
    int16_t raw = (int16_t)(b[0] << 8 | b[1]);
    if (raw & 0x000F) s_ads_lsb_seen = 1;
    *v = (float)raw * (6.144f / 32768.0f);
    return 1;
}

static int ads_read(sens_t *s)
{
    for (int ch = 0; ch < 4; ch++)
        if (!ads_channel(ch, &s->ads_v[ch])) return -1;
    s->mq135 = s->ads_v[ADS_CH_MQ135];
    s->mq7 = s->ads_v[ADS_CH_MQ7];
    s->light = s->ads_v[ADS_CH_LIGHT];
    if (++s_ads_reads == 20)                                       // the 12 bit ADS1015 never sets the low 4 bits
        snprintf(s->info[SEN_ADS1115], sizeof s->info[0], s_ads_lsb_seen ? "ADS1115, 16 BIT" : "ADS1015, 12 BIT");
    return 1;
}

/* ---------- HDC1080 ---------- */

/* TI HDC1080 at 0x40: manufacturer ID 0x5449 in register 0xFE, device ID 0x1050 in 0xFF (read
 * off these boards; the README said SI7021). after power on it measures temperature and
 * humidity together, 14 bit each, when its pointer is written to 0x00. the result can be read
 * ~13 ms later with a plain read, before that it NACKs */
static i2c_dev_t s_hdc;
static int s_hdc_have;

static int hdc_start(sens_t *s)
{
    if (!s_hdc) s_hdc = i2c_dev(ADDR_HDC1080);
    uint8_t m[2] = { 0 }, d[2] = { 0 };
    if (!i2c_rd(s_hdc, 0xFE, m, 2) || !i2c_rd(s_hdc, 0xFF, d, 2)) return 0;
    if (m[0] != 0x54 || m[1] != 0x49 || d[0] != 0x10 || d[1] != 0x50) {
        snprintf(s->info[SEN_HDC1080], sizeof s->info[0], "ID %02X%02X/%02X%02X, NOT HDC1080", m[0], m[1], d[0], d[1]);
        return 0;
    }
    // config: heater off, temperature and humidity in one go, 14 bit each (the power on default)
    if (!i2c_wr(s_hdc, 0x02, (const uint8_t[]){ 0x10, 0x00 }, 2)) return 0;
    snprintf(s->info[SEN_HDC1080], sizeof s->info[0], "HDC1080");
    s_hdc_have = 0;
    return 1;
}

// 1 ok, -1 failed
static int hdc_read(sens_t *s)
{
    const uint8_t p = 0x00;
    if (!i2c_tx(s_hdc, &p, 1)) return -1;                          // start: temperature, then humidity
    vTaskDelay(pdMS_TO_TICKS(20));                                 // 6.35 + 6.5 ms at 14 bit
    uint8_t b[4];
    if (!i2c_rx(s_hdc, b, 4)) return -1;
    s->hdc_t = (float)((b[0] << 8) | b[1]) * (165.0f / 65536.0f) - 40.0f;
    s->hdc_rh = (float)((b[2] << 8) | b[3]) * (100.0f / 65536.0f);
    s_hdc_have = 1;
    uint8_t c[2];
    if (i2c_rd(s_hdc, 0x02, c, 2)) s->hdc_lowv = (c[0] >> 3) & 1;     // BTST: its supply is under 2.8 V
    return 1;
}

/* ---------- the lot ---------- */

static uint8_t ADDR[SENSORS] = { ADDR_CCS811, ADDR_BMP280, ADDR_ADS1115, ADDR_HDC1080, 0 };
static const char *const NAME[SENSORS] = { "CCS811", "BMP280", "ADS1115", "HDC1080", "DHT22" };

/* the guard. a chip that holds the bus makes ESP-IDF's I2C driver wait forever, the task watchdog
 * restarts the board, and the same chip would hang it again: a restart loop, the disease of the
 * old OWLOS. so the sensor being talked to is written into RTC memory first, which lives through
 * a restart but not a power cut. after a watchdog or panic restart that sensor is left alone till
 * the power goes off, and the screen says why */
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
            ESP_LOGE(TAG, "%s hung the I2C bus before the restart: left alone till the power goes off", NAME[i]);
        }
}

void sensors_init(sens_t *s)
{
    memset(s, 0, sizeof *s);
    dht_init();
    for (int i = 0; i < SENSORS; i++) s_retry[i] = 0;
    guard_boot(s);
}

static void start(sens_t *s, int i, uint32_t now)
{
    s_retry[i] = now + RETRY_MS;
    s_fails[i] = 0;
    guard(i);
    // the pressure chip answers at 0x76 or 0x77, by its SDO pin (a plugged-in module may differ)
    if (i == SEN_BMP280 && !i2c_probe(ADDR[i])) ADDR[i] = ADDR[i] == 0x76 ? 0x77 : 0x76;
    if (ADDR[i] && !i2c_probe(ADDR[i])) {
        unguard();
        if (i == SEN_BMP280) ADDR[i] = ADDR_BMP280;
        if (s->state[i] != SS_ABSENT) ESP_LOGW(TAG, "%s: nothing at 0x%02X", NAME[i], ADDR[i]);
        s->state[i] = SS_ABSENT;
        snprintf(s->info[i], sizeof s->info[0], i == SEN_BMP280 ? "NOTHING AT 0x76, 0x77" : "NOTHING AT 0x%02X", ADDR[i]);
        return;
    }
    if (i == SEN_BMP280 && s_bmp_addr != ADDR[i]) {                // it moved: talk to it at its new address

        s_bmp = 0;
        s_bmp_addr = ADDR[i];
    }
    int ok = i == SEN_CCS811 ? ccs_start(s) : i == SEN_BMP280 ? bmp_start(s) : i == SEN_HDC1080 ? hdc_start(s) : ads_start(s);
    unguard();
    s->state[i] = ok ? (i == SEN_CCS811 ? SS_STARTING : SS_OK) : SS_ERROR;
    s_since[i] = now;
    ESP_LOGI(TAG, "%s at 0x%02X: %s (%s)", NAME[i], ADDR[i], ok ? "started" : "FAILED", s->info[i]);
}

// a read went wrong: after FAILS_MAX in a row the sensor counts as gone and is started again later
static void failed(sens_t *s, int i, uint32_t now)
{
    s->fail[i]++;
    if (++s_fails[i] >= FAILS_MAX && s->state[i] != SS_ERROR) {
        s->state[i] = SS_ERROR;
        s_retry[i] = now + RETRY_MS;
        ESP_LOGW(TAG, "%s stopped answering", NAME[i]);
    }
}

static void good(sens_t *s, int i)
{
    s->ok[i]++;
    s_fails[i] = 0;
}

void sensors_read(sens_t *s, uint32_t now)
{
    s->have_ccs = s->have_bmp = s->have_ads = s->have_hdc = s->have_dht = 0;
    for (int i = 0; i < SEN_DHT22; i++)
        if (!(r_bad & (1u << i)) && (s->state[i] == SS_ABSENT || s->state[i] == SS_ERROR) && (int32_t)(now - s_retry[i]) >= 0)
            start(s, i, now);

    if (s->state[SEN_CCS811] == SS_OK || s->state[SEN_CCS811] == SS_STARTING) {
        guard(SEN_CCS811);
        int r = ccs_read(s);
        if (r == 1) { good(s, SEN_CCS811); s->have_ccs = 1; }
        else if (r < 0) { failed(s, SEN_CCS811, now); if (r == -2) ESP_LOGW(TAG, "CCS811 error 0x%02X", s->ccs_error); }
        if (s->state[SEN_CCS811] == SS_STARTING && now - s_since[SEN_CCS811] >= CCS_WARM_S * 1000u) s->state[SEN_CCS811] = SS_OK;
        if ((s_hdc_have || s_dht_have) && (now - s_env_last >= 60000u || !s_env_last)) {
            if (s_hdc_have) ccs_env(s->hdc_t, s->hdc_rh);             // the better of the two
            else ccs_env(s_dht_t, s_dht_rh);
            s_env_last = now ? now : 1;
        }
        unguard();
    }
    if (s->state[SEN_BMP280] == SS_OK) {
        guard(SEN_BMP280);
        int r = bmp_read(s);
        unguard();
        if (r == 1) { good(s, SEN_BMP280); s->have_bmp = 1; }
        else if (r < 0) failed(s, SEN_BMP280, now);
    }
    if (s->state[SEN_ADS1115] == SS_OK) {
        guard(SEN_ADS1115);
        int r = ads_read(s);
        unguard();
        if (r == 1) { good(s, SEN_ADS1115); s->have_ads = 1; }
        else failed(s, SEN_ADS1115, now);
    }
    if (s->state[SEN_HDC1080] == SS_OK) {
        guard(SEN_HDC1080);
        int r = hdc_read(s);
        unguard();
        if (r == 1) { good(s, SEN_HDC1080); s->have_hdc = 1; }
        else { failed(s, SEN_HDC1080, now); if (s->state[SEN_HDC1080] != SS_OK) s_hdc_have = 0; }
    }
    // the DHT22: every 3 s. it has no address, so "absent" means it never answered
    if (now - s_dht_last >= 3000u || !s_dht_last) {
        s_dht_last = now ? now : 1;
        float t, rh;
        int r = dht_read(&t, &rh);
        if (r == 1) {
            good(s, SEN_DHT22);
            s->have_dht = 1;
            s->dht_t = s_dht_t = t;
            s->dht_rh = s_dht_rh = rh;
            s_dht_have = 1;
            if (s->state[SEN_DHT22] != SS_OK) {
                s->state[SEN_DHT22] = SS_OK;
                snprintf(s->info[SEN_DHT22], sizeof s->info[0], "IO%d", PIN_DHT);
                ESP_LOGI(TAG, "DHT22 on IO%d answers", PIN_DHT);
            }
        } else {
            s->fail[SEN_DHT22]++;
            if (++s_fails[SEN_DHT22] >= FAILS_MAX) {
                if (s->state[SEN_DHT22] == SS_OK) ESP_LOGW(TAG, "DHT22 stopped answering");
                s->state[SEN_DHT22] = s->ok[SEN_DHT22] ? SS_ERROR : SS_ABSENT;
                s_dht_have = 0;
                snprintf(s->info[SEN_DHT22], sizeof s->info[0], r < 0 ? "IO%d, BAD CHECKSUM" : "IO%d, NO ANSWER", PIN_DHT);
            }
        }
    }
}
