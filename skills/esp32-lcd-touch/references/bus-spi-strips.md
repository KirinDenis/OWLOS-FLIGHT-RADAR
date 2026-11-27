# SPI panels: strips, DMA and the done gate

How a picture gets to an SPI panel that keeps its own frame memory (ILI9341, ST7789, ST7796, GC9A01) on
ESP-IDF 5.5 through `esp_lcd` panel IO, with no panel component. Source: OWLOS Flight Radar
`firmware/main/lcd_spi.c` (about 500 lines with the tables) and `core/gfx.c`. The start-up tables are in the
panel files: [ili9341](panels/ili9341.md), [st7789](panels/st7789.md), [st7796](panels/st7796.md),
[gc9a01](panels/gc9a01.md). The RPi LCD's ILI9486 takes raw SPI instead: [ili9486](panels/ili9486.md).

Sureness words as in [SKILL.md](../SKILL.md): measured, built, docs, reasoned.

## Why strips

A 320x240 frame in RGB565 is 153,600 bytes, and the biggest free block after WiFi on a board with no PSRAM
is about 110 KB. So the picture is never stored whole. The scene is drawn once per strip of rows into a
32-bit canvas (0x00RRGGBB), dithered to 565 into a DMA buffer, and sent while the next strip is drawn.

Strip heights now (`DISP_STRIP` in board.h), and why:

- 2.8" CYD and other 320 wide on the classic ESP32 or the S3: 24 rows, 10 strips, 15,360 bytes a DMA
  buffer, 30,720 the canvas.
- 3.5" ST7796, 480 wide, no PSRAM: 6 rows. It was 8 (WiFi Radar ran 12). Each row less is 3.8 KB less for
  the strip and its DMA buffers, and as much less for the second core. With the weather page added, 8 rows
  left the map's tile reader too little room; at 6 rows 153 of 153 maps came whole in a 20 minute cycle of
  looks and ranges. Cost: MAP 12 to 10 fps, CLASSIC 11 to 9 (more transfers; why each transfer costs that
  much is not understood) [measured].
