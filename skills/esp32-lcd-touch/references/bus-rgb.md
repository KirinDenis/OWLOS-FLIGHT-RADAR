# The ESP32-S3 RGB bus: 800x480, 1024x600 and 480x272 panels

How OWLOS Flight Radar puts a picture on the S3's 16-bit RGB parallel panels. Sources: `firmware/main/lcd_rgb.c`,
`lcd_wave7.c`, `lcd_wave7_panel.cpp`, `board.h`, `sdkconfig.defaults.s3rgb` and `.wave7`, chipguyhere's
vendored library in `firmware/components/chipguy_lcd7_7b`, `FORK-NOTES.md`, `BOARDS-WANTED.md`.

Sureness words as in [SKILL.md](../SKILL.md). Pins are in the board files of esp32-boards, linked below.

## What is different on this bus

- There are no commands and no window. The S3's LCD peripheral reads a whole frame out of memory all the
  time and the panel shows what comes: refresh = pixel clock / (htotal x vtotal), so 16 MHz on an 800x480
  with porches 4/8/8 both ways is 16,000,000 / (820 x 500), about 39 Hz [reasoned]. A strip is only copied
  into that frame; nothing waits for a transfer.
- The frame is RGB565, 768,000 bytes at 800x480, so it lives in PSRAM. It goes out through two small bounce
  buffers in internal RAM (esp_lcd's `bounce_buffer_size_px`, what Elecrow's IDF examples do), which ride
  out what else the PSRAM is busy with.
- The frame's rows have to divide by the bounce buffer's rows, or esp_lcd takes no panel at all. 10 rows on
  the 5" and 7" panels, 8 on the 480x272 (272 does not divide by 10) [built, a `_Static_assert` in lcd_rgb.c].
- Byte order: the chip reads the frame, so pixels go in the native order, no swap (`RADAR_RGB_BUS 1`,
  [bus-spi-strips](bus-spi-strips.md) has `gfx_pack565`).
- Colour order is a matter of pins, not of a MADCTL bit: which five data lines carry blue and which red. The
  bus's low five are blue. Sources name them differently, and a bus has nothing to ask. So the second screen
  profile swaps the two groups of five and starts the panel again (code below). RGB first, BGR behind BOOT.
- A board without PSRAM, or with it off, gets no panel: lcd_rgb.c logs it and runs on, so the site can still
  reach the board. A restart loop would hide the log.
- With the code in PSRAM too (XIP from PSRAM), a write to the flash does not stop the picture.
- The layout and the glass: the 800x480 boards draw the 3.5"'s 480x320-class layout 3/2 as big (layout
  534x320), the 1024x600 one 15/8 (547x320). The driver works in the glass's pixels (`GFX_PW`, `GFX_PH`).
  The 480x272 has a layout of its own, one to one.

## The driver, lcd_rgb.c

```c
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

/* --- the backlight and the touch's reset (the CrowPanel Advance 5.0's board chip, below) --- */

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
    /* LEDC timer 0, channel 0, 5 kHz, 10 bits on PIN_LCD_BL, duty 0: as lcd_spi.c */
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
        const canvas_t one = { cv->px + r * GFX_PW, cv->y0 + r, 1, 0, 0, 0 };
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
```

