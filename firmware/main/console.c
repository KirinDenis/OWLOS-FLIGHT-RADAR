#include "console.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "board.h"
#define CHIP_USB (SOC_USB_SERIAL_JTAG_SUPPORTED && !NO_CHIP_USB)   // the Guition 5" uses its pins for I2C
#if CHIP_USB
#include "hal/usb_serial_jtag_ll.h"
#endif

static const char *TAG = "console";
static console_line_fn s_fn;
static SemaphoreHandle_t s_one;                        // one line at a time, whichever port it came on

typedef struct { char line[1024]; int n; } line_t;

static void take(line_t *l, uint8_t ch)
{
    if (ch == '\r') return;
    if (ch == '\n') {
        l->line[l->n] = 0;
        /* (chipguyhere) from "OWLOS " on: a reset (opening the port does one) leaves a few stray bytes in the
         * UART, and glued to the front of the first line they hid its command */
        const char *at = strstr(l->line, "OWLOS ");
        if (l->n && s_fn) {
            xSemaphoreTake(s_one, portMAX_DELAY);
            s_fn(at ? at : l->line);
            xSemaphoreGive(s_one);
        }
        l->n = 0;
        return;
    }
    if (l->n < (int)sizeof l->line - 1) l->line[l->n++] = (char)ch;
    else l->n = 0;                                     // too long for anything we know: drop it
}

static void uart_task(void *arg)
{
    (void)arg;
    static line_t l;
    for (;;) {
        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(1000)) == 1) take(&l, ch);
    }
}

#if CHIP_USB
/* the chip's own USB (S3, C3, C5): the log already goes out there as well as on UART0, this reads
 * what comes in. on the ES3C28P it is the only USB there is, the NM-CYD-C5 has it beside its CH340.
 * no driver: it would take over the log's way out too. the chip holds the host back while its
 * 64 bytes are full, so looking every 10 ms loses nothing */
static void usb_task(void *arg)
{
    (void)arg;
    static line_t l;
    for (;;) {
        uint8_t buf[64];
        int n = usb_serial_jtag_ll_rxfifo_data_available() ? (int)usb_serial_jtag_ll_read_rxfifo(buf, sizeof buf) : 0;
        for (int i = 0; i < n; i++) take(&l, buf[i]);
        if (!n) vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif

void console_start(console_line_fn fn)
{
    s_fn = fn;
    s_one = xSemaphoreCreateMutex();
#if CHIP_USB
    xTaskCreate(usb_task, "console usb", 4096, NULL, 3, NULL);
#endif
#if CONFIG_ESP_CONSOLE_UART
    /* the receiving side, and a 4 KB ring for what goes out, which stdout now writes into: without it every
     * printf and log line waited for the UART at 115200 baud, spinning, a GET's CFG ~90 ms and the page's planes
     * (13 KB every 5 s) ~1.2 s of a core (the 2026-11-27 review). a full ring now blocks the task, it sleeps */
    if (uart_driver_install(UART_NUM_0, 2048, 4096, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "no UART driver: settings can't come from the site on UART0");
        return;
    }
    uart_vfs_dev_use_driver(UART_NUM_0);
#if DISP_JC8012 || DISP_WAVE7
    /* (chipguyhere) its stack in the PSRAM: no deadline, and with XIP from PSRAM nothing runs with the cache off */
    if (xTaskCreateWithCaps(uart_task, "console", 4096, NULL, 3, NULL, MALLOC_CAP_SPIRAM) != pdPASS)
#endif
    xTaskCreate(uart_task, "console", 4096, NULL, 3, NULL);
#else
    /* the log goes to the chip's USB alone (the P4: the Tab5). UART0 was never started, and the
     * driver waiting for it to answer held the core till the watchdog bit */
    (void)uart_task;
    ESP_LOGI(TAG, "the console is the chip's USB alone");
#endif
}

// finds "key": in a flat object and leaves p after the colon
static const char *field(const char *json, const char *key)
{
    size_t kl = strlen(key);
    const char *p = json;
    while ((p = strchr(p, '"')) != NULL) {
        p++;
        if (!strncmp(p, key, kl) && p[kl] == '"') {
            p += kl + 1;
            while (*p == ' ') p++;
            if (*p != ':') continue;
            p++;
            while (*p == ' ') p++;
            return p;
        }
        while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }   // skip the rest of this string
        if (*p) p++;
    }
    return NULL;
}

int json_str(const char *json, const char *key, char *out, int cap)
{
    const char *p = field(json, key);
    if (!p || *p != '"') return 0;
    p++;
    int n = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\' && *p == 'u') {                 // č: a letter written as its number, back to UTF-8
            /* up to four hex digits and no further: a text cut short after \u (Nominatim's answer is cut
             * at 2 KB) was read past its end, and a short one like \u12" took the closing quote with it */
            int k = 1;
            while (k < 5 && isxdigit((unsigned char)p[k])) k++;
            char hex[5] = { 0 };
            memcpy(hex, p + 1, (size_t)(k - 1));
            p += k;
            if (k == 1) continue;                      // \u and no number: left out
            unsigned cp = (unsigned)strtoul(hex, NULL, 16);
            char u8[3];
            int len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;
            if (len == 1) u8[0] = (char)cp;
            else if (len == 2) { u8[0] = (char)(0xC0 | cp >> 6); u8[1] = (char)(0x80 | (cp & 63)); }
            else { u8[0] = (char)(0xE0 | cp >> 12); u8[1] = (char)(0x80 | (cp >> 6 & 63)); u8[2] = (char)(0x80 | (cp & 63)); }
            if (n + len < cap) { memcpy(out + n, u8, (size_t)len); n += len; }
            continue;
        }
        if (ch == '\\' && *p) {
            ch = *p++;
            if (ch == 'n') ch = '\n';
            else if (ch == 't') ch = '\t';
        }
        if (n < cap - 1) out[n++] = ch;
    }
    out[n] = 0;
    return 1;
}

int json_num(const char *json, const char *key, double *out)
{
    const char *p = field(json, key);
    if (!p || (*p != '-' && (*p < '0' || *p > '9'))) return 0;
    *out = strtod(p, NULL);
    return isfinite(*out);                             // not -nan or -inf: a NaN home passed every range check and was kept
}
