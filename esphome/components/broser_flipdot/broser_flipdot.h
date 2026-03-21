#pragma once

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

namespace esphome {
namespace broser_flipdot {

/// Width of a single flip-dot module in pixels.
static constexpr int MODULE_WIDTH = 28;
/// Height of a single flip-dot module in pixels.
static constexpr int MODULE_HEIGHT = 16;
/// Bytes per module (MODULE_WIDTH * MODULE_HEIGHT / 8).
static constexpr int MODULE_BUFFER_SIZE = MODULE_WIDTH * MODULE_HEIGHT / 8;

class BroserFlipdot : public display::DisplayBuffer, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  void set_num_chips(uint8_t num_chips) { this->num_chips_ = num_chips; }

  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_BINARY; }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  int get_width_internal() override;
  int get_height_internal() override;

  /// Compute the size of the framebuffer in bytes.
  size_t get_buffer_length_();
  /// Send the framebuffer to the flip-dot controller over I2C.
  void write_display_data_();

  uint8_t num_chips_{1};
};

}  // namespace broser_flipdot
}  // namespace esphome
