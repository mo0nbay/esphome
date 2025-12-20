#include "gu128x64d7000.h"
#include "esphome/core/log.h"

namespace esphome {
namespace gu128x64d7000 {

static const char *const TAG = "gu128x64d7000";

void GU128X64D7000::setup() {
  // Init internal buffer (128x64 = 8192 bits = 1024 bytes).
  this->init_internal_(this->get_width_internal() * this->get_height_internal() / 8);

  // Setup pins.
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
  }
  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();
  }

  // Briefly hold reset pin low.
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(120);
  }

  // Reset command.
  this->write_byte(0x1B);
  this->write_byte(0x40);
  delay(50);

  ESP_LOGI(TAG, "Display initialized");
}

void GU128X64D7000::dump_config() {
  ESP_LOGCONFIG(TAG, "GU128X64D7000:");
  ESP_LOGCONFIG(TAG, "  Width: %d, Height: %d", this->get_width_internal(), this->get_height_internal());
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_UPDATE_INTERVAL(this);
  LOG_DISPLAY("", "GU128X64D7000", this);
}

void GU128X64D7000::update() {
  this->do_update_();
  this->write_display_data_();
  ESP_LOGD(TAG, "Display data sent to UART");
}

void HOT GU128X64D7000::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= this->get_width_internal() || y >= this->get_height_internal() || x < 0 || y < 0)
    return;

  // Each byte contains 8 vertical pixels.
  // Position calculated as (x * height + y) / 8.
  const uint32_t pos = (x * this->get_height_internal() + y) / 8;
  const uint8_t bit = 7 - (y % 8);

  if (color.is_on()) {
    this->buffer_[pos] |= (1 << bit);
  } else {
    this->buffer_[pos] &= ~(1 << bit);
  }
}

void GU128X64D7000::write_display_data_() {
  // Write bitmap command.
  const uint8_t write_bitmap_cmd[] = {
      0x1F,
      0x28,
      0x66,
      0x11,
      static_cast<uint8_t>(this->get_width_internal()),       // xlo
      0x00,                                                   // xhi
      static_cast<uint8_t>(this->get_height_internal() / 8),  // ylo
      0x00,                                                   // yhi
      0x01,                                                   // d
  };
  this->write_array(write_bitmap_cmd, sizeof(write_bitmap_cmd));

  // At higher baud rates, the display needs time to process each byte
  // The proper solution would be to monitor the BUSY pin, but adding
  // a small delay between bytes is a janky proof of concept solution.
  const size_t buffer_size = this->get_width_internal() * this->get_height_internal() / 8;
  for (size_t i = 0; i < buffer_size; i++) {
    this->write_byte(this->buffer_[i]);
    this->flush();
    delayMicroseconds(20);  // Max 30us from datasheet.
  }
}

}  // namespace gu128x64d7000
}  // namespace esphome
