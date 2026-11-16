// See shim/Arduino.h and shim/Wire.h. (OWLOS Flight Radar fork, JC8012P4A1C port.)
#include "Arduino.h"
#include "Wire.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "esp_clk_tree.h"

ShimSerial Serial;
TwoWire Wire;

extern "C" __attribute__((weak)) i2c_master_bus_handle_t arduino_shim_i2c_bus(int sda, int scl)
{
    static i2c_master_bus_handle_t bus;
    if (bus) return bus;
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = -1;
    cfg.sda_io_num = (gpio_num_t)sda;
    cfg.scl_io_num = (gpio_num_t)scl;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) bus = nullptr;
    return bus;
}

bool TwoWire::begin(int sda, int scl, uint32_t frequency)
{
    if (frequency) _hz = frequency;
    if (!_bus) _bus = arduino_shim_i2c_bus(sda, scl);
    return _bus != nullptr;
}

i2c_master_dev_handle_t TwoWire::dev(uint8_t addr)
{
    addr &= 0x7F;
    if (!_dev[addr] && _bus) {
        i2c_device_config_t d = {};
        d.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        d.device_address = addr;
        d.scl_speed_hz = _hz;
        if (i2c_master_bus_add_device(_bus, &d, &_dev[addr]) != ESP_OK) _dev[addr] = nullptr;
    }
    return _dev[addr];
}

// Arduino's codes: 0 done, 2 address not acknowledged, 4 anything else
uint8_t TwoWire::endTransmission(bool sendStop)
{
    if (!_bus) return 4;
    if (_txLen == 0) return i2c_master_probe(_bus, _addr, _timeout) == ESP_OK ? 0 : 2;
    if (!sendStop) {            // a register address for the read that follows
        _held = true;
        return 0;
    }
    i2c_master_dev_handle_t h = dev(_addr);
    if (!h) return 4;
    esp_err_t e = i2c_master_transmit(h, _tx, _txLen, _timeout);
    _txLen = 0;
    return e == ESP_OK ? 0 : 2;
}

size_t TwoWire::requestFrom(uint8_t addr, size_t n, bool)
{
    _rxLen = _rxPos = 0;
    if (n > sizeof _rx) n = sizeof _rx;
    i2c_master_dev_handle_t h = dev(addr);
    if (!h || !n) { _held = false; _txLen = 0; return 0; }
    esp_err_t e;
    if (_held && _addr == addr && _txLen) e = i2c_master_transmit_receive(h, _tx, _txLen, _rx, n, _timeout);
    else e = i2c_master_receive(h, _rx, n, _timeout);
    _held = false;
    _txLen = 0;
    if (e != ESP_OK) return 0;
    _rxLen = n;
    return n;
}

// one LEDC channel, made on the first call: 8 bits at 1 kHz, the Arduino core's analogWrite, which
// the library was written on. only the backlight calls it
void analogWrite(int pin, int duty)
{
    static int s_pin = -1;
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    if (s_pin != pin) {
        ledc_timer_config_t t = {};
        t.speed_mode = LEDC_LOW_SPEED_MODE;
        t.duty_resolution = LEDC_TIMER_8_BIT;
        t.timer_num = LEDC_TIMER_0;
        t.freq_hz = 1000;
        t.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer_config(&t);
        ledc_channel_config_t c = {};
        c.gpio_num = pin;
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel = LEDC_CHANNEL_0;
        c.timer_sel = LEDC_TIMER_0;
        c.duty = (uint32_t)duty;
        ledc_channel_config(&c);
        s_pin = pin;
        return;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

uint32_t getCpuFrequencyMhz(void)
{
    uint32_t hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &hz);
    return hz / 1000000;
}
