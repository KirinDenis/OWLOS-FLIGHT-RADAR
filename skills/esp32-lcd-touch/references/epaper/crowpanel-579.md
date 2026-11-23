# Elecrow CrowPanel 5.79" e-paper (two SSD1683 in a cascade, 792x272)

ESP32-S3 (8 MB flash, 8 MB PSRAM), a 792x272 panel driven by two SSD1683 controllers, master and slave, one CS.
The shared driver: [ssd1683](ssd1683.md). Pins and the rest of the board:
[epd792](../../../esp32-boards/references/boards/epd792.md). Sureness words as in [SKILL.md](../../SKILL.md).

## State

- Ran OWLOS WiFi Radar on the panel here [measured]: full 4-grey refresh 3.1 to 3.3 s and it flashes; the partial,
  black and white on top, 1.86 s, no flash. Flight Radar's epd.c is the same file for this panel byte for byte; its
  board.h comment "NOT run" for the 5.79" in older versions was stale.
- OWLOS Flight Radar's own build for it has not run on it [built]. It has no map there: the map code takes at most
  480 wide, less than 792. The paper weather pages are the 4.2"'s; the 5.79" keeps the colour print.
- Greys as the glass shows them, measured in ViewOwl: 0, 60, 140, 255 (`PAPER_DARK` 60, `PAPER_LIGHT` 140). It
  keeps its two greys apart, unlike the 4.2" before its change.
- Two frames of 2-bit codes: 198 bytes a row x 272 = 53,856 bytes each, in PSRAM.
- Strips of 8 rows: a 792 x 24 canvas in 32 bits was 76 KB in one piece, not there after WiFi. Paper is slow
  anyway.

## The cascade

- Master gets panel columns 0 to 399, the slave 392 to 791: 8 columns hide behind the seam.
- The slave answers to the master's opcodes + 0x80: 0xA4, 0xA6, 0x91, 0xC4, 0xC5, 0xCE, 0xCF. 0x12 soft reset
  reaches both.
- With this addressing the picture comes out turned 180 degrees, so the frame is read flipped.
- The partial on the cascade uses another addressing than the full refresh: columns, `0x11 0x05` and `0x91
  0x04`, the new picture in 0x24 and 0xA4, the one on the glass in 0x26 and 0xA6, then `0x22 0xFF`, and the panel
  loads its own fast waveform. ViewOwl never had a partial on this panel; the controller takes it, and the picture
  was good on the glass [measured].
- A reset before the cascade partial: registers back to normal, the picture stays.

## The 5.79" half of epd.c

