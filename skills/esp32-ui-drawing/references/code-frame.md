# The frame on the board, complete

Everything that turns `ui_frame` and `ui_strip` into a picture on glass. ESP-IDF 5.5, `esp_lcd`
SPI panel IO for 8 bit panels, plain `spi_master` for the ILI9486 on shift registers. All of it runs
on hardware unless a line says otherwise.

## The interface a firmware gives the screen code (firmware/main/display.h, Flight Radar)

```c
/* display - one face for every screen. main.c draws a frame strip by strip and hands
 * each strip over, the driver sends it (LCD) or collects it for the next refresh (e-paper).
 * only one driver file is compiled in, see main/CMakeLists.txt */
#pragma once
#include <stdint.h>
#include "gfx.h"
#include "board.h"

void     disp_init(void);
void     disp_backlight(int percent);     // 0..100, does nothing where there is no backlight
// a strip of the frame, top to bottom. last strip of a frame may be shorter
void     disp_strip(const canvas_t *cv);
// frame done. e-paper refreshes here (seconds), LCDs have nothing to do
void     disp_frame_end(void);
// how often a new frame is worth drawing, ms. 0 = as fast as it goes
uint32_t disp_frame_ms(void);
// e-paper: make the next refresh a full one (new screen). LCDs ignore it
void     disp_full_next(void);
// strips that never got the done callback (LCD), refreshes that timed out (e-paper)
uint32_t disp_lost(void);
/* SPI LCDs only (lcd_spi.c): screen start-up profiles. one on most boards, the 2.8" CYD has
 * three (board.h LCD_PROFILES). disp_profile before disp_init picks, after it restarts the screen */
int         disp_profiles(void);
void        disp_profile(int p);
const char *disp_profile_name(void);
// LCDs only: the picture turned 180 degrees, for a screen mounted upside down
void        disp_flip(int on);
/* SPI LCDs only: wait till the strip in flight is out. the 3.5" CYD's touch shares the screen's
 * bus and asks for it only then, never against a strip on its way */
void        disp_wait_idle(void);
/* DISP_HELPER boards (lcd_spi.c): the second core packs its strip under the next one
 * (disp_pack_under), then both go in one transfer (disp_strip2 with its rows); the DMA buffers
 * sized for that (rows = 2 * DISP_STRIP) or back (DISP_STRIP), 0: no memory */
void        disp_pack_under(const canvas_t *under);
void        disp_strip2(const canvas_t *cv, int under_rows);
int         disp_rows(int rows);
```

## Touch read between the strips (firmware/main/main.c, Flight Radar)

```c
typedef struct { uint8_t down; int16_t x, y, rx, ry; uint32_t t; } tsample_t;
static tsample_t s_ts[24];
static int       s_nts;
static uint32_t  s_ts_last;
static uint32_t  s_touch_us, s_send_us;   // reading the touch and handing strips over, for the 10 s log

static void touch_sample(int force)
{
    uint32_t now = now_ms();
    if (s_nts >= (int)(sizeof s_ts / sizeof s_ts[0]) || (!force && now - s_ts_last < 15u)) return;
    int rx = 0, ry = 0, x = 0, y = 0;
    uint32_t t0 = now_us();
    int down = tp_read(&rx, &ry);
    s_touch_us += now_us() - t0;
    if (down) touch_apply(&s_ui.cal, rx, ry, &x, &y);
    s_ts[s_nts++] = (tsample_t){ (uint8_t)down, (int16_t)x, (int16_t)y, (int16_t)rx, (int16_t)ry, now };
    s_ts_last = now;
}
```

## The second core on the 3.5" CYD (main.c, Flight Radar)

Takes its memory only for the look that needs it, and only with 48 KB left for TLS.

