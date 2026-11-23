# The files that describe a board, shared by every board

Copies of OWLOS Flight Radar's shared files as they stood on 2026-11-23 (19 builds), the working example for a new
repository: take what the project needs and rename RADAR_ to the project's word. Each board's own board.h block and its
sdkconfig lines are in its file under boards/. What each board really is, and how sure, is in those files: comments in
these copies can be older than the facts. Known stale lines:

- board.h says "NOT run on hardware yet" for the ILI9486. Its panel runs under Air Quality UI ([ili9486](boards/ili9486.md)).
- board.h calls the 3.5" board ESP32-3248S035 and the 2.8" "the one this was written on". The 3.5" measured is an LCDWIKI
  E32R35T, and the desk's 2.8" boards are LCDWIKI E32R28T.
- board.h's Waveshare block says "the -7 not yet"; the -7 ran in the contributor's fork (boards/wave7.md).
- firmware/CMakeLists.txt still has a line "NOTE: flash is 40 MHz in sdkconfig.defaults, at 80 MHz the CYD hangs". It is left
  out of the copy below: `.esp32`, `.esp32c3`, `.esp32c5` and `.s3lcd` set QIO 80 over it, and that runs.
- BUILD.BAT's example port is written `<port>` here. Find the real one first, SKILL.md.

## firmware/main/board.h: the head

```c
/*
 * pins and parts of every board, picked by RADAR_DISPLAY (core/config.h).
 * wiring is the same as in ViewOwl's firmware for that display, change pins only here.
 * what is checked on hardware and what is not, is written next to each board.
 */
#pragma once
#include "config.h"

/* how the picture gets out:
 *   DISP_LCD_SPI   esp_lcd SPI panel IO, 8 bit commands (ILI9341, ST7796, GC9A01)
 *   DISP_LCD_RGB   a frame in the PSRAM on the S3's 16-bit RGB bus (the 5" boards, lcd_rgb.c)
 *   DISP_ILI9486   raw SPI, every byte padded to 16 bits (RPi LCD with shift registers)
 *   DISP_EPD       SSD1683 e-paper, bit-banged, 1 bit a pixel (core/paper.c)
 *   DISP_LCD_DSI   a frame in the PSRAM on the P4's MIPI-DSI, the panel standing (the Tab5, lcd_dsi.c)
 *   DISP_WAVE7     the Waveshare 7/7B through chipguyhere's library, the moving layer over it (lcd_wave7.c)
 *   DISP_JC8012    the Guition JC8012P4A1C through chipguyhere's library, the panel standing, the moving
 *                  layer over it by columns (lcd_jc8012.c)
 * touch: TOUCH_XPT_BITBANG, TOUCH_XPT_SPI (on the LCD bus), TOUCH_CST816, TOUCH_FT6336, TOUCH_GT911,
 * TOUCH_BSP (the board package's, the Tab5), TOUCH_JC8012 (the GSL3680 through chipguyhere's library),
 * or none */

```

Then one `#if RADAR_DISPLAY == ...` block a board (in the board files), then the tail:

```c
#endif

#ifndef PIN_LCD_BL
#define PIN_LCD_BL (-1)
#endif

/* the RGB LED on the back of the 2.8" and the 3.5" CYD, each colour pulled to ground = lit. the same
 * pins on both (rzeldent/platformio-espressif32-sunton); on the 2.8" the Thermo Relay drives them.
 * red is GPIO 4 there and GPIO 22 on LCDWIKI's ESP32-32E boards (lcdwiki.com, E32R28T, E32R35T),
 * whose GPIO 4 switches the speaker's amplifier: the user picks, core/light.h LIGHT_RED_.
 * the 3.5" here is one of those (2026-11-03: red lit on 22 alone). the boards with a WS2812 name
 * its one pin above, PIN_LED_WS2812 */
#if RADAR_HAS_LED && !defined(PIN_LED_WS2812)
#define PIN_LED_G     16
#define PIN_LED_B     17
#endif
#ifndef DISP_HELPER
#define DISP_HELPER 0
#endif
#ifndef DISP_HELPER_HALF
#define DISP_HELPER_HALF 0      // 0: the second core's strips are its own, under this core's (main.c)
#endif

// raw touch units across the screen, for the sanity check of a stored alignment
#if TOUCH_CST816
#define TOUCH_RAW_SPAN 240
#elif TOUCH_FT6336
#define TOUCH_RAW_SPAN 320      // it counts screen pixels, 240x320 standing
#elif TOUCH_GT911
#define TOUCH_RAW_SPAN GFX_PW   // so does this one, lying down
#elif TOUCH_BSP || TOUCH_JC8012
#define TOUCH_RAW_SPAN GFX_PW   // the Tab5's counts the standing glass's pixels, 720 x 1280: the long way is the picture's width
#else
#define TOUCH_RAW_SPAN 3700
#endif
```

## core/config.h

