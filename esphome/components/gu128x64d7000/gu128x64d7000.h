#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace gu128x64d7000 {

class GU128X64D7000 : public display::DisplayBuffer, public uart::UARTDevice {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;

  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_busy_pin(GPIOPin *busy_pin) { this->busy_pin_ = busy_pin; }

  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_BINARY; }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  int get_height_internal() override { return 64; }
  int get_width_internal() override { return 128; }

  void write_display_data_();

  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
};

}  // namespace gu128x64d7000
}  // namespace esphome
