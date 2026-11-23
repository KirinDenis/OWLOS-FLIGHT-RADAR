/*
 * DHT22: the host pulls the line low for a millisecond, lets go, the sensor answers 80 us low,
 * 80 us high, then 40 bits: each one 50 us low and then high, 26 us for a 0 and 70 us for a 1.
 * the timing is read off the CPU cycle counter with interrupts held off, ~5 ms for the lot.
 * the line is open drain with the pull-up on (the modules have their own 10k too)
 */
#include "dht22.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#define MHZ CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void dht_init(void)
{
    gpio_reset_pin(PIN_DHT);
    gpio_set_direction(PIN_DHT, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(PIN_DHT, GPIO_PULLUP_ONLY);
    gpio_set_level(PIN_DHT, 1);
}

// wait while the line stays at level. us it took, or -1 after max_us
static int hold(int level, int max_us)
{
    const uint32_t t0 = esp_cpu_get_cycle_count(), lim = (uint32_t)max_us * MHZ;
    while (gpio_get_level(PIN_DHT) == level)
        if (esp_cpu_get_cycle_count() - t0 > lim) return -1;
    return (int)((esp_cpu_get_cycle_count() - t0) / MHZ);
}

int dht_read(float *t_c, float *rh)
{
    uint8_t b[5] = { 0 };
    int ok = 1;
    gpio_set_level(PIN_DHT, 0);                       // start: low for more than 1 ms
    esp_rom_delay_us(1200);
    portENTER_CRITICAL(&s_mux);
    gpio_set_level(PIN_DHT, 1);                       // let go, the pull-up takes the line high
    if (hold(1, 100) < 0 || hold(0, 100) < 0 || hold(1, 100) < 0) ok = 0;   // its answer: low 80, high 80
    for (int i = 0; ok && i < 40; i++) {
        int h;
        if (hold(0, 80) < 0 || (h = hold(1, 100)) < 0) { ok = -1; break; }
        b[i >> 3] = (uint8_t)(b[i >> 3] << 1 | (h > 45));
    }
    portEXIT_CRITICAL(&s_mux);
    if (ok <= 0) return ok;
    if ((uint8_t)(b[0] + b[1] + b[2] + b[3]) != b[4]) return -1;
    float h = (float)((b[0] << 8) | b[1]) * 0.1f;
    float t = (float)(((b[2] & 0x7F) << 8) | b[3]) * 0.1f;
    if (b[2] & 0x80) t = -t;
    if (h < 0.0f || h > 100.0f || t < -40.0f || t > 80.0f) return -1;
    *t_c = t;
    *rh = h;
    return 1;
}
