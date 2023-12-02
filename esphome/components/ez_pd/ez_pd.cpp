#include "esphome/core/log.h"

#include "ez_pd.h"
#include "pdo.h"
#include "regs.h"

#define MAX_PDOS 7

#define HAS_BITS(v, b, n) (((v) >> (b)) & ((1 << (n)) - 1))
#define HAS_BIT(v, b) (HAS_BITS(v, b, 1))

#define SWAP16(v) ((((v) >> 8) & 0xff) | (((v) & 0xff) << 8))

namespace esphome {
namespace ez_pd {

static const char *TAG = "ez_pd.component";

namespace {

void dump_pd_status(uint32_t pd_status) {
  ESP_LOGI(TAG, "PD status: 0x%08X", pd_status);
  ESP_LOGI(TAG, "- Current port data role: %s", HAS_BIT(pd_status, 6) == 0 ? "UFP" : "DFP");
  ESP_LOGI(TAG, "- Current port power role: %s", HAS_BIT(pd_status, 8) == 0 ? "Sink" : "INVALID");
  ESP_LOGI(TAG, "- Contract state: %s", HAS_BIT(pd_status, 10) == 0 ? "No contract" : "Exists");
  ESP_LOGI(TAG, "- Sink TX: %s", HAS_BIT(pd_status, 14) == 0 ? "Ready" : "Not ready");
  ESP_LOGI(TAG, "- Policy engine state: %s", HAS_BIT(pd_status, 15) == 0 ? "Not ready" : "Ready");
  ESP_LOGI(TAG, "- PD spec revision in BCR: %s", HAS_BIT(pd_status, 16) == 0 ? "2.0" : "3.0");
  ESP_LOGI(TAG, "- PD spec revision in Partner: %s", HAS_BIT(pd_status, 18) == 0 ? "2.0" : "3.0");
}

}  // namespace

void EZPD::setup() {
  ESP_LOGI(TAG, "Setup");

  // Attach interrupt -- active low.
  // this->int_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
  this->int_pin_->pin_mode(gpio::FLAG_INPUT);
  this->int_pin_->setup();

  this->int_pin_->attach_interrupt(ISR, this, gpio::INTERRUPT_FALLING_EDGE);

  uint32_t pd_response;
  if (this->read_register16(REG_PD_RESPONSE, (uint8_t *) &pd_response, sizeof(pd_response))) {
    ESP_LOGE(TAG, "Failed to read PD_RESPONSE");
  } else {
    ESP_LOGI(TAG, "PD response: 0x%08X", pd_response);
  }

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

  delay_microseconds_safe(5000);

  this->get_vbus_voltage_();
  this->get_current_pdo();

  uint32_t pd_status;
  if (this->read_register16(REG_PD_STATUS, (uint8_t *) &pd_status, sizeof(pd_status))) {
    ESP_LOGE(TAG, "Failed to read PD status");
  } else {
    dump_pd_status(pd_status);
  }

  // Enable interrupt events.
  uint32_t event_mask = 0x00000000;
  event_mask |= (1 << 5);  // PD negotiation complete.
  event_mask |= (1 << 6);  // PD control message received.
  event_mask |= (1 << 8);  // Source capabilities received.

  // event_mask |= 0xffff;
  if (this->write_register16(REG_EVENT_MASK, (uint8_t *) &event_mask, sizeof(event_mask))) {
    ESP_LOGE(TAG, "Failed to write event mask");
  }

  // Check for active interrupts (it may have asserted before we set up the int pin).
  // ISR(this);

  // Test: enable 5V.
  // uint8_t select_sink_pdo = 0x01;
  // if (this->write_register16(REG_SELECT_SINK_PDO, &select_sink_pdo, 1)) {
  //   ESP_LOGE(TAG, "Failed to write select sink PDO");
  // }

  // TODO: maybe quickly check current PDO and quickly bail out if it's already compatible.

  // TODO: datasheet says it could trigger a power cycle.
  uint8_t pd_control = 0x0a;  // Send Get_Source_Cap.
  if (this->write_register16(REG_PD_CONTROL, &pd_control, 1)) {
    ESP_LOGE(TAG, "Failed to write PD control");
  }

  // uint16_t reset = 'R' | (1 << 8);
  // if (this->write_register16(REG_RESET, (uint8_t *) &reset, sizeof(reset))) {
  //   ESP_LOGE(TAG, "Failed to write reset");
  // }

  // delay_microseconds_safe(50000);

  // uint8_t interrupt;
  // if (this->read_register16(REG_INTERRUPT, &interrupt, 1)) {
  //   ESP_LOGE(TAG, "Failed to read interrupt");
  // } else {
  //   ESP_LOGI(TAG, "Interrupt: 0x%02X", interrupt);
  // }

  // // Clear interrupt.
  // if (this->write_register16(REG_INTERRUPT, &interrupt, 1)) {
  //   ESP_LOGE(TAG, "Failed to clear interrupt");
  // }
}

void EZPD::loop() {
  // if (this->interrupt_pending_) {
  this->process_interrupt();
  // }

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

PDO EZPD::get_current_pdo() {
  // TODO: uint32_t.
  uint8_t pdo_bytes[4];
  if (this->read_register16(REG_CURRENT_PDO, pdo_bytes, sizeof(pdo_bytes))) {
    ESP_LOGE(TAG, "Failed to read current PDO");
    return PDO{};
  }

  ESP_LOGD(TAG, "PDO: %02X %02X %02X %02X\n", pdo_bytes[0], pdo_bytes[1], pdo_bytes[2], pdo_bytes[3]);

  // Bytes are received in little endian.
  uint32_t pdo_data = pdo_bytes[0] | (pdo_bytes[1] << 8) | (pdo_bytes[2] << 16) | (pdo_bytes[3] << 24);

  PDO pdo = parse_pdo(pdo_data);
  if (!pdo.parsed) {
    ESP_LOGE(TAG, "Failed to parse PDO");
    return PDO{};
  }
  log_pdo(pdo);
  return pdo;
}

// Interrupt callback.
void EZPD::ISR(EZPD *instance) { instance->interrupt_pending_ = true; }

bool EZPD::process_interrupt() {
  // ESP_LOGI(TAG, "Processing interrupt");

  // Read interrupt.
  uint8_t interrupt;
  if (this->read_register16(REG_INTERRUPT, &interrupt, 1)) {
    ESP_LOGE(TAG, "Failed to read interrupt");
    return false;
  }

  if (!interrupt) {
    // ESP_LOGE(TAG, "Interrupt is not actually set");
    return false;
  }
  // ESP_LOGI(TAG, "Interrupt: 0x%02X", interrupt);

  if (interrupt & 0x1) {
    ESP_LOGI(TAG, "Device interrupt");
    uint16_t dev_response;
    if (this->read_register16(REG_DEV_RESPONSE, (uint8_t *) &dev_response, sizeof(dev_response))) {
      ESP_LOGE(TAG, "Failed to read DEV_RESPONSE");
    } else {
      ESP_LOGI(TAG, "Device response: 0x%04X", dev_response);
    }
  }
  if (interrupt & 0x2) {
    ESP_LOGI(TAG, "PD port interrupt");
    uint32_t pd_response;
    if (this->read_register16(REG_PD_RESPONSE, (uint8_t *) &pd_response, sizeof(pd_response))) {
      ESP_LOGE(TAG, "Failed to read PD_RESPONSE");
    } else {
      this->handle_pd_response(pd_response);
    }
  }

  // Clear interrupt.
  interrupt = 0xff;
  if (this->write_register16(REG_INTERRUPT, &interrupt, 1)) {
    ESP_LOGE(TAG, "Failed to clear interrupt");
    return false;
  }

  interrupt_pending_ = false;
  return true;
}

// TODO: break loop.
bool EZPD::handle_pd_response(uint32_t pd_response) {
  ESP_LOGI(TAG, "PD response: 0x%08X", pd_response);
  ESP_LOGI(TAG, "PD response: %s", pd_response & (1 << 7) ? "ASYNC" : "CMD");

  // TODO: longer responses are possible.

  // This seems weird. From the datasheet, we should do & 0x7f, but that doesn't work.
  // Doing & 0xff yields the expected results.
  uint8_t code = pd_response & 0xff;
  uint8_t len = (pd_response >> 8) & 0xff;
  ESP_LOGI(TAG, "PD response code: 0x%02X, len: 0x%02X", code, len);

  if (code != 0x91) {
    ESP_LOGW(TAG, "Nothing to do with response code 0x%02X", code);
    return false;
  }

  PDO curr_pdo = get_current_pdo();
  if (is_pdo_compatible(curr_pdo, this->power_requirement_)) {
    ESP_LOGI(TAG, "Current PDO is compatible, we're done here");
    return false;
  }
  ESP_LOGI(TAG, "Current PDO is not compatible, requesting changes.");

  uint8_t n_pdos = (len - 4) / 4;
  ESP_LOGI(TAG, "Number of PDOS: %d", n_pdos);

  if (n_pdos > MAX_PDOS) {
    ESP_LOGE(TAG, "Too many PDOS");
    return false;
  }

  ESP_LOGI(TAG, "Reading PD response data from memory");

  // TODO: check bounds.
  uint8_t buff[128];
  for (uint8_t i = 0; i < len; i++) {
    if (this->read_register16(SWAP16(REG_READ_MEM_LO + i), &buff[i], 1)) {
      ESP_LOGE(TAG, "Failed to read PD response data");
      return false;
    }

    if (i < 4) {
      ESP_LOGI(TAG, "PD response data at %d: 0x%02X", i, buff[i]);
    }

    // Write to memory write registers.
    if (this->write_register16(SWAP16(REG_WRITE_MEM_LO + i), &buff[i], 1)) {
      ESP_LOGE(TAG, "Failed to write PD response data");
      return false;
    }
  }

  PDO pdos[MAX_PDOS];

  // As per spec, PDOs are ordered by voltage so we select the first one that's compatible.
  int selected_pdo_idx = -1;
  for (uint8_t i = 0; i < n_pdos; i++) {
    uint32_t *pdo_data = (uint32_t *) &buff[i * 4 + 4];
    pdos[i] = parse_pdo(*pdo_data);
    log_pdo(pdos[i]);

    if (selected_pdo_idx == -1 && is_pdo_compatible(pdos[i], this->power_requirement_)) {
      selected_pdo_idx = i;
      break;
    }
  }

  if (selected_pdo_idx == -1) {
    ESP_LOGE(TAG, "No compatible PDO found");
    return false;
  }

  ESP_LOGI(TAG, "Selected PDO:");
  log_pdo(pdos[selected_pdo_idx]);

  if (pdos[selected_pdo_idx].type != PDO::Type::FIXED) {
    ESP_LOGE(TAG, "Selected PDO is not fixed, we don't know how to handle it yet.");
    // TODO: maybe change power requirements and set up the safe 5V PDO.
    return false;
  }

  const uint8_t header[] = {0x50, 0x4B, 0x4E, 0x53};
  for (uint8_t i = 0; i < 4; i++) {
    if (this->write_register16(SWAP16(REG_WRITE_MEM_LO + i), header + i, 1)) {
      ESP_LOGE(TAG, "Failed to write PD response data");
      return false;
    }
  }

  uint8_t select_sink_pdo = 1 << (selected_pdo_idx & 0x7);
  if (this->write_register16(REG_SELECT_SINK_PDO, &select_sink_pdo, 1)) {
    ESP_LOGE(TAG, "Failed to write select sink PDO");
    return false;
  }

  return true;
}

}  // namespace ez_pd
}  // namespace esphome
