#include "brose_flipdot.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace brose_flipdot {

static const char *const TAG = "brose_flipdot";

constexpr uint8_t kI2CAddrMod = 0x20;
constexpr uint8_t kI2CAddrCol = 0x21;
constexpr uint8_t kI2CAddrRow = 0x22;

constexpr int kFlipTimeUs = 550;

void BroseFlipdot::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Brose Flip-Dot display...");

  // Probe the I2C device.
  auto err = this->write(nullptr, 0);
  if (err != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "I2C communication failed!");
    this->mark_failed();
    return;
  }

  // Allocate the internal framebuffer (1 bit per pixel).
  this->init_internal_(this->get_buffer_length_());

  // Allocate the previous-frame buffer for dirty-pixel tracking.
  // Initialized to 0xff (inverted of cleared display) so the first frame
  // forces a full refresh.
  size_t buf_len = this->get_buffer_length_();
  // Never freed, so direct `new` should be fine.
  this->prev_buffer_ = new uint8_t[buf_len];  // NOLINT
  memset(this->prev_buffer_, 0xff, buf_len);

  // Clear the display.
  this->fill(Color::BLACK);
  this->write_display_data_();
}

void BroseFlipdot::update() {
  // Let ESPHome's display engine draw into the buffer (lambda / pages).
  this->do_update_();
  // Push the buffer to the hardware.
  this->write_display_data_();
}

void BroseFlipdot::dump_config() {
  ESP_LOGCONFIG(TAG, "Brose Flip-Dot:");
  ESP_LOGCONFIG(TAG, "  Num chips: %u", this->num_chips_);
  ESP_LOGCONFIG(TAG, "  Width: %dpx", this->get_width_internal());
  ESP_LOGCONFIG(TAG, "  Height: %dpx", this->get_height_internal());
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
}

void HOT BroseFlipdot::draw_absolute_pixel_internal(int x, int y, Color color) {
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

int BroseFlipdot::get_width_internal() { return MODULE_WIDTH * this->num_chips_; }

int BroseFlipdot::get_height_internal() { return MODULE_HEIGHT; }

size_t BroseFlipdot::get_buffer_length_() { return static_cast<size_t>(this->num_chips_) * MODULE_BUFFER_SIZE; }

struct FlipCommands {
  uint8_t col;
  uint8_t row;
};

void compute_flip_commands(int x, int y, bool on_off, FlipCommands &cmds) {
  // To set a dot, we set the column to - and row to +.
  // To reset a dot, we set the column to - and row to -.

  // Bits.
  // xy => B1 B0 A2 A1 A0
  //  i =>  4  3  2  1  0

  const uint8_t col_b = (x % MODULE_WIDTH) / 7;
  const uint8_t col_a = (x % 7) + 1;

  const uint8_t y_local = y % 14;
  const uint8_t row_b = y_local / 7;
  const uint8_t row_a = (y_local % 7) + 1;

  cmds.col = ((col_b >> 1) & 1) << 1     // COL_B1.
             | ((col_b >> 0) & 1) << 2   // COL_B0.
             | ((col_a >> 2) & 1) << 3   // COL_A2.
             | ((col_a >> 1) & 1) << 4   // COL_A1.
             | ((col_a >> 0) & 1) << 5   // COL_A0.
             | (!on_off) << 6            // COLDAT.
             | ((row_a >> 2) & 1) << 7;  // ROW_A2.

  // E.g.: To turn on a pixel in row 0-13:
  //  Row set: 0A - 0G, 2A - 2G (total: 14 rows)
  //  Row reset: 1A - 1G, 3A - 3G (total: 14 rows)
  // The B1 Address controls maps to which half (0-7 or 8-15) of rows are affected
  // The B0 Address bit maps to whether it's 0A/1A or 2A/3A (set vs reset).

  // let ymod = y % 14;
  // A0 = (ymod >> 0) & 1
  // A1 = (ymod >> 1) & 1
  // A2 = (ymod >> 2) & 1
  // B0 = !on_off (if it's on, no shift)
  // B1 = ymod > 7
  // ROW_0-13_DAT = on_off
  // ROW_0-13_EN = y < 14
  // ROW_14-27_DAT = on_off
  // ROW_14-27_EN = y >= 14
  cmds.row = ((row_a >> 1) & 1) << 0    // ROW_A1.
             | ((row_a >> 0) & 1) << 1  // ROW_A0.
             | (row_b & 1) << 2         // ROW_B1.
             | (!on_off) << 3           // ROW_B0.
             | on_off << 4              // ROW_0-13_DAT.
             | (y < 14) << 5            // ROW_0-13_EN.
             | on_off << 6              // ROW_14-27_DAT.
             | (y >= 14) << 7;          // ROW_14-27_EN.
}

void BroseFlipdot::send_flip_command(int x, int y, bool on) {
  if (x < 0 || x >= MODULE_WIDTH * this->num_chips_ || y < 0 || y >= MODULE_HEIGHT) {
    ESP_LOGE(TAG, "Invalid coordinates for flip command: (%d, %d)", x, y);
    return;
  }

  FlipCommands cmds;
  compute_flip_commands(x, y, on, cmds);

  // Disable module.
  this->address_ = kI2CAddrMod;
  uint8_t mod_cmd = 0x00;
  this->write(&mod_cmd, 1);

  // Set column.
  this->address_ = kI2CAddrCol;
  this->write(&cmds.col, 1);

  // Set row.
  this->address_ = kI2CAddrRow;
  this->write(&cmds.row, 1);

  // Enable module.
  this->address_ = kI2CAddrMod;
  uint8_t mod_enable_cmd = 0xff;
  this->write(&mod_enable_cmd, 1);

  // Sleep for the flip time.
  delay_microseconds_safe(kFlipTimeUs);

  // Disable row drivers.
  uint8_t row_disable_cmd = 0x00;
  this->address_ = kI2CAddrRow;
  this->write(&row_disable_cmd, 1);
}

void BroseFlipdot::write_display_data_() {
  size_t buf_len = this->get_buffer_length_();
  int flipped = 0;

  for (size_t i = 0; i < buf_len; i++) {
    uint8_t diff = this->buffer_[i] ^ this->prev_buffer_[i];
    if (diff == 0)
      continue;

    // Map byte index -> (x, y_base).
    int module_index = i / MODULE_BUFFER_SIZE;
    int local_offset = i % MODULE_BUFFER_SIZE;
    int local_x = local_offset / (MODULE_HEIGHT / 8);
    int y_byte = local_offset % (MODULE_HEIGHT / 8);
    int x = module_index * MODULE_WIDTH + local_x;
    int y_base = y_byte * 8;

    // Flip only the bits that changed.
    for (int bit = 0; bit < 8; bit++) {
      if (!(diff & (1 << bit)))
        continue;
      bool is_on = (this->buffer_[i] >> bit) & 1;
      this->send_flip_command(x, y_base + bit, is_on);
      flipped++;
    }
  }

  // Snapshot current frame as the new baseline.
  // Note that we could probably do a pointer swap here instead of memcpy, but that requires some introspection into the
  // Display base class.
  memcpy(this->prev_buffer_, this->buffer_, buf_len);
  ESP_LOGD(TAG, "Flipped %d pixels", flipped);
}

}  // namespace brose_flipdot
}  // namespace esphome
