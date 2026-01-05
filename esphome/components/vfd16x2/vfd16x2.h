#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/gpio.h"

namespace esphome {
namespace vfd16x2 {

class VFD16X2 : public Component,
                public spi::SPIDevice<spi::BIT_ORDER_LSB_FIRST, spi::CLOCK_POLARITY_HIGH, spi::CLOCK_PHASE_TRAILING,
                                      spi::DATA_RATE_200KHZ> {
 public:
  void setup() override;
  void dump_config() override;

  void set_n_reset_pin(GPIOPin *n_reset_pin) { this->n_reset_pin_ = n_reset_pin; }
  void print(uint8_t column, uint8_t row, const char *str);
  void clear();
  void set_brightness(uint8_t brightness);

 protected:
  GPIOPin *n_reset_pin_{nullptr};
};

}  // namespace vfd16x2
}  // namespace esphome
