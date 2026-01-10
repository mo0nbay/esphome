#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/gpio.h"
#include "esphome/components/display/display.h"

namespace esphome {
namespace vfd16x2 {

class VFD16X2;

using vfd16x2_writer_t = display::DisplayWriter<VFD16X2>;

class VFD16X2 : public PollingComponent,
                public spi::SPIDevice<spi::BIT_ORDER_LSB_FIRST, spi::CLOCK_POLARITY_HIGH, spi::CLOCK_PHASE_TRAILING,
                                      spi::DATA_RATE_200KHZ> {
 public:
  void set_writer(vfd16x2_writer_t &&writer) { this->writer_ = std::move(writer); }
  void setup() override;
  void dump_config() override;

  void set_n_reset_pin(GPIOPin *n_reset_pin) { this->n_reset_pin_ = n_reset_pin; }

  void print(uint8_t column, uint8_t row, const char *str);
  void printf(uint8_t column, uint8_t row, const char *format, ...) __attribute__((format(printf, 4, 5)));

  void clear();
  void set_brightness(uint8_t brightness);

  void update() override;

 protected:
  GPIOPin *n_reset_pin_{nullptr};
  vfd16x2_writer_t writer_;
};

}  // namespace vfd16x2
}  // namespace esphome
