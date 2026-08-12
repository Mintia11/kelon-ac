#include "kelon_ac.h"
#include "esphome/core/log.h"
#include <vector>

namespace esphome {
namespace kelon_ac {

static const char *const TAG = "kelon_ac.climate";

void KelonClimate::setup() {
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->target_temperature = 23;
    this->fan_mode = climate::CLIMATE_FAN_AUTO;
  }

  this->packet_.raw = 0;
  this->packet_.preamble[0] = 0b10000011;
  this->packet_.preamble[1] = 0b00000110;
}

climate::ClimateTraits KelonClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(false);
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY
  });
  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH
  });
  // Control ranges derived from Ranges.control value [16, 31]
  traits.set_visual_min_temperature(16.0); 
  traits.set_visual_max_temperature(31.0); 
  traits.set_visual_temperature_step(1.0);
  return traits;
}

void KelonClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value()) this->target_temperature = *call.get_target_temperature();
  if (call.get_fan_mode().has_value()) this->fan_mode = *call.get_fan_mode();

  this->publish_state();
  this->send_command_();
}

int encode_kelon_signal(const uint8_t *data, size_t length, int32_t *timing_out, size_t max_buf_len);

void KelonClimate::send_command_() {
  if ((this->mode == climate::CLIMATE_MODE_OFF && this->was_on_) || (this->mode != climate::CLIMATE_MODE_OFF && !this->was_on_)) {
    this->packet_.PowerToggle = 1;
    this->was_on_ = !this->was_on_;
  } else {
    this->packet_.PowerToggle = 0;
  }
  
  if (this->mode == climate::CLIMATE_MODE_HEAT) {
    this->packet_.Mode = kKelonModeHeat;
  } else if (this->mode == climate::CLIMATE_MODE_COOL) {
    this->packet_.Mode = kKelonModeCool;
  } else if (this->mode == climate::CLIMATE_MODE_DRY) {
    this->packet_.Mode = kKelonModeDry;
  } else if (this->mode == climate::CLIMATE_MODE_FAN_ONLY) {
    this->packet_.Mode = kKelonModeFan;
  }

  if (this->fan_mode == climate::CLIMATE_FAN_AUTO) {
    this->packet_.Fan = kKelonFanAuto;
  } else if (this->fan_mode == climate::CLIMATE_FAN_LOW) {
    this->packet_.Fan = kKelonFanMin;
  } else if (this->fan_mode == climate::CLIMATE_FAN_MEDIUM) {
    this->packet_.Fan = kKelonFanMedium;
  } else if (this->fan_mode == climate::CLIMATE_FAN_HIGH) {
    this->packet_.Fan = kKelonFanMax;
  }

  this->packet_.Temperature = (uint8_t)this->target_temperature;

  ESP_LOGD(TAG, "Sending command: mode=%d, fan=%d, temp=%d, power_toggle=%d", this->packet_.Mode, this->packet_.Fan, this->packet_.Temperature, this->packet_.PowerToggle);

  int32_t timings[2 + 8 * 2 * sizeof(KelonProtocol)];
  int count = encode_kelon_signal((uint8_t*)&this->packet_.raw, sizeof(KelonProtocol), timings, sizeof(timings)/sizeof(timings[0]));
  if (count < 0) {
    ESP_LOGE(TAG, "Failed to encode Kelon signal");
    return;
  }

  // dump the timings for debugging
  ESP_LOGD(TAG, "Kelon signal timings:");
  for (int i = 0; i < count; i++) {
    ESP_LOGD(TAG, "timings[%d] = %d", i, timings[i]);
  }

  ir_send_raw(timings, count);
  delayMicroseconds(kKelonGap);
}

int encode_kelon_signal(const uint8_t *data, size_t length, int32_t *timing_out, size_t max_buf_len) {
  size_t required_len = 2 + (length * 8 * 2);

  if (max_buf_len < required_len) {
    return -1;
  }

  size_t idx = 0;

  timing_out[idx++] = kKelonHdrMark;
  timing_out[idx++] = kKelonHdrSpace;

  for (size_t b = 0; b < length; b++) {
    uint8_t byte = data[b];
    for (int i = 0; i < 8; i++) {
      timing_out[idx++] = kKelonBitMark;
        
      if (byte & (1 << i)) {
        timing_out[idx++] = kKelonOneSpace;
      } else {
        timing_out[idx++] = kKelonZeroSpace;
      }
    }
  }

  return (int)idx;
}


}  // namespace kelon_ac
}  // namespace esphome