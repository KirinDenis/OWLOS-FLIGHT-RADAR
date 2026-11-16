/* Copyright (c) 2026 Michael Caldwell-Waller (chipguyhere). MIT License, see LICENSE in the repository root. */
/*
 * the Guition JC8012P4A1C (chipguyhere). chipguyhere's library (lcd_jc8012_panel.cpp) feeds the
 * standing 800x1280 panel from one of two frames in the PSRAM; the picture lies, 1280x800.
 *
 * the background (everything but what moves) is drawn strip by strip as time allows, and each strip
 * is turned into the frame that does not show as it is handed over (the P4's PPA, run on the first core: disp_strip), so the frames are
 * always in the panel's own order: a row of the panel is a column of the picture. the frame is shown
 * from the next panel frame on once it is whole.
 *
 * what moves (ovl.c, by columns) goes over it as the panel is fed: the whole panel is the library's
 * raster band, in chunks of 16 rows (16 columns of the picture). a chunk the moving layer touches this
 * frame is copied out of the frame into a bounce buffer and the layer put on it; a chunk it does not
 * touch is read by the panel's DMA straight from the frame (a change made for this firmware to the library's driver),
 * so most of the screen costs the fill nothing.
 */
#include "display.h"
#if DISP_JC8012

#include <stdio.h>
#include <string.h>
#include "esp_private/gdma.h"
#include "esp_private/gdma_link.h"
#include "hal/gdma_ll.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ppa.h"
#include "ovl.h"
#include "busy.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#error "this build folder's sdkconfig predates sdkconfig.defaults.jc8012: delete the folder (or its sdkconfig) and build again"
#endif

static const char *TAG = "lcd";

#define PANEL_W 800                                    // the standing glass
#define PANEL_H 1280
#define CHUNK   16                                     // panel rows a raster chunk: 235 us of the panel's time (8 rows: the
                                                       // fixed cost a chunk took the most of its 117 us, measured)
#define NBUF     4                                     // bounce buffers (the library's): a chunk is made 3 chunks ahead
#define CHUNKS  (PANEL_H / CHUNK)
_Static_assert(GFX_PW == PANEL_H && GFX_PH == PANEL_W, "the picture lies on the standing glass");
_Static_assert(CHUNKS % NBUF == 0 && CHUNKS <= 256, "the library's band is a whole number of buffer rounds");

int       jc_begin(int blocks);
uint16_t *jc_fb(int idx);
void      jc_show(int idx);
void      jc_backlight(int percent);
uint32_t  jc_period_us(void);
uint32_t  jc_frames(void);
int       jc_raster(int y0, int y1, void (*fn)(uint16_t *, int, int, uint32_t, void *), int core, int rows);
void      jc_direct(int chunk, int on);
uint32_t  jc_src(int fb, int chunk, int *bit, int *fresh);   // TEMP
static volatile uint32_t s_dir_on[4], s_dir_off[4];  // TEMP: chunks 0-3 turned direct / back to the buffer, for the log
uint16_t *jc_rbuf(int chunk);
uint32_t  jc_read(void);
uint32_t  jc_late(void);
void      jc_log(uint32_t *late, uint32_t *dropped, uint32_t *max_us, uint32_t *chunk_us, uint32_t *underruns);

/* ovl.c's counters from the S3 library, here: no deadline for each line (the library counts the
 * chunks that are late), and the rows lost, from those */
volatile uint32_t g_ovlRowDeadline = 0;
volatile uint32_t g_ovlRowsSkipped;

static int                 s_ok, s_flip, s_band, s_layer;
static uint16_t           *s_fb[2];
static int                 s_draw = 1;                 // the frame the background is drawn into
static volatile int        s_twin;                     // (chipguyhere) the two frames alike: see disp_twin_copy
static volatile int        s_shown;                    // the frame the panel shows, as of the chunk being filled
static volatile int        s_want = -1;                // a whole background: shown from the next panel frame on
static volatile uint32_t   s_shown_at;                 // the library's frame count when it went up
static uint32_t            s_frame = 0xFFFFFFFFu;
static DRAM_ATTR uint8_t   s_direct[CHUNKS];           // the chunk is read from the frame: nothing goes over it
static volatile uint32_t   s_n_bounce, s_n_direct, s_copy_cyc, s_ovl_cyc;
/* the background's rows into the bounce buffer by the AXI GDMA: by the CPU, out of the PSRAM, a
 * chunk took ~400 us of its ~300 (measured 2026-11-13), and the late chunks showed the chunk from two
 * before, 32 columns off */
/* the AXI GDMA as a copier, its two link lists made once. ESP-IDF's async memcpy did the same, but
 * made its lists anew and wrote the source back from the cache on every call: ~165 us of a chunk's
 * ~235 (measured). the frames are never written by the CPU, so there is nothing to write back */
static gdma_channel_handle_t   s_dtx, s_drx;
static gdma_link_list_handle_t s_ltx, s_lrx;
static int                     s_mcp;                  // the copier is there
static volatile int            s_mcp_done;
static volatile int           *s_dma_flag;             // set when the copy in flight lands
static int                     s_mcp_failed;
/* the next chunk's background, fetched by the GDMA straight into that chunk's bounce buffer (the
 * library keeps four, so it is free) while this chunk is put together: reading a chunk out of the PSRAM
 * takes ~180 us however it is done (the CPU, the GDMA, or both at once: measured), the chunk has ~235,
 * and the moving layer wants ~150 of them. one after the other they did not fit. through a staging
 * buffer, its copy into the bounce buffer cost what the fetch saved */
static volatile int          s_pre_done;
static int                   s_pre_c = -1;             // the chunk being fetched, -1: none
static uint32_t              s_pre_frame;
static int                   s_pre_fb;                 // the frame it was fetched from
static const uint16_t       *s_pre_srcp;              // and exactly from where (a chunk composed beforehand, or the frame)
/* its layer words too, into one of two RAM buffers (ovl_chunk_words): a chunk's words are one piece,
 * the layer being kept by columns. a chunk with more than fit is read from the PSRAM as before */
#define WORDS_CAP 4096                                 // words a buffer: 16 KB
static uint32_t             *s_wbuf[2];
static const uint32_t       *s_pre_wat;               // where they came from, NULL: not fetched
static uint32_t              s_pre_wn;
static volatile uint32_t     s_n_words_local, s_n_words_over;
static volatile uint32_t     s_n_staged, s_wait_cyc, s_launch_cyc, s_sync_cyc;

/* the other core's half of each chunk: a task above everything else there, woken by the fill with the
 * chunk, which does the first half meanwhile and waits for it. one core was not enough: the moving layer
 * took ~185 us of a chunk's ~235 */
static TaskHandle_t       s_helper;
static uint16_t *volatile s_h_dst;
static volatile int       s_h_y, s_h_r0, s_h_r1;
static volatile uint32_t  s_h_frame;
static volatile int       s_h_done = 1;
static int                s_h_claim = 1;               // taken (by the helper, or by the fill when the helper is late)
static volatile uint32_t  s_h_stolen;
static TaskHandle_t          s_ppa_task;
static SemaphoreHandle_t     s_ppa_done;
static ppa_client_handle_t   s_ppa;
static ppa_srm_oper_config_t s_ppa_op;
static void ppa_task(void *arg);
/* TEMP: ground truth. each buffer stamped with the chunk (frame * CHUNKS + chunk) it holds once made;
 * checked as the DMA finishes reading a chunk from its buffer */
static volatile uint32_t s_tag[NBUF] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
static volatile uint32_t s_wrong, s_wrong_by[8], s_wrong_off[9];
static volatile uint32_t s_fill_cur = 0xFFFFFFFFu, s_fill_end = 0xFFFFFFFFu;   // TEMP: the chunk being filled, the last filled
static volatile uint32_t s_ev[8][5], s_nev;
static IRAM_ATTR void check_cb(uint32_t chunk, uint32_t frame)
{
    if (chunk >= CHUNKS || s_direct[chunk]) return;
    const uint32_t want = frame * CHUNKS + chunk, have = s_tag[chunk % NBUF];
    if (have == want) return;
    s_wrong++;
    s_wrong_by[chunk * 8 / CHUNKS]++;
    if (s_nev < 8) { volatile uint32_t *e = s_ev[s_nev++]; e[0] = want; e[1] = have; e[2] = s_fill_cur; e[3] = s_fill_end; e[4] = jc_read(); }
    const int32_t off = (int32_t)(want - have);          // how many chunks old the buffer was
    s_wrong_off[off < 0 ? 0 : off > 32 ? 8 : (off + 3) / 4]++;
}
static volatile uint32_t  s_shown_late;               // filled chunks done after the panel had begun reading them
static volatile uint32_t  s_n_pre;                     // filled chunks composed beforehand (copied whole)
static volatile uint32_t  s_cut_at;                    // the chunk being made, as the library counts chunks read

/* the moving layer's making (core 0) paced to the panel (ovl_build_pace): on at once while the panel and
 * the fill's next chunks are on chunks read straight from the frame, waiting a tick at a time while they
 * are on chunks being filled. a frame with no such place (the flat map, the layer all over) and it goes on */
static volatile uint32_t s_layer_wait_us;
static volatile int s_sc_on;                           // (chipguyhere) a page being scrolled (below): no layer to pace
/* (chipguyhere, 2026-11-14) the fill's runway is clear: no hot chunk (ovl_chunk_hot) among the next RUNWAY
 * chunks the panel reads. the fill composes chunk c while the panel reads c - 3 .. c - 1 and gets ahead in
 * the cheap stretch before a hot run (the user's point): PSRAM work there spends the slack the hot run needs.
 * so the horizon is the fill's lead and a run-up, not only what the panel is on */
