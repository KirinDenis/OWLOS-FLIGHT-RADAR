# LEDs: the CYD's RGB LED on LEDC, and a WS2812 on the RMT

Sources: OWLOS Flight Radar `firmware/main/led.c`, `core/light.c`; OWLOS Thermo Relay `firmware/main/relay.c`.
Sureness words as in [SKILL.md](../SKILL.md).

## The facts

- **The CYD's RGB LED**: three LEDC channels on a timer of their own, inverted because each colour is pulled to
  ground to light. Timer 1, channels 1, 2, 3, 5 kHz, 10 bits, `.flags.output_invert = 1`, so duty 0 is dark, and
  the backlight's timer 0 and channel 0 are left alone. Green 16, blue 17.
- **Red is a setting, GPIO 4 or 22.** 4 on the CYD, 22 on LCDWIKI's ESP32-32E boards (E32R28T, E32R35T, also sold
  by Elegoo), where GPIO 4 switches the speaker amplifier, low is on. Nothing on the board tells them apart, so
  never drive both: the person picks, the test runs on the other pin. The old pin is let go with
  `gpio_reset_pin` [measured: the 3.5" here lit red on 22 alone]. Board files have the rest:
  [ili9341](../../esp32-boards/references/boards/ili9341.md), [e32r28t](../../esp32-boards/references/boards/e32r28t.md).
- **Finding an unknown LED pin**: hold one pin low for minutes and ask yes or no. A blink-count scan was too hard to
  read.
- **Levels**: the CYD's LED is bright. 12, 64, 255 of 255 for low, mid, high in Flight Radar. In Thermo Relay a
  fifth reads well in a lit room, and green gets less than red at the same look (110 against 220 of 1023).
- **WS2812 over the RMT** [built]: on the ES3C28P and the NM-CYD-C5 (behind a level shifter there), green red blue
  (Bruce's order). 10 MHz resolution, a tick is 0.1 us; bytes encoder, bit0 3 and 9 ticks, bit1 9 and 3, MSB first.
  - `mem_block_symbols` from the SOC macro (`SOC_RMT_MEM_WORDS_PER_CHANNEL`: 64 on the ESP32, 48 on C3, S3, C5): it
    must be even and at least one block [docs, rmt_tx.c], and the block is not the same size on every chip. Hard
    coded, `rmt_new_tx_channel` fails on one chip and not another.
  - The RMT reads the buffer after `rmt_transmit` returns: `rmt_tx_wait_all_done(s_ch, 20)` before touching it,
    and the buffer is static, never on the stack. Otherwise it flickers or shows old colours [from the code
    comment, not seen].
  - 50 us of low latches the colour. Changing at most once a frame is far enough apart.
  - Send only on a change: compare with the last value.
- **LED and backlight on one LEDC timer**: fine at the same settings (Thermo Relay does it, and configures it
  itself since it runs before the screen); different settings on one timer are not. Flight Radar gives the LED
  timer 1.
- Not driven by the radar: the ZLX-ESP32-2's green LED, the JC8012's WS2812, the S3 RGB boards' (they have none).

## led.c, all of it

All of led.c. Red is an argument because it is a setting: GPIO 4 on the CYD, 22 on LCDWIKI's ESP32-32E
boards, never both. Each colour of a CYD's LED is lit by pulling its pin to ground, so the outputs are inverted and duty 0
is dark.

```c
/* the RGB LED on the back of the CYD: three LEDC channels on a timer of their own, the backlight's
 * (lcd_spi.c, timer 0 and channel 0) is left alone. each colour is pulled to ground to light, so
 * the outputs are inverted: duty 0 is dark. a WS2812 instead on the boards that have one (board.h
 * PIN_LED_WS2812). what it shows is core/light.c's */
#include "led.h"
#include "board.h"

#if RADAR_HAS_LED && defined(PIN_LED_WS2812)
/* one WS2812 on one pin: 24 bits, green red blue, the highest first. a bit is a high and a low,
 * 0.3 + 0.9 us a 0 and 0.9 + 0.3 us a 1, sent by the RMT. 50 us low and it takes them: the screen
 * task changes it at most once a frame, far more apart than that */
#include <string.h>
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

static const char *TAG = "led";
static rmt_channel_handle_t s_ch;
static rmt_encoder_handle_t s_bits;
static uint8_t s_now[3];
static uint8_t s_grb[3];                               // the RMT reads it after rmt_transmit is back
static int s_ready;

void led_init(int red_gpio)
{
    (void)red_gpio;                                    // one pin for all three colours here
    rmt_tx_channel_config_t ch = {
        .gpio_num = PIN_LED_WS2812,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,             // a tick is 0.1 us
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,   // 24 bits fit
        .trans_queue_depth = 1,
    };
    rmt_bytes_encoder_config_t bits = {
        .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
        .flags.msb_first = 1,
    };
    esp_err_t e = rmt_new_tx_channel(&ch, &s_ch);
    if (e == ESP_OK) e = rmt_new_bytes_encoder(&bits, &s_bits);
    if (e == ESP_OK) e = rmt_enable(s_ch);
    if (e != ESP_OK) { ESP_LOGE(TAG, "WS2812 on GPIO %d: %s", PIN_LED_WS2812, esp_err_to_name(e)); return; }
    s_ready = 1;
    s_now[0] = 1;                                      // not what led_set gets first: dark goes out too
    const uint8_t dark[3] = { 0, 0, 0 };
    led_set(dark);
    ESP_LOGI(TAG, "WS2812 LED on GPIO %d", PIN_LED_WS2812);
}

void led_red(int gpio) { (void)gpio; }                 // a CYD setting, nothing to move here

void led_set(const uint8_t rgb[3])
{
    if (!s_ready || !memcmp(rgb, s_now, 3)) return;
    memcpy(s_now, rgb, 3);
    rmt_tx_wait_all_done(s_ch, 20);                    // the last one out first: s_grb is read while it goes
    s_grb[0] = rgb[1];
    s_grb[1] = rgb[0];
    s_grb[2] = rgb[2];
    const rmt_transmit_config_t once = { .loop_count = 0 };
    rmt_transmit(s_ch, s_bits, s_grb, sizeof s_grb, &once);
}

#elif RADAR_HAS_LED
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "led";
static int s_pin[3] = { -1, PIN_LED_G, PIN_LED_B };           // red's comes from the settings
static const ledc_channel_t CH[3] = { LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3 };
static uint8_t s_now[3];
static int s_ready;

static esp_err_t channel(int i)
{
    ledc_channel_config_t ch = {
        .gpio_num = s_pin[i],
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CH[i],
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 1,
    };
    s_now[i] = 0;
    return ledc_channel_config(&ch);
}

void led_init(int red_gpio)
{
    s_pin[0] = red_gpio;
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                              .timer_num = LEDC_TIMER_1, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) { ESP_LOGE(TAG, "timer: %s", esp_err_to_name(e)); return; }
    for (int i = 0; i < 3; i++) {
        e = channel(i);
        if (e != ESP_OK) { ESP_LOGE(TAG, "GPIO %d: %s", s_pin[i], esp_err_to_name(e)); return; }
    }
    s_ready = 1;
    ESP_LOGI(TAG, "RGB LED on GPIO %d %d %d, lit by pulling them low", s_pin[0], s_pin[1], s_pin[2]);
}

void led_red(int gpio)
{
    if (!s_ready || gpio == s_pin[0]) return;
    gpio_reset_pin(s_pin[0]);                          // off the LEDC: an input with its pull-up, nothing driven
    s_pin[0] = gpio;
    esp_err_t e = channel(0);
    if (e != ESP_OK) ESP_LOGE(TAG, "GPIO %d: %s", gpio, esp_err_to_name(e));
    else ESP_LOGI(TAG, "red moved to GPIO %d", gpio);
}

void led_set(const uint8_t rgb[3])
{
    if (!s_ready) return;
    for (int i = 0; i < 3; i++) {
        if (rgb[i] == s_now[i]) continue;              // the same as now: nothing to write
        s_now[i] = rgb[i];
        ledc_set_duty(LEDC_LOW_SPEED_MODE, CH[i], (uint32_t)rgb[i] * 1023u / 255u);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, CH[i]);
    }
}

#else
void led_init(int red_gpio) { (void)red_gpio; }
void led_red(int gpio) { (void)gpio; }
void led_set(const uint8_t rgb[3]) { (void)rgb; }
#endif
```

Thermo Relay, for comparison, shares the backlight's timer 0 (channels 1 to 3, same 5 kHz, 10 bits) and
configures it itself, because it runs before the screen does. Its colours, duty of 1023:

```c
enum { CH_R = LEDC_CHANNEL_1, CH_G = LEDC_CHANNEL_2, CH_B = LEDC_CHANNEL_3 };
static const struct { uint16_t r, g, b; } COLOUR[] = {
    [LED_OFF]   = { 0, 0, 0 },
    [LED_IDLE]  = { 0, 110, 0 },
    [LED_WAIT]  = { 200, 80, 0 },
    [LED_HEAT]  = { 220, 0, 0 },
    [LED_COOL]  = { 0, 0, 220 },
    [LED_ALARM] = { 300, 0, 0 },
};
```

Full brightness is blinding, a fifth of it reads well in a lit room. Green looks brighter than red at the
same current, so it gets less. An alarm blinks twice a second so it cannot be taken for steady red. At
power on it shows red, green, blue, 1.5 s each, while the screen says what should glow and on which pin:
a person with another board sees at once if the pins differ.

Flight Radar's colours in `core/light.c` (0 to 255, then scaled by level 12, 64 or 255): SOS red fast, IN RANGE
green, TAKE-OFF blue, LANDING cyan, ANY NEAR white in a short blink, NO DATA amber (255, 80, 0), and a weather
warning that came or got worse yellow (the WEATHER row).

How sure: LEDC LED on the 2.8" (Thermo Relay on the board) and the 3.5" [measured: checked by eye on the boards here].
Red on 22 found on the 3.5" [measured]. WS2812 [built], on the ES3C28P
(GPIO 42) and NM-CYD-C5 (GPIO 27, behind a level shifter).
