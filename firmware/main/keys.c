#include "keys.h"
#include "board.h"
#include "ui.h"

#ifdef PIN_KEY_OK
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* all low when pressed, on the chip's pull-up (Elecrow's examples say the same, and a key that is not
 * there reads a steady "not pressed"). a key counts once it reads the same for 30 ms. the dial's press
 * and MENU are held for something else too (700 ms), so those two count on release, the rest when
 * pressed. they are read on a task of their own every 10 ms: the UI's task waits a second, or six,
 * while the glass changes, and a flick of the dial is far shorter (the WiFi radar's keys.c, so) */
#define STABLE_MS 30u
#define LONG_MS   700u
#define EVERY_MS  10

static const struct { int pin, key, longkey; } KEYS[] = {
    { PIN_KEY_UP, KEY_UP, 0 }, { PIN_KEY_DOWN, KEY_DOWN, 0 }, { PIN_KEY_OK, KEY_OK, KEY_OK_LONG },
    { PIN_KEY_MENU, KEY_MENU, KEY_MENU_LONG }, { PIN_KEY_BACK, KEY_BACK, 0 },
};
#define NK (int)(sizeof KEYS / sizeof KEYS[0])

static QueueHandle_t s_q;

static void keys_task(void *arg)
{
    (void)arg;
    uint8_t raw_was[NK] = { 0 }, down[NK] = { 0 }, held[NK] = { 0 };
    uint32_t change[NK] = { 0 }, since[NK] = { 0 };
    for (;;) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        for (int i = 0; i < NK; i++) {
            int ev = 0;
            uint8_t raw = gpio_get_level(KEYS[i].pin) == 0;
            if (raw != raw_was[i]) {
                raw_was[i] = raw;
                change[i] = now;
            }
            if (raw == down[i] || now - change[i] < STABLE_MS) {
                // held: a key with a second meaning gives it once, while still held
                if (down[i] && KEYS[i].longkey && !held[i] && now - since[i] >= LONG_MS) {
                    held[i] = 1;
                    ev = KEYS[i].longkey;
                }
            } else {
                down[i] = raw;
                if (KEYS[i].longkey) {
                    if (raw) {
                        since[i] = now;
                        held[i] = 0;
                    } else if (!held[i]) {
                        ev = KEYS[i].key;                  // a short press counts as it is let go
                    }
                } else if (raw) {
                    ev = KEYS[i].key;
                }
            }
            if (ev) xQueueSend(s_q, &ev, 0);               // eight waiting already: this one is dropped
        }
        vTaskDelay(pdMS_TO_TICKS(EVERY_MS));
    }
}

void keys_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < NK; i++) mask |= 1ULL << KEYS[i].pin;
    gpio_config_t c = { .pin_bit_mask = mask, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&c);
    s_q = xQueueCreate(8, sizeof(int));
    xTaskCreate(keys_task, "keys", 2048, NULL, 4, NULL);
}

int keys_poll(void)
{
    int ev = 0;
    if (s_q && xQueueReceive(s_q, &ev, 0) == pdTRUE) return ev;
    return 0;
}

#else

void keys_init(void) { }
int keys_poll(void) { return 0; }

#endif