```c
/* --- the second core: on the 3.5" in the radar look it draws every other strip while this core
 * draws the one above it, and the two go out in one transfer. its strip, its stack and the DMA
 * buffers' second half are taken only for that look: the square map wants the memory, and there
 * the SPI is what a frame waits for anyway --- */
#if DISP_HELPER
#define HELPER_STACK 7168
static TaskHandle_t      s_helper;
static SemaphoreHandle_t s_help_go, s_help_done;
static canvas_t          s_help_cv;            // the strip it draws now
static volatile int      s_help_stop;
static uint32_t          s_help_wait_us;       // this core waiting for it, for the 10 s log

static void helper_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_help_go, portMAX_DELAY);
        if (s_help_stop) break;
        ui_strip(&s_ui, &s_help_cv);                   // the strips only read what ui_frame set up
        disp_pack_under(&s_help_cv);                   // into the transfer, under this core's strip
        xSemaphoreGive(s_help_done);
    }
    xSemaphoreGive(s_help_done);
    vTaskDelete(NULL);
}

// between frames: on when wanted and the memory is there (with room left for TLS), off otherwise
static void helper_set(int on)
{
    if (on == (s_helper != NULL)) return;
    const size_t px = GFX_W * DISP_STRIP * sizeof(uint32_t), dma = GFX_W * DISP_STRIP * sizeof(uint16_t) * 2;
    if (!on) {
        s_help_stop = 1;
        xSemaphoreGive(s_help_go);
        xSemaphoreTake(s_help_done, portMAX_DELAY);   // out of its loop, it deletes itself
        s_helper = NULL;
        heap_caps_free(s_help_cv.px);
        s_help_cv.px = NULL;
        disp_rows(DISP_STRIP);
        ESP_LOGI(TAG, "the second core stops drawing, heap %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
        return;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < px + dma + HELPER_STACK + 48 * 1024) return;
    if (!s_help_go) {
        s_help_go = xSemaphoreCreateBinary();
        s_help_done = xSemaphoreCreateBinary();
    }
    s_help_cv = (canvas_t){ heap_caps_malloc(px, MALLOC_CAP_INTERNAL), 0, DISP_STRIP, 1 };
    if (!s_help_cv.px) return;
    s_help_stop = 0;
    if (!disp_rows(2 * DISP_STRIP) ||
        xTaskCreatePinnedToCore(helper_task, "strips", HELPER_STACK, NULL, 4, &s_helper, 0) != pdPASS) {
        s_helper = NULL;
        disp_rows(DISP_STRIP);
        heap_caps_free(s_help_cv.px);
        s_help_cv.px = NULL;
        return;
    }
    ESP_LOGI(TAG, "the second core draws every other strip, heap %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
}
#endif
```

## The frame itself (main.c, Flight Radar, the end of ui_task)

```c
#if DISP_HELPER
        helper_set(s_ui.view == VIEW_MAIN && s_ui.cfg.look == LOOK_RADAR);
        const int step = s_helper ? 2 * DISP_STRIP : DISP_STRIP;
#else
        const int step = DISP_STRIP;
#endif
        int64_t t0 = esp_timer_get_time();
        ui_frame(&s_ui, now);
        for (int sy0 = 0; sy0 < GFX_H; sy0 += step) {
            canvas_t cv = { work, sy0, sy0 + DISP_STRIP <= GFX_H ? DISP_STRIP : GFX_H - sy0, 0 };
#if DISP_HELPER
            const int y1 = sy0 + DISP_STRIP, two = s_helper && y1 < GFX_H;
            if (two) {                                 // the strip under it, on the other core meanwhile
                s_help_cv.y0 = y1;
                s_help_cv.h = y1 + DISP_STRIP <= GFX_H ? DISP_STRIP : GFX_H - y1;
                xSemaphoreGive(s_help_go);
            }
            ui_strip(&s_ui, &cv);
            if (two) {
                uint32_t tw = now_us();
                xSemaphoreTake(s_help_done, portMAX_DELAY);
                s_help_wait_us += now_us() - tw;
            }
            uint32_t ts = now_us();
            if (two) disp_strip2(&cv, s_help_cv.h);    // both in one transfer
            else disp_strip(&cv);
#else
            ui_strip(&s_ui, &cv);
            uint32_t ts = now_us();
            disp_strip(&cv);                           // packs it to 565, waits for the strip before to be out
#endif
            s_send_us += now_us() - ts;
            touch_sample(0);
        }
        disp_frame_end();
        busy_us += (uint32_t)(esp_timer_get_time() - t0);
        frames++;
        if (light < 100) disp_backlight(light += 10);

        if (now - t_stat >= 10000u) {
            uint32_t f = frames ? frames : 1;
            ESP_LOGI(TAG, "%u fps, %u ms a frame, view %d, %d planes, heap %u", (unsigned)(frames * 1000u / (now - t_stat)),
                     (unsigned)(busy_us / 1000u / f), s_ui.view, s_ui.snap.n, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
            const uint32_t *pr = s_ui.prof;                // where a frame's time goes, ms: what to make faster
            ESP_LOGI(TAG, "ms a frame: background %u, disc %u, compass %u, planes %u, panel %u, the rest %u, "
                          "sending %u, touch %u",
                     (unsigned)(pr[0] / 1000u / f), (unsigned)(pr[1] / 1000u / f), (unsigned)(pr[2] / 1000u / f),
                     (unsigned)(pr[3] / 1000u / f), (unsigned)(pr[4] / 1000u / f), (unsigned)(pr[5] / 1000u / f),
                     (unsigned)(s_send_us / 1000u / f), (unsigned)(s_touch_us / 1000u / f));
#if DISP_HELPER
            if (s_helper)                              // the layers above are this core's strips only
                ESP_LOGI(TAG, "the second core draws: waited for it %u ms a frame, its stack %u bytes free",
                         (unsigned)(s_help_wait_us / 1000u / f), (unsigned)uxTaskGetStackHighWaterMark(s_helper));
            s_help_wait_us = 0;
#endif
            memset(s_ui.prof, 0, sizeof s_ui.prof);
            if (now / 1000u - s_ran_kept >= 60u) {     // a reset from here on knows how long this run was
                s_ran_kept = now / 1000u;
                store_save_ran(s_ran_kept);
            }
            s_send_us = s_touch_us = 0;
            frames = 0;
            busy_us = 0;
            t_stat = now;
        }
        esp_task_wdt_reset();
        vTaskDelay(1);
    }
```

