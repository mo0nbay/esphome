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

}  // namespace vfd16x2
}  // namespace esphome
