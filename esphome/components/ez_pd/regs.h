#pragma once

#include <cstdint>

namespace esphome {
namespace ez_pd {

// Returns 0x95 (datasheet says 0x92).
constexpr uint16_t REG_DEVICE_MODE = 0x0000;
// Returns 0x2004 (datasheet says 0x11b0).
constexpr uint16_t REG_SILICON_ID = 0x0200;
constexpr uint16_t REG_BUS_VOLTAGE = 0x0D10;
constexpr uint16_t REG_CURRENT_PDO = 0x1010;

}  // namespace ez_pd
}  // namespace esphome