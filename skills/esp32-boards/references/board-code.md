# Code that makes one board differ from another

From OWLOS Flight Radar's firmware/main/ unless it says otherwise. Only what a port touches: long panel init tables, strips,
DMA and touch alignment are in esp32-lcd-touch. Sureness words as in SKILL.md.

## The board.h switches, and what each one costs

A new board should be a board.h block and nothing else. Every switch below has a default that changes nothing, so the
boards that do not name it compile as before (check by building one and comparing the .bin).

How the picture gets out, one of: `DISP_LCD_SPI` (esp_lcd SPI, 8-bit commands), `DISP_ILI9486` (raw SPI, bytes padded to
16 bits), `DISP_EPD` (SSD1683 e-paper), `DISP_LCD_RGB` (a frame in PSRAM on the S3's RGB bus), `DISP_LCD_DSI` (a frame in
PSRAM on the P4's MIPI-DSI, through a board package), `DISP_WAVE7` and `DISP_JC8012` (a contributor's library owns the
panel).

The rest, with their defaults:

- `DISP_STRIP`: rows a strip. 24 at 320 wide with RAM to spare, 16 on the C3, 12 on the C5 and the RGB boards, 6 on the
  3.5" classic, 8 on the e-papers, 80 (in PSRAM) on the JC8012. A heap decision per board.
- `DISP_HELPER` (0): the second core draws every other strip, in strips and DMA buffers of its own.
  `DISP_HELPER_HALF` (0): it draws the lower half of this core's strip instead, costing only its stack (the 2.8").