#define RUNWAY (NBUF + 6)
static int runway_clear(void)
{
    const uint32_t r = jc_read();
    for (uint32_t k = 0; k < RUNWAY; k++)
        if (ovl_chunk_hot((r + k) / CHUNKS, (int)((r + k) % CHUNKS))) return 0;
    return 1;
}
static void layer_pace_for(int64_t cap_us)
{
    /* a page being scrolled: every chunk is gathered, none is quiet, and nothing moves to make room for
     * (it waited 25 ms a time, ~4.7 s in 10, and the ui task followed the finger twice a second) */
    if (!s_band || !s_layer || s_sc_on || cap_us <= 0) return;
    const int64_t t0 = esp_timer_get_time();
    /* (chipguyhere, 2026-11-14) quiet: none of the next chunks the panel reads is one the fill composes
     * itself heavily (ovl_chunk_hot, the planner's runners-up). before: all of them read straight from the
     * frame, which a busy sky never had, and every wait ran to its cap (a build waited up to 3.4 s). a hot
     * run lasts a few chunks of ~235 us: polled closely at first, then a tick at a time */
    for (;;) {
        const int quiet = runway_clear();
        const int64_t w = esp_timer_get_time() - t0;
        if (quiet || w > cap_us) break;
        if (w < 2000) esp_rom_delay_us(50);
        else vTaskDelay(1);
    }
    s_layer_wait_us += (uint32_t)(esp_timer_get_time() - t0);
}
static void layer_pace(void) { layer_pace_for(25000); }   // the background's turns (cpu_turn), as before
/* (chipguyhere, 2026-11-14) chunks the fill finished late or gave up, counted for good: the moving layer's
 * builds are paced only for a while after one (layer_pace_ovl) */
static volatile uint32_t s_late_mono;
static void layer_pace_ovl(void);                     // (chipguyhere) below: the same, timed for the ui's pass

/* for ovl.c's planner: the background's panel row py as the next panel frame will show it, and its
 * chunks out of the cache for the fill's GDMA */
static const uint16_t *next_row(int py)
{
    const int fb = s_want >= 0 ? s_want : s_shown;
    return s_fb[fb] ? s_fb[fb] + (size_t)py * PANEL_W : NULL;
}
static void written_back(const void *addr, size_t len) { esp_cache_msync((void *)addr, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M); }

// ovl_chunk_lines asks between lines: the panel has begun reading the chunk being made
static IRAM_ATTR int too_late(void) { return (int32_t)(jc_read() - s_cut_at) >= 0; }
static volatile uint32_t  s_h_timeouts, s_slow_h, s_slow_pre, s_slow_fill, s_slow_copy, s_gave_up;

static void helper_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (__atomic_exchange_n(&s_h_claim, 1, __ATOMIC_ACQ_REL)) continue;   // the fill did it itself
        /* this core's cache, both ways, over its half: what the GDMA put there, then what this core
         * wrote (the fill's write-back after the chunk is made runs on the other core) */
        uint16_t *half = s_h_dst + (size_t)s_h_r0 * PANEL_W;
        const size_t hb = (size_t)(s_h_r1 - s_h_r0) * PANEL_W * sizeof(uint16_t);
        esp_cache_msync(half, hb, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        ovl_chunk_lines(s_h_dst, s_h_y, s_h_r0, s_h_r1, s_h_frame, 1);
        esp_cache_msync(half, hb, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        s_h_done = 1;
    }
}

static IRAM_ATTR bool dma_eof(gdma_channel_handle_t ch, gdma_event_data_t *e, void *arg)
{
    (void)ch; (void)e; (void)arg;
    volatile int *f = s_dma_flag;
    s_dma_flag = NULL;
    if (f) *f = 1;
    return false;
}

static int dma_init(void)
{
    gdma_channel_alloc_config_t tc = { .flags.reserve_sibling = 1, .direction = GDMA_CHANNEL_DIRECTION_TX };
    if (gdma_new_axi_channel(&tc, &s_dtx) != ESP_OK) return 0;
    gdma_channel_alloc_config_t rc = { .direction = GDMA_CHANNEL_DIRECTION_RX, .sibling_chan = s_dtx };
    if (gdma_new_axi_channel(&rc, &s_drx) != ESP_OK) return 0;
    gdma_trigger_t trig = GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_M2M, 0);
    uint32_t mask = 0;
    gdma_get_free_m2m_trig_id_mask(s_dtx, &mask);
    if (!mask) return 0;
    trig.instance_id = __builtin_ctz(mask);
    if (gdma_connect(s_drx, trig) != ESP_OK || gdma_connect(s_dtx, trig) != ESP_OK) return 0;
    gdma_strategy_config_t st = { .owner_check = true, .auto_update_desc = true, .eof_till_data_popped = true };
    gdma_apply_strategy(s_dtx, &st);
    gdma_apply_strategy(s_drx, &st);
    gdma_transfer_config_t xc = { .max_data_burst_size = 128, .access_ext_mem = true };   // 64: 265 us a chunk, 128: 254, 16: 417 (measured, async memcpy)
    if (gdma_config_transfer(s_dtx, &xc) != ESP_OK || gdma_config_transfer(s_drx, &xc) != ESP_OK) return 0;
    gdma_rx_event_callbacks_t cb = { .on_recv_eof = dma_eof };
    if (gdma_register_rx_event_callbacks(s_drx, &cb, NULL) != ESP_OK) return 0;
    gdma_link_list_config_t lc = { .num_items = 16, .item_alignment = GDMA_LL_AXI_DESC_ALIGNMENT, .flags.check_owner = 1 };   // 25.6 KB + 16 KB in pieces of at most 4095 bytes
    if (gdma_new_link_list(&lc, &s_ltx) != ESP_OK || gdma_new_link_list(&lc, &s_lrx) != ESP_OK) return 0;
    return 1;
}

/* a chunk's rows from the frame (PSRAM) to dst (internal), and with them, when n2, n2 bytes from src2
 * to dst2 (its layer words); *done set when all has landed. one at a time: the caller waits for the one
 * before */
static IRAM_ATTR int dma_copy2(void *dst, const void *src, size_t n, void *dst2, const void *src2, size_t n2, volatile int *done)
{
    if (!s_mcp || ((uintptr_t)dst & 127) || ((uintptr_t)src & 127) || (n & 127)) return 0;
    if (n2 && (((uintptr_t)dst2 & 127) || ((uintptr_t)src2 & 127) || (n2 & 127))) n2 = 0;
    const int k = n2 ? 2 : 1;
    gdma_buffer_mount_config_t t[2] = {
        { .buffer = (void *)src, .buffer_alignment = 128, .length = n,
          .flags = { .mark_eof = k == 1, .mark_final = k == 1 ? GDMA_FINAL_LINK_TO_NULL : GDMA_FINAL_LINK_TO_DEFAULT } },
        { .buffer = (void *)src2, .buffer_alignment = 128, .length = n2,
          .flags = { .mark_eof = 1, .mark_final = GDMA_FINAL_LINK_TO_NULL } } };
    gdma_buffer_mount_config_t r[2] = {
        { .buffer = dst, .buffer_alignment = 128, .length = n,
          .flags = { .mark_eof = k == 1, .mark_final = k == 1 ? GDMA_FINAL_LINK_TO_NULL : GDMA_FINAL_LINK_TO_DEFAULT } },
        { .buffer = dst2, .buffer_alignment = 128, .length = n2,
          .flags = { .mark_eof = 1, .mark_final = GDMA_FINAL_LINK_TO_NULL } } };
    if (gdma_link_mount_buffers(s_ltx, 0, t, k, NULL) != ESP_OK || gdma_link_mount_buffers(s_lrx, 0, r, k, NULL) != ESP_OK) return 0;
    *done = 0;
    s_dma_flag = done;
    gdma_start(s_drx, gdma_link_get_head_addr(s_lrx));
    gdma_start(s_dtx, gdma_link_get_head_addr(s_ltx));
    return 1;
}

static IRAM_ATTR int dma_copy(void *dst, const void *src, size_t n, volatile int *done)
{
    return dma_copy2(dst, src, n, NULL, NULL, 0, done);
}

