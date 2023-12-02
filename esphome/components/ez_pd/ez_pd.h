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
  // Interrupt pin.
  InternalGPIOPin *int_pin_{nullptr};
  bool interrupt_pending_{false};

  // Methods.
  void get_current_pdo();
  bool process_interrupt();

  bool handle_pd_response(uint32_t pd_response);

  // Handles interrupts.
  static void ISR(EZPD *instance);
};

}  // namespace ez_pd
}  // namespace esphome
