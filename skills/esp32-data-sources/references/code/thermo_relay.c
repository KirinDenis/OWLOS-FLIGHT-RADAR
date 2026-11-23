#include "relay.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "led";

/* each colour dimmed by PWM on the backlight's timer (5 kHz, 10 bit), LED on = pin low, so
 * inverted. full brightness is blinding, a fifth of it reads well in a lit room. green looks
 * brighter than red at the same current, so it gets less */
enum { CH_R = LEDC_CHANNEL_1, CH_G = LEDC_CHANNEL_2, CH_B = LEDC_CHANNEL_3 };
static const struct { uint16_t r, g, b; } COLOUR[] = {
    [LED_OFF]   = { 0, 0, 0 },
    [LED_IDLE]  = { 0, 110, 0 },
    [LED_WAIT]  = { 200, 80, 0 },
    [LED_HEAT]  = { 220, 0, 0 },
    [LED_COOL]  = { 0, 0, 220 },
    [LED_ALARM] = { 300, 0, 0 },
};

static volatile int s_state;
static int s_tick;

/* power-on test, the screen says along what should glow: 1 red, 2 green, 3 blue, 4 all off,
 * 1.5 s each from 1.5 s after power on. 0 = over */
int led_test_step(void)
{
    int64_t ms = esp_timer_get_time() / 1000;
    if (ms < 1500 || ms >= 7500) return 0;
    return (int)((ms - 1500) / 1500) + 1;
}

static void set_rgb(uint32_t r, uint32_t g, uint32_t b)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_R, r);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_R);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_G, g);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_G);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_B, b);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_B);
}

// every 250 ms: the alarm goes on and off, everything else just holds its colour
static void led_timer(void *arg)
{
    (void)arg;
    int step = led_test_step();
    if (step || esp_timer_get_time() < 1500000) {     // the power-on test owns the LED for now
        set_rgb(step == 1 ? 512 : 0, step == 2 ? 512 : 0, step == 3 ? 512 : 0);
        return;
    }
    int st = s_state;
    s_tick ^= 1;
    if (st == LED_ALARM && s_tick) set_rgb(0, 0, 0);
    else set_rgb(COLOUR[st].r, COLOUR[st].g, COLOUR[st].b);
}

static int s_pulled_up = -1;

int relay_pulled_up(void) { return s_pulled_up; }

void relay_init(int active_low)
{
    /* before driving it: does something pull the relay input up? a module that switches on LOW
     * does (its optocoupler LED hangs from VCC), one that switches on HIGH or nothing at all
     * lets our weak pull-down win */
    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_RELAY, .mode = GPIO_MODE_INPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE };
    gpio_config(&in);
    esp_rom_delay_us(500);
    s_pulled_up = gpio_get_level(PIN_RELAY);
    ESP_LOGI(TAG, "relay input IO%d left alone reads %s", PIN_RELAY,
             s_pulled_up ? "HIGH: a module that switches on LOW is there" : "LOW: nothing pulls it up");

    gpio_set_level(PIN_RELAY, active_low ? 1 : 0);   // the level is latched before the pin turns output
    gpio_config_t c = { .pin_bit_mask = 1ULL << PIN_RELAY, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&c);
    gpio_set_level(PIN_RELAY, active_low ? 1 : 0);

    // the LED dims on the backlight timer. this runs before the screen is up, so set it here too (same values)
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                              .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) ESP_LOGE(TAG, "LED timer: %s", esp_err_to_name(e));
    const struct { int pin; ledc_channel_t ch; } leds[3] = { { PIN_LED_R, CH_R }, { PIN_LED_G, CH_G }, { PIN_LED_B, CH_B } };
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t ch = {
            .gpio_num = leds[i].pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = leds[i].ch,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
            .flags.output_invert = 1,
        };
        e = ledc_channel_config(&ch);
        if (e != ESP_OK) ESP_LOGE(TAG, "LED channel on GPIO %d: %s", leds[i].pin, esp_err_to_name(e));
    }
    const esp_timer_create_args_t ta = { .callback = led_timer, .name = "led" };
    esp_timer_handle_t h;
    if (esp_timer_create(&ta, &h) == ESP_OK) esp_timer_start_periodic(h, 250 * 1000);
}

void relay_set(int on, int active_low)
{
    gpio_set_level(PIN_RELAY, (on ? 1 : 0) ^ (active_low ? 1 : 0));
}

void led_show(int state)
{
    s_state = state >= LED_OFF && state <= LED_ALARM ? state : LED_OFF;
}

