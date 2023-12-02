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

constexpr uint16_t REG_INTERRUPT = 0x0600;
constexpr uint16_t REG_EVENT_MASK = 0x2410;
constexpr uint16_t REG_PD_CONTROL = 0x0610;
constexpr uint16_t REG_PD_STATUS = 0x0810;
constexpr uint16_t REG_PD_RESPONSE = 0x0014;
constexpr uint16_t REG_DEV_RESPONSE = 0x7e00;

constexpr uint16_t REG_RESET = 0x0800;

// 0x1404 to 0x150b.
constexpr uint16_t REG_READ_MEM_LO = 0x1404;
// 0x1800 to 0x19ff.
constexpr uint16_t REG_WRITE_MEM_LO = 0x1800;

}  // namespace ez_pd
}  // namespace esphome