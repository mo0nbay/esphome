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

bool done = false;

enum class ResponseCode : uint8_t {
  NO_RESPONSE = 0x00,
  SUCCESS = 0x02,
  INVALID_CMD = 0x09,
  NOT_SUPPORTED = 0x0A,
  TRANSACTION_FAILED = 0x0C,
  PD_CMD_FAILED = 0x0D,
  PD_NEGOTIATION_COMPLETE = 0x86,
  PS_READY = 0x8A,
  ACCEPT_MSG_RECEIVED = 0x8C,
  REJECT_MSG_RECEIVED = 0x8D,
  SOURCE_CAPABILITIES = 0x91,
  TYPE_C_ERROR_RECOVERY = 0xA1,
};

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

void dump_rdo(uint32_t *rdo_data) {
  uint8_t obj_pos = (*rdo_data >> 28) & 0x7;

  if (obj_pos <= 5) {
    uint8_t give_back_flag = (*rdo_data >> 27) & 0x1;
    uint8_t cap_mis = (*rdo_data >> 26) & 0x1;
    uint8_t usb_cap = (*rdo_data >> 25) & 0x1;
    uint8_t no_usb_sus = (*rdo_data >> 24) & 0x1;
    uint8_t unchunked_msg_sup = (*rdo_data >> 23) & 0x1;
    uint8_t epr_cap = (*rdo_data >> 22) & 0x1;
    uint16_t curr_ma = (*rdo_data >> 10) & ((1 << 11) - 1);
    uint16_t max_curr_ma = (*rdo_data >> 0) & ((1 << 11) - 1);

    ESP_LOGI(TAG,
             "FIXED Object position: %d, give back flag: %d, cap mismatch: %d, USB cap: %d, no USB suspend: %d, "
             "unchunked msg sup: %d, EPR cap: %d, current: %d mA, max current: %d mA",
             obj_pos, give_back_flag, cap_mis, usb_cap, no_usb_sus, unchunked_msg_sup, epr_cap, curr_ma * 10,
             max_curr_ma * 10);
  } else {
    // Assume PPS for testing.
    uint8_t should_be_zero1 = (*rdo_data >> 27) & 0x1;
    uint8_t cap_mis = (*rdo_data >> 26) & 0x1;
    uint8_t usb_cap = (*rdo_data >> 25) & 0x1;
    uint8_t no_usb_sus = (*rdo_data >> 24) & 0x1;
    uint8_t unchunked_msg_sup = (*rdo_data >> 23) & 0x1;
    uint8_t epr_cap = (*rdo_data >> 22) & 0x1;
    uint8_t should_be_zero2 = (*rdo_data >> 21) & 0x1;
    uint16_t voltage = (*rdo_data >> 9) & ((1 << 13) - 1);
    uint8_t should_be_zero3 = (*rdo_data >> 7) & ((1 << 3) - 1);
    uint16_t current = (*rdo_data >> 0) & ((1 << 8) - 1);

    ESP_LOGI(TAG,
             "PPS Object position: %d, cap mismatch: %d, USB cap: %d, no USB suspend: %d, unchunked msg sup: %d, "
             "EPR cap: %d, voltage: %d mV, current: %d mA, should be zero: %d %d %d",
             obj_pos, cap_mis, usb_cap, no_usb_sus, unchunked_msg_sup, epr_cap, voltage * 20, current * 50,
             should_be_zero1, should_be_zero2, should_be_zero3);
  }
}

}  // namespace

