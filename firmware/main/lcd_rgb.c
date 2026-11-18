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

#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_idf_version.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

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
/* the CrowPanel Advance 5.0 keeps them off the S3's pins, each version of it its own way (printed
 * on the board). V1.0: a PCA9557 at 0x18, bit 1 the backlight, bit 2 the touch's reset. from V1.1:
 * an STC8H1K28 at 0x30 that takes a byte: V1.1 0x05 off to 0x10 brightest, 0x19 wakes the touch;
 * V1.2 and V1.3 0 brightest to 245 off, 250 wakes the touch. nothing tells V1.1 from V1.2, and
 * 0x10 is brightest on one and very nearly on the other: it is all this sends, the board has no
 * dimming to offer anyway */
#define EXPANDER_ADDR 0x18
#define STC_ADDR      0x30
#define GT911_ADDR    0x5D

static i2c_master_dev_handle_t s_board;
static int s_expander;

static void chip_send(uint8_t v)
{
    if (i2c_master_transmit(s_board, &v, 1, 50) != ESP_OK) ESP_LOGW(TAG, "the board's chip did not take 0x%02x", v);
}

static void expander_set(uint8_t reg, uint8_t v)
{
    const uint8_t b[2] = { reg, v };
    if (i2c_master_transmit(s_board, b, 2, 50) != ESP_OK) ESP_LOGW(TAG, "the expander did not take register %d", reg);
}

// the touch's interrupt line low for a moment, then let go: it may not be driven high, the chip drives it too
static void int_low(int ms)
{
    gpio_config_t out = { .pin_bit_mask = 1ULL << PIN_T_INT, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&out);
    gpio_set_level(PIN_T_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_T_INT, .mode = GPIO_MODE_INPUT };
    gpio_config(&in);
}

static void board_start(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));
    s_expander = i2c_probe(EXPANDER_ADDR);
    if (s_expander) {
        s_board = i2c_dev(EXPANDER_ADDR);
        expander_set(1, 0x00);                         // backlight off, the touch held in reset
        expander_set(3, 0xE1);                         // bits 1 to 4 are outputs, as in Elecrow's V1.0 demos
        gpio_config_t out = { .pin_bit_mask = 1ULL << PIN_T_INT, .mode = GPIO_MODE_OUTPUT };
        gpio_config(&out);
        gpio_set_level(PIN_T_INT, 0);                  // low through its reset: the touch answers at 0x5D
        vTaskDelay(pdMS_TO_TICKS(20));
        expander_set(1, 0x04);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_T_INT, .mode = GPIO_MODE_INPUT };
        gpio_config(&in);
        ESP_LOGI(TAG, "a PCA9557 at 0x%02x: board V1.0", EXPANDER_ADDR);
        return;
    }
    s_board = i2c_dev(STC_ADDR);
    /* Elecrow's demos ask till both answer, for ever. here five times, then on without: a dark
     * board that says so in the log is better than one that hangs */
    for (int tries = 0; tries < 5; tries++) {
        int stc = i2c_probe(STC_ADDR), touch = i2c_probe(GT911_ADDR);
        ESP_LOGI(TAG, "the board's chip %s, the touch %s", stc ? "answers" : "is silent", touch ? "answers" : "is silent");
        if (stc && touch) return;
        chip_send(0x19);
        chip_send(250);
        int_low(120);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void board_light(void)
{
    if (s_expander) expander_set(1, 0x06);             // out of reset, and the light
    else if (s_board) chip_send(0x10);
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
        const canvas_t one = { cv->px + r * GFX_PW, cv->y0 + r, 1, 0, 0 };
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