## Strips out by DMA (firmware/main/lcd_spi.c, Flight Radar)

The done callback and the wait:

```c
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
```

The buffers, the pack under the other core's strip, and one transfer for a pair:

```c
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
    if (!ok && !alloc_bufs(DISP_STRIP)) ESP_LOGE(TAG, "no DMA memory for the strips");
    xSemaphoreGive(s_done);
    return ok;
}
#endif
```

```c
void disp_strip(const canvas_t *cv) { disp_strip2(cv, 0); }

#if DISP_HELPER
/* the second core packs its strip itself, under the one this core draws, into the buffer the
 * next disp_strip2 sends. that buffer's last transfer was waited for before the one after it */
void disp_pack_under(const canvas_t *under)
{
    gfx_pack565(under, s_buf[s_k] + GFX_W * DISP_STRIP);
}
#endif

/* a strip, and the under_rows of the next one disp_pack_under put under it, in one transfer: the
 * SPI driver waits for the transfer before at the start of every one, so two strips sent apart
 * keep the sender waiting */
void disp_strip2(const canvas_t *cv, int under_rows)
{
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
```

Telling the screen again how it runs, every 5 s between frames. A 3.5" was found standing still after
about a day, the cause not known. A screen that lost its setup to a glitch comes back by itself.

```c
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
```

## Only what changed, and two painters (OWLOS-AIR-QUALITY-UI)

`firmware/main/display.h`:

```c
/* display - the ILI9486. main.c draws a frame strip by strip and hands each strip over,
 * only the columns that changed go to the glass (lcd_ili9486.c) */
#pragma once
#include <stdint.h>
#include "gfx.h"
#include "board.h"

void     disp_init(void);
void     disp_strip(const canvas_t *cv);  // a strip of the frame, top to bottom: pack and send

/* the same in two steps, for two cores: disp_pack on the core that drew the strip, disp_send on
 * the screen task in strip order */
#define DISP_COLS (GFX_W / 32)
typedef struct {
    uint16_t *px;                         // GFX_W x DISP_STRIP, from disp_buffer() (DMA capable)
    int      y0, h, any;
    uint8_t  changed[DISP_COLS];
} packed_t;
uint16_t *disp_buffer(void);
void      disp_pack(const canvas_t *cv, packed_t *p);
void      disp_send(packed_t *p);
void     disp_wait(void);                 // the strip on its way is out (the touch shares the bus)
void     disp_invalidate(void);           // the next frame goes out whole
uint32_t disp_sent_rows(void);            // rows that really went out, unchanged strips are skipped
```

`firmware/main/lcd_ili9486.c`, the sending part. The header comment explains why it pays: 20 MHz
through shift registers, a whole frame is 123 ms on the wire.

