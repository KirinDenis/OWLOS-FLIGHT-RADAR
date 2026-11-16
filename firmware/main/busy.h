/* Copyright (c) 2026 Michael Caldwell-Waller (chipguyhere). MIT License, see LICENSE in the repository root. */
#pragma once
/* (fork addition, 2026-11-14) TEMP diagnostic: what else is going on while the fill makes a chunk. each task
 * doing work that may compete with the fill (the PSRAM, the core) sets its bit meanwhile; the fill samples the
 * bits at each chunk, and counts them over all chunks and over the late ones (lcd_jc8012.c, the 10 s log) */
#include <stdint.h>

enum {
    BUSY_DRAW,      /* the layer's builder drawing a band */
    BUSY_PACK,      /* the builder packing it into records (PSRAM) */
    BUSY_END,       /* the builder sorting the records into columns (PSRAM) */
    BUSY_BG,        /* the background task drawing or turning a strip */
    BUSY_PLAN,      /* the planner (core 0) composing chunks beforehand */
    BUSY_UI,        /* the ui task working (not waiting for the next frame) */
    BUSY_HTTP,      /* the fetch task receiving */
    BUSY_MAP,       /* the fetch task making the map */
    BUSY_PAGE,      /* a page being drawn */
    BUSY_LOG,       /* the log written out */
    BUSY_N
};
#define BUSY_NAMES { "draw", "pack", "end", "bg", "plan", "ui", "http", "map", "page", "log" }

extern volatile uint32_t g_busy;
static inline void busy_on(int b)  { __atomic_fetch_or(&g_busy, 1u << b, __ATOMIC_RELAXED); }
static inline void busy_off(int b) { __atomic_fetch_and(&g_busy, ~(1u << b), __ATOMIC_RELAXED); }
