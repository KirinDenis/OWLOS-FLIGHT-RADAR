/*
 * JD9365 MIPI-DSI display driver for the JC8012P4A1C 8" / 10.1" ESP32-P4 LCD.
 *
 * This is the Guition "JC8012P4A1C" board: an ESP32-P4 driving an 800x1280
 * JD9365 panel over 2-lane MIPI-DSI, with a GSL3680 capacitive touch panel on
 * I2C.  800x1280 is the panel's NATIVE (portrait) geometry.
 *
 * Designed to feed an LVGL display in DIRECT render mode: LVGL renders a full
 * frame into a draw buffer, and the ESP32-P4 PPA (Pixel Processing Accelerator)
 * rotates that buffer in hardware into one of two DSI framebuffers, then flips
 * the panel to show it.  Rotation is therefore free (no CPU cost, no per-pixel
 * software transform) and the same UI code runs in any of the four orientations.
 *
 * Unlike the companion chipguy_10inchP4_480display library, this driver renders
 * at the panel's full native resolution: there is NO scaling, only rotation.
 *
 *   rotation 0  -> portrait  (800x1280, native, PPA 0 deg)
 *   rotation 1  -> landscape (1280x800, picture turned 90 deg clockwise)
 *   rotation 2  -> portrait flipped  (800x1280, 180 deg)
 *   rotation 3  -> landscape flipped (1280x800, picture turned 90 deg CCW)
 *
 * Degrees (0/90/180/270) are accepted as aliases for the indices 0/1/2/3.
 *
 * Board wiring (fixed on the JC8012P4A1C board) — the pin map lives in
 * JC8012P4A1C_Board (chipguy_JC8012P4A1C_board); pins_config.h aliases it:
 *   LCD: MIPI-DSI (2 data lanes), RST GPIO27, backlight GPIO23 (active HIGH)
 *   Touch (GSL3680, I2C 0x40): RST GPIO22, INT GPIO21, on the board's SHARED
 *   I2C bus (GPIO7/8, also the codec, RTC, camera and headers), which the
 *   board object owns — so this driver takes a JC8012P4A1C_Board&.
 *
 * Copyright (c) 2025-2026 chipguyhere
 * MIT License
 */

#pragma once

#ifdef ARDUINO_ESP32P4_DEV

#ifndef BOARD_HAS_PSRAM
#error "This library requires PSRAM. Enable PSRAM in the Arduino IDE Tools menu."
#endif

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "driver/ppa.h"
#include <JC8012P4A1C_Board.h>   // shared I2C bus + pin map
#include "gsl3680_touch.h"
// pins_config.h is included by the .cpp only, so its short macro names
// (LCD_RST, TP_INT, ...) don't leak into sketches.

// Native panel geometry (portrait).  width()/height() reflect the active
// rotation; these are the unrotated physical dimensions.
#define JC8012_NATIVE_WIDTH   800
#define JC8012_NATIVE_HEIGHT  1280

// Normalize a rotation argument to an index 0..3.  Accepts either the index
// directly (0..3) or degrees (0/90/180/270) as aliases for 0/1/2/3.
static inline uint8_t jc8012_normalize_rotation(uint16_t rotation) {
    switch (rotation) {
        case 90:  return 1;
        case 180: return 2;
        case 270: return 3;
        default:  return (uint8_t)(rotation & 0x03);
    }
}

class chipguy_JC8012P4A1C_display {
public:
    // Takes the board that owns the shared I2C bus the touch controller is on.
    // begin() starts the board (idempotent) before bringing up touch.
    explicit chipguy_JC8012P4A1C_display(JC8012P4A1C_Board &board);

    // Bring up the MIPI-DSI panel, the GSL3680 touch, two DSI framebuffers,
    // two full-resolution LVGL draw buffers, and the PPA rotation client.
    // rotation: 0 = portrait (800x1280, native), 1 = landscape (1280x800),
    //           2 = portrait flipped, 3 = landscape flipped.  Degrees
    //           (0/90/180/270) are accepted as aliases for 0/1/2/3.
    // Returns false if any allocation or hardware step failed.
    bool begin(uint16_t rotation = 0);

