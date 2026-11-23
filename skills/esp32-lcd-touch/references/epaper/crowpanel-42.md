# Elecrow CrowPanel 4.2" e-paper (one SSD1683, 400x300)

ESP32-S3 N8R8, 400x300, one SSD1683, five keys. The shared driver: [ssd1683](ssd1683.md). Pins and the rest of the
board: [epd400](../../../esp32-boards/references/boards/epd400.md). Sureness words as in [SKILL.md](../../SKILL.md).

## State

- Runs OWLOS Flight Radar on the board on our desk [measured]: 4 greys after the waveform change below, drawn on
  top in about 1.2 s, a full 4-grey refresh for every new screen. The paper's own screens since then: every partial
  had grey debt 0 on the sheet; labels over the woods' grey add 150 to 300 px of debt a frame, so the debt rule
  brings a full refresh now and then (3 minutes apart at most).
- Changed since the first version of these notes: this panel was "built, preview only"; it runs now.
- The start-up sequence is ViewOwl's epd_4in2.c (Waveshare's 4.2" V2), where it ran first.

## The greys

- ViewOwl's waveform gave 0, 50, 220 and 255 of 255: it pulls its two greys towards black and white, and the light
  grey was not to be seen on the glass [measured].
- A grey is the third group of a block, repeated 5 times: frames towards white, then towards black. As it came the
  greys were 8:3 (light) and 8:5 (dark). One frame of white more or less each: light 7:3 (byte 141 = 0x47), dark
  9:5 (byte 99 = 0x49). A photo of the test bands then showed four clear tones [measured].
- `PAPER_DARK` 95 and `PAPER_LIGHT` 185 in paper.h are read off that photo [measured, by eye from a photo].
- The colour looks' ink steps in paper.c were tuned for the old greys and not checked again since.
- A tint (220 of 255) or sparse dots are invisible on this glass: a visible step must be a whole grey.

## The 4.2" half of epd.c

```c
#define EPD_W 400
#define EPD_H 300

/* ViewOwl's (Waveshare's 4.2" V2), the two greys moved to the middle. a grey is the third group,
 * repeated 5 times: frames towards white, then towards black. as it came the greys were 8:3 and 8:5,
 * 220 and 50 of 255, the light one not to be seen on the glass. a frame more or less of white each
 * (OWLOS EPDLUT 99 49, 141 47, then OWLOS EPDTEST) and the photo had four tones */
static const uint8_t LUT_4GRAY[233] = {
    0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01, 0x01,
    0x05, 0x0A, 0x01, 0x0A, 0x01, 0x01, 0x01,
    0x05, 0x08, 0x03, 0x02, 0x04, 0x01, 0x01,
    0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01, 0x01,
    0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01,
    0x05, 0x48, 0x03, 0x82, 0x84, 0x01, 0x01,
    0x01, 0x84, 0x84, 0x82, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x0A, 0x1B, 0x8F, 0x03, 0x01, 0x01,
    0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01,
    0x05, 0x49, 0x83, 0x82, 0x04, 0x01, 0x01,     // dark grey: 9 frames white to 5 black (was 8, it came out all but black)
    0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x8A, 0x1B, 0x8F, 0x03, 0x01, 0x01,
    0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01,
    0x05, 0x47, 0x83, 0x02, 0x04, 0x01, 0x01,     // light grey: 7 frames white to 3 black (was 8, it came out all but white)
    0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x8A, 0x9B, 0x8F, 0x03, 0x01, 0x01,
    0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01,
    0x05, 0x48, 0x03, 0x42, 0x04, 0x01, 0x01,
    0x01, 0x04, 0x04, 0x42, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x07, 0x17, 0x41, 0xA8,
    0x32, 0x30,
};

static void reset(void)
{
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(EPD_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void window_all(void)
{
    wr_cmd(0x44); wr_data(0); wr_data((EPD_W - 1) >> 3);          // X in bytes
    wr_cmd(0x45); wr_data(0); wr_data(0); wr_data((EPD_H - 1) & 255); wr_data((EPD_H - 1) >> 8);
    wr_cmd(0x4E); wr_data(0);
    wr_cmd(0x4F); wr_data(0); wr_data(0);
}

static void init_4gray(void)
{
    reset();
    wait_busy();
    wr_cmd(0x12);                                 // soft reset
    wait_busy();
    wr_cmd(0x21); wr_data(0x00); wr_data(0x00);   // display update control 1
    wr_cmd(0x3C); wr_data(0x03);                  // border
    wr_cmd(0x0C); wr_data(0x8B); wr_data(0x9C); wr_data(0xA4); wr_data(0x0F);   // booster soft start
    load_lut(s_lut);
    wr_cmd(0x11); wr_data(0x03);                  // X+, Y+
    window_all();
    wait_busy();
}

static void push(void)
{
    init_4gray();
    for (int bit = 0; bit < 2; bit++) {
        window_all();
        wr_cmd(bit ? 0x26 : 0x24);
        for (int y = 0; y < EPD_H; y++)
            for (int x = 0; x < EPD_W; x += 8) wr_data(plane8(x, y, bit, 0));
    }
    wr_cmd(0x22); wr_data(0xCF);                  // show it with the grey LUT
    wr_cmd(0x20);
    wait_busy();
}

/* drawing on top: ViewOwl's partial (its Class-C player runs on it) plus the old picture
 * in RAM 0x26, so unchanged pixels get no drive at all. 0xFF makes the panel load its own
 * fast waveform again, the grey one from push() is still in the registers */
static void push_partial(void)
{
    wr_cmd(0x3C); wr_data(0x80);
    wr_cmd(0x21); wr_data(0x00); wr_data(0x00);
    wr_cmd(0x3C); wr_data(0x80);
    wr_cmd(0x11); wr_data(0x03);
    for (int ram = 0; ram < 2; ram++) {           // 0x24 the new picture, 0x26 the one on the glass
        window_all();
        wr_cmd(ram ? 0x26 : 0x24);
        for (int y = 0; y < EPD_H; y++)
            for (int x = 0; x < EPD_W; x += 8) {
                uint8_t b = 0;
                for (int k = 0; k < 8; k++) b = (uint8_t)(b << 1 | (ram ? bw_old(x + k, y) : bw_new(x + k, y)));
                wr_data(b);
            }
    }
    wr_cmd(0x22); wr_data(0xFF);
    wr_cmd(0x20);
    wait_busy();
}
```

The old LUT had 0x48 in both changed bytes (99 and 141); the rest is ViewOwl's.

## The keys

- Five: the dial (up, down, pressed), MENU and EXIT (BACK), plus BOOT. All active low on the chip's pull-up, by
  Elecrow's examples. The pins in the board file are the 5.79"'s, the same in Elecrow's 4.2" wiki and in a
  published pinout of the 4.2"; read over the console here, not yet pressed by hand.
- Where they sit: on the right edge of the screen as drawn, from the top a key, the dial, a key [measured, from a
  photo of the board]. Which key is MENU and which EXIT is a guess: MENU the top one [reasoned]. The wiki and the
  pinouts do not say.
- How the radar shows and reads them: [paper-screen](paper-screen.md), [buttons-keys](../buttons-keys.md).

## Memory and maps

- Two frames of 2-bit codes, 30,000 bytes each, in PSRAM.
- Strips of 8 rows.
- The radar's disc map is 2 x its radius wide; its rows are counted in 8-pixel words, so a width not a multiple of
  8 wrote past each band and crashed the board when the map came. The width is rounded up to 8 now, and the map's
  allocator refuses anything else [measured on this board].
