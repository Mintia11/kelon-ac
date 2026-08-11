#include "kelon_ac.h"
#include "ir_tx_rtl8720cf.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace kelon_ac {

static const char *const TAG = "kelon_ac.climate";

// Timing constants, copied verbatim from IRremoteESP8266's ir_Kelon.cpp.
static const int32_t KELON_HDR_MARK = 9000;
static const int32_t KELON_HDR_SPACE = 4600;
static const int32_t KELON_BIT_MARK = 560;
static const int32_t KELON_ONE_SPACE = 1680;
static const int32_t KELON_ZERO_SPACE = 600;
// IRremoteESP8266's kDefaultMessageGap is 100000us; Kelon's gap is 2x that.
// NOTE: not directly visible in the files you shared - verify against your
// build of IRremoteESP8266 (`kDefaultMessageGap` in IRsend.h) if transmissions
// aren't accepted reliably.
static const int32_t KELON_GAP = 200000;
static const int32_t KELON168_FOOTER_SPACE = 8000;

static const uint8_t KELON_MODE_HEAT = 0;
static const uint8_t KELON_MODE_SMART = 1;
static const uint8_t KELON_MODE_COOL = 2;
static const uint8_t KELON_MODE_DRY = 3;
static const uint8_t KELON_MODE_FAN = 4;
static const uint8_t KELON_MIN_TEMP = 18;
static const uint8_t KELON_MAX_TEMP = 32;

static const uint8_t KELON168_MODE_HEAT = 0;
static const uint8_t KELON168_MODE_AUTO = 1;
static const uint8_t KELON168_MODE_COOL = 2;
static const uint8_t KELON168_MODE_DRY = 3;
static const uint8_t KELON168_MODE_FAN = 4;
static const uint8_t KELON168_MIN_TEMP = 16;
static const uint8_t KELON168_MAX_TEMP = 32;

static const uint8_t KELON168_CMD_POWER = 0x01;
static const uint8_t KELON168_CMD_TEMP = 0x02;
static const uint8_t KELON168_CMD_MODE = 0x06;
static const uint8_t KELON168_CMD_SWING = 0x07;
static const uint8_t KELON168_CMD_FAN = 0x11;

void KelonAcClimate::setup() {
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = 24;
  this->fan_mode = climate::CLIMATE_FAN_AUTO;
  this->swing_mode = climate::CLIMATE_SWING_OFF;
  this->publish_state();
}

void KelonAcClimate::dump_config() {
  ESP_LOGCONFIG(TAG, "Kelon AC:");
  ESP_LOGCONFIG(TAG, "  Protocol: %s", this->protocol_ == PROTOCOL_KELON168 ? "KELON168" : "KELON");
}

climate::ClimateTraits KelonAcClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(false);
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT_COOL,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
  });
  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });
  traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL,
  });
  traits.set_visual_min_temperature(16);
  traits.set_visual_max_temperature(32);
  traits.set_visual_temperature_step(1);
  return traits;
}

void KelonAcClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value())
    this->target_temperature = *call.get_target_temperature();
  if (call.get_fan_mode().has_value())
    this->fan_mode = *call.get_fan_mode();
  if (call.get_swing_mode().has_value())
    this->swing_mode = *call.get_swing_mode();

  this->transmit_state();
  this->publish_state();
}

void KelonAcClimate::append_bytes_(std::vector<int32_t> &out, const uint8_t *bytes, uint8_t nbytes,
                                    bool with_header, int32_t footer_space) {
  if (with_header) {
    out.push_back(KELON_HDR_MARK);
    out.push_back(-KELON_HDR_SPACE);
  }
  for (uint8_t i = 0; i < nbytes; i++) {
    for (uint8_t bit = 0; bit < 8; bit++) {  // LSB first per byte (sendGeneric(..., false))
      bool on = bytes[i] & (1 << bit);
      out.push_back(KELON_BIT_MARK);
      out.push_back(on ? -KELON_ONE_SPACE : -KELON_ZERO_SPACE);
    }
  }
  out.push_back(KELON_BIT_MARK);
  out.push_back(-footer_space);
}