```c
/* config.h - which display this build is for (the same set of screens as OWLOS WiFi Radar).
 * pick one here, or pass it from the build: BUILD.BAT <display> does -DRADAR_DISPLAY=...
 * pins and drivers for each one are in firmware/main/board.h */
#pragma once

#define DISPLAY_ILI9341_320x240  1   // ESP32-2432S028R "cheap yellow display", every revision (v1 v2 v3: board.h)
#define DISPLAY_ST7796_480x320   2   // 3.5" ST7796 on classic ESP32 (ViewOwl 480x320)
#define DISPLAY_ILI9486_480x320  3   // Waveshare RPi LCD (A) 3.5" on OWLOSAirQuality board
#define DISPLAY_GC9A01_240x240   4   // round 1.28", ESP32-C3
#define DISPLAY_EPD_400x300      5   // e-paper 4.2" SSD1683, ESP32-S3
#define DISPLAY_EPD_792x272      6   // wide e-paper 5.79", two SSD1683, ESP32-S3
#define DISPLAY_ES3C28P_320x240  7   // LCDWIKI ES3C28P 2.8": ESP32-S3, ILI9341V, FT6336 touch
#define DISPLAY_NMCYDC5_320x240  8   // NM-CYD-C5 2.8": ESP32-C5, ST7789, XPT2046 touch
#define DISPLAY_CROWADV28_320x240 9  // Elecrow CrowPanel Advance 2.8": ESP32-S3, ST7789 IPS, FT6336 touch
#define DISPLAY_CROWADV50_800x480 10 // Elecrow CrowPanel Advance 5.0": ESP32-S3, RGB bus, GT911 touch
#define DISPLAY_JC8048W550_800x480 11 // Guition JC8048W550 5": ESP32-S3, RGB bus, GT911 touch
#define DISPLAY_TAB5_1280x720   12   // M5Stack Tab5 5": ESP32-P4, 720x1280 standing on MIPI-DSI, its touch, through Espressif's board package
#define DISPLAY_4827S043_480x272 13  // ESP32-4827S043C 4.3": ESP32-S3, 480x272 on the RGB bus, GT911 touch
#define DISPLAY_CROW70_800x480  14   // Elecrow CrowPanel 7.0" (the plain one, not the Advance): ESP32-S3, RGB bus, GT911 touch
#define DISPLAY_WAVE7_800x480   15   // Waveshare ESP32-S3-Touch-LCD-7: RGB bus, GT911, chipguy's 7/7B driver (chipguyhere)
#define DISPLAY_WAVE7B_1024x600 16   // Waveshare ESP32-S3-Touch-LCD-7B: 1024x600, the same driver (chipguyhere)
#define DISPLAY_JC8012_1280x800 17   // Guition JC8012P4A1C 10.1": ESP32-P4, 800x1280 standing on MIPI-DSI, GSL3680, chipguy's driver (chipguyhere)
#define DISPLAY_CROW50_800x480  18   // Elecrow CrowPanel 5.0" plain (DIS07050H), not the Advance: ESP32-S3, RGB bus, GT911, backlight on GPIO 2
#define DISPLAY_ZLX2_320x240    19   // ZLX-ESP32-2 2.8": ESP32-C5, ST7789, XPT2046 touch on its own pins

#ifndef RADAR_DISPLAY
#define RADAR_DISPLAY DISPLAY_ILI9341_320x240
#endif

#if RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || RADAR_DISPLAY == DISPLAY_ES3C28P_320x240 || \
    RADAR_DISPLAY == DISPLAY_NMCYDC5_320x240 || RADAR_DISPLAY == DISPLAY_CROWADV28_320x240 || \
    RADAR_DISPLAY == DISPLAY_ZLX2_320x240
#define GFX_W 320
#define GFX_H 240
#elif RADAR_DISPLAY == DISPLAY_ST7796_480x320 || RADAR_DISPLAY == DISPLAY_ILI9486_480x320
#define GFX_W 480
#define GFX_H 320
#elif RADAR_DISPLAY == DISPLAY_GC9A01_240x240
#define GFX_W 240
#define GFX_H 240
#define RADAR_ROUND 1                // glass is a circle, nothing may sit in the corners
#elif RADAR_DISPLAY == DISPLAY_EPD_400x300
#define GFX_W 400
#define GFX_H 300
#define RADAR_EPAPER 1               // slow refresh, 4 greys, no animation
#elif RADAR_DISPLAY == DISPLAY_EPD_792x272
#define GFX_W 792
#define GFX_H 272
#define RADAR_EPAPER 1
#elif RADAR_DISPLAY == DISPLAY_CROWADV50_800x480 || RADAR_DISPLAY == DISPLAY_JC8048W550_800x480 || \
      RADAR_DISPLAY == DISPLAY_CROW70_800x480 || RADAR_DISPLAY == DISPLAY_CROW50_800x480 || \
      RADAR_DISPLAY == DISPLAY_WAVE7_800x480
/* the 5" boards and the 7": the 3.5"'s layout, everything half as big again on an 800x480 glass.
 * GFX_W and GFX_H stay the layout's: the pages, the touch and every number in them go on as they are, and
 * core/gfx.c puts it on the glass in the glass's own pixels (GFX_Z). 534 and not 533: its middle
 * is the glass's middle, and the half pixel past the edge is clipped */
#define GFX_W 534
#define GFX_H 320
#define GFX_PW 800
#define GFX_PH 480
#define GFX_SCALED 1
#define GFX_SCALE_NUM 3              // the glass's pixels for one of the layout's: 3/2
#define GFX_SCALE_SHIFT 1
#define RADAR_RGB_BUS 1              // a frame in memory read out by the chip: its own byte order
#elif RADAR_DISPLAY == DISPLAY_WAVE7B_1024x600
/* the Waveshare 7B: the same layout 15/8 as big on a 1024x600 glass. 547 and not 546: 1024 / 1.875 =
 * 546.1, the half pixel past the edge is clipped. the fonts are the 5" boards' 3/2 ones, as on the
 * Tab5: the writing is 4/5 of its size */
#define GFX_W 547
#define GFX_H 320
#define GFX_PW 1024
#define GFX_PH 600
#define GFX_SCALED 1
#define GFX_SCALE_NUM 15             // 15/8
#define GFX_SCALE_SHIFT 3
#define RADAR_RGB_BUS 1
#elif RADAR_DISPLAY == DISPLAY_TAB5_1280x720
/* the Tab5: the same layout 9/4 as big on a 1280x720 glass (the panel stands, 720x1280: the
 * driver turns it, firmware/main/lcd_dsi.c). 569 and not 568: 1280 / 2.25 = 568.9, the half
 * pixel past the edge is clipped. the fonts are the 5" boards' 3/2 ones for now, so the writing
 * is two thirds of its size: a 9/4 bake of them is to come */
#define GFX_W 569
#define GFX_H 320
#define GFX_PW 1280
#define GFX_PH 720
#define GFX_SCALED 1
#define GFX_SCALE_NUM 9              // 9/4
#define GFX_SCALE_SHIFT 2
#define RADAR_RGB_BUS 1
#elif RADAR_DISPLAY == DISPLAY_JC8012_1280x800
/* the Guition JC8012P4A1C (chipguyhere): the same layout 5/2 as big on a 1280x800 glass, lying on a
 * panel that stands (800x1280: firmware/main/lcd_jc8012.c turns it). 512 x 5/2 is 1280 exactly. the
 * fonts are the 5" boards' 3/2 ones, as on the Tab5 and the 7B: the writing is 3/5 of its size */
#define GFX_W 512
#define GFX_H 320
#define GFX_PW 1280
#define GFX_PH 800
#define GFX_SCALED 1
#define GFX_SCALE_NUM 5              // 5/2
#define GFX_SCALE_SHIFT 1
#define RADAR_RGB_BUS 1
#elif RADAR_DISPLAY == DISPLAY_4827S043_480x272
/* the 4.3" 480x272: a layout of its own, one pixel one pixel. the 2.8"'s choices where they go by the
 * height (the pages, the keyboard, the gear), as everything under 320 rows takes. its panel, 219 wide,
 * takes the wide panels' letters (PANEL_WIDE), in rows a little shorter */
#define GFX_W 480
#define GFX_H 272
#define RADAR_RGB_BUS 1
#else
#error "unknown RADAR_DISPLAY, see config.h"
#endif

/* the glass: on every other screen one pixel of the layout is one pixel of it */
#ifndef GFX_SCALED
#define GFX_SCALED 0
#define GFX_PW GFX_W
#define GFX_PH GFX_H
#endif
#ifndef RADAR_RGB_BUS
#define RADAR_RGB_BUS 0
#endif

/* the moving layer (chipguyhere): the planes, the sweep and the compass ring are not drawn into
 * the frame in memory but put over it as the panel is fed, every panel frame (firmware/main/
 * lcd_wave7.cpp). the frame in memory holds the rest and is drawn again only as time allows */
#if RADAR_DISPLAY == DISPLAY_WAVE7_800x480 || RADAR_DISPLAY == DISPLAY_WAVE7B_1024x600 || RADAR_DISPLAY == DISPLAY_JC8012_1280x800
#define RADAR_OVERLAY 1
#else
#define RADAR_OVERLAY 0
#endif
/* the JC8012P4A1C's panel stands and the picture lies: the panel is fed a column of the picture at a
 * time, so the moving layer is kept and put on by columns (firmware/main/ovl.c) */
#if RADAR_DISPLAY == DISPLAY_JC8012_1280x800
#define RADAR_OVERLAY_COLS 1
#else
#define RADAR_OVERLAY_COLS 0
#endif

#ifndef RADAR_ROUND
#define RADAR_ROUND 0
#endif
#ifndef RADAR_EPAPER
#define RADAR_EPAPER 0
#endif

/* boards with touch. all of them go thru the 5 cross alignment once: the resistive ones
 * need it anyway, and on the round one it finds out how the touch chip's axes sit */
#define RADAR_TOUCH_CAL (RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || RADAR_DISPLAY == DISPLAY_ST7796_480x320 || \
                         RADAR_DISPLAY == DISPLAY_GC9A01_240x240 || RADAR_DISPLAY == DISPLAY_ES3C28P_320x240 || \
                         RADAR_DISPLAY == DISPLAY_NMCYDC5_320x240 || RADAR_DISPLAY == DISPLAY_CROWADV28_320x240 || \
                         RADAR_DISPLAY == DISPLAY_ZLX2_320x240 || \
                         RADAR_DISPLAY == DISPLAY_4827S043_480x272 || GFX_SCALED)

/* the RPi LCD has no touch: BOOT does what it can there, the site sends the rest */
#define RADAR_HAS_TOUCH RADAR_TOUCH_CAL

/* the RGB LED: on three pins on the back of the 2.8" and the 3.5" CYD, a WS2812 on one pin on the
 * ES3C28P and the NM-CYD-C5 (firmware/main/board.h has the pins) */
#define RADAR_HAS_LED (RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || RADAR_DISPLAY == DISPLAY_ST7796_480x320 || \
                       RADAR_DISPLAY == DISPLAY_ES3C28P_320x240 || RADAR_DISPLAY == DISPLAY_NMCYDC5_320x240)
/* the CYDs' red is on one of two pins and nothing tells which: the user picks (core/light.h) */
#define RADAR_LED_RED_PIN (RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || RADAR_DISPLAY == DISPLAY_ST7796_480x320)

/* a BOOT button the firmware can read: every board but the Tab5, whose buttons go to its power
 * chip and its C6, not the P4, and the plain CrowPanel 5.0 and the CrowPanel 7.0, whose BOOT is
 * the screen's clock line. the pages say what BOOT does only where there is one */
#define RADAR_HAS_BUTTON (RADAR_DISPLAY != DISPLAY_TAB5_1280x720 && RADAR_DISPLAY != DISPLAY_CROW70_800x480 && \
                          RADAR_DISPLAY != DISPLAY_CROW50_800x480 && \
                          (!RADAR_OVERLAY || RADAR_OVERLAY_COLS))   // the Waveshare 7/7B: BOOT is GPIO0, a panel data line. the JC8012's is GPIO 35

/* the screen's light turns down (SET's BACKLIGHT): a PWM pin, or a driver that dims it. not the RPi LCD
 * (its light is wired to the power), the round board (a bit of its expander), the CrowPanel Advance 5.0
 * (its own chip only turns it on) or e-paper */
#define RADAR_HAS_DIM (!RADAR_EPAPER && RADAR_DISPLAY != DISPLAY_ILI9486_480x320 && \
                       RADAR_DISPLAY != DISPLAY_GC9A01_240x240 && RADAR_DISPLAY != DISPLAY_CROWADV50_800x480)

#define RADAR_NAME "OWLOS FLIGHTS"

/* the board, as ABOUT says it */
#if RADAR_DISPLAY == DISPLAY_ILI9341_320x240
#define RADAR_BOARD "2.8\" CYD, 320x240"
#elif RADAR_DISPLAY == DISPLAY_ST7796_480x320
#define RADAR_BOARD "3.5\" CYD, ST7796 480x320"
#elif RADAR_DISPLAY == DISPLAY_ILI9486_480x320
#define RADAR_BOARD "3.5\" RPI LCD, ILI9486 480x320"
#elif RADAR_DISPLAY == DISPLAY_GC9A01_240x240
#define RADAR_BOARD "1.28\" ROUND, GC9A01"
#elif RADAR_DISPLAY == DISPLAY_ES3C28P_320x240
#define RADAR_BOARD "2.8\" ES3C28P, ESP32-S3"
#elif RADAR_DISPLAY == DISPLAY_NMCYDC5_320x240
#define RADAR_BOARD "2.8\" NM-CYD-C5, ESP32-C5"
#elif RADAR_DISPLAY == DISPLAY_ZLX2_320x240
#define RADAR_BOARD "2.8\" ZLX-ESP32-2, ESP32-C5"
#elif RADAR_DISPLAY == DISPLAY_CROWADV28_320x240
#define RADAR_BOARD "2.8\" CrowPanel Advance, S3"
#elif RADAR_DISPLAY == DISPLAY_CROWADV50_800x480
#define RADAR_BOARD "5\" CrowPanel Advance, S3"
#elif RADAR_DISPLAY == DISPLAY_CROW50_800x480
#define RADAR_BOARD "5\" CrowPanel, S3"
#elif RADAR_DISPLAY == DISPLAY_JC8048W550_800x480
#define RADAR_BOARD "5\" Guition JC8048W550, S3"
#elif RADAR_DISPLAY == DISPLAY_TAB5_1280x720
#define RADAR_BOARD "5\" M5STACK TAB5, ESP32-P4"
#elif RADAR_DISPLAY == DISPLAY_4827S043_480x272
#define RADAR_BOARD "4.3\" ESP32-4827S043, S3"
#elif RADAR_DISPLAY == DISPLAY_CROW70_800x480
#define RADAR_BOARD "7\" CrowPanel 7.0, S3"
#elif RADAR_DISPLAY == DISPLAY_WAVE7_800x480
#define RADAR_BOARD "7\" Waveshare S3-Touch-LCD-7"
#elif RADAR_DISPLAY == DISPLAY_WAVE7B_1024x600
#define RADAR_BOARD "7\" Waveshare S3-Touch-LCD-7B"
#elif RADAR_DISPLAY == DISPLAY_JC8012_1280x800
#define RADAR_BOARD "10.1\" Guition JC8012P4A1C, P4"
#elif RADAR_DISPLAY == DISPLAY_EPD_400x300
#define RADAR_BOARD "4.2\" CrowPanel E-PAPER, S3"
#elif RADAR_DISPLAY == DISPLAY_EPD_792x272
#define RADAR_BOARD "5.79\" CrowPanel E-PAPER, S3"
#else
#define RADAR_BOARD "E-PAPER"
#endif
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
       RADAR_DISPLAY EQUAL 15 OR RADAR_DISPLAY EQUAL 16 OR RADAR_DISPLAY EQUAL 18)   # and those with an RGB panel (15, 16: the Waveshare 7/7B, 18: the plain CrowPanel 5.0)
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.s3lcd;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.s3rgb")
    if(RADAR_DISPLAY EQUAL 15 OR RADAR_DISPLAY EQUAL 16)
        list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.wave7")
    endif()
elseif(RADAR_DISPLAY EQUAL 17)   # the Guition JC8012P4A1C (chipguyhere): after sdkconfig.defaults.esp32p4, the Tab5's
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.jc8012")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "RADAR_DISPLAY=${RADAR_DISPLAY}" APPEND)

# the version is the first "## " line of CHANGES.md: the site shows that file as the history and
# publishes the same number in version.json, which the board compares its own with
set(CHANGES "${CMAKE_CURRENT_LIST_DIR}/../CHANGES.md")
file(STRINGS "${CHANGES}" FLIGHT_VERSION REGEX "^## [0-9]" LIMIT_COUNT 1)
# "(beta)" on that line: a beta build. the site keeps it beside the stable, and it follows the site's
# betas where a stable build follows only the stable (main.c update_check)
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

## firmware/main/CMakeLists.txt

```cmake
# every board's driver is in here, each file compiles to nothing unless board.h picks it
# the P4 (the Tab5) turns its strips with the chip's PPA, a component the other chips do not have
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