```c
/*
 * ILI9486 on Waveshare's RPi LCD (A), the panel of the OWLOS Air Quality board. the board turns
 * SPI into the panel's 16 bit bus with shift registers, so every command and parameter byte goes
 * out as a 16 bit word (real byte first, then 0x00) with its own CS pulse. pixels are 16 bit
 * anyway and go out in one burst. esp_lcd with 8 bit commands leaves this panel grey (ViewOwl
 * found that the hard way), so this is plain spi_master, the sequence of OWLOS WiFi Radar.
 *
 * 20 MHz is the most the shift registers take: a whole frame is 123 ms on the wire, 8 frames a
 * second at best. but a dashboard changes little from frame to frame, so every strip is cut in
 * columns, each column gets a checksum, and only the span from the first changed column to the
 * last one is sent. a still screen sends nothing at all. the dithering is ordered (gfx_pack565),
 * a pixel that did not change packs to the same bits, so the checksums are honest.
 * while one strip goes out by DMA the next one is drawn (two buffers, CS goes up in the
 * transfer's done callback)
 */
```

```c
#define COL_W   32                         // checksum column, px
#define COLS    (GFX_W / COL_W)
#define STRIPS  ((GFX_H + DISP_STRIP - 1) / DISP_STRIP)

static spi_device_handle_t s_spi;
static uint16_t *s_buf[2];
static int       s_busy;
static spi_transaction_t s_t;
static uint32_t  s_sum[STRIPS][COLS];
static uint32_t  s_rows, s_px;

uint32_t disp_sent_rows(void) { return s_px / GFX_W; }   // full rows' worth of pixels that went out
```

```c
static uint8_t s_ok[STRIPS];                   // this strip's checksums are from the glass

void disp_invalidate(void) { memset(s_ok, 0, sizeof s_ok); }

uint16_t *disp_buffer(void)
{
    return heap_caps_malloc(GFX_W * DISP_STRIP * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
}

/* any core: dither the strip to the panel's 565 and see which columns changed since it was last
 * packed. each strip has its own checksums, so two cores packing two strips never meet */
void disp_pack(const canvas_t *cv, packed_t *p)
{
    gfx_pack565(cv, p->px);
    p->y0 = cv->y0;
    p->h = cv->h;
    p->any = 0;
    const int si = cv->y0 / DISP_STRIP;
    for (int c = 0; c < COLS; c++) {
        uint32_t h = 2166136261u ^ (uint32_t)cv->h;
        for (int r = 0; r < cv->h; r++) {
            const uint32_t *q = (const uint32_t *)(p->px + r * GFX_W + c * COL_W);
            for (int i = 0; i < COL_W / 2; i++) h = (h ^ q[i]) * 16777619u;
        }
        p->changed[c] = !s_ok[si] || h != s_sum[si][c];
        p->any |= p->changed[c];
        s_sum[si][c] = h;
    }
    s_ok[si] = 1;
}

/* the screen task only, strips in any order: what changed goes to the glass. p->px is pulled
 * together in place, so it is used up; it is free again when the next disp_send returns */
void disp_send(packed_t *p)
{
    if (!p->any) return;                            // nothing changed in this strip
    /* the changed columns in runs: two HUDs side by side change on the left and on the right, the
     * gap between them did not. runs closer than 2 columns go out as one */
    int run0[4], run1[4], nr = 0;
    for (int c = 0; c < COLS; c++) {
        if (!p->changed[c]) continue;
        if (nr && c - run1[nr - 1] <= 2) run1[nr - 1] = c;
        else if (nr < 4) { run0[nr] = c; run1[nr] = c; nr++; }
        else run1[nr - 1] = c;
    }
    uint16_t *buf = p->px, *spare = s_buf[0];
    for (int k = 0; k < nr; k++) {
        const int x0 = run0[k] * COL_W, w = (run1[k] - run0[k] + 1) * COL_W;
        uint16_t *src;
        disp_wait();                                // nothing in flight: the spare and buf are free to change
        if (k < nr - 1 && spare) {                  // not the last run: copied out to the spare buffer
            for (int r = 0; r < p->h; r++) memcpy(spare + r * w, buf + r * GFX_W + x0, (size_t)w * 2);
            src = spare;
        } else {                                    // the last: pulled together in place
            if (w != GFX_W)
                for (int r = 0; r < p->h; r++) memmove(buf + r * w, buf + r * GFX_W + x0, (size_t)w * 2);
            src = buf;
        }
        window(x0, p->y0, x0 + w - 1, p->y0 + p->h - 1);
        gpio_set_level(PIN_LCD_DC, 0);
        tx16(0x2C);                                 // memory write
        gpio_set_level(PIN_LCD_DC, 1);
        memset(&s_t, 0, sizeof s_t);
        s_t.length = (size_t)w * p->h * 16;
        s_t.tx_buffer = src;
        gpio_set_level(PIN_LCD_CS, 0);              // the whole run in one CS low, cs_up() ends it
        if (spi_device_queue_trans(s_spi, &s_t, portMAX_DELAY) == ESP_OK) s_busy = 1;
        else gpio_set_level(PIN_LCD_CS, 1);
        s_px += (uint32_t)(w * p->h);
    }
    s_rows += (uint32_t)p->h;
}

void disp_strip(const canvas_t *cv)
{
    static packed_t p[2];
    static int cur;
    if (!p[0].px) { p[0].px = s_buf[1]; p[1].px = disp_buffer(); }
    if (!p[cur].px) return;
    disp_pack(cv, &p[cur]);
    disp_send(&p[cur]);
    cur ^= 1;
}
```