void EZPD::setup() {
  ESP_LOGI(TAG, "Power requirement: %d mV, %d mA", this->power_requirement_.voltage_mv,
           this->power_requirement_.current_ma);

  // Attach interrupt -- active low.
  // this->int_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
  this->int_pin_->pin_mode(gpio::FLAG_INPUT);
  this->int_pin_->setup();

  this->int_pin_->attach_interrupt(ISR, this, gpio::INTERRUPT_FALLING_EDGE);

  // uint8_t device_mode;
  // if (this->read_register16(REG_DEVICE_MODE, &device_mode, 1, true)) {
  //   ESP_LOGE(TAG, "Failed to read device mode");
  // } else {
  //   ESP_LOGI(TAG, "Device mode: 0x%02X", device_mode);
  // }

  // uint16_t device_id;
  // if (this->read_register16(REG_SILICON_ID, (uint8_t *) &device_id, 2, false)) {
  //   ESP_LOGE(TAG, "Failed to read device id");
  // } else {
  //   ESP_LOGI(TAG, "Device id: 0x%04X", device_id);
  // }

  // // Enable interrupt events.
  uint32_t event_mask = 0x00000000;
  // event_mask |= (1 << 5);   // PD negotiation complete.
  // event_mask |= (1 << 6);   // PD control message received.
  // event_mask |= (1 << 8);   // Source capabilities received.
  // event_mask |= (1 << 11);  // Errors.
  // event_mask |= (1 << 17);  // Extended data message received.

  // Unset bits 12...28.
  // event_mask &= ~((1 << 17) - 1) << 12;

  // Set first 11 bits.
  event_mask |= ((1 << 12) - 1);
  event_mask |= (1 << 29);
  event_mask |= (1 << 30);

  if (this->write_register16(REG_EVENT_MASK, (uint8_t *) &event_mask, sizeof(event_mask))) {
    ESP_LOGE(TAG, "Failed to write event mask");
  }

  // // TODO: make interrupts work instead of polling.
  // // ISR(this);

  // // TODO: maybe quickly check current PDO and quickly bail out if it's already compatible.

  // // Get capabilities.
  // // TODO: datasheet says it could trigger a power cycle.
  // uint8_t pd_control = 0x0a;  // Send Get_Source_Cap.
  // if (this->write_register16(REG_PD_CONTROL, &pd_control, 1)) {
  //   ESP_LOGE(TAG, "Failed to write PD control");
  // }

  // uint32_t rdo;
  // if (this->read_register16(REG_CURRENT_RDO, (uint8_t *) &rdo, sizeof(rdo))) {
  //   ESP_LOGE(TAG, "Failed to read RDO");
  // } else {
  //   ESP_LOGI(TAG, "Initial RDO: 0x%08X", rdo);
  //   dump_rdo(&rdo);
  // }
}

void EZPD::loop() {
  uint32_t event_status;
  if (this->read_register16(REG_EVENT_STATUS, (uint8_t *) &event_status, sizeof(event_status))) {
    ESP_LOGE(TAG, "Failed to read event status");
  }

  if (event_status > 0) {
    ESP_LOGI(TAG, "Event status: 0x%08X", event_status);

    handle_event_status(event_status);

    if (this->write_register16(REG_EVENT_STATUS, (uint8_t *) &event_status, sizeof(event_status))) {
      ESP_LOGE(TAG, "Failed to clear event status");
    }
  }

  // if (this->interrupt_pending_) {
  this->process_interrupt();
  // }

  // ESP_LOGI(TAG, "State: %d", static_cast<int>(state_));
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
  ESP_LOGI(TAG, "Interrupt: 0x%02X", interrupt);

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
  // interrupt = 0xff;
  if (this->write_register16(REG_INTERRUPT, &interrupt, 1)) {
    ESP_LOGE(TAG, "Failed to clear interrupt");
    return false;
  }

  interrupt_pending_ = false;
  return true;
}

bool EZPD::handle_event_status(uint32_t event_status) { return true; }

