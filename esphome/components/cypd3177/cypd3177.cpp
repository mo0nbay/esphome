#include "esphome/core/log.h"

#include "cypd3177.h"
#include "pdo.h"
#include "regs.h"

// TODO: Use ESPHome's built-in bit manipulation functions.
#define HAS_BITS(v, b, n) (((v) >> (b)) & ((1 << (n)) - 1))
#define HAS_BIT(v, b) (HAS_BITS(v, b, 1))
#define SWAP16(v) ((((v) >> 8) & 0xff) | (((v) & 0xff) << 8))

namespace esphome {
namespace cypd3177 {

static const char *TAG = "cypd3177.component";

namespace {

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
  ESP_LOGD(TAG, "PD status: 0x%08X", pd_status);
  ESP_LOGD(TAG, "- Current port data role: %s", HAS_BIT(pd_status, 6) == 0 ? "UFP" : "DFP");
  ESP_LOGD(TAG, "- Current port power role: %s", HAS_BIT(pd_status, 8) == 0 ? "Sink" : "INVALID");
  ESP_LOGD(TAG, "- Contract state: %s", HAS_BIT(pd_status, 10) == 0 ? "No contract" : "Exists");
  ESP_LOGD(TAG, "- Sink TX: %s", HAS_BIT(pd_status, 14) == 0 ? "Ready" : "Not ready");
  ESP_LOGD(TAG, "- Policy engine state: %s", HAS_BIT(pd_status, 15) == 0 ? "Not ready" : "Ready");
  ESP_LOGD(TAG, "- PD spec revision in BCR: %s", HAS_BIT(pd_status, 16) == 0 ? "2.0" : "3.0");
  ESP_LOGD(TAG, "- PD spec revision in Partner: %s", HAS_BIT(pd_status, 18) == 0 ? "2.0" : "3.0");
}

void dump_rdo(uint32_t *rdo_data, const PDO *pdos) {
  uint8_t obj_pos = (*rdo_data >> 28) & 0x7;
  uint8_t pdo_idx = obj_pos - 1;

  if (pdos[pdo_idx].type == PDO::Type::FIXED) {
    uint8_t give_back_flag = (*rdo_data >> 27) & 0x1;
    uint8_t cap_mis = (*rdo_data >> 26) & 0x1;
    uint8_t usb_cap = (*rdo_data >> 25) & 0x1;
    uint8_t no_usb_sus = (*rdo_data >> 24) & 0x1;
    uint8_t unchunked_msg_sup = (*rdo_data >> 23) & 0x1;
    uint8_t epr_cap = (*rdo_data >> 22) & 0x1;
    uint16_t curr_ma = (*rdo_data >> 10) & ((1 << 11) - 1);
    uint16_t max_curr_ma = (*rdo_data >> 0) & ((1 << 11) - 1);

    ESP_LOGD(TAG,
             "FIXED Object position: %d, give back flag: %d, cap mismatch: %d, USB cap: %d, no USB suspend: %d, "
             "unchunked msg sup: %d, EPR cap: %d, current: %d mA, max current: %d mA",
             obj_pos, give_back_flag, cap_mis, usb_cap, no_usb_sus, unchunked_msg_sup, epr_cap, curr_ma * 10,
             max_curr_ma * 10);
    return;
  } else if (pdos[pdo_idx].type == PDO::Type::AUGMENTED &&
             pdos[pdo_idx].augmented.type == PDO::Augmented::Type::SPR_PPS) {
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

    ESP_LOGD(TAG,
             "PPS Object position: %d, cap mismatch: %d, USB cap: %d, no USB suspend: %d, unchunked msg sup: %d, "
             "EPR cap: %d, voltage: %d mV, current: %d mA, should be zero: %d %d %d",
             obj_pos, cap_mis, usb_cap, no_usb_sus, unchunked_msg_sup, epr_cap, voltage * 20, current * 50,
             should_be_zero1, should_be_zero2, should_be_zero3);
    return;
  }
  ESP_LOGW(TAG, "Unsupported RDO type: 0x%08X (PDO position %d)", *rdo_data, obj_pos);
}

}  // namespace

void CYPD3177::setup() {
  // Dump config.
  ESP_LOGCONFIG(TAG, "Initializing cypd3177 component");
  ESP_LOGI(TAG, "Power requirement: %d mV, %d mA", this->power_requirement_.voltage_mv,
           this->power_requirement_.current_ma);

  // Attach interrupt -- active low.
  // this->int_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
  this->int_pin_->pin_mode(gpio::FLAG_INPUT);
  this->int_pin_->setup();

  this->int_pin_->attach_interrupt(ISR, this, gpio::INTERRUPT_FALLING_EDGE);

  uint16_t device_id;
  if (this->read_register16(REG_SILICON_ID, (uint8_t *) &device_id, 2, false)) {
    ESP_LOGE(TAG, "Failed to read device id");
  } else {
    ESP_LOGV(TAG, "Device id: 0x%04X", device_id);
  }

  // // Enable interrupt events.
  uint32_t event_mask = 0x00000000;
  // Set first 11 bits.
  event_mask |= ((1 << 12) - 1);
  event_mask |= (1 << 29);
  event_mask |= (1 << 30);

  if (this->write_register16(REG_EVENT_MASK, (uint8_t *) &event_mask, sizeof(event_mask))) {
    // TODO: fatal.
    ESP_LOGE(TAG, "Failed to write event mask. Aborting.");
    state_ = State::FAILURE;
    return;
  }

  // TODO: make interrupts work instead of polling.
  // ISR(this);

  // Get capabilities. This will cause the PD contract to be renegotiated. We wait for a "negotiation
  // complete" event to request the actual power we want.
  uint8_t pd_control = 0x0a;  // Send Get_Source_Cap.
  if (this->write_register16(REG_PD_CONTROL, &pd_control, 1)) {
    // TODO: fatal.
    ESP_LOGE(TAG, "Failed to write PD control to get capabilities. Aborting.");
    state_ = State::FAILURE;
    return;
  }

  state_ = State::REQUESTED_CAPS;
}

void CYPD3177::loop() {
  if (state_ == State::FAILURE) {
    return;
  }
  this->process_interrupt();
}

void CYPD3177::dump_config() { ESP_LOGCONFIG(TAG, "cypd3177 component"); }

float CYPD3177::get_vbus_voltage_() {
  uint8_t bus_voltage_dv;
  if (this->read_register16(REG_BUS_VOLTAGE, &bus_voltage_dv, 1)) {
    ESP_LOGE(TAG, "Failed to read bus voltage");
    return 0.0f;
  }
  float bus_voltage = bus_voltage_dv * 0.1f;
  ESP_LOGD(TAG, "Bus voltage: %.2f", bus_voltage);
  return bus_voltage;
}

PDO CYPD3177::get_current_pdo() {
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
  return pdo;
}

// Interrupt callback.
void CYPD3177::ISR(CYPD3177 *instance) { instance->interrupt_pending_ = true; }

bool CYPD3177::process_interrupt() {
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
  ESP_LOGD(TAG, "Interrupt: 0x%02X", interrupt);

  if (interrupt & 0x1) {
    ESP_LOGD(TAG, "Device interrupt");
    uint16_t dev_response;
    if (this->read_register16(REG_DEV_RESPONSE, (uint8_t *) &dev_response, sizeof(dev_response))) {
      ESP_LOGE(TAG, "Failed to read DEV_RESPONSE");
    } else {
      ESP_LOGD(TAG, "Device response: 0x%04X", dev_response);
    }
  }
  if (interrupt & 0x2) {
    ESP_LOGD(TAG, "PD port interrupt");
    uint32_t pd_response;
    if (this->read_register16(REG_PD_RESPONSE, (uint8_t *) &pd_response, sizeof(pd_response))) {
      ESP_LOGE(TAG, "Failed to read PD_RESPONSE");
    } else {
      this->handle_pd_response(pd_response);
    }
  }

  // Clear interrupt.
  if (this->write_register16(REG_INTERRUPT, &interrupt, 1)) {
    ESP_LOGE(TAG, "Failed to clear interrupt");
    return false;
  }

  interrupt_pending_ = false;
  return true;
}

bool CYPD3177::handle_event_status(uint32_t event_status) { return true; }

bool CYPD3177::handle_pd_response(uint32_t pd_response) {
  ESP_LOGD(TAG, "PD response: 0x%08X -- %s", pd_response, pd_response & (1 << 7) ? "ASYNC" : "CMD");

  // This seems weird. From the datasheet, we should do & 0x7f, but that doesn't work as some response codes are larger
  // than 0x7f. The response type is likely included in the code, making it 0xff.
  ResponseCode code = static_cast<ResponseCode>(pd_response & 0xff);
  // TODO: longer responses are possible.
  uint8_t len = (pd_response >> 8) & 0xff;
  ESP_LOGD(TAG, "PD response code: 0x%02X, len: 0x%02X", code, len);

  switch (code) {
    case ResponseCode::NO_RESPONSE:
      ESP_LOGE(TAG, "No response");
      return true;
    case ResponseCode::SUCCESS:
      ESP_LOGD(TAG, "Success");
      return true;
    case ResponseCode::INVALID_CMD:
      ESP_LOGD(TAG, "Invalid command");
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
      ESP_LOGD(TAG, "PS ready");
      return true;
    case ResponseCode::PD_NEGOTIATION_COMPLETE:
      return handle_pd_negotiation_complete(len);
    case ResponseCode::ACCEPT_MSG_RECEIVED:
      ESP_LOGD(TAG, "Accept message received");
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
      ESP_LOGD(TAG, "Unhandled response code to %s: 0x%02X (full: 0x%08X)", pd_response & (0x1 << 7) ? "ASYNC" : "CMD",
               code, pd_response);
      return false;
  }
}

bool CYPD3177::handle_source_capabilities(uint8_t len) {
  ESP_LOGI(TAG, "Source capabilities received. Current state: %d", static_cast<int>(state_));

  uint8_t n_pdos = (len - 4) / 4;
  ESP_LOGD(TAG, "Number of PDOS: %d", n_pdos);

  if (n_pdos > CYPD3177_MAX_PDOS) {
    ESP_LOGE(TAG, "Too many PDOS");
    return false;
  }

  ESP_LOGD(TAG, "Reading PD response data from memory");

  uint8_t buff[4 * CYPD3177_MAX_PDOS + 4];
  memset(buff, 0, sizeof(buff));
  for (uint8_t i = 0; i < len; i++) {
    if (this->read_register16(SWAP16(REG_READ_MEM_LO + i), &buff[i], 1)) {
      ESP_LOGE(TAG, "Failed to read PD response data");
      return false;
    }
  }

  // Parse and store PDOs.
  for (uint8_t i = 0; i < n_pdos; i++) {
    uint32_t *pdo_data = (uint32_t *) &buff[i * 4 + 4];
    pdos_[i] = parse_pdo(*pdo_data);
    log_pdo(pdos_[i]);
  }

  ESP_LOGD(TAG, "Writing PD response data to memory. Current state: %d (req_caps: %d)", static_cast<int>(state_),
           static_cast<int>(State::REQUESTED_CAPS));

  // As per datasheet, to get ready for a power negotiation, we need to write the PD response data to memory.
  for (uint8_t i = 0; i < len; i++) {
    if (this->write_register16(SWAP16(REG_WRITE_MEM_LO + i), &buff[i], 1)) {
      ESP_LOGE(TAG, "Failed to write PD response data");
      return false;
    }
  }

  // As per datasheet, we write the required header "SNKP" to memory.
  const uint8_t header[] = {0x50, 0x4B, 0x4E, 0x53};
  for (uint8_t i = 0; i < sizeof(header); i++) {
    if (this->write_register16(SWAP16(REG_WRITE_MEM_LO + i), header + i, 1)) {
      ESP_LOGE(TAG, "Failed to write PD response data");
      return false;
    }
  }

  // Can we find a suitable PDO?
  selected_pdo_idx_ = -1;
  for (int idx = 0; idx < n_pdos; idx++) {
    const PDO *pdo = &pdos_[idx];
    if (pdo->type == PDO::Type::FIXED && is_pdo_compatible(*pdo, this->power_requirement_)) {
      selected_pdo_idx_ = idx;
      break;
    }
  }

  if (selected_pdo_idx_ == -1) {
    // TODO: fatal.
    ESP_LOGE(TAG, "No suitable fixed PDO found");
    state_ = State::FAILURE;
    return false;
  }

  ESP_LOGI(TAG, "Now we're cooking! Found suitable PDO:");
  log_pdo(pdos_[selected_pdo_idx_]);

  return true;
}

// We can probably just use the simpler SELECT_SINK_PDO register for this, but while I implemented this lower level
// REQUEST to learn how it works while I unsuccessfully tried to get a PPS request to work. Well, it did work, and I
// verified with a logic analyzer that the request is sent correctly and the source responds with both an ACCEPT and
// READY message. But CYPD3177 freaks out and issues a hard request upon the ACCEPT or RDY response for a PPS RDO :(.
bool CYPD3177::request_selected_fixed_pdo() {
  if (selected_pdo_idx_ == -1) {
    ESP_LOGE(TAG, "No suitable selected PDO -- aborting.");
    state_ = State::FAILURE;
    return false;
  }

  ESP_LOGD(TAG, "Requesting fixed PDO with index %d", selected_pdo_idx_);

  uint32_t request = 0;

  // Object position -- index + 1.
  request |= (((selected_pdo_idx_ + 1) & 0x07) << 28);

  // USB communications capability.
  request |= (0x1 << 25);

  // No USB suspend.
  // request |= (0x1 << 24);

  // Unchunked message supported.
  request |= (0x1 << 23);

  uint32_t current_10ma = 150;
  request |= (current_10ma << 10);
  request |= current_10ma;

  ESP_LOGV(TAG, "Will send RDO: 0x%08X", request);
  dump_rdo(&request, pdos_);

  // Invert.
  request = byteswap(request);
  if (this->write_register16(REG_REQUEST, (uint8_t *) &request, sizeof(request))) {
    // TODO: fatal.
    ESP_LOGE(TAG, "Failed to write PD request");
    return false;
  }

  state_ = State::REQUESTED_PDO;
  return true;
}

bool CYPD3177::handle_pd_negotiation_complete(uint8_t len) {
  ESP_LOGI(TAG, "PD negotiation complete");
  uint8_t buff[8];
  if (len > sizeof(buff)) {
    ESP_LOGE(TAG, "PD negotiation complete -- response is too long");
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
    ESP_LOGW(TAG, "Reason for pd negotiation failure: 0x%02X", buff[0] >> 2 & 0x3);
  }

  uint32_t *rdo_data = (uint32_t *) &buff[4];
  ESP_LOGV(TAG, "Used request (RDO):");
  dump_rdo(rdo_data, pdos_);

  const PDO curr_pdo = get_current_pdo();
  ESP_LOGV(TAG, "Currently active power delivery object (PDO):");
  log_pdo(curr_pdo);

  if (curr_pdo.type != PDO::Type::FIXED) {
    ESP_LOGE(TAG, "Currently active PDO is not fixed -- something is very fishy. Aborting.");
    state_ = State::FAILURE;
    return false;
  }

  if (state_ == State::REQUESTED_CAPS) {
    return request_selected_fixed_pdo();
  } else if (state_ == State::REQUESTED_PDO) {
    int available_power = curr_pdo.fixed.max_current_ma * curr_pdo.fixed.voltage_mv / (1000 * 1000);
    ESP_LOGI(TAG, "Done. We got the power we wanted! Responsibly enjoy them %d Watts!!", available_power);
    state_ = State::READY;
  }

  return true;
}

}  // namespace cypd3177
}  // namespace esphome
