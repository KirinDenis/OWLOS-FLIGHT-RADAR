#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Dynamic display geometry (auto-configured at boot by computeLayout())
extern int GFX_W;
extern int GFX_H;
extern int GFX_PW;
extern int GFX_PH;

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ESP32S3)
#define G_RQ_MAX   240
#define GFX_PW_MAX 800
#define GFX_PH_MAX 480
#else
#define G_RQ_MAX   160
#define GFX_PW_MAX 480
#define GFX_PH_MAX 320
#endif

#define DISPLAY_ILI9341_320x240  1
#define DISPLAY_ST7796_480x320   2
#define DISPLAY_ILI9486_480x320  3

#ifndef RADAR_DISPLAY
#define RADAR_DISPLAY DISPLAY_ILI9341_320x240
#endif

#define GFX_SCALED 0
#define RADAR_RGB_BUS 0
#define RADAR_ROUND 0
#define RADAR_EPAPER 0
#define RADAR_TOUCH_CAL 1
#define RADAR_HAS_TOUCH 1
#define RADAR_HAS_LED 1
#define RADAR_LED_RED_PIN 1

#define RADAR_NAME "OWLOS FLIGHTS"
#define RADAR_BOARD "CYD Universal"

#ifdef __cplusplus
}
#endif