## firmware/main/idf_component.yml

What the P4 boards fetch from the component registry. Each entry has a rule, so nothing is fetched for the other chips.

```yaml
# the Tab5 (RADAR_DISPLAY 12, ESP32-P4) comes from the component registry: Espressif's board package
# for it, and the WiFi, which the P4 has none of its own: esp_wifi_remote puts the esp_wifi calls
# through to the ESP32-C6 on the board over ESP-Hosted (sdkconfig.defaults.esp32p4 has the wires).
# nothing is fetched for the other chips
dependencies:
  idf: ">=5.5"
  espressif/m5stack_tab5:
    version: "^1.3.2"
    rules:
      - if: "target == esp32p4"
  espressif/esp_lcd_touch:       # the package reads the touch through it, touch.c asks it for the points
    version: "^1.2.1"    # the newest when the Tab5 was built; a 2.x may change the calls touch.c makes
    rules:
      - if: "target == esp32p4"
  espressif/esp_lvgl_port:       # the Tab5's package wants it; 2.9.0 (2026-11) uses a DPI callback ESP-IDF 5.5.3 does not have
    version: "~2.8.0"            # (chipguyhere: every P4 build stopped on it, the JC8012P4A1C's too, which does not use it)
    rules:
      - if: "target == esp32p4"
  espressif/esp_wifi_remote:
    version: "^1.6.5"    # the newest when the Tab5 was built; 1.6.3 and older refuse an esp_hosted 3.x
    rules:
      - if: "target == esp32p4"
  espressif/esp_hosted:
    version: "^3.1.0"    # knows the Tab5 (ESP32P4_TAB5_C6_BOARD); the board's own C6 firmware (1.4.1) answers a 3.x host, as OrcSDR shows
    rules:
      - if: "target == esp32p4"
```