    // Draw buffers LVGL renders into (two, for DIRECT render mode).  Sized to
    // the logical orientation: 800x1280 portrait, 1280x800 landscape.
    uint16_t *getDrawBuffer(uint8_t index);

    // The two native 800x1280 DSI framebuffers the panel scans out of.
    uint16_t *getFramebuffer(uint8_t index);

    // Choose which framebuffer the panel displays (used internally by flip()).
    void setActiveFramebuffer(uint8_t index, bool wait_for_vsync = true);

    // PPA-rotate the given draw buffer into the given framebuffer (using the
    // rotation chosen in begin(), no scaling), then show that framebuffer.
    // Runs asynchronously on a dedicated task; serializes on the previous flip.
    void flip(uint8_t draw_buf_index, uint8_t target_fb_index);

    // Block until the next frame boundary (vsync).
    bool waitVsync(uint32_t timeout_ms = 50);

    uint8_t getNumFramebuffers();

    // Logical dimensions for the current rotation (what LVGL renders at).
    int16_t width()  const { return _draw_w; }
    int16_t height() const { return _draw_h; }
    size_t  framebufferSize() const { return (size_t)_draw_w * _draw_h * sizeof(uint16_t); }

    // Native (unrotated) panel dimensions.
    int16_t nativeWidth()  const { return JC8012_NATIVE_WIDTH; }
    int16_t nativeHeight() const { return JC8012_NATIVE_HEIGHT; }

    // Current rotation (0..3).
    uint8_t rotation() const { return _rotation; }

    // Backlight brightness, 0..100 (active HIGH on this board).
    void setBacklight(int percentage);
    void setBacklight(bool on) { setBacklight(on ? 100 : 0); }

    // Panel low-power mode, for sleeping the board (e.g. light sleep until a
    // touch).  sleep() turns the backlight off, waits for any frame the PPA is
    // still writing, sends Display Off + Sleep In, and stops the video stream
    // at a frame boundary — so the chip can then light-sleep safely.  wake()
    // restarts the stream, sends Sleep Out + Display On and
    // restores the backlight to its level before sleep().  Touch keeps working
    // throughout.  Both are no-ops if already in that state.
    void sleep();
    void wake();
    bool isAsleep() const { return _asleep; }

    // ── Low-power sleep ─────────────────────────────────────────────────────
    // Each call: waits for any finger to lift, puts the panel to sleep
    // (sleep()), light-sleeps the chip, and on waking brings the display back
    // (wake(), ~170 ms) — the sketch carries on where it was, RAM and
    // framebuffers intact.  The BOOT button wakes all three.  Measured battery
    // current while asleep (Guition's cell, radio held off with
    // board.setRadioPower(false); floors vary ~6-10 mA between boards):
    //
    //   sleepUntilSwipe()   ~16 mA at 3 s checks (~24 mA at 1 s).  Touch is off
    //                       between checks; every checkMs it is restarted and
    //                       looked at for ~70 ms.  A finger RESTING on the
    //                       screen isn't seen (the controller calibrates it
    //                       away on restart), so the gesture is a swipe lasting
    //                       up to checkMs.
    //   sleepUntilTap()     ~45 mA.  Touch stays armed; any touch wakes at once.
    //   sleepUntilButton()  ~10 mA.  Touch off; only BOOT wakes.
    //
    // If the Wi-Fi chip is running (the default), add ~22 mA to each.
    enum WakeReason : uint8_t { WAKE_SWIPE, WAKE_TAP, WAKE_BUTTON };
    WakeReason sleepUntilSwipe(uint32_t checkMs = 3000);
    WakeReason sleepUntilTap();
    WakeReason sleepUntilButton();

    // Touch driver access (the GSL3680 reports in NATIVE panel coordinates).
    gsl3680_touch& getTouchDriver() { return _touch; }

