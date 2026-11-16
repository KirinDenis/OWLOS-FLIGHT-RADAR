// JC8012P4A1C_Board - shared I2C bus and pin map for the Guition JC8012P4A1C.
//
// Copyright (c) 2026 chipguyhere
// MIT License

#include "JC8012P4A1C_Board.h"

JC8012P4A1C_Board::JC8012P4A1C_Board()
    : Wire(::Wire, SDA_PIN, SCL_PIN, "I2C0-GPIO7/8"), _begun(false) {}

bool JC8012P4A1C_Board::begin() {
    if (_begun) return true;

    // ThreadsafeWire on GPIO7/8.  The board's own pull-ups (~1.5k) are strong
    // enough for 400 kHz; the core's internal pull-ups in parallel are harmless.
    if (!Wire.begin()) return false;
    Wire.setClock(400000);

    // GPIO35 has its own 10k pull-up (R40).
    pinMode(BOOT_BUTTON_PIN, INPUT);

    _begun = true;
    return true;
}

bool JC8012P4A1C_Board::hasTouch() {
    begin();
    return Wire.probe(GSL3680_ADDR);
}

bool JC8012P4A1C_Board::hasCodec() {
    begin();
    return Wire.probe(ES8311_ADDR);
}

bool JC8012P4A1C_Board::hasRtc() {
    begin();
    return Wire.probe(RX8025T_ADDR);
}

bool JC8012P4A1C_Board::bootButtonPressed() {
    return digitalRead(BOOT_BUTTON_PIN) == LOW;
}

void JC8012P4A1C_Board::setLed(uint8_t r, uint8_t g, uint8_t b) {
    rgbLedWrite(RGB_LED_PIN, r, g, b);
}

void JC8012P4A1C_Board::ampEnable(bool on) {
    pinMode(AMP_ENABLE_PIN, OUTPUT);
    digitalWrite(AMP_ENABLE_PIN, on ? HIGH : LOW);
}

void JC8012P4A1C_Board::setRadioPower(bool on) {
    if (on) {
        pinMode(C6_RESET_PIN, INPUT);        // R59 pulls CHIP_PU up; the C6 boots
    } else {
        pinMode(C6_RESET_PIN, OUTPUT);
        digitalWrite(C6_RESET_PIN, LOW);     // held in reset
    }
}