## firmware/BUILD.BAT

```bat
@echo off
rem build OWLOS Flight Radar firmware with ESP-IDF 5.5 (same install as for ViewOwl)
rem   BUILD.BAT                   build for the 320x240 CYD
rem   BUILD.BAT <port>              build, flash to <port>, open serial monitor (opening the port resets the board)
rem   BUILD.BAT st7796            build for another display
rem   BUILD.BAT st7796 <port>       same, and flash it
rem displays: ili9341 (2.8" CYD, all three revisions, the default)  st7796 (3.5" CYD)
rem           ili9486 (RPi LCD on OWLOSAirQuality)  gc9a01 (round 1.28", ESP32-C3)
rem           es3c28p (LCDWIKI ES3C28P 2.8", ESP32-S3)  nmcydc5 (NM-CYD-C5 2.8", ESP32-C5)
rem           zlx2 (ZLX-ESP32-2 2.8", ESP32-C5)
rem           crowadv28 (Elecrow CrowPanel Advance 2.8", ESP32-S3)
rem           crowadv50 (Elecrow CrowPanel Advance 5.0", ESP32-S3)  jc8048w550 (Guition 5", ESP32-S3)
rem           tab5 (M5Stack Tab5 5", ESP32-P4)  esp4827s043 (ESP32-4827S043C 4.3", ESP32-S3)
rem           crow70 (Elecrow CrowPanel 7.0", ESP32-S3)
rem           crow50 (Elecrow CrowPanel 5.0" plain, DIS07050H, ESP32-S3)
rem           wave7 (Waveshare ESP32-S3-Touch-LCD-7)  wave7b (Waveshare ESP32-S3-Touch-LCD-7B, 1024x600)
rem           jc8012 (Guition JC8012P4A1C 10.1", ESP32-P4)
rem           epd400 (4.2" e-paper, S3)  epd792 (5.79" e-paper, S3)
rem each display gets its own build folder and sdkconfig, so switching doesnt rebuild everything
setlocal
set DISP=%~1
set PORT=%~2
if /i "%DISP:~0,3%"=="COM" (
  set PORT=%DISP%
  set DISP=
)
if "%DISP%"=="" set DISP=ili9341
set NUM=
if /i "%DISP%"=="ili9341" (set NUM=1& set TARGET=esp32)
if /i "%DISP%"=="st7796"  (set NUM=2& set TARGET=esp32)
if /i "%DISP%"=="ili9486" (set NUM=3& set TARGET=esp32)
if /i "%DISP%"=="gc9a01"  (set NUM=4& set TARGET=esp32c3)
if /i "%DISP%"=="epd400"  (set NUM=5& set TARGET=esp32s3)
if /i "%DISP%"=="epd792"  (set NUM=6& set TARGET=esp32s3)
if /i "%DISP%"=="es3c28p" (set NUM=7& set TARGET=esp32s3)
if /i "%DISP%"=="nmcydc5" (set NUM=8& set TARGET=esp32c5)
if /i "%DISP%"=="zlx2" (set NUM=19& set TARGET=esp32c5)
if /i "%DISP%"=="crowadv28" (set NUM=9& set TARGET=esp32s3)
if /i "%DISP%"=="crowadv50" (set NUM=10& set TARGET=esp32s3)
if /i "%DISP%"=="jc8048w550" (set NUM=11& set TARGET=esp32s3)
if /i "%DISP%"=="tab5" (set NUM=12& set TARGET=esp32p4)
if /i "%DISP%"=="esp4827s043" (set NUM=13& set TARGET=esp32s3)
if /i "%DISP%"=="crow70" (set NUM=14& set TARGET=esp32s3)
if /i "%DISP%"=="crow50" (set NUM=18& set TARGET=esp32s3)
if /i "%DISP%"=="wave7" (set NUM=15& set TARGET=esp32s3)
if /i "%DISP%"=="wave7b" (set NUM=16& set TARGET=esp32s3)
if /i "%DISP%"=="jc8012" (set NUM=17& set TARGET=esp32p4)
if "%NUM%"=="" (
  echo unknown display %DISP%, pick one of: ili9341 st7796 ili9486 gc9a01 es3c28p nmcydc5 zlx2 crowadv28 crowadv50 crow50 jc8048w550 tab5 esp4827s043 crow70 wave7 wave7b jc8012 epd400 epd792
  exit /b 1
)
rem the CYD keeps the plain build folder and sdkconfig it always had
set BDIR=build
set SDK=%~dp0sdkconfig
if not "%NUM%"=="1" (
  set BDIR=build_%DISP%
  set SDK=%~dp0build_%DISP%\sdkconfig
)

if "%IDF_PATH%"=="" set "IDF_PATH=%USERPROFILE%\esp\v5.5\esp-idf"
if "%IDF_TOOLS_PATH%"=="" set "IDF_TOOLS_PATH=%USERPROFILE%\.espressif"
if not exist "%IDF_PATH%\export.bat" (
  echo ESP-IDF not found at %IDF_PATH%
  exit /b 1
)
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
rem ESP-IDF 5.5.0 still calls the C5 a preview target, later 5.5 releases (the site's build) do not.
rem the flag lets 5.5.0 build it and changes nothing for the other chips
set PREVIEW=
if /i "%TARGET%"=="esp32c5" set PREVIEW=--preview
if "%PORT%"=="" (
  idf.py %PREVIEW% %ARGS% build
) else (
  idf.py %PREVIEW% %ARGS% -p %PORT% build flash monitor
)
```

