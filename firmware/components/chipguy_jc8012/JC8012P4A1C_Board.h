#pragma once

// Board abstraction for the Guition JC8012P4A1C — the 10.1" ESP32-P4 panel
// (800x1280 JD9365 MIPI-DSI, GSL3680 touch, ESP32-C6 radio, ES8311 codec).
//
// There is no I/O expander and no PMIC with a bus on this board: every control
// line is a real ESP32-P4 GPIO or fixed in hardware.  What the board DOES have
// is one I2C bus (GPIO7 SDA / GPIO8 SCL) that everything shares — the GSL3680
// touch controller, the ES8311 audio codec, the RX8025T real-time clock, the
// MIPI-CSI camera's SCCB, the 4-pin I2C header CN4 and the FPC3 expansion
// header.  This class owns that bus, wrapped in a mutex-protected
// ThreadsafeWire so tasks and drivers can share it safely, and it is the one
// place the board's pin map and I2C address map are written down so the
// display, power and sketch code can reference names instead of numbers.
//
// Source for every number here: Guition's schematic (sheets 1_PWR … 7_CODEC)
// as republished in github.com/p1ngb4ck/unofficial_guition_esp32p4_repo,
// JC8012P4A1C_I_W_Y/schematics.  See schematic-description.md.
//
// The ThreadsafeWire wrapper lives in chipguy_ThreadsafeWire.h -- a verbatim
// copy of the one in chipguy_Board, vendored so this library depends on nothing
// but Wire.  Its unit of locking is the TRANSACTION, so an ordinary
// beginTransmission/write/endTransmission sequence is already atomic against
// every other task, with no Guard of its own.
//
// Copyright (c) 2026 chipguyhere
// MIT License

#include <Arduino.h>
#include <Wire.h>

// After Arduino.h: CONFIG_IDF_TARGET_* comes from sdkconfig.h, not the command line.
#ifndef CONFIG_IDF_TARGET_ESP32P4
#error "JC8012P4A1C_Board: this board is an ESP32-P4. Select ESP32P4 Dev Module."
#endif

// The shared-bus wrapper, vendored from chipguy_Board so this library depends on
// nothing but Wire. Kept in its own header so device drivers can be written
// against ThreadsafeWire without including this board class. Copy it forward
// when chipguy_Board's copy changes; do not fork it.
#include "chipguy_ThreadsafeWire.h"


class JC8012P4A1C_Board {
public:
    // ── Shared I2C bus ──────────────────────────────────────────────────────
    // Pull-ups on the board: 5.1k (R53/R54) and 2.2k (R43/R44), both to 3.3 V,
    // in parallel — about 1.5k.  Also brought out on the 4-pin header CN4
    // (GND, 3V3, SCL, SDA) and on FPC3 pins 11/12.
    static constexpr int SDA_PIN = 7;
    static constexpr int SCL_PIN = 8;

    // 7-bit I2C addresses of what is on the bus.
    static constexpr uint8_t ES8311_ADDR  = 0x18;  // audio codec (CE pulled low)
    static constexpr uint8_t RX8025T_ADDR = 0x32;  // real-time clock
    static constexpr uint8_t GSL3680_ADDR = 0x40;  // touch controller
    // The camera's SCCB address depends on which sensor is plugged into FPC5.

    // ── Display (JD9365, 2-lane MIPI-DSI) ───────────────────────────────────
    // Driven by chipguy_JC8012P4A1C_display / _480display; listed for the map.
    static constexpr int LCD_RST_PIN       = 27;
    static constexpr int LCD_BACKLIGHT_PIN = 23;  // MP3202 boost enable, active HIGH,
                                                  // PWM-dimmable, 10k pull-down
    static constexpr int TOUCH_RST_PIN     = 22;
    static constexpr int TOUCH_INT_PIN     = 21;  // not an LP GPIO: cannot wake deep sleep
    static constexpr int DSI_PHY_LDO_CHAN  = 3;   // on-chip LDO, 2.5 V, owned by the display

    // ── Audio (ES8311 codec, one I2S port; NS4150 speaker amp) ──────────────
    static constexpr int I2S_MCLK_PIN   = 13;
    static constexpr int I2S_BCLK_PIN   = 12;
    static constexpr int I2S_WS_PIN     = 10;   // LRCK
    static constexpr int I2S_DOUT_PIN   = 9;    // ESP32-P4 -> ES8311 DSDIN (playback)
    static constexpr int I2S_DIN_PIN    = 11;   // ES8311 ASDOUT -> ESP32-P4 (microphone)
    static constexpr int AMP_ENABLE_PIN = 20;   // NS4150 CTRL, active HIGH, 10k pull-down

    // ── TF / microSD slot (SDMMC slot 0, 4-bit) ─────────────────────────────
    // Card power comes from the P4's on-chip LDO channel 4 — see README-sdcard.md.
    static constexpr int SD_CLK_PIN  = 43;
    static constexpr int SD_CMD_PIN  = 44;
    static constexpr int SD_D0_PIN   = 39;
    static constexpr int SD_D1_PIN   = 40;
    static constexpr int SD_D2_PIN   = 41;
    static constexpr int SD_D3_PIN   = 42;
    static constexpr int SD_LDO_CHAN = 4;