`firmware/main/main.c`, the painter task and how the strips interleave:

```c
/* the second painter. core 0 has the sensors once a second and is idle the rest of it, so it
 * draws every odd strip of a frame while core 1 draws the even ones; core 1 sends them all to the
 * glass in order. ui_strip only reads the screen's state, and that state only changes between
 * frames (ui_frame, ui_touch), when the painter is waiting */
static TaskHandle_t      s_painter;
static SemaphoreHandle_t s_painted;
static volatile int      s_paint_y;
static uint32_t         *s_p_canvas, *s_p_scratch;
static uint32_t          s_t_frame, s_t_draw, s_t_send, s_t_wait;   // where core 1's frame goes, us

static canvas_t strip_at(uint32_t *px, int y)
{
    return (canvas_t){ px, y, y + DISP_STRIP <= GFX_H ? DISP_STRIP : GFX_H - y };
}

/* each painter packs what it drew too (dithering and the column checksums are a third of the
 * work), into its own two packed buffers in turn: a strip's buffer is free again once the strip
 * after the next one was sent */
static packed_t s_pk0[2], s_pk1[2];
static int      s_pk0_i;

static void painter_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        canvas_t cv = strip_at(s_p_canvas, s_paint_y);
        ui_strip(&s_ui, &cv, s_p_scratch);
        disp_pack(&cv, &s_pk0[s_pk0_i]);
        xSemaphoreGive(s_painted);
    }
}

static void paint(int y)                             // core 0, strip at y: start it
{
    s_paint_y = y;
    xTaskNotifyGive(s_painter);
}
```

```c
        int64_t t0 = esp_timer_get_time();
        ui_frame(&s_ui, now);
        int64_t t1 = esp_timer_get_time();
        if (s_painter) {
            // even strips here, odd ones on core 0; each goes to the glass in order
            const int n = (GFX_H + DISP_STRIP - 1) / DISP_STRIP;
            int i1 = 0;
            s_pk0_i = 0;
            paint(DISP_STRIP);
            for (int k = 0; k < n; k += 2) {
                canvas_t cv = strip_at(work, k * DISP_STRIP);
                int64_t a = esp_timer_get_time();
                ui_strip(&s_ui, &cv, scratch);
                disp_pack(&cv, &s_pk1[i1]);
                int64_t b = esp_timer_get_time();
                disp_send(&s_pk1[i1]);
                i1 ^= 1;
                touch_sample(0);
                int64_t c = esp_timer_get_time();
                s_t_draw += (uint32_t)(b - a);
                s_t_send += (uint32_t)(c - b);
                if (k + 1 >= n) continue;
                xSemaphoreTake(s_painted, portMAX_DELAY);
                int64_t d = esp_timer_get_time();
                packed_t *pp = &s_pk0[s_pk0_i];
                s_pk0_i ^= 1;
                if (k + 3 < n) paint((k + 3) * DISP_STRIP);   // its canvas is packed: draw on while this one goes out
                disp_send(pp);
                s_t_wait += (uint32_t)(d - c);
                s_t_send += (uint32_t)(esp_timer_get_time() - d);
            }
            s_t_frame += (uint32_t)(t1 - t0);
        } else {
            for (int sy0 = 0; sy0 < GFX_H; sy0 += DISP_STRIP) {
                canvas_t cv = strip_at(work, sy0);
                ui_strip(&s_ui, &cv, scratch);
                disp_strip(&cv);
                touch_sample(0);
            }
        }
```

