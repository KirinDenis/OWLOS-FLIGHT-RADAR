/* Copyright (c) 2026 Michael Caldwell-Waller (chipguyhere). MIT License, see LICENSE in the repository root. */
/* the moving layer, see ovl.h. fork addition */
#include "board.h"
#if RADAR_OVERLAY

#include <math.h>
#include <string.h>
#include "ovl.h"
#include "busy.h"
#include "esp_rom_sys.h"
#ifndef OVL_PLANNER_PIE
#define OVL_PLANNER_PIE 1                              // the planner may use PIE: needs the IDF's PIE context fix (below)
#endif
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "ovl";

/* the fill's lines (fork addition, the JC8012P4A1C): on the S3 boards the panel is fed a row of the
 * picture at a time; where the panel stands and the picture lies (config.h RADAR_OVERLAY_COLS) it is
 * fed a column at a time. everything below that says "row" of what the fill does means a line: a
 * column of the picture there, its pixels top to bottom. OVL_REV: along a line, the panel runs the
 * other way from the picture when it is not turned over */
#if RADAR_OVERLAY_COLS
#define OVL_LINES GFX_PW
#define OVL_LEN   GFX_PH
#define OVL_REV   1
#else
#define OVL_LINES GFX_PH
#define OVL_LEN   GFX_PW
#define OVL_REV   0
#endif
/* 128-bit SIMD for the long runs (fork addition): the S3's PIE, or the P4's (Xesppie), which differs */
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
#define OVL_PIE 1
#else
#define OVL_PIE 0
#endif

/* a pixel of the layer, as the fill wants it: its colour (already scaled by its coverage) spread out
 * as 565 with room between the fields, 0x07E0F81F, and in the free bits 5..10 how much of what is
 * under it shows, 0..32. a run is a word x | n << 16 and its n pixels. the rows' starts are in the
 * RAM, the runs in the PSRAM */
#define SPREAD   0x07E0F81Fu
#define OVF      0x08010020u
#if RADAR_OVERLAY_COLS
#define OVL_CAP  (128 * 1024)                 // words a layer, 512 KB of PSRAM each (the JC8012's 32 MB; 96 K till its
                                              // padded runs and 8-pixel PIE minimum, 2026-11-14)
#else
#define OVL_CAP  (96 * 1024)                  // words a layer, 384 KB of PSRAM each
#endif
#define LEVELS   32                           // steps of the trail's fading
/* the trail on every row (1) or every other (2): half the sweep's work in the fill, a faint scanline
 * look in the trail. the bright edge is on every row either way */
#ifndef OVL_SWEEP_EVERY
#define OVL_SWEEP_EVERY 2
#endif

/* the busiest rows (fork addition): where putting the layer on would take the fill too long, the row is
 * made whole when the layer is made (the background's row with the layer on it) and kept here; the
 * fill then only copies it in and adds the sweep */
#ifndef OVL_BUSY_ROWS
#define OVL_BUSY_ROWS   192                            // rows a layer may keep made, 2 KB each, PSRAM
#endif
#ifndef OVL_BUSY_CYCLES
#define OVL_BUSY_CYCLES 4000                           // a row whose layer would take the fill more than this (~17 us)
#endif

/* the busiest rows made whole are the S3 builds' (by columns, the heaviest chunks are composed beforehand
 * instead): their tables are a word there, ~30 KB of internal RAM back */
#if RADAR_OVERLAY_COLS
#define OVL_BUSY_N 1
#else
#define OVL_BUSY_N OVL_LINES
#endif
typedef struct {
    uint32_t  *w;
    uint32_t   n;
    uint32_t   row[OVL_LINES + 1];
    ovl_beam_t beam;
    int16_t    busy[OVL_BUSY_N];                       // the row's place in rows, -1: not made
    int16_t    bx0[OVL_BUSY_N], bx1[OVL_BUSY_N];       // the stretch of it the layer covers, as the panel has it
    uint16_t   cost[OVL_LINES];                           // what the row's layer costs the fill, cycles / 4: core 0 picks chunks by it without reading the PSRAM
    uint16_t  *rows;                                   // OVL_BUSY_ROWS rows as the panel shows them
    int        nbusy;
} layer_t;

static layer_t      s_l[4];                         // shown, ready, planned for the next frame, being made
static int          s_show = -1, s_ready = -1, s_build = -1;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int          s_flip, s_next_y, s_full;
static TaskHandle_t s_waiter;
/* the next panel frame as core 0 planned it (ovl_plan_next): which layer and where the beam is. the
 * fill takes it at that frame's start when it is for that frame, so what core 0 composed beforehand
 * and what the fill puts on agree */
static volatile uint32_t s_plan_frame = 0xFFFFFFFFu;
static volatile int      s_plan_layer = -1;
static float             s_plan_ang;
static volatile int64_t  s_latch_us;                   // when the fill took the frame on the panel
#if RADAR_OVERLAY_COLS
#define PRE_ROWS   16                                  // a chunk: the JC8012's raster chunk (lcd_jc8012.c CHUNK)
#define PRE_LEAD   3                                   // the fill makes chunk c when chunk c - 3 has been read (4 buffers)
#define PRE_PICK   10                                  // ~2 x 10 x 25 KB of PSRAM. 16 (2026-11-14): ~3,000 late chunks per 10 s, the PSRAM traffic
#define PRE_CYCLES 50000                               // a chunk that would take the fill more than this (~140 us of its ~235)
#else
#define PRE_ROWS   10                                  // a chunk: the library's bounce rows (LCD7_7B_BOUNCE_ROWS)
#define PRE_LEAD   2
#endif
#define PRE_CHUNKS (OVL_LINES / PRE_ROWS)
#ifndef PRE_PICK
#define PRE_PICK   8                                   // the heaviest chunks composed a frame: ~40 KB of PSRAM traffic each
#endif
#define PRE_MAX    PRE_PICK                            // their buffers, 2 x 8 x 20 KB of PSRAM (24 of them left the PSRAM short)
#ifndef PRE_CYCLES
#define PRE_CYCLES 40000                               // a chunk that would take the fill more than this (~170 us)
#endif
_Static_assert(OVL_LINES % PRE_ROWS == 0, "the chunks must fill the frame");
static uint16_t *s_pre[2][PRE_MAX];                     // by the frame's parity
static volatile int8_t   s_pre_slot[2][PRE_CHUNKS];     // the chunk's buffer, -1: not composed
static volatile uint8_t  s_pre_what[2][PRE_CHUNKS];     // what is in it: PRE_SWEEP, PRE_LAYER
#define PRE_SWEEP 1
#define PRE_LAYER 2
#ifndef PRE_LAYER_CYCLES
#define PRE_LAYER_CYCLES 20000                         // the layer is composed too only where it costs this much (many planes)
#endif
static TaskHandle_t s_planner;
static volatile uint32_t s_pre_for[2];                  // the frame those are for
/* (fork addition, 2026-11-14) the chunks the fill will compose itself and that are heavy: over PRE_CYCLES but
 * not composed beforehand (the runners-up, or picked and not done in time). PSRAM work (the moving layer's
 * build, the background's turns) keeps off them: ovl_chunk_hot */
static volatile uint8_t  s_hot[2][PRE_CHUNKS];
volatile uint32_t g_plan_hot, g_plan_heavy, g_plan_made, g_plans;   // TEMP: summed for the 10 s log (lcd_jc8012.c)
static volatile uint32_t s_hot_for[2];

int ovl_chunk_hot(uint32_t frame, int c)
{
    const int p = frame & 1;
    return c >= 0 && c < PRE_CHUNKS && s_hot_for[p] == frame && s_hot[p][c];
}
static void (*s_wb)(const void *addr, size_t len);     // written back from the cache: the DMA reads the PSRAM
static uint32_t s_pre_cost[PRE_CHUNKS];                 // the planner's last estimate of each chunk, for the log
static uint16_t s_pre_count[PRE_CHUNKS];                // how often each was composed beforehand

static volatile uint32_t s_frame = 0xFFFFFFFFu;
static ovl_stats_t  s_st;
static const uint16_t *(*s_src)(int py);              // the background's row py as the panel shows it now (lcd_wave7.c)
static void busy_render(layer_t *L);
#include "esp_cpu.h"

static const DRAM_ATTR uint8_t BAYER[4][4] = {      // gfx_pack565's
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
};

/* --- runs of the layer as two 16-bit arrays (fork addition): the colour, premultiplied 565, and how
 * much of what is under it shows, 0..32. both start at the same offset within 16 bytes as the panel
 * row's pixel they go to, so the S3's 128-bit SIMD (PIE) can take them 8 pixels at a time. a run
 * word: x | n << 16 (11 bits) | pad << 27 (16-bit entries before the colours) | PLANAR. shorter runs
 * stay one 32-bit word a pixel */
#define PLANAR   0x40000000u
#define PIE_MIN  16                                    // pixels a run needs to be stored for PIE
#define PIE_MIN_COLS 8                                 // (fork addition) by columns: runs are padded to whole blocks
                                                       // (pad2_*), so a short run costs a block or two of PIE

static inline unsigned planar_q(unsigned n) { return (16u - ((2u * n) & 15u)) & 15u; }   // bytes between the arrays
static inline unsigned planar_words(unsigned p, unsigned n) { return (2u * p + 4u * n + planar_q(n) + 3u) >> 2; }
#if RADAR_OVERLAY_COLS
/* (fork addition, 2026-11-14) by columns, a long run is kept in whole 8-pixel blocks on the bounce buffer's
 * 16-byte grid: both arrays start on a 16-byte boundary (gap: the 16-bit entries skipped after the run word to
 * get there, worked out from where the words are: the fill's copy keeps their place within 128 bytes), the
 * run's pixels from head = its first pixel's place in memory & 7, and every other entry the identity (colour 0,
 * all of what is under it shows: what is there stays exactly). the fill then puts a run on in one kernel call,
 * no C for its ends (they were ~45 cycles a pixel); what it writes past the run is in the same panel row, and
 * written with what was there. the run word: y (15 bits) | which way round << 15 | n << 16 | PLANAR */
static inline unsigned pad2_head(uint32_t h, unsigned n)
{
    const unsigned y = h & 0x7FFF, p0 = ((h >> 15) & 1) ? (unsigned)(OVL_LEN - 1) - (y + n - 1) : y;
    return p0 & 7u;
}
static inline unsigned pad2_len(uint32_t h, unsigned n) { return (pad2_head(h, n) + n + 7u) & ~7u; }
static inline unsigned pad2_gap(const void *after) { return ((16u - ((unsigned)(uintptr_t)after & 15u)) & 15u) >> 1; }
static inline unsigned pad2_words(const void *after, uint32_t h, unsigned n) { return (2u * pad2_gap(after) + 4u * pad2_len(h, n) + 3u) >> 2; }
#endif

// one pixel, as the C composite does it: c over *d, *d showing through u/32 of it
static IRAM_ATTR inline void pix(uint16_t *d, uint16_t c, unsigned u)
{
    uint32_t o = (c | ((uint32_t)c << 16)) & SPREAD;
    if (u) o += ((((*d | ((uint32_t)*d << 16)) & SPREAD) * u) >> 5) & SPREAD;
    if (o & OVF) {
        if (o & 0x20u) o |= 0x1Fu;
        if (o & 0x10000u) o |= 0xF800u;
        if (o & 0x08000000u) o |= 0x07E00000u;
    }
    o &= SPREAD;
    *d = (uint16_t)(o | (o >> 16));
}

#if OVL_PIE && CONFIG_IDF_TARGET_ESP32S3
/* the same, 8 pixels at a time. only the instructions chipguyhere's RasterBouncingBalls already runs on
 * this chip (vld/vst.128, andq, xorq, vmul.u16, vadds.s16, loopgtz, ssai). per field: masked out and
 * moved down with a multiply (every product below 65536, so nothing hangs on how vmul.u16 rounds),
 * scaled by what shows through, the layer's added, held to the field's top with two saturating adds
 * (+(0x7FFF - top), then back), moved up again, and the three fields put together. bit for bit what
 * pix does: checked at start (ovl_init), the C one is used if it differs */
static const uint16_t DRAM_ATTR __attribute__((aligned(16))) PIE_K[15][8] = {
#define V(v) { v, v, v, v, v, v, v, v }
    V(0xF800), V(1), V(0x7FFF - 31), V((uint16_t)-(0x7FFF - 31)), V(2048),     // red: down 11
    V(0x07E0), V(1), V(0x7FFF - 63), V((uint16_t)-(0x7FFF - 63)), V(32),       // green: down 5
    V(0x001F), V(1), V(0x7FFF - 31), V((uint16_t)-(0x7FFF - 31)), V(1),        // blue
#undef V
};
#define PIE_FIELD(DOWN, REWIND) \
    "ee.vld.128.ip q6, %[k], 16\n"  "ee.andq q4, q0, q6\n"  "ee.andq q5, q2, q6\n" \
    "ee.vld.128.ip q7, %[k], 16\n"  "ssai " DOWN "\n" \
    "ee.vmul.u16 q4, q4, q7\n"     "ee.vmul.u16 q5, q5, q7\n" \
    "ssai 5\n"                     "ee.vmul.u16 q4, q4, q1\n"  "ee.vadds.s16 q4, q4, q5\n" \
    "ee.vld.128.ip q7, %[k], 16\n"  "ee.vadds.s16 q4, q4, q7\n" \
    "ee.vld.128.ip q7, %[k], 16\n"  "ee.vadds.s16 q4, q4, q7\n" \
    "ee.vld.128.ip q7, %[k], " REWIND "\n"  "ssai 0\n"  "ee.vmul.u16 q4, q4, q7\n" \
    "ee.xorq q3, q3, q4\n"

