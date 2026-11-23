# Project skeleton

Real files from OWLOS Flight Radar (`firmware/`, `core/`, `web/`) unless it says otherwise. Copy them, rename
`RADAR_`, `FLIGHT_` and the project name. How sure each fact is: the tags of SKILL.md.

## The tree

```text
core/                 portable C: an IDF component, and what MSVC (PC preview) and Emscripten (web twin) build
  CMakeLists.txt      idf_component_register(...), -O2 -ffast-math, font sets per screen size
  config.h            RADAR_DISPLAY -> GFX_W, GFX_H, RADAR_ROUND, RADAR_EPAPER, RADAR_HAS_TOUCH, RADAR_NAME
  demo.c, sim.c       made-up data that goes through the real parser or model
firmware/
  CMakeLists.txt      EXTRA_COMPONENT_DIRS ../core, RADAR_DISPLAY, SDKCONFIG_DEFAULTS chains, the version
  main/board.h        pins and parts for each RADAR_DISPLAY: the only place pins live
  main/main.c         tasks, the lock, the fetch loop, the console's lines, app_main
  main/net.c nets.c store.c tiles.c console.c keys.c lcd_*.c epd.c touch.c logbuf.c ...
  components/         vendored panel libraries of a contributor, with their own licences
  sdkconfig.defaults  + .esp32 .esp32c3 .esp32c5 .esp32s3 .esp32p4, and .s3lcd .s3rgb .wave7 .jc8012 per board group
  partitions*.csv     4 MB (most boards), 2 MB (round C3), 16 MB, and two with a coredump partition
  BUILD.BAT           <screen> [port], own build folder and sdkconfig per screen; build.sh for macOS and Linux
web/                  twin.c, BUILD.BAT, emcc.args, index.html: core/ in the browser as WebAssembly
tools/preview/        core/ plus a script of touches: PNGs, frame by frame, and the film
tools/fakeserial.js   a pretend Web Serial port with boards behind it, for testing the site
```

## firmware/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

# core/ = everything that doesnt touch hardware (the sky, the demo, fonts, drawing),
# the PC preview in tools/preview builds the same files
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../core")

# which screen, see core/config.h. BUILD.BAT passes it, 1 (the 320x240 CYD) if nobody does
if(NOT DEFINED RADAR_DISPLAY)
    set(RADAR_DISPLAY 1)
endif()

# a board whose settings differ from the rest on its chip: its own file goes after
# sdkconfig.defaults and sdkconfig.defaults.<chip> and wins over them. read only when a build
# folder's sdkconfig is made
if(RADAR_DISPLAY EQUAL 7 OR RADAR_DISPLAY EQUAL 9)   # the S3 boards with an LCD
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.s3lcd")
elseif(RADAR_DISPLAY EQUAL 10 OR RADAR_DISPLAY EQUAL 11 OR RADAR_DISPLAY EQUAL 13 OR RADAR_DISPLAY EQUAL 14 OR
       RADAR_DISPLAY EQUAL 15 OR RADAR_DISPLAY EQUAL 16 OR RADAR_DISPLAY EQUAL 18)   # and those with an RGB panel
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.s3lcd;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.s3rgb")
    if(RADAR_DISPLAY EQUAL 15 OR RADAR_DISPLAY EQUAL 16)
        list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.wave7")
    endif()
elseif(RADAR_DISPLAY EQUAL 17)   # a P4 board: after sdkconfig.defaults.esp32p4
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.jc8012")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "RADAR_DISPLAY=${RADAR_DISPLAY}" APPEND)

# the version is the first "## " line of CHANGES.md: the site shows that file as the history and
# publishes the same number in version.json, which the board compares its own with
set(CHANGES "${CMAKE_CURRENT_LIST_DIR}/../CHANGES.md")
file(STRINGS "${CHANGES}" FLIGHT_VERSION REGEX "^## [0-9]" LIMIT_COUNT 1)
# "(beta)" on that line: a beta build, it follows the site's betas
set(FLIGHT_BETA 0)
if(FLIGHT_VERSION MATCHES "\\(beta\\)")
    set(FLIGHT_BETA 1)
