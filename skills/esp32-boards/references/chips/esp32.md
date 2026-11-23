# ESP32 (classic)

The workhorse under the 2.8" and 3.5" yellow boards. Two cores and WiFi, no PSRAM on most screen boards, so no frame
buffer: everything is drawn in strips. Sureness words as in SKILL.md: measured, built, docs, reasoned.

Raw numbers were read from ESP-IDF 5.5's own files (`components/soc/esp32/include/soc/soc_caps.h`, `spi_pins.h`,
`uart_pins.h`, `docs/en/api-reference/peripherals/gpio/esp32.inc`, the bootloader's Kconfig): docs read from the source.
Boot mode notes are from Espressif's esptool docs, "Boot Mode Selection".

Boards here on this chip: [ili9341](../boards/ili9341.md), [e32r28t](../boards/e32r28t.md),
[bruce-cyd](../boards/bruce-cyd.md), [st7796](../boards/st7796.md), [e32r35t](../boards/e32r35t.md),
[ili9486](../boards/ili9486.md), [wrover-n16r8](../boards/wrover-n16r8.md).

## The numbers

- CPU: 2 Xtensa cores at 240 MHz, FPU flag set in soc_caps.h.
- WiFi: 2.4 GHz only.
- Internal RAM: two regions, DRAM 328 KB and IRAM 168 KB.
- GPIO: 0 to 39, with holes at 24 and 28 to 31.
- Input only: 34 to 39, with no pull-ups or pull-downs either.
- Flash and PSRAM pins: 6 to 11, and 16, 17.
- PSRAM: quad when there is one. None on the screen boards here; the WROVER module has it (wrover-n16r8).
- UART0: TX 1, RX 3.
- USB: none of its own. A bridge chip (CH340, CP2102) to UART0 is the only way in.
- BOOT pin (low = download): 0. Must also hold for download: GPIO 2 low or floating.
- Strapping pins: 0, 2, 5, 12, 15.
- Bootloader at 0x1000. Partition table 0x8000, app 0x10000.
- General SPI hosts: SPI2 and SPI3. SPI2 IOMUX pins SCLK 14, MOSI 13, MISO 12, CS 15. SPI3 IOMUX: 18, 23, 19, 5.
- SPI through the GPIO matrix stops at 40 MHz, on IOMUX pins 80.
- LCD peripherals: I80 only.
- I2C controllers: 2. LEDC channels: 8. RMT words a channel: 64.
- DAC: two 8-bit channels, GPIO 25 and 26.
- ESP Web Tools chipFamily: `ESP32`. ESP-IDF 5.5: supported.

## Cores and tasks

- WiFi, the fetch task and the console on core 0, the screen task on core 1 (Flight Radar main.c, measured).
- A second core can draw too, but do not expect 2x. Measured:
  - 3.5" board, the second core drawing every other strip (`DISP_HELPER`): about 1.25x, the radar look 139 -> 98 ms a
    frame. Each core's strip gets slower when both run.
  - 2.8" board, the second core drawing the lower half of each strip (`DISP_HELPER_HALF`, it costs only its 7 KB
    stack): RADAR 9 -> 12 fps, WEATHER 12 -> 17, MAP 16 -> 19, CLASSIC 13 -> 15, at 200 km with 48 planes. Only about
    1.3x: core 0 also carries the WiFi and the fetch (165 KB of planes every few seconds at 200 km).
  - The helper wants pixels, DMA buffers, a stack and 48 KB free before it starts (`helper_set`). On the 3.5" that is
    about 87 KB, so after a square map it often waits.

## Memory

No frame fits: a 320x240 RGB565 frame is 153,600 bytes, a 480x320 one 307,200. Draw in strips
(../../../esp32-lcd-touch/references/bus-spi-strips.md).

Measured free heap:

- 2.8" board, WiFi Radar (promiscuous listening, no TLS), a 2 hour log: 49 KB, 0 lost strips, no reboot.
- 3.5" board, WiFi Radar: 24 KB.
- Flight Radar, WiFi, TLS, map and tiles: low mark 10.5 KB; 6.4 KB when 3.8 KB of static buffers were added (two
  features then shared one buffer and the 4 KB came back).