__attribute__((noinline)) static IRAM_ATTR void pie_blend(uint16_t *d, const uint16_t *c, const uint16_t *u, int n8)
{
    const uint16_t *k = &PIE_K[0][0];
    asm volatile("loopgtz %[n], 1f\n"
                 "ee.vld.128.ip q0, %[d], 0\n"
                 "ee.vld.128.ip q1, %[u], 16\n"
                 "ee.vld.128.ip q2, %[c], 16\n"
                 "ee.xorq q3, q3, q3\n"
                 PIE_FIELD("11", "16")
                 PIE_FIELD("5", "16")
                 PIE_FIELD("0", "-224")
                 "ee.vst.128.ip q3, %[d], 16\n"
                 "1:\n"
                 : [d] "+r"(d), [u] "+r"(u), [c] "+r"(c), [n] "+r"(n8), [k] "+r"(k) :: "memory");
}
#elif OVL_PIE
/* the P4's (fork addition, the JC8012P4A1C): the S3 kernel's steps in the P4's own instructions (esp.*),
 * with the field held to its top by a vmin instead of two saturating adds. per field: masked out, moved
 * down (vmul.u16 by 1, the shift from SAR), scaled by what shows through (vmul.u16, SAR 5), the layer's
 * added, held to the top, moved up (vmul.u16 by 2^shift, SAR 0) and or'ed in. checked against pix at
 * start, as on the S3 */
static const uint16_t DRAM_ATTR __attribute__((aligned(16))) PIE_K[12][8] = {
#define V(v) { v, v, v, v, v, v, v, v }
    V(0xF800), V(1), V(31), V(2048),                   // red: down 11
    V(0x07E0), V(1), V(63), V(32),                     // green: down 5
    V(0x001F), V(1), V(31), V(1),                      // blue
#undef V
};
#define PIE_FIELD(DOWN, REWIND) \
    "esp.vld.128.ip q6, %[k], 16\n"  "esp.andq q4, q0, q6\n"  "esp.andq q5, q2, q6\n" \
    "esp.vld.128.ip q7, %[k], 16\n"  "li %[t], " DOWN "\n"  "esp.movx.w.sar %[t]\n" \
    "esp.vmul.u16 q4, q4, q7\n"     "esp.vmul.u16 q5, q5, q7\n" \
    "li %[t], 5\n"  "esp.movx.w.sar %[t]\n"   "esp.vmul.u16 q4, q4, q1\n"  "esp.vadd.u16 q4, q4, q5\n" \
    "esp.vld.128.ip q7, %[k], 16\n"  "esp.vmin.u16 q4, q4, q7\n" \
    "esp.vld.128.ip q7, %[k], " REWIND "\n"  "li %[t], 0\n"  "esp.movx.w.sar %[t]\n"  "esp.vmul.u16 q4, q4, q7\n" \
    "esp.orq q3, q3, q4\n"

__attribute__((noinline)) static IRAM_ATTR void pie_blend(uint16_t *d, const uint16_t *c, const uint16_t *u, int n8)
{
    /* the P4's vector instructions take only x8..x15 for their address and SAR registers: a0..a5 here */
    register uint16_t *rd asm("a0") = d;
    register const uint16_t *ru asm("a1") = u, *rc asm("a2") = c, *rk asm("a3") = &PIE_K[0][0];
    register int t asm("a4");
    for (; n8 > 0; n8--) {
        asm volatile("esp.vld.128.ip q0, %[d], 0\n"
                     "esp.vld.128.ip q1, %[u], 16\n"
                     "esp.vld.128.ip q2, %[c], 16\n"
                     "esp.zero.q q3\n"
                     PIE_FIELD("11", "16")
                     PIE_FIELD("5", "16")
                     PIE_FIELD("0", "-176")
                     "esp.vst.128.ip q3, %[d], 16\n"
                     : [d] "+r"(rd), [u] "+r"(ru), [c] "+r"(rc), [k] "+r"(rk), [t] "=&r"(t)
                     :: "memory");
    }
}
/* (fork addition, 2026-11-14) a solid run: n alike, one colour and one see-through amount for all of it. the
 * blend's steps as pie_blend, with the colour and the amount loaded from 8-wide copies with no step, so only
 * the background moves. translucent solid runs (glows, rings, panels) were ~20 cycles a pixel in C */
#define HAVE_PIE_SOLID 1
/* (fork addition, 2026-11-14) the kernel again, SAR at 5 throughout: each field is scaled where it lies.
 * blue (b*u)>>5 and green ((g<<5)*u)>>5 = g*u, masked to 0xFFE0, fit 16 bits as they are; red ((r<<11)*u)>>5
 * = (r*u)<<6 (at most 63,488), masked to 0xF800 it is ((r*u)>>5)<<11, what pix's spread word gives. blue and
 * green are held to their tops by vmin; red, at the top of the word, by a compare (the sum wrapped: below
 * what was added) or'ed in. one SAR write a block instead of nine, ~30 instructions instead of ~60 */
static const uint16_t DRAM_ATTR __attribute__((aligned(16))) PIE_K2[4][8] = {
#define V(v) { v, v, v, v, v, v, v, v }
    V(0xF800), V(0x07E0), V(0xFFE0), V(0x001F),
#undef V
};
#define PIE2_BODY \
    "li %[t], 5\n"  "esp.movx.w.sar %[t]\n" \
    "esp.zero.q q3\n" \
    /* red */ \
    "esp.vld.128.ip q7, %[k], 16\n"  "esp.andq q4, q0, q7\n"  "esp.vmul.u16 q4, q4, q1\n"  "esp.andq q4, q4, q7\n" \
    "esp.andq q5, q2, q7\n"  "esp.vadd.u16 q4, q4, q5\n"  "esp.vcmp.lt.u16 q6, q4, q5\n"  "esp.orq q4, q4, q6\n" \
    "esp.andq q3, q4, q7\n" \
    /* green */ \
    "esp.vld.128.ip q7, %[k], 16\n"  "esp.andq q4, q0, q7\n"  "esp.andq q5, q2, q7\n" \
    "esp.vld.128.ip q6, %[k], 16\n"  "esp.vmul.u16 q4, q4, q1\n"  "esp.andq q4, q4, q6\n" \
    "esp.vadd.u16 q4, q4, q5\n"  "esp.vmin.u16 q4, q4, q7\n"  "esp.orq q3, q3, q4\n" \
    /* blue */ \
    "esp.vld.128.ip q7, %[k], -48\n"  "esp.andq q4, q0, q7\n"  "esp.vmul.u16 q4, q4, q1\n"  "esp.andq q5, q2, q7\n" \
    "esp.vadd.u16 q4, q4, q5\n"  "esp.vmin.u16 q4, q4, q7\n"  "esp.orq q3, q3, q4\n"

__attribute__((noinline)) static IRAM_ATTR void pie_blend2(uint16_t *d, const uint16_t *c, const uint16_t *u, int n8)
{
    register uint16_t *rd asm("a0") = d;
    register const uint16_t *ru asm("a1") = u, *rc asm("a2") = c, *rk asm("a3") = &PIE_K2[0][0];
    register int t asm("a4");
    for (; n8 > 0; n8--) {
        asm volatile("esp.vld.128.ip q0, %[d], 0\n"
                     "esp.vld.128.ip q1, %[u], 16\n"
                     "esp.vld.128.ip q2, %[c], 16\n"
                     PIE2_BODY
                     "esp.vst.128.ip q3, %[d], 16\n"
                     : [d] "+r"(rd), [u] "+r"(ru), [c] "+r"(rc), [k] "+r"(rk), [t] "=&r"(t)
                     :: "memory");
    }
}

__attribute__((noinline)) static IRAM_ATTR void pie_solid2(uint16_t *d, uint16_t c, uint16_t u, int n8)
{
    uint16_t cc[8] __attribute__((aligned(16))), uu[8] __attribute__((aligned(16)));
    for (int i = 0; i < 8; i++) { cc[i] = c; uu[i] = u; }
    register uint16_t *rd asm("a0") = d;
    register const uint16_t *ru asm("a1") = uu, *rc asm("a2") = cc, *rk asm("a3") = &PIE_K2[0][0];
    register int t asm("a4");
    for (; n8 > 0; n8--) {
        asm volatile("esp.vld.128.ip q0, %[d], 0\n"
                     "esp.vld.128.ip q1, %[u], 0\n"
                     "esp.vld.128.ip q2, %[c], 0\n"
                     PIE2_BODY
                     "esp.vst.128.ip q3, %[d], 16\n"
                     : [d] "+r"(rd), [u] "+r"(ru), [c] "+r"(rc), [k] "+r"(rk), [t] "=&r"(t)
                     :: "memory");
    }
}
#define HAVE_PIE2 1
__attribute__((noinline)) static IRAM_ATTR void pie_solid(uint16_t *d, uint16_t c, uint16_t u, int n8)
{
    uint16_t cc[8] __attribute__((aligned(16))), uu[8] __attribute__((aligned(16)));
    for (int i = 0; i < 8; i++) { cc[i] = c; uu[i] = u; }
    register uint16_t *rd asm("a0") = d;
    register const uint16_t *ru asm("a1") = uu, *rc asm("a2") = cc, *rk asm("a3") = &PIE_K[0][0];
    register int t asm("a4");
    for (; n8 > 0; n8--) {
        asm volatile("esp.vld.128.ip q0, %[d], 0\n"
                     "esp.vld.128.ip q1, %[u], 0\n"
                     "esp.vld.128.ip q2, %[c], 0\n"
                     "esp.zero.q q3\n"
                     PIE_FIELD("11", "16")
                     PIE_FIELD("5", "16")
                     PIE_FIELD("0", "-176")
                     "esp.vst.128.ip q3, %[d], 16\n"
                     : [d] "+r"(rd), [u] "+r"(ru), [c] "+r"(rc), [k] "+r"(rk), [t] "=&r"(t)
                     :: "memory");
    }
}
#endif
#if OVL_PIE

static int s_pie;                                      // the PIE path agreed with the C one at start
#ifndef HAVE_PIE_SOLID
#define HAVE_PIE_SOLID 0
#endif
static int s_pie_solid;                                // (fork addition) and pie_solid did too
#ifndef HAVE_PIE2
#define HAVE_PIE2 0
#endif
static int s_pie2;                                     // (fork addition) the SAR-once kernels agreed with C: used instead
/* the blend and the solid run, by whichever kernel passed */
static IRAM_ATTR inline void blend_n8(uint16_t *d, const uint16_t *c, const uint16_t *u, int n8)
{
#if HAVE_PIE2
    if (s_pie2) { pie_blend2(d, c, u, n8); return; }
#endif
    pie_blend(d, c, u, n8);
}

