# The ESP32-P4 MIPI-DSI panels: the M5Stack Tab5 and the Guition JC8012P4A1C

How OWLOS Flight Radar drives two P4 boards whose panels stand (portrait) while the picture lies
(landscape). Sources: `firmware/main/lcd_dsi.c`, `touch.c`, `idf_component.yml`, `lcd_jc8012.c`,
`lcd_jc8012_panel.cpp`, chipguyhere's vendored library in `firmware/components/chipguy_jc8012`,
`sdkconfig.defaults.esp32p4` and `.jc8012`, `FORK-NOTES.md`, `README.md`.

Sureness words as in [SKILL.md](../SKILL.md). Neither board has run here: the Tab5 runs on its contributor's,
the JC8012 on its contributor's. Pins and the rest of each board: [tab5](../../esp32-boards/references/boards/tab5.md),
[jc8012](../../esp32-boards/references/boards/jc8012.md). The P4 chip itself:
[esp32-p4](../../esp32-boards/references/chips/esp32-p4.md).

## What is different on this bus

- As on the S3 RGB bus, the panel is fed from a whole frame in PSRAM all the time (DPI video mode); a strip is
  only put into that frame. Byte order is native, no swap (`RADAR_RGB_BUS 1`).
- The glass stands and the radar lies. A strip of the picture's rows becomes a column band of the frame:
  the picture's row y goes to the frame's column (width - 1 - y), its x down the frame.
- The P4 has a hardware block turner, the PPA (pixel processing accelerator, `esp_driver_ppa`). It reads the
  32-bit canvas as it is (ARGB8888), turns it, writes RGB565 into the frame, cache and all. That spares the
  packing and its dither: 40 ms a frame at 1280 wide on the Tab5.
- The P4's caches: a write into the frame by the CPU has to be written back before the DMA reads it
  (`esp_cache_msync`, C2M), and a range given to `esp_cache_msync` or written by the PPA wants the 128-byte
  cache line alignment (`CONFIG_CACHE_L2_CACHE_LINE_128B`, frames from `heap_caps_aligned_calloc`). `esp_lcd`'s
  `draw_bitmap` keeps the cache straight by itself.
- The P4 has no WiFi: an ESP32-C6 on the board does it over SDIO with ESP-Hosted (`esp_wifi_remote`). That is
  esp32-boards' subject; here only the order matters: the radio's power switch is on the Tab5's IO expander,
  so `disp_board_up()` runs from app_main before anything asks for the WiFi.

## The M5Stack Tab5, through Espressif's board package