    // ── ESP32-C6 radio co-processor (SDIO, SDMMC slot 1) ────────────────────
    // Owned by the core's esp_hosted Wi-Fi/BLE; listed so nothing else uses them.
    static constexpr int C6_SDIO_CLK_PIN = 18;
    static constexpr int C6_SDIO_CMD_PIN = 19;
    static constexpr int C6_SDIO_D0_PIN  = 14;
    static constexpr int C6_SDIO_D1_PIN  = 15;
    static constexpr int C6_SDIO_D2_PIN  = 16;
    static constexpr int C6_SDIO_D3_PIN  = 17;
    static constexpr int C6_RESET_PIN    = 54;  // C6 CHIP_PU (EN), active LOW reset
    static constexpr int C6_IO2_PIN      = 6;   // C6 GPIO2 <-> P4 GPIO6

    // ── Buttons, LED, battery sense ─────────────────────────────────────────
    static constexpr int BOOT_BUTTON_PIN = 35;  // SW3, active LOW, 10k pull-up;
                                                // strapping pin (download mode)
    static constexpr int RGB_LED_PIN     = 26;  // one WS2812 (LED1)
    static constexpr int BATTERY_ADC_PIN = 52;  // V_BAT x 100/168 — see the power library
    // The IP5306's KEY button (SW1) and the reset button (SW2, on CHIP_PU)
    // reach no GPIO.

    // ── UART0 (CH340C USB-serial, and the 4-pin UART connector CN2) ─────────
    static constexpr int UART0_TX_PIN = 37;     // strapping pin
    static constexpr int UART0_RX_PIN = 38;     // strapping pin

    // ── 32.768 kHz crystal (X2) ─────────────────────────────────────────────
    // GPIO0 / GPIO1 carry the RTC crystal and are not usable as GPIOs.

    // ── Expansion headers: free GPIOs ───────────────────────────────────────
    // FPC3 (16-pin FPC): GPIO2..5 and GPIO45..48, plus SCL/SDA and 3.3 V.
    // GPIO2..5 are LP GPIOs, so a button there CAN wake the chip from deep sleep.
    // GPIO45..48 are in the P4's VDDPST_5 I/O bank, which this board powers
    // from on-chip LDO channel 4 — the same LDO as the SD card.  They have no
    // I/O supply unless LDO4 is on (SD_MMC.setPowerChannel(4), or
    // esp_ldo_acquire_channel() on channel 4 at 3300 mV).
    // FPC4 (16-pin FPC): GPIO28..34, plus 3.3 V.  GPIO34 is a strapping pin.
    // See README-board.md for the pin-by-pin tables.
    static constexpr int FPC3_GPIOS[8] = { 2, 3, 4, 5, 45, 46, 47, 48 };
    static constexpr int FPC4_GPIOS[7] = { 28, 29, 30, 31, 32, 33, 34 };

    JC8012P4A1C_Board();

    // Bring up the shared I2C bus (400 kHz) and configure the BOOT button input.
    // Idempotent: the display libraries, the power library and a sketch can all
    // call it; only the first does the work.
    bool begin();

    // The shared, mutex-protected I2C bus.  Use it directly for any other I2C
    // peripheral on the board (or on CN4 / FPC3): every transaction locks
    // itself, so a Guard is needed only to make SEVERAL transactions atomic as
    // a group.  Do not call ::Wire.begin() on other pins — ::Wire IS this bus.
    ThreadsafeWire Wire;

    // ── What answers on the bus?  (I2C probes; call after begin()) ──────────
    bool hasTouch();        // GSL3680 at 0x40 (answers only once out of reset)
    bool hasCodec();        // ES8311 at 0x18
    bool hasRtc();          // RX8025T at 0x32

    // ── Buttons ─────────────────────────────────────────────────────────────
    // True while the BOOT button is held.  GPIO35 is the boot-mode strapping
    // pin, so holding it through a reset enters the bootloader — fine for a UI
    // button, just don't hold it while pressing RESET.  It is not an LP GPIO
    // and so cannot wake the chip from deep sleep.
    bool bootButtonPressed();

    // ── RGB LED ─────────────────────────────────────────────────────────────
    // Set the on-board WS2812.  0,0,0 is off.
    void setLed(uint8_t r, uint8_t g, uint8_t b);

    // ── ESP32-C6 radio ──────────────────────────────────────────────────────
    // The C6 boots its Wi-Fi firmware at power-on (its enable has a pull-up)
    // and draws ~22 mA from the battery whether or not the sketch uses Wi-Fi
    // (measured).  setRadioPower(false) holds it in reset via GPIO54; true
    // releases it (it then reboots its firmware).  The core's Wi-Fi also
    // drives this pin when it starts, so call this only in sketches that don't
    // use Wi-Fi, or around periods when Wi-Fi is off.  The display library's
    // sleepUntil...() calls keep whatever state you set here through the sleep.
    void setRadioPower(bool on);

    // ── Audio power and speaker amplifier ───────────────────────────────────
    // Family-wide audio API: every chipguy board library with sound has these
    // two, so an audio sketch is written the same way on every board.
    //
    // audioPower(): switch the codec's supply.  This board has NO audio rail
    // switch — the ES8311 is on the always-on 3.3 V rail — so this does
    // nothing and returns false, meaning "no switch here; audio is always
    // powered".  Boards that have a switch return true.  Call it anyway.
    bool audioPower(bool on) { (void)on; return false; }

    // ampEnable(): enable or disable the NS4150 class-D amplifier (GPIO20).
    // It is held OFF by a pull-down until something drives this pin.  Turn it
    // on only while playing: enabled with no signal it still draws current and
    // may hiss.
    void ampEnable(bool on);

private:
    bool _begun;
};