static void pie_selftest(void)
{
    static DRAM_ATTR uint16_t __attribute__((aligned(16))) d1[64], d2[64], cc[64], uu[64];
    uint32_t seed = 12345;
    for (int round = 0; round < 64; round++) {
        for (int i = 0; i < 64; i++) {
            seed = seed * 1103515245u + 12345u;
            d1[i] = d2[i] = (uint16_t)(seed >> 8);
            seed = seed * 1103515245u + 12345u;
            cc[i] = (uint16_t)(seed >> 8);
            seed = seed * 1103515245u + 12345u;
            uu[i] = (uint16_t)((seed >> 16) % 33);
        }
        for (int i = 0; i < 64; i++) pix(&d1[i], cc[i], uu[i]);
        pie_blend(d2, cc, uu, 8);
        for (int i = 0; i < 64; i++) {
            if (d1[i] != d2[i]) {
                ESP_LOGW(TAG, "PIE composite differs from C at pixel %d of round %d (colour %04x, shows %u: C %04x, PIE %04x): C only",
                         i, round, (unsigned)cc[i], (unsigned)uu[i], (unsigned)d1[i], (unsigned)d2[i]);
                s_pie = 0;
                return;
            }
        }
    }
    s_pie = 1;
    ESP_LOGI(TAG, "PIE composite matches C on 4096 pixels: in use");
#if HAVE_PIE_SOLID
    for (int round = 0; round < 64; round++) {         // the solid runs' kernel, the same way
        seed = seed * 1103515245u + 12345u;
        const uint16_t c = (uint16_t)(seed >> 8);
        seed = seed * 1103515245u + 12345u;
        const uint16_t u = (uint16_t)((seed >> 16) % 33);
        for (int i = 0; i < 64; i++) {
            seed = seed * 1103515245u + 12345u;
            d1[i] = d2[i] = (uint16_t)(seed >> 8);
        }
        for (int i = 0; i < 64; i++) pix(&d1[i], c, u);
        pie_solid(d2, c, u, 8);
        for (int i = 0; i < 64; i++) {
            if (d1[i] != d2[i]) {
                ESP_LOGW(TAG, "PIE solid run differs from C at pixel %d of round %d: C only for those", i, round);
                return;
            }
        }
    }
    s_pie_solid = 1;
    ESP_LOGI(TAG, "PIE solid runs match C on 4096 pixels: in use");
#endif
#if HAVE_PIE2
    {   /* the SAR-once kernels: every colour against every amount 0..32 over random backgrounds, and random runs */
        int ok = 1;
        for (int round = 0; round < 256 && ok; round++) {
            for (int i = 0; i < 64; i++) {
                seed = seed * 1103515245u + 12345u;
                d1[i] = d2[i] = (uint16_t)(seed >> 8);
                seed = seed * 1103515245u + 12345u;
                cc[i] = (uint16_t)(seed >> 8);
                seed = seed * 1103515245u + 12345u;
                uu[i] = (uint16_t)((seed >> 16) % 33);
            }
            if (round < 32) for (int i = 0; i < 64; i++) { cc[i] = round & 1 ? 0xFFFF : cc[i]; uu[i] = (uint16_t)((round / 2) * 2 + (i & 1)); }
            for (int i = 0; i < 64; i++) pix(&d1[i], cc[i], uu[i]);
            pie_blend2(d2, cc, uu, 8);
            for (int i = 0; i < 64 && ok; i++)
                if (d1[i] != d2[i]) {
                    ESP_LOGW(TAG, "PIE kernel 2 differs from C at pixel %d of round %d (colour %04x, shows %u, under %04x: C %04x, PIE %04x)",
                             i, round, (unsigned)cc[i], (unsigned)uu[i], 0u, (unsigned)d1[i], (unsigned)d2[i]);
                    ok = 0;
                }
            seed = seed * 1103515245u + 12345u;
            const uint16_t c = (uint16_t)(seed >> 8), u = (uint16_t)((seed >> 16) % 33);
            for (int i = 0; i < 64; i++) { seed = seed * 1103515245u + 12345u; d1[i] = d2[i] = (uint16_t)(seed >> 8); }
            for (int i = 0; i < 64; i++) pix(&d1[i], c, u);
            pie_solid2(d2, c, u, 8);
            for (int i = 0; i < 64 && ok; i++)
                if (d1[i] != d2[i]) {
                    ESP_LOGW(TAG, "PIE solid kernel 2 differs from C at pixel %d of round %d (colour %04x, shows %u: C %04x, PIE %04x)",
                             i, round, (unsigned)c, (unsigned)u, (unsigned)d1[i], (unsigned)d2[i]);
                    ok = 0;
                }
        }
        s_pie2 = ok;
        if (ok) ESP_LOGI(TAG, "PIE kernel 2 (SAR once) matches C on 32768 pixels: in use");
    }
#endif
}
#else
static const int s_pie = 0;
static void pie_blend(uint16_t *d, const uint16_t *c, const uint16_t *u, int n8) { (void)d; (void)c; (void)u; (void)n8; }
static inline void blend_n8(uint16_t *d, const uint16_t *c, const uint16_t *u, int n8) { pie_blend(d, c, u, n8); }
#endif

static uint32_t *s_tmp;                                // a run's words while they are rearranged (PSRAM)

#if RADAR_OVERLAY_COLS
static uint32_t *s_stage;                              // the layer being made, band by band, before it is sorted by columns (PSRAM)
static uint32_t *s_cols;                               // and sorted, before the long runs are laid out for PIE (PSRAM)
#endif

