# OWLOS Flight Radar — Universal Arduino Edition

A standalone, 100% Arduino IDE-compatible port of **OWLOS Flight Radar** designed to run out-of-the-box on the entire **ESP32 Cheap Yellow Display (CYD)** family and **ESP32-S3 Parallel RGB** displays.

Based on **OWLOS Flight Radar v0.6** by [Kirin Denis](https://github.com/KirinDenis/OWLOS-FLIGHT-RADAR).

---

## ✈️ Key Features & Enhancements

1. **Universal Hardware Support**:
   - Zero-configuration hardware auto-detection powered by the bundled [`AMK_CYD_Universal_TFT`](https://github.com/aungmoekhine/AMK_CYD_Universal_TFT) library.
   - Automatically probes display controllers, SPI bus pinouts, and touch controllers at boot.
   - Fully tested across:
     - **2.4", 2.8", 3.2" CYD** (ESP32-2432S028, ST7789/ILI9341, 320x240)
     - **3.5", 4.0" CYD** (ESP32-3248S035, ST7796, 480x320)
     - **ESP32-S3 Boards** (Elecrow Advance, LCDWIKI ES3C28P, NM-CYD-C5)
     - **4.3", 5.0", 7.0" ESP32-S3 RGB Displays** (Sunton ESP32-8048Sxxx, Guition, Waveshare, 800x480)

2. **WebSerial Digital Twin & 2-Way Real-Time Mirror**:
   - Full bidirectional communication with the official OWLOS web companion (`https://kirindenis.github.io/OWLOS-FLIGHT-RADAR/`).
   - Seamlessly mirrors:
     - Screen Modes (Radar / Map / Classic)
     - Theme Colors (HUE cycling)
     - Zoom Range (25 km to 350 km)
     - Heading orientation (`up` / `north`)
     - Aircraft touch selection (`OWLOS SEL` / `OWLOS PICK`)
     - Live coordinates & flight telemetry

3. **Multi-Resolution UI Scaling**:
   - Native layout scaling tailored for 320x240, 480x320, and 800x480 resolutions with clean typography and zero text overlap.

4. **Regional Vector & Offline Map Enhancements**:
   - Enhanced offline map boundaries and coastline data (covering Southeast Asia, Myanmar, Thailand, etc.).

5. **ESP32 Core 2.x & 3.x Dual-Mode Compatibility**:
   - Built-in conditional compilation guards for `WiFi.setDNS` and LwIP DNS, compiling cleanly on both older ESP32 Arduino Core 2.0.x and modern Core 3.0.x+.

6. **Hardware Backlight & LDR Auto-Dimming**:
   - Automatic 8-bit LEDC PWM brightness control.
   - Boot-primed 10-read moving average filter for flicker-free ambient light dimming via the onboard LDR (GPIO 34).

---

## 🛠️ Arduino IDE Setup & Build Instructions

### Required Libraries
* **`AMK_CYD_Universal_TFT`** (v1.6.3+): Install via Arduino Library Manager or [GitHub](https://github.com/aungmoekhine/AMK_CYD_Universal_TFT).
* **`ArduinoJson`** (v6.x or v7.x)

### Board & Partition Settings
* **Board**: `ESP32 Dev Module` (or your specific S3 board)
* **Flash Size**: 4MB (for classic CYD) / 8MB or 16MB (for ESP32-S3)
* **Partition Scheme**: `No OTA (2MB APP/2MB SPIFFS)` or `Huge APP (3MB No OTA/1MB SPIFFS)`
* **Upload Speed**: 921600

---

## 👤 Credits & Attribution
* **Original OWLOS Project & Web Companion**: [Kirin Denis](https://github.com/KirinDenis) ([OWLOS-FLIGHT-RADAR](https://github.com/KirinDenis/OWLOS-FLIGHT-RADAR))
* **Universal Arduino Port & Driver Integration**: Aung Moe Khine ([@aungmoekhine](https://github.com/aungmoekhine))