```c
#define EPD_W 792
#define EPD_H 272

static const uint8_t LUT_4GRAY[233] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x4A, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
    0x01, 0x8A, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x41, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
    0x01, 0x81, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x81, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
    0x01, 0x41, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x8A, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
    0x01, 0x4A, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00,                             // FR, XON
    0x22, 0x17, 0x41, 0xA8, 0x32, 0x40,           // EOPT VGH VSH1 VSH2 VSL VCOM
};

static void init_4gray(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_busy();
    wr_cmd(0x12);                                 // soft reset, reaches both
    wait_busy();
    wr_cmd(0x0C); wr_data(0x8B); wr_data(0x9C); wr_data(0xA6); wr_data(0x0F);   // booster soft start
    wr_cmd(0x3C); wr_data(0x81);                  // border
    wait_busy();
    // master: rows, X up. Y from 271 down to 0
    wr_cmd(0x11); wr_data(0x01);
    wr_cmd(0x44); wr_data(0x00); wr_data(0x31);
    wr_cmd(0x45); wr_data(0x0F); wr_data(0x01); wr_data(0x00); wr_data(0x00);
    wr_cmd(0x4E); wr_data(0x00);
    wr_cmd(0x4F); wr_data(0x0F); wr_data(0x01);
    wait_busy();
    // slave: same thru 0x80 opcodes, X mirrored (0x91 is its data entry register)
    wr_cmd(0x91); wr_data(0x00);
    wr_cmd(0xC4); wr_data(0x31); wr_data(0x00);
    wr_cmd(0xC5); wr_data(0x0F); wr_data(0x01); wr_data(0x00); wr_data(0x00);
    wr_cmd(0xCE); wr_data(0x31);
    wr_cmd(0xCF); wr_data(0x0F); wr_data(0x01);
    load_lut(s_lut);
}

/* four whole planes one after the other, no cursor moves in between. master gets panel
 * columns 0..399, slave 392..791 (8 hide behind the seam). with this addressing the
 * picture comes out turned 180 degrees, so we read our frame flipped */
static void push(void)
{
    init_4gray();
    const int bytes = 50;                         // 400 px of a controller row
    const uint8_t cmds[4] = { 0x24, 0x26, 0xA4, 0xA6 };
    for (int p = 0; p < 4; p++) {
        int bit = p & 1, col0 = p < 2 ? 0 : (bytes - 1) * 8;
        wr_cmd(cmds[p]);
        for (int y = 0; y < EPD_H; y++)
            for (int i = 0; i < bytes; i++) wr_data(plane8(col0 + i * 8, y, bit, 1));
    }
    wr_cmd(0x22); wr_data(0xCF);                  // show it with the grey LUT
    wr_cmd(0x20);
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_busy();
}

/* drawing on top, the cascade. NOTE: ViewOwl never had a partial refresh on this panel,
 * this is the first try: the mono addressing of ViewOwl's epd_wide.c (columns, Y down,
 * slave X mirrored, 8 px seam, turned 180), new picture in 0x24/0xA4, the one on the glass
 * in 0x26/0xA6, and the panel's own mode 2 waveform (0xFF) */
#define RAM_ROW 100                               // 800 px of RAM a row

// one byte of cascade RAM, column c (0..99) and line (0..271), from our frame
static uint8_t ram_byte(int c, int line, int old)
{
    uint8_t out = 0;
    for (int b = 0; b < 8; b++) {
        int xp = RAM_ROW * 8 - 1 - (c * 8 + b), y = EPD_H - 1 - line, v = 1;
        int x = xp < 396 ? xp : xp >= 404 ? xp - 8 : -1;   // 396..403 sit behind the seam
        if (x >= 0) v = old ? bw_old(x, y) : bw_new(x, y);
        out = (uint8_t)(out << 1 | v);
    }
    return out;
}

static void stream(int col0, int old)
{
    for (int c = col0; c < col0 + 50; c++)
        for (int line = 0; line < EPD_H; line++) wr_data(ram_byte(c, line, old));
}

static void push_partial(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));                // registers back to normal, the picture stays
    gpio_set_level(EPD_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_busy();
    wr_cmd(0x12);
    wait_busy();
    wr_cmd(0x3C); wr_data(0x80);                  // leave the border alone
    // master
    wr_cmd(0x11); wr_data(0x05);
    wr_cmd(0x44); wr_data(0x00); wr_data(0x31);
    wr_cmd(0x45); wr_data(0x0F); wr_data(0x01); wr_data(0x00); wr_data(0x00);
    for (int ram = 0; ram < 2; ram++) {
        wr_cmd(0x4E); wr_data(0x00);
        wr_cmd(0x4F); wr_data(0x0F); wr_data(0x01);
        wr_cmd(ram ? 0x26 : 0x24);
        stream(0, ram);
    }
    // slave
    wr_cmd(0x91); wr_data(0x04);
    wr_cmd(0xC4); wr_data(0x31); wr_data(0x00);
    wr_cmd(0xC5); wr_data(0x0F); wr_data(0x01); wr_data(0x00); wr_data(0x00);
    for (int ram = 0; ram < 2; ram++) {
        wr_cmd(0xCE); wr_data(0x31);
        wr_cmd(0xCF); wr_data(0x0F); wr_data(0x01);
        wr_cmd(ram ? 0xA6 : 0xA4);
        stream(50, ram);
    }
    wr_cmd(0x22); wr_data(0xFF);
    wr_cmd(0x20);
    wait_busy();
}
```

## The broken glass on our panel

- Vertical garbage at the right of the 5.79" panel here was not code: a heavy object had been dropped on it and
  about 2 cm of the right side's glass is shattered. The panel is about 139 x 48 mm of active area at 5.69 px a
  mm, so 2 cm is about 114 px, leaving about 678 of 792 that still show.
- A straight read off a photograph gave 648: perspective squeezes the far edge, so a photo understates what
  survives. Measure the panel itself.
- ViewOwl's templates for it draw nothing past 672 px and leave the rest white. WiFi Radar draws the full width on
  purpose, the crack ignored.
- Look before debugging: on another panel, vertical garbage may well be code.

## A picture rendered somewhere else

ViewOwl feeds this panel pictures that a server renders by screenshotting a web page. The screenshot was taken
mid-load and a half-drawn page reached the panel. The rule that came out: draw the picture's fixed part
synchronously, before any fetch, so any early capture is still a valid picture, and let the fetched part replace
its own area when it comes; a failure state keeps the fixed part and adds one small note [measured on that
system].