// TODO: break loop.
bool EZPD::handle_pd_response(uint32_t pd_response) {
  ESP_LOGI(TAG, "PD response: 0x%08X", pd_response);
  ESP_LOGI(TAG, "PD response: %s", pd_response & (1 << 7) ? "ASYNC" : "CMD");

  // This seems weird. From the datasheet, we should do & 0x7f, but that doesn't work.
  // Doing & 0xff yields the expected results.
  ResponseCode code = static_cast<ResponseCode>(pd_response & 0xff);
  // TODO: longer responses are possible.
  uint8_t len = (pd_response >> 8) & 0xff;
  // ESP_LOGI(TAG, "PD response code: 0x%02X, len: 0x%02X", code, len);

  switch (code) {
    case ResponseCode::NO_RESPONSE:
      ESP_LOGE(TAG, "No response");
      return true;
    case ResponseCode::SUCCESS:
      ESP_LOGI(TAG, "Success");
      if (state_ == State::INITIALIZING) {
        // Get PD status.
        uint32_t pd_status;
        if (this->read_register16(REG_PD_STATUS, (uint8_t *) &pd_status, sizeof(pd_status))) {
          ESP_LOGE(TAG, "Failed to read PD status");
          return false;
        }
        ESP_LOGI(TAG, "PD status: 0x%08X", pd_status);
        dump_pd_status(pd_status);

        // Request source capabilities.
        uint8_t pd_control = 0x0a;  // Send Get_Source_Cap.
        if (this->write_register16(REG_PD_CONTROL, &pd_control, 1)) {
          ESP_LOGE(TAG, "Failed to write PD control");
        }
        state_ = State::REQUESTED_CAPS;
      } else if (state_ == State::UPDATING_PDOS) {
      }
      return true;
    case ResponseCode::INVALID_CMD:
      ESP_LOGI(TAG, "Invalid command");
      return true;
    case ResponseCode::NOT_SUPPORTED:
      ESP_LOGE(TAG, "Not supported");
      return true;
    case ResponseCode::TRANSACTION_FAILED:
      ESP_LOGE(TAG, "Transaction failed");
      return true;
    case ResponseCode::PD_CMD_FAILED:
      ESP_LOGE(TAG, "PD command failed");
      return true;
    case ResponseCode::PS_READY:
      ESP_LOGI(TAG, "PS ready");
      return true;
    case ResponseCode::PD_NEGOTIATION_COMPLETE:
      return handle_pd_negotiation_complete(len);
    case ResponseCode::ACCEPT_MSG_RECEIVED:
      ESP_LOGI(TAG, "Accept message received");
      return true;
    case ResponseCode::REJECT_MSG_RECEIVED:
      ESP_LOGE(TAG, "Reject message received");
      return true;
    case ResponseCode::SOURCE_CAPABILITIES:
      return handle_source_capabilities(len);
    case ResponseCode::TYPE_C_ERROR_RECOVERY:
      ESP_LOGE(TAG, "Type C error recovery");
      return true;
    default:
      ESP_LOGI(TAG, "Unhandled response code to %s: 0x%02X (full: 0x%08X)", pd_response & (0x1 << 7) ? "ASYNC" : "CMD",
               code, pd_response);
      return false;
  }
}