    // False if the touch controller failed to initialize and begin() came up
    // with touch disabled (display still works; getTouch() reports no touches).
    bool touchAvailable() const { return _touch_ok; }

    // Map a raw NATIVE touch point (as returned by getTouchDriver().getTouch())
    // to LOGICAL coordinates for the active rotation, so a touch lines up with
    // what LVGL drew.  Returns false (and leaves lx/ly untouched) if the point
    // falls outside the logical area.
    bool mapTouch(uint16_t rawX, uint16_t rawY, int32_t &lx, int32_t &ly) const;

    // Debug counters from the DPI panel driver.
    uint32_t getFrameCount();
    uint32_t getUnderrunCount();

    // --- Without LVGL -----------------------------------------------------------
    // begin() normally allocates two full-screen LVGL draw buffers in PSRAM
    // (2 MB each) and a PPA client.  setDrawBuffers(false), BEFORE begin(),
    // skips them, for a sketch that draws into the framebuffers itself -- as
    // chipguy_JC8012P4A1C_terminal does.  flip() then does nothing.
    void setDrawBuffers(bool allocate) { _want_draw_buffers = allocate; }

    // How many framebuffers to allocate: 1, 2 (default) or 3.  Call BEFORE
    // begin().  A third lets a PPA-composed picture (the terminal in its PPA
    // modes) start on the next frame while the last one is still waiting to
    // be shown, at the cost of one more framebuffer of PSRAM.  One halves
    // the internal RAM the DMA lists take (setScanBlocks()), but LVGL and
    // the terminal need at least two.
    void setFramebufferCount(uint8_t n) { _num_fbs = n < 1 ? 1 : n > 3 ? 3 : n; }

    // --- Hardware scrolling (used by chipguy_JC8012P4A1C_terminal) -------------
    // Make each framebuffer `rows` rows tall instead of 1280, so a scrolling
    // ring can keep rows off screen.  Call BEFORE begin(); 0 means 1280.
    void setFramebufferRows(uint16_t rows) { _fb_rows = rows; }
    uint16_t framebufferRows() const { return _fb_rows ? _fb_rows : JC8012_NATIVE_HEIGHT; }

    // How the panel reads the active framebuffer each frame, in native rows: a
    // frozen top band (rows 0..top-1), `window` rows from a ring of `ring` rows
    // starting at row `top` (beginning `offset` rows in, wrapping), then the
    // frozen bottom band from row top+ring.  Takes effect at the next frame
    // boundary, so changing only `offset` scrolls without tearing.  Plain:
    // (0, 1280, 1280, 0).  Returns false if it doesn't fit the framebuffer.
    bool setScanLayout(uint16_t top, uint16_t ring, uint16_t window, uint16_t offset);

    // The most DMA blocks a frame may be read as (default 8).  Call BEFORE
    // begin().  The hardware has no limit; this sizes the DMA lists, at 64
    // bytes of internal RAM per block per framebuffer.  A raster band needs
    // one block per chunk plus one each for the framebuffer above and below.
    void setScanBlocks(uint16_t n) { _scan_blocks = n; }

    // EXPERIMENTAL: read each frame as an arbitrary list of blocks -- `bytes`
    // from `src`, in order, together exactly one 800 x 1280 frame (2,048,000
    // bytes).  A block may end mid-row, so for example each row can come half
    // from one buffer and half from another (2560 blocks).  Every src and
    // bytes must be a multiple of 8, and n at most setScanBlocks().  The array
    // is used in place: keep it valid, and call again after changing it.
    // Sources in PSRAM must be written back from the cache (esp_cache_msync)
    // first.  n = 0 returns to the framebuffer.  Not with a raster band.
    struct ScanBlock { const void *src; uint32_t bytes; };
    bool setScanList(const ScanBlock *blocks, uint32_t n);