- `LCD_CHIP_ILI9341`, `LCD_CHIP_ST7796`, `LCD_CHIP_GC9A01`, `LCD_CHIP_ST7789`; `LCD_MADCTL`; `LCD_SPI_HZ`.
- `LCD_INVERT` (0): INVON on the generic ST7789 (an IPS panel that wants it).
- `LCD_PROFILES`, `LCD_ORDER`: the start-ups BOOT walks through, below.
- `PIN_LCD_RST` (-1 means the software reset only), `PIN_LCD_BL` (-1: none; LEDC PWM, high = lit).
- `PIN_SD_CS`: an SD slot on the screen's bus, held high before the bus starts.
- `LCD_RGB_DATA` (16 pins, blue, green, red, lowest bit first), `PIN_LCD_HSYNC`, `_VSYNC`, `_DE`, `_PCLK`, `LCD_RGB_HZ`;
  `LCD_RGB_H_PULSE`, `_H_BACK`, `_H_FRONT`, `_V_PULSE`, `_V_BACK`, `_V_FRONT` (4, 8, 8 both ways unless named);
  `LCD_RGB_BOUNCE` (10 rows; the frame's rows must divide by it); `LCD_RGB_SYNC_IDLE_LOW`.
- `LCD_BOARD_CHIP`: the backlight and the touch's reset on a chip of the board's own (the CrowPanel Advance 5.0).
- Touch, one of: `TOUCH_XPT_BITBANG`, `TOUCH_XPT_SPI`, `TOUCH_CST816`, `TOUCH_FT6336`, `TOUCH_GT911`, `TOUCH_BSP` (the
  board package's `esp_lcd_touch`), `TOUCH_JC8012`; or none. `TOUCH_GT911_TOO`: ask for a GT911 first, then the XPT2046.
- `PIN_T_CLK`, `_MOSI`, `_MISO`, `_CS`, `_IRQ`, `_RST`, `_INT`; `PIN_T_IRQ_PULLUP` (0: the yellow boards pull it up on the
  board).
- `PIN_I2C_SDA`, `PIN_I2C_SCL`, `I2C_BUS_HZ` (400000); `GT911_STOP` (a stop between the register and the read);
  `TOUCH_PCA9557_BOOT` and `TOUCH_RESET_PCA9557` (a PCA9557 holds the GT911's reset, below); `IOEX_ADDR` and its bits (an
  expander holds the LCD's reset and backlight, the round board).
- `NO_CHIP_USB` (0): the console leaves the chip's own USB alone, because its pins are used for something else.
- `PIN_AMP_EN`: a speaker amplifier's enable, held low (off) from the start of app_main.
- `PIN_BUTTON` (-1: no BOOT the firmware can read), the e-paper's `PIN_KEY_*`.
- `PIN_LED_WS2812`: one WS2812 on this pin; without it, the boards in `RADAR_HAS_LED` get the RGB LED on three pins.

What core/config.h derives from the number: `GFX_W`/`GFX_H` (the layout), `GFX_PW`/`GFX_PH` and `GFX_SCALED` (the
glass, when the layout is scaled onto it), `RADAR_ROUND`, `RADAR_EPAPER`, `RADAR_RGB_BUS`, `RADAR_OVERLAY`,
`RADAR_OVERLAY_COLS`, `RADAR_TOUCH_CAL`, `RADAR_HAS_LED`, `RADAR_LED_RED_PIN`, `RADAR_HAS_BUTTON` (every board but the
Tab5, the plain CrowPanel 5.0 and 7.0, and the Waveshare 7/7B), `RADAR_HAS_DIM` (the light turns down: not on e-paper,
the RPi LCD, the round board or the CrowPanel Advance 5.0), `RADAR_BOARD` (what ABOUT says). Copy in board-files.md.

## Screen profiles: one build for panels nobody can tell apart

The problem: three panels under one name (2.8" CYD), or a colour order the maker's own demos disagree on. The firmware cannot
tell: it only writes to these panels, on the 2.8" CYD MISO stays off to keep strapping pin 12 alone, and reading a panel's ID
register was never tried. So the build carries every start-up and a person picks by looking.

board.h names how many and in what order BOOT walks them:

```c
#define LCD_PROFILES   3
#define LCD_ORDER      { 1, 0, 2 }    // starts as index 1, BOOT goes 1 -> 0 -> 2 -> 1
```

The list in lcd_spi.c keeps a fixed index for each start-up, since boards save that index. Change the walk with LCD_ORDER, never
the list:

```c
typedef struct { const char *name; const init_cmd_t *cmds; size_t n; } profile_t;
#define PROFILE(name, t) { name, t, sizeof t / sizeof t[0] }
static const profile_t PROFILE_LIST[] = {
#if LCD_PROFILES && LCD_CHIP_ILI9341
    PROFILE("ILI9341 V1", INIT),
    PROFILE("ILI9341 V2", INIT_V2),
    PROFILE("ST7789 V3", INIT_V3),
#elif LCD_PROFILES && LCD_CHIP_ST7789
    PROFILE("ST7789 RGB", INIT),
    PROFILE("ST7789 BGR", INIT_BGR),
#else
    PROFILE(CHIP, INIT),
#endif
};
```

The screen task (main.c):

```c
static const int ORDER[LCD_PROFILES] = LCD_ORDER;   // what BOOT goes through, the first one if none was picked
int screen = store_load_screen();                    // NVS u8 "screen", -1 when never picked
if (screen < 0 || screen >= LCD_PROFILES) screen = ORDER[0];
disp_profile(screen);                                // before disp_init: only remembered
...
int ev = button_poll(now);
/* a short BOOT press starts the screen as the next revision: on the touch alignment page (the first thing
 * after flashing) or in the first seconds after power on, both say so on the screen */
if (ev == BTN_SHORT && (s_ui.view == VIEW_CAL || now < UI_SCREEN_PICK_MS)) {    // UI_SCREEN_PICK_MS 8000
    int at = 0;
    while (at < LCD_PROFILES - 1 && ORDER[at] != screen) at++;
    screen = ORDER[(at + 1) % LCD_PROFILES];
    pick_screen(screen, now, "BOOT");                // disp_profile, save, toast with its name
    ev = 0;
}
```

Every profile starts with a software reset, so one can follow another with the board running. Waiting for the strip in flight comes
first (`xSemaphoreTake(s_done, ...)`), then `run_profile()`.

Why BOOT and not a setting on the site: a screen select in the settings proved confusing and was taken off. `SET
{"screen":n}` stays for scripts. The site says it loud, in a block under the cards. Why a wrong start-up cannot be fixed by touch:
with a mirrored or negative picture the touch alignment makes no sense to a person. BOOT is a real button that always works.

### The two CYD start-ups that differ from plain ILI9341 and ST7789

From rzeldent/platformio-espressif32-sunton, the orientation turned into our landscape. Built, only v1 has been on the desk.

```c
static const init_cmd_t INIT_V2[] = {                         /* USB-C: ILI9341, its own power and gamma */
    { 0x01, 0, { 0 }, 120 },
    { 0xCF, 3, { 0x00, 0xC1, 0x30 }, 0 },
    { 0xED, 4, { 0x64, 0x03, 0x12, 0x81 }, 0 },
    { 0xE8, 3, { 0x85, 0x00, 0x78 }, 0 },
    { 0xCB, 5, { 0x39, 0x2C, 0x00, 0x34, 0x02 }, 0 },
    { 0xF7, 1, { 0x20 }, 0 },
    { 0xEA, 2, { 0x00, 0x00 }, 0 },
    { 0xC0, 1, { 0x10 }, 0 },
    { 0xC1, 1, { 0x00 }, 0 },
    { 0xC5, 2, { 0x30, 0x30 }, 0 },
    { 0xC7, 1, { 0xB7 }, 0 },
    { 0x3A, 1, { 0x55 }, 0 },
    { 0x36, 1, { LCD_MADCTL }, 0 },                          /* same way round as v1 */
    { 0xB1, 2, { 0x00, 0x1A }, 0 },
    { 0xB6, 3, { 0x08, 0x82, 0x27 }, 0 },
    { 0xF2, 1, { 0x00 }, 0 },
    { 0x26, 1, { 0x01 }, 0 },
    { 0xE0, 15, { 0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00 }, 0 },
    { 0xE1, 15, { 0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F }, 0 },
    { 0x21, 0, { 0 }, 0 },                                   /* inversion on: this panel needs it */
    { 0x11, 0, { 0 }, 120 },
    { 0x29, 0, { 0 }, 20 },
};
/* v3: an ST7789 on the chip's own settings. against v1 it is RGB not BGR and mirrored on one axis, so MADCTL
 * loses BGR and MX: 0xE8 -> 0xA0 */
static const init_cmd_t INIT_V3[] = {
    { 0x01, 0, { 0 }, 150 },                                  /* software reset */
    { 0x11, 0, { 0 }, 120 },                                  /* sleep out */
    { 0x3A, 1, { 0x55 }, 10 },                               /* 16 bits a pixel */
    { 0x36, 1, { 0xA0 }, 0 },                                /* MY|MV, RGB */
    { 0x21, 0, { 0 }, 0 },                                   /* inversion on */
    { 0x13, 0, { 0 }, 10 },                                  /* normal display mode */
    { 0x29, 0, { 0 }, 20 },
};
```

The generic ST7789 (NM-CYD-C5, CrowPanel Advance) is INIT_V3 with `LCD_MADCTL` and a switch for inversion, and a BGR twin with
`LCD_MADCTL | 0x08`:

```c
#ifndef LCD_INVERT
#define LCD_INVERT 0
#endif
#define ST7789_INV (LCD_INVERT ? 0x21 : 0x20)   /* board.h: 1 on an IPS panel that wants it */
```

Adding that switch left the NM-CYD-C5's binary byte-identical (built): the proof a shared driver change did nothing to the
other board.

### Profiles on an RGB bus

An RGB panel has no MADCTL and nothing to ask, and the sources name the pins of red and blue differently. The second
profile ("BGR") brings the panel up again with the two groups of five data pins traded (lcd_rgb.c); blue is the low
five:

```c
if (s_profile) {
    for (int i = 0; i < 5; i++) {
        int t = cfg.data_gpio_nums[i];
        cfg.data_gpio_nums[i] = cfg.data_gpio_nums[11 + i];
        cfg.data_gpio_nums[11 + i] = t;
    }
}
```

On the boards whose BOOT cannot be read (the plain CrowPanel 5.0 and 7.0) the site or `SET {"screen":n}` picks.

## MADCTL by board

Bits: 0x80 MY, 0x40 MX, 0x20 MV (swap: landscape on a panel made standing), 0x10 ML, 0x08 BGR, 0x04 MH.

```text
0xE8  MY|MX|MV|BGR   2.8" v1 and v2, 3.5" ST7796
0x28  MV|BGR         ES3C28P, ILI9486
0xA0  MY|MV          2.8" v3, NM-CYD-C5, CrowPanel Advance (+0x08 in its BGR profile)
0x68  MX|MV|BGR      round GC9A01
0x60  MX|MV          ZLX-ESP32-2 (its BGR profile, first, sends 0x68: the maker's TFT_eSPI rotation 1)
```

`m ^ 0xC0` is the same landscape turned 180 degrees: 0xE8 and 0x28 are one pair, 0xA0 and 0x60 the other. That is the
"upside down" setting on every SPI board (`disp_flip`: MX and MY of the profile's own MADCTL flipped), and the touch
alignment is turned with it, so no new crosses. On the RGB and DSI boards the driver turns the strips itself; on the
JC8012 the setting's sense is inverted, so that "normal" is camera up:

```c
c->a = -c->a; c->b = -c->b; c->c = (float)(GFX_W - 1) - c->c;
c->d = -c->d; c->e = -c->e; c->f = (float)(GFX_H - 1) - c->f;
```

So on a new board only one question is open, which landscape is up, and it costs one XOR to change.

## Reset and backlight behind an expander: the round CrowPanel

PI4IOE5V6408 at 0x43, sent before any SPI (lcd_spi.c, measured). The I2C bus has to be up first, and the touch on the same bus
gets 50 ms more after this:

```c
#define IOEX_ADDR      0x43
#define IOEX_BIT_BL    (1 << 2)
#define IOEX_BIT_TRST  (1 << 3)  // touch reset
#define IOEX_BIT_RST   (1 << 4)  // LCD reset

static void expander_power_on(void)
{
    i2c_master_dev_handle_t ex = i2c_dev(IOEX_ADDR);
    const uint8_t mask = IOEX_BIT_BL | IOEX_BIT_RST | IOEX_BIT_TRST;
    const uint8_t seq[][2] = {
        { 0x03, mask },                      /* direction: these are outputs */
        { 0x07, (uint8_t)~mask },            /* hi-z off for them */
        { 0x0B, 0x00 },                      /* no pulls */
        { 0x05, IOEX_BIT_BL },               /* resets low, backlight on */
    };
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++)
        if (i2c_master_transmit(ex, seq[i], 2, 100) != ESP_OK) ESP_LOGE(TAG, "expander write %02x failed", seq[i][0]);
    vTaskDelay(pdMS_TO_TICKS(20));
    i2c_master_transmit(ex, (const uint8_t[]){ 0x05, mask }, 2, 100);   /* resets released */
    vTaskDelay(pdMS_TO_TICKS(120));
}
```

## Backlight and touch reset on a chip of the board's own: the CrowPanel Advance 5.0

`LCD_BOARD_CHIP` (lcd_rgb.c, built; a tester's V1.3 lights and its touch answers). Each board version its own way,
printed on the board:

- V1.0: a PCA9557 at 0x18. Register 1 to 0 (backlight off, touch held in reset), register 3 to 0xE1 (bits 1 to 4
  outputs, as Elecrow's V1.0 demos), the touch's INT line low, 20 ms, register 1 to 0x04, 100 ms, INT let go (an input:
  the chip drives it too, it may never be driven high). The light: register 1 to 0x06.
- From V1.1: an STC8H1K28 at 0x30 that takes one byte. Up to five tries: if the chip or the touch at 0x5D is silent, send
  0x19 and 250, hold the touch's INT low for 120 ms, let it go, wait 100 ms. Elecrow's demos try for ever; a dark board
  that says so in the log is better than one that hangs. The light: 0x10, sent only once the first frame is in, so no
  noise shows at start. No dimming.

## A PCA9557 in front of the GT911: the plain CrowPanels

touch.c, built. Both boards keep the GT911's reset on the expander's P0 (IO0) and its INT on P1 (IO1).

- The plain CrowPanel 5.0 (`TOUCH_PCA9557_BOOT 0x18`): the sequence the P2000 firmware runs on it, every start, before
  the touch is asked for. Register writes in order: 3 = 0xFF, 1 = 0xFF, 2 = 0x00, 3 = 0x00, 1 = 0xFE, 1 = 0xFC; 20 ms;
  1 = 0xFD; 100 ms; 3 = 0xFE; 60 ms.
- The CrowPanel 7.0 (`TOUCH_RESET_PCA9557 0x18`): only when the GT911 does not answer. ESPHome's people found a reset at
  every start broke the touch, and an older V3.0 example of Elecrow's leaves the chip alone. Then Elecrow's own order,
  INT low through the reset for 0x5D: 1 = 0xFC (levels first), 3 = 0xFC (then those two driven), 20 ms, 1 = 0xFD (out
  of reset, INT still low), 100 ms, 3 = 0xFE (INT is the touch's again), 60 ms. IO2 and up, the panel's reset and power
  among them, stay as they are.

## A speaker amplifier held off

The ZLX-ESP32-2's SC8002B (`PIN_AMP_EN 24`, high = on). An enable left floating may hum; the radar is silent. First thing
in app_main (main.c, built):

```c
#ifdef PIN_AMP_EN
    gpio_set_level(PIN_AMP_EN, 0);
    gpio_config_t amp = { .pin_bit_mask = 1ULL << PIN_AMP_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&amp);
#endif
```

The yellow LCDWIKI boards are the other way round: their amplifier is on while GPIO 4 is low, and GPIO 4 is the red LED
on a Sunton. The radar never drives 4 unless the red LED is set there.

## An SD slot on the screen's bus

NM-CYD-C5 and ZLX-ESP32-2. Its chip select goes up before the bus starts and stays up, or a card in the slot answers too
(built):

```c
#ifdef PIN_SD_CS
    gpio_config_t sd = { .pin_bit_mask = 1ULL << PIN_SD_CS, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&sd);
    gpio_set_level(PIN_SD_CS, 1);
#endif
```

Same rule for any device on a shared bus the firmware does not drive.

## Touch: asking who is there

The board file is the truth, there is no general I2C scan (autoscan was tried in old OWLOS and rejected). The one
exception is a short known list that decides between versions of one board.

3.5" R or C: the GT911's I2C pins are the XPT2046's CS on the other version, so the probe must give the pin back:

```c
void tp_init(void)
{
#if TOUCH_GT911_TOO
    if (gt_find()) return;
    i2c_bus_free();                 // nobody there: 33 is the XPT2046's chip select again
#endif
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 2 * 1000 * 1000,       // the XPT2046 wants it slow
        .mode = 0,
        .spics_io_num = PIN_T_CS,
        .queue_size = 1,
    };
    ...
}

static int gt_find(void)            // both addresses, then the id says it really is one
{
    const uint8_t addr[2] = { 0x5D, 0x14 };
    for (int i = 0; i < 2; i++) {
        if (!i2c_probe(addr[i])) continue;
        s_gt = i2c_dev(addr[i]);
        uint8_t id[4] = { 0 };
        if (gt_reg(0x8140, id, 4) && id[0] == '9' && id[1] == '1' && id[2] == '1') return 1;
        i2c_master_bus_rm_device(s_gt);          // something else answered, not ours
        s_gt = NULL;
    }
    return 0;
}
```

Capacitive chips that answer on I2C:

- CST816 at 0x15. Needs register 0xFE = 1, or it dozes off and ignores I2C until touched. Read from 0x02: fingers, X hi,
  X lo, Y hi, Y lo. Span 240.
- FT6336 at 0x38. Needs RST low 10 ms, up, then 300 ms before it answers. Read the same way; fingers > 2 is a register
  not ready. Span 320 (it counts screen pixels, 240x320 standing).
- GT911 at 0x5D or 0x14. Status 0x814E bit 7 is fresh, write 0 back; silence of 300 ms = lifted. Read 0x8150: X lo,
  X hi, Y lo, Y hi. It counts screen pixels: span `GFX_PW`, the glass's width lying down (480 on the 3.5"). Some panels'
  GT911 want a stop between the register and the read, a repeated start comes back empty (`GT911_STOP`, the plain
  CrowPanel 5.0).
- The board package's touch on the Tab5 and the GSL3680 on the JC8012 count the standing glass's pixels: span `GFX_PW`.
- The XPT2046 has no address and a raw span of about 3700.

Span is the raw units across the screen, for the sanity check of a stored alignment: `want = GFX_W / span`, the saved alignment's scale
must be within 3x of it or the crosses come back. Getting it wrong sends the board to the alignment page on every start.
Mirrors and swaps need no code: the 5 cross alignment finds them on every kind.

The FT6336 costs two lines because its first finger reads as the CST816's, so it shares the code path:

```c
uint8_t reg = 0x02, b[5];                     // finger count, X high, X low, Y high, Y low
if (i2c_master_transmit_receive(s_dev, &reg, 1, b, sizeof b, 20) != ESP_OK) return 0;
const int fingers = b[0] & 0x0F;
if (!fingers) return 0;
#if TOUCH_FT6336
if (fingers > 2) return 0;
#endif
*rx = ((b[1] & 0x0F) << 8) | b[2];
*ry = ((b[3] & 0x0F) << 8) | b[4];
```

If the chip does not answer the firmware goes on with no touch, BOOT still works: a board with a wrong touch guess still shows the
picture, which is worth a lot on a board you cannot see.

XPT2046 on the LCD's bus (3.5" CYD, NM-CYD-C5): before a transfer wait for the strip in flight, the touch never asks for the bus
against it. Waiting in the driver's bus lock was what hung in `spi_device_acquire_bus`, seen in a core dump. The pen IRQ keeps the
touch off a busy bus (a third of the frame on the 3.5"), and it does not have to be believed:

```c
static void irq_init(void)
{
    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_T_IRQ, .mode = GPIO_MODE_INPUT };
    gpio_config(&in);
    chip_select(1);
    xfer(0xD0);                                      // power down, pen interrupt on
    chip_select(0);
    s_irq = gpio_get_level(PIN_T_IRQ) == 1;          // low with nobody pressing: not wired, or a lie
}
...
int idle = s_irq && gpio_get_level(PIN_T_IRQ);
if (idle && ++n % 20) return 0;                      // not pressed, says the pin. every 20th: ask anyway
...
if (pressed && idle) s_irq = 0;                      // a press the pin did not show: stop believing it
```

That is how one build deals with a board where most ports ignore the pin (NM-CYD-C5).

A pen IRQ with no pull-up on the board (the ZLX-ESP32-2, whose maker's test turns the chip's on) sets
`PIN_T_IRQ_PULLUP 1`; on the yellow boards GPIO 36 has no pull-up of its own and the board pulls it up.

Since 0.16.1 (measured on the 2.8" and the 3.5"): a press counts once two reads in a row say it, 15 ms later than
before; one noisy read of the XPT2046 used to become a tap, and on the 3.5" it went on to the next look by itself after a
restart and stopped trusting its pen interrupt. On every board the glass is not read in the first 1.5 s after power on,
and a press held from then on counts only once it has lifted.

## LED

Two kinds, one interface (`led_init`, `led_set(rgb)`), led.c picks by board.h.

RGB on three pins, each pulled to ground to light (CYDs). LEDC on a timer of its own, the backlight keeps timer 0 and channel 0:

```c
ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                          .timer_num = LEDC_TIMER_1, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
ledc_channel_config_t ch = {
    .gpio_num = s_pin[i], .speed_mode = LEDC_LOW_SPEED_MODE, .channel = CH[i],   // channels 1, 2, 3
    .timer_sel = LEDC_TIMER_1, .duty = 0, .hpoint = 0,
    .flags.output_invert = 1,                          // duty 0 is dark
};
```

Red moves between 4 and 22 by setting, and the old pin is let go first. Never both: on LCDWIKI's boards 4 switches the speaker's amp.

```c
void led_red(int gpio)
{
    if (!s_ready || gpio == s_pin[0]) return;
    gpio_reset_pin(s_pin[0]);          // off the LEDC: an input with its pull-up, nothing driven
    s_pin[0] = gpio;
    channel(0);
}
```

WS2812 on one pin (ES3C28P, NM-CYD-C5). RMT with a bytes encoder, GRB, no component to download (built, not run):

```c
rmt_tx_channel_config_t ch = {
    .gpio_num = PIN_LED_WS2812,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10 * 1000 * 1000,                 // a tick is 0.1 us
    .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,   // 64 on classic, 48 on the others, 24 bits fit
    .trans_queue_depth = 1,
};
rmt_bytes_encoder_config_t bits = {
    .bit0 = { .level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9 },
    .bit1 = { .level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3 },
    .flags.msb_first = 1,
};
```

Keep the 3 byte buffer static: the RMT reads it after `rmt_transmit` is back. Before writing it, `rmt_tx_wait_all_done(s_ch, 20)`.

Finding an LED's pin on a board you cannot see: hold ONE pin low for minutes and ask a yes or no. A blink-count scan over many pins
was too hard to read (measured, that is how 22 was found). The Thermo Relay's way is good too: at power on it lights red, green, blue
for 1.5 s each while the screen names the colour and its pin.

## USB console on both ports

Log and reading on UART0 as always, with a second reader on the chip's own USB. No usb_serial_jtag driver: it would take the log's way
out too. The low level functions read the 64 byte FIFO, and the chip holds the host back while it is full, so looking every 10 ms
loses nothing. A mutex keeps one line at a time, whichever port it came on (console.c, built: all boards on the desk use a bridge).

```c
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "hal/usb_serial_jtag_ll.h"
#endif

typedef struct { char line[1024]; int n; } line_t;

#if SOC_USB_SERIAL_JTAG_SUPPORTED
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
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    xTaskCreate(usb_task, "console usb", 4096, NULL, 3, NULL);
#endif
    if (uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0) != ESP_OK) return;   // the log keeps writing to UART0
    xTaskCreate(uart_task, "console", 4096, NULL, 3, NULL);
}
```

`SOC_USB_SERIAL_JTAG_SUPPORTED` compiles it out on the classic ESP32, no board switch needed. A board whose USB pins do
something else needs one: `#define CHIP_USB (SOC_USB_SERIAL_JTAG_SUPPORTED && !NO_CHIP_USB)` stands where the code above
says `SOC_USB_SERIAL_JTAG_SUPPORTED`; the S3 RGB boards with a CH340 have their touch's I2C on 19 and 20, the ZLX-ESP32-2
its backlight and DC on 13 and 14. The P4 boards set their console in sdkconfig instead (USB-Serial-JTAG on the Tab5,
UART0 on the JC8012). The console takes a line from "OWLOS " on, so stray bytes a restart leaves in front of a command do
not hide it. Start the console last in app_main:
the site sends SET the moment it reads READY, and a SET before the fetch task exists hit an assert (measured).

Junk from the reset at connect glues onto the first command, so the site sends "\n" before each line and the console skips empty
lines (measured: GET at READY answered 2 of 6, with "\n" first 6 of 6).

## main/CMakeLists.txt: every board's driver is in the build

Each driver file compiles to nothing unless board.h picks it (`#if DISP_LCD_SPI` round the whole file). One SRCS list, and a new board
touches no CMake there. REQUIRES names `esp_driver_rmt` for the WS2812 on all boards, the ones without a WS2812 just do not use it.

A component only one chip has goes in by target, so the others never see it:

```cmake
# the P4 (the Tab5) turns its strips with the chip's PPA, a component the other chips do not have
set(TAB5_REQ "")
if(IDF_TARGET STREQUAL "esp32p4")
    set(TAB5_REQ esp_driver_ppa)
endif()
```

Registry components the same way, by a rule in main/idf_component.yml (`rules: - if: "target == esp32p4"`), and vendored
board libraries as components under firmware/components whose own CMake checks `RADAR_DISPLAY` (chipguy_lcd7_7b,
chipguy_jc8012). Copies in board-files.md.

A sdkconfig line that only some boards' code relies on can guard itself: lcd_wave7.c stops the build with an `#error`
when the build folder's sdkconfig predates `sdkconfig.defaults.wave7`, because such a folder hung on a dark screen.

A name clash to know: `touch_init` and `touch_read` clash with something inside ESP-IDF's librtc.a, so the touch functions are `tp_*`
(measured).