uint8_t KelonAcClimate::xor_bytes_(const uint8_t *bytes, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < len; i++) sum ^= bytes[i];
  return sum;
}

void KelonAcClimate::transmit_state() {
  if (this->protocol_ == PROTOCOL_KELON168) {
    this->transmit_kelon168_();
  } else {
    this->transmit_kelon_();
  }
  this->last_mode_ = this->mode;
  this->last_target_temperature_ = this->target_temperature;
  this->last_fan_mode_ = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  this->last_swing_mode_ = this->swing_mode;
  this->first_transmit_ = false;
}

// ---------------- Kelon (48-bit) ----------------
// Ported from IRKelonAc::send()/setMode()/setTemp()/setFan()/stateReset().

void KelonAcClimate::transmit_kelon_() {
  this->kelon_state_.raw = 0;
  this->kelon_state_.preamble[0] = 0b10000011;
  this->kelon_state_.preamble[1] = 0b00000110;

  // Kelon only supports *toggling* power - fire the toggle bit only when
  // on/off actually changed since the last transmit.
  bool want_on = this->mode != climate::CLIMATE_MODE_OFF;
  bool was_on = this->last_mode_ != climate::CLIMATE_MODE_OFF;
  this->kelon_state_.PowerToggle = this->first_transmit_ ? false : (want_on != was_on);

  uint8_t mode;
  switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT:
      mode = KELON_MODE_HEAT;
      break;
    case climate::CLIMATE_MODE_DRY:
      mode = KELON_MODE_DRY;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      mode = KELON_MODE_FAN;
      break;
    case climate::CLIMATE_MODE_COOL:
      mode = KELON_MODE_COOL;
      break;
    default:
      mode = KELON_MODE_SMART;  // AUTO/HEAT_COOL -> Kelon's "smart" mode
      break;
  }
  this->kelon_state_.Mode = mode;
  this->kelon_state_.SmartModeEnabled = (mode == KELON_MODE_SMART);

  uint8_t temp = (uint8_t) clamp<float>(this->target_temperature, KELON_MIN_TEMP, KELON_MAX_TEMP);
  this->kelon_state_.Temperature = temp - KELON_MIN_TEMP;

  uint8_t fan;
  switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
    case climate::CLIMATE_FAN_LOW:
      fan = 1;
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      fan = 2;
      break;
    case climate::CLIMATE_FAN_HIGH:
      fan = 3;
      break;
    default:
      fan = 0;  // Auto
      break;
  }
  // Kelon fan speeds are reversed in hardware (0:Auto,1:Max,2:Med,3:Min) -
  // this mirrors the inversion in the original IRKelonAc::setFan().
  this->kelon_state_.Fan = ((int16_t) fan - 4) * -1 % 4;

  this->kelon_state_.SwingVToggle =
      this->first_transmit_ ? false : (this->swing_mode != this->last_swing_mode_);

  uint8_t bytes[6];
  for (int i = 0; i < 6; i++) bytes[i] = (this->kelon_state_.raw >> (i * 8)) & 0xFF;

  std::vector<int32_t> timings;
  this->append_bytes_(timings, bytes, 6, true, KELON_GAP);
  ir_send_raw(timings.data(), timings.size());
}

// ---------------- Kelon168 (21-byte, 3 sections) ----------------
// Ported from IRKelon168Ac::send()/setMode()/setTemp()/setFan()/setSwing()/
// setPower()/checksum()/stateReset(), and IRsend::sendKelon168()'s
// three-section framing.