- Round C3, GC9A01: 16 rows, 15 strips: the C3's heap is small, room for the map and TLS.
- NM-CYD-C5 and ZLX-ESP32-2 (ESP32-C5): 12 rows. At 24 the strips took 60 KB of the chip's own RAM and left
  none for the AES: with TLS in PSRAM the AES decrypts through a buffer of up to 1.6 KB of internal RAM, and
  every answer failed with "esp-aes: Failed to allocate memory" (a tester's log). 12 gave back 30 KB
  [measured on a tester's board for the failure, built for the fix].
- RPi LCD ILI9486: 8 rows, not run with the weather yet [built].
- E-paper: 8 rows: a 792 x 24 canvas in 32 bits was 76 KB in one piece, not there after WiFi even on the S3.

Everything drawn per strip runs strip-count times: work that does not depend on the row belongs once a
frame. Measured on the 3.5": trails walked in every strip cost 62 ms a frame, done once a frame 7 ms; a
grid 38 of 110 ms.

## The driver, lcd_spi.c without its tables

```c
#include "display.h"
#if DISP_LCD_SPI

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#if LCD_CHIP_GC9A01
#include "i2c_bus.h"
#endif

static const char *TAG = "lcd";

static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_buf[2];                      // fill one while DMA sends the other
static int s_k;                                 // which one is next
static int s_rows;                              // rows they hold: DISP_STRIP, or two strips (disp_rows)

/* careful: dont send a strip while the previous one is still in flight.
 * that sometimes hung for ever, core dump showed the screen task stuck in
 * spi_device_acquire_bus (light views draw faster than the 40 MHz bus).
 * so wait for the done callback first, ViewOwl does the same. */
static SemaphoreHandle_t s_done;
static volatile uint32_t s_lost;

static bool IRAM_ATTR on_strip_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    (void)io; (void)e; (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

uint32_t disp_lost(void) { return s_lost; }
uint32_t disp_frame_ms(void) { return 0; }
void disp_full_next(void) { }

void disp_wait_idle(void)
{
    if (!s_done) return;
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;
    xSemaphoreGive(s_done);
}

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[15];
    uint8_t wait_ms;
} init_cmd_t;

/* ... the INIT tables of the chip picked by board.h (panel files), and for the GC9A01 its
 * expander_power_on() (panels/gc9a01.md) ... */

static void cmd(uint8_t c, const uint8_t *d, size_t n)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, c, d, n));
}

void disp_backlight(int percent)
{
    if (PIN_LCD_BL < 0) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(1023 * percent / 100));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* screen profiles: one start-up for most boards, three for the CYD revisions (board.h) */
typedef struct { const char *name; const init_cmd_t *cmds; size_t n; } profile_t;
#define PROFILE(name, t) { name, t, sizeof t / sizeof t[0] }
static const profile_t PROFILE_LIST[] = {
#if LCD_PROFILES && LCD_CHIP_ILI9341
    PROFILE("ILI9341 V1", INIT),
    PROFILE("ILI9341 V2", INIT_V2),
    PROFILE("ST7789 V3", INIT_V3),
#elif LCD_PROFILES && LCD_CHIP_ST7789
    PROFILE("ST7789 RGB", INIT),
    PROFILE("ST7789 BGR", INIT_BGR),
#else
    PROFILE(CHIP, INIT),
#endif
};
#define NPROFILES ((int)(sizeof PROFILE_LIST / sizeof PROFILE_LIST[0]))
static int s_profile, s_ready, s_flip;

/* the screen turned 180 degrees: MX and MY of the profile's own MADCTL flipped. for a board
 * standing on its head; the touch alignment is turned with it in main.c */
static void send_madctl(void)
{
    const profile_t *p = &PROFILE_LIST[s_profile];
    for (size_t i = 0; i < p->n; i++)
        if (p->cmds[i].cmd == 0x36 && p->cmds[i].len) {
            uint8_t m = (uint8_t)(p->cmds[i].data[0] ^ (s_flip ? 0xC0 : 0x00));
            cmd(0x36, &m, 1);
        }
}

static void run_profile(void)
{
    const profile_t *p = &PROFILE_LIST[s_profile];
    for (size_t i = 0; i < p->n; i++) {
        cmd(p->cmds[i].cmd, p->cmds[i].len ? p->cmds[i].data : NULL, p->cmds[i].len);
        if (p->cmds[i].wait_ms) vTaskDelay(pdMS_TO_TICKS(p->cmds[i].wait_ms));
    }
    if (s_flip) send_madctl();
}

void disp_flip(int on)
{
    s_flip = on ? 1 : 0;
    if (!s_ready) return;
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;
    send_madctl();
    xSemaphoreGive(s_done);
}

int disp_profiles(void) { return NPROFILES; }
const char *disp_profile_name(void) { return PROFILE_LIST[s_profile].name; }

/* before disp_init: just remember it. after: start the screen again with it, between frames
 * (the strip still in flight is waited for first) */
void disp_profile(int p)
{
    s_profile = p >= 0 && p < NPROFILES ? p : 0;
    if (!s_ready) return;
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;
    run_profile();
    xSemaphoreGive(s_done);
    ESP_LOGI(TAG, "screen started as %s", PROFILE_LIST[s_profile].name);
}

/* every few seconds, between frames, the screen is told again how it runs: awake, on, its pixel
 * format, inversion, its way round. it has all of that already and nothing shows; a screen
 * that lost them to a glitch on the line comes back by itself instead of standing still (a
 * 3.5" was found standing still after about a day, the cause not known) */
static void remind(void)
{
    const profile_t *p = &PROFILE_LIST[s_profile];
    cmd(0x11, NULL, 0);                                /* sleep out: no effect on a screen awake */
    vTaskDelay(pdMS_TO_TICKS(5));                     /* the 5 ms a waking one asks for */
    for (size_t i = 0; i < p->n; i++) {
        uint8_t c = p->cmds[i].cmd;
        if (c == 0x3A || c == 0x20 || c == 0x21 || c == 0x13 || c == 0x29)
            cmd(c, p->cmds[i].len ? p->cmds[i].data : NULL, p->cmds[i].len);
    }
    send_madctl();
}

void disp_frame_end(void)
{
    static int64_t last;
    const int64_t now = esp_timer_get_time();
    if (!s_ready || now - last < 5000000) return;
    last = now;
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;   // the last strip out first
    remind();
    xSemaphoreGive(s_done);
}

// the two DMA buffers, strips of this many rows. 0 when there was no memory for them
static int alloc_bufs(int rows)
{
    for (int k = 0; k < 2; k++) {
        s_buf[k] = heap_caps_malloc(GFX_W * rows * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_buf[k]) {
            heap_caps_free(s_buf[0]);
            s_buf[0] = NULL;
            return 0;
        }
    }
    s_rows = rows;
    return 1;
}

#if DISP_HELPER
/* the second core draws: pairs of strips, the DMA buffers twice the rows. back to single strips
 * when it stops, the square map wants the memory. between frames only. 0: no memory, single */
int disp_rows(int rows)
{
    if (rows == s_rows) return 1;
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;   // the last strip out first
    for (int k = 0; k < 2; k++) {
        heap_caps_free(s_buf[k]);
        s_buf[k] = NULL;
    }
    int ok = alloc_bufs(rows);
    if (!ok && !alloc_bufs(DISP_STRIP)) {
        /* none at all: the fetch task took the memory meanwhile. s_rows said the old size, and the
         * next disp_rows(DISP_STRIP) took that for done and the next strip went to NULL. now
         * disp_strip2 asks again */
        s_rows = 0;
        ESP_LOGE(TAG, "no DMA memory for the strips");
    }
    xSemaphoreGive(s_done);
    return ok;
}
#endif

void disp_init(void)
{
    if (PIN_LCD_BL >= 0) {
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
    }
#if LCD_CHIP_GC9A01
    expander_power_on();
#endif
#ifdef PIN_SD_CS
    // an SD slot on the screen's bus: its chip select up for good, or a card in it answers too
    gpio_config_t sd = { .pin_bit_mask = 1ULL << PIN_SD_CS, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&sd);
    gpio_set_level(PIN_SD_CS, 1);
#endif

    const size_t strip_bytes = GFX_W * DISP_STRIP * sizeof(uint16_t);
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)strip_bytes * (DISP_HELPER && !DISP_HELPER_HALF ? 2 : 1) + 8,   // the second core's pairs go in one
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = LCD_SPI_MODE,                  /* board.h, 0 unless a board says: 3 on the 3.2" CYD */
        .trans_queue_depth = 4,
        .on_color_trans_done = on_strip_done,
    };
    s_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_done);                        /* nothing in flight yet */
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &s_io));

    if (!alloc_bufs(DISP_STRIP)) ESP_LOGE(TAG, "no DMA memory for the strips");

    run_profile();
    s_ready = 1;
    ESP_LOGI(TAG, "%s %dx%d, SPI %d MHz, %d-row strips", PROFILE_LIST[s_profile].name, GFX_W, GFX_H,
             LCD_SPI_HZ / 1000000, DISP_STRIP);
}

void disp_strip(const canvas_t *cv) { disp_strip2(cv, 0); }

#if DISP_HELPER
/* the second core packs its strip itself, under the one this core draws, into the buffer the
 * next disp_strip2 sends. that buffer's last transfer was waited for before the one after it */
void disp_pack_under(const canvas_t *under)
{
    if (!s_buf[s_k]) return;                           // no strips just now (disp_rows): nothing to pack into
    gfx_pack565(under, s_buf[s_k] + GFX_W * DISP_STRIP);
}
#endif

/* a strip, and the under_rows of the next one disp_pack_under put under it, in one transfer: the
 * SPI driver waits for the transfer before at the start of every one, so two strips sent apart
 * keep the sender waiting */
void disp_strip2(const canvas_t *cv, int under_rows)
{
    /* the strips' memory lost (disp_rows, or none at the start): asked for again here, single strips,
     * the second core is not drawing then. none yet: this strip is not shown, the next one asks again */
    if (!s_buf[0] && !alloc_bufs(DISP_STRIP)) return;
    uint16_t *buf = s_buf[s_k];
    s_k ^= 1;
    gfx_pack565(cv, buf);   // this buffer went out two strips ago and we waited for that, so its free
    const size_t rows = (size_t)(cv->h + under_rows);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;   // max 200 ms, then count it lost and go on
    const size_t bytes = GFX_W * rows * sizeof(uint16_t);
    if (cv->y0 == 0) {
        cmd(0x2A, (const uint8_t[]){ 0, 0, (GFX_W - 1) >> 8, (GFX_W - 1) & 255 }, 4);
        cmd(0x2B, (const uint8_t[]){ 0, 0, (GFX_H - 1) >> 8, (GFX_H - 1) & 255 }, 4);
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x2C, buf, bytes));
    } else {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x3C, buf, bytes));
    }
}

#endif
```

### What each part is for

- `max_transfer_sz`: esp_lcd asks the bus for its limit and cuts a bigger buffer into chunks with CS kept
  low; `on_color_trans_done` fires only after the last chunk [docs, esp_lcd_panel_io_spi.c]. Sized to the
  strip, a strip is one transaction. If the second core packs pairs of strips, size it for two.
- DMA buffers must be `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`: the SPI DMA reads them.
- RAMWR 0x2C opens the full window once a frame, RAMWRC 0x3C goes on from where the panel stopped. A window
  is two polling commands with 4 bytes each, and 40 strips would pay them 40 times. A lost strip heals by
  itself: the next frame sets the window again. ILI9341, ST7796 and GC9A01 take it [measured], ST7789
  [built].
- The 200 ms limit means a callback that never comes costs a frame, not the screen. `disp_lost()` goes in
  the periodic log: anything but 0 is a bug.
- `disp_rows` failing whole once left `s_rows` at the old size, and the next strip went to NULL. Now
  `s_rows` is 0 then and `disp_strip2` asks for single strips again [built, from a crash path found in
  review].

## The semaphore, and the hang it cures

The trap, [measured]: a strip sent while the one before is still in flight sometimes hung for ever. The core
dump showed the screen task stuck in `spi_device_acquire_bus`. Light views were the ones that did it: they
draw faster than the 40 MHz bus sends. esp_lcd's `tx_color` takes the bus with `portMAX_DELAY`, drains what
is in flight, sends the command by polling, then queues the DMA and returns [docs]. It should cope, and in
that race it did not. Why exactly is not known.

The cure: never enter esp_lcd while a strip is in flight. Wait for the done callback first, with a limit,
count what is lost, go on.

**The gate rule.** Anything else that talks on this bus takes the same gate, between strips or frames, and
gives it back:

```c
if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;   // the last strip out first
/* a command, a profile, the buffers freed and made again */
xSemaphoreGive(s_done);
```

`disp_wait_idle()` is that take and give with nothing between, for the touch on the same bus. It is safe
because the touch is read from the same task that sends the strips: nothing can start a strip between the
wait and the touch's transfer. Call it from another task and the race is back.

## Packing to 565

```c
CORE_HOT void gfx_pack565(const canvas_t *cv, uint16_t *out)
{
    /* NOTE: tried three lookup tables instead of this math, it was slower on the
     * board (10.8 ms a frame vs 9.7). dont go back to tables */
    static const CORE_DATA uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    for (int r = 0; r < cv->h; r++) {
        const uint32_t *row = cv->px + r * GFX_PW;
        const uint8_t *bm = bayer[(cv->y0 + r) & 3];
        uint16_t *o = out + r * GFX_PW;
        for (int x = 0; x < GFX_PW; x++) {
            uint32_t p = row[x];
            unsigned d = bm[x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3;
            unsigned gg = (((p >> 8) & 255) + (d >> 2)) >> 2;
            unsigned bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            uint16_t v = (uint16_t)((rr << 11) | (gg << 5) | bb);
            o[x] = RADAR_RGB_BUS ? v : (uint16_t)((v >> 8) | (v << 8));
        }
    }
}
```

- `CORE_HOT` is `IRAM_ATTR`, `CORE_DATA` is `DRAM_ATTR`: a frame runs this every strip and a flash cache miss
  costs microseconds.
- The byte swap is for SPI, which sends the high byte first. A frame that the chip reads out of memory (the
  RGB bus, MIPI-DSI) wants the pixel in its own byte order: `RADAR_RGB_BUS` is 1 on those builds and there is
  no swap. `GFX_PW` is the glass's width; on SPI boards it equals `GFX_W`.
- The dither is ordered, by screen position, so a pixel that did not change packs to the same bits: the Air
  Quality driver relies on that to skip unchanged columns ([ili9486](panels/ili9486.md)).

## What SPI speed each board got, and why

Time on the wire is bytes x 8 / clock.

- 320x240 at 40 MHz: 30.7 ms a frame, 32 fps at most. 480x320 at 40 MHz: 61 ms, 16 fps at most [measured: the
  3.5" square map is SPI-bound there, 81 ms a frame of which 61 on the wire; later 53 of 82 ms waiting on
  the bus].
- 240x240 at 80 MHz: 11.5 ms. 480x320 at 20 MHz: 123 ms, 8 fps at most. 480x320 in 18-bit pixels at 40 MHz:
  92 ms ([ili9488](panels/ili9488.md)).

SPI2's IOMUX pins [docs, soc/*/include/soc/spi_pins.h, v5.5]: classic ESP32 CLK 14, MOSI 13, MISO 12, CS 15.
C3 and C5 CLK 6, MOSI 7, MISO 2, CS 10. S3 CLK 12, MOSI 11, MISO 13, CS 10.

- Classic ESP32: IOMUX up to 80 MHz, the GPIO matrix up to 40.
- Other chips: the matrix behaves the same as IOMUX up to 80 MHz [docs, spi_master.rst].

Board by board (pins in the board files):

- **80 MHz, round C3** ([gc9a01 board](../../esp32-boards/references/boards/gc9a01.md)). Its pins are the C3's
  IOMUX pins for SPI2. Runs clean [measured]. board.h's note: snow on the screen means go to 40.
- **40 MHz, 2.8" and 3.5" CYD.** 80 MHz APB divided by 2. ViewOwl (an earlier firmware of ours on the same
  panels) asks for 50 and the divider ends up here anyway. These pins happen to be the classic ESP32's IOMUX
  set for SPI2, so the GPIO matrix is not what holds them back [docs]; 80 was not tried on them.
- **40 MHz through the matrix, CrowPanel Advance 2.8".** Its pins are not IOMUX pins. On the classic ESP32
  the matrix caps the clock at 40 MHz. For the S3 and later, ESP-IDF's text says the matrix behaves like
  IOMUX up to 80 MHz [docs], and Elecrow itself runs 80: 40 here is caution, not a wall. ES3C28P also 40, as
  LCDWIKI's and Bruce's setups do [built].
- **30 MHz, NM-CYD-C5.** Screen, touch and SD share one bus. The maker's demos run 20, one project saw
  stripes at 40 and runs 30, another runs 40. 30 asked; the driver takes the nearest it can make below,
  probably 26.7 [reasoned]. The boot log prints what was asked, not what came out.
- **40 MHz, ZLX-ESP32-2 (C5).** What the maker's factory test runs; the touch has its own pins there, only the
  SD shares the screen's bus [built].
- **20 MHz, RPi LCD.** The 74HC shift registers on that board top out there; the old firmware ran 10.
- FusionEdge (another firmware on some of these boards, through LovyanGFX) runs its SPI panels at 27 MHz
  write and 12 MHz read [docs, its lgfx_base.h].

## The second core

Two ways, picked by board.h (`DISP_HELPER`, `DISP_HELPER_HALF`), both driven from main.c:

- **Pairs** (the 3.5" and the boards with PSRAM): core 0 draws every other strip in a strip of its own while
  core 1 draws the one above it. Because esp_lcd waits for the last transfer at the start of each one, two
  strips sent apart keep the sender waiting, so the pair goes as one transfer: `disp_pack_under` packs the
  helper's strip into the second half of the next buffer, `disp_strip2` sends both. `max_transfer_sz` and the
  DMA buffers double (`disp_rows`, freed and made again under the gate, back to single when the look wants the
  heap). The helper starts only with its pixels, the DMA half, its stack and 48 KB left over for TLS: about
  87 KB on the 3.5", so after MAP or CLASSIC it often waits. Its stack: 4980 of 7168 bytes free [measured].
  On the 3.5" the pairs run in the radar and weather looks and their pages; its square maps stay on one core:
  they want the memory, and their frames wait for the SPI anyway.
- **Halves** (the 2.8" CYD, `DISP_HELPER_HALF`): the second core draws the lower rows of this core's own strip
  in this core's canvas, and the strip goes out as any other. No strip and no DMA memory of its own, only its
  7 KB stack. It starts 1.6 s after boot, before the WiFi, and never stops. The cut row moves a row a frame
  (6 to 18 of 24) towards whichever core waited less: the second core shares core 0 with the WiFi and the
  fetch. Measured at 200 km with 48 planes: RADAR 9 to 12 fps, WEATHER 12 to 17, MAP 16 to 19, CLASSIC 13 to
  15. Only about 1.3x, because core 0 also carries the WiFi and 165 KB of planes every few seconds.
- A PC rig drew every look and page whole, in strips and in halves cut at any row: 0 pixels differ on the
  2.8", 3.5", 5" and 4.3" layouts. That is the proof that every strip only reads [measured on the PC].
- History: on the 3.5" the radar look went 139 to 98 ms a frame over the work that added pairs and flash at QIO
  80 MHz. Two cores alone gave about 1.25x, each core's strip gets slower when both run. The sdkconfig note
  credits the flash alone with 124 to 99 ms; the two numbers overlap and were never taken apart. Either way the
  flash setting is the cheap one, try it first.

## Other things on the same bus

- **An SD slot** sees the screen's clock and data and may answer on MISO. Hold its chip select up for good
  before the bus starts (`PIN_SD_CS`, code above) [built: NM-CYD-C5, ZLX-ESP32-2]. On the ZLX-ESP32-2 the
  screen's bus has a MISO line, but only the SD card answers on it, so the screen's `PIN_LCD_MISO` is -1.
  The 2.8" CYD's SD and the CrowPanel Advance's are on pins of their own.
- **A touch chip** on the bus (XPT2046 of the 3.5" CYD, NM-CYD-C5, Air Quality): a second device at 2 MHz,
  `disp_wait_idle()` before each transfer ([xpt2046](touch/xpt2046.md)).
- **A build for one board on another** with the same chip can drive another board's lines: the NM-CYD-C5
  build on a ZLX-ESP32-2 holds that board's screen select (10) up as the NM-CYD-C5's SD select, so the screen
  stays dark, and drives the screen's DC on the ZLX's amplifier enable [reasoned from the two pin lists].
- **MISO on GPIO 12 of the 2.8" CYD.** It is a strapping pin, the flash voltage. Keep `PIN_LCD_MISO -1` where
  nothing reads. The 3.5" uses it because the touch has to.
