// DS18B20 on bit-banged 1-Wire, timing from the Maxim app note 126 (standard speed)
#include "ds18b20.h"
#include "board.h"
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"

/* the slots are tens of microseconds and an interrupt in the middle breaks them, so every
 * slot runs with interrupts off. the longest is the reset sample, 70 us */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void ds_init(void)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << PIN_PROBE,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,      // open drain: we only pull low, the resistor pulls up
        .pull_up_en = GPIO_PULLUP_ENABLE,       // the weak inner one helps on short wires, 4.7k is still wanted
    };
    gpio_config(&c);
    gpio_set_level(PIN_PROBE, 1);
}

static int ow_reset(void)
{
    if (!gpio_get_level(PIN_PROBE)) {           // held low: shorted, or nothing pulls it up
        esp_rom_delay_us(10);
        if (!gpio_get_level(PIN_PROBE)) return 0;
    }
    gpio_set_level(PIN_PROBE, 0);
    esp_rom_delay_us(480);
    portENTER_CRITICAL(&s_mux);
    gpio_set_level(PIN_PROBE, 1);
    esp_rom_delay_us(70);
    int present = !gpio_get_level(PIN_PROBE);   // the probe answers by pulling the line low
    portEXIT_CRITICAL(&s_mux);
    esp_rom_delay_us(410);
    return present;
}

static void ow_write_bit(int b)
{
    portENTER_CRITICAL(&s_mux);
    gpio_set_level(PIN_PROBE, 0);
    esp_rom_delay_us(b ? 6 : 60);
    gpio_set_level(PIN_PROBE, 1);
    portEXIT_CRITICAL(&s_mux);
    esp_rom_delay_us(b ? 64 : 10);
}

static int ow_read_bit(void)
{
    portENTER_CRITICAL(&s_mux);
    gpio_set_level(PIN_PROBE, 0);
    esp_rom_delay_us(3);
    gpio_set_level(PIN_PROBE, 1);
    esp_rom_delay_us(10);
    int b = gpio_get_level(PIN_PROBE);
    portEXIT_CRITICAL(&s_mux);
    esp_rom_delay_us(53);
    return b;
}

static void ow_write(uint8_t v)
{
    for (int i = 0; i < 8; i++) ow_write_bit((v >> i) & 1);
}

static uint8_t ow_read(void)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint8_t)(ow_read_bit() << i);
    return v;
}

static uint8_t crc8(const uint8_t *d, int n)        // Dallas/Maxim, x^8 + x^5 + x^4 + 1
{
    uint8_t c = 0;
    while (n--) {
        uint8_t b = *d++;
        for (int i = 0; i < 8; i++) {
            uint8_t mix = (c ^ b) & 1;
            c >>= 1;
            if (mix) c ^= 0x8C;
            b >>= 1;
        }
    }
    return c;
}

int ds_start(void)
{
    if (!ow_reset()) return 0;
    ow_write(0xCC);                 // SKIP ROM: the only one on the bus
    ow_write(0x44);                 // CONVERT T
    return 1;
}

int ds_read(float *c)
{
    if (!ow_reset()) return 0;
    ow_write(0xCC);
    ow_write(0xBE);                 // READ SCRATCHPAD
    uint8_t b[9];
    int all0 = 1, all1 = 1;
    for (int i = 0; i < 9; i++) {
        b[i] = ow_read();
        if (b[i]) all0 = 0;
        if (b[i] != 0xFF) all1 = 0;
    }
    if (all0 || all1 || crc8(b, 8) != b[8]) return 0;  // zeros pass the CRC, a line stuck low gives them
    int16_t raw = (int16_t)((b[1] << 8) | b[0]);
    *c = (float)raw / 16.0f;
    return *c > -56.0f && *c < 126.0f;
}

int ds_rom(char *buf, int sz)
{
    if (!ow_reset()) return 0;
    ow_write(0x33);                 // READ ROM
    uint8_t r[8];
    for (int i = 0; i < 8; i++) r[i] = ow_read();
    if (crc8(r, 7) != r[7] || !r[0]) return 0;
    snprintf(buf, (size_t)sz, "%02X-%02X%02X%02X%02X%02X%02X", r[0], r[6], r[5], r[4], r[3], r[2], r[1]);
    return 1;
}
