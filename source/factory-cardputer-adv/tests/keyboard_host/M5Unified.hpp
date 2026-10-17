#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#define IRAM_ATTR
#define ESP_INTR_FLAG_IRAM 1
using gpio_num_t = int;
using esp_err_t = int;
constexpr int GPIO_INTR_ANYEDGE = 3;
constexpr int GPIO_MODE_INPUT = 1;
constexpr int GPIO_PULLDOWN_DISABLE = 0;
constexpr int GPIO_PULLUP_DISABLE = 0;
struct gpio_config_t {
    std::uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
};
int gpio_config(const gpio_config_t*);
int gpio_install_isr_service(int);
int gpio_isr_handler_add(gpio_num_t, void (*)(void*), void*);

namespace keyboard_host {
bool read(std::uint8_t reg, std::uint8_t* data, std::size_t length);
bool write(std::uint8_t reg, std::uint8_t value);
}
namespace m5 {
class I2C_Class {
public:
    bool writeRegister8(std::uint8_t, std::uint8_t reg, std::uint8_t data, std::uint32_t) const
    { return keyboard_host::write(reg, data); }
    std::uint8_t readRegister8(std::uint8_t, std::uint8_t reg, std::uint32_t) const
    {
        std::uint8_t value = 0;
        // Same public behavior as actual M5 I2C_Class::readRegister8 value_or(0).
        return keyboard_host::read(reg, &value, 1) ? value : 0;
    }
    bool readRegister(std::uint8_t, std::uint8_t reg, std::uint8_t* data, std::size_t length, std::uint32_t) const
    { return keyboard_host::read(reg, data, length); }
};
inline I2C_Class In_I2C;
class I2C_Device {
public:
    I2C_Device(std::uint8_t address, std::uint32_t frequency, I2C_Class* bus)
      : _i2c(bus), _freq(frequency), _addr(address) {}
    bool writeRegister8(std::uint8_t reg, std::uint8_t data) const
    { return _i2c->writeRegister8(_addr, reg, data, _freq); }
    std::uint8_t readRegister8(std::uint8_t reg) const
    { return _i2c->readRegister8(_addr, reg, _freq); }
    bool readRegister(std::uint8_t reg, std::uint8_t* data, std::size_t length) const
    { return _i2c->readRegister(_addr, reg, data, length, _freq); }
protected:
    I2C_Class* _i2c;
    std::uint32_t _freq;
    std::uint8_t _addr;
};
}
