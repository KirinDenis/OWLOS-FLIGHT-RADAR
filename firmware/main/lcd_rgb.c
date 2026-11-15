/*
 * the 5" boards, the 7" and the 4.3": an 800x480 or 480x272 panel on the S3's 16-bit RGB bus. no
 * commands and no window here: the chip reads a whole frame out of memory all the time, so a strip is only
 * put in that frame. the frame lies in the PSRAM and goes out through two small buffers in the RAM
 * (esp_lcd's bounce buffers, what Elecrow's IDF examples do), which ride out what else the PSRAM is
 * busy with. with the code in the PSRAM too (sdkconfig.defaults.s3rgb) a write to the flash does not
 * stop it.
 * written without a board with this bus at hand; a viewer's CrowPanel runs it (board.h)
 */
#include "display.h"
#if DISP_LCD_RGB

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_idf_version.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lcd";

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;                          // the frame, row by row. NULL: no panel, the rest goes on
static int s_profile, s_ready, s_flip;

/* the frame's rows have to divide by this, or esp_lcd takes no panel at all */
#ifdef LCD_RGB_BOUNCE
#define BOUNCE_ROWS LCD_RGB_BOUNCE
#else
#define BOUNCE_ROWS 10
#endif
_Static_assert(GFX_PH % BOUNCE_ROWS == 0, "the frame's rows must divide by the bounce buffer's");

/* the clocks round a row and a frame: the 5" panels' unless board.h names its own */
#ifndef LCD_RGB_H_PULSE
#define LCD_RGB_H_PULSE 4
#endif
#ifndef LCD_RGB_H_BACK
#define LCD_RGB_H_BACK 8
#endif
#ifndef LCD_RGB_H_FRONT
#define LCD_RGB_H_FRONT 8
#endif
#ifndef LCD_RGB_V_PULSE
#define LCD_RGB_V_PULSE 4
#endif
#ifndef LCD_RGB_V_BACK
#define LCD_RGB_V_BACK 8
#endif
#ifndef LCD_RGB_V_FRONT
#define LCD_RGB_V_FRONT 8
#endif

/* the two colour orders, BOOT in the first seconds or the site: the sources name the pins of red
 * and blue differently, and a bus has nothing to ask. the second one trades the two groups of
 * five, lowest bit first, blue is the low five */
static const char *const PROFILE_NAME[LCD_PROFILES] = { "RGB", "BGR" };

static void panel_up(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_RGB_HZ,
            .h_res = GFX_PW,
            .v_res = GFX_PH,
            .hsync_pulse_width = LCD_RGB_H_PULSE,
            .hsync_back_porch = LCD_RGB_H_BACK,
            .hsync_front_porch = LCD_RGB_H_FRONT,
            .vsync_pulse_width = LCD_RGB_V_PULSE,
            .vsync_back_porch = LCD_RGB_V_BACK,
            .vsync_front_porch = LCD_RGB_V_FRONT,
            .flags.pclk_active_neg = 1,
#ifdef LCD_RGB_SYNC_IDLE_LOW
            .flags.hsync_idle_low = 1,
            .flags.vsync_idle_low = 1,
#endif
        },
        .data_width = 16,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        /* 6.0 names the pixel format. 5.5 still has bits_per_pixel. both are 16-bit RGB565 */
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
#else
        .bits_per_pixel = 16,
#endif
        .num_fbs = 1,
        .bounce_buffer_size_px = GFX_PW * BOUNCE_ROWS,
        .dma_burst_size = 64,
        .hsync_gpio_num = PIN_LCD_HSYNC,
        .vsync_gpio_num = PIN_LCD_VSYNC,
        .de_gpio_num = PIN_LCD_DE,
        .pclk_gpio_num = PIN_LCD_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = { LCD_RGB_DATA },
        .flags.fb_in_psram = 1,
    };
    if (s_profile) {
        for (int i = 0; i < 5; i++) {
            int t = cfg.data_gpio_nums[i];
            cfg.data_gpio_nums[i] = cfg.data_gpio_nums[11 + i];
            cfg.data_gpio_nums[11 + i] = t;
        }
    }
    s_fb = NULL;
    /* a board without PSRAM, or with it asleep, ends here: said in the log and the rest runs, the
     * site can still reach the board. it would be a restart for ever otherwise */
    esp_err_t e = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "no RGB panel: %s", esp_err_to_name(e));
        s_panel = NULL;
        return;
    }
    void *fb = NULL;
    e = esp_lcd_panel_reset(s_panel);
    if (e == ESP_OK) e = esp_lcd_panel_init(s_panel);
    if (e == ESP_OK) e = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "the RGB panel did not start: %s", esp_err_to_name(e));
        return;
    }
    memset(fb, 0, GFX_PW * GFX_PH * sizeof(uint16_t));
    s_fb = fb;
}

