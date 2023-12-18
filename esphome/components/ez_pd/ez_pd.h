#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/i2c/i2c.h"

#include "esphome/components/ez_pd/pdo.h"

namespace esphome {
namespace ez_pd {

enum class State {
  INITIALIZING = 0x0,
  REQUESTED_CAPS,
  UPDATING_PDOS,
  REQUESTED_PDO1,
  REQUESTED_PDO,
  READY,
  FAILURE,
};

class EZPD : public i2c::I2CDevice, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_interrupt_pin(InternalGPIOPin *int_pin) { this->int_pin_ = int_pin; }

  void set_power_requirement(uint16_t voltage_mv, uint16_t current_ma) {
    this->power_requirement_.voltage_mv = voltage_mv;
    this->power_requirement_.current_ma = current_ma;
  }

 private:
  State state_{State::INITIALIZING};

  PowerRequirement power_requirement_;

  float get_vbus_voltage_();
  // Interrupt pin.
  InternalGPIOPin *int_pin_{nullptr};
  bool interrupt_pending_{false};

  // Methods.
  PDO get_current_pdo();
  bool process_interrupt();

  bool handle_event_status(uint32_t event_status);
  bool handle_pd_response(uint32_t pd_response);
  bool handle_source_capabilities(uint8_t len);
  bool handle_pd_negotiation_complete(uint8_t len);
  bool select_pps_pdo(uint8_t pdo_idx);

  // Handles interrupts.
  static void ISR(EZPD *instance);
};

}  // namespace ez_pd
}  // namespace esphome