## firmware/build.sh (macOS and Linux, the contributed boards)

```sh
#!/bin/sh
# macOS / Linux counterpart of BUILD.BAT for the boards chipguyhere added (and any other, by number):
#   ./build.sh wave7b                     build for the Waveshare ESP32-S3-Touch-LCD-7B
#   ./build.sh wave7b /dev/cu.usbserial-X build, flash, open the serial monitor
#   ./build.sh jc8012                     the Guition JC8012P4A1C (ESP32-P4)
# needs ESP-IDF 5.5 ($IDF_PATH, or ~/esp/esp-idf, ~/espidf5.5/esp-idf)
set -e
DISP=${1:-wave7b}
PORT=$2
case "$DISP" in
  wave7)  NUM=15 ;;
  wave7b) NUM=16 ;;
  crow70) NUM=14 ;;
  jc8012) NUM=17 ;;
  crow50) NUM=18 ;;
  *) echo "this script knows wave7 wave7b crow70 jc8012 crow50; the rest: BUILD.BAT"; exit 1 ;;
esac
TARGET=esp32s3
[ "$NUM" = 17 ] && TARGET=esp32p4
if [ -z "$IDF_PATH" ]; then
  for d in "$HOME/esp/esp-idf" "$HOME/espidf5.5/esp-idf"; do [ -f "$d/export.sh" ] && IDF_PATH=$d && break; done
fi
[ -f "$IDF_PATH/export.sh" ] || { echo "ESP-IDF not found, set IDF_PATH"; exit 1; }
. "$IDF_PATH/export.sh" >/dev/null
cd "$(dirname "$0")"
ARGS="-B build_$DISP -DSDKCONFIG=build_$DISP/sdkconfig -DIDF_TARGET=$TARGET -DRADAR_DISPLAY=$NUM"
if [ -z "$PORT" ]; then idf.py $ARGS build; else idf.py $ARGS -p "$PORT" build flash monitor; fi
```

## The sdkconfig files

How they are layered (firmware/CMakeLists.txt, set before `include(project.cmake)`), later files win:

- every board: `sdkconfig.defaults`, then `sdkconfig.defaults.<target>` (found by IDF by name);
- the S3 SPI LCD boards (7, 9): then `.s3lcd`;
- the S3 RGB boards (10, 11, 13, 14, 15, 16, 18): then `.s3lcd` and `.s3rgb`;
- the Waveshare 7/7B (15, 16): then `.wave7` as well;
- the Guition JC8012P4A1C (17): `.esp32p4` (by its target), then `.jc8012`.

The chip files describe what each setting is for: chips/.

### firmware/sdkconfig.defaults