/* --- the backlight and the touch's reset --- */

#if LCD_BOARD_CHIP
#include "esp_rom_sys.h"
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "soc/usb_serial_jtag_struct.h"
#endif
/* the CrowPanel Advance 5.0 keeps them off the S3's pins, each version of it its own way (printed
 * on the board). V1.0: a PCA9557 at 0x18, bit 1 the backlight, bit 2 the touch's reset. from V1.1:
 * an STC8H1K28 at 0x30 that takes a byte: V1.1 0x05 off to 0x10 brightest, 0x19 wakes the touch;
 * V1.2 and V1.3 0 brightest to 245 off, 250 wakes the touch. nothing tells V1.1 from V1.2, and
 * 0x10 is brightest on one and very nearly on the other: it is all this sends, the board has no
 * dimming to offer anyway. Elecrow's lessons talk to it at 100 kHz (Wire's own speed). at 400 kHz
 * it takes nothing, and the log says it did not take 0x10, which is the backlight */
#define EXPANDER_ADDR 0x18
#define STC_ADDR      0x30
#define GT911_ADDR    0x5D
#define STC_HZ        100000

static int s_expander;

/* the chip's own I2C driver never got an answer out of this board (ESP-IDF 6). Elecrow's lessons
 * use Arduino's Wire, which is just the two pins. the same here, open drain, about 50 kHz: slower
 * than their 100 kHz, which this chip accepts, and slower than the 400 kHz it ignores.
 * WiFi's start marks every pin to be let go of while the radio naps. these two are RTC pins, and
 * a nap then leaves them floating, so a chip's answer reads as silence. they stay driven */
static void pin_awake(gpio_num_t pin)
{
    gpio_hold_dis(pin);
    gpio_sleep_sel_dis(pin);
}

static void bb_idle(void)
{
    gpio_set_level(PIN_I2C_SDA, 1);
    gpio_set_level(PIN_I2C_SCL, 1);
}

static void bb_init(void)
{
    gpio_reset_pin(PIN_I2C_SDA);
    gpio_reset_pin(PIN_I2C_SCL);
    pin_awake(PIN_I2C_SDA);
    pin_awake(PIN_I2C_SCL);
    gpio_set_direction(PIN_I2C_SDA, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(PIN_I2C_SCL, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(PIN_I2C_SDA, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_I2C_SCL, GPIO_PULLUP_ONLY);
    bb_idle();
    /* nine clocks, then a stop: a chip left mid-byte from the last boot holds SDA down */
    for (int i = 0; i < 9; i++) {
        gpio_set_level(PIN_I2C_SCL, 0);
        esp_rom_delay_us(10);
        gpio_set_level(PIN_I2C_SCL, 1);
        esp_rom_delay_us(10);
    }
    gpio_set_level(PIN_I2C_SDA, 0);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SCL, 1);
    esp_rom_delay_us(10);
    bb_idle();
}

static void bb_scl_high(void)
{
    gpio_set_level(PIN_I2C_SCL, 1);
    for (int i = 0; i < 50 && !gpio_get_level(PIN_I2C_SCL); i++) esp_rom_delay_us(10);   // the chip may stretch
}

static int bb_byte(uint8_t v)
{
    for (int bit = 7; bit >= 0; bit--) {
        gpio_set_level(PIN_I2C_SDA, (v >> bit) & 1);
        esp_rom_delay_us(10);
        bb_scl_high();
        esp_rom_delay_us(10);
        gpio_set_level(PIN_I2C_SCL, 0);
        esp_rom_delay_us(10);
    }
    gpio_set_level(PIN_I2C_SDA, 1);
    esp_rom_delay_us(10);
    bb_scl_high();
    esp_rom_delay_us(5);                            // the chip pulls SDA after the clock rises
    int ack = gpio_get_level(PIN_I2C_SDA) == 0;
    esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SCL, 0);
    return ack;
}

