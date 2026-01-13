#include "vfd16x2.h"
#include "esphome/core/log.h"

namespace esphome {
namespace vfd16x2 {

static const char *const TAG = "vfd16x2";

static const uint8_t CMD_INIT0 = 0x70;
static const uint8_t CMD_INIT1 = 0x6c;
static const uint8_t CMD_SET_BRIGHTNESS = 0x53;
static const uint8_t CMD_POS_ROW_0 = 0x90;
static const uint8_t CMD_POS_ROW_1 = 0x10;
static const uint8_t ROWS = 2;
static const uint8_t COLS = 16;

void VFD16X2::setup() {
  ESP_LOGCONFIG(TAG, "Setting up VFD16X2...");

  if (this->n_reset_pin_ != nullptr) {
    this->n_reset_pin_->setup();
    this->n_reset_pin_->digital_write(true);
  }

  this->spi_setup();
  this->enable();
  this->write_byte(CMD_INIT0);
  this->disable();
  delayMicroseconds(50000);
  this->enable();
  this->write_byte(CMD_INIT0);
  this->disable();
  delayMicroseconds(50000);
  this->enable();
  this->write_byte(CMD_INIT1);
  this->disable();
  delayMicroseconds(50000);
  this->enable();
  this->write_byte(CMD_SET_BRIGHTNESS);
  this->write_byte(0x7f);
  this->disable();
}

void VFD16X2::dump_config() {
  ESP_LOGCONFIG(TAG, "VFD16X2:");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  N_RESET Pin: ", this->n_reset_pin_);
}

void VFD16X2::store_custom_char(uint8_t bank, uint8_t location, const uint8_t cols_bitmap[5]) {
  if (bank > 1) {
    ESP_LOGE(TAG, "Custom character bank must be 0 or 1");
    return;
  }
  if (location > 15) {
    ESP_LOGE(TAG, "Custom character location must be between 0 and 15");
    return;
  }
  this->enable();
  this->write_byte(0x20 | (bank << 7));
  this->write_byte(location);
  for (uint8_t i = 0; i < 5; i++) {
    this->write_byte(cols_bitmap[i]);
  }
  this->disable();
}

void VFD16X2::store_custom_char(uint8_t location, const uint8_t cols_bitmap[5]) {
  // For convenience we store it in both CGRAM banks. This way it can be used on both rows.
  for (uint8_t bank = 0; bank < 2; bank++) {
    this->store_custom_char(bank, location, cols_bitmap);
  }
}

void VFD16X2::print(uint8_t column, uint8_t row, uint8_t byte) {
  if (row >= ROWS || column >= COLS) {
    ESP_LOGE(TAG, "Position out of bounds: row %d, column %d", row, column);
    return;
  }
  this->enable();
  this->write_byte(row == 0 ? CMD_POS_ROW_0 : CMD_POS_ROW_1);
  this->write_byte(COLS - 1 - column);
  this->write_byte(byte);
  this->disable();
}

void VFD16X2::print(uint8_t column, uint8_t row, const char *str) {
  if (row >= ROWS || column >= COLS) {
    ESP_LOGE(TAG, "Position out of bounds: row %d, column %d", row, column);
    return;
  }
  this->enable();
  // Write row.
  this->write_byte(row == 0 ? CMD_POS_ROW_0 : CMD_POS_ROW_1);
  // Write the string backwards.
  size_t len = strlen(str);
  // Write column.
  this->write_byte(COLS - 1 - column - (len - 1));

  for (size_t i = 0; i < len; i++) {
    this->write_byte(static_cast<uint8_t>(str[len - 1 - i]));
  }
  this->disable();
}

void VFD16X2::printf(uint8_t column, uint8_t row, const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  char buffer[16 + 1];
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret > 0) {
    this->print(column, row, buffer);
  }
}

void VFD16X2::clear() {
  for (uint8_t row = 0; row < ROWS; row++) {
    this->enable();
    this->write_byte(row == 0 ? CMD_POS_ROW_0 : CMD_POS_ROW_1);
    this->write_byte(0);
    for (uint8_t col = 0; col < COLS; col++) {
      this->write_byte(' ');
    }
    this->disable();
  }
}

void VFD16X2::set_brightness(uint8_t brightness) {
  this->enable();
  this->write_byte(CMD_SET_BRIGHTNESS);
  this->write_byte(brightness);
  this->disable();
}

void VFD16X2::update() {
  // this->clear();
  this->writer_(*this);
}

void VFD16X2::on_off(bool state) {
  this->enable();
  this->write_byte(state ? 0x70 : 0x71);
  this->disable();
}

}  // namespace vfd16x2
}  // namespace esphome
