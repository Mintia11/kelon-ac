#include "kelon_ac.h"
#include "esphome/core/log.h"
#include <vector>
#include <string>

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
  this->second_part_ = 0; // Reset second part for each command
}

climate::ClimateTraits KelonClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(false);
  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
  });
  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
      climate::CLIMATE_FAN_QUIET // TODO: Map this to a real fan speed
  });
  traits.set_visual_min_temperature(18.0); 
  traits.set_visual_max_temperature(30.0); 
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

int encode_kelon_signal(const uint8_t *data, size_t length, 
  int32_t *timing_out, size_t max_buf_len, 
  bool has_header, bool has_footer, 
  bool inter_header,
);

void KelonClimate::send_command_() {
  this->second_part_ = 0; // Reset second part for each command

  if ((this->mode == climate::CLIMATE_MODE_OFF && this->was_on_) || (this->mode != climate::CLIMATE_MODE_OFF && !this->was_on_)) {
    this->packet_.PowerToggle = 1;
    this->was_on_ = !this->was_on_;
  } else {
    this->packet_.PowerToggle = 0;
  }
  
  if (this->mode == climate::CLIMATE_MODE_HEAT) {
    this->packet_.Mode = kKelonModeHeat;
  } else if (this->mode == climate::CLIMATE_MODE_COOL || this->mode == climate::CLIMATE_MODE_HEAT_COOL) {
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
  } else if (this->fan_mode == climate::CLIMATE_FAN_QUIET) {
    this->packet_.Fan = kKelonFanMin;
    this->second_part_ = 0b000000111000000000000000000000000000000000000010;
  }

  this->packet_.Temperature = (uint8_t)this->target_temperature;

  ESP_LOGD(TAG, "Sending command: mode=%d, fan=%d, temp=%d, power_toggle=%d", this->packet_.Mode, this->packet_.Fan, this->packet_.Temperature, this->packet_.PowerToggle);

  this->send_command_raw_(this->packet_);
}

void KelonClimate::send_command_raw_(KelonProtocol &packet) {
  int32_t timings[2 + 8 * 12 + 1 + 8 * 12 + 1];
  int count = encode_kelon_signal((uint8_t*)&packet.raw, 6, 
    timings, sizeof(timings)/sizeof(timings[0]), 
    true, false, 
    true,
  );
  if (count < 0) {
    ESP_LOGE(TAG, "Failed to encode Kelon signal");
    return;
  }
  int count2 = encode_kelon_signal((uint8_t*)&this->second_part_, 6, 
    timings + count, sizeof(timings)/sizeof(timings[0]) - count, 
      false, true, 
    false,
  );
  if (count2 < 0) {
    ESP_LOGE(TAG, "Failed to encode Kelon second part signal");
    return;
  }

  int total_count = count + count2;

  // dump the timings for debugging
  ESP_LOGD(TAG, "Kelon signal timings %d:", total_count);
  // split into lines of 10 using a string as buffer
  std::string line;
  for (int i = 0; i < total_count; i++) {
    line += std::to_string(timings[i]) + ", ";
    if ((i + 1) % 10 == 0 || i == total_count - 1) {
      ESP_LOGD(TAG, "%s", line.c_str());
      line.clear();
    }
  }

  ir_send_raw(timings, total_count);
  delayMicroseconds(kKelonGap);
}

int encode_kelon_signal(const uint8_t *data, size_t length, 
  int32_t *timing_out, size_t max_buf_len, 
  bool has_header, bool has_footer, 
  bool inter_header
) {
  size_t required_len = 0;
  if (has_header) {
    required_len += 2; // header mark and space
  }
  required_len += length * 8 * 2; // each bit has a mark and a space
  if (has_footer) {
    required_len += 1; // footer mark
  }
  if (inter_header) {
    required_len += 2; // inter-header space
  }

  if (max_buf_len < required_len) {
    return -1;
  }

  size_t idx = 0;

  if (has_header) {
    timing_out[idx++] = kKelonHdrMark;
    timing_out[idx++] = kKelonHdrSpace;
  }

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

  if (has_footer) {
    timing_out[idx++] = kKelonBitMark; // footer mark
  }

  if (inter_header) {
    timing_out[idx++] = kKelonBitMark;
    timing_out[idx++] = kKelonInterHeaderSpace;
  }

  return (int)idx;
}


}  // namespace kelon_ac
}  // namespace esphome