- Biggest block, WiFi up and little else: about 110 KB (core/gfx.h comment). With everything running: about 24 KB.
- Two hour logs on 0.14, heap at fetch time flat: the 3.5" about 44 KB, the 2.8" about 68 KB.
- The 3.5"'s square map: the map takes 77 KB, about 43 KB stays free and the tile reader takes about 38 KB. The least
  free heap fell to 880 B; with the PNG window in 512 B pieces the least while reading was 1068 B on the 3.5" and
  17.9 KB on the 2.8".
- With the weather look the 2.8" dips to about 25 KB during fetches.
- With a TLS connection held open the 3.5" bottoms out near 5 KB: a live stream that keeps TLS open (20 to 40 KB for
  good) does not fit there.
- FusionEdge's radio on a 2.8" and a 3.5" board with no PSRAM: about 50 KB in one piece while an MP3 stream plays,
  about 20 KB while the radio's web page is served, about 5 KB in one piece after an HTTPS handshake (too little for
  the MP3 decoder, so HTTPS streams do not play there).

What follows:

- The largest block kills you, not the free total. miniz's inflater in ROM wants 32 KB in one piece and the heap had
  24 KB blocks after WiFi: that is why Flight Radar has its own inflater with its window in pieces (core/png_decode.c,
  pieces of 4 KB, then 1 KB, then 512 B as memory got tighter).
- Watch the low mark, `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)`, after a full TLS request.
- IRAM is a separate region here. What the code leaves free there TLS and WiFi cannot use, but data read and written in
  32-bit words can: `heap_caps_malloc(bytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT)`, Flight Radar's `iram_first`.
  Turning `CONFIG_ESP_WIFI_IRAM_OPT` and `CONFIG_ESP_WIFI_RX_IRAM_OPT` off frees about 36 KB more of it (measured).