Brought in by parispitts (pull request #7), who runs it on a Tab5 with the ST7121 panel, board V1.0: WiFi,
planes, the map and the touch work [measured, a contributor's board]. Not run here.

- 720x1280 IPS standing, 5". Three panel revisions are sold: ILI9881C with a GT911 touch, ST7123, ST7121 with
  its own touch ([st7121](panels/st7121.md)). Espressif's board package `espressif/m5stack_tab5` tells them apart
  at power on and brings up the one it finds, so the firmware names no panel or touch pins and the I2C bus is
  the package's. The ILI9881C and ST7123 ones not seen.
- No BOOT reaches the P4 (the power button is the charger chip's), no LED. `PIN_BUTTON -1`.
- The layout is drawn 9/4 as big (569x320 to 1280x720); the fonts are the 5" boards' 3/2 ones for now.
- `DISP_STRIP 12`, 60 strips: at 1280 wide the strip, its turned copy and the two cores' canvases share
  346 KB of RAM.

Components, fetched only for the P4 (`main/idf_component.yml`, each with `if: "target == esp32p4"`):

```yaml
dependencies:
  idf: ">=5.5"
  espressif/m5stack_tab5:
    version: "^1.3.2"
  espressif/esp_lcd_touch:       # the package reads the touch through it, touch.c asks it for the points
    version: "^1.2.1"            # the newest when the Tab5 was built; a 2.x may change the calls touch.c makes
  espressif/esp_lvgl_port:       # the Tab5's package wants it; 2.9.0 uses a DPI callback ESP-IDF 5.5.3 does not have
    version: "~2.8.0"            # (every P4 build stopped on it, the JC8012's too, which does not use it)
  espressif/esp_wifi_remote:
    version: "^1.6.5"            # 1.6.3 and older refuse an esp_hosted 3.x
  espressif/esp_hosted:
    version: "^3.1.0"            # knows the Tab5; the board's own C6 firmware (1.4.1) answers a 3.x host
```

The CMake adds `esp_driver_ppa` to REQUIRES for the P4 only. On the build PC here the Tab5 did not build (the
component manager stopped with a MissingKconfigError before compiling); CI builds it [built in CI].

### lcd_dsi.c

```c
#include "display.h"
#if DISP_LCD_DSI

#include <string.h>
#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "lcd";

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_line[2];     // a strip packed to 565 the picture's way, one a core (cv->helper): the hand path only
static uint16_t *s_col;         // the same turned by hand: DISP_STRIP wide, GFX_PW rows, when the PPA is not there
static uint16_t *s_fb;          // the frame, the panel's: 720 across, 1280 down
static ppa_client_handle_t s_ppa; // the P4's pixel processing accelerator: turns the strip into the frame
static int s_flip, s_up;

/* the package's I2C bus and the switches on the board's IO expanders. the radio's is thrown here
 * because this is the one file that knows the board: app_main calls it before the WiFi starts */
void disp_board_up(void)
{
    if (s_up) return;
    s_up = 1;
    esp_err_t e = bsp_i2c_init();
    if (e != ESP_OK) ESP_LOGE(TAG, "the board's I2C bus did not come up: %s", esp_err_to_name(e));
    e = bsp_feature_enable(BSP_FEATURE_WIFI, true);
    if (e != ESP_OK) ESP_LOGW(TAG, "the radio's power switch: %s", esp_err_to_name(e));
}

void disp_backlight(int percent)
{
    if (!s_panel) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    bsp_display_brightness_set(percent);
}

void disp_init(void)
{
    disp_board_up();
    const bsp_display_config_t cfg = {
        .dsi_bus = { .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT, .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS },
    };
    bsp_lcd_handles_t h = { 0 };
    /* a board the package does not know ends here: said in the log and the rest runs, the site can
     * still reach the board */
    esp_err_t e = bsp_display_new_with_handles(&cfg, &h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "no DSI panel: %s", esp_err_to_name(e));
        return;
    }
    s_panel = h.panel;
    s_line[0] = heap_caps_aligned_alloc(64, GFX_PW * DISP_STRIP * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_line[1] = heap_caps_aligned_alloc(64, GFX_PW * DISP_STRIP * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_col  = heap_caps_malloc(GFX_PW * DISP_STRIP * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_line[0] || !s_line[1] || !s_col) {
        ESP_LOGE(TAG, "no memory for a strip");
        s_panel = NULL;
        return;
    }
    /* the P4 has a block turner in hardware (the PPA): it reads the strip, turns it and writes it
     * into the frame, cache and all, while this core goes on. without it the strip is turned by
     * hand and the driver copies it in line by line, 1280 lines a strip */
    void *fb = NULL;
    ppa_client_config_t pc = { .oper_type = PPA_OPERATION_SRM };
    if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &fb) == ESP_OK && fb && ppa_register_client(&pc, &s_ppa) == ESP_OK) {
        s_fb = fb;
    } else {
        s_ppa = NULL;
        ESP_LOGW(TAG, "no PPA: the strips are turned by hand");
    }
    esp_lcd_panel_disp_on_off(s_panel, true);
    bsp_display_brightness_set(0);                     // main.c brings it up once the first frames are in
    ESP_LOGI(TAG, "MIPI-DSI panel, the glass %dx%d standing, the picture %dx%d lying, turned by %s", BSP_LCD_H_RES, BSP_LCD_V_RES,
             GFX_PW, GFX_PH, s_ppa ? "the PPA" : "hand");
}

void disp_flip(int on) { s_flip = on ? 1 : 0; }

/* into the frame, turned. the frame stands: BSP_LCD_H_RES (720) across, GFX_PW (1280) down. the
 * strip's h rows (the picture's rows cv->y0 .. cv->y0 + h - 1) become h columns of it, side by
 * side, and the driver takes them as one rectangle h wide and GFX_PW tall: in s_col each of its
 * GFX_PW rows holds the h pixels of one column, left to right. the picture's left edge goes to
 * the top of the glass; turned over (disp_flip) it goes the other way, as on every other board */
static void put(const canvas_t *cv)
{
    if (!s_panel) return;
    const int h = cv->h, k = cv->helper ? 1 : 0;
    if (s_ppa) {
        /* the PPA reads the canvas as it is, 0x00RRGGBB a pixel, and writes 565 into the frame: the
         * packing (and its dither) is spared, 40 ms a frame at this width. it turns counter-
         * clockwise: 270 puts the picture's left edge at the top of the glass, 90 the other way
         * round. the block it writes is h across and GFX_PW down, at column x0. blocking, a few ms a
         * frame: the core draws the next strip into this same canvas the moment this returns */
        ppa_srm_oper_config_t op = {
            .in = { .buffer = cv->px, .pic_w = GFX_PW, .pic_h = (uint32_t)h, .block_w = GFX_PW, .block_h = (uint32_t)h,
                    .block_offset_x = 0, .block_offset_y = 0, .srm_cm = PPA_SRM_COLOR_MODE_ARGB8888 },
            .out = { .buffer = s_fb, .buffer_size = (uint32_t)BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t),
                     .pic_w = BSP_LCD_H_RES, .pic_h = BSP_LCD_V_RES,
                     .block_offset_x = (uint32_t)(s_flip ? cv->y0 : BSP_LCD_H_RES - cv->y0 - h), .block_offset_y = 0,
                     .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
            .rotation_angle = s_flip ? PPA_SRM_ROTATION_ANGLE_90 : PPA_SRM_ROTATION_ANGLE_270,
            .scale_x = 1.0f, .scale_y = 1.0f,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };
        esp_err_t e = ppa_do_scale_rotate_mirror(s_ppa, &op);
        if (e == ESP_OK) return;
        ESP_LOGW(TAG, "the PPA refused a strip: %s, turning by hand from now on", esp_err_to_name(e));
        s_ppa = NULL;
    }
    uint16_t *line = s_line[k];
    gfx_pack565(cv, line);                             // h rows of GFX_PW pixels, the picture's way
    /* the picture's row y goes to the frame's column 719 - y and its x down the frame: the rows of
     * the strip fill columns 720 - y0 - h .. 719 - y0, the last row leftmost. turned over, row y
     * goes to column y and x up the frame. x outer: s_col is written in runs of h, s_line read
     * with a stride, the cheaper way round for a cache */
    int x0;
    if (!s_flip) {
        x0 = BSP_LCD_H_RES - cv->y0 - h;
        for (int x = 0; x < GFX_PW; x++) {
            uint16_t *to = s_col + x * h + h - 1;
            const uint16_t *from = line + x;
            for (int r = 0; r < h; r++, from += GFX_PW) *to-- = *from;
        }
    } else {
        x0 = cv->y0;
        for (int x = 0; x < GFX_PW; x++) {
            uint16_t *to = s_col + (GFX_PW - 1 - x) * h;
            const uint16_t *from = line + x;
            for (int r = 0; r < h; r++, from += GFX_PW) *to++ = *from;
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, x0, 0, x0 + h, GFX_PW, s_col);
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

### The Tab5's touch, through the package (`TOUCH_BSP`)

The package found the touch chip with the panel and reads it through `esp_lcd_touch`. It counts the standing
glass's pixels, 720 x 1280; the 5 cross alignment turns them ([touch-alignment](touch-alignment.md)).

Asking every 15 ms was one to three reads over I2C at 100 kHz through the package, 5 ms each with the
driver's queueing: 200 ms a frame with nobody touching [measured, a contributor's board]. So the chip's
interrupt line is watched, and it is asked only after the line spoke, or while a finger is on:

```c
#include "esp_lcd_touch.h"      // before the package's touch.h, which wants its type
#include "bsp/m5stack_tab5.h"
#include "bsp/touch.h"

static esp_lcd_touch_handle_t s_tp;
static volatile int s_tp_irq;      // the chip pulled its line: there is a report to read
static int s_tp_down;

static void IRAM_ATTR tp_irq(esp_lcd_touch_handle_t tp) { (void)tp; s_tp_irq = 1; }

void tp_init(void)
{
    esp_err_t e = bsp_touch_new(NULL, &s_tp);
    if (e != ESP_OK) {
        s_tp = NULL;
        ESP_LOGW(TAG, "no touch: %s, going on without", esp_err_to_name(e));
        return;
    }
    s_tp_irq = 1;                                     // once at the start, in case a finger is already there
    if (esp_lcd_touch_register_interrupt_callback(s_tp, tp_irq) != ESP_OK)
        ESP_LOGW(TAG, "no touch interrupt: the chip is asked every time, slower");
}

int tp_read(int *rx, int *ry)
{
    if (!s_tp) return 0;
    if (!s_tp_irq && !s_tp_down) return 0;
    s_tp_irq = 0;
    esp_lcd_touch_point_data_t p;
    uint8_t n = 0;
    esp_lcd_touch_read_data(s_tp);
    if (esp_lcd_touch_get_data(s_tp, &p, &n, 1) != ESP_OK || !n) {
        s_tp_down = 0;
        return 0;
    }
    s_tp_down = 1;
    *rx = p.x;
    *ry = p.y;
    return 1;
}
```

`TOUCH_RAW_SPAN` is `GFX_PW` there: the long way of the standing glass is the picture's width.

## The Guition JC8012P4A1C, through chipguyhere's library

Brought in by chipguyhere, who runs it on one with a v1.3 P4 [measured, a contributor's board; not run here].
The library (MIT, with Espressif's Apache-2.0 `esp_lcd_jd9365` panel driver) owns the panel, the backlight,
the touch and the I2C bus. 10.1", an 800x1280 JD9365 panel ([jd9365](panels/jd9365.md)) standing, the
picture lying as 1280x800; the layout drawn 5/2 as big (512x320, exactly 1280 wide).

The panel's bring-up in the library [docs, its source]:

- DSI: 2 data lanes at 1500 Mbps, PHY clock from PLL_F20M. DPI clock 60 MHz, RGB565. Porches H 20/20/40,
  V 4/8/20 (pulse, back, front).
- The D-PHY's power is the P4's on-chip LDO channel 3 at 2500 mV, acquired before the bus.
- The panel's reset is held low for the whole host-side bring-up (LDO, DSI bus, DBI IO, DPI panel), at least
  50 ms, then 200 ms to settle before the first command. A watchdog reset restarts the chip but never
  power-cycles the panel, and the board has no panel power switch: the reset line is the only lever.
- A failed bring-up holds reset 1000 ms and reboots, at most 3 times in a row, counted in `RTC_NOINIT` memory
  with a magic value (it holds garbage after power on), cleared on success. A dead panel then shows a serial
  log instead of a reboot loop.
- Reads the ID with 0x04 (for the log), sends 0xE0 0x00, MADCTL 0x36 0xC0 (MY|MX, the panel's base scan; the
  PPA does the turning), 0x3A 0x55, 0x80 0x01, then the vendor table, then `esp_lcd_panel_init` (video mode
  starts), and only then 0x11 sleep out (120 ms) and 0x29 display on.
- Both frames cleared and written back from the cache (`esp_cache_msync` C2M).
- The backlight is a GPIO that enables an MP3202 boost; off during init, brought up by main.c once frames are
  in.

How lcd_jc8012.c feeds it (chipguyhere's design, measured on his board):

- The background is drawn in strips of 80 picture rows, in PSRAM (no room in RAM, and the background has no
  deadline), and each strip is turned by the PPA into the hidden frame: 270 degrees, 90 when flipped. Tall
  strips because each PPA turn then writes 160 bytes a panel row; 8-row strips (16 bytes a row, 100 turns a
  frame) made the fill's fetches slow. A PPA turn measured at boot: about 12 ms at 16 rows, 27 at 80, 42 at
  128, then 190 ms at 160 and 925 ms for the whole picture: 80 rows is the cheapest a page (about 270 ms).
- The whole panel is the library's raster band: 80 chunks of 16 panel rows (16 picture columns), about 235 us
  of the panel's time each. Four bounce buffers; chunk c is made when chunk c - 3 has been read, so each fill
  has two chunk times. A chunk the moving layer does not touch is read by the panel's DMA straight from the
  frame (`setRasterDirect`, a change made for this firmware).
- Copying a chunk out of PSRAM by the CPU took about 400 us of its 235, and late chunks showed the chunk four
  before, 64 columns off ("slices moving sideways"). Reading 25.6 KB out of PSRAM takes about 180 us however
  it is done. ESP-IDF's async memcpy cost about 165 us a call (new link lists, the source written back from
  the cache every time); the AXI GDMA driven directly with lists made once costs about 14 us. So the GDMA
  fetches the next chunk's background straight into its bounce buffer while this one is composited, and two
  cores composite half a chunk each.
- XIP from PSRAM made the biggest single difference: filled chunks 150 to 103 us, a background frame 0.8 to
  5 s down to 0.41 s. Flash runs at 40 MHz on this board and shares the L2 cache with the PSRAM; XIP also
  keeps flash writes from stopping the fill. It costs about 11 KB of internal RAM.
- The beam's set-up called about 130 `tanf` and `fmodf` from flash and made the frame's first chunk 1.7 ms
  late every frame: now stepped in IRAM.
- Panel 52.0 Hz, 0 underruns; about 47 chunks a frame filled, 32 straight from the frame; internal RAM about
  93 KB free, 28 KB at the least (TLS).
- The planner uses the P4's PIE vector instructions from a task that can be preempted, which needs a fix to
  ESP-IDF 5.5's FreeRTOS port (espressif/esp-idf `51d5738ad769` on release/v5.5, in no 5.5.x release yet). On a
  stock 5.5.x set `OVL_PLANNER_PIE` to 0 in ovl.c, or thin 8-pixel slivers appear.
- A build folder whose sdkconfig predates `sdkconfig.defaults.jc8012` is refused at compile time
  (`#error` on `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`).

The touch is a GSL3680 that needs its firmware uploaded at every power-up: [gsl3680](touch/gsl3680.md).

BOOT (GPIO 35) is a real button on this board, pulled up.

## Settings these boards need

`sdkconfig.defaults.jc8012` (after the Tab5's `sdkconfig.defaults.esp32p4`), the display-related lines:

```
CONFIG_CACHE_L2_CACHE_LINE_128B=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
```

The ESP-Hosted, partition and console lines are esp32-flash-and-settings' and esp32-boards' subject.

## The moving layer, in one paragraph

On the Waveshare 7/7B and the JC8012 the planes, the sweep and the compass ring are not drawn into the frame:
they are laid over each chunk of the frame as the panel is fed, every panel frame, and the frame in PSRAM holds
only what stands still and is redrawn as time allows. It suits an S3 with an RGB panel (by rows) or a P4 with a
DPI or MIPI-DSI panel (by rows or columns). An SPI or QSPI panel keeps its own frame memory and has no feed to lay
anything onto, and the other ESP32 chips have no PIE. The full notes: `FORK-NOTES.md` in the public OWLOS
Flight Radar repo; the drawing side belongs to esp32-ui-drawing.