```text
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y
CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
# the servers are HTTPS: the common root certificates, enough for the ADS-B networks and FR24
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y
# ADSB.FI sends its GTS Root R4 signed by an old GlobalSign root. that old root is not in
# this bundle, and the same GTS root is already trusted on its own. ESP-IDF 6. a 5.5 build
# warns that it does not know this line and skips it
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

### firmware/sdkconfig.defaults.esp32

```text
# classic ESP32 boards (both CYDs, OWLOSAirQuality): full speed
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
# the flash at 80 MHz on four lines (how Arduino flashes these boards too): the code and the fonts
# that do not fit in IRAM are read from it. the 3.5" radar went 124 -> 99 ms a frame with it
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
```

### firmware/sdkconfig.defaults.esp32c3

```text
# round CrowPanel 1.28": ESP32-C3, 2 MB flash, one core, 160 MHz tops
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_2mb.csv"
# the flash at 80 MHz on four lines: the C3 runs most of its code from it
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
```

### firmware/sdkconfig.defaults.esp32s3

```text
# CrowPanel e-paper boards: ESP32-S3 with 8 MB of flash (the 4.2" read so, 2026-11-22). the 4 MB
# layout of the other boards: ViewOwl's 2 MB left the app 1.3 MB, and it is 1.2 already. the S3
# boards with an LCD say the same again in their own file
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
# PSRAM: the e-paper keeps two 54 KB pictures there (main/epd.c). the 5.79" board is an
# ESP32-S3R8 (8 MB octal). a board without it just runs without it
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_IGNORE_NOTFOUND=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
```

### firmware/sdkconfig.defaults.s3lcd

```text
# the S3 boards with an LCD, ES3C28P and CrowPanel Advance 2.8" (both N16R8): read after
# sdkconfig.defaults.esp32s3, whose 2 MB is for the e-paper boards. these have 16 MB of flash;
# the CYD's 4 MB layout runs on them as it is, the map's tiles get the same 2 MB as there. the
# octal PSRAM stays on from the S3 file
# the log goes to two ports here, UART0 and the S3's own USB: deeper than on the ESP32, and on the
# event task that went past the stack's end
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
```

### firmware/sdkconfig.defaults.s3rgb

```text
# the S3 boards with an RGB panel (the 5" ones, main/lcd_rgb.c): read after sdkconfig.defaults.s3lcd.
# the screen is read out of the PSRAM all the time, so the PSRAM runs at its full speed, and the code
# lies there too: ESP-IDF's RGB notes ask for that, it leaves the flash alone while a frame goes out
# and keeps the cache on while the map's tiles and the settings are written
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_LCD_RGB_ISR_IRAM_SAFE=y
# when a frame cannot keep up all the same, the picture finds its place again at the next VSYNC
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y
# TLS and the WiFi's buffers out of the RAM: the strips and the bounce buffers want it, on the first
# board that ran this the second core's strip found no room. the WiFi asks for PSRAM only when
# PSRAM is not optional, and on these boards the picture is in it anyway
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set
```

### firmware/sdkconfig.defaults.wave7

```text
# the Waveshare ESP32-S3-Touch-LCD-7 and -7B (chipguyhere), read after sdkconfig.defaults.s3rgb.
# chipguyhere's 7/7B library (components/chipguy_lcd7_7b) was written on the Arduino core, whose
# ESP-IDF has these on: the run-time counters it reads each core's idle share from
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
# plain malloc may go to the PSRAM for anything of 1 KB or more (the scope's 41 KB angle table among
# them): the internal RAM is wanted for the 1024-wide bounce buffers, the strips and the WiFi. what
# must be internal asks for it by name (heap_caps_malloc MALLOC_CAP_INTERNAL)
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024
# TLS: AES in software. the hardware AES copies data that lies in the PSRAM (where TLS's buffers are,
# sdkconfig.defaults.s3rgb) through a DMA buffer it takes from the internal RAM, which the 1024-wide
# bounce buffers and strips leave short: "esp-aes: Failed to allocate memory", and the request failed
CONFIG_MBEDTLS_HARDWARE_AES=n
# big state no interrupt touches goes in the PSRAM (core/gfx.h CORE_BIG): ~65 KB of internal RAM back
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
# a crash leaves its core dump in the flash (partitions_wave7.csv), for idf.py coredump-info afterwards:
# the board is often running with nobody on its serial port. both boards have 8 MB or more
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_wave7.csv"
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
# the watchdog keeps watching the ui task, not the first core's idle task: a TLS handshake (seconds of
# elliptic-curve maths in the fetch task) beside a page being drawn left the idle task no turn, which
# is no hang, and the board was restarted for it
# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is not set
```

### firmware/sdkconfig.defaults.esp32c5

```text
# ESP32-C5 boards (NM-CYD-C5): one RISC-V core at 240 MHz, WiFi on 2.4 and 5 GHz. the one asked for
# has 16 MB of flash and 8 MB of PSRAM; the CYD's 4 MB layout runs on it as it is
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
# the PSRAM, asked for by name (heap_caps_malloc with MALLOC_CAP_SPIRAM): plain malloc stays in the
# chip's own RAM. the first board that ran this had about 35 KB of that left for the strips and
# stopped, so TLS and the WiFi's buffers go out, and the map's picture (iram_first in main.c). the
# WiFi asks for PSRAM only when PSRAM is not optional: a C5 without it does not start on this build
CONFIG_SPIRAM=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set
# that way the WiFi keeps its send buffers for good, 1.6 KB each and 16 unless told. the radar sends
# short questions, what does not fit waits in the PSRAM
CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=6
# the log goes to two ports here too, see sdkconfig.defaults.s3lcd
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
```

### firmware/sdkconfig.defaults.esp32p4

```text
# the M5Stack Tab5 (main/board.h, lcd_dsi.c): read after sdkconfig.defaults. 16 MB of flash, 32 MB
# of PSRAM, the frame and the map's picture in it. the chip in the one this was written on is a
# P4 before production silicon (v1.3), hence the two lines that let the build run on it; they do
# no harm on a newer one
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
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
# the board package: one frame, and the strips copied in by the CPU (no DMA2D), lcd_dsi.c
CONFIG_BSP_LCD_DPI_BUFFER_NUMS=1
# CONFIG_BSP_LCD_USE_DMA2D is not set
# the WiFi: an ESP32-C6 on the board, reached over SDIO with ESP-Hosted, which knows this board:
# the preset puts the bus on GPIO 8 to 13 and the C6's reset on 15. the C6 keeps the co-processor
# firmware M5Stack shipped (1.4.1 as it reports itself), which this host talks to as it is
CONFIG_ESP32P4_TAB5_C6_BOARD=y
```

### firmware/sdkconfig.defaults.jc8012

```text
# the Guition JC8012P4A1C (RADAR_DISPLAY 17, chipguyhere), read after sdkconfig.defaults.esp32p4 (the
# Tab5's: the P4's PSRAM, clock and flash settings hold here too, and are not repeated)
# the console on UART0: the board's CH340 (its USB_UART port) is the one to upload and watch on
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
# the 16 MB layout with a core dump on flash, as the Waveshare 7/7B have
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_jc8012.csv"
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
# big state no interrupt touches goes in the PSRAM (core/gfx.h CORE_BIG), as on the 7/7B
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
CONFIG_SPIRAM_USE_MALLOC=y
# the watchdog keeps watching the ui task, not the first core's idle task (sdkconfig.defaults.wave7 says why)
# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is not set
# the WiFi: the ESP32-C6 on SDIO slot 1 as on Espressif's ESP32-P4-Function-EV-Board (CLK 18, CMD 19,
# D0..D3 14..17, the C6's reset on 54: chipguy_JC8012P4A1C_board's pin map), not the Tab5's wires
# CONFIG_ESP32P4_TAB5_C6_BOARD is not set
CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD=y
# 128-byte lines in the L2 cache: the fill reads the frames out of the PSRAM a chunk at a time
CONFIG_CACHE_L2_CACHE_LINE_128B=y
# code and read-only data copied into the PSRAM at start and run from there: a flash write (a map tile
# kept, the settings) then no longer stops everything outside IRAM on both cores, the panel's fill and
# its interrupts among them. the S3 builds have it too (sdkconfig.defaults.s3rgb)
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
# each task's share of each core, for the 10 s log (lcd_jc8012.c disp_log)
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
# internal RAM (2026-11-13): the fill's bounce buffers and tables leave ~75 KB free and no block over
# ~31 KB, and TLS handshakes were failing in it. what has no deadline goes to the PSRAM: mbedTLS
# (with XIP from PSRAM, flash writes leave the cache on, so nothing here runs with it off), lwIP's
# buffers, ESP-Hosted's task stacks. the SDIO buffers stay internal (DMA)
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y
```

## The partition tables

### firmware/partitions.csv

```text
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x200000
# the map's tiles as they came from OpenStreetMap (main/tiles.c)
tiles,    data, 0x40,    0x210000, 0x1F0000
```

### firmware/partitions_2mb.csv

```text
# Name,   Type, SubType, Offset,   Size
# for the 2 MB boards (the round C3)
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x150000
# the map's tiles as they came from OpenStreetMap (main/tiles.c)
tiles,    data, 0x40,    0x160000, 0xA0000
```

### firmware/partitions_16mb.csv

```text
# Name,   Type, SubType, Offset,   Size
# the 16 MB boards (the Tab5): the app has room to grow, and the map's tiles 6 MB (main/tiles.c)
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x400000
tiles,    data, 0x40,    0x410000, 0x600000
```

### firmware/partitions_wave7.csv

```text
# Name,   Type, SubType,  Offset,   Size
# the Waveshare 7 (8 MB flash) and 7B (16 MB), chipguyhere: the 4 MB layout's app and tiles, and a
# core dump on flash, so a crash can be read afterwards (idf.py coredump-info) even when nobody was
# watching the serial port
nvs,      data, nvs,      0x9000,   0x6000
phy_init, data, phy,      0xf000,   0x1000
factory,  app,  factory,  0x10000,  0x200000
tiles,    data, 0x40,     0x210000, 0x1F0000
coredump, data, coredump, 0x400000, 0x10000
```

### firmware/partitions_jc8012.csv

```text
# Name,   Type, SubType,  Offset,   Size
# the Guition JC8012P4A1C (16 MB flash), chipguyhere: the 16 MB boards' layout (partitions_16mb.csv)
# and a core dump on flash, as the Waveshare 7/7B have, so a crash can be read afterwards
# (idf.py coredump-info) even when nobody was watching the serial port
nvs,      data, nvs,      0x9000,   0x6000
phy_init, data, phy,      0xf000,   0x1000
factory,  app,  factory,  0x10000,  0x400000
tiles,    data, 0x40,     0x410000, 0x600000
coredump, data, coredump, 0xA10000, 0x10000
```

Which board takes which: `partitions_2mb.csv` the round C3; `partitions_16mb.csv` the Tab5; `partitions_wave7.csv` the
Waveshare 7/7B; `partitions_jc8012.csv` the JC8012; `partitions.csv` every other board, including the 8 and 16 MB S3 and
C5 boards (one layout, the map's tiles get the same 2 MB everywhere).

## .github/make-manifests.sh

```bash
#!/usr/bin/env bash
# writes an ESP Web Tools manifest.json next to every screen's binaries
#   make-manifests.sh <fw folder> "<version text>"
# <fw folder>/<screen>/ holds bootloader.bin, partition-table.bin, owlos-flight-radar.bin and
# the build's flash_args, which says at what address each one goes
set -euo pipefail
FW="$1"
VERSION="$2"

