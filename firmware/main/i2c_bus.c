#include "i2c_bus.h"
#ifdef PIN_I2C_SDA
#include "driver/gpio.h"
#include "esp_err.h"

static i2c_master_bus_handle_t s_bus;

static void bus_up(void)
{
    if (s_bus) return;
    i2c_master_bus_config_t cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
    /* the same nap that isolates every other pin: the bus driver has just set these two */
    gpio_hold_dis(PIN_I2C_SDA);
    gpio_hold_dis(PIN_I2C_SCL);
    gpio_sleep_sel_dis(PIN_I2C_SDA);
    gpio_sleep_sel_dis(PIN_I2C_SCL);
}

i2c_master_dev_handle_t i2c_dev_hz(uint8_t addr, uint32_t hz)
{
    bus_up();
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = hz,
    };
    i2c_master_dev_handle_t h = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &h));
    return h;
}

#ifndef I2C_BUS_HZ
#define I2C_BUS_HZ 400000
#endif

i2c_master_dev_handle_t i2c_dev(uint8_t addr)
{
    return i2c_dev_hz(addr, I2C_BUS_HZ);
}

int i2c_probe(uint8_t addr)
{
    bus_up();
    return i2c_master_probe(s_bus, addr, 50) == ESP_OK;
}

void i2c_bus_free(void)
{
    if (!s_bus) return;
    i2c_del_master_bus(s_bus);
    s_bus = NULL;
}
#endif
