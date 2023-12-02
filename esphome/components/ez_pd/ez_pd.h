#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace ez_pd {

class EZPD : public i2c::I2CDevice, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_interrupt_pin(InternalGPIOPin *int_pin) { this->int_pin_ = int_pin; }

 private:
  float get_vbus_voltage_();
  void get_current_pdo();
  // Interrupt pin.
  InternalGPIOPin *int_pin_{nullptr};
};

}  // namespace ez_pd
}  // namespace esphome