for dir in "$FW"/*/; do
  disp=$(basename "$dir")
  case "$disp" in
    ili9341) name='2.8" CYD (320x240, ILI9341 or ST7789)';   family='ESP32' ;;
    st7796)  name='3.5" CYD (ST7796 480x320)';                family='ESP32' ;;
    ili9486) name='3.5" RPi LCD on OWLOSAirQuality (ILI9486)'; family='ESP32' ;;
    gc9a01)  name='1.28" round (GC9A01 240x240)';              family='ESP32-C3' ;;
    es3c28p) name='2.8" LCDWIKI ES3C28P (ESP32-S3, ILI9341V)'; family='ESP32-S3' ;;
    nmcydc5) name='2.8" NM-CYD-C5 (ESP32-C5, ST7789)';          family='ESP32-C5' ;;
    zlx2)    name='2.8" ZLX-ESP32-2 (ESP32-C5, ST7789, experimental)'; family='ESP32-C5' ;;
    crowadv28) name='2.8" Elecrow CrowPanel Advance (ESP32-S3, ST7789)'; family='ESP32-S3' ;;
    crowadv50) name='5" Elecrow CrowPanel Advance (ESP32-S3, 800x480, experimental)'; family='ESP32-S3' ;;
    jc8048w550) name='5" Guition JC8048W550 (ESP32-S3, 800x480, experimental)'; family='ESP32-S3' ;;
    tab5) name='5" M5Stack Tab5 (ESP32-P4, 1280x720 on MIPI-DSI, experimental)'; family='ESP32-P4' ;;
    esp4827s043) name='4.3" ESP32-4827S043C (ESP32-S3, 480x272, experimental)'; family='ESP32-S3' ;;
    crow70) name='7" Elecrow CrowPanel 7.0 (ESP32-S3, 800x480, experimental)'; family='ESP32-S3' ;;
    wave7) name='7" Waveshare ESP32-S3-Touch-LCD-7 (ESP32-S3, 800x480, experimental)'; family='ESP32-S3' ;;
    wave7b) name='7" Waveshare ESP32-S3-Touch-LCD-7B (ESP32-S3, 1024x600, experimental)'; family='ESP32-S3' ;;
    jc8012) name='10.1" Guition JC8012P4A1C (ESP32-P4, 1280x800 on MIPI-DSI, experimental)'; family='ESP32-P4' ;;
    crow50) name='5" Elecrow CrowPanel (ESP32-S3, 800x480, plain DIS07050H, experimental)'; family='ESP32-S3' ;;
    *) echo "skipping unknown folder $disp"; continue ;;
  esac
  parts=""
  while read -r addr path; do
    case "$addr" in ""|--*) continue ;; esac          # the first line is the flash settings
    file=$(basename "$path")
    [ -f "$dir/$file" ] || { echo "$disp: $file is missing" >&2; exit 1; }
    parts="$parts${parts:+, }{ \"path\": \"$file\", \"offset\": $((addr)) }"
  done < "$dir/flash_args"
  cat > "$dir/manifest.json" <<JSON
{
  "name": "OWLOS Flight Radar - ${name//\"/\\\"}",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [ { "chipFamily": "$family", "parts": [ $parts ] } ]
}
JSON
  echo "$disp: $(tr -d '\n' < "$dir/manifest.json")"
done
```

## .github/workflows/pages.yml, the firmware job (the site job is in esp32-flash-and-settings)

```yaml
  firmware:
    name: Firmware ${{ matrix.disp }}
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        include:
          - { disp: ili9341, num: 1, target: esp32 }
          - { disp: st7796,  num: 2, target: esp32 }
          - { disp: ili9486, num: 3, target: esp32 }
          - { disp: gc9a01,  num: 4, target: esp32c3 }
          - { disp: es3c28p, num: 7, target: esp32s3 }
          - { disp: nmcydc5, num: 8, target: esp32c5 }
          - { disp: zlx2, num: 19, target: esp32c5 }
          - { disp: crowadv28, num: 9, target: esp32s3 }
          - { disp: crowadv50, num: 10, target: esp32s3 }
          - { disp: jc8048w550, num: 11, target: esp32s3 }
          - { disp: tab5, num: 12, target: esp32p4 }
          - { disp: esp4827s043, num: 13, target: esp32s3 }
          - { disp: crow70, num: 14, target: esp32s3 }
          - { disp: wave7, num: 15, target: esp32s3 }
          - { disp: wave7b, num: 16, target: esp32s3 }
          - { disp: jc8012, num: 17, target: esp32p4 }
          - { disp: crow50, num: 18, target: esp32s3 }
    steps:
      - uses: actions/checkout@v4

      - name: Build
        # the whole repo goes in, firmware/ pulls ../core. git inside the container runs as
        # root on files that aren't root's, hence the safe.directory line
        run: |
          docker run --rm -v "${{ github.workspace }}:/project" -w /project/firmware --user root \
            espressif/idf:release-v5.5 \
            bash -c "git config --global --add safe.directory '*' && \
                     idf.py --no-ccache -B build_${{ matrix.disp }} \
                       -DSDKCONFIG=/project/firmware/build_${{ matrix.disp }}/sdkconfig \
                       -DIDF_TARGET=${{ matrix.target }} -DRADAR_DISPLAY=${{ matrix.num }} build"

      - name: Collect the binaries
        # flash_args is written by the build: where each part goes
        run: |
          B=firmware/build_${{ matrix.disp }}
          mkdir -p out/${{ matrix.disp }}
          cp "$B/bootloader/bootloader.bin" "$B/partition_table/partition-table.bin" \
             "$B/owlos-flight-radar.bin" "$B/flash_args" out/${{ matrix.disp }}/
          ls -l out/${{ matrix.disp }}

      - uses: actions/upload-artifact@v4
        with:
          name: fw-${{ matrix.disp }}
          path: out

      # not for the site: the ELF turns a crash backtrace from a board flashed off the
      # site into file and line. a locally built ELF doesn't match
      - uses: actions/upload-artifact@v4
        with:
          name: elf-${{ matrix.disp }}
          path: |
            firmware/build_${{ matrix.disp }}/owlos-flight-radar.elf
            firmware/build_${{ matrix.disp }}/sdkconfig
          retention-days: 90
```

The same workflow has a `web` job that builds `core/` for WebAssembly, one twin a screen size (`10:Twin`, `1:Twin1`,
`2:Twin2`, `4:Twin4`, `13:Twin5`: 800x480, 320x240, 480x320, the round one, 480x272), the 5" one with its own glass
fonts. A board of a new size needs a twin there and a `twin` name in flow.js.

## Where else a board is named

- `site/flow.js`, `BOARDS`: one line a build, what the page shows and does. The real line of a board added last:

  ```js
  zlx2: { name: '2.8" ZLX-ESP32-2', spec: 'ESP32-C5, 320x240, resistive touch, experimental', chip: 'c5', w: 320, h: 240, inch: 2.8, touch: 'resistive', twin: 'Twin1', shot: '02-live.png', screens: 'two' },
  ```

  Fields: `chip` (esp32, s3, c3, c5, p4), the glass's `w` and `h`, `inch`, `touch`, `twin` (the web twin of its size),
  `shot` (a picture kept by .gitignore), `screens` ('two' or 'three': the colour profiles BOOT goes through), `round`,
  `yellow`, `noboot` (no BOOT the firmware reads: the page says nothing about pressing it), `nvsfw` (the erase box warns
  that the touch firmware goes too). `stableBoards` and `inStable()`: a board not in the stable version yet is flashed
  the beta only, with no row to pick the stable.
- `tools/assemble.sh`: the list of build folders put together for a look at the site on the build PC.
- `server/appsettings.json`, `Stats.Builds`: the build names the stats server counts. It is read once, at start: a board
  added to the repository means a server update too.
- `README.md`: the boards list, the `screen` values of the USB console, the build list under "Build it yourself".

## The other projects, where they differ

Each is its own repository with its own copy, so the same board can have other values:

- OWLOS WiFi Radar `core/config.h`: the numbers 1 to 6 are the same. `board.h`: the same blocks with no `LCD_ORDER`, so a
  2.8" starts as v1; strips of 12 rows on the 480-wide boards, 24 on the round one. `sdkconfig.defaults`: DIO 40 MHz,
  4 MB, no WiFi or TLS lines; `.esp32` CPU 240 only; `.esp32c3` CPU 160, 2 MB; `.esp32s3` the same file as Flight
  Radar's had then (2 MB). Partitions: app 0x10000 size 0x180000, `radar` data 0x40 at 0x190000 size 0x80000 (2 MB
  layout: app 0x170000, radar at 0x180000), a ring of 12 KB slots.
- OWLOS Thermo Relay: the 2.8" yellow board only, no WiFi. `board.h` adds `PIN_PROBE 27`, `PIN_RELAY 22`, `PIN_LED_R 4`,
  `PIN_LED_G 16`, `PIN_LED_B 17`. sdkconfig adds `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y` and `CONFIG_ESP_BROWNOUT_DET=y`: a
  weak USB supply or the relay's coil resets it cleanly, and the relay starts off. Partitions: app 0x180000, `thermo` at
  0x190000 size 0x40000, with a magic of its own ("THRM") because the radar left its "SLOT" records at the same address
  on boards that ran it. Build with `cmd /c` from PowerShell (Git Bash's `cmd //c` did not find idf.py).
- OWLOS Air Quality UI: the OWLOSAirQuality board only, see [ili9486](boards/ili9486.md).
- RadarScan is an older copy of WiFi Radar: use OWLOS WiFi Radar.
- ESP32 Web Flasher has no board file: one Arduino sketch for all chips.

## Arduino and PlatformIO builds

ESP32 Web Flasher builds with arduino-cli, core esp32:esp32@3.3.5 (chips esp32, s2, s3, c3, c6):

- Four parts, not merged.bin: bootloader (0x1000 on esp32 and s2, 0x0 on those others), partitions 0x8000, boot_app0
  0xE000, app 0x10000. merged.bin is the whole flash and wipes the settings on every update.
- Chips with their own USB are built with `CDCOnBoot=cdc`, and the firmware talks on `Serial` and `Serial0` because it
  cannot know which port is plugged in.
- Build with `--build-path`: on Windows `--output-dir` exits 1 from the core's boot_app0 export hook (measured).
- "0x0 on all but classic" is wrong for a C5 and a P4 (0x2000): its list stops at the C6.

FusionEdge (github.com/SimZs/FusionEdge, GPL-3.0: take facts, never its code into an MIT project) builds with PlatformIO
and pioarduino:

- `platform-espressif32` 55.03.37 = Arduino 3.3.7 on ESP-IDF 5.5.2, LovyanGFX ^1.2.19. A board is a folder of its own
  (`<board>_options`) with `myoptions.h` (pins and parts as defines) and, where the environment differs, a
  `platformio.ini`. CI copies the folder's files over the root ones in its checkout and builds; nothing in the tree
  changes. Matrix: s3-ili9488 (the root files), jc3248w535, gen4-35ct, wrover-n16r8, crowadv28, cyd28, e32r35t.
- The addresses come from the build (`pio project metadata`) and the partition table, never typed.
- PlatformIO refuses Git Bash ("MSys/Mingw is not supported"): run `pio` from PowerShell through `cmd /c`, and set
  `PLATFORMIO_CORE_DIR` to a scratch folder so another project's ~/.platformio is never touched (about a gigabyte on
  first install). pioarduino's `pio project metadata` has flash_images but no application_offset.
- Partitions, 16 MB (`partitions.csv`): nvs 0x9000 size 0x5000, otadata 0xE000 size 0x2000, app0 0x10000 size 0x600000,
  app1 0x610000 size 0x600000, spiffs (LittleFS) 0xC10000 size 0x3D0000, coredump 0xFE0000 size 0x20000.
- 8 MB (`partitions_n8r8.csv`): the same with apps of 0x300000 at 0x10000 and 0x310000, spiffs 0x610000 size 0x1D0000,
  coredump 0x7E0000.
- 4 MB, no PSRAM (`partitions_4mb.csv`, the 2.8" and 3.5" yellow boards): one app slot 0x10000 size 0x330000 with the
  fonts inside it, spiffs 0x340000 size 0xC0000. No second slot to update into, so those boards update from the flasher
  page or over USB. A `data.txt` in the board's folder lists what of `data/` goes into its LittleFS image.
- The no-PSRAM boards' build flags: `-DBOARD_NO_PSRAM` (small audio buffers, MP3 only), `-DLITE_UI` with
  `LITE_SCREEN_W/H` (a screen drawn two rows at a time in place of FusionEdge's own), `-DAUDIO_INTERNAL_DAC` (GPIO 26),
  `-DAUDIO_DAC_LEVEL=<percent>`; flash DIO 40 MHz, `board = esp32dev`.