- With PSRAM (WROVER), the classic chip maps only about 4 MB of it into the heap Arduino uses, and DMA, TLS and system
  locks still need internal RAM (docs: FusionEdge's esp32-wrover-n16r8 README).

The sdkconfig lines that made room (Flight Radar `sdkconfig.defaults`, measured):

```text
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y              # TLS buffers only while used: the peak of a request drops
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y        # certificates let go after the handshake
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=5       # 5 kept for good instead of 10, 1.6 KB each
CONFIG_ESP_WIFI_RX_BA_WIN=5
# CONFIG_ESP_WIFI_IRAM_OPT is not set        # a little slower to receive, IRAM for the map
# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set
```

Deeper memory work is in esp32-runtime.

## Flash

- Read off real boards: ESP32-D0WD-V3 on the desk's 2.8" boards and ESP32-D0WDQ6 rev 1.0 on the Air Quality board,
  4 MB both (measured).
- Flight Radar runs QIO at 80 MHz (`sdkconfig.defaults.esp32`). Every classic board on the desk boots with it. It was
  the cheap big win: code and fonts outside IRAM are read from flash, the 3.5" radar went 124 -> 99 ms a frame
  (measured).
- The older projects (WiFi Radar, Thermo Relay, Air Quality UI) and FusionEdge's classic builds stay on DIO 40 MHz. In
  October 80 MHz flash made a 2.8" board hang silently; the cause was never found, and on 2026-11-01 QIO 80 ran on the
  same board. So QIO 80 is the thing to try for speed, DIO 40 the thing to try for a new board that hangs with no log.
- `flash_args` says `--flash_mode dio` even with QIO set, and that is right: the bootloader switches to quad itself
  (`components/esptool_py/Kconfig.projbuild`). Read `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` in the build's sdkconfig to see
  what you got (built).
- App sizes (built, local builds 2026-11-19): ili9341 1,210,000 bytes, st7796 1,225,216, in a 2 MB slot.
- The bootloader is 27,904 bytes in the 0x7000 = 28,672 between 0x1000 and the partition table: 768 bytes left, so a
  feature that grows the bootloader will not fit there without moving the table (built, 2026-11-04).

## USB: a bridge only

- The port's DTR and RTS are wired to EN and BOOT, so esptool resets the board by itself, and OPENING THE PORT RESETS
  THE BOARD, even with DTR and RTS off (measured): keep one long logger open rather than short peeks.
- CH340 reads fail at 921600 baud: use 460800 (measured).
- USB ids (docs): CH340 `VID_1A86`, CP210x `VID_10C4`. The desk's yellow boards have a CH340, the Air Quality board a
  CP210x (measured).

## Strapping and BOOT

- GPIO 0 low at reset = download mode. It has an internal pull-up.
- GPIO 2 must be low or floating for download. It is the LCD's DC on every yellow board: a panel input, no harm.
- GPIO 12 (MTDI) high at reset sets the flash to 1.8 V, and a 3.3 V flash then fails to boot. Hence
  `PIN_LCD_MISO (-1)  // write only, leave strapping pin 12 alone` on the 2.8" boards. The 3.5" board does use 12 as
  MISO, the touch reads it, and it boots (measured).
- GPIO 15 (MTDO) low silences the ROM's boot messages. GPIO 5 is a strap too.
- `boot:0xNN` in the ROM's second line at 115200 shows what the straps read, `DOWNLOAD_BOOT` means download mode (docs).
- BOOT is a user button once the chip runs (pull-up on, low = pressed). Holding BOOT while pressing RST wakes the
  flasher and the screen stays dark: say so wherever a feature asks for a BOOT press.
- A home-made button that must pull a strapping pin low: the internal pull-up is about 45k, so use a strong
  pull-down, 10k (docs).

## Pins you cannot have

- 6 to 11 and 16, 17 are the module's flash and PSRAM.
- 34 to 39 are inputs with no pulls: fine for XPT2046 MISO (39) and pen IRQ (36, the board pulls it up), useless for
  anything driven. An encoder on them needs outside pull resistors (FusionEdge's WROVER example).
- ADC2 does not work with WiFi on.
- GPIO 36 and 39: Espressif says do not use their interrupts together with ADC or WiFi and sleep modes. The firmware
  polls the pen IRQ with `gpio_get_level`, which is safe.

## SPI

- The yellow boards have the LCD on the SPI2 (HSPI) IOMUX pins. SPI3's IOMUX pins (18, 23, 19, 5) are their SD card.
- 40 MHz is 80 MHz APB / 2; asking for 50 lands there too, the divider ends there (measured).
- A clock asked for is not a clock got, on every chip: the driver takes the nearest it can make at or below it. The log
  line of `disp_init` prints what was asked.
- The bus takes IOMUX only when all its pins are IOMUX pins; one stray pin sends all of it through the matrix. CS is not
  counted in that check (`bus_uses_iomux_pins`, esp_driver_spi).
- An XPT2046 on a shared bus wants 2 MHz and its own CS, added with `spi_bus_add_device` (esp32-lcd-touch).
- What 40 MHz costs, the floor of a frame: 480x320 RGB565 is 61 ms on the wire (measured as part of a frame), 320x240
  31 ms. The 3.5"'s MAP look is SPI-bound: 53 of 82 ms waiting on the bus (measured).
- The ILI9486 behind the Waveshare's shift registers at 20 MHz: 123 ms a frame (measured).

## I2C

ESP-IDF 5.5's `i2c_master` spun forever after a NACK on a classic ESP32 (Air Quality UI, measured), so that project
bit-bangs I2C. Which NACK is not recorded: Flight Radar's `i2c_master_probe` of an empty address does return on the
3.5" board (its GT911 check), so a plain probe is not what spins.

## The DAC as a speaker

FusionEdge plays the radio through GPIO 26 on the yellow boards (measured by ear there):

- The ESP32's I2S-DAC DMA sends each 32-bit word's second 16-bit slot first: adjacent samples swap, heard as a hiss of
  digital noise. Fixed by swapping each pair before it goes out (`Audio::dacWrite`).
- The DAC on the APLL clock crackled all the while a stream played. The default clock measures 44100.1 Hz and is clean;
  APLL is kept only under 19.6 kHz.
- 8 bits leave a coarse staircase on quiet passages: running the DAC at twice the stream's rate with halfway points
  and feeding each sample's rounding error into the next with a little dither made it clean to the ear.

## sdkconfig

Flight Radar's `sdkconfig.defaults.esp32` (copy in ../board-files.md): CPU 240, QIO, 80 MHz flash. With
`sdkconfig.defaults`: 4 MB, `partitions.csv`. The older projects' files differ, see ../board-files.md.

## Traps

- A new classic board hangs silently with no log: try DIO 40 flash.
- Red never lights on a yellow board: LCDWIKI's boards have red on 22, and their GPIO 4 is the amplifier's enable.
- A GPS on TX and RX (1, 3) talks over the flashing: take it off to flash.
- `i2c_master` never returns: bit-banged I2C there.