void KelonAcClimate::transmit_kelon168_() {
  if (this->first_transmit_) {
    for (uint8_t i = 2; i < KELON168_STATE_LENGTH; i++) this->kelon168_state_.raw[i] = 0;
    this->kelon168_state_.raw[0] = 0x83;
    this->kelon168_state_.raw[1] = 0x06;
    this->kelon168_state_.raw[6] = 0x80;
    this->kelon168_state_.Model1 = 0b1000;
    this->kelon168_state_.Model2 = 0b001;
  }

  bool want_on = this->mode != climate::CLIMATE_MODE_OFF;
  bool changed_power = this->first_transmit_ || (want_on != (this->last_mode_ != climate::CLIMATE_MODE_OFF));
  bool changed_temp = this->first_transmit_ || (this->target_temperature != this->last_target_temperature_);
  bool changed_fan = this->first_transmit_ ||
                      (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO) != this->last_fan_mode_);
  bool changed_swing = this->first_transmit_ || (this->swing_mode != this->last_swing_mode_);
  bool changed_mode = this->first_transmit_ || (this->mode != this->last_mode_);

  this->kelon168_state_.Power = true;
  this->kelon168_state_.On = want_on;

  uint8_t mode;
  switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT:
      mode = KELON168_MODE_HEAT;
      break;
    case climate::CLIMATE_MODE_DRY:
      mode = KELON168_MODE_DRY;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      mode = KELON168_MODE_FAN;
      break;
    case climate::CLIMATE_MODE_COOL:
      mode = KELON168_MODE_COOL;
      break;
    default:
      mode = KELON168_MODE_AUTO;
      break;
  }
  this->kelon168_state_.Mode = mode;

  uint8_t temp = (uint8_t) clamp<float>(this->target_temperature, KELON168_MIN_TEMP, KELON168_MAX_TEMP);
  this->kelon168_state_.Temp = temp - KELON168_MIN_TEMP;

  // Collapses HA's 4 fan speeds onto Kelon168's Low/Medium/Max levels.
  // (Kelon168 also has distinct Min and High levels - extend this switch
  // if you need finer control, e.g. via a separate `select` entity.)
  uint8_t fan_bits, fan2_bit;
  switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
    case climate::CLIMATE_FAN_LOW:
      fan_bits = 0b11;
      fan2_bit = 1;
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      fan_bits = 0b10;
      fan2_bit = 0;
      break;
    case climate::CLIMATE_FAN_HIGH:
      fan_bits = 0b01;
      fan2_bit = 0;
      break;
    default:
      fan_bits = 0;
      fan2_bit = 0;
      break;
  }
  this->kelon168_state_.Fan = fan_bits;
  this->kelon168_state_.Fan2 = fan2_bit;

  bool swing_on = this->swing_mode == climate::CLIMATE_SWING_VERTICAL;
  this->kelon168_state_.Swing1 = swing_on;
  this->kelon168_state_.Swing2 = swing_on;

  // The real remote sets exactly one "Cmd" byte per button press. Since a HA
  // climate entity can change several fields in a single call, approximate
  // that by prioritizing whichever field changed since the last transmit.
  uint8_t cmd = KELON168_CMD_MODE;
  if (changed_power)
    cmd = KELON168_CMD_POWER;
  else if (changed_mode)
    cmd = KELON168_CMD_MODE;
  else if (changed_temp)
    cmd = KELON168_CMD_TEMP;
  else if (changed_fan)
    cmd = KELON168_CMD_FAN;
  else if (changed_swing)
    cmd = KELON168_CMD_SWING;
  this->kelon168_state_.Cmd = cmd;

  // Checksums - see IRKelon168Ac::checksum().
  this->kelon168_state_.Sum1 = this->xor_bytes_(this->kelon168_state_.raw + 2, 13 - 1 - 2);
  this->kelon168_state_.Sum2 = this->xor_bytes_(this->kelon168_state_.raw + 14, 20 - 14);

  std::vector<int32_t> timings;
  // 3 sections: 6 bytes (48 bits) / 8 bytes (64 bits) / 7 bytes (56 bits).
  this->append_bytes_(timings, this->kelon168_state_.raw, 6, true, KELON168_FOOTER_SPACE);
  this->append_bytes_(timings, this->kelon168_state_.raw + 6, 8, false, KELON168_FOOTER_SPACE);
  this->append_bytes_(timings, this->kelon168_state_.raw + 14, 7, false, KELON_GAP);
  ir_send_raw(timings.data(), timings.size());
}

}  // namespace kelon_ac
}  // namespace esphome