/* waits for a copy, and drops what the cache holds of dst: the DMA wrote round it */
static IRAM_ATTR int dma_wait(void *dst, size_t n, volatile int *done)
{
    const uint32_t t0 = esp_cpu_get_cycle_count();
    while (!*done) {
        if (esp_cpu_get_cycle_count() - t0 > 2000u * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) {   // 2 ms: lost. the CPU from now on
            s_dma_flag = NULL;
            gdma_reset(s_dtx);
            gdma_reset(s_drx);
            s_mcp = 0;
            s_mcp_failed++;
            return 0;
        }
    }
    esp_cache_msync(dst, n, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    return 1;
}

/* (chipguyhere) a page that scrolls, while a finger drags it: nothing is drawn. the list's whole height is
 * in the PSRAM (main.c draws it beforehand), turned and in 565 like the frame, each panel row's part of it in
 * one piece. every chunk is then gathered by the AXI GDMA straight into its bounce buffer, each of its 16
 * panel rows in three pieces: the frame's fixed start, the list from where it is scrolled to, the frame's
 * fixed end. measured at start (gather_test): right at any pixel, as fast as a plain chunk copy (~150 us
 * without the panel also reading the PSRAM, which it does not do then: it reads the bounce buffers). the
 * lists are made once and only their addresses written per chunk (mounting 48 pieces took 55 us). where it
 * is scrolled to is taken once a panel frame. the finger lifts: main.c draws the page there, and the gather
 * stops with the panel frame that shows it */
typedef struct { uint32_t dw0; void *buffer; void *next; } jc_lli_t;          // gdma_link.c's list item
typedef struct { uint32_t num_items; size_t item_size; uint8_t *items; uint8_t *items_nc; } jc_ll_t;   // and its list
static uint16_t           *s_sc_ct;                    // the list: per picture column, s_sc_hc pixels in panel order
static size_t              s_sc_cap;
static int                 s_sc_hc, s_sc_gy0, s_sc_gy1, s_sc_c0, s_sc_h, s_sc_bx0, s_sc_bx1;
static volatile int        s_sc_S, s_sc_off_flip;
static volatile uint32_t   s_sc_lat_frame = 0xFFFFFFFFu;
static volatile int        s_sc_lat_on, s_sc_lat_S;
static gdma_link_list_handle_t s_sc_tx, s_sc_rx[NBUF];
static uint16_t           *s_sc_rx_dst[NBUF];
static volatile uint32_t   s_sc_n, s_sc_late;
#define SC_TAG ((const uint16_t *)1)                   // s_pre_srcp of a gathered chunk

static inline jc_lli_t *sc_item(gdma_link_list_handle_t l, int i)
{
    const jc_ll_t *ll = (const jc_ll_t *)l;
    return (jc_lli_t *)(ll->items_nc + (size_t)i * ll->item_size);
}

// the scroll state for panel frame f, taken the first time it is asked for that frame
static IRAM_ATTR int sc_frame_on(uint32_t f, int *S)
{
    if (s_sc_lat_frame != f) {
        s_sc_lat_frame = f;
        s_sc_lat_on = s_sc_on;
        s_sc_lat_S = s_sc_S;
    }
    *S = s_sc_lat_S;
    return s_sc_lat_on;
}

static IRAM_ATTR gdma_link_list_handle_t sc_rx(uint16_t *dst)
{
    for (int i = 0; i < NBUF; i++) if (s_sc_rx_dst[i] == dst) return s_sc_rx[i];
    for (int i = 0; i < NBUF; i++)
        if (!s_sc_rx_dst[i] && s_sc_rx[i]) {
            gdma_buffer_mount_config_t rx = { .buffer = dst, .buffer_alignment = 128, .length = (size_t)CHUNK * PANEL_W * 2,
                                              .flags = { .mark_eof = 1, .mark_final = GDMA_FINAL_LINK_TO_NULL } };
            if (gdma_link_mount_buffers(s_sc_rx[i], 0, &rx, 1, NULL) != ESP_OK) return NULL;
            s_sc_rx_dst[i] = dst;
            return s_sc_rx[i];
        }
    return NULL;
}

// chunk n of the panel, scrolled to S, from frame fb, into dst: started, *done set when it lands
static IRAM_ATTR int sc_gather(uint16_t *dst, int n, int fb, int S, volatile int *done)
{
    gdma_link_list_handle_t rx = sc_rx(dst);
    if (!s_mcp || !rx || !s_sc_tx) return 0;
    if (S < 0) S = 0;
    if (S > s_sc_hc - s_sc_h) S = s_sc_hc - s_sc_h;
    const int off = s_flip ? S : s_sc_hc - S - s_sc_h;   // where in each column's list the panel row's piece starts
    for (int r = 0; r < CHUNK; r++) {
        const int py = n * CHUNK + r, x = s_flip ? PANEL_H - 1 - py : py;
        uint16_t *row = s_fb[fb] + (size_t)py * PANEL_W;
        const uint16_t *mid = x >= s_sc_bx0 && x < s_sc_bx1 ? row + s_sc_c0 : s_sc_ct + (size_t)x * s_sc_hc + off;
        jc_lli_t *a = sc_item(s_sc_tx, 3 * r), *b = sc_item(s_sc_tx, 3 * r + 1), *c = sc_item(s_sc_tx, 3 * r + 2);
        a->buffer = row;
        b->buffer = (void *)mid;
        c->buffer = row + s_sc_c0 + s_sc_h;
        a->dw0 |= 1u << 31;                            // the DMA's again
        b->dw0 |= 1u << 31;
        c->dw0 |= 1u << 31;
    }
    const jc_ll_t *rl = (const jc_ll_t *)rx;
    for (uint32_t i = 0; i < rl->num_items; i++) sc_item(rx, (int)i)->dw0 |= 1u << 31;
    __sync_synchronize();
    *done = 0;
    s_dma_flag = done;
    gdma_start(s_drx, gdma_link_get_head_addr(rx));
    gdma_start(s_dtx, gdma_link_get_head_addr(s_sc_tx));
    s_sc_n++;
    return 1;
}

/* main.c, the ui task: a buffer for the list, gy0 .. gy1 - 1 the viewport's glass rows, hc the list's
 * height in glass rows, bx0 .. bx1 - 1 glass columns that do not scroll. NULL: no room. only while not on */
uint16_t *disp_scroll_prepare(int gy0, int gy1, int hc, int bx0, int bx1)
{
    if (!s_ok || !s_mcp || s_sc_on || gy1 <= gy0 || hc < gy1 - gy0) return NULL;
    const size_t need = (size_t)GFX_PW * hc * sizeof(uint16_t);
    if (need > s_sc_cap) {
        heap_caps_free(s_sc_ct);
        s_sc_ct = heap_caps_aligned_alloc(128, need, MALLOC_CAP_SPIRAM);
        s_sc_cap = s_sc_ct ? need : 0;
        if (!s_sc_ct) return NULL;
    }
    if (!s_sc_tx) {
        gdma_link_list_config_t lc = { .num_items = 3 * CHUNK, .item_alignment = GDMA_LL_AXI_DESC_ALIGNMENT, .flags.check_owner = 1 };
        if (gdma_new_link_list(&lc, &s_sc_tx) != ESP_OK) { s_sc_tx = NULL; return NULL; }
        lc.num_items = 8;
        for (int i = 0; i < NBUF; i++) if (gdma_new_link_list(&lc, &s_sc_rx[i]) != ESP_OK) s_sc_rx[i] = NULL;
    }
    const int h = gy1 - gy0, c0 = s_flip ? gy0 : PANEL_W - gy1;
    if (h != s_sc_h || c0 != s_sc_c0) {                // the pieces' lengths: made once a viewport
        gdma_buffer_mount_config_t t[3 * CHUNK];
        for (int r = 0; r < CHUNK; r++) {
            uint16_t *row = s_fb[0] + (size_t)r * PANEL_W;
            t[3 * r + 0] = (gdma_buffer_mount_config_t){ .buffer = row, .buffer_alignment = 1, .length = (size_t)c0 * 2 };
            t[3 * r + 1] = (gdma_buffer_mount_config_t){ .buffer = row + c0, .buffer_alignment = 1, .length = (size_t)h * 2 };
            t[3 * r + 2] = (gdma_buffer_mount_config_t){ .buffer = row + c0 + h, .buffer_alignment = 1, .length = (size_t)(PANEL_W - c0 - h) * 2 };
        }
        t[3 * CHUNK - 1].flags.mark_eof = 1;
        t[3 * CHUNK - 1].flags.mark_final = GDMA_FINAL_LINK_TO_NULL;
        if (c0 <= 0 || c0 + h >= PANEL_W || gdma_link_mount_buffers(s_sc_tx, 0, t, 3 * CHUNK, NULL) != ESP_OK) { s_sc_h = 0; return NULL; }
    }
    s_sc_gy0 = gy0; s_sc_gy1 = gy1; s_sc_h = h; s_sc_c0 = c0; s_sc_hc = hc; s_sc_bx0 = bx0; s_sc_bx1 = bx1;
    return s_sc_ct;
}

// rows of a canvas into the list, the viewport's row gy0 being the list's row yc
void disp_scroll_put(const canvas_t *cv, int yc)
{
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    int a = cv->y0 > s_sc_gy0 ? cv->y0 : s_sc_gy0, b = cv->y0 + cv->h < s_sc_gy1 ? cv->y0 + cv->h : s_sc_gy1;
    if (yc + a - s_sc_gy0 < 0) a = s_sc_gy0 - yc;
    if (yc + b - s_sc_gy0 > s_sc_hc) b = s_sc_hc - yc + s_sc_gy0;
    /* a column at a time: in the list each column's rows lie together, so the writes go in a few cache lines
     * (a row at a time put every pixel in a line of its own: ~840 ms a list) */
    for (int x = 0; x < GFX_PW; x++) {
        uint16_t *col = s_sc_ct + (size_t)x * s_sc_hc;
        for (int y = a; y < b; y++) {
            const int l = yc + y - s_sc_gy0;           // the list's row
            const uint32_t p = cv->px[(size_t)(y - cv->y0) * GFX_PW + x];
            uint16_t *o = col + (s_flip ? l : s_sc_hc - 1 - l);
            const unsigned d = bayer[l & 3][x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3, gg = (((p >> 8) & 255) + (d >> 2)) >> 2, bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            *o = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
    }
}

void disp_scroll_done(void) { if (s_sc_ct) esp_cache_msync(s_sc_ct, (size_t)GFX_PW * s_sc_hc * sizeof(uint16_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M); }
void disp_scroll_to(int S) { s_sc_S = S; if (!s_sc_on && s_sc_h) { s_sc_off_flip = 0; s_sc_on = 1; } }
void disp_scroll_stop_at_flip(void) { if (s_sc_on) s_sc_off_flip = 1; }
int  disp_scroll_on(void) { return s_sc_on; }
void disp_scroll_stats(uint32_t *n, uint32_t *late) { *n = s_sc_n; *late = s_sc_late; s_sc_n = s_sc_late = 0; }
int  disp_copier_ok(void) { return s_mcp; }            // the GDMA copier still there (a copy lost for 2 ms drops it)

/* the next chunk's background on its way into its buffer (free: the library draws three ahead and
 * keeps four), if the moving layer goes over it too. past the frame's end, chunk 0 of the next frame
 * if it was filled this frame, from the frame that will be up then (checked again when it comes) */
static IRAM_ATTR void prefetch_next(int c, uint32_t frame)
{
    if (!s_mcp) return;
    if (s_dma_flag) dma_wait(jc_rbuf(s_pre_c), (size_t)CHUNK * PANEL_W * sizeof(uint16_t), &s_pre_done);   // one at a time
    s_pre_c = -1;
    int n = c + 1, fb = s_shown;
    uint32_t f = frame;
    {                                                  // (chipguyhere) a scrolling page: gathered
        const int nn = n < CHUNKS ? n : 0;
        const uint32_t ff = n < CHUNKS ? frame : frame + 1;
        int S;
        if (sc_frame_on(ff, &S)) {
            const int fbb = ff != frame && s_want >= 0 ? s_want : s_shown;
            if (sc_gather(jc_rbuf(nn), nn, fbb, S, &s_pre_done)) {
                s_pre_c = nn;
                s_pre_frame = ff;
                s_pre_fb = fbb;
                s_pre_srcp = SC_TAG;
                s_pre_wat = NULL;
            }
            return;
        }
    }
    const uint16_t *ps = NULL;                         // composed beforehand (ovl.c's planner): from there, whole
    if (n < CHUNKS) {
        if (!s_layer) return;
        ps = ovl_src(n * CHUNK, CHUNK, frame, NULL);
        if (!ps && !ovl_want(n * CHUNK, CHUNK, frame, NULL)) return;
    } else {
        if (s_direct[0] || !s_layer) return;
        n = 0;
        f = frame + 1;
        if (s_want >= 0) fb = s_want;
    }
    const uint32_t tl = esp_cpu_get_cycle_count();
    const uint32_t *wat = NULL;
    uint32_t wn = 0;
    if (!ps && s_wbuf[0] && f == frame && ovl_chunk_words(n * CHUNK, CHUNK, frame, &wat, &wn)) {
        if (wn > WORDS_CAP) { wat = NULL; s_n_words_over++; }
    } else {
        wat = NULL;
    }
    s_pre_wat = NULL;
    const uint16_t *from = ps ? ps : s_fb[fb] + (size_t)n * CHUNK * PANEL_W;
    if (dma_copy2(jc_rbuf(n), from, (size_t)CHUNK * PANEL_W * sizeof(uint16_t),
                  wat ? s_wbuf[n & 1] : NULL, wat, wat ? wn * 4 : 0, &s_pre_done)) {
        if (wat) { s_pre_wat = wat; s_pre_wn = wn; }
        s_pre_srcp = from;
        s_pre_c = n;
        s_pre_frame = f;
        s_pre_fb = fb;
    }
    s_launch_cyc += esp_cpu_get_cycle_count() - tl;
}

/* the library's raster function, for every chunk of every panel frame, three chunks ahead of the panel
 * (its task, top priority, the second core). dst: CHUNK rows of the panel, row y first */
static IRAM_ATTR void fill_(uint16_t *dst, int y, int rows, uint32_t frame, void *ctx);
/* TEMP (2026-11-14): the fill's idle time, its buffers full and waiting for the panel to free one: the gap
 * from one chunk's end to the next one's start (over 5 ms: a frame's turn or a stall, not counted). split by
 * whether a hot chunk (ovl_chunk_hot) is among the next 4 it makes: time there could be spent getting ahead */
/* TEMP: what a pool of buffers in any order would give: hot chunks by how many of the 4 chunks before them are
 * read straight from the frame (each one a chunk time the pool would hand back) */
static DRAM_ATTR volatile uint32_t s_pool_hist[5];
static DRAM_ATTR int64_t s_fe_us;
static DRAM_ATTR volatile uint32_t s_fi_busy, s_fi_idle, s_fi_idle_hot, s_fi_max, s_fi_frames0, s_fi_hist[5];
static IRAM_ATTR void fill(uint16_t *dst, int y, int rows, uint32_t frame, void *ctx)
{
    const int64_t t0 = esp_timer_get_time();
    const int c = y / CHUNK;
    if (s_fe_us) {
        const uint32_t gap = (uint32_t)(t0 - s_fe_us);
        if (gap < 5000) {
            s_fi_idle += gap;
            if (gap > s_fi_max) s_fi_max = gap;
            const uint32_t a = frame * CHUNKS + (uint32_t)c;
            int hot = 0;
            for (uint32_t k = 0; k < 4 && !hot; k++) hot = ovl_chunk_hot((a + k) / CHUNKS, (int)((a + k) % CHUNKS));
            if (hot) {
                s_fi_idle_hot += gap;
                s_fi_hist[gap < 25 ? 0 : gap < 50 ? 1 : gap < 100 ? 2 : gap < 200 ? 3 : 4]++;
            }
        }
    }
    if (c >= 4 && ovl_chunk_hot(frame, c)) {          // TEMP (s_pool_hist)
        int d = 0;
        for (int k = 1; k <= 4; k++) d += s_direct[c - k] != 0;
        s_pool_hist[d]++;
    }
    s_fill_cur = frame * CHUNKS + (uint32_t)c;
    fill_(dst, y, rows, frame, ctx);
    s_fill_end = s_fill_cur;
    const int64_t t1 = esp_timer_get_time();
    s_fi_busy += (uint32_t)(t1 - t0);
    s_fe_us = t1;
}
/* TEMP (busy.h): what was going on at each chunk, and at the late ones (g_busy is in main.c) */
static DRAM_ATTR volatile uint32_t s_bz_all[BUSY_N], s_bz_late[BUSY_N], s_bz_n_all, s_bz_n_late, s_bz_hot, s_bz_giveup, s_bz_shown;
static DRAM_ATTR volatile uint32_t s_bz_slowpre[BUSY_N], s_bz_n_slowpre;   // TEMP: at prefetches waited for over 100 us

static IRAM_ATTR void bz_count(volatile uint32_t *h, uint32_t b) { for (int i = 0; i < BUSY_N; i++) if (b >> i & 1) h[i]++; }
static IRAM_ATTR void bz_late(uint32_t b, uint32_t frame, int c, int how)
{
    bz_count(s_bz_late, b);
    s_bz_n_late++;
    if (ovl_chunk_hot(frame, c)) s_bz_hot++;
    if (how) s_bz_shown++; else s_bz_giveup++;
}
static IRAM_ATTR void fill_(uint16_t *dst, int y, int rows, uint32_t frame, void *ctx)
{
    (void)ctx;
    const int c = y / CHUNK;
    const uint32_t b0 = g_busy;                        // TEMP (busy.h)
    if (frame != s_frame) {                            // the first chunk seen of a panel frame
        s_frame = frame;
        /* a background drawn whole: up from this frame on. chunk 0 is filled while the frame before
         * is still being read, before its end puts the next one's list in: the panel's DMA and this
         * fill change frames together. a frame first seen later (its chunk 0 dropped) waits */
        if (c == 0 && s_want >= 0) {
            s_shown = s_want;
            s_want = -1;
            jc_show(s_shown);
            s_shown_at = jc_frames();
        }
        g_ovlRowsSkipped = jc_late() * CHUNK;
    }
    const size_t bytes = (size_t)rows * PANEL_W * sizeof(uint16_t);
    {                                                  // (chipguyhere) a scrolling page: the chunk gathered
        int S;
        if (c == 0 && s_sc_off_flip && s_want < 0 && s_shown_at == jc_frames()) {   // the page at rest is up from this frame
            s_sc_on = 0;
            s_sc_off_flip = 0;
        }
        if (sc_frame_on(frame, &S) && (int32_t)(frame * CHUNKS + (uint32_t)c) - (int32_t)jc_read() >= 1) {
            ovl_frame(frame);                          // the ui task is woken by each panel frame's start
            int ok = 0;
            if (s_pre_c == c && s_pre_frame == frame && s_pre_srcp == SC_TAG) {
                ok = dma_wait(dst, bytes, &s_pre_done);
            } else {
                if (s_dma_flag) dma_wait(jc_rbuf(s_pre_c), bytes, &s_pre_done);
                ok = sc_gather(dst, c, s_shown, S, &s_mcp_done) && dma_wait(dst, bytes, &s_mcp_done);
                if (!ok) s_sc_late++;
            }
            s_pre_c = -1;
            if (ok) {
                s_tag[c % NBUF] = frame * CHUNKS + (uint32_t)c;   // TEMP
                if (s_direct[c]) { s_direct[c] = 0; jc_direct(c, 0); if (c < 4) s_dir_off[c]++; }
                prefetch_next(c, frame);
                s_n_bounce++;
                return;
            }
        }
    }
    /* too late: the panel has begun on this chunk already (normally it is two before it), so it is read from
     * the frame this time, without the moving layer; made anyway, the panel would show what the buffer held,
     * the chunk four before. one chunk before, there is still up to a chunk's time: it is made, and stops
     * between lines if the panel gets there first (too_late) */
    const int32_t ahead = (int32_t)(frame * CHUNKS + (uint32_t)c) - (int32_t)jc_read();
    const int give_up = ahead < 1;
    if (give_up) { s_gave_up++; s_late_mono++; }
    bz_count(s_bz_all, b0);                            // TEMP (busy.h): every chunk past the scrolling
    s_bz_n_all++;
    if (give_up && s_layer) bz_late(b0, frame, c, 0);
    /* composed beforehand on core 0 (ovl.c's planner: the heaviest chunks of the frame, the background, the
     * sweep and the layer, in the PSRAM): copied from there, nothing more to do */
    const uint16_t *pre = s_layer && !give_up ? ovl_src(y, rows, frame, NULL) : NULL;
    const uint16_t *src = pre ? pre : s_fb[s_shown] + (size_t)y * PANEL_W;
    if (give_up || !s_layer || (!pre && !ovl_want(y, rows, frame, NULL))) {
        if (!s_direct[c] && give_up) {             // no time to fill the buffer too
            s_direct[c] = 1;
            jc_direct(c, 1); if (c < 4) s_dir_on[c]++;
        } else if (!s_direct[c]) {
            /* from the frame from now on. the buffer is filled this once too, in case the DMA has fetched
             * the chunk's list item already and still reads the buffer */
            if (s_dma_flag) dma_wait(jc_rbuf(s_pre_c), bytes, &s_pre_done);
            s_pre_c = -1;
            if (!(dma_copy(dst, src, bytes, &s_mcp_done) && dma_wait(dst, bytes, &s_mcp_done))) memcpy(dst, src, bytes);
            esp_cache_msync(dst, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            s_tag[c % NBUF] = frame * CHUNKS + (uint32_t)c;   // TEMP
            s_direct[c] = 1;
            jc_direct(c, 1); if (c < 4) s_dir_on[c]++;
        }
        s_n_direct++;
        /* given up or not, the next chunk's background and words on their way: without them it started
         * cold (a copy waited for, its words out of the PSRAM), was slow too, and was given up in turn */
        prefetch_next(c, frame);
        return;
    }
    const uint32_t t0 = esp_cpu_get_cycle_count();
    if (s_pre_c == c && s_pre_frame == frame && s_pre_srcp == src) {   // fetched while the chunk before was made
        if (!dma_wait(dst, bytes, &s_pre_done)) {
            memcpy(dst, src, bytes);
            s_pre_wat = NULL;
        }
        if (s_pre_wat) {                               // its layer words came with it
            esp_cache_msync(s_wbuf[c & 1], s_pre_wn * 4, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            ovl_chunk_local(y, rows, s_wbuf[c & 1], s_pre_wat);
            s_n_words_local++;
        }
        s_wait_cyc += esp_cpu_get_cycle_count() - t0;
        if (esp_cpu_get_cycle_count() - t0 > 100u * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) { s_slow_pre++; bz_count(s_bz_slowpre, b0); s_bz_n_slowpre++; }
        s_n_staged++;
    } else {
        if (s_dma_flag) dma_wait(jc_rbuf(s_pre_c), bytes, &s_pre_done);   // a fetch for a chunk not filled after all (dropped): let it land
        if (dma_copy(dst, src, bytes, &s_mcp_done) && dma_wait(dst, bytes, &s_mcp_done)) {
            s_sync_cyc += esp_cpu_get_cycle_count() - t0;
        } else {
            if (s_mcp) s_mcp_failed++;
            memcpy(dst, src, bytes);                   // the background's rows, out of the PSRAM
        }
    }
    s_pre_c = -1;
    prefetch_next(c, frame);
    const uint32_t t1 = esp_cpu_get_cycle_count();
    if (t1 - t0 > 150u * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) s_slow_copy++;
    s_cut_at = frame * CHUNKS + (uint32_t)c;
    if (pre) {
        s_n_pre++;                                     // whole already
    } else if (s_helper) {                             // the sweep and the layer on them, half here, half there
        const int half = rows / 2;
        s_h_dst = dst;
        s_h_y = y;
        s_h_r0 = half;
        s_h_r1 = rows;
        s_h_frame = frame;
        s_h_done = 0;
        __atomic_store_n(&s_h_claim, 0, __ATOMIC_RELEASE);
        xTaskNotifyGive(s_helper);
        ovl_chunk_lines(dst, y, 0, half, frame, 0);
        /* the other half not begun yet (its core held up: an interrupt, the PPA's cache work): done here */
        if (!__atomic_exchange_n(&s_h_claim, 1, __ATOMIC_ACQ_REL)) {
            ovl_chunk_lines(dst, y, half, rows, frame, 0);
            s_h_done = 1;
            s_h_stolen++;
        }
        const uint32_t tw = esp_cpu_get_cycle_count();
        while (!s_h_done) {
            if (esp_cpu_get_cycle_count() - tw > 2000u * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) { s_h_timeouts++; break; }   // the panel must not hang on it
        }
        if (esp_cpu_get_cycle_count() - tw > 100u * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) s_slow_h++;
    } else {
        ovl_chunk(dst, y, rows, frame, NULL);
    }
    const uint32_t t2 = esp_cpu_get_cycle_count();
    ovl_chunk_local(0, 0, NULL, NULL);
    s_copy_cyc += t1 - t0;
    s_ovl_cyc += t2 - t1;
    if (t2 - t0 > 300u * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) s_slow_fill++;
    if ((int32_t)(frame * CHUNKS + (uint32_t)c) - (int32_t)jc_read() < 1) { s_shown_late++; s_late_mono++; bz_late(b0, frame, c, 1); }   // (cut short, then)
    s_tag[c % NBUF] = frame * CHUNKS + (uint32_t)c;       // TEMP
    if (s_direct[c]) {                                 // from the buffer again: written back first
        esp_cache_msync(dst, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        s_direct[c] = 0;
        jc_direct(c, 0); if (c < 4) s_dir_off[c]++;
    }
    s_n_bounce++;
}

#ifndef JC_GATHER_TEST
#define JC_GATHER_TEST 0                               // 1: the scrolling pages' GDMA gather checked and timed at start (2026-11-14: right at any pixel; 151 us a chunk, 159 a plain copy)
#endif
#if JC_GATHER_TEST
/* (chipguyhere, temporary) can the AXI GDMA gather a chunk the way a scrolling page needs: each of its 16
 * panel rows a fixed start and end from the frame and a span of the list between, the list's source and
 * its place in the row at any pixel? checked against the CPU, and timed against a plain chunk copy */
static void gather_test(const char *when)
{
    const int ROWS = CHUNK, W = PANEL_W, CW = 2400;    // CW: a list's height in glass pixels, one panel row's worth
    const size_t cb = (size_t)ROWS * W * 2;
    uint16_t *fr = heap_caps_aligned_alloc(128, cb, MALLOC_CAP_SPIRAM);
    uint16_t *ct = heap_caps_aligned_alloc(128, (size_t)ROWS * CW * 2, MALLOC_CAP_SPIRAM);
    uint16_t *dst = heap_caps_aligned_alloc(128, cb, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    uint16_t *ref = heap_caps_malloc(cb, MALLOC_CAP_INTERNAL);
    gdma_link_list_handle_t ltx = NULL, lrx = NULL;
    gdma_link_list_config_t lc = { .num_items = 3 * CHUNK + 4, .item_alignment = GDMA_LL_AXI_DESC_ALIGNMENT, .flags.check_owner = 1 };
    if (!fr || !ct || !dst || !ref || !s_mcp || gdma_new_link_list(&lc, &ltx) != ESP_OK) goto out;
    lc.num_items = 8;
    if (gdma_new_link_list(&lc, &lrx) != ESP_OK) goto out;
    for (size_t i = 0; i < (size_t)ROWS * W; i++) fr[i] = (uint16_t)(i * 2654435761u >> 16);
    for (size_t i = 0; i < (size_t)ROWS * CW; i++) ct[i] = (uint16_t)(i * 40503u + 7);
    esp_cache_msync(fr, cb, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_cache_msync(ct, (size_t)ROWS * CW * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    // a plain chunk copy, the fill's own, for the time to compare with
    uint32_t plain = 0;
    for (int k = 0; k < 8; k++) {
        const uint32_t t = esp_cpu_get_cycle_count();
        if (!(dma_copy(dst, fr, cb, &s_mcp_done) && dma_wait(dst, cb, &s_mcp_done))) { ESP_LOGE(TAG, "gather test: the plain copy failed"); goto out; }
        plain += esp_cpu_get_cycle_count() - t;
    }
    plain /= 8;
    int bad_any = 0;
    uint32_t worst = 0, sum = 0, mount = 0;
    for (int sh = 0; sh < 32; sh++) {
        const int a = 160 + sh, h = 520;               // the list's place in the row: from column a, h pixels
        const int e = 1000 + 3 * sh;                   // where in the list: any pixel
        gdma_buffer_mount_config_t t[3 * CHUNK];
        for (int r = 0; r < ROWS; r++) {
            uint16_t *row = fr + (size_t)r * W;
            t[3 * r + 0] = (gdma_buffer_mount_config_t){ .buffer = row, .buffer_alignment = 1, .length = (size_t)a * 2 };
            t[3 * r + 1] = (gdma_buffer_mount_config_t){ .buffer = ct + (size_t)r * CW + e, .buffer_alignment = 1, .length = (size_t)h * 2 };
            t[3 * r + 2] = (gdma_buffer_mount_config_t){ .buffer = row + a + h, .buffer_alignment = 1, .length = (size_t)(W - a - h) * 2 };
        }
        t[3 * ROWS - 1].flags.mark_eof = 1;
        t[3 * ROWS - 1].flags.mark_final = GDMA_FINAL_LINK_TO_NULL;
        gdma_buffer_mount_config_t rx = { .buffer = dst, .buffer_alignment = 128, .length = cb,
                                          .flags = { .mark_eof = 1, .mark_final = GDMA_FINAL_LINK_TO_NULL } };
        memset(dst, 0, cb);
        esp_cache_msync(dst, cb, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        const uint32_t tm = esp_cpu_get_cycle_count();
        if (gdma_link_mount_buffers(ltx, 0, t, 3 * ROWS, NULL) != ESP_OK || gdma_link_mount_buffers(lrx, 0, &rx, 1, NULL) != ESP_OK) {
            ESP_LOGE(TAG, "gather test: the lists were refused at shift %d", sh);
            goto out;
        }
        const uint32_t t0 = esp_cpu_get_cycle_count();
        mount += t0 - tm;
        s_mcp_done = 0;
        s_dma_flag = &s_mcp_done;
        gdma_start(s_drx, gdma_link_get_head_addr(lrx));
        gdma_start(s_dtx, gdma_link_get_head_addr(ltx));
        if (!dma_wait(dst, cb, &s_mcp_done)) { ESP_LOGE(TAG, "gather test: no answer at shift %d", sh); s_mcp = dma_init(); goto out; }
        const uint32_t d = esp_cpu_get_cycle_count() - t0;
        sum += d;
        if (d > worst) worst = d;
        for (int r = 0; r < ROWS; r++) {
            memcpy(ref + (size_t)r * W, fr + (size_t)r * W, (size_t)W * 2);
            memcpy(ref + (size_t)r * W + a, ct + (size_t)r * CW + e, (size_t)h * 2);
        }
        int bad = 0;
        for (size_t i = 0; i < (size_t)ROWS * W; i++) bad += dst[i] != ref[i];
        if (bad) { bad_any++; ESP_LOGW(TAG, "gather test: shift %d (list from pixel %d to column %d): %d pixels wrong", sh, e, a, bad); }
    }
    const uint32_t mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    ESP_LOGI(TAG, "gather test, %s: %s; a chunk gathered (48 pieces, misaligned) %u us on average, %u at most, its lists made in %u us; a plain chunk copy %u us",
             when, bad_any ? "WRONG at some shifts" : "every shift right", (unsigned)(sum / 32 / mhz), (unsigned)(worst / mhz),
             (unsigned)(mount / 32 / mhz), (unsigned)(plain / mhz));
out:
    if (ltx) gdma_del_link_list(ltx);
    if (lrx) gdma_del_link_list(lrx);
    heap_caps_free(fr); heap_caps_free(ct); heap_caps_free(dst); heap_caps_free(ref);
}
#endif

void disp_init(void)
{
    s_layer = ovl_init();
#if JC_GATHER_TEST
    s_mcp = dma_init();
    gather_test("before the panel starts");
#endif
    if (!jc_begin(CHUNKS + 2)) return;
    s_fb[0] = jc_fb(0);
    s_fb[1] = jc_fb(1);
    ppa_client_config_t pc = { .oper_type = PPA_OPERATION_SRM };
    s_ppa_done = xSemaphoreCreateBinary();
    if (!s_fb[0] || !s_fb[1] || !s_ppa_done || ppa_register_client(&pc, &s_ppa) != ESP_OK ||
        xTaskCreatePinnedToCore(ppa_task, "ppa", 3072, NULL, 4, &s_ppa_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "no frames or no PPA: the screen stays dark");
        return;
    }
    ovl_stop_when(too_late);
    ovl_source(next_row);
    ovl_writeback(written_back);
    { extern void jc_set_check(void (*f)(uint32_t, uint32_t)); jc_set_check(check_cb); }   // TEMP
    ovl_build_pace(layer_pace_ovl);
    ovl_planner_quiet(runway_clear);                   // the planner's PSRAM bursts kept off the fill's hot runs too
    if (!s_mcp) s_mcp = dma_init();
    for (int i = 0; i < 2 && s_mcp; i++)
        s_wbuf[i] = heap_caps_aligned_alloc(128, WORDS_CAP * sizeof(uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_wbuf[0] || !s_wbuf[1]) {
        heap_caps_free(s_wbuf[0]);
        heap_caps_free(s_wbuf[1]);
        s_wbuf[0] = s_wbuf[1] = NULL;
        ESP_LOGW(TAG, "no RAM for the layer's words: read from the PSRAM");
    }
#if JC_GATHER_TEST
    gather_test("the panel running");
#endif
    if (xTaskCreatePinnedToCore(helper_task, "fill2", 3072, NULL, configMAX_PRIORITIES - 1, &s_helper, 0) != pdPASS) s_helper = NULL;
    if (!s_mcp) ESP_LOGW(TAG, "no AXI GDMA for the copies: the CPU copies, too slowly");
    s_shown = 0;
    s_draw = 1;
    s_ok = 1;
    /* the band over the whole panel, the fill on the second core: the moving layer may be anywhere
     * (the flat map has planes all over it). the chunks it leaves alone cost a few microseconds */
    s_band = jc_raster(0, PANEL_H, fill, 1, CHUNK);
    if (!s_band) {
        s_layer = 0;
        ESP_LOGE(TAG, "no raster band (the library says why above): the background alone, nothing moving over it");
    }
    ESP_LOGI(TAG, "JC8012P4A1C, %dx%d standing, the picture %dx%d lying, turned as it is drawn; the moving layer %s",
             PANEL_W, PANEL_H, GFX_PW, GFX_PH, s_layer ? "over it, by columns" : "NOT THERE");
}

void disp_backlight(int percent) { if (s_ok) jc_backlight(percent < 0 ? 0 : percent > 100 ? 100 : percent); }

// the last background is up, and the panel has finished with the frame before it
int disp_bg_ready(void) { return !s_ok || (s_want < 0 && jc_frames() != s_shown_at); }

void disp_bg_begin(void) { s_draw = s_shown ? 0 : 1; }   // the one that does not show

/* into the frame, turned (as the Tab5's lcd_dsi.c): the PPA reads the canvas as it is, 0x00RRGGBB a
 * pixel, and writes 565 into the frame. it turns counter-clockwise: 270 puts the picture's left edge at
 * the top of the glass and its row y in the glass's column 799 - y; 90 the other way round. the block it
 * writes is h across and 1280 down.
 *
 * on the first core, by a task of its own: the PPA's driver then drops the cache over all it wrote round
 * (the whole 2 MB frame, for a strip so turned) with that core's interrupts held off meanwhile. on the
 * second core that held off the panel's chunk interrupts: several came as one, the driver counts them to
 * know which chunk is done, and for the rest of the frame the fill made each buffer's chunk for the
 * wrong place (chunks whole, but one or more chunks over, seen on the glass). turning it on the CPU
 * instead (tried) kept the cache so busy that up to ~1,500 chunks a 10 s were late */

static volatile uint32_t s_ppa_max_us, s_ppa_wait_ms;
static void layer_pace(void);
/* (chipguyhere) the moving layer's making waiting for the panel, as the ui task's pass timing counts it
 * (main.c): ovl.c's builds only, not the background's turns */
static volatile uint32_t s_pace_ui_us;
volatile int64_t g_nopace_until;                       // TEMP: OWLOS NOPACE on the console, the layer's builds unpaced till then (us)
/* (chipguyhere, 2026-11-14) the moving layer's builds, paced only when the fill has been late lately. measured
 * with 48 planes: paced at every step (25 ms each, ~140 steps a build), a build waited up to 3.4 s, the radar
 * ran at 1 fps, and chunks were late as often as unpaced (20-208 against 85 in 10 s). now each wait lasts only
 * while a hot chunk (ovl_chunk_hot) is coming, at most PACE_BUDGET_US a build */
#define PACE_TROUBLE_US 2000000
#define PACE_BUDGET_US  30000
static int64_t  s_pace_left_us, s_trouble_until;
static uint32_t s_late_seen;
static volatile uint32_t s_paced_builds, s_builds;      // for the log
void disp_pace_new_build(void)
{
    const uint32_t m = s_late_mono;
    const int64_t t = esp_timer_get_time();
    if (m != s_late_seen) { s_late_seen = m; s_trouble_until = t + PACE_TROUBLE_US; }
    (void)s_trouble_until;                             // (the late count is still followed, for the log)
    s_pace_left_us = t >= g_nopace_until ? PACE_BUDGET_US : 0;   // always: the wait is only as long as a hot run
    s_builds++;
    if (s_pace_left_us) s_paced_builds++;
}
/* (chipguyhere) the build's last step (the records sorted into columns, all PSRAM) gets a budget of its own:
 * the bands always spent theirs, so it ran unpaced, and it was busy at 30-88 % of the late chunks against 11 %
 * of all (TEMP busy, 2026-11-14) */
#define PACE_END_US 15000
void disp_pace_end_phase(void) { s_pace_left_us = g_nopace_until > esp_timer_get_time() ? 0 : PACE_END_US; }
static void layer_pace_ovl(void)
{
    if (s_pace_left_us <= 0) return;
    const int64_t t = esp_timer_get_time();
    layer_pace_for(s_pace_left_us);
    const int64_t w = esp_timer_get_time() - t;
    s_pace_left_us -= w;
    s_pace_ui_us += (uint32_t)w;
}
uint32_t disp_pace_us(void) { return s_pace_ui_us; }
static void ppa_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* while the radar's chunks are being filled, the PPA waits: its driver's cache work holds this core's
         * interrupts off for its length, and the fill with them */
        const int64_t w0 = esp_timer_get_time();
        layer_pace();
        const int64_t t0 = esp_timer_get_time();
        s_ppa_wait_ms += (uint32_t)((t0 - w0) / 1000);
        esp_err_t e = ppa_do_scale_rotate_mirror(s_ppa, &s_ppa_op);
        const uint32_t d = (uint32_t)(esp_timer_get_time() - t0);
        if (d > s_ppa_max_us) s_ppa_max_us = d;
        if (e != ESP_OK) ESP_LOGW(TAG, "the PPA refused a strip: %s", esp_err_to_name(e));
        xSemaphoreGive(s_ppa_done);
    }
}

#ifndef JC_CPU_TURN
#define JC_CPU_TURN 1                                  // TEMP A/B: 1 the CPU turns the strips, paced; 0 the PPA
#endif
static volatile uint32_t s_cpu_turn_us;
/* (chipguyhere) main.c's background frame may be given up (a page or a press wants the core): asked every
 * 16 columns, the strip left part turned */
static int (*s_turn_stop)(void);
void disp_bg_stop_fn(int (*fn)(void)) { s_turn_stop = fn; }
static void cpu_turn(const canvas_t *cv)
{
    s_twin = 0;                                        // (chipguyhere) one frame only
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    const int h = cv->h;
    uint16_t *fb = s_fb[s_draw];
    const int c0 = s_flip ? cv->y0 : PANEL_W - cv->y0 - h;     // the glass's columns c0 .. c0 + h - 1
    for (int x = 0; x < GFX_PW; x++) {
        if (!(x & 15)) {                                       // the panel off the radar's chunks: on; else wait
            if (s_turn_stop && s_turn_stop()) return;
            layer_pace();
        }
        const int64_t t0 = esp_timer_get_time();
        const int py = s_flip ? PANEL_H - 1 - x : x;
        uint16_t *o = fb + (size_t)py * PANEL_W + c0;
        const uint32_t *in = cv->px + x;
        for (int r = 0; r < h; r++) {
            const int y = cv->y0 + r;
            const uint32_t p = in[(size_t)r * GFX_PW];
            const unsigned d = bayer[y & 3][x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3, gg = (((p >> 8) & 255) + (d >> 2)) >> 2, bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            o[s_flip ? r : h - 1 - r] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
        esp_cache_msync(o, (size_t)h * sizeof(uint16_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        s_cpu_turn_us += (uint32_t)(esp_timer_get_time() - t0);
    }
}

/* (chipguyhere) a page's bands (main.c page_draw): turned by the CPU into the frame, no pacing (a page has
 * no moving layer) and no cache sync per column (esp_cache_msync 1,280 times made a 16-row band ~10 ms);
 * disp_page_flush syncs the frame once, after all the bands a change made. tried and dropped: the cache's
 * registers driven directly (cache_ll_*), past esp_cache_msync's lock: stale lines on the glass, and the SDIO
 * link to the C6 timed out (its DMA's cache syncs run under that lock) and restarted the board */
void disp_page_begin(void) { s_draw = s_shown; }       // into the frame showing
void disp_page_turn(const canvas_t *cv)
{
    s_twin = 0;                                        // (chipguyhere) one frame only
    if (!s_ok) return;
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    const int h = cv->h;
    uint16_t *fb = s_fb[s_draw];
    const int c0 = s_flip ? cv->y0 : PANEL_W - cv->y0 - h;
    for (int x = 0; x < GFX_PW; x++) {
        const int py = s_flip ? PANEL_H - 1 - x : x;
        uint16_t *o = fb + (size_t)py * PANEL_W + c0;
        const uint32_t *in = cv->px + x;
        for (int r = 0; r < h; r++) {
            const int y = cv->y0 + r;
            const uint32_t p = in[(size_t)r * GFX_PW];
            const unsigned d = bayer[y & 3][x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3, gg = (((p >> 8) & 255) + (d >> 2)) >> 2, bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            o[s_flip ? r : h - 1 - r] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
    }
}
/* (chipguyhere) columns x0 .. x1 - 1 of a strip only (a rectangle the ui says changed, main.c), into the
 * frame showing, and only the panel rows they land on written back: x is the panel's row here */
void disp_page_rect(const canvas_t *cv, int x0, int x1, int sync)   // sync 0: disp_page_flush after the last
{
    if (!s_ok || x0 >= x1) return;
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    const int h = cv->h;
    uint16_t *fb = s_fb[s_draw];                       // (chipguyhere) the frame chosen: the hidden one now (main.c rects_to_glass)
    const int c0 = s_flip ? cv->y0 : PANEL_W - cv->y0 - h;
    for (int x = x0; x < x1; x++) {
        const int py = s_flip ? PANEL_H - 1 - x : x;
        uint16_t *o = fb + (size_t)py * PANEL_W + c0;
        const uint32_t *in = cv->px + x;
        for (int r = 0; r < h; r++) {
            const int y = cv->y0 + r;
            const uint32_t p = in[(size_t)r * GFX_PW];
            const unsigned d = bayer[y & 3][x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3, gg = (((p >> 8) & 255) + (d >> 2)) >> 2, bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            o[s_flip ? r : h - 1 - r] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
    }
    const int pa = s_flip ? PANEL_H - x1 : x0, pb = s_flip ? PANEL_H - 1 - x0 : x1 - 1;   // the panel rows, first and last
    uint16_t *first = fb + (size_t)pa * PANEL_W + c0, *last = fb + (size_t)pb * PANEL_W + c0 + h;
    if (sync) esp_cache_msync(first, (size_t)(last - first) * sizeof(uint16_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

uint32_t disp_frames_now(void) { return jc_frames(); }   // TEMP
/* (chipguyhere, 2026-11-14) the two frames hold the same picture (s_twin): a change can then be drawn into the
 * one that does not show and flipped in whole, then drawn into the other. drawn into the frame showing, a
 * button took 20-48 ms (a cache line fetched for each of the panel's 1,280 rows it crosses) and the glass
 * showed it half done, in whatever order the cache let the lines go. anything that draws into one frame alone
 * clears it; disp_twin_copy makes the hidden one the showing one again */
int  disp_twin(void) { return s_twin; }
void disp_twin_set(int on) { s_twin = on; }
void disp_twin_copy(void)
{
    if (!s_ok) return;
    const uint8_t *from = (const uint8_t *)s_fb[s_shown];
    uint8_t *to = (uint8_t *)s_fb[s_shown ? 0 : 1];
    const size_t all = (size_t)PANEL_W * PANEL_H * sizeof(uint16_t), piece = 64 * 1024;
    for (size_t at = 0; at < all; at += piece) {      // in pieces, each written back and kept off the fill's hot runs
        const size_t n = all - at < piece ? all - at : piece;
        layer_pace_for(2000);
        memcpy(to + at, from + at, n);
        esp_cache_msync(to + at, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    s_twin = 1;
}
void disp_page_flush(void)
{
    esp_cache_msync(s_fb[s_draw], (size_t)PANEL_W * PANEL_H * sizeof(uint16_t), ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

void disp_strip(const canvas_t *cv)
{
    if (!s_ok || !s_ppa_task) return;
    s_twin = 0;                                        // (chipguyhere) one frame only
    if (JC_CPU_TURN) { cpu_turn(cv); return; }
    const int h = cv->h;
    s_ppa_op = (ppa_srm_oper_config_t){
        .in = { .buffer = cv->px, .pic_w = GFX_PW, .pic_h = (uint32_t)h, .block_w = GFX_PW, .block_h = (uint32_t)h,
                .block_offset_x = 0, .block_offset_y = 0, .srm_cm = PPA_SRM_COLOR_MODE_ARGB8888 },
        .out = { .buffer = s_fb[s_draw], .buffer_size = (uint32_t)PANEL_W * PANEL_H * sizeof(uint16_t),
                 .pic_w = PANEL_W, .pic_h = PANEL_H,
                 .block_offset_x = (uint32_t)(s_flip ? cv->y0 : PANEL_W - cv->y0 - h), .block_offset_y = 0,
                 .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = s_flip ? PPA_SRM_ROTATION_ANGLE_90 : PPA_SRM_ROTATION_ANGLE_270,
        .scale_x = 1.0f, .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    xTaskNotifyGive(s_ppa_task);
    xSemaphoreTake(s_ppa_done, portMAX_DELAY);
}

void disp_frame_end(void)
{
    if (!s_ok) return;
    if (s_band) {
        s_want = s_draw;                               // the fill puts it up at the next panel frame's start
    } else {
        jc_show(s_draw);
        s_shown = s_draw;
        s_shown_at = jc_frames();
    }
}

uint32_t disp_frame_us(void)
{
    uint32_t p = s_ok ? jc_period_us() : 0;
    return p ? p : 19231;
}

void disp_log(void)
{
    if (!s_ok) return;
    {   /* TEMP: the panel's first chunks (the strip at the start of the scan). per chunk: the fill's direct flag, the
         * library's bit, and for each frame's list where its item reads from: F the frame's own rows, B<n> bounce
         * buffer n, ? elsewhere, a trailing * if that list is out of date (rebuilt before it is used) */
        char line[200];
        int at = 0;
        for (int c = 0; c < 4; c++) {
            int bit = 0, fresh = 0;
            at += snprintf(line + at, sizeof line - (size_t)at, " c%d d%d", c, s_direct[c]);
            for (int f = 0; f < 2; f++) {
                const uint32_t src = jc_src(f, c, &bit, &fresh);
                char w[8] = "?";
                if (src == (uint32_t)(s_fb[f] + (size_t)c * CHUNK * PANEL_W)) snprintf(w, sizeof w, "F");
                for (int b = 0; b < NBUF; b++) if (src == (uint32_t)jc_rbuf(b)) snprintf(w, sizeof w, "B%d", b);
                at += snprintf(line + at, sizeof line - (size_t)at, " %s%s", w, fresh ? "" : "*");
            }
            at += snprintf(line + at, sizeof line - (size_t)at, " b%d +%u-%u;", bit, (unsigned)s_dir_on[c], (unsigned)s_dir_off[c]);
            s_dir_on[c] = s_dir_off[c] = 0;
        }
        ESP_LOGI(TAG, "TEMP first chunks (shown fb %d):%s", s_shown, line);
    }
    ESP_LOGI(TAG, "moving layer builds: %u, %u of them paced", (unsigned)s_builds, (unsigned)s_paced_builds);
    {   /* TEMP: the fill's idle time (see fill) */
        const uint32_t fr = jc_frames(), nf = fr - s_fi_frames0 ? fr - s_fi_frames0 : 1;
        ESP_LOGI(TAG, "TEMP fill a frame: busy %u us, idle %u us (%u us of it with a hot chunk in the next 4), the longest gap %u us; "
                 "gaps before hot chunks: <25 us %u, 25-50 %u, 50-100 %u, 100-200 %u, >200 %u",
                 (unsigned)(s_fi_busy / nf), (unsigned)(s_fi_idle / nf), (unsigned)(s_fi_idle_hot / nf), (unsigned)s_fi_max,
                 (unsigned)s_fi_hist[0], (unsigned)s_fi_hist[1], (unsigned)s_fi_hist[2], (unsigned)s_fi_hist[3], (unsigned)s_fi_hist[4]);
        s_fi_busy = s_fi_idle = s_fi_idle_hot = s_fi_max = 0;
        for (int i = 0; i < 5; i++) s_fi_hist[i] = 0;
        s_fi_frames0 = fr;
    }
    {   /* TEMP (busy.h): each activity's share of all chunks / of the late ones, %. over-represented among the
         * late ones: it competes with the fill */
        static const char *const nm[BUSY_N] = BUSY_NAMES;
        char line[300];
        int at = 0;
        const uint32_t na = s_bz_n_all ? s_bz_n_all : 1, nl = s_bz_n_late ? s_bz_n_late : 1;
        for (int i = 0; i < BUSY_N; i++)
            at += snprintf(line + at, sizeof line - (size_t)at, " %s %u/%u", nm[i], (unsigned)(s_bz_all[i] * 100u / na),
                           (unsigned)(s_bz_late[i] * 100u / nl));
        extern volatile uint32_t g_plan_hot, g_plan_heavy, g_plan_made, g_plans;
        const uint32_t np = g_plans ? g_plans : 1;
        ESP_LOGI(TAG, "TEMP planner: a frame %u.%u heavy chunks, %u.%u composed beforehand, %u.%u left hot to the fill (%u plans)",
                 (unsigned)(g_plan_heavy / np), (unsigned)(g_plan_heavy * 10 / np % 10), (unsigned)(g_plan_made / np),
                 (unsigned)(g_plan_made * 10 / np % 10), (unsigned)(g_plan_hot / np), (unsigned)(g_plan_hot * 10 / np % 10), (unsigned)g_plans);
        {   /* TEMP: the fill's layer time by path (ovl.c words_apply) */
            void ovl_path_stats(uint32_t out[4][3]);
            uint32_t ps[4][3];
            ovl_path_stats(ps);
            static const char *const pn[4] = { "PIE", "PIE edges (C)", "solid runs", "per-pixel words" };
            const uint32_t nfr = s_fi_frames0 ? 1 : 1;
            (void)nfr;
            for (int k = 0; k < 4; k++)
                ESP_LOGI(TAG, "TEMP layer path %-16s: %7u runs, %8u px, %6u us; %3u.%u cycles a px, %4u a run",
                         pn[k], (unsigned)ps[k][0], (unsigned)ps[k][1], (unsigned)(ps[k][2] / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
                         (unsigned)(ps[k][1] ? ps[k][2] / ps[k][1] : 0), (unsigned)(ps[k][1] ? ps[k][2] * 10 / ps[k][1] % 10 : 0),
                         (unsigned)(ps[k][0] ? ps[k][2] / ps[k][0] : 0));
        }
        extern volatile uint32_t g_plan_wait_us;
        ESP_LOGI(TAG, "TEMP planner waited %u ms for the runway in 10 s", (unsigned)(g_plan_wait_us / 1000u));
        g_plan_hot = g_plan_heavy = g_plan_made = g_plans = g_plan_wait_us = 0;
        ESP_LOGI(TAG, "TEMP busy %% of all %u chunks / of %u late (%u given up, %u finished late, %u hot):%s",
                 (unsigned)s_bz_n_all, (unsigned)s_bz_n_late, (unsigned)s_bz_giveup, (unsigned)s_bz_shown, (unsigned)s_bz_hot, line);
        {
            char l2[300];
            int a2 = 0;
            const uint32_t ns = s_bz_n_slowpre ? s_bz_n_slowpre : 1;
            for (int i = 0; i < BUSY_N; i++)
                a2 += snprintf(l2 + a2, sizeof l2 - (size_t)a2, " %s %u/%u", nm[i], (unsigned)(s_bz_all[i] * 100u / na), (unsigned)(s_bz_slowpre[i] * 100u / ns));
            ESP_LOGI(TAG, "TEMP busy %% of all chunks / of %u prefetches waited for over 100 us:%s", (unsigned)s_bz_n_slowpre, l2);
            ESP_LOGI(TAG, "TEMP pool: hot chunks by direct chunks among the 4 before: 0 %u, 1 %u, 2 %u, 3 %u, 4 %u",
                     (unsigned)s_pool_hist[0], (unsigned)s_pool_hist[1], (unsigned)s_pool_hist[2], (unsigned)s_pool_hist[3], (unsigned)s_pool_hist[4]);
            for (int i = 0; i < BUSY_N; i++) s_bz_slowpre[i] = 0;
            for (int i = 0; i < 5; i++) s_pool_hist[i] = 0;
            s_bz_n_slowpre = 0;
        }
        for (int i = 0; i < BUSY_N; i++) s_bz_all[i] = s_bz_late[i] = 0;
        s_bz_n_all = s_bz_n_late = s_bz_giveup = s_bz_shown = s_bz_hot = 0;
    }
    s_builds = s_paced_builds = 0;
    static uint32_t late0, drop0, fr0;
    uint32_t late, dropped, max_us, chunk_us, under;
    jc_log(&late, &dropped, &max_us, &chunk_us, &under);
    const uint32_t fr = jc_frames(), nf = fr - fr0 ? fr - fr0 : 1;
    const uint32_t p = jc_period_us();
    const uint32_t nb = s_n_bounce, nd = s_n_direct, cc = s_copy_cyc, oc = s_ovl_cyc, ns = s_n_staged, wc = s_wait_cyc;
    s_n_bounce = s_n_direct = s_copy_cyc = s_ovl_cyc = s_n_staged = s_wait_cyc = 0;
    ESP_LOGI(TAG, "panel %.1f Hz, %u underruns; chunks a frame: %u filled, %u straight from the frame; "
                  "late %u, dropped %u; worst chunk %u us of %u",
             p ? 1e6 / p : 0.0, (unsigned)under, (unsigned)(nb / nf), (unsigned)(nd / nf), (unsigned)(late - late0),
             (unsigned)(dropped - drop0), (unsigned)max_us, (unsigned)chunk_us);
    ESP_LOGI(TAG, "of the filled chunks %u a frame were fetched beforehand, waited for %u us a frame; launching %u us, "
                  "fetching while waiting %u us a frame", (unsigned)(ns / nf),
             (unsigned)(wc / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / nf), (unsigned)(s_launch_cyc / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / nf),
             (unsigned)(s_sync_cyc / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / nf));
    s_launch_cyc = s_sync_cyc = 0;
    ESP_LOGI(TAG, "layer words fetched with the chunk: %u chunks; too many to fetch: %u", (unsigned)s_n_words_local,
             (unsigned)s_n_words_over);
    s_n_words_local = s_n_words_over = 0;
    { extern uint32_t jc_late_exact(void); static uint32_t le0; const uint32_t le = jc_late_exact();
      ESP_LOGI(TAG, "chunks the panel began before they were drawn (the library's exact count): %u", (unsigned)(le - le0)); le0 = le; }
    ESP_LOGI(TAG, "chunks given up as too near the panel: %u; filled but finished after the panel began on them: %u",
             (unsigned)s_gave_up, (unsigned)s_shown_late);
    s_shown_late = 0;
    s_gave_up = 0;
    ESP_LOGI(TAG, "slow: fills over 300 us %u, copies over 150 us %u, fetch waits over 100 us %u, other core over 100 us %u, "
                  "its timeouts %u", (unsigned)s_slow_fill, (unsigned)s_slow_copy, (unsigned)s_slow_pre, (unsigned)s_slow_h,
             (unsigned)s_h_timeouts);
    s_slow_fill = s_slow_copy = s_slow_pre = s_slow_h = s_h_timeouts = 0;
    { extern uint32_t jc_merged(void); static uint32_t m0; const uint32_t m = jc_merged();
      ESP_LOGI(TAG, "chunk interrupts that came merged, caught up from where the DMA is: %u", (unsigned)(m - m0)); m0 = m; }
    ESP_LOGI(TAG, "the moving layer's making waited for the panel %u ms; the PPA %u ms, its longest turn %u us",
             (unsigned)(s_layer_wait_us / 1000), (unsigned)s_ppa_wait_ms, (unsigned)s_ppa_max_us);
    s_ppa_wait_ms = s_ppa_max_us = 0;
    ESP_LOGI(TAG, "TEMP the CPU turning strips: %u ms", (unsigned)(s_cpu_turn_us / 1000));
    s_cpu_turn_us = 0;
    s_layer_wait_us = 0;
    ESP_LOGI(TAG, "TEMP WRONG chunks on the glass: %u; by eighths %u %u %u %u %u %u %u %u; chunks old <0,1-4,5-8,..,>32: %u %u %u %u %u %u %u %u %u",
             (unsigned)s_wrong, (unsigned)s_wrong_by[0], (unsigned)s_wrong_by[1], (unsigned)s_wrong_by[2], (unsigned)s_wrong_by[3],
             (unsigned)s_wrong_by[4], (unsigned)s_wrong_by[5], (unsigned)s_wrong_by[6], (unsigned)s_wrong_by[7],
             (unsigned)s_wrong_off[0], (unsigned)s_wrong_off[1], (unsigned)s_wrong_off[2], (unsigned)s_wrong_off[3], (unsigned)s_wrong_off[4],
             (unsigned)s_wrong_off[5], (unsigned)s_wrong_off[6], (unsigned)s_wrong_off[7], (unsigned)s_wrong_off[8]);
    for (uint32_t i = 0; i < s_nev && i < 8; i++)
        ESP_LOGI(TAG, "TEMP wrong: want %u (chunk %u) have %u, filling %u, last filled %u, read %u", (unsigned)s_ev[i][0],
                 (unsigned)(s_ev[i][0] % CHUNKS), (unsigned)s_ev[i][1], (unsigned)s_ev[i][2], (unsigned)s_ev[i][3], (unsigned)s_ev[i][4]);
    s_nev = 0;
    s_wrong = 0;
    for (int i = 0; i < 8; i++) s_wrong_by[i] = 0;
    for (int i = 0; i < 9; i++) s_wrong_off[i] = 0;
    ESP_LOGI(TAG, "chunks composed beforehand, copied whole: %u", (unsigned)s_n_pre);
    s_n_pre = 0;
    ESP_LOGI(TAG, "halves the other core had not begun, done here: %u", (unsigned)s_h_stolen);
    s_h_stolen = 0;
    if (s_mcp_failed) ESP_LOGW(TAG, "%d copies the GDMA refused, done by the CPU", s_mcp_failed);
    ESP_LOGI(TAG, "fill a frame: copying %u us, the moving layer %u us (%u us a filled chunk)",
             (unsigned)(cc / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / nf), (unsigned)(oc / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / nf),
             (unsigned)(nb ? (cc + oc) / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / nb : 0));
    late0 = late;
    drop0 = dropped;
    fr0 = fr;
}

/* (chipguyhere, 2026-11-14) the setting's sense is the glass's: "normal" has the camera at the top, as a
 * user holds it. that is the drawing's turned-over way (s_flip 1 here and in ovl.c); the setting's "flipped"
 * is the way this build first drew it, the camera at the bottom. jc_touch turns the touch to match */
void disp_flip(int on)
{
    s_flip = on ? 0 : 1;
    ovl_flip(!on);
}

int disp_profiles(void) { return 1; }
void disp_profile(int p) { (void)p; }
const char *disp_profile_name(void) { return "DSI"; }
void disp_pack_under(const canvas_t *under) { disp_strip(under); }
void disp_strip2(const canvas_t *cv, int under_rows) { (void)under_rows; disp_strip(cv); }
int disp_rows(int rows) { (void)rows; return 1; }
uint32_t disp_frame_ms(void) { return 0; }
void disp_full_next(void) { }
uint32_t disp_lost(void) { return 0; }
void disp_wait_idle(void) { }

#endif
