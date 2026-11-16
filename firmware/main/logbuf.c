/* Copyright (c) 2026 Michael Caldwell-Waller (chipguyhere). MIT License, see LICENSE in the repository root. */
/* (fork addition, 2026-11-14) the log through a buffer in the PSRAM: ESP_LOGx no longer waits for the
 * UART. at 115200 baud the screen task's 10 s statistics (~3 KB) held it ~0.3-0.5 s every 10 s, a stutter
 * on the glass. each line is formatted straight into a ring buffer (no copy on the caller's stack: some
 * tasks have 3 KB), a task of the lowest priority writes it out. a full buffer drops lines and counts
 * them rather than wait. printf() itself (the console's OWLOS lines) still goes straight out, and a crash's
 * panic output too; lines still in the buffer when the board resets are lost */
#include "logbuf.h"
#include "busy.h"

#include <stdarg.h>
#include <stdio.h>

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#if CONFIG_SPIRAM
#define LOGBUF_BYTES (64 * 1024)
static RingbufHandle_t     s_rb;
static StaticRingbuffer_t  s_rb_struct;               // the control block: internal RAM
static volatile uint32_t   s_dropped;

static int buffered_vprintf(const char *fmt, va_list ap)
{
    va_list a2;
    va_copy(a2, ap);
    const int n = vsnprintf(NULL, 0, fmt, a2);       // how long, then formatted where it goes
    va_end(a2);
    if (n <= 0) return n;
    void *at = NULL;
    if (xRingbufferSendAcquire(s_rb, &at, (size_t)n + 1, 0) != pdTRUE || !at) {
        s_dropped++;
        return n;
    }
    vsnprintf((char *)at, (size_t)n + 1, fmt, ap);
    xRingbufferSendComplete(s_rb, at);
    return n;
}

static void drain_task(void *arg)
{
    (void)arg;
    uint32_t told = 0;
    for (;;) {
        size_t len = 0;
        char *s = xRingbufferReceive(s_rb, &len, portMAX_DELAY);
        if (!s) continue;
        busy_on(BUSY_LOG);                             // TEMP (busy.h)
        if (len > 1) fwrite(s, 1, len - 1, stdout); // without its terminating 0
        vRingbufferReturnItem(s_rb, s);
        const uint32_t d = s_dropped;
        if (d != told) {
            printf("(log: %u lines dropped, the buffer was full)\n", (unsigned)(d - told));
            told = d;
        }
        fflush(stdout);
        busy_off(BUSY_LOG);
    }
}

void logbuf_start(void)
{
    if (s_rb) return;
    uint8_t *store = heap_caps_malloc(LOGBUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!store) return;
    s_rb = xRingbufferCreateStatic(LOGBUF_BYTES, RINGBUF_TYPE_NOSPLIT, store, &s_rb_struct);
    if (!s_rb) { heap_caps_free(store); return; }
    /* priority 1 on the first core: it gets the UART when nothing else wants the core. a task waiting on
     * stdout's lock behind it (a printf) lends it its priority meanwhile */
    /* its stack in the PSRAM where a flash write leaves the cache on (XIP from PSRAM): the S3's internal RAM
     * has no 3 KB to spare */
#if CONFIG_SPIRAM_XIP_FROM_PSRAM
    if (xTaskCreatePinnedToCoreWithCaps(drain_task, "log", 3072, NULL, 1, NULL, 0, MALLOC_CAP_SPIRAM) != pdPASS)
#endif
    if (xTaskCreatePinnedToCore(drain_task, "log", 3072, NULL, 1, NULL, 0) != pdPASS) {
        vRingbufferDelete(s_rb);
        heap_caps_free(store);
        s_rb = NULL;
        return;
    }
    esp_log_set_vprintf(buffered_vprintf);
}
#else
void logbuf_start(void) { }
#endif