    // --- Raster band: drawing in step with the panel's scan -------------------
    // Native panel rows y0..y1-1 are not read from the framebuffer.  Instead fn
    // draws them, chunkRows rows at a time, into one of two internal-RAM bounce
    // buffers (2 x chunkRows x 1600 bytes in all) just before the panel scans
    // them:
    //
    //     void fn(uint16_t* dst, int y, int rows, uint32_t frame, void* ctx)
    //         dst    rows x 800 RGB565 pixels, row y first
    //         y      first panel row to draw; rows: chunkRows
    //         frame  counts panel frames; every chunk of one frame has the same
    //                number, so a change means "first chunk of a new frame"
    //
    // Rows are the panel's NATIVE (portrait) rows whatever the rotation: the
    // PPA rotates only what LVGL draws.  fn runs in a high-priority task pinned
    // to `core` and has about one chunk's scan time to finish
    // (rasterChunkPeriodUs(): 14.7 us per row, so ~235 us for 16 rows); a chunk
    // that isn't ready in time counts in rasterLate() and shows whatever the
    // buffer held.  A task that falls a whole queue (8 chunks) behind skips
    // chunks (rasterDropped()), so fn must not rely on seeing every chunk --
    // for instance, on the last one of a frame to start the next.  Keep fn and
    // its data in internal RAM.  The rest of the screen is the framebuffer as
    // usual (LVGL).
    //
    // y0 and y1 are rounded outward to chunkRows multiples, and the band
    // must be an even number of chunks (y1 is extended, or y0 lowered, by one
    // chunk if not).  Each chunk is a DMA block, so the band needs
    // chunks + 2 of setScanBlocks().  Needs the plain scan layout.
    // fn = nullptr turns the band off.  Returns false if it doesn't fit.
    typedef void (*RasterFn)(uint16_t* dst, int y, int rows, uint32_t frame, void* ctx);
    bool setRasterRows(int y0, int y1, RasterFn fn, void* ctx = nullptr, int core = 0,
                       int chunkRows = 16);
    int rasterChunkRows() const { return _rRows; }
    // Fork addition (OWLOS Flight Radar): chunk `chunk` of the band (0 = its first) read straight
    // from the active framebuffer instead of its bounce buffer, or (false) from the buffer again.
    // Call it from fn for that chunk: fn then need not fill dst for a direct chunk. The chunk's turn
    // comes round all the same, so fn is still called for it every frame.
    void setRasterDirect(int chunk, bool direct);
    // Fork addition, TEMP diagnostic: chipguy_lcd_dpi_panel_raster_src
    uint32_t rasterSrc(int fb, int chunk, int *bit, int *fresh) const;
    // Fork addition: 2 to 8 bounce buffers instead of 2 (call before setRasterRows; N x chunkRows x 1600
    // bytes of internal RAM). fn is then called for chunk c when chunk c - (N - 1) has been read, so it
    // has N - 2 chunk times, and the buffer of chunk c + 1 is free too: rasterBuffer(c + 1) may be filled
    // ahead (by a DMA, say) while fn makes chunk c. The band is then a multiple of N chunks.
    void setRasterBuffers(int n) { _rNBuf = n < 2 ? 2 : n > 8 ? 8 : n; }
    uint16_t *rasterBuffer(int chunk) const { return chunk < 0 ? nullptr : _rBuf[chunk % _rNBuf]; }
    // Fork addition: how far the panel's DMA has got, as chunks read since setRasterRows (all frames
    // counted end to end: frame * chunks + chunk + 1). fn can tell from it how much time it has left.
    uint32_t rasterChunksRead() const { return _rRead; }
    // Fork addition: chunk interrupts that came merged with another, which the driver caught up on from
    // where the DMA is (counted, they had every later chunk of the frame drawn for the wrong place).
    uint32_t rasterMerged() const;
    // TEMP: called from the DMA interrupt as each chunk has been read (chunk, frame)
    void (*_rCheck)(uint32_t chunk, uint32_t frame) = nullptr;