bool EZPD::handle_source_capabilities(uint8_t len) {
  ESP_LOGI(TAG, "Source capabilities received. Current state: %d", static_cast<int>(state_));

  // REMOVE
  return true;

  // PDO curr_pdo = get_current_pdo();
  // if (is_pdo_compatible(curr_pdo, this->power_requirement_)) {
  //   ESP_LOGI(TAG, "Current PDO is compatible, we're done here");
  //   return false;
  // }
  // ESP_LOGI(TAG, "Current PDO is not compatible, requesting changes.");

  uint8_t n_pdos = (len - 4) / 4;
  ESP_LOGI(TAG, "Number of PDOS: %d", n_pdos);

  if (n_pdos > MAX_PDOS) {
    ESP_LOGE(TAG, "Too many PDOS");
    return false;
  }

  ESP_LOGI(TAG, "Reading PD response data from memory");

  // TODO: check bounds.
  uint8_t buff[4 * MAX_PDOS + 4];
  memset(buff, 0, sizeof(buff));
  for (uint8_t i = 0; i < len; i++) {
    if (this->read_register16(SWAP16(REG_READ_MEM_LO + i), &buff[i], 1)) {
      ESP_LOGE(TAG, "Failed to read PD response data");
      return false;
    }
  }

  // uint8_t selected_pdo_idx = 1;
  // uint8_t select_sink_pdo = 1 << (selected_pdo_idx & 0x7);
  if (state_ == State::REQUESTED_CAPS) {
    ESP_LOGI(TAG, "State::REQUESTED_CAPS -- Writing PD response data to memory. Current state: %d (req_caps: %d)",
             static_cast<int>(state_), static_cast<int>(State::REQUESTED_CAPS));

    // Write 7 bytes to memory.
    for (uint8_t i = 0; i < len; i++) {
      if (this->write_register16(SWAP16(REG_WRITE_MEM_LO + i), &buff[i], 1)) {
        ESP_LOGE(TAG, "Failed to write PD response data");
        return false;
      }
    }

    const uint8_t header[] = {0x50, 0x4B, 0x4E, 0x53};
    for (uint8_t i = 0; i < sizeof(header); i++) {
      if (this->write_register16(SWAP16(REG_WRITE_MEM_LO + i), header + i, 1)) {
        ESP_LOGE(TAG, "Failed to write PD response data");
        return false;
      }
    }

    uint8_t select_sink_pdo = 0x2;
    if (this->write_register16(REG_SELECT_SINK_PDO, &select_sink_pdo, 1)) {
      ESP_LOGE(TAG, "Failed to write select sink PDO");
      return false;
    }
    state_ = State::REQUESTED_PDO1;
  }
  return true;

  // Update PDO list.
  // ESP_LOGI(TAG, "Updating PDO list");
  // uint8_t select_sink_pdo = (1 << (n_pdos + 1)) - 1;
  // uint8_t select_sink_pdo = (1 << (n_pdos + 1)) - 1;
  // uint8_t select_sink_pdo = (1 << 2);
  // if (this->write_register16(REG_SELECT_SINK_PDO, &select_sink_pdo, 1)) {
  //   ESP_LOGE(TAG, "Failed to write select sink PDO");
  //   return false;
  // }

  // state_ = State::UPDATING_PDOS;
  // return true;

  // PDO pdos[MAX_PDOS];

  // // As per spec, PDOs are ordered by voltage so we select the first one that's compatible.
  // int selected_pdo_idx = -1;
  // for (uint8_t i = 0; i < n_pdos; i++) {
  //   uint32_t *pdo_data = (uint32_t *) &buff[i * 4 + 4];
  //   pdos[i] = parse_pdo(*pdo_data);
  //   log_pdo(pdos[i]);

  //   if (selected_pdo_idx == -1 && is_pdo_compatible(pdos[i], this->power_requirement_)) {
  //     selected_pdo_idx = i;
  //     break;
  //   }
  // }

  // REMOVE.
  // return false;

  // if (selected_pdo_idx == -1) {
  //   ESP_LOGE(TAG, "No compatible PDO found");
  //   return false;
  // }

  // ESP_LOGI(TAG, "Selected PDO:");
  // log_pdo(pdos[selected_pdo_idx]);

  // if (pdos[selected_pdo_idx].type == PDO::Type::AUGMENTED &&
  //     pdos[selected_pdo_idx].augmented.type == PDO::Augmented::Type::SPR_PPS) {
  //   // TODO: make this work.
  //   // return select_pps_pdo(selected_pdo_idx);
  // } else if (pdos[selected_pdo_idx].type == PDO::Type::FIXED) {
  //   uint8_t select_sink_pdo = 1 << (selected_pdo_idx & 0x7);
  //   if (this->write_register16(REG_SELECT_SINK_PDO, &select_sink_pdo, 1)) {
  //     ESP_LOGE(TAG, "Failed to write select sink PDO");
  //     return false;
  //   }
  //   return true;
  // }

  // ESP_LOGE(TAG, "No suitable PDOs found.");
  // return false;
}