static int bb_write(uint8_t addr, const uint8_t *b, int n)
{
    gpio_set_level(PIN_I2C_SDA, 0);                 // start: SDA falls while SCL is high
    esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SCL, 0);
    int ok = bb_byte((uint8_t)(addr << 1));
    for (int i = 0; ok && i < n; i++) ok = bb_byte(b[i]);
    gpio_set_level(PIN_I2C_SDA, 0);                 // stop
    esp_rom_delay_us(10);
    bb_scl_high();
    esp_rom_delay_us(10);
    bb_idle();
    esp_rom_delay_us(10);
    return ok;
}

static int bb_probe(uint8_t addr) { return bb_write(addr, NULL, 0); }

/* one pass over the bus, for the log: who, if anyone, is actually there */
static void bb_log_bus(const char *when)
{
    char hits[80];
    int n = 0;
    hits[0] = '\0';
    for (int a = 0x08; a < 0x78; a++) {
        if (!bb_probe((uint8_t)a)) continue;
        int room = (int)sizeof hits - n;
        if (room < 6) break;
        n += snprintf(hits + n, (size_t)room, "%s0x%02x", n ? " " : "", a);
    }
    if (n)
        ESP_LOGI(TAG, "%s: i2c answers %s", when, hits);
    else
        ESP_LOGW(TAG, "%s: i2c empty, SDA %d, SCL %d, clock pin %d", when,
                 gpio_get_level(PIN_I2C_SDA), gpio_get_level(PIN_I2C_SCL), gpio_get_level(PIN_BOARD_HOLD));
}

// the touch's interrupt line low for a moment, then let go: it may not be driven high, the chip drives it too
static void int_low(int ms)
{
    /* gpio_config on a pin it already owns logs "conflict found". direction is enough the second time */
    static int ready;
    if (!ready) {
        gpio_reset_pin(PIN_T_INT);
        pin_awake(PIN_T_INT);
        ready = 1;
    }
    gpio_set_direction(PIN_T_INT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_T_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_direction(PIN_T_INT, GPIO_MODE_INPUT);
}

/* GPIO 19 is the microphone clock, and also the S3's USB D-. the ROM leaves the USB pad on, so a
 * write never reaches the clock until that pad is off. held so a nap cannot let the clock float.
 * high is only a try: Elecrow's lessons leave the pin an output, which sits low */
static void clock_usb_off(void)
{
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    USB_SERIAL_JTAG.conf0.usb_pad_enable = 0;
#endif
    pin_awake(PIN_BOARD_HOLD);
    gpio_reset_pin(PIN_BOARD_HOLD);
    pin_awake(PIN_BOARD_HOLD);
}

static void clock_release(void)
{
    clock_usb_off();
    gpio_set_direction(PIN_BOARD_HOLD, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_BOARD_HOLD, GPIO_PULLUP_ONLY);
}

static void clock_drive(int high)
{
    clock_usb_off();
    gpio_set_direction(PIN_BOARD_HOLD, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BOARD_HOLD, high ? 1 : 0);
    gpio_hold_en(PIN_BOARD_HOLD);
}

static int board_here(void)
{
    s_expander = bb_probe(EXPANDER_ADDR);
    return s_expander || bb_probe(STC_ADDR);
}

static void board_start(void)
{
    bb_init();
    /* holding the clock low from the first instruction never got an answer. try the pin left
     * alone, then low, then high, and keep the one the chip answers on */
    clock_release();
    vTaskDelay(pdMS_TO_TICKS(30));
    int here = board_here();
    if (!here) {
        bb_log_bus("clock pin left alone");
        clock_drive(0);
        vTaskDelay(pdMS_TO_TICKS(30));
        here = board_here();
    }
    if (!here) {
        bb_log_bus("clock pin held low");
        clock_drive(1);
        vTaskDelay(pdMS_TO_TICKS(30));
        here = board_here();
        if (!here) {
            bb_log_bus("clock pin held high");
            clock_drive(0);
        }
    }
    if (here)
        ESP_LOGI(TAG, "the board's chip answers, clock pin reads %d", gpio_get_level(PIN_BOARD_HOLD));
    if (s_expander) {
        bb_write(EXPANDER_ADDR, (const uint8_t[]){ 1, 0x00 }, 2);   // backlight off, the touch held in reset
        bb_write(EXPANDER_ADDR, (const uint8_t[]){ 3, 0xE1 }, 2);   // bits 1 to 4 are outputs, as in Elecrow's V1.0 demos
        int_low(20);
        bb_write(EXPANDER_ADDR, (const uint8_t[]){ 1, 0x04 }, 2);
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "a PCA9557 at 0x%02x: board V1.0", EXPANDER_ADDR);
        return;
    }
    /* Elecrow's demos ask till both answer, for ever. here eight times, then on without: a dark
     * board that says so in the log is better than one that hangs */
    for (int tries = 0; tries < 8; tries++) {
        int stc = bb_probe(STC_ADDR), touch = bb_probe(GT911_ADDR) || bb_probe(0x14);
        ESP_LOGI(TAG, "the board's chip %s, the touch %s", stc ? "answers" : "is silent", touch ? "answers" : "is silent");
        if (stc && touch) return;
        bb_write(STC_ADDR, (const uint8_t[]){ 250 }, 1);    // V1.2 and V1.3: let the touch out
        bb_write(STC_ADDR, (const uint8_t[]){ 0x19 }, 1);   // V1.1: the same
        int_low(120);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!bb_probe(STC_ADDR))
        ESP_LOGW(TAG, "the board's chip stayed silent. its switch selects the microphone: that is the position this needs");
}

