#include "broser_flipdot.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace broser_flipdot {

static const char *const TAG = "broser_flipdot";

constexpr uint8_t kI2CAddrMod = 0x20;
constexpr uint8_t kI2CAddrCol = 0x21;
constexpr uint8_t kI2CAddrRow = 0x22;

constexpr int kFlipTimeUs = 550;

void BroserFlipdot::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Broser Flip-Dot display...");

  // Probe the I2C device.
  auto err = this->write(nullptr, 0);
  if (err != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "I2C communication failed!");
    this->mark_failed();
    return;
  }

  // Allocate the internal framebuffer (1 bit per pixel).
  this->init_internal_(this->get_buffer_length_());

  // Clear the display.
  this->fill(Color::BLACK);
  this->write_display_data_();
}

void BroserFlipdot::update() {
  // Let ESPHome's display engine draw into the buffer (lambda / pages).
  this->do_update_();
  // Push the buffer to the hardware.
  this->write_display_data_();
}

void BroserFlipdot::dump_config() {
  ESP_LOGCONFIG(TAG, "Broser Flip-Dot:");
  ESP_LOGCONFIG(TAG, "  Num chips: %u", this->num_chips_);
  ESP_LOGCONFIG(TAG, "  Width: %dpx", this->get_width_internal());
  ESP_LOGCONFIG(TAG, "  Height: %dpx", this->get_height_internal());
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
}

void HOT BroserFlipdot::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= this->get_width_internal() || y < 0 || y >= this->get_height_internal())
    return;

  // The framebuffer is organized in column-major byte order within each module:
  // Each byte represents 8 vertical pixels (one column-slice).
  // Byte index: x * (MODULE_HEIGHT / 8) + (y / 8)
  // Bit index within that byte: y % 8
  int module_index = x / MODULE_WIDTH;
  int local_x = x % MODULE_WIDTH;

  int byte_offset = module_index * MODULE_BUFFER_SIZE + local_x * (MODULE_HEIGHT / 8) + (y / 8);
  uint8_t bit_mask = 1 << (y % 8);

  if (color.is_on()) {
    this->buffer_[byte_offset] |= bit_mask;
  } else {
    this->buffer_[byte_offset] &= ~bit_mask;
  }
}

int BroserFlipdot::get_width_internal() { return MODULE_WIDTH * this->num_chips_; }

int BroserFlipdot::get_height_internal() { return MODULE_HEIGHT; }

size_t BroserFlipdot::get_buffer_length_() { return static_cast<size_t>(this->num_chips_) * MODULE_BUFFER_SIZE; }

void BroserFlipdot::write_display_data_() {
  // Send the entire framebuffer over I2C.
  // The controller expects the raw pixel data written directly.
  // uint32_t length = this->get_buffer_length_();

  // // I2C has limited transaction sizes on some platforms; send in chunks.
  // static constexpr uint8_t CHUNK_SIZE = 32;
  // for (uint32_t offset = 0; offset < length; offset += CHUNK_SIZE) {
  //   uint8_t chunk_len = std::min(static_cast<uint32_t>(CHUNK_SIZE), length - offset);
  //   this->write(this->buffer_ + offset, chunk_len);
  // }

  // static bool on = 1;

  // // Select col 0.
  // uint8_t col = on << 3 | 0 | on << 6;
  // this->address_ = kI2CAddrCol;
  // this->write(&col, 1);

  // // Select row 0.
  // uint8_t row = 0 | 1 << 4 | !on << 5;
  // this->address_ = kI2CAddrRow;
  // this->write(&row, 1);

  // // Enable module.
  // uint8_t mod = 0xff;
  // this->address_ = kI2CAddrMod;
  // this->write(&mod, 1);

  // // Sleep.
  // delay_microseconds_safe(kFlipTimeUs);

  // // Disable row drivers.
  // row = 0;
  // this->address_ = kI2CAddrRow;
  // this->write(&row, 1);

  // on = !on;

  // Dot is set when col is - and row is +.
  // Dot is cleared when col is + and row is -.

  // Write 1 dot.
  uint8_t col = 0, row = 0, mod = 0;

  ESP_LOGI(TAG, "Flipping a single dot...");

  // Disable module.
  mod = 0x00;
  this->address_ = kI2CAddrMod;
  this->write(&mod, 1);

  // Set col 0 to -.
  // B1 B0 A2 A1 A0
  //  0  0  0  0  1
  // DAT = 0
  col = 1 << 5 | 0 << 6;
  this->address_ = kI2CAddrCol;
  this->write(&col, 1);

  // Set row 0 to +.
  // 0A: R+;
  // B1 B0 A2 A1 A0
  //  0  0  0  0  1
  row = 1 << 1 | 1 << 4 | 1 << 5;
  this->address_ = kI2CAddrRow;
  this->write(&row, 1);

  // Enable module.
  mod = 0xff;
  this->address_ = kI2CAddrMod;
  this->write(&mod, 1);

  // Sleep.
  delay_microseconds_safe(kFlipTimeUs);

  // Disable row drivers.
  row = 0;
  this->address_ = kI2CAddrRow;
  this->write(&row, 1);

  delay_microseconds_safe(1e6);

  ESP_LOGI(TAG, "Flipping the dot back...");

  // Now the opposite.

  // Disable module.
  mod = 0x00;
  this->address_ = kI2CAddrMod;
  this->write(&mod, 1);

  // Set col 0 to +.
  // B1 B0 A2 A1 A0
  //  0  0  0  0  1
  // DAT = 1
  col = 1 << 5 | 1 << 6;
  this->address_ = kI2CAddrCol;
  this->write(&col, 1);

  // Set row 0 to -.
  // B1 B0 A2 A1 A0
  //  0  1  0  0  1
  // DAT = 0
  row = 1 << 1 | 1 << 3 | 0 << 4 | 1 << 5;
  this->address_ = kI2CAddrRow;
  this->write(&row, 1);

  // Enable module.
  mod = 0xff;
  this->address_ = kI2CAddrMod;
  this->write(&mod, 1);

  // Sleep.
  delay_microseconds_safe(kFlipTimeUs);

  // Disable row drivers.
  row = 0;
  this->address_ = kI2CAddrRow;
  this->write(&row, 1);

  delay_microseconds_safe(1e6);
}

}  // namespace broser_flipdot
}  // namespace esphome