- `pclk_active_neg = 1` on every board here (the 5" setups name the clock idle high) [docs].
- Flip: no MADCTL on this bus. The rows go in last first and each back to front.
- The second core's strip goes straight into the frame: nothing is in flight to wait for, so pairs need no
  transfer logic.

## The CrowPanel Advance 5.0's board chip (`LCD_BOARD_CHIP`)

The backlight and the touch's reset are not on the S3's pins, and how they are reached goes by the board
version printed on it [docs, Elecrow's repository and V1.0 to V1.3 examples]:

- V1.0: a PCA9557 at 0x18, bit 1 the backlight, bit 2 the touch's reset.
- V1.1: an STC8H1K28 at 0x30 on the same I2C that takes one byte: 0x05 off to 0x10 brightest, 0x19 wakes the
  touch, and 0x10 must go first.
- V1.2 and V1.3: the same chip, 0 brightest to 245 off, 250 wakes the touch.
- Nothing tells V1.1 from V1.2, and 0x10 is brightest on one and very nearly on the other, so 0x10 is all the
  build sends: no dimming on this board.
- When 0x30 or the touch at 0x5D does not answer, Elecrow's examples send 250 to 0x30, hold the touch's INT
  low 120 ms, let it go and wait 100 ms, and ask again for ever. Here five times at most, then on: a dark board
  that says so in the log is better than one that hangs.

```c
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
```

The light goes on with the first frame, not at start: no noise from the panel is ever seen.

## The boards, one by one

- **Elecrow CrowPanel Advance 5.0 (DIS02050A), `crowadv50`**: ST7262 800x480 IPS
  ([st7262](panels/st7262.md)), GT911. 16 MHz: Elecrow's V1.0 LovyanGFX setup says 21 MHz, its V1.2 and V1.3
  examples, Arduino and IDF, 16. Porches 8, pulse 4, back 8 both ways, the clock idle high (the defaults in
  the code). Elecrow's IDF example runs the PSRAM at 80 MHz; the repository also has an Arduino build at
  120. First run on a tester's V1.3 [measured, a tester's board]: picture and touch work, 3 fps and 280 ms
  a frame on the radar (disc 151 ms, panel 39, packing into the frame 52), 14 fps on the settings page. The
  second core never started: no room in RAM for its strip, so `DISP_STRIP` went 16 to 12 [built]. Its V1.0
  to V1.2 not seen. Board file: [crowadv50](../../esp32-boards/references/boards/crowadv50.md).
- **Guition JC8048W550C, `jc8048w550`**: ST7262 800x480, GT911 with its own reset GPIO. 16 MHz, porches 8,
  pulse 4, back 8 both ways, from one source so far (rzeldent's JC8048W550C.json, the same as its
  ESP32-8048S050C). That file calls the low five data lines red, every other setup for those pins has them
  blue, and the bus's low five are blue: they go in as listed, with BGR behind BOOT in case [built]. A version
  without touch is sold too. Board file: [jc8048w550](../../esp32-boards/references/boards/jc8048w550.md).
- **ESP32-4827S043C, `esp4827s043`**: 480x272, GT911. The data, sync and touch pins are the JC8048W550's, so
  the 5" build showed a picture on it that spilled over the glass, as an 800x480 frame on a 480x272 glass
  would [measured, a tester's board]. Its own timing: 8 MHz, HSYNC 4/43/8 and VSYNC 4/12/8 (pulse, back,
  front), bounce buffers of 8 rows, a layout of its own at 480x272 [built]. Sold as C (GT911), R (XPT2046,
  not in this build) and N (no touch). Board file:
  [esp4827s043](../../esp32-boards/references/boards/esp4827s043.md).
- **Elecrow CrowPanel 7.0, the plain one (DIS08070H), `crow70`**: 800x480 TN by Elecrow's spec (many shops call
  it IPS), controller not named in any source used here. Rows and frames 48/40/40 and 31/13/1 (pulse, back,
  front) in both of Elecrow's V3.0 examples, the clock 15 MHz in one and 24 in the other: 16 here. An ESPHome
  setup said to work has the same pins, 15 MHz and esp_lcd's own sync polarity. A PCA9557 at 0x18 holds the
  GT911's reset and INT; it is touched only when the touch is silent ([gt911](touch/gt911.md)). The backlight
  pin enables an MT9201. An owner flashed the Advance 5.0 build on it: WiFi and planes, the screen dark, as
  expected with other pins [measured, a tester's board]. Its own build not run yet [built]. The CrowPanel
  Advance 7.0 is another board (N16R8, IPS, SC7277), not built. Board file:
  [crow70](../../esp32-boards/references/boards/crow70.md).
- **Elecrow CrowPanel 5.0, the plain one (DIS07050H), `crow50`**: 800x480, GT911. Brought in by bertvm (pull
  request #11) with the pins a P2000 firmware already runs on this board: 10 MHz, H pulse 1, front 8, back 32,
  V pulse 1, front 8, back 8, both syncs idle low, backlight high on a plain GPIO, touch I2C at 100 kHz with a
  stop between register and read, and a PCA9557 boot sequence ([gt911](touch/gt911.md)) [built, the radar not
  run on one yet]. Board file: [crow50](../../esp32-boards/references/boards/crow50.md).
- **Waveshare ESP32-S3-Touch-LCD-7 and -7B, `wave7` and `wave7b`**: driven by chipguyhere's library, below.
  Board files: [wave7](../../esp32-boards/references/boards/wave7.md),
  [wave7b](../../esp32-boards/references/boards/wave7b.md).

**BOOT is a panel line on four of them.** On the plain CrowPanel 5.0 and 7.0 GPIO 0 is the panel's pixel
clock: no button the firmware can read, and pressing BOOT while the screen runs shorts the clock. On the
Waveshare 7 and 7B GPIO 0 is a panel data line. `PIN_BUTTON -1` there.

## The Waveshare 7 and 7B: chipguyhere's library

Contributed by chipguyhere from the fork chipguyhere/OWLOS-FLIGHT-RADAR-7-7B, MIT. The library owns the panel,
its pins and timing, and the board's I2C expander (panel reset and supply, backlight, the GT911's reset). Its
I2C goes over the firmware's own bus through a small Arduino shim (`i2c_bus_get()`), which the GT911 shares.

What it knows of the two boards [docs, the library's headers; measured where it says]:

- -7: 800x480 ST7262. Porches h 4/8/8, v 4/8/8 (pulse, back, front): htotal 820, vtotal 500, refresh =
  pclk / 410000. 14 MHz, about 34.1 Hz. Backlight on or off only.
- -7B: 1024x600, controller not named. Porches h 162/152/48, v 45/13/3: htotal 1386, vtotal 661, refresh =
  pclk / 916146. 30 MHz, about 32.7 Hz. Backlight PWM through the expander, inverted (more duty is dimmer),
  and 0 does not reach black: off is the enable line.
- The expanders differ and both answer at 0x24. -7: a CH422G with no registers, each function its own
  address (0x24 system, 0x38 outputs, 0x26 inputs, 0x23 open-drain outputs), no PWM, no ADC. -7B: a small
  microcontroller with registers (0x02 mode, 0x03 outputs, 0x04 inputs, 0x05 backlight PWM, 0x06 battery ADC).
  A write to 0x24 means different things to each, so the board is told apart before anything is written: an
  address-only probe of 0x38, which only the CH422G answers (a bench check of that is still open in the
  library).
- Frames: two or three RGB565 buffers in PSRAM, 1.2 MB each on the -7B, 750 KB on the -7. Bounce buffers of
  20 rows by default (2 KB a row on the -7B); built at 10 here to save internal RAM.
- The fill: the library hands each chunk to a memory-to-memory GDMA channel (64-byte bursts) instead of a CPU
  copy. A CPU copy of every chunk took the whole core at 1024x600 [measured by its contributor]. The DMA reads
  PSRAM behind the cache, so every strip packed into a frame is written back from the cache as it is packed
  (`w7_written`, the library's `writeBackRange`), not the whole cache at a flip.
- 180 degrees is done by the library's CPU path (DMA cannot reverse pixel order); 90 and 270 are not done by
  it at all.
- One library, two glasses, but the layout is built for one: each board has its own build, and the wrong one
  says so in the log and leaves the screen dark.

lcd_wave7.c uses two frames: the background (everything that stands still) is drawn strip by strip into the
frame that does not show and is shown from the next panel frame once whole. What moves (planes, sweep,
compass ring) is laid over each chunk as it lands in a bounce buffer, every panel frame: chipguyhere's moving
layer (`ovl.c`). The design, the per-row deadlines and the measurements are in the public repo's
`FORK-NOTES.md`; how it is drawn belongs to esp32-ui-drawing.

Measured by its contributor on his boards:

- -7B: panel 32.7 Hz, 0 fills dropped, 0 desyncs; the moving layer at about 15 fps; the background a whole
  frame every 1.9 s; internal RAM left about 8 KB, 3 KB at the least before stacks went to PSRAM, then 45 KB
  free.
- -7: 22 to 23 fps on the demo sky, no late rows, 2.4 MB of PSRAM heap free.
- The README gives the sweep's rate as the panel's own: about 34 frames a second on the -7, 30 on the -7B.

Traps met there:

- **A build folder made before `sdkconfig.defaults.wave7` existed** kept the old settings, ran out of internal
  RAM and hung on a dark screen after the WiFi started. lcd_wave7.c now refuses to compile with them
  (`#error` on `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, `CONFIG_MBEDTLS_HARDWARE_AES`,
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`). Delete the folder's sdkconfig and build again.
- **The -7B's screen sometimes goes dim and does not come back** (seen twice). Sending the backlight again
  every 10 s does not help. The 10 s log says whether the expander took the write, its lines (backlight,
  panel reset, panel supply) and the battery input's voltage (4.16 to 4.49 V with no battery). If the lines
  stay right while it dims, suspect the power path: the charger, or the USB port under a 7" backlight. Open.
- **Hardware AES with TLS buffers in PSRAM** takes a DMA buffer from internal RAM, which the 1024-wide bounce
  buffers leave short: "esp-aes: Failed to allocate memory". These builds use AES in software.
- The -7's RS-485 transceiver runs from 5 V and drives one GPIO to 5 V while it receives; the library holds
  the transmit line low to stop that (its "errata 21", measured by its contributor). Board file has it.

## Settings this bus needs

`sdkconfig.defaults.s3rgb`, read after the S3 LCD boards' file [built; it runs on the boards above that ran]:

```
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_LCD_RGB_ISR_IRAM_SAFE=y
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set
```

- PSRAM at full speed and the code in it: ESP-IDF's RGB notes ask for that; it leaves the flash alone while a
  frame goes out and keeps the cache on while tiles and settings are written.
- `RESTART_IN_VSYNC`: when a frame cannot keep up, the picture finds its place again at the next VSYNC.
- TLS and the WiFi's buffers in PSRAM: on the first board that ran this the second core's strip found no
  room.

`sdkconfig.defaults.wave7` on top, for the 7 and 7B: run-time stats on (the library reads each core's idle
share); `CONFIG_SPIRAM_USE_MALLOC` with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024` (plain malloc of 1 KB or
more may go to PSRAM; what must be internal asks by name); `CONFIG_MBEDTLS_HARDWARE_AES=n`;
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` (big state no interrupt touches goes to PSRAM, about 65 KB of
internal RAM back); core dump to flash; the task watchdog does not watch core 0's idle task (a TLS handshake
beside a page being drawn left it no turn, which is no hang). Flash and partitions are esp32-flash-and-settings'
subject.

## Still open on this bus

- Whether WiFi and flash writes (the map's tiles, the settings) shake the picture on the plain esp_lcd path:
  it shows only on a real board. On the -7B the late rows measured as CPU-bound, not bus-bound, and XIP keeps
  flash writes from stopping the fill.
- The other 800x480 Sunton boards (ESP32-8048S043, S050, S070) may take the JC8048W550 build; not tried, pins
  not checked.
