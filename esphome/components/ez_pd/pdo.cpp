#include "esphome/core/log.h"
#include "pdo.h"

#include <optional>

static const char *TAG = "ez_pd.pdo";

namespace esphome {
namespace ez_pd {

// Example of a Variable PDO:
// 2C C1 03 00
// 00101100 11000001 00000011 00000000
// 31:30 00 - PDO type: fixed
// 29: 1
// 28: 0
// 27 1
// 26 1
// 25 0
// 24 0
// 23 1
// 22 1
// 21: 20 00
// 19:10 0001000000  3.2V
// 9:0   1100000000  38 Does not make sense

// Let's try the reverse:

// Example of a fixed PDO (note that it's reversed)
// 00000000 00000011 11000001 00101100
// 00 fixed
// 0
// 0
// 0
// 0
// 0
// 0
// 0
// 0
// 00 peak current
// 0011110000 * 0.05 = 12V
// 0100101100 * 0.01 = 3.0 A

PDO parse_pdo(uint32_t data) {
  PDO pdo;
  pdo.type = static_cast<PDO::Type>(data >> 30);
  if (pdo.type == PDO::Type::FIXED) {
    pdo.fixed.voltage_mv = ((data >> 10) & 0x3FF) * 50;
    pdo.fixed.max_current_ma = (data & 0x3FF) * 10;
    pdo.parsed = true;
    return pdo;
  } else if (pdo.type == PDO::Type::VARIABLE) {
    pdo.variable.max_voltage_mv = ((data >> 20) & 0x3FF) * 50;
    pdo.variable.min_voltage_mv = ((data >> 10) & 0x3FF) * 50;
    pdo.variable.max_power_mw = (data & 0x3FF) * 250;
    pdo.parsed = true;
    return pdo;
  }
  ESP_LOGW(TAG, "Unsupported PDO type: %d", pdo.type);
  return pdo;
}

void log_pdo(const PDO &pdo) {
  if (pdo.type == PDO::Type::FIXED) {
    ESP_LOGI(TAG, "Fixed PDO: %d mV, %d mA", pdo.fixed.voltage_mv, pdo.fixed.max_current_ma);
  } else if (pdo.type == PDO::Type::VARIABLE) {
    ESP_LOGI(TAG, "Variable PDO: %d-%d mV, %d mW", pdo.variable.min_voltage_mv, pdo.variable.max_voltage_mv,
             pdo.variable.max_power_mw);
  } else {
    ESP_LOGW(TAG, "Unsupported PDO type");
  }
}

}  // namespace ez_pd
}  // namespace esphome