    // Raster band instrumentation.
    uint32_t rasterChunksDrawn() const { return _rDrawn; }
    uint32_t rasterLate() const { return _rLate; }       // chunks not ready in time
    uint32_t rasterDropped() const { return _rDropped; } // chunks never drawn: the task was a queue behind
    uint32_t takeRasterMaxUs();                          // longest chunk (draw + cache write-back), then reset
    uint32_t rasterChunkPeriodUs() const;                // the time each chunk has
    uint32_t framePeriodUs() const;                      // measured panel frame time

private:
    JC8012P4A1C_Board &_board;   // declared before _touch: _touch uses its bus
    gsl3680_touch _touch;
    esp_lcd_panel_handle_t _panel_handle = nullptr;
    esp_lcd_panel_io_handle_t _io_handle = nullptr;
    int  _backlight_pct = 0;     // last level set, restored by wake()
    bool _asleep = false;
    WakeReason lightSleep(uint8_t mode, uint32_t checkMs);
    ppa_client_handle_t _ppa_client = nullptr;
    uint16_t *_draw_buffers[2] = {nullptr, nullptr};
    uint8_t  _rotation = 0;
    uint16_t _draw_w = JC8012_NATIVE_WIDTH;
    uint16_t _draw_h = JC8012_NATIVE_HEIGHT;
    bool     _touch_ok = false;
    bool     _want_draw_buffers = true;
    uint8_t  _num_fbs = 2;
    uint16_t _fb_rows = 0;
    uint16_t _scan_blocks = 0;

    // Raster band
    static bool _rasterDoneISR(uint32_t chunk, void *ctx);
    static void _rasterTaskEntry(void *arg);
    void _rasterDraw(uint32_t chunk, uint32_t frame);
    RasterFn _rFn = nullptr;
    void *_rCtx = nullptr;
    uint16_t *_rBuf[8] = {};
    int _rNBuf = 2;             // fork addition: bounce buffers, 2 to 8
    int _rY0 = 0;
    int _rRows = 16;            // chunk height
    int _rBufRows = 0;          // what _rBuf was allocated for
    uint32_t _rChunks = 0;
    QueueHandle_t _rQueue = nullptr;
    TaskHandle_t _rTask = nullptr;
    volatile uint32_t _rFrame = 0;       // frame number of the chunks being scanned now
    volatile uint32_t _rRead = 0;        // fork addition: chunks read so far
    volatile uint32_t _rDoneLin = 0;     // fork addition: 1 + the last drawn chunk, counted as _rRead counts
public:
    volatile uint32_t _rLateExact = 0;   // fork addition: chunks the DMA began before they were drawn (exact, unlike _rLate)
private:
    volatile uint32_t _rIssued = 0, _rCompleted = 0;
    volatile uint32_t _rDrawn = 0, _rLate = 0, _rDropped = 0;
    volatile uint32_t _rMaxCycles = 0;
    volatile uint32_t _rLastIsrCycles = 0, _rPeriodCycles = 0;

    bool initHardware();
    bool allocateDrawBuffers();

    // Panel reset line (LCD_RST).  The panel is held in reset across the whole
    // host-side bring-up rather than pulsed afterwards; see the PANEL_RESET_*
    // timing constants and their rationale in the .cpp.
    void assertPanelReset();
    void releasePanelFromReset();

    // Failure path for any bring-up step: backlight off, long reset assertion,
    // then reboot for a clean retry (bounded, so a dead panel can't reboot-loop
    // forever).  Always returns false, for `return panelInitFailed("...", ret)`.
    bool panelInitFailed(const char *step, esp_err_t err);

    // Async PPA task: rotates a draw buffer into a framebuffer and flips.
    static void ppaTaskFunc(void *arg);
    TaskHandle_t _ppa_task_handle = nullptr;
    SemaphoreHandle_t _ppa_done_sem = nullptr;
    struct {
        uint8_t draw_buf_index;
        uint8_t target_fb_index;
    } _ppa_work;
};

#endif // ARDUINO_ESP32P4_DEV