## Timing layers (core/ui.c, Flight Radar) and the 10 s log

```c
const char *ui_screen_name;
uint32_t (*ui_clock_us)(void);
static uint32_t s_lap;

// time since the last lap goes to prof[layer]. layer -1 only starts the clock. the second core's
// strips are not timed: one clock, one core
void ui_lap(ui_t *u, const canvas_t *cv, int layer)
{
    if (!ui_clock_us || cv->helper) return;
    uint32_t now = ui_clock_us();
    if (layer >= 0 && layer < (int)(sizeof u->prof / sizeof u->prof[0])) u->prof[layer] += now - s_lap;
    s_lap = now;
}
```

```c
        if (now - t_stat >= 10000u) {
            uint32_t f = frames ? frames : 1;
            ESP_LOGI(TAG, "%u fps, %u ms a frame, view %d, %d planes, heap %u", (unsigned)(frames * 1000u / (now - t_stat)),
                     (unsigned)(busy_us / 1000u / f), s_ui.view, s_ui.snap.n, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
            const uint32_t *pr = s_ui.prof;                // where a frame's time goes, ms: what to make faster
            ESP_LOGI(TAG, "ms a frame: background %u, disc %u, compass %u, planes %u, panel %u, the rest %u, "
                          "sending %u, touch %u",
                     (unsigned)(pr[0] / 1000u / f), (unsigned)(pr[1] / 1000u / f), (unsigned)(pr[2] / 1000u / f),
                     (unsigned)(pr[3] / 1000u / f), (unsigned)(pr[4] / 1000u / f), (unsigned)(pr[5] / 1000u / f),
                     (unsigned)(s_send_us / 1000u / f), (unsigned)(s_touch_us / 1000u / f));
#if DISP_HELPER
            if (s_helper)                              // the layers above are this core's strips only
                ESP_LOGI(TAG, "the second core draws: waited for it %u ms a frame, its stack %u bytes free",
                         (unsigned)(s_help_wait_us / 1000u / f), (unsigned)uxTaskGetStackHighWaterMark(s_helper));
            s_help_wait_us = 0;
#endif
            memset(s_ui.prof, 0, sizeof s_ui.prof);
            if (now / 1000u - s_ran_kept >= 60u) {     // a reset from here on knows how long this run was
                s_ran_kept = now / 1000u;
                store_save_ran(s_ran_kept);
            }
            s_send_us = s_touch_us = 0;
            frames = 0;
            busy_us = 0;
            t_stat = now;
        }
```

## A loop gated for e-paper (OWLOS-WIFI-RADAR firmware/main/main.c)

A new picture every `disp_frame_ms()`, or right after a key. LCDs return 0 and draw as fast as they can.

```c
        /* e-paper: a new picture only every disp_frame_ms(), or right after a key.
         * LCDs draw as fast as they can */
        uint32_t every = disp_frame_ms();
        if (every && drawn && !input && now - last_frame < every) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        last_frame = now;
        drawn = 1;

        int64_t t0 = esp_timer_get_time();
        ui_frame(&s_ui, now);
        if (s_ui.view != last_view) {                 // e-paper: a new screen gets a full refresh, not drawn on top
            disp_full_next();
            last_view = s_ui.view;
        }
        for (int sy = 0; sy < GFX_H; sy += DISP_STRIP) {
            canvas_t cv = { work, sy, sy + DISP_STRIP <= GFX_H ? DISP_STRIP : GFX_H - sy };   // last one may be short
            ui_strip(&s_ui, &cv);
            disp_strip(&cv);
            touch_sample(0);                          // every 15 ms or so, see touch_sample()
        }
        disp_frame_end();
        busy_us += (uint32_t)(esp_timer_get_time() - t0);
        frames++;
```

