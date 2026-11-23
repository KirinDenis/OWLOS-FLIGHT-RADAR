# Buttons and keys: BOOT, and the e-paper boards' dial and keys

Sources: OWLOS Flight Radar `firmware/main/button.c`, `button.h`, `keys.c`, `keys.h`; OWLOS WiFi Radar
`firmware/main/keys.c`. Sureness words as in [SKILL.md](../SKILL.md).

## BOOT

Polled from the screen task, no interrupt, no task of its own [measured on the CYDs and the round C3].

```c
enum { BTN_NONE, BTN_SHORT, BTN_LONG };

void button_init(void);
/* call every frame. long press fires at 700 ms while still held, so the turn
 * starts without waiting for release. short press fires on release */
int  button_poll(uint32_t now_ms);
```

```c
// BOOT button, polled from the screen task
#include "button.h"
#include "board.h"
#include "driver/gpio.h"

#define LONG_MS 700u            // long press

#if PIN_BUTTON < 0
/* the Tab5: nothing on a pin, its power button is the charger chip's */
void button_init(void) { }
int button_poll(uint32_t now) { (void)now; return BTN_NONE; }
#else

static int      s_down;
static uint32_t s_since;
static int      s_fired;

void button_init(void)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&c);
}

int button_poll(uint32_t now)
{
    int down = gpio_get_level(PIN_BUTTON) == 0;
    int ev = BTN_NONE;
    if (down && !s_down) {
        s_since = now;
        s_fired = 0;
    } else if (down && !s_fired && now - s_since >= LONG_MS) {
        s_fired = 1;
        ev = BTN_LONG;                         // fires while held, the action starts at once
    } else if (!down && s_down && !s_fired && now - s_since >= 30u) {
        ev = BTN_SHORT;                        // on release. under 30 ms it's just contact bounce
    }
    s_down = down;
    return ev;
}
#endif
```

- Pins: 0 on the ESP32 and the S3, 9 on the C3, 28 on the C5, 35 on the Guition JC8012 (P4, pulled up on the
  board).
- No BOOT the firmware can read (`PIN_BUTTON -1`): the Tab5 (its buttons go to the charger chip and the C6, not the
  P4); the plain CrowPanel 5.0 and 7.0 (GPIO 0 is the panel's pixel clock: pressing BOOT while the screen runs
  shorts the clock); the Waveshare 7 and 7B (GPIO 0 is a panel data line). The pages say what BOOT does only where
  there is one.
- What a press does is the screen's business. On a board with no touch BOOT is the whole interface: a short press
  steps through the items, a long one changes the mode. In the first 8 s after power on, and on the touch alignment
  page, a short press steps the screen profile ([SKILL.md](../SKILL.md), section 6); a long press on that page skips
  the alignment.
- Held through a reset, BOOT puts the chip in download mode. The sequence to tell people: press RST, then BOOT,
  never held together.

## The e-paper boards' keys

The Elecrow CrowPanel e-paper boards have a dial (up, down, pressed), MENU and EXIT (called BACK in the code),
besides BOOT. Two side keys are enough to drive the radar: MENU, held for the long action, and EXIT.

- All active low on the chip's pull-up (Elecrow's examples say the same), so a board without the part reads a
  steady "not pressed" instead of noise.
- A key counts once it reads the same for 30 ms.
- The dial's press and MENU have a long action (700 ms), so those two count on release and give the long one while
  still held; the rest count when pressed.
- **Read on a task of their own every 10 ms**, into a queue of 8 (a ninth waiting press is dropped). The screen
  task waits a second, or six, while the glass changes, and a flick of the dial is far shorter. Changed since the
  first version of these notes: the keys were polled from the screen task then.

```c
/* keys - the dial (up, down, pressed) and the MENU and EXIT keys of the Elecrow CrowPanel e-paper boards.
 * BOOT is apart, button.h. a board without them never gets a key */
#pragma once

void keys_init(void);
/* the next key pressed, one of ui.h's KEY_*, or 0. the presses wait in a queue meanwhile: the glass
 * takes seconds to change, and the UI's task waits that long */
int  keys_poll(void);
```

```c
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
```

- How sure: the pins from Elecrow's 5.79" key example [docs], the 4.2" the same by Elecrow's wiki and a published
  pinout [docs]; on the 4.2" the key handling ran through the console's test key [measured], the keys not yet
  pressed by hand. Where they sit on the 4.2": [crowpanel-42](epaper/crowpanel-42.md).
- How the radar shows them on the paper (the key strip, gathered presses): [paper-screen](epaper/paper-screen.md).
