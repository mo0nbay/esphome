#include "esphome/core/log.h"

#include "ez_pd.h"
#include "pdo.h"
#include "regs.h"

namespace esphome {
namespace ez_pd {

static const char *TAG = "ez_pd.component";

namespace {}  // namespace

void EZPD::setup() {}

void EZPD::loop() {
  uint8_t device_mode;
  if (this->read_register16(REG_DEVICE_MODE, &device_mode, 1, true)) {
    ESP_LOGE(TAG, "Failed to read device mode");
  } else {
    ESP_LOGI(TAG, "Device mode: 0x%02X", device_mode);
  }

  uint16_t device_id;
  if (this->read_register16(REG_SILICON_ID, (uint8_t *) &device_id, 2, false)) {
    ESP_LOGE(TAG, "Failed to read device id");
  } else {
    ESP_LOGI(TAG, "Device id: 0x%04X", device_id);
  }

  this->get_vbus_voltage_();

  this->get_current_pdo();
}

void EZPD::dump_config() { ESP_LOGCONFIG(TAG, "ez_pd component"); }

float EZPD::get_vbus_voltage_() {
  uint8_t bus_voltage_dv;
  if (this->read_register16(REG_BUS_VOLTAGE, &bus_voltage_dv, 1)) {
    ESP_LOGE(TAG, "Failed to read bus voltage");
    return 0.0f;
  }
  float bus_voltage = bus_voltage_dv * 0.1f;
  ESP_LOGD(TAG, "Bus voltage: %.2f", bus_voltage);
  return bus_voltage;
}

// For PDO representation, see table 6.7 in the USB_PD specs.
void EZPD::get_current_pdo() {
  uint8_t pdo_bytes[4];
  if (this->read_register16(REG_CURRENT_PDO, pdo_bytes, sizeof(pdo_bytes))) {
    ESP_LOGE(TAG, "Failed to read current PDO");
    return;
  }

  ESP_LOGD(TAG, "PDO: %02X %02X %02X %02X\n", pdo_bytes[0], pdo_bytes[1], pdo_bytes[2], pdo_bytes[3]);

  uint32_t pdo_data = pdo_bytes[0] | (pdo_bytes[1] << 8) | (pdo_bytes[2] << 16) | (pdo_bytes[3] << 24);

  PDO pdo = parse_pdo(pdo_data);
  if (!pdo.parsed) {
    ESP_LOGE(TAG, "Failed to parse PDO");
    return;
  }

  log_pdo(pdo);

  // switch (pdo_bytes[0] & 0x3) {
  //   case 0b00:
  //     ESP_LOGI(TAG, "PDO: Fixed supply");
  //     break;
  //   case 0b01:
  //     ESP_LOGI(TAG, "PDO: Battery supply");
  //     break;
  //   case 0b10:
  //     ESP_LOGI(TAG, "PDO: Variable supply");
  //     break;
  //   case 0b11:
  //     ESP_LOGI(TAG, "PDO: Augmented supply");
  //     break;
  // }
}

}  // namespace ez_pd
}  // namespace esphome
