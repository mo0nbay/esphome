#pragma once

namespace esphome {
namespace ez_pd {

struct PDO {
  enum class Type {
    FIXED = 0b00,
    BATTERY = 0b01,
    VARIABLE = 0b10,
    AUGMENTED = 0b11,
  };

  struct Fixed {
    uint16_t voltage_mv;
    uint16_t max_current_ma;
  };

  struct Variable {
    uint16_t max_voltage_mv;
    uint16_t min_voltage_mv;
    uint16_t max_power_mw;
  };

  Type type;
  bool parsed = false;
  union {
    Fixed fixed;
    Variable variable;
  };
};

PDO parse_pdo(uint32_t data);

void log_pdo(const PDO &pdo);

}  // namespace ez_pd
}  // namespace esphome