bool EZPD::select_pps_pdo(uint8_t pdo_idx) {
  ESP_LOGI(TAG, "Selecting PPS PDO with index %d", pdo_idx);

  uint32_t request = 0;

  // Object position.
  request |= (((pdo_idx + 1) & 0x07) << 28);

  // No USB suspend.
  // request |= (0x1 << 24);

  // USB communications capability.
  request |= (0x1 << 25);

  // Unchunked message supported.
  request |= (0x1 << 23);

  // Output voltage in 20 mV units.
  uint32_t voltage_mv = (this->power_requirement_.voltage_mv) / 20;
  // Set last two bits to zero
  request |= (voltage_mv << 9);

  // Output current in 50 mA units.
  uint16_t current_ma = (this->power_requirement_.current_ma) / 50;
  request |= (current_ma & 0x7f);

  ESP_LOGI(TAG, "Will send request: 0x%08X", request);
  dump_rdo(&request);

  // Get PD status.
  uint32_t pd_status;
  if (this->read_register16(REG_PD_STATUS, (uint8_t *) &pd_status, sizeof(pd_status))) {
    ESP_LOGE(TAG, "Failed to read PD status");
    return false;
  }
  ESP_LOGI(TAG, "PD status before: 0x%08X", pd_status);
  dump_pd_status(pd_status);

  // Invert.
  request = byteswap(request);
  if (this->write_register16(REG_REQUEST, (uint8_t *) &request, sizeof(request))) {
    ESP_LOGE(TAG, "Failed to write PD response");
    return false;
  }

  // // Test 2 - use DM_CONTROL directly with hand crafted header.

  // uint16_t header = 0x00;
  // header |= (1 << 12);
  // header |= 0x40;
  // header |= (0x2 << 6);
  // // Message type - request.
  // header |= (0x82 & 0x1f);

  // header = byteswap(header);

  // // Copy header.
  // for (uint8_t i = 0; i < 2; i++) {
  //   if (this->write_register16(SWAP16(REG_WRITE_MEM_LO + i), (uint8_t *) &header + i, 1)) {
  //     ESP_LOGE(TAG, "Failed to write PD response data");
  //     return false;
  //   }
  // }

  // // Copy request to memory.
  // uint8_t *request_bytes = (uint8_t *) &request;
  // for (uint8_t i = 0; i < 4; i++) {
  //   if (this->write_register16(SWAP16(REG_WRITE_MEM_LO + sizeof(header) + i), &request_bytes[i], 1)) {
  //     ESP_LOGE(TAG, "Failed to write PD response data");
  //     return false;
  //   }
  // }

  // uint16_t dm_control = 0x0000 | (1 << 8);
  // // dm_control = SWAP16(dm_control);
  // if (this->write_register16(REG_DM_CONTROL, (uint8_t *) &dm_control, sizeof(dm_control))) {
  //   ESP_LOGE(TAG, "Failed to write DM control");
  //   return false;
  // }

  return true;
}

bool EZPD::handle_pd_negotiation_complete(uint8_t len) {
  ESP_LOGI(TAG, "PD negotiation complete");
  uint8_t buff[8];
  if (len > sizeof(buff)) {
    ESP_LOGE(TAG, "PD negotiation complete response is too long");
    return false;
  }

  // Read from memory registers.
  for (uint8_t i = 0; i < len; i++) {
    if (this->read_register16(SWAP16(REG_READ_MEM_LO + i), &buff[i], 1)) {
      ESP_LOGE(TAG, "Failed to read PD negotiation complete response");
      return false;
    }
  }

  if ((buff[0] & 0x1) == 0) {
    ESP_LOGI(TAG, "Reason for failure: 0x%02X", buff[0] >> 2 & 0x3);
  }

  uint32_t *rdo_data = (uint32_t *) &buff[4];
  ESP_LOGI(TAG, "%s, %s. Sent RDO: 0x%08X", buff[0] & 0x1 ? "Success" : "Failure",
           buff[0] & 0x2 ? "Cap. mismatch" : "No cap. mismatch", *rdo_data);
  dump_rdo(rdo_data);

  PDO curr_pdo = get_current_pdo();
  ESP_LOGI(TAG, "Current PDO:");
  log_pdo(curr_pdo);

  // if (state_ == State::REQUESTED_PDO1) {
  //   ESP_LOGI(TAG, "Will request PPS PDO");
  //   // Request PDO.
  //   select_pps_pdo(5);
  //   state_ = State::REQUESTED_PDO;
  // }

  // if (state_ == State::REQUESTED_CAPS) {
  //   // Will update PDOs.
  // }

  return true;
}

}  // namespace ez_pd
}  // namespace esphome