int ovl_init(void)
{
#if OVL_PIE
    pie_selftest();
#endif
#if RADAR_OVERLAY_COLS
    /* by columns: no busiest rows made whole (rows stays NULL); the heaviest chunks of each next frame
     * are composed beforehand (the planner), into buffers on the cache line, for the fill's GDMA */
    for (int p = 0; p < 2; p++)
        for (int k = 0; k < PRE_MAX; k++)
            if (!s_pre[p][k]) s_pre[p][k] = heap_caps_aligned_alloc(128, (size_t)PRE_ROWS * OVL_LEN * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    /* the band's runs wait in s_stage till the layer is whole */
    if (!s_stage) s_stage = heap_caps_malloc(OVL_CAP * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!s_cols) s_cols = heap_caps_malloc(OVL_CAP * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!s_stage || !s_cols) {
        ESP_LOGE(TAG, "no PSRAM for the moving layer");
        return 0;
    }
#else
    for (int p = 0; p < 2; p++)
        for (int k = 0; k < PRE_MAX; k++)
            if (!s_pre[p][k]) s_pre[p][k] = heap_caps_aligned_alloc(64, (size_t)PRE_ROWS * GFX_PW * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
#endif
    for (int p = 0; p < 2; p++) for (int c = 0; c < PRE_CHUNKS; c++) s_pre_slot[p][c] = -1;
    if (!s_tmp) s_tmp = heap_caps_malloc(GFX_PW * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    for (int i = 0; i < 4; i++) {
#if RADAR_OVERLAY_COLS
        /* on the cache line: the JC8012's fill has the GDMA fetch a chunk's words (ovl_chunk_words) */
        if (!s_l[i].w) s_l[i].w = heap_caps_aligned_alloc(128, OVL_CAP * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
#else
        if (!s_l[i].w) s_l[i].w = heap_caps_malloc(OVL_CAP * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
#endif
        if (!s_l[i].w) {
            ESP_LOGE(TAG, "no PSRAM for the moving layer");
            return 0;
        }
        s_l[i].n = 0;
        memset(s_l[i].row, 0, sizeof s_l[i].row);
        memset(s_l[i].busy, 0xFF, sizeof s_l[i].busy);
        s_l[i].nbusy = 0;
#if !RADAR_OVERLAY_COLS
        if (!s_l[i].rows)
            s_l[i].rows = heap_caps_aligned_alloc(16, (size_t)OVL_BUSY_ROWS * GFX_PW * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
#endif
    }
    return 1;
}

void ovl_flip(int on) { s_flip = on ? 1 : 0; }
void ovl_source(const uint16_t *(*row)(int py)) { s_src = row; }
void ovl_waiter(TaskHandle_t t) { s_waiter = t; }

void ovl_stats(ovl_stats_t *s, int reset)
{
    *s = s_st;
    if (reset) memset(&s_st, 0, sizeof s_st);
}

/* --- making it (the ui task) --- */

static ovl_beam_t s_bholes;                            // the holes of the layer being made
#if RADAR_OVERLAY_COLS
static uint32_t s_sn;                                  // words in s_stage
#endif

void ovl_build_begin(const ovl_beam_t *b)
{
    s_bholes = *b;
    portENTER_CRITICAL(&s_mux);
    int f = 0;
    while (f == s_show || f == s_ready || f == s_plan_layer) f++;   // of four, one is always free
    s_build = f;
    portEXIT_CRITICAL(&s_mux);
    s_l[f].n = 0;
    s_next_y = 0;
    s_full = 0;
#if RADAR_OVERLAY_COLS
    s_sn = 0;
#endif
}

static inline uint32_t to_word_d(uint32_t p, unsigned d)
{
    const unsigned a = p >> 24;
    unsigned r = (((p >> 16) & 255) + (d >> 1)) >> 3, g = (((p >> 8) & 255) + (d >> 2)) >> 2, b = ((p & 255) + (d >> 1)) >> 3;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    const unsigned under = 32 - ((a * 32 + 128) >> 8);
    if (under == 32 && !(r | g | b)) return 0;        // light too faint to show: nothing
    return (g << 21) | (r << 11) | b | (under << 5);
}

static inline uint32_t to_word(uint32_t p, int x, int y) { return to_word_d(p, BAYER[y & 3][x & 3]); }

// a run of n pixel words from x0, its run word at at: closed, and laid out as two arrays when long
static void close_run(layer_t *L, uint32_t at, int x0, uint32_t n)
{
    if (OVL_PIE && n >= PIE_MIN && s_tmp && L->n == at + 1 + n) {
        const uint8_t *base = (const uint8_t *)(L->w + at + 1);
        const unsigned pad = (((unsigned)(2 * x0) - (unsigned)(uintptr_t)base) & 15u) >> 1;
        const unsigned words = planar_words(pad, n);
        if (at + 1 + words <= OVL_CAP) {
            memcpy(s_tmp, L->w + at + 1, n * sizeof(uint32_t));
            uint16_t *col = (uint16_t *)(L->w + at + 1) + pad, *sh = col + n + planar_q(n) / 2;
            for (uint32_t i = 0; i < n; i++) {
                const uint32_t v = s_tmp[i];
                col[i] = (uint16_t)((((v >> 11) & 31) << 11) | (((v >> 21) & 63) << 5) | (v & 31));
                sh[i] = (uint16_t)((v >> 5) & 63);
            }
            L->w[at] = (uint32_t)x0 | (n << 16) | ((uint32_t)pad << 27) | PLANAR;
            L->n = at + 1 + words;
            return;
        }
    }
    L->w[at] = (uint32_t)x0 | (n << 16);
}

/* the planner's waits for the fill's runway: any build (the hook is NULL but on the JC8012) */
static int (*s_plan_quiet)(void);                      // (fork addition) lcd_jc8012.c runway_clear
#define PLAN_WAIT_US 4000                              // the planner's waits for the runway, at most, a frame
void ovl_planner_quiet(int (*quiet)(void)) { s_plan_quiet = quiet; }
volatile uint32_t g_plan_wait_us;                      // TEMP: the planner's waits, for the 10 s log

#if RADAR_OVERLAY_COLS
/* by columns (fork addition, the JC8012P4A1C). the ui task still draws the layer in bands of the
 * picture's rows; each band is read down its columns, and every column with anything in it leaves a
 * record in s_stage: its x and how many words follow, then its runs as the fill wants them (the S3
 * builds' run words, with y where they have x). once the layer is whole, ovl_build_end sorts the
 * records into columns, each column's runs top to bottom, so the fill finds a column's words in one
 * piece, as it finds a row's on the S3 */
static uint32_t s_colw[GFX_PW + 1];                    // a column's words, then where they go
static uint32_t s_lastrun[GFX_PW];                     // where the column's last run's word went
static uint32_t s_colrow[GFX_PW + 1];                  // each column's start in s_cols

/* the band 16 columns at a time (a 16 x 16 tile): its rows read across, which the cache likes, and an
 * empty tile is seen at once; then turned into t, where each column lies in one piece. down the band's
 * columns straight away, every step 5 KB on, it took ~30 ms a layer, and a pass to find the columns
 * with anything in them another ~25 (measured, 32 planes) */
#define TILE 16

/* the display's pacing (the JC8012's): called between pieces of making a layer, it returns at once
 * while the panel is reading what nothing goes over, and waits while the fill is busy with the chunks
 * the layer goes over: each piece of the layer's PSRAM traffic, ~4 times a second, had been taking from
 * the fill's (its glitches came in that rhythm, seen) */
static void (*s_pace)(void);
void ovl_build_pace(void (*pace)(void)) { s_pace = pace; }
static inline void pace(void) { if (s_pace) s_pace(); }
_Static_assert(GFX_PW % TILE == 0, "the bands are whole tiles across");

void ovl_build_band(canvas_t *cv, int x0, int x1)
{
    if (s_build < 0) return;
    pace();
    if (x0 < 0) x0 = 0;
    if (x1 > GFX_PW) x1 = GFX_PW;
    if (x1 < x0) x1 = x0;
    const int h = cv->h > TILE ? TILE : cv->h;
    for (int r = 0; r < cv->h; r++) {
        const int y = cv->y0 + r;
        uint32_t *row = cv->px + r * GFX_PW;
        /* outside the box nothing should be, but whatever is goes: the band is used again */
        if (x0 > 0) memset(row, 0, x0 * sizeof(uint32_t));
        if (x1 < GFX_PW) memset(row + x1, 0, (GFX_PW - x1) * sizeof(uint32_t));
        for (int k = 0; k < s_bholes.holes; k++) {     // the popups' boxes: nothing of the layer there
            if (y < s_bholes.hy0[k] || y >= s_bholes.hy1[k]) continue;
            int a = s_bholes.hx0[k] < 0 ? 0 : s_bholes.hx0[k], b = s_bholes.hx1[k] > GFX_PW ? GFX_PW : s_bholes.hx1[k];
            if (a < b) memset(row + a, 0, (b - a) * sizeof(uint32_t));
        }
    }
    static uint32_t t[TILE][TILE];                     // t[column][row]
    for (int tx = x0 & ~(TILE - 1); tx < x1; tx += TILE) {
        uint32_t any = 0;                              // the tile's rows with anything, a bit each
        for (int r = 0; r < h; r++) {
            uint32_t *q = cv->px + r * GFX_PW + tx;
            uint32_t o = 0;
            for (int c = 0; c < TILE; c++) o |= q[c];
            if (!o) continue;
            any |= 1u << r;
            for (int c = 0; c < TILE; c++) t[c][r] = q[c];
            memset(q, 0, TILE * sizeof(uint32_t));     // the band is left at 0 for the next one
        }
        if (!any) continue;
        for (int r = 0; r < h; r++)
            if (!(any >> r & 1)) for (int c = 0; c < TILE; c++) t[c][r] = 0;
        for (int c = 0; c < TILE; c++) {
            const int x = tx + c;
            const uint32_t *col = t[c];
            uint32_t o = 0;
            for (int r = 0; r < h; r++) o |= col[r];
            if (!o) continue;
            const uint32_t rec = s_sn;
            int room = !s_full && rec + 3 <= OVL_CAP;  // the record's word and a run's two at least
            if (room) s_sn++;
            const int reserved = room;
            int open = 0, ry = 0;                      // a run of pixel words being stored: from row ry
            uint32_t at = 0, n = 0;
            for (int r = 0; r < h;) {
                const uint32_t p = col[r];
                if (!p) {
                    if (open) { s_stage[at] = (uint32_t)ry | (n << 16); open = 0; }
                    r++;
                    continue;
                }
                int same = 1;                          // the same pixel again and again (a label's card): a run of its own
                while (r + same < h && col[r + same] == p) same++;
                if (same >= 8) {
                    if (open) { s_stage[at] = (uint32_t)ry | (n << 16); open = 0; }
                    const uint32_t w = to_word_d(p, 8);   // one flat colour: no dither, so one word stands for all
                    if (w && room && s_sn + 2 <= OVL_CAP) {
                        s_stage[s_sn++] = (uint32_t)(cv->y0 + r) | ((uint32_t)same << 16) | 0x80000000u;
                        s_stage[s_sn++] = w;
                    } else if (w) {
                        room = 0;
                        s_full = 1;
                    }
                    r += same;
                    continue;
                }
                const int y = cv->y0 + r;
                const uint32_t w = to_word(p, x, y);
                r++;
                if (!w) {                              // nothing to show here: the run ends, the next starts after it
                    if (open) { s_stage[at] = (uint32_t)ry | (n << 16); open = 0; }
                    continue;
                }
                if (!room) continue;                   // the layer is full: the rest is dropped
                if (!open) {
                    if (s_sn + 2 > OVL_CAP) { room = 0; s_full = 1; continue; }
                    at = s_sn++;
                    n = 0;
                    ry = y;
                    open = 1;
                } else if (s_sn + 1 > OVL_CAP) {
                    s_stage[at] = (uint32_t)ry | (n << 16);
                    open = 0;
                    room = 0;
                    s_full = 1;
                    continue;
                }
                s_stage[s_sn++] = w;
                n++;
            }
            if (open) s_stage[at] = (uint32_t)ry | (n << 16);
            if (reserved) {
                if (s_sn == rec + 1) s_sn = rec;       // all of it too faint: no record
                else s_stage[rec] = (uint32_t)x | ((s_sn - rec - 1) << 16);
            }
        }
    }
}

void ovl_build_end(const ovl_beam_t *b)
{
    if (s_build < 0) return;
    layer_t *L = &s_l[s_build];
    /* the records sorted into columns (into s_cols): count each column's words, lay the columns out one
     * after the other, then copy each record to the end of its column. bands came top to bottom, so a
     * column's runs stay in order */
    memset(s_colw, 0, sizeof s_colw);
    for (uint32_t i = 0; i < s_sn;) {
        const uint32_t h = s_stage[i];
        s_colw[h & 0xFFFF] += h >> 16;
        i += 1 + (h >> 16);
    }
    uint32_t at = 0;
    for (int x = 0; x < GFX_PW; x++) {
        const uint32_t n = s_colw[x];
        s_colrow[x] = s_colw[x] = at;
        at += n;
    }
    s_colrow[GFX_PW] = at;
    /* a run that goes on where the column's last one stopped (the bands are 16 rows: a plane's glow or
     * a label's card down a column was cut into pieces) is joined onto it, so the long runs are long
     * enough for PIE (measured: none longer than 16 before). the words left over at a column's end
     * become 0, a run of nothing */
    for (int x = 0; x < GFX_PW; x++) s_lastrun[x] = UINT32_MAX;
    uint32_t *w = s_cols;
    uint32_t paced = 0;
    for (uint32_t i = 0; i < s_sn;) {
        if (i - paced >= 4096) { pace(); paced = i; }  // every 16 KB of records
        const uint32_t h = s_stage[i], x = h & 0xFFFF, n = h >> 16;
        const uint32_t *r = s_stage + i + 1, *e = r + n;
        uint32_t cur = s_colw[x];
        while (r < e) {
            const uint32_t rh = *r++, y = rh & 0xFFFF, k = (rh >> 16) & 0x7FF, fill = rh & 0x80000000u;
            const uint32_t lr = s_lastrun[x];
            if (lr != UINT32_MAX) {
                const uint32_t lh = w[lr], ly = lh & 0xFFFF, lk = (lh >> 16) & 0x7FF;
                if (ly + lk == y && lk + k <= 0x7FF && (lh & 0x80000000u) == fill && (!fill || w[lr + 1] == r[0])) {
                    w[lr] = (lh & ~(0x7FFu << 16)) | ((lk + k) << 16);
                    if (!fill) { memcpy(w + cur, r, k * sizeof(uint32_t)); cur += k; }
                    r += fill ? 1 : k;
                    continue;
                }
            }
            s_lastrun[x] = cur;
            w[cur++] = rh;
            const uint32_t m = fill ? 1 : k;
            memcpy(w + cur, r, m * sizeof(uint32_t));
            cur += m;
            r += m;
        }
        s_colw[x] = cur;
        i += 1 + n;
    }
    /* then into the layer, column by column: the runs of PIE_MIN pixels or more as two 16-bit arrays
     * (the colour, how much shows through) in the panel's own order, lined up with the bounce buffer's
     * 16-byte boundaries, as the S3 builds keep their long runs */
    const int rev = s_flip ^ OVL_REV;
    uint32_t out = 0;
    for (int x = 0; x < GFX_PW; x++) {
        if (!(x & 63)) pace();
        L->row[x] = out;
        const uint32_t *r = s_cols + s_colrow[x], *e = s_cols + s_colw[x];
        while (r < e) {
            const uint32_t rh = *r++;
            if (!rh) continue;
            const uint32_t y = rh & 0xFFFF, k = (rh >> 16) & 0x7FF;
            if (rh & 0x80000000u) {
                if (out + 2 > OVL_CAP) { s_full = 1; r++; continue; }
                L->w[out++] = rh;
                L->w[out++] = *r++;
                continue;
            }
            if (s_pie && k >= PIE_MIN_COLS) {           // whole 8-pixel blocks, the ends the identity (pad2_*)
                const uint32_t hdr = y | ((uint32_t)rev << 15) | (k << 16) | PLANAR;
                const void *after = L->w + out + 1;
                const unsigned head = pad2_head(hdr, k), len = pad2_len(hdr, k), gap = pad2_gap(after);
                const unsigned words = pad2_words(after, hdr, k);
                if (out + 1 + words <= OVL_CAP) {
                    uint16_t *col = (uint16_t *)after + gap, *sh = col + len;
                    for (unsigned i = 0; i < len; i++) { col[i] = 0; sh[i] = 32; }
                    for (uint32_t i = 0; i < k; i++) {
                        const uint32_t v = r[rev ? k - 1 - i : i];
                        col[head + i] = (uint16_t)((((v >> 11) & 31) << 11) | (((v >> 21) & 63) << 5) | (v & 31));
                        sh[head + i] = (uint16_t)((v >> 5) & 63);
                    }
                    L->w[out] = hdr;
                    out += 1 + words;
                    r += k;
                    continue;
                }
            }
            if (out + 1 + k > OVL_CAP) { s_full = 1; r += k; continue; }
            L->w[out++] = rh;
            memcpy(L->w + out, r, k * sizeof(uint32_t));
            out += k;
            r += k;
        }
    }
    L->row[GFX_PW] = out;
    at = out;
    L->n = at;
    /* out of the cache into the PSRAM: the JC8012's fill has the GDMA fetch the words from there */
    {   /* in 16 KB pieces: one call over it all held this core's interrupts off for as long */
        const size_t all = ((size_t)at * sizeof(uint32_t) + 127) & ~(size_t)127;
        for (size_t o = 0; o < all; o += 16384) {
            pace();
            esp_cache_msync((uint8_t *)L->w + o, all - o < 16384 ? all - o : 16384, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        }
    }
    L->beam = *b;
    busy_render(L);
    if (L->n > s_st.words_max) s_st.words_max = L->n;
    if (s_full) s_st.overflow++;
    s_st.built++;
    portENTER_CRITICAL(&s_mux);
    if (s_ready >= 0) s_st.skipped++;                  // the one before was never shown
    s_ready = s_build;
    s_build = -1;
    portEXIT_CRITICAL(&s_mux);
}

#else
void ovl_build_band(canvas_t *cv, int x0, int x1)
{
    if (s_build < 0) return;
    layer_t *L = &s_l[s_build];
    if (x0 < 0) x0 = 0;
    if (x1 > GFX_PW) x1 = GFX_PW;
    x0 &= ~7;                                          // eight words at a time from here
    for (int r = 0; r < cv->h; r++) {
        const int y = cv->y0 + r;
        uint32_t *row = cv->px + r * GFX_PW;
        while (s_next_y <= y && s_next_y <= GFX_PH) L->row[s_next_y++] = L->n;
        /* outside the box nothing should be, but whatever is goes: the band is used again */
        if (x0 > 0) memset(row, 0, x0 * sizeof(uint32_t));
        if (x1 < GFX_PW) memset(row + x1, 0, (GFX_PW - x1) * sizeof(uint32_t));
        for (int k = 0; k < s_bholes.holes; k++) {     // the popups' boxes: nothing of the layer there
            if (y < s_bholes.hy0[k] || y >= s_bholes.hy1[k]) continue;
            int a = s_bholes.hx0[k] < 0 ? 0 : s_bholes.hx0[k], b = s_bholes.hx1[k] > GFX_PW ? GFX_PW : s_bholes.hx1[k];
            if (a < b) memset(row + a, 0, (b - a) * sizeof(uint32_t));
        }
        int x = x0;
        while (x < x1) {
            if (!row[x]) {                             // the most of it: nothing, eight at a time
                if (!(x & 7) && x + 8 <= x1) {
                    const uint32_t *q = row + x;
                    if (!(q[0] | q[1] | q[2] | q[3] | q[4] | q[5] | q[6] | q[7])) { x += 8; continue; }
                }
                x++;
                continue;
            }
            const int rx0 = x;
            const uint32_t at = L->n;
            uint32_t n = 0;
            int room = !s_full && at + 2 <= OVL_CAP, filled = 0;
            if (room) L->n++;                          // the run's word, filled in below
            while (x < x1 && row[x]) {
                uint32_t w = to_word(row[x], x, y);
                if (!w) {                              // nothing to show here: the run ends, the next starts after it
                    row[x++] = 0;
                    break;
                }
                int same = 1;                          // the same pixel again and again (a label's card): a run of its own
                while (x + same < x1 && row[x + same] == row[x] && same < 0x7FFF) same++;
                if (same >= 8) {
                    w = to_word_d(row[x], 8);          // one flat colour: no dither, so one word stands for all
                    if (!w) {                          // too faint without the dither: nothing
                        memset(row + x, 0, same * sizeof(uint32_t));
                        x += same;
                        break;
                    }
                    if (n) {                           // close the run so far, a fill run after it
                        close_run(L, at, rx0, n);
                    } else if (room) {
                        L->n = at;                     // nothing in it yet: the fill run takes its place
                    }
                    if (!s_full && L->n + 2 <= OVL_CAP) {
                        L->w[L->n++] = (uint32_t)x | ((uint32_t)same << 16) | 0x80000000u;
                        L->w[L->n++] = w;
                    } else {
                        s_full = 1;
                    }
                    memset(row + x, 0, same * sizeof(uint32_t));
                    x += same;
                    n = 0;
                    room = 0;                          // the next pixels open a new run
                    filled = 1;
                    break;
                }
                row[x++] = 0;
                if (room && L->n < OVL_CAP) {
                    L->w[L->n++] = w;
                    n++;
                } else {                               // the layer is full: the rest is cleared, not kept
                    room = 0;
                    s_full = 1;
                }
            }
            if (!filled && L->n > at) {
                if (n) close_run(L, at, rx0, n);
                else L->n = at;                        // all of it too faint
            }
        }
    }
}

void ovl_build_end(const ovl_beam_t *b)
{
    if (s_build < 0) return;
    layer_t *L = &s_l[s_build];
    while (s_next_y <= GFX_PH) L->row[s_next_y++] = L->n;
    L->beam = *b;
    busy_render(L);
    if (L->n > s_st.words_max) s_st.words_max = L->n;
    if (s_full) s_st.overflow++;
    s_st.built++;
    portENTER_CRITICAL(&s_mux);
    if (s_ready >= 0) s_st.skipped++;                  // the one before was never shown
    s_ready = s_build;
    s_build = -1;
    portEXIT_CRITICAL(&s_mux);
}
#endif

/* --- putting it on (the fill task, at the panel's pace) --- */

#define EDGE_STEPS 22                                  // the edge's brightness, by its coverage / 16
/* the sweep for one panel frame, worked out once at its start. two of them: the fill's for the frame on
 * the panel, and the planner's for the next one (core 0 composes the busiest chunks of it beforehand) */
typedef struct {
    int      on, cx, cy, r;
    float    ang;                                      // the beam, degrees clockwise from up
    /* by columns (RADAR_OVERLAY_COLS): everything here is worked out with the picture's x and y
     * swapped, so a fill line (a column) is a "row" of it. the swap mirrors the picture: the beam then
     * turns the other way (dir -1, the trail ahead of ang in degrees), at 270 - its angle. ang0 is
     * the beam as the picture has it */
    int      dir;
    float    ang0;
    float    tlo[2][LEVELS], thi[2][LEVELS];           // tangents of each trail step's edges, the row above/below the middle
    uint8_t  tok[2][LEVELS];
    uint32_t tadd[LEVELS];                             // what each step adds, spread
    float    bc, bs, bw, ibc;                          // the edge: its line, its half width, 1 / its cosine
    int16_t  chord[OVL_LINES];                         // half the range ring's chord, by rows from the middle
    int      chord_r;
    uint32_t focus;
    uint32_t eadd[EDGE_STEPS];
} beam_t;
static DRAM_ATTR beam_t s_bf = { .chord_r = -1 }, s_bp = { .chord_r = -1 };

static inline float wrap180(float a)
{
    a = fmodf(a + 180.0f, 360.0f);
    return (a < 0 ? a + 360.0f : a) - 180.0f;
}

static IRAM_ATTR uint32_t spread_scaled(uint32_t c, unsigned k)  // a colour scaled by k/256, rounded to 565 and spread
{
    unsigned r = (((c >> 16) & 255) * k) >> 8, g = (((c >> 8) & 255) * k) >> 8, b = ((c & 255) * k) >> 8;
    r = (r * 31 + 127) / 255;
    g = (g * 63 + 127) / 255;
    b = (b * 31 + 127) / 255;
    return (g << 21) | (r << 11) | b;
}

#if RADAR_OVERLAY_COLS
/* sine and cosine of a in degrees without libm (the fill, in IRAM): to within a quarter turn, then
 * the series to the 9th power (under 1e-6 off there) */
static IRAM_ATTR void sincos_deg(float a, float *s, float *c)
{
    while (a >= 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    int q = 0;                                         // quarter turns taken off
    if (a > 45.0f) { if (a > 135.0f) { a -= 180.0f; q = 2; } else { a -= 90.0f; q = 1; } }
    else if (a < -45.0f) { if (a < -135.0f) { a += 180.0f; q = 2; } else { a += 90.0f; q = 3; } }
    const float x = a * (3.14159265f / 180.0f), x2 = x * x;
    const float sn = x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
    const float cs = 1.0f - x2 / 2.0f * (1.0f - x2 / 12.0f * (1.0f - x2 / 30.0f * (1.0f - x2 / 56.0f)));
    switch (q) {
    case 0: *s = sn; *c = cs; break;
    case 1: *s = cs; *c = -sn; break;
    case 2: *s = -sn; *c = -cs; break;
    default: *s = -cs; *c = sn; break;
    }
}
#define BEAM_ATTR IRAM_ATTR
#else
#define BEAM_ATTR
#endif

static BEAM_ATTR void beam_frame(beam_t *M, const ovl_beam_t *b, float ang)
{
    M->on = b->on && b->turn_ms;
    if (!M->on) return;
    M->ang0 = ang;
#if RADAR_OVERLAY_COLS
    M->cx = b->cy;
    M->cy = b->cx;
    M->dir = -1;
    ang = 270.0f - ang;
    if (ang < 0.0f) ang += 360.0f;
    if (ang >= 360.0f) ang -= 360.0f;
#else
    M->cx = b->cx;
    M->cy = b->cy;
    M->dir = 1;
#endif
    M->r = b->r;
    M->ang = ang;
    const float step = 90.0f / LEVELS;
#if RADAR_OVERLAY_COLS
    /* the same without libm (fork addition, the JC8012P4A1C): this runs in the fill as each panel frame
     * starts, from flash through a cache the PSRAM keeps busy, and ~130 tanf and fmodf calls took
     * ~1.7 ms of a chunk's ~235 us (measured): the frame's first chunk was late every frame, and the next
     * ones after it. the edges' tangents from one sine and cosine, turned a step at a time; the wrap by
     * adding. tan repeats every 180 degrees, so the half a row is in does not change it */
    {
        static float sd, cd;                           // a step, turned the trail's way
        static int sd_dir;
        if (sd_dir != M->dir) {
            sd_dir = M->dir;
            sincos_deg(-(float)M->dir * step, &sd, &cd);
        }
        float sn, cs;
        sincos_deg(M->ang, &sn, &cs);
        M->bc = cs;
        M->bs = sn;
        float tv[LEVELS + 1], ev[LEVELS + 1];
        for (int k = 0; k <= LEVELS; k++) {
            tv[k] = cs > 1e-6f || cs < -1e-6f ? sn / cs : (sn > 0.0f ? 1e9f : -1e9f);
            ev[k] = M->ang - (float)M->dir * (float)k * step;
            const float s2 = sn * cd + cs * sd, c2 = cs * cd - sn * sd;
            sn = s2;
            cs = c2;
        }
        for (int c = 0; c < 2; c++) {
            for (int k = 0; k < LEVELS; k++) {
                float e1 = ev[k] - (float)c * 180.0f, e2 = ev[k + 1] - (float)c * 180.0f;
                while (e1 >= 180.0f) e1 -= 360.0f;
                while (e1 < -180.0f) e1 += 360.0f;
                while (e2 >= 180.0f) e2 -= 360.0f;
                while (e2 < -180.0f) e2 += 360.0f;
                const float hi = M->dir > 0 ? e1 : e2, lo = M->dir > 0 ? e2 : e1;
                const int ih = M->dir > 0 ? k : k + 1, il = M->dir > 0 ? k + 1 : k;
                M->tok[c][k] = (uint8_t)(hi > lo && hi > -90.0f && lo < 90.0f);
                M->tlo[c][k] = lo < -89.99f ? -5729.578f : tv[il];   // tan(89.99 degrees), as the clamp below
                M->thi[c][k] = hi > 89.99f ? 5729.578f : tv[ih];
            }
        }
    }
#else
    for (int c = 0; c < 2; c++) {                      // c 0: rows above the middle, angles round 0; 1: below, round 180
        for (int k = 0; k < LEVELS; k++) {
            const float e1 = wrap180(M->ang - M->dir * k * step - c * 180.0f), e2 = wrap180(M->ang - M->dir * (k + 1) * step - c * 180.0f);
            float hi = M->dir > 0 ? e1 : e2, lo = M->dir > 0 ? e2 : e1;
            int ok = hi > lo && hi > -90.0f && lo < 90.0f;
            if (lo < -89.99f) lo = -89.99f;
            if (hi > 89.99f) hi = 89.99f;
            M->tok[c][k] = (uint8_t)ok;
            M->tlo[c][k] = tanf(lo * (3.14159265f / 180.0f));
            M->thi[c][k] = tanf(hi * (3.14159265f / 180.0f));
        }
    }
#endif
    for (int k = 0; k < LEVELS; k++) {                 // ui_scope.c's trail: 80 f^2 at the step's middle
        unsigned behind = (unsigned)((k * 2 + 1) * 256 / (2 * LEVELS)), f = 256 - behind;
        M->tadd[k] = spread_scaled(b->prim, 80 * f * f >> 16);
    }
#if !RADAR_OVERLAY_COLS
    const float a = M->ang * (3.14159265f / 180.0f);
    M->bc = cosf(a);
    M->bs = sinf(a);
#endif
    M->ibc = fabsf(M->bc) > 1e-4f ? 1.0f / M->bc : 0.0f;
    if (M->chord_r != M->r) {                           // the ring's chord, row by row: again only when the range ring moves
        M->chord_r = M->r;
        for (int d = 0; d < OVL_LINES; d++) M->chord[d] = d < M->r ? (int16_t)sqrtf((float)(M->r * M->r - d * d)) : 0;
    }
    M->bw = (float)GFX_Z(333) / 256.0f;
    M->focus = b->focus;
    for (int i = 0; i < EDGE_STEPS; i++) {             // ui_scope.c's edge: focus * 190 * coverage
        int cov = i * 16 + 8;
        if (cov > 333) cov = 333;
        M->eadd[i] = spread_scaled(b->focus, (unsigned)(190 * cov) >> 8);
    }
}

static IRAM_ATTR inline uint16_t sat(uint32_t x)
{
    if (x & OVF) {
        if (x & 0x20u) x |= 0x1Fu;
        if (x & 0x10000u) x |= 0xF800u;
        if (x & 0x08000000u) x |= 0x07E00000u;
    }
    x &= SPREAD;
    return (uint16_t)(x | (x >> 16));
}

static IRAM_ATTR inline uint32_t spread(uint16_t d) { return (d | ((uint32_t)d << 16)) & SPREAD; }

/* a constant added to a stretch of a row, saturating each field: two pixels a word, the fields'
 * top bits kept out of the sum so no carry crosses into the next, then put back and every field that
 * carried filled with ones */
static IRAM_ATTR inline uint32_t sat_add2(uint32_t w, uint32_t a2)
{
    const uint32_t H = 0x84108410u;                    // the top bit of R, G and B, both pixels
    uint32_t t = (w & ~H) + (a2 & ~H);
    uint32_t sum = t ^ ((w ^ a2) & H);
    uint32_t c = ((w & a2) | ((w | a2) & ~sum)) & H;   // which fields carried out
    return sum | ((c & 0x80108010u) >> 4) * 31u | ((c & 0x04000400u) >> 5) * 63u;
}

// pixels x0..x1 of the layout's row; px is its pixel 0, st -1 when the picture is turned
static IRAM_ATTR void add_span(uint16_t *px, int st, int x0, int x1, uint32_t add)
{
    if (!add || x0 > x1) return;
    uint16_t *p = st > 0 ? px + x0 : px - x1;          // the same stretch, left to right in memory
    int n = x1 - x0 + 1;
    const uint32_t a565 = (add | (add >> 16)) & 0xFFFFu;
    if ((uintptr_t)p & 2) {                            // to a word
        *p = sat(spread(*p) + add);
        p++;
        n--;
    }
    uint32_t *w = (uint32_t *)p;
    const uint32_t a2 = a565 | (a565 << 16);
    for (; n >= 2; n -= 2, w++) *w = sat_add2(*w, a2);
    if (n) {
        p = (uint16_t *)w;
        *p = sat(spread(*p) + add);
    }
}

/* no libm in the fill: its code lies in the PSRAM with the rest (XIP), behind the DMA that is busy
 * there. these are inline, in registers */
static inline int iceil(float v) { int i = (int)v; return (float)i < v ? i + 1 : i; }
static inline int ifloor(float v) { int i = (int)v; return (float)i > v ? i - 1 : i; }
static inline float fabs_(float v) { return v < 0 ? -v : v; }

/* --- a row in two halves: what can be worked out before its pixels are there (prepare: the sweep's
 * stretches and edge, the layer's words fetched out of the PSRAM) and what needs them (apply). the
 * fill prepares a row while the copy is still on its way to it, then applies it once it has landed */
#define ROW_SPANS  128                                 // the trail's steps, and the edge's runs of one brightness (both sides of it)
#define ROW_AHEAD  3                                   // rows prepared at once: the library prepares up to 2 ahead of the one it applies

typedef struct {
    int      ly;                                       // the layout's row, -1: nothing prepared
    int      ns, nw;
    int      nh;                                       // the popups' boxes on this row
    int16_t  h0[4], h1[4];                             // x0..x1-1 of each
    int16_t  s0[ROW_SPANS], s1[ROW_SPANS];             // the sweep's stretches, x0..x1
    uint32_t sa[ROW_SPANS];                            // and what each adds
    const uint32_t *w;                                 // the layer's words for the row, in the PSRAM: fetched into the cache by prepare
    int      made;                                     // a busiest row: px holds its stretch m0..m1 made, the words are not needed
    int      m0, m1;
    uint16_t *px;                                      // its pixels, in the RAM
} row_t;

static DRAM_ATTR uint16_t __attribute__((aligned(16))) s_rowpx[ROW_AHEAD][OVL_LEN];
static DRAM_ATTR row_t s_rows[ROW_AHEAD] = { { .ly = -1, .px = s_rowpx[0] }, { .ly = -1, .px = s_rowpx[1] }, { .ly = -1, .px = s_rowpx[2] } };
#if RADAR_OVERLAY_COLS
/* the other core's (ovl_chunk_lines who 1): the JC8012's fill hands it half of each chunk */
static DRAM_ATTR uint16_t __attribute__((aligned(16))) s_rowpx2[ROW_AHEAD][OVL_LEN];
static DRAM_ATTR row_t s_rows2[ROW_AHEAD] = { { .ly = -1, .px = s_rowpx2[0] }, { .ly = -1, .px = s_rowpx2[1] }, { .ly = -1, .px = s_rowpx2[2] } };
#endif

static IRAM_ATTR void span_from(row_t *R, int x0, int x1, uint32_t add, int k)
{
    if (x0 > x1 || !add) return;
    for (; k < R->nh; k++) {                           // a popup's box cuts it: what is left of it each side
        if (x1 < R->h0[k] || x0 >= R->h1[k]) continue;
        span_from(R, x0, R->h0[k] - 1, add, k + 1);
        span_from(R, R->h1[k], x1, add, k + 1);
        return;
    }
    if (R->ns < ROW_SPANS) {
        R->s0[R->ns] = (int16_t)x0;
        R->s1[R->ns] = (int16_t)x1;
        R->sa[R->ns++] = add;
    }
}

static IRAM_ATTR inline void span(row_t *R, int x0, int x1, uint32_t add) { span_from(R, x0, x1, add, 0); }


static IRAM_ATTR void beam_prepare(row_t *R, int y, int trail, const beam_t *M)
{
    const int dy = y - M->cy;
    if (dy <= -M->r || dy >= M->r) return;
    const int s = M->chord[dy < 0 ? -dy : dy];
    int xa = M->cx - s, xb = M->cx + s;
    if (xa < 0) xa = 0;
    if (xb > OVL_LEN - 1) xb = OVL_LEN - 1;
    if (xa > xb) return;
    const float up = (float)-dy, lim_a = (float)xa - 1.0f, lim_b = (float)xb + 1.0f;
    if (!trail) {
        // the edge alone, below
    } else if (dy == 0) {                              // the middle row: right of it 90 degrees, left 270
        for (int side = 0; side < 2; side++) {
            float d = (float)M->dir * (M->ang - (side ? 270.0f : 90.0f));
            while (d < 0.0f) d += 360.0f;
            while (d >= 360.0f) d -= 360.0f;
            if (d >= 90.0f) continue;
            int k = (int)(d * LEVELS / 90.0f);
            if (side) span(R, xa, M->cx - 1, M->tadd[k]);
            else span(R, M->cx + 1, xb, M->tadd[k]);
        }
    } else {
        const int c = dy > 0;                          // pixels x: x = middle + up * tan(angle)
        for (int k = 0; k < LEVELS; k++) {
            if (!M->tok[c][k]) continue;
            float x1 = M->cx + up * M->tlo[c][k], x2 = M->cx + up * M->thi[c][k];
            if (x1 > x2) { float t = x1; x1 = x2; x2 = t; }
            if (x2 < lim_a || x1 > lim_b) continue;
            if (x1 < lim_a) x1 = lim_a;
            if (x2 > lim_b) x2 = lim_b;
            int i0 = iceil(x1), i1 = iceil(x2) - 1;
            if (i0 < xa) i0 = xa;
            if (i1 > xb) i1 = xb;
            span(R, i0, i1, M->tadd[k]);
        }
    }
    // the edge: a bright line just at the beam, what is within M->bw of it and ahead of the middle
    float e0, e1;
    if (fabs_(M->bc) > 1e-4f) {
        float m = -(float)dy * M->bs * M->ibc, h = M->bw * fabs_(M->ibc);
        e0 = m - h;
        e1 = m + h;
    } else if (fabs_((float)dy * M->bs) < M->bw) {
        e0 = (float)(xa - M->cx);
        e1 = (float)(xb - M->cx);
    } else {
        return;
    }
    int d0 = iceil(e0), d1 = ifloor(e1);
    if (d0 < xa - M->cx) d0 = xa - M->cx;
    if (d1 > xb - M->cx) d1 = xb - M->cx;
    /* across a row the distance to the beam's line changes by its cosine a pixel. so the row is taken a
     * run at a time, each as long as the brightness stays within half a step (the line nearly along the
     * row, at 3 and 9 o'clock: one or two runs for hundreds of pixels; nearly across it: a few pixels
     * anyway), not a pixel at a time, which near 3 and 9 o'clock was the row's most costly work. ahead
     * of the middle is cut once for the row. only near the middle, where the 6.6 degrees matter, a
     * pixel at a time */
    const int zmax = GFX_Z(333) > 333 ? 333 : GFX_Z(333);
    if (fabs_(M->bs) > 1e-4f) {                         // ahead of the middle: dx * sin - dy * cos > 0
        const float cut = (float)dy * M->bc / M->bs;
        if (M->bs > 0.0f) { const int c = ifloor(cut) + 1; if (d0 < c) d0 = c; }
        else             { const int c = iceil(cut) - 1;  if (d1 > c) d1 = c; }
    } else if (-(float)dy * M->bc <= 0.0f) {
        return;
    }
    const float ab = fabs_(M->bc);
    const int step = ab * 256.0f * 256.0f < 16.0f ? 4096 : (int)(16.0f / (ab * 256.0f)) + 1;   // pixels a run: a brightness step
    int dx = d0, run0 = d0;
    uint32_t radd = 0;
    while (dx <= d1) {
        int n;
        uint32_t add = 0;
        if (dx > -22 && dx < 22 && dy > -22 && dy < 22) {   // near the middle: a pixel at a time
            n = 1;
            const float off = fabs_((float)dx * M->bc + (float)dy * M->bs), r2 = (float)(dx * dx + dy * dy);
            if (r2 > 4.0f && off * off <= 0.01318f * r2) {
                int cov = GFX_Z(333) - (int)(off * 256.0f);
                if (cov > zmax) cov = zmax;
                if (cov > 0) add = M->eadd[cov >> 4];
            }
        } else {
            n = step;
            if (dx < 22 && dx + n > -22 && dy > -22 && dy < 22) n = -21 - dx > 0 ? -21 - dx : 1;   // stop where the middle begins
            if (dx + n - 1 > d1) n = d1 - dx + 1;
            const float mid = (float)dx + (float)(n - 1) * 0.5f;
            const float off = fabs_(mid * M->bc + (float)dy * M->bs);
            int cov = GFX_Z(333) - (int)(off * 256.0f);
            if (cov > zmax) cov = zmax;
            if (cov > 0) add = M->eadd[cov >> 4];
        }
        if (add != radd) {                             // runs of one brightness put down as one stretch
            if (radd) span(R, M->cx + run0, M->cx + dx - 1, radd);
            run0 = dx;
            radd = add;
        }
        dx += n;
    }
    if (radd) span(R, M->cx + run0, M->cx + d1, radd);
}

// a new panel frame: the newest layer, the beam now
static int s_planned;                                  // the frame on the panel is the planned one: its composed chunks hold

static IRAM_ATTR void frame_latch(uint32_t frame)
{
    if (frame == s_frame) return;
    s_frame = frame;
    s_latch_us = esp_timer_get_time();
    float ang = -1.0f;
    portENTER_CRITICAL(&s_mux);
    s_planned = s_plan_frame == frame && s_plan_layer >= 0;
    if (s_planned) {                                   // core 0 planned this frame: its layer, its beam
        if (s_ready == s_plan_layer) s_ready = -1;
        s_show = s_plan_layer;
        ang = s_plan_ang;
    } else if (s_ready >= 0) {
        s_show = s_ready;
        s_ready = -1;
    }
    portEXIT_CRITICAL(&s_mux);
    if (s_show >= 0) {
        const ovl_beam_t *b = &s_l[s_show].beam;
        if (ang < 0.0f && b->turn_ms) ang = (float)((uint32_t)(s_latch_us / 1000) % b->turn_ms) * (360.0f / (float)b->turn_ms);
        beam_frame(&s_bf, b, ang < 0.0f ? 0.0f : ang);
    }
    const beam_t *M = &s_bf;
    {                                                  // the rows the last frame lost, by where the beam was
        extern volatile uint32_t g_ovlRowsSkipped;
        static uint32_t was;
        static int sector;
        const uint32_t now = g_ovlRowsSkipped;
        s_st.late_by_beam[sector] += now - was;
        was = now;
        sector = M->on ? ((int)(M->ang0 / 30.0f + 0.5f)) % 12 : 0;
    }
    s_st.frames++;
    if (s_waiter) xTaskNotifyGive(s_waiter);
    if (s_planner) xTaskNotifyGive(s_planner);
}

/* (fork addition) a panel frame begun, for a fill that does not ask ovl_want / ovl_src (lcd_jc8012.c's
 * scrolling pages): the ui task and the planner are woken by it */
IRAM_ATTR void ovl_frame(uint32_t frame) { frame_latch(frame); }

#define RP_LAYER 1                                     // the layer's part too (reads the PSRAM)
#define RP_COUNT 2                                     // into the fill's counters
#define RP_SWEEP 4                                     // the sweep's part
#define RP_NOLOCAL 8                                   // not from the fill's copy of its chunk's words (the planner)
static IRAM_ATTR inline int logical(int py) { return s_flip ? OVL_LINES - 1 - py : py; }

#if RADAR_OVERLAY_COLS
/* a chunk's layer words, fetched into the RAM by the fill (ovl_chunk_local): lines s_loc_l0..l1-1 of
 * layer s_loc_layer, word s_loc_w0 of it at s_loc */
static const uint32_t *volatile s_loc;
static volatile int      s_loc_layer = -1, s_loc_l0, s_loc_l1;
static volatile uint32_t s_loc_w0;

IRAM_ATTR int ovl_chunk_words(int py, int n, uint32_t frame, const uint32_t **at, uint32_t *count)
{
    if (frame != s_frame || s_show < 0) return 0;      // only the frame the fill is on
    int a = logical(py), b = logical(py + n - 1);
    if (a > b) { const int t = a; a = b; b = t; }
    const layer_t *L = &s_l[s_show];
    const uint32_t w0 = L->row[a], w1 = L->row[b + 1];
    if (w1 == w0) return 0;
    /* from the cache line it starts in, to the end of the one it ends in */
    const uintptr_t s = (uintptr_t)(L->w + w0) & ~(uintptr_t)127, e = ((uintptr_t)(L->w + w1) + 127) & ~(uintptr_t)127;
    *at = (const uint32_t *)s;
    *count = (uint32_t)((e - s) / 4);
    return 1;
}

IRAM_ATTR void ovl_chunk_local(int py, int n, const uint32_t *local, const uint32_t *at)
{
    if (!local || s_show < 0) { s_loc_layer = -1; return; }
    int a = logical(py), b = logical(py + n - 1);
    if (a > b) { const int t = a; a = b; b = t; }
    s_loc = local;
    s_loc_w0 = (uint32_t)(at - s_l[s_show].w);
    s_loc_l0 = a;
    s_loc_l1 = b + 1;
    s_loc_layer = s_show;
}
#endif

static IRAM_ATTR void row_prepare(row_t *R, int ly, const beam_t *M, int li, int what)
{
    uint32_t t0 = esp_cpu_get_cycle_count();
    R->ly = ly;
    R->ns = R->nw = R->nh = 0;
    R->w = NULL;
    const ovl_beam_t *B = &s_l[li].beam;
    for (int k = 0; k < B->holes; k++)
#if RADAR_OVERLAY_COLS
        if (ly >= B->hx0[k] && ly < B->hx1[k]) { R->h0[R->nh] = B->hy0[k]; R->h1[R->nh++] = B->hy1[k]; }
#else
        if (ly >= B->hy0[k] && ly < B->hy1[k]) { R->h0[R->nh] = B->hx0[k]; R->h1[R->nh++] = B->hx1[k]; }
#endif
    if (M->on && (what & RP_SWEEP)) beam_prepare(R, ly, OVL_SWEEP_EVERY < 2 || !(ly & 1), M);
    uint32_t t1 = esp_cpu_get_cycle_count();
    const layer_t *L = &s_l[li];
    R->made = 0;
    if (!(what & RP_LAYER)) return;
    R->made = !RADAR_OVERLAY_COLS && L->busy[ly] >= 0;
    if (R->made) {                                     // made already: its stretch out of the PSRAM now, while the copy runs
        R->m0 = L->bx0[ly];
        R->m1 = L->bx1[ly];
        memcpy(R->px + R->m0, L->rows + (size_t)L->busy[ly] * OVL_LEN + R->m0, (size_t)(R->m1 - R->m0 + 1) * sizeof(uint16_t));
        if (what & RP_COUNT) {
            s_st.layer_cyc += esp_cpu_get_cycle_count() - t1;
            s_st.beam_cyc += t1 - t0;
        }
        return;
    }
    const uint32_t n = L->row[ly + 1] - L->row[ly];
#if RADAR_OVERLAY_COLS
    if (n && !(what & RP_NOLOCAL) && li == s_loc_layer && ly >= s_loc_l0 && ly < s_loc_l1) {   // fetched into the RAM with the chunk
        R->w = s_loc + (L->row[ly] - s_loc_w0);
        R->nw = (int)n;
        if (what & RP_COUNT) {
            s_st.words += n;
            s_st.beam_cyc += t1 - t0;
            s_st.layer_cyc += esp_cpu_get_cycle_count() - t1;
        }
        return;
    }
#endif
    if (n) {
        const uint32_t *src = L->w + L->row[ly];
        volatile uint32_t sink = 0;                    // into the cache now, a word a 64-byte line, while the copy is still busy
        for (uint32_t i = 0; i < n; i += 16) sink += src[i];
        sink += src[n - 1];
        (void)sink;
        R->w = src;
        R->nw = (int)n;
        if (what & RP_COUNT) s_st.words += n;
    }
    if (what & RP_COUNT) {
        s_st.beam_cyc += t1 - t0;
        s_st.layer_cyc += esp_cpu_get_cycle_count() - t1;
    }
}

/* TEMP (2026-11-14): where the fill's layer time goes, by path: PIE runs (the vector part), their C edges,
 * solid runs (one word, n alike), per-pixel words. runs, pixels, cycles; per core, the fill and its helper only */
enum { WP_PIE, WP_EDGE, WP_SOLID, WP_WORDS, WP_N };
static DRAM_ATTR uint32_t s_wp[2][WP_N][3];
void ovl_path_stats(uint32_t out[WP_N][3])
{
    for (int k = 0; k < WP_N; k++) for (int j = 0; j < 3; j++) { out[k][j] = s_wp[0][k][j] + s_wp[1][k][j]; s_wp[0][k][j] = s_wp[1][k][j] = 0; }
}
#define WP_ADD(k, npx, cyc) do { if (wp) { wp[k][0]++; wp[k][1] += (uint32_t)(npx); wp[k][2] += (uint32_t)(cyc); } } while (0)

// the layer's words for one row onto it: px its pixel 0 as the layout counts, st -1 when turned
static IRAM_ATTR void words_apply(const uint32_t *w0, int nw, uint16_t *px, int st)
{
    const uint32_t *w = w0, *end = w0 + nw;
    uint32_t (*wp)[3] = xTaskGetCurrentTaskHandle() == s_planner ? NULL : s_wp[esp_cpu_get_core_id() & 1];   // TEMP
    while (w < end) {
        const uint32_t h = *w++;
        const int x = (int)(h & 0xFFFF), n = (int)((h >> 16) & 0x7FF);
        const uint32_t tw = esp_cpu_get_cycle_count();  // TEMP
#if RADAR_OVERLAY_COLS
        if (h & PLANAR) {                              // whole 8-pixel blocks on the 16-byte grid (pad2_*)
            const int y = (int)(h & 0x7FFF), rev = st < 0;
            const unsigned head = pad2_head(h, (unsigned)n), len = pad2_len(h, (unsigned)n), gap = pad2_gap(w);
            const uint16_t *col = (const uint16_t *)w + gap, *sh = col + len;
            w += (2u * gap + 4u * len + 3u) >> 2;
            if ((int)((h >> 15) & 1) != rev) continue; // made before a flip: not this way round
            uint16_t *d = rev ? px - (y + n - 1) : px + y;   // its first pixel in memory
            /* the planner shares core 0 with the fill's helper, and the vector state did not survive the helper
             * cutting in mid-kernel (seen: 8-pixel slivers of a plane gone white). why (2026-11-14): ESP-IDF
             * 5.5's FreeRTOS port (portasm.S, pie_save_regs / pie_restore_regs) never saves q3, the register
             * the kernel builds its result in, and restores SAR from the FFT_BIT_WIDTH bits of the saved word.
             * tried: the scheduler held around each call in the planner (no task switch inside the kernel):
             * ~2,000 late chunks per 10 s instead of 20-100, the helper's wakes held back. fixed upstream
             * (espressif/esp-idf 51d5738ad769 on release/v5.5, b25cb2906c7d on v6): with it, the planner uses
             * PIE too. an IDF without it: set OVL_PLANNER_PIE 0 */
            if (s_pie && !((uintptr_t)(d - head) & 15) && (OVL_PLANNER_PIE || xTaskGetCurrentTaskHandle() != s_planner)) {
                const uint32_t tp = esp_cpu_get_cycle_count();
                blend_n8(d - head, col, sh, (int)(len >> 3));
                WP_ADD(WP_PIE, len, esp_cpu_get_cycle_count() - tp);
                if (wp) wp[WP_EDGE][2] += tp - tw;      // the run's set-up
                continue;
            }
            for (int i = 0; i < n; i++) pix(d + i, col[head + i], sh[head + i]);
            WP_ADD(WP_EDGE, n, esp_cpu_get_cycle_count() - tw);
            continue;
        }
#endif
        if (h & PLANAR) {                              // two arrays: PIE 8 at a time where the row lines up
            const unsigned pad = (h >> 27) & 7;
            const uint16_t *col = (const uint16_t *)w + pad, *sh = col + n + planar_q((unsigned)n) / 2;
            int i = 0;
            if (st > 0 && s_pie && !((uintptr_t)px & 15)) {
                uint16_t *d = px + x;
                for (; i < n && ((uintptr_t)(d + i) & 15); i++) pix(d + i, col[i], sh[i]);
                const int n8 = (n - i) >> 3;
                if (n8 > 0) {
                    blend_n8(d + i, col + i, sh + i, n8);
                    i += n8 * 8;
                }
                for (; i < n; i++) pix(d + i, col[i], sh[i]);
            } else {
                for (; i < n; i++) pix(px + st * (x + i), col[i], sh[i]);
            }
            w += planar_words(pad, (unsigned)n);
            continue;
        }
        if (h & 0x80000000u) {                         // n of the same: one word
            const uint32_t v = *w++, under = (v >> 5) & 63, o0 = v & SPREAD;
            uint16_t *p = px + st * x;
            if (!under) {
                const uint16_t c = sat(o0);
                for (int i = 0; i < n; i++, p += st) *p = c;
            }
#if HAVE_PIE_SOLID
            /* (fork addition) translucent and long enough: PIE, 8 at a time from the run's lowest address (one
             * colour, so which way the row runs does not matter), the unaligned ends in C. pix(c, under) is the
             * loop below exactly: c is o0 in 565, and its saturation is sat's */
            else if (s_pie_solid && n >= 16 && (OVL_PLANNER_PIE || xTaskGetCurrentTaskHandle() != s_planner)) {
                const uint16_t c = (uint16_t)((((v >> 11) & 31) << 11) | (((v >> 21) & 63) << 5) | (v & 31));
                uint16_t *a = st > 0 ? p : p - (n - 1);
                int i = 0;
                for (; i < n && ((uintptr_t)(a + i) & 15); i++) pix(a + i, c, under);
                const int n8 = (n - i) >> 3;
                if (n8 > 0) {
#if HAVE_PIE2
                    if (s_pie2) pie_solid2(a + i, c, (uint16_t)under, n8);
                    else
#endif
                    pie_solid(a + i, c, (uint16_t)under, n8);
                    i += n8 * 8;
                }
                for (; i < n; i++) pix(a + i, c, under);
            }
#endif
            else {
                for (int i = 0; i < n; i++, p += st) *p = sat(o0 + (((spread(*p) * under) >> 5) & SPREAD));
            }
            WP_ADD(WP_SOLID, n, esp_cpu_get_cycle_count() - tw);
            continue;
        }
        uint16_t *p = px + st * x;
        for (int i = 0; i < n; i++, p += st) {
            const uint32_t v = w[i], under = (v >> 5) & 63;
            uint32_t o = v & SPREAD;
            if (under) o += ((spread(*p) * under) >> 5) & SPREAD;
            *p = sat(o);
        }
        w += n;
        WP_ADD(WP_WORDS, n, esp_cpu_get_cycle_count() - tw);
    }
}

/* the busiest rows of a layer, made whole now (the ui task): what the fill would spend on them is
 * estimated from their words, and those over OVL_BUSY_CYCLES get the background's row as it shows now
 * with the layer on it. a background put up afterwards shows in them only with the next layer */
static void busy_render(layer_t *L)
{
    memset(L->busy, 0xFF, sizeof L->busy);
    memset(L->cost, 0, sizeof L->cost);
    L->nbusy = 0;
    for (int ly = 0; ly < OVL_LINES; ly++) {
        const uint32_t *w = L->w + L->row[ly], *end = L->w + L->row[ly + 1];
        if (w == end) continue;
        /* what the fill would spend: measured with 48 planes, ~60 cycles a stored word all told (the
         * PSRAM reads, the run's set-up, the pixels) and ~300 a run. a cheaper guess, PIE runs nearly
         * free, made almost no row busy */
        uint32_t cost = 0, stored = 0;
        int xa = OVL_LEN, xb = -1;
        for (const uint32_t *q = w; q < end;) {
            const uint32_t h = *q++;
            const unsigned n = (h >> 16) & 0x7FF, x = h & 0xFFFF;
#if RADAR_OVERLAY_COLS
            const unsigned words = (h & PLANAR) ? pad2_words(q, h, n) : (h & 0x80000000u) ? 1 : n;
#else
            const unsigned words = (h & PLANAR) ? planar_words((h >> 27) & 7, n) : (h & 0x80000000u) ? 1 : n;
#endif
            cost += 300 + 60 * words;
            stored += words;
            if ((int)x < xa) xa = (int)x;
            if ((int)(x + n - 1) > xb) xb = (int)(x + n - 1);
            q += words;
        }
        L->cost[ly] = (uint16_t)(cost / 4 > 65535u ? 65535u : cost / 4);
        if (!L->rows || !s_src || L->nbusy >= OVL_BUSY_ROWS) continue;
        /* only where copying the stretch the layer covers is the cheaper way: whole rows (2 KB of PSRAM
         * each) cost the fill more than putting most layers on, and made it far later */
        const int span = xb - xa + 1;
        if (cost < OVL_BUSY_CYCLES || span > 2 * (int)stored) continue;
        const int py = s_flip ? OVL_LINES - 1 - ly : ly;
        const uint16_t *src = s_src(py);
        if (!src) return;
        const int p0 = s_flip ? OVL_LEN - 1 - xb : xa;  // the stretch on the panel
        uint16_t *row = L->rows + (size_t)L->nbusy * OVL_LEN;
        memcpy(row + p0, src + p0, (size_t)span * sizeof(uint16_t));
        words_apply(w, (int)(end - w), s_flip ? row + OVL_LEN - 1 : row, s_flip ? -1 : 1);
        L->bx0[ly] = (int16_t)p0;
        L->bx1[ly] = (int16_t)(p0 + span - 1);
        L->busy[ly] = (int16_t)L->nbusy++;
        /* what the fill does with it now: copy the stretch out of the PSRAM, beside the panel's DMA there,
         * ~12 cycles a pixel (priced at 1, such rows near the middle were never picked and lost rows) */
        L->cost[ly] = (uint16_t)((300 + 12 * span) / 4 > 65535 ? 65535 : (300 + 12 * span) / 4);
    }
    s_st.busy += L->nbusy;
}

/* the fill says by when the row must be done (the library's g_ovlRowDeadline, cycles; 0: no limit).
 * the sweep is what has to keep the panel's pace: it goes first, always. the layer (planes, labels,
 * the compass ring) only when its words fit in what time is left, else the row shows the sweep alone
 * this frame. about 30 cycles a word, measured */
extern volatile uint32_t g_ovlRowDeadline;
#define WORD_CYCLES 30

static IRAM_ATTR void row_apply(const row_t *R, uint16_t *row, uint16_t *px, int st, uint32_t dl)
{
    uint32_t t0 = esp_cpu_get_cycle_count();
    if (R->made) memcpy(row + R->m0, R->px + R->m0, (size_t)(R->m1 - R->m0 + 1) * sizeof(uint16_t));   // its layer, then the sweep over it
    for (int i = 0; i < R->ns; i++) add_span(px, st, R->s0[i], R->s1[i], R->sa[i]);
    uint32_t t1 = esp_cpu_get_cycle_count();
    if (R->made) {
        s_st.beam_cyc += t1 - t0;
        return;
    }
    if (dl && R->nw && (int32_t)(dl - t1) < R->nw * WORD_CYCLES) {
        s_st.layer_dropped++;
        s_st.beam_cyc += t1 - t0;
        return;
    }
    words_apply(R->w, R->nw, px, st);
    s_st.beam_cyc += t1 - t0;
    s_st.layer_cyc += esp_cpu_get_cycle_count() - t1;
}


// the fill, first for each chunk: does anything go over its rows this frame
/* --- the busiest chunks of the next frame, composed beforehand on core 0 (fork addition) ---
 * core 0, as the frame on the panel starts, plans the next one (its layer, where its beam will be) and
 * works out what each chunk of it would cost the fill. the chunks over PRE_CYCLES, at most PRE_MAX of them
 * and in the order the panel will scan them, it composes whole: the background's rows, the sweep, the
 * layer, into a buffer in the PSRAM. the fill copies such a chunk from there instead of the framebuffer
 * and has nothing more to do. only the busiest: every chunk would double the PSRAM's traffic */

static IRAM_ATTR int pre_slot(int y, uint32_t frame)
{
    const int p = frame & 1;
    if (!s_planned || s_pre_for[p] != frame) return -1;
    return s_pre_slot[p][y / PRE_ROWS];
}

// what the fill still does on physical row py of this frame: RP_SWEEP and RP_LAYER, less what core 0 did
static IRAM_ATTR int fill_what(int py)
{
    const uint32_t frame = s_frame;
    if (pre_slot(py, frame) < 0) return RP_SWEEP | RP_LAYER | RP_COUNT;
    const uint8_t done = s_pre_what[frame & 1][py / PRE_ROWS];
    return ((done & PRE_SWEEP) ? 0 : RP_SWEEP) | ((done & PRE_LAYER) ? 0 : RP_LAYER) | RP_COUNT;
}

// the fill, before anything else for each chunk: composed beforehand? then from there
IRAM_ATTR const uint16_t *ovl_src(int y, int rows, uint32_t frame, void *ctx)
{
    (void)rows; (void)ctx;
    frame_latch(frame);
    const int k = pre_slot(y, frame);
    if (k < 0) return NULL;
    s_st.pre_used++;
    return s_pre[frame & 1][k];
}

IRAM_ATTR bool ovl_want(int y, int rows, uint32_t frame, void *ctx)
{
    (void)ctx;
    frame_latch(frame);
    if (s_show < 0) return false;
    if (pre_slot(y, frame) >= 0 && s_pre_what[frame & 1][y / PRE_ROWS] == (PRE_SWEEP | PRE_LAYER))
        return false;                                  // composed whole beforehand, copied from there
    const beam_t *M = &s_bf;
    int a = y, b = y + rows - 1;                       // its rows as the layout has them
    if (s_flip) { a = OVL_LINES - 1 - b; b = OVL_LINES - 1 - y; }
    if (M->on && b > M->cy - M->r && a < M->cy + M->r) return true;   // the sweep crosses them
    const layer_t *L = &s_l[s_show];
    return L->row[b + 1] != L->row[a];                 // the layer has words in them
}

// the fill, before the row's pixels are there (dst unused): rows is 1
IRAM_ATTR void ovl_prepare(uint16_t *dst, int y, int rows, uint32_t frame, void *ctx)
{
    (void)dst; (void)rows; (void)ctx;
    frame_latch(frame);
    if (s_show < 0) return;
    const int ly = logical(y);
    row_prepare(&s_rows[ly % ROW_AHEAD], ly, &s_bf, s_show, fill_what(y));
}

void ovl_writeback(void (*wb)(const void *addr, size_t len)) { s_wb = wb; }

/* the planner's task: woken at each panel frame's start, above the ui task, so a layer being made never
 * makes it miss a frame (run from the ui loop, it planned only a frame in three) */
static uint32_t (*s_period)(void);
static void planner_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ovl_plan_next(s_period ? s_period() : 30000);
    }
}

void ovl_planner_start(uint32_t (*period_us)(void), int core, int prio)
{
    s_period = period_us;
    if (!s_planner) xTaskCreatePinnedToCore(planner_task, "planner", 4096, NULL, prio, &s_planner, core);
}

// the chunks that lost the most rows since the last call: which, how many, what the planner thought of them
void ovl_log_chunks(void)
{
#if RADAR_OVERLAY_COLS
    return;                                            // the S3 library's per-chunk counters
#else
    extern volatile uint16_t g_ovlSkipChunk[64];
    char buf[200];
    int at = 0;
    for (int k = 0; k < 6; k++) {
        int best = -1;
        for (int c = 0; c < PRE_CHUNKS; c++)
            if (g_ovlSkipChunk[c] && (best < 0 || g_ovlSkipChunk[c] > g_ovlSkipChunk[best])) best = c;
        if (best < 0) break;
        at += snprintf(buf + at, sizeof buf - at, " #%d: %u lost, cost %u, composed %u;", best,
                       (unsigned)g_ovlSkipChunk[best], (unsigned)s_pre_cost[best], (unsigned)s_pre_count[best]);
        g_ovlSkipChunk[best] = 0;
    }
    for (int c = 0; c < PRE_CHUNKS; c++) { g_ovlSkipChunk[c] = 0; s_pre_count[c] = 0; }
    ESP_LOGI(TAG, "chunks losing rows most (threshold %u):%s", (unsigned)PRE_CYCLES, at ? buf : " none");
#endif
}

static DRAM_ATTR uint16_t __attribute__((aligned(16))) s_prowpx[OVL_LEN];
static DRAM_ATTR row_t s_prow = { .ly = -1, .px = s_prowpx };

// what the sweep of a prepared row costs the fill (its spans; the internal RAM only)
static uint32_t sweep_cost(const row_t *R)
{
    uint32_t c = 0;
    for (int i = 0; i < R->ns; i++) c += 60 + (uint32_t)(R->s1[i] - R->s0[i] + 1) * 2;
    return c;
}

void ovl_plan_next(uint32_t period_us)
{
    if (!s_pre[0][0] || !period_us) return;
    const uint32_t frame = s_frame, next = frame + 1;
    const int64_t start = s_latch_us;
    int li;
    portENTER_CRITICAL(&s_mux);
    li = s_ready >= 0 ? s_ready : s_show;
    portEXIT_CRITICAL(&s_mux);
    if (li < 0) return;
    const ovl_beam_t *b = &s_l[li].beam;
    const int64_t t_next = start + period_us;          // when the next frame starts
    const float ang = b->turn_ms ? (float)((uint32_t)(t_next / 1000) % b->turn_ms) * (360.0f / (float)b->turn_ms) : 0.0f;
    beam_frame(&s_bp, b, ang);
    const int p = next & 1;
    for (int c = 0; c < PRE_CHUNKS; c++) s_pre_slot[p][c] = -1;
    s_pre_for[p] = next;
    s_hot_for[p] = 0;                                  // not known till the picks are made (below)
    portENTER_CRITICAL(&s_mux);                        // the plan, the frame last: the fill reads it whole or not at all
    s_plan_layer = li;
    s_plan_ang = ang;
    s_plan_frame = next;
    portEXIT_CRITICAL(&s_mux);
    if (!s_src) return;
    /* every chunk's cost first (the internal RAM only), then the heaviest PRE_PICK of those over PRE_CYCLES,
     * composed in scan order, each before the panel gets to it in the next frame. by cost, not all over
     * the line: twenty chunks a frame from core 0 (~800 KB of PSRAM traffic) slowed the panel's DMA */
    const uint32_t chunk_us = period_us / PRE_CHUNKS;
    static uint32_t cost_s[PRE_CHUNKS], cost_l[PRE_CHUNKS];
    for (int c = 0; c < PRE_CHUNKS; c++) {
        /* the sweep's from every other row, twice over (its trail is on every other row anyway): half
         * the planner's time, which the layer on this core wants */
        uint32_t cs = 0, cl = 0;
        for (int r = 0; r < PRE_ROWS; r++) {
            const int ly = logical(c * PRE_ROWS + r);
            if (!(r & 1)) {
                row_prepare(&s_prow, ly, &s_bp, li, RP_SWEEP | RP_NOLOCAL);
                cs += 2u * sweep_cost(&s_prow);
            }
            cl += 4u * s_l[li].cost[ly];
        }
        cost_s[c] = cs;
        cost_l[c] = cl;
        s_pre_cost[c] = cs + cl;
    }
#if RADAR_OVERLAY_COLS
    /* the even chunks a little ahead of the odd ones (the user's idea): in a run of heavy chunks every
     * other one made beforehand leaves the fill a cheap one between to catch up on */
    static uint32_t rank[PRE_CHUNKS];
    for (int c = 0; c < PRE_CHUNKS; c++) rank[c] = (c & 1) ? s_pre_cost[c] : s_pre_cost[c] + s_pre_cost[c] / 4;
#else
    const uint32_t *rank = s_pre_cost;
#endif
    static uint8_t pick[PRE_CHUNKS];
    memset(pick, 0, sizeof pick);
    for (int k = 0; k < PRE_PICK; k++) {
        int best = -1;
        for (int c = 0; c < PRE_CHUNKS; c++)
            if (!pick[c] && s_pre_cost[c] >= PRE_CYCLES && (best < 0 || rank[c] > rank[best])) best = c;
        if (best < 0) break;
        pick[best] = 1;
    }
    int used = 0;
    int64_t waited = 0;                                // for the runway, this plan (PLAN_WAIT_US)
    for (int c = 0; c < PRE_CHUNKS && used < PRE_MAX; c++) {
        if (!pick[c]) continue;
        /* the fill asks for chunk c about a bounce period before the panel scans it, the first two at
         * the frame's start: composed by then, or not used */
        const int64_t due = t_next + (int64_t)(c - PRE_LEAD) * chunk_us;
        if (esp_timer_get_time() > due - 1500) continue;   // too late for this one
        const uint32_t cl = cost_l[c];
        /* the sweep is always composed here; the layer too only where it is heavy (many planes and
         * labels): elsewhere the fill puts it on, it reads its words from the PSRAM anyway */
#if RADAR_OVERLAY_COLS
        const uint8_t what = PRE_SWEEP | PRE_LAYER;    // whole: the sweep under the labels, as the fill does it
        (void)cl;
#else
        const uint8_t what = PRE_SWEEP | (cl >= PRE_LAYER_CYCLES ? PRE_LAYER : 0);
#endif
        /* (fork addition) its PSRAM burst kept off the fill's hot runs and their run-ups, as long as the chunk
         * can still be ready: it takes ~s_pre_us to compose */
        static uint32_t s_pre_us = 400;
        /* asleep a tick at a time, at most PLAN_WAIT_US a frame: it is above the fetch task, the log and the idle
         * task on core 0, and spinning here (20 us at a time, till the chunk's deadline) starved them all */
        if (s_plan_quiet && waited < PLAN_WAIT_US) {
            const int64_t w0 = esp_timer_get_time();
            while (!s_plan_quiet() && waited + (esp_timer_get_time() - w0) < PLAN_WAIT_US &&
                   esp_timer_get_time() + 2 * (int64_t)s_pre_us + 1000 < due - 1500)
                vTaskDelay(1);
            const int64_t w = esp_timer_get_time() - w0;
            waited += w;
            g_plan_wait_us += (uint32_t)w;
        }
        if (esp_timer_get_time() > due - 1500) continue;   // waited too long for it
        const int64_t c0 = esp_timer_get_time();
        uint16_t *buf = s_pre[p][used];
        const int rev = s_flip ^ OVL_REV;
        busy_on(BUSY_PLAN);                            // TEMP (busy.h)
        for (int r = 0; r < PRE_ROWS; r++) {           // the background's rows, the sweep, the layer
            const int py = c * PRE_ROWS + r, ly = logical(py);
            uint16_t *row = buf + (size_t)r * OVL_LEN;
            const uint16_t *src = s_src(py);
            if (!src) { busy_off(BUSY_PLAN); return; }
            memcpy(row, src, OVL_LEN * sizeof(uint16_t));
            row_prepare(&s_prow, ly, &s_bp, li, RP_SWEEP | RP_NOLOCAL | ((what & PRE_LAYER) ? RP_LAYER : 0));
            row_apply(&s_prow, row, rev ? row + OVL_LEN - 1 : row, rev ? -1 : 1, 0);
        }
        if (s_wb) s_wb(buf, (size_t)PRE_ROWS * OVL_LEN * sizeof(uint16_t));
        busy_off(BUSY_PLAN);
        s_pre_us = (s_pre_us * 7u + (uint32_t)(esp_timer_get_time() - c0)) / 8u;
        if (esp_timer_get_time() > due - 200) continue;   // done too late: not used
        s_pre_what[p][c] = what;
        s_pre_slot[p][c] = (int8_t)used++;
        s_pre_count[c]++;
        s_st.pre_made++;
    }
    int hot = 0, heavy = 0;
    for (int c = 0; c < PRE_CHUNKS; c++) {
        s_hot[p][c] = s_pre_cost[c] >= PRE_CYCLES && s_pre_slot[p][c] < 0;
        hot += s_hot[p][c];
        heavy += s_pre_cost[c] >= PRE_CYCLES;
    }
    s_hot_for[p] = next;
    g_plan_hot += (uint32_t)hot;                       // TEMP: for the 10 s log
    g_plan_heavy += (uint32_t)heavy;
    g_plan_made += (uint32_t)used;
    g_plans++;
}

#if RADAR_OVERLAY_COLS
/* lines r0..r1-1 of the chunk at panel row y, with the row set of core `who` (0 the fill's, 1 its
 * helper's), the frame already taken (ovl_want or ovl_chunk did) */
static int (*s_too_late)(void);                       // the fill's: the panel has begun on the chunk, stop

void ovl_stop_when(int (*too_late)(void)) { s_too_late = too_late; }

IRAM_ATTR void ovl_chunk_lines(uint16_t *dst, int y, int r0, int r1, uint32_t frame, int who)
{
    (void)frame;
    if (s_show < 0) return;
    row_t *ring = who ? s_rows2 : s_rows;
    const int rev = s_flip ^ OVL_REV;
    for (int r = r0; r < r1; r++) {
        /* the panel is reading this chunk already: what is not on it yet would land too late, and the
         * time is the next chunks'. a fill that went on here made them late too, one after another */
        if (s_too_late && s_too_late()) { s_st.layer_dropped += (uint32_t)(r1 - r); break; }
        const int ly = logical(y + r);
        uint16_t *row = dst + r * OVL_LEN;
        uint16_t *px = rev ? row + OVL_LEN - 1 : row;
        const int st = rev ? -1 : 1;
        row_t *R = &ring[ly % ROW_AHEAD];
        if (R->ly != ly) row_prepare(R, ly, &s_bf, s_show, fill_what(y + r));
        row_apply(R, row, px, st, g_ovlRowDeadline);
        R->ly = -1;
    }
}
#endif

IRAM_ATTR void ovl_chunk(uint16_t *dst, int y, int rows, uint32_t frame, void *ctx)
{
    (void)ctx;
    frame_latch(frame);
    if (s_show < 0) return;
    const int rev = s_flip ^ OVL_REV;                  // the line runs the other way on the panel
    for (int r = 0; r < rows; r++) {                   // row by row: the panel scans them in this order
        const int ly = logical(y + r);
        uint16_t *row = dst + r * OVL_LEN;
        uint16_t *px = rev ? row + OVL_LEN - 1 : row;
        const int st = rev ? -1 : 1;
        row_t *R = &s_rows[ly % ROW_AHEAD];
        if (R->ly != ly) row_prepare(R, ly, &s_bf, s_show, fill_what(y + r));   // not prepared ahead: now
        row_apply(R, row, px, st, g_ovlRowDeadline);
        R->ly = -1;
    }
}

#endif