endif()
string(REGEX REPLACE "^## ([0-9.]+).*" "\\1" FLIGHT_VERSION "${FLIGHT_VERSION}")
if(NOT FLIGHT_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR "no version in ${CHANGES}: its first \"## \" line should be one, like ## 0.3")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHANGES}")   # a new version: configure again
idf_build_set_property(COMPILE_DEFINITIONS "FLIGHT_VERSION=\"${FLIGHT_VERSION}\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "FLIGHT_BETA=${FLIGHT_BETA}" APPEND)
project(owlos-flight-radar)
```

- The repository's file still carries a line `NOTE: flash is 40 MHz ... at 80 MHz the CYD hangs`. It is left out
  here on purpose: it is wrong for this repo, see [Flash speed](#flash-speed).
- Order matters. IDF reads `SDKCONFIG_DEFAULTS` inside `project()` [docs, `tools/cmake/project.cmake`], and this
  file sets it before the include, so `RADAR_DISPLAY` has to be known that early. `idf_build_set_property`
  needs the include done, and comes before `project()`.
- `CMAKE_CONFIGURE_DEPENDS` is what makes a new line in `CHANGES.md` reach the binary without a clean build.
- `FLIGHT_BETA` lets the firmware say BETA on its about page and follow the site's betas: a stable build only
  ever compares itself with the stable number. The release side is in esp32-project-playbook.

## firmware/main/CMakeLists.txt

```cmake
# every board's driver is in here, each file compiles to nothing unless board.h picks it
# the P4 turns its strips with the chip's PPA, a component the other chips do not have
set(TAB5_REQ "")
if(IDF_TARGET STREQUAL "esp32p4")
    set(TAB5_REQ esp_driver_ppa)
endif()
idf_component_register(SRCS "main.c" "net.c" "nets.c" "store.c" "console.c" "button.c" "keys.c" "touch.c"
                            "lcd_spi.c" "lcd_rgb.c" "lcd_dsi.c" "lcd_ili9486.c" "epd.c" "i2c_bus.c" "tiles.c" "led.c"
                            "lcd_wave7.c" "lcd_wave7_panel.cpp" "ovl.c" "lcd_jc8012.c" "lcd_jc8012_panel.cpp" "logbuf.c"
                       INCLUDE_DIRS "."
                       REQUIRES core esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_rmt
                                esp_driver_uart esp_wifi esp_event esp_netif nvs_flash esp_timer
                                esp_http_client mbedtls esp_partition chipguy_lcd7_7b chipguy_jc8012 ${TAB5_REQ})
```

Each driver file opens with `#include "display.h"` then `#if DISP_LCD_SPI` (or its own switch) and closes with
`#endif`. No lists of sources per board in CMake. A component a single chip needs goes in by `IDF_TARGET`, as the
PPA does here.

## core/CMakeLists.txt

```cmake
# portable core as an IDF component. tools/preview builds the same files on PC with MSVC
# the 5" boards and the 7" (config.h GFX_SCALED) have the same fonts half as big again, and one more
if(RADAR_DISPLAY EQUAL 10 OR RADAR_DISPLAY EQUAL 11 OR RADAR_DISPLAY EQUAL 12 OR RADAR_DISPLAY EQUAL 14 OR
   RADAR_DISPLAY EQUAL 15 OR RADAR_DISPLAY EQUAL 16 OR RADAR_DISPLAY EQUAL 17 OR RADAR_DISPLAY EQUAL 18)
    set(FONTS "font_glass_small.c" "font_glass_medium.c" "font_glass_large.c" "font_glass_huge.c" "font_glass_tiny.c")
else()
    set(FONTS "font_small.c" "font_medium.c" "font_large.c" "font_huge.c")
endif()
# the e-paper boards (5: 4.2", 6: 5.79") draw their own main screen (ui_paper.c) in bold letters
if(RADAR_DISPLAY EQUAL 5 OR RADAR_DISPLAY EQUAL 6)
    list(APPEND FONTS "font_paper_s.c" "font_paper_m.c" "font_paper_l.c" "font_paper_xl.c" "font_paper_xxl.c")
endif()
idf_component_register(SRCS "sky.c" "demo.c" "gfx.c" "font.c" "ui.c" "ui_scope.c" "ui_pages.c" "ui_wifi.c" "paper.c" "ui_paper.c"
                            "ui_about.c" "ui_weather.c" "wx_parse.c" "wx_alert.c" "qr_site.c" "map.c" "png_decode.c" "watch.c" "light.c" "sun.c"
                            ${FONTS}
                       INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math)
```

`-O2 -ffast-math` on core only: it is all float maths and pixels, the rest keeps the project's level. Fonts are
picked per screen size, because fonts are the second biggest item in flash (see [Flash budget](#flash-budget)).

What keeps `core/` portable:

- No `esp_*` and no FreeRTOS includes. The exception is the guarded block in `gfx.h`:

```c
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#if RADAR_OVERLAY
#define CORE_HOT                       /* the 7" boards: drawing code stays out of internal RAM */
#define CORE_DATA
#else
#define CORE_HOT  IRAM_ATTR            /* hot loops in IRAM: every flash cache miss costs microseconds */
#define CORE_DATA DRAM_ATTR
#endif
/* CORE_BIG: big state no interrupt touches, in PSRAM where sdkconfig lets .bss go there */
#else
#define CORE_HOT
#define CORE_DATA
#endif
```

- Time comes in as a `uint32_t now` argument, so a test can jump it.
- Whatever needs the platform is a hook the firmware sets, NULL meaning "not there":
  `uint32_t (*ui_clock_us)(void)` (the layer timer), `void *(*map_mem)(size_t)` (IRAM or PSRAM first),
  `void *map_scratch` (a buffer the firmware lends while a tile is read), and in WiFi Radar
  `void (*map_yield)(void)` (give the core back).
- The screen does not call the firmware. It fills a `ui_cmd_t` (cfg changed, scan, refetch, home, watch) and
  the firmware acts on it after `ui_touch`, `ui_button` or `ui_key`.
- Chip differences are compile switches that the PC can also set, like `GFX_SOFT_FLOAT`: a C3 has no FPU, so
  the busiest loops take a whole-number path there, and the preview can draw that path too.
- The browser build adds one more switch, `RADAR_WEB=1`, which leaves out what makes no sense in a page (the
  on-screen WiFi keyboard page).

## core/config.h: which screen

```c
#define DISPLAY_ILI9341_320x240    1   // 2.8" CYD, every revision
#define DISPLAY_ST7796_480x320     2   // 3.5" on a classic ESP32
#define DISPLAY_ILI9486_480x320    3
#define DISPLAY_GC9A01_240x240     4   // round, ESP32-C3
#define DISPLAY_EPD_400x300        5   // e-paper 4.2", ESP32-S3
#define DISPLAY_EPD_792x272        6   // wide e-paper 5.79", ESP32-S3
#define DISPLAY_ES3C28P_320x240    7
#define DISPLAY_NMCYDC5_320x240    8
#define DISPLAY_CROWADV28_320x240  9
#define DISPLAY_CROWADV50_800x480 10
#define DISPLAY_JC8048W550_800x480 11
#define DISPLAY_TAB5_1280x720     12   // ESP32-P4, MIPI-DSI
#define DISPLAY_4827S043_480x272  13
#define DISPLAY_CROW70_800x480    14
#define DISPLAY_WAVE7_800x480     15
#define DISPLAY_WAVE7B_1024x600   16
#define DISPLAY_JC8012_1280x800   17   // ESP32-P4, MIPI-DSI
#define DISPLAY_CROW50_800x480    18
#define DISPLAY_ZLX2_320x240      19   // ESP32-C5

#ifndef RADAR_DISPLAY
#define RADAR_DISPLAY DISPLAY_ILI9341_320x240
#endif

#if RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || ...
#define GFX_W 320
#define GFX_H 240
#elif RADAR_DISPLAY == DISPLAY_GC9A01_240x240
#define GFX_W 240
#define GFX_H 240
#define RADAR_ROUND 1
#elif RADAR_DISPLAY == DISPLAY_EPD_400x300
#define GFX_W 400
#define GFX_H 300
#define RADAR_EPAPER 1               // slow refresh, 4 greys, no animation
#elif ...                            // the 800x480 boards: the 3.5"'s layout, GFX_W 534, GFX_H 320, GFX_PW 800, GFX_PH 480
#else
#error "unknown RADAR_DISPLAY, see config.h"
#endif

#define RADAR_TOUCH_CAL (RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || ...)
#define RADAR_HAS_TOUCH RADAR_TOUCH_CAL
#define RADAR_HAS_LED   (...)
```

`config.h` is in `core/` because the renderer needs the size. It holds what the screens must know, no pins. A
number once given to a board is never reused: BUILD.BAT, CI and the site's manifests all know the board by it.
When a contributor's fork already used a number, the fork's numbers were kept so its later changes come over
clean, and the next board took the next free one.

## firmware/main/board.h: pins, one block per screen

```c
#if RADAR_DISPLAY == DISPLAY_ILI9341_320x240
/* ESP32-2432S028R "cheap yellow display". the one this was written on, all of it works */
#define DISP_LCD_SPI   1
#define LCD_CHIP_ILI9341 1
#define PIN_LCD_SCLK  14
...
#define LCD_SPI_HZ    (40 * 1000 * 1000)
#define DISP_STRIP    24        // rows a strip: 240 / 24 = 10 strips
#define DISP_HELPER   1
#define DISP_HELPER_HALF 1      // the second core draws the lower half of every strip
```

Every block says in a comment what was checked on hardware and what was not, and where the pins came from.
`DISP_STRIP` and the second-core switches are set here because they are a heap decision per board, see
[memory.md](memory.md). Other per-board switches met in board.h: `PIN_AMP_EN` (a speaker amplifier held off in
`app_main` before anything else), `NO_CHIP_USB` (a board whose USB pins are used for something else, so the
console does not read the chip's own USB), `PIN_KEY_*` (the e-paper boards' keys). The pins themselves:
[esp32-boards](../../esp32-boards/SKILL.md).

## The builds

`BUILD.BAT <screen>` maps a build name to a number and a chip. On 2026-11-23 [from BUILD.BAT]:

- `ili9341` 1, esp32 (the default; it keeps the plain `build` folder and `firmware/sdkconfig`)
- `st7796` 2, esp32
- `ili9486` 3, esp32
- `gc9a01` 4, esp32c3
- `epd400` 5, esp32s3
- `epd792` 6, esp32s3
- `es3c28p` 7, esp32s3
- `nmcydc5` 8, esp32c5
- `crowadv28` 9, esp32s3
- `crowadv50` 10, esp32s3
- `jc8048w550` 11, esp32s3
- `tab5` 12, esp32p4
- `esp4827s043` 13, esp32s3
- `crow70` 14, esp32s3
- `wave7` 15, esp32s3
- `wave7b` 16, esp32s3
- `jc8012` 17, esp32p4
- `crow50` 18, esp32s3
- `zlx2` 19, esp32c5

`firmware/build.sh <screen> [port]` does the same on macOS and Linux for `wave7`, `wave7b`, `jc8012`, `crow70` and
`crow50`: a contributor works there.

## Version from CHANGES.md, four readers

- Firmware: CMake above gives `FLIGHT_VERSION` as a string define and `FLIGHT_BETA` as 0 or 1.
- Preview (`tools/preview/BUILD.BAT`):
  `for /f "tokens=2" %%v in ('findstr /b /c:"## " ..\..\CHANGES.md') do if not defined VER set VER=%%v`
  then `cl ... /DFLIGHT_VERSION=\"%VER%\"`.
- Web twin (`web/BUILD.BAT`): the same `findstr` line, then `emcc ... -DFLIGHT_VERSION=\"%VER%\"`.
- CI (`.github/workflows/pages.yml`):
  `VER=$(sed -n 's/^## \([0-9][0-9.]*\).*/\1/p' CHANGES.md | head -n 1)`, then
  `printf '{"version":"%s"}\n' "$VER" > public/version.json` (today with a `beta` and a `stats` field too).

Each one stops with an error when the line is missing: an unnumbered build is worse than no build.

## sdkconfig.defaults, what each line is for

```text
CONFIG_COMPILER_OPTIMIZATION_PERF=y        # -O2 for the whole project
CONFIG_FREERTOS_HZ=1000                    # vTaskDelay(1) is 1 ms. at the default 100 it is 10 ms a frame
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10           # a stuck loop reboots with a core dump
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y           # the chip files below may override this
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y
CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
# the servers are HTTPS: the common root certificates, enough for the ADS-B networks and FR24
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y
# a server that sends its root signed by an older root not in the bundle: ESP-IDF 6 only.
# a 5.5 build warns that it does not know this line and skips it
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y
# TLS buffers only while they are used, the certificates let go after the handshake: the peak of
# a request drops, and the map's picture fits beside it on the 3.5" board
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y
# WiFi: 5 receive buffers kept for good instead of 10 (1.6 KB each). a few KB of JSON and map
# tiles need no more, more come and go as they are needed
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=5
CONFIG_ESP_WIFI_RX_BA_WIN=5
# the WiFi's code stays in flash, not in IRAM: a little slower to receive, and the IRAM it leaves
# holds most of the map's picture (see iram_first in main.c)
# CONFIG_ESP_WIFI_IRAM_OPT is not set
# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set
```

A firmware with no network (Thermo Relay, Air Quality) has the top half and adds:

```text
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
# a brown-out (weak USB supply, the relay coil pulling) resets cleanly, the relay starts OFF
CONFIG_ESP_BROWNOUT_DET=y
```

IDF's Kconfig recommends static RX buffers at or above the RX BA window, so the two are set together [docs].

## The chip and board files

How they stack: `sdkconfig.defaults`, then `sdkconfig.defaults.<chip>` (IDF picks it by target), then what
`SDKCONFIG_DEFAULTS` adds in its order, the later file wins. **All of this is read only when `sdkconfig` is
created.** Change a default and nothing happens until `build*/sdkconfig` is deleted. Each build folder has its
own: BUILD.BAT passes `-DSDKCONFIG=<dir>\build_<disp>\sdkconfig`. The CI starts clean every time, so a laptop and
the CI can differ exactly here.

```text
# sdkconfig.defaults.esp32: classic ESP32 boards: full speed
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
# the flash at 80 MHz on four lines (how Arduino flashes these boards too): the code and the fonts
# that do not fit in IRAM are read from it. the 3.5" radar went 124 -> 99 ms a frame with it
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y

# sdkconfig.defaults.esp32c3: round CrowPanel 1.28": one core, 160 MHz tops, 2 MB layout
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_2mb.csv"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y

# sdkconfig.defaults.esp32s3: the e-paper boards (8 MB of flash, read on the 4.2"). the 4 MB layout:
# the app is 1.2 MB and more already, the 2 MB layout's 1.3 MB was too tight
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
# PSRAM: the e-paper keeps two 54 KB pictures there. the 5.79" board is an ESP32-S3R8 (8 MB octal).
# a board without it just runs without it
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_IGNORE_NOTFOUND=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y

# sdkconfig.defaults.s3lcd: read after the S3 file, for the S3 boards with an LCD (N16R8). the CYD's
# 4 MB layout runs on them as it is. the octal PSRAM stays on
# the log goes to two ports here, UART0 and the S3's own USB: deeper than on the ESP32, and on the
# event task that went past the stack's end
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y

# sdkconfig.defaults.s3rgb: after s3lcd, the S3 boards with an RGB panel. the screen is read out of
# the PSRAM all the time, so the PSRAM runs at its full speed, and the code lies there too: it
# leaves the flash alone while a frame goes out and keeps the cache on while tiles and settings are written
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_LCD_RGB_ISR_IRAM_SAFE=y
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y          # a frame that cannot keep up finds its place at the next VSYNC
# TLS and the WiFi's buffers out of the RAM: the strips and the bounce buffers want it
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set  # the WiFi asks for PSRAM only when PSRAM is not optional

# sdkconfig.defaults.esp32c5: NM-CYD-C5 and ZLX-ESP32-2: one RISC-V core at 240 MHz, 2.4 and 5 GHz WiFi
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
# the PSRAM asked for by name; the first board that ran this had about 35 KB of its own RAM left for
# the strips and stopped, so TLS, the WiFi's buffers and the map's picture go out
CONFIG_SPIRAM=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set  # a C5 without PSRAM does not start on this build
# that way the WiFi keeps its send buffers for good, 1.6 KB each and 16 unless told
CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=6
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096   # the log goes to two ports here too
# no SPIRAM_MODE line: the C5's PSRAM is quad

# sdkconfig.defaults.esp32p4: the M5Stack Tab5. 16 MB flash, 32 MB PSRAM
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y       # its P4 is before production silicon (v1.3)
CONFIG_IDF_EXPERIMENTAL_FEATURES=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_16mb.csv"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
# the console on the chip's USB: the board has no UART0 to speak of, and the UART driver waiting for
# it held core 0 till the watchdog bit
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
# WiFi: an ESP32-C6 beside it, over SDIO with ESP-Hosted
CONFIG_ESP32P4_TAB5_C6_BOARD=y
```

The two board files a contributor added for his 7" S3 and 10.1" P4 boards (`sdkconfig.defaults.wave7`,
`.jc8012`) carry what those boards need on top, all for the internal RAM [docs, the files' own comments]:

- `CONFIG_SPIRAM_USE_MALLOC=y` with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024`: plain malloc of 1 KB and more may
  go to PSRAM, and what must be internal asks for it by name.
- `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`: big statics marked `CORE_BIG` go to PSRAM, about 65 KB of
  internal RAM back on the 7".
- `CONFIG_MBEDTLS_HARDWARE_AES=n` on the 7": the hardware AES copies PSRAM data through a DMA buffer from
  internal RAM, which was short: `esp-aes: Failed to allocate memory`. See [memory.md](memory.md).
- A `coredump` partition and `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`: these boards often run with nobody on
  the serial port. See [crashes.md](crashes.md).
- `# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is not set`: the watchdog watches the ui task instead. See
  [tasks-and-watchdog.md](tasks-and-watchdog.md).
- `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y`, `CONFIG_FREERTOS_USE_TRACE_FACILITY=y`: each task's share of each
  core and its unused stack, for the log.
- On the P4: `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` (that board's CH340 is the port to use),
  `CONFIG_CACHE_L2_CACHE_LINE_128B=y`, `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`, `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y`.

## Flash speed

What the repos really do [computed from sdkconfig, the chip files and `flash_args`; first read on 2026-11-04,
read again on 2026-11-23]:

- WiFi Radar: DIO 40 MHz, set in `sdkconfig.defaults`.
- Thermo Relay: DIO 40 MHz, set in `sdkconfig.defaults`.
- Air Quality UI: DIO 40 MHz, set in `sdkconfig.defaults`.
- Flight Radar, classic ESP32, C3, C5, S3 with an LCD or RGB panel: QIO 80 MHz (`flash_args` says
  `--flash_freq 80m`). The chip and board files override the 40M line.
- Flight Radar, P4: 80 MHz (`FLASHFREQ_80M` in the P4 file, the mode left to IDF).
- Flight Radar, S3 e-paper: DIO 40 MHz. The S3 file has no flash mode lines, and the 2026-11-23 build's sdkconfig
  says DIO and 40M.

The story, in order:

1. WiFi Radar, 2026-10: "80 MHz flash made the CYD hang randomly, so it runs at 40 MHz" [measured]. A note went
   into `firmware/CMakeLists.txt`, and every repo copied it with the rest of the skeleton.
2. Same days, same board: the screen also hung for ever on a strip sent over a strip in flight. Which of the
   two fixes came first is not recorded, so it is not known whether the hang blamed on 80 MHz was the
   same bug. The cause of the 80 MHz hang was never found.
3. Flight Radar, 2026-11-01: QIO 80 tried for speed. Code and fonts outside IRAM are read from flash, so the
   frame depends on it. The 3.5" radar went 124 -> 99 ms a frame, all three boards booted [measured]. The releases
   since are built so. The old note stayed in the CMake file, and is wrong for this repo.
4. Since then the bench boards (2.8" CYD, 3.5", round C3) ran two-hour logs and a 0.16.2 soak with no restart
   at QIO 80 [measured]. That is hours, not days.

So: 40 MHz is the known-safe start, 80 MHz QIO is known to run on the 2.8", the 3.5" and the round C3 in Flight
Radar for hours. Do not copy the note, do not copy the opposite claim either. For a new board: bring it up at
DIO 40, and when a frame is limited by code in flash, set QIO 80, reboot it a few times, then leave a long log
running.

`flash_args` says `--flash_mode dio` even for a QIO build. That is normal: IDF writes DIO in the image header
and the bootloader switches the chip to QIO itself (`default "dio" if ESPTOOLPY_FLASHMODE_QIO`) [docs].

## Partitions

```text
# partitions.csv (4 MB): most boards
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x200000
# the map's tiles as they came from OpenStreetMap (main/tiles.c)
tiles,    data, 0x40,    0x210000, 0x1F0000

# partitions_2mb.csv (the round C3)
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x150000
tiles,    data, 0x40,    0x160000, 0xA0000

# partitions_16mb.csv (the Tab5): the app has room to grow, and the tiles 6 MB
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x400000
tiles,    data, 0x40,    0x410000, 0x600000

# partitions_wave7.csv (8 MB and up): the 4 MB layout plus a core dump on flash
factory,  app,  factory,  0x10000,  0x200000
tiles,    data, 0x40,     0x210000, 0x1F0000
coredump, data, coredump, 0x400000, 0x10000

# partitions_jc8012.csv (16 MB): the 16 MB layout plus a core dump on flash
factory,  app,  factory,  0x10000,  0x400000
tiles,    data, 0x40,     0x410000, 0x600000
coredump, data, coredump, 0xA10000, 0x10000
```

- A **raw data partition** has a custom subtype (0x40 here) and is found by name:
  `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "tiles")`. No filesystem:
  a ring needs none, and it wears evenly by construction.
- **`factory` only, no OTA.** Updates go through the browser flasher. Two app slots would halve what is left, and
  on the 2 MB layout they would not fit at all (2 x 1.32 MB). OTA with signing and rollback has been talked about
  for later: it needs two app slots, so the tiles partition would shrink [reasoned].
- The other firmwares, for scale:

```text
# WiFi Radar, 4 MB: factory 0x180000, radar 0x80000 at 0x190000. 2 MB: factory 0x170000, radar 0x80000 at 0x180000
# Thermo Relay:     factory 0x180000, thermo 0x40000 at 0x190000
# Air Quality UI:   factory 0x200000, aqhist 0x80000 at 0x210000
```

Two firmwares can share an offset on a board flashed without erase, so a data partition's first bytes may be
the last project's. That is why each ring has its own magic: `SLOT`, `THRM`, `AQH1`, `OSMT`.

## Flash budget

**2026-11-21 to 2026-11-23, Flight Radar 0.16.2 beta and the work after it** [computed from the app bins in the
build folders, each against its app partition]:

- `ili9341` (2.8" CYD): 1 225 520 of 2 097 152, 58%.
- `st7796` (3.5"): 1 242 528, 59%.
- `ili9486`: 1 194 704, 57%.
- `gc9a01` (round C3): **1 319 968 of 1 376 256, 96%: 56 288 bytes, about 4%, left** [measured from the build of
  2026-11-23]. The tightest thing in the project, and it shrinks with every feature: 88% on 2026-11-04, 95% on
  2026-11-19 (1 308 336 bytes).
- `es3c28p`: 1 249 024, 60%. `crowadv28`: 1 232 816, 59%.
- `nmcydc5` (C5): 1 440 160, 69%. `zlx2` (C5): 1 430 112, 68%.
- `crowadv50`: 1 257 408, 60%. `jc8048w550`: 1 264 080, 60%. `esp4827s043`: 1 225 088, 58%. `crow70`: 1 261 808, 60%.
  `crow50`: 1 261 680, 60%.
- `wave7`: 1 338 240, 64%. `wave7b`: 1 341 168, 64%.
- `epd400`: 1 443 696, 69% (the paper fonts). `epd792`: 1 276 944, 61%.
- `tab5`, `jc8012`: 4 MB app partitions, not built here (the P4 builds fail locally at configure, CI builds them).

**2026-11-04, for comparison** [computed]:

- Flight Radar 2.8" ESP32: 1 115 008 of 2 097 152, 53%.
- Flight Radar 3.5" ESP32: 1 130 912, 54%.
- Flight Radar RPi LCD ESP32: 1 097 824, 52%.
- Flight Radar ES3C28P S3: 1 151 136, 55%.
- Flight Radar CrowPanel Advance S3: 1 135 024, 54%.
- Flight Radar NM-CYD-C5: 1 318 368, 63%.
- Flight Radar round C3: 1 213 392 of 1 376 256, 88%.
- WiFi Radar 2.8": 819 264 of 1 572 864, 52%.
- WiFi Radar 3.5": 834 224 of 1 572 864, 53%.
- WiFi Radar round C3: 879 136 of 1 507 328, 58%.
- WiFi Radar e-paper 5.79": 997 040 of 1 507 328, 66%.
- Thermo Relay: 379 680 of 1 572 864, 24%.
- Air Quality UI: 390 560 of 2 097 152, 19%.

Reading it: a screen firmware with no WiFi is about 0.38 MB, WiFi alone takes it to 0.8 MB, and HTTPS with the
certificate bundle to 1.1 MB. Those are different programs, so take that as a rough size of each step, not a
measurement of it. Fonts are the other big item: the e-paper builds carry five more of them, about 170 KB more
than the LCD builds. RISC-V code is bigger: the C3's `.flash.text` was 0.90 MB against the ESP32's 0.80 MB in
Flight Radar, and the C5 builds are the largest LCD builds today. A weather screen experiment on the 3.5" cost
190 KB (1.24 to 1.42 MB) and was taken out again [measured from the builds].

The C3 has 2 MB of flash on paper (the one here has 4 MB). When it fills, the cheap room is the tiles partition:
640 KB there, and the app partition can grow into it. A rebuild of an untouched board should keep its size: a
change in size is a question.

## BUILD.BAT, the parts that matter

```bat
set BDIR=build
set SDK=%~dp0sdkconfig
if not "%NUM%"=="1" (
  set BDIR=build_%DISP%
  set SDK=%~dp0build_%DISP%\sdkconfig
)
if "%IDF_PATH%"=="" set "IDF_PATH=%USERPROFILE%\esp\v5.5\esp-idf"
if "%IDF_TOOLS_PATH%"=="" set "IDF_TOOLS_PATH=%USERPROFILE%\.espressif"
rem IDF venv was made with IDF's own python, not the system one, so put that first in PATH
for /d %%P in ("%IDF_TOOLS_PATH%\tools\idf-python\*") do set "PATH=%%P;%PATH%"
call "%IDF_PATH%\export.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
rem a build folder made where the project used to be (it was moved or copied) keeps the
rem old path in its CMake cache and CMake refuses it. such a folder is started fresh
set "HERE=%~dp0"
set "HERE=%HERE:\=/%"
if exist "%BDIR%\CMakeCache.txt" (
  findstr /i /c:"CMAKE_HOME_DIRECTORY:INTERNAL=%HERE:~0,-1%" "%BDIR%\CMakeCache.txt" >nul
  if errorlevel 1 (
    echo %BDIR% was made in another folder, starting it fresh
    rmdir /s /q "%BDIR%"
  )
)
set ARGS=-B %BDIR% "-DSDKCONFIG=%SDK%" -DIDF_TARGET=%TARGET% -DRADAR_DISPLAY=%NUM%
rem ESP-IDF 5.5.0 still calls the C5 a preview target, later 5.5 releases (the site's build) do not
set PREVIEW=
if /i "%TARGET%"=="esp32c5" set PREVIEW=--preview
if "%PORT%"=="" (
  idf.py %PREVIEW% %ARGS% build
) else (
  idf.py %PREVIEW% %ARGS% -p %PORT% build flash monitor
)
```

Run it from PowerShell as `cmd /c`: from Git Bash `cmd //c` did not find `idf.py` [measured]. The CI's line,
in Espressif's image, is the same build:

```sh
docker run --rm -v "$WS:/project" -w /project/firmware --user root espressif/idf:release-v5.5 \
  bash -c "git config --global --add safe.directory '*' && \
           idf.py --no-ccache -B build_$DISP -DSDKCONFIG=/project/firmware/build_$DISP/sdkconfig \
                  -DIDF_TARGET=$TARGET -DRADAR_DISPLAY=$NUM build"
```

- The local IDF is the v5.5 tag, the CI's image is `release-v5.5`, a moving branch: two builds of one commit are
  not the same bytes, which is why a local ELF does not fit a board flashed from the site. See
  [crashes.md](crashes.md).
- With Docker not running here, the local v5.5 stood in for the CI image in a release rehearsal: three fresh
  builds with the workflow's `idf.py` line came out byte for byte the size of the dev builds [measured].
- The P4 builds (`tab5`, `jc8012`) stop at configure on the build PC with the component manager's
  `MissingKconfigError`; CI builds them [measured].

## web/: the same core for WebAssembly

```bat
rem web\BUILD.BAT: Emscripten from %EMSDK%, its own python and settings, not whatever else is on this PC
set "PATH=%EMSDK%\upstream\emscripten;%PATH%"
set "EM_CONFIG=%EMSDK%\.emscripten"
for /d %%P in ("%EMSDK%\python\*") do set "EMSDK_PYTHON=%%P\python.exe"
rem one radar for each size of screen, each with its own letters
call :one 10 Twin  twin  "%GLASS%"       & rem 800x480
call :one 1  Twin1 twin1 "%FONTS%"       & rem 320x240
call :one 2  Twin2 twin2 "%FONTS%"       & rem 480x320
call :one 4  Twin4 twin4 "%FONTS%"       & rem the round one
call :one 13 Twin5 twin5 "%FONTS%"       & rem 480x272
:one
call emcc @emcc.args %~4 -DFLIGHT_VERSION=\"%VER%\" -DRADAR_DISPLAY=%1 -sEXPORT_NAME=%2 -o %3.js
```

`web/emcc.args`, read by the local script and by the CI job alike, so the two cannot drift:

```text
-O2
-I../core
-DRADAR_WEB=1
twin.c
../core/sky.c ../core/demo.c ../core/gfx.c ../core/font.c ../core/ui.c ../core/ui_scope.c ../core/ui_pages.c
../core/ui_wifi.c ../core/ui_about.c ../core/ui_weather.c ../core/wx_parse.c ../core/wx_alert.c ../core/sun.c
../core/qr_site.c ../core/map.c ../core/png_decode.c ../core/watch.c ../core/light.c
-sMODULARIZE=1
-sENVIRONMENT=web
-sALLOW_MEMORY_GROWTH=1
-sINITIAL_MEMORY=16777216
-sSTACK_SIZE=262144
-sEXPORTED_FUNCTIONS=_malloc,_free
-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString,stringToNewUTF8
```

(In the repository each source is on its own line.) Every file `core/` gains must be added in three places:
`core/CMakeLists.txt`, `tools/preview/BUILD.BAT` and `web/emcc.args`. A new core file missing from `emcc.args`
is a twin that does not link. The CI job uses the `emscripten/emsdk` image of the same version as the local one
(6.0.13 on both) [measured]. Running `emsdk_env.bat` from a batch file left no `emcc` on PATH; setting `EM_CONFIG`
and `EMSDK_PYTHON` and calling `emcc` from its folder works [measured]. What the twin does:
[testing.md](testing.md#the-webassembly-twin).
