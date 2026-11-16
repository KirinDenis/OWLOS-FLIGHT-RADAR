/* Copyright (c) 2026 Michael Caldwell-Waller (chipguyhere). MIT License, see LICENSE in the repository root. */
/*
 * the Guition JC8012P4A1C through chipguyhere's library (components/chipguy_jc8012), as a few C calls
 * for lcd_jc8012.c and touch.c. fork addition
 */
#include "board.h"
#if DISP_JC8012

#include "JC8012P4A1C_Board.h"
#include "chipguy_JC8012P4A1C_display.h"
#include "esp_log.h"

static const char *TAG = "jc8012";

static JC8012P4A1C_Board s_board;
static chipguy_JC8012P4A1C_display s_disp(s_board);

/* the panel up, with two framebuffers and no LVGL draw buffers: the firmware turns its strips into
 * the frames itself (lcd_jc8012.c). blocks: the scan list must hold one item per raster chunk and the
 * two framebuffer pieces round them. rotation 1 is the picture lying with its left edge at the top
 * of the standing glass, as lcd_jc8012.c turns it; the library only uses it for mapTouch */
extern "C" int jc_begin(int blocks)
{
    esp_log_level_set("gsl3680", ESP_LOG_WARN);       // keep the touch driver's bring-up INFO lines out
    s_disp.setDrawBuffers(false);
    s_disp.setFramebufferCount(2);
    s_disp.setScanBlocks((uint16_t)blocks);
    if (!s_disp.begin(1)) {
        ESP_LOGE(TAG, "the panel did not start, the library says why above");
        return 0;
    }
    s_disp.setBacklight(0);                            // main.c brings it up once the first frames are in
    if (!s_disp.touchAvailable())
        ESP_LOGW(TAG, "no touch: %s", gsl3680_touch::statusText(s_disp.getTouchDriver().status()));
    return 1;
}

extern "C" uint16_t *jc_fb(int idx) { return s_disp.getFramebuffer((uint8_t)idx); }
extern "C" void jc_show(int idx) { s_disp.setActiveFramebuffer((uint8_t)idx, false); }
extern "C" void jc_backlight(int percent) { s_disp.setBacklight(percent); }
extern "C" uint32_t jc_period_us(void) { return s_disp.framePeriodUs(); }
extern "C" uint32_t jc_frames(void) { return s_disp.getFrameCount(); }

extern "C" int jc_raster(int y0, int y1, chipguy_JC8012P4A1C_display::RasterFn fn, int core, int rows)
{
    s_disp.setRasterBuffers(4);                        // 4 of them: a chunk is made 3 chunks ahead, and the next one's buffer is free
    return s_disp.setRasterRows(y0, y1, fn, nullptr, core, rows);
}

extern "C" IRAM_ATTR uint16_t *jc_rbuf(int chunk) { return s_disp.rasterBuffer(chunk); }
extern "C" IRAM_ATTR uint32_t jc_read(void) { return s_disp.rasterChunksRead(); }

extern "C" IRAM_ATTR void jc_direct(int chunk, int on) { s_disp.setRasterDirect(chunk, on != 0); }
extern "C" uint32_t jc_src(int fb, int chunk, int *bit, int *fresh) { return s_disp.rasterSrc(fb, chunk, bit, fresh); }   // TEMP
extern "C" IRAM_ATTR uint32_t jc_late(void) { return s_disp.rasterLate(); }

extern "C" uint32_t jc_late_exact(void) { return s_disp._rLateExact; }
extern "C" uint32_t jc_merged(void) { return s_disp.rasterMerged(); }
extern "C" void jc_set_check(void (*f)(uint32_t, uint32_t)) { s_disp._rCheck = f; }   // TEMP
extern "C" void jc_log(uint32_t *late, uint32_t *dropped, uint32_t *max_us, uint32_t *chunk_us, uint32_t *underruns)
{
    *late = s_disp.rasterLate();
    *dropped = s_disp.rasterDropped();
    *max_us = s_disp.takeRasterMaxUs();
    *chunk_us = s_disp.rasterChunkPeriodUs();
    *underruns = s_disp.getUnderrunCount();
}

/* the touch, in the lying picture's pixels: the library's filtered point on the standing glass
 * (800 x 1280), turned as lcd_jc8012.c turns the picture in the setting's normal way, the camera at the
 * top (main.c turns the alignment for a flip) */
extern "C" int jc_touch(int *x, int *y)
{
    if (!s_disp.touchAvailable()) return 0;
    uint16_t px, py;
    if (!s_disp.getTouchDriver().getTouch(&px, &py)) return 0;
    *x = JC8012_NATIVE_HEIGHT - 1 - py;
    *y = px;
    return 1;
}

#endif