static void board_light(void)
{
    static int sent;
    if (sent) return;                              // the I2C driver owns the pins after this
    sent = 1;
    if (s_expander) {
        if (!bb_write(EXPANDER_ADDR, (const uint8_t[]){ 1, 0x06 }, 2))
            ESP_LOGW(TAG, "the expander did not take the backlight");
        return;
    }
    /* V1.2 and V1.3: 0 is full brightness. V1.1: 0x10 is. 0 last, so a new board is fully lit;
     * on a V1.1, 0x10 has already been sent and 0 is not one of its levels */
    int on = bb_write(STC_ADDR, (const uint8_t[]){ 0x10 }, 1);
    on = bb_write(STC_ADDR, (const uint8_t[]){ 0x00 }, 1) || on;
    if (!on) ESP_LOGW(TAG, "the board's chip did not take the backlight");
}
#endif

void disp_backlight(int percent)
{
#if LCD_BOARD_CHIP
    static int lit;
    if (percent > 0 && !lit) {                         // the first frame is in: only now, no noise from the start
        lit = 1;
        board_light();
    }
#else
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(1023 * percent / 100));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}

void disp_init(void)
{
#if LCD_BOARD_CHIP
    board_start();
#else
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));
    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
#endif
    panel_up();
#if LCD_BOARD_CHIP
    board_light();                                 // the frame is cleared, and the I2C driver does not own the pins yet
#endif
    s_ready = 1;
    ESP_LOGI(TAG, "RGB bus %s, %dx%d, %d MHz, frame %s", PROFILE_NAME[s_profile], GFX_PW, GFX_PH, LCD_RGB_HZ / 1000000,
             s_fb ? "in the PSRAM" : "NOT THERE");
}

int disp_profiles(void) { return LCD_PROFILES; }
const char *disp_profile_name(void) { return PROFILE_NAME[s_profile]; }

/* before disp_init: just remember it. after: the panel again with the other pins, between frames */
void disp_profile(int p)
{
    s_profile = p >= 0 && p < LCD_PROFILES ? p : 0;
    if (!s_ready) return;
    if (s_panel) esp_lcd_panel_del(s_panel);
    panel_up();
    ESP_LOGI(TAG, "screen started as %s", PROFILE_NAME[s_profile]);
}

void disp_flip(int on) { s_flip = on ? 1 : 0; }

/* into the frame, where the strip's rows are. turned over: last row first, each one back to front */
static void put(const canvas_t *cv)
{
    if (!s_fb) return;
    if (!s_flip) {
        gfx_pack565(cv, s_fb + cv->y0 * GFX_PW);
        return;
    }
    uint16_t line[GFX_PW];
    for (int r = 0; r < cv->h; r++) {
        const canvas_t one = { cv->px + r * GFX_PW, cv->y0 + r, 1, 0 };
        gfx_pack565(&one, line);
        uint16_t *to = s_fb + (GFX_PH - 1 - one.y0) * GFX_PW;
        for (int x = 0; x < GFX_PW; x++) to[GFX_PW - 1 - x] = line[x];
    }
}

void disp_strip(const canvas_t *cv) { put(cv); }
/* the second core's strip goes in by itself, nothing is in flight on this bus to wait for */
void disp_pack_under(const canvas_t *under) { put(under); }
void disp_strip2(const canvas_t *cv, int under_rows) { (void)under_rows; put(cv); }
int disp_rows(int rows) { (void)rows; return 1; }

void disp_frame_end(void) { }
uint32_t disp_frame_ms(void) { return 0; }
void disp_full_next(void) { }
uint32_t disp_lost(void) { return 0; }
void disp_wait_idle(void) { }

#endif
