#include "hisense_ac.h"
#include "esphome/core/log.h"
#include <vector>

namespace esphome {
namespace hisense_ac {

static const char *const TAG = "hisense_ac.climate";

void HisenseClimate::setup() {
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->target_temperature = 23;
    this->fan_mode = climate::CLIMATE_FAN_AUTO;
  }
}

climate::ClimateTraits HisenseClimate::traits() {
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

void HisenseClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) this->mode = *call.get_mode();
  if (call.get_target_temperature().has_value()) this->target_temperature = *call.get_target_temperature();
  if (call.get_fan_mode().has_value()) this->fan_mode = *call.get_fan_mode();

  this->publish_state();
  this->send_command_();
}

void HisenseClimate::send_command_() {
  // Generic DG11L1 14-byte payload state
  uint8_t base_code[14] = {0xC3, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    uint8_t off_code[14] = {0xC3, 0x0C, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE3};
    memcpy(base_code, off_code, 14);
  } else {
    uint8_t fan_val = 0x00;
    switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
      case climate::CLIMATE_FAN_LOW:    fan_val = 0x02; break;
      case climate::CLIMATE_FAN_MEDIUM: fan_val = 0x01; break;
      case climate::CLIMATE_FAN_HIGH:   fan_val = 0x00; break;
      default:                          fan_val = 0x05; break; // Auto
    }
    
    base_code[2] = fan_val | 0x80; // Overwrite fan mode

    uint8_t temp = (uint8_t)this->target_temperature;
    uint8_t mode_bits = 0;

    // Map operation modes to binary and enforce forced temps
    switch (this->mode) {
      case climate::CLIMATE_MODE_HEAT:     mode_bits = 0x00; break;
      case climate::CLIMATE_MODE_COOL:     mode_bits = 0x02; break;
      case climate::CLIMATE_MODE_DRY:      mode_bits = 0x03; temp = 23; break; // Forced to 23
      case climate::CLIMATE_MODE_FAN_ONLY: mode_bits = 0x04; temp = 23; break; // Forced to 23
      default: mode_bits = 0x01; temp = 23; break;
    }
    
    if (temp < 16) temp = 16;
    if (temp > 31) temp = 31;

    // Shift temp 4 bits left and append mode bits
    base_code[3] = (((temp - 16) & 0x0F) << 4) | (mode_bits & 0x0F);

    // XOR Checksum calculation for bytes 2 to 12
    uint8_t checksum = 0;
    for (int i = 2; i < 13; i++) {
      checksum ^= base_code[i];
    }
    base_code[13] = checksum;
  }

  std::vector<int32_t> timings;
  timings.reserve(250);

  // Derived Header Marks/Spaces (Converted from RAW_INIT broadlink ticks to microseconds)
  timings.push_back(7695);  
  timings.push_back(-3888); 

  int bit_count = 0;
  // Serialize bits into pulse bursts and spaces (LSB first per byte)
  for (int b = 0; b < 14; b++) {
    uint8_t byte_val = base_code[b];
    for (int bit = 0; bit < 8; bit++) {
      bool bit_val = (byte_val >> bit) & 0x01;

      // Insert Segment Separators into the raw pulse train
      // (104 and 234 in python correspond to bits 52 and 117 respectively)
      if (bit_count == 52 || bit_count == 117) {
        timings.push_back(540);   // EMPTY_TIME pulse (20 * 27us)
        timings.push_back(-6885); // Segment Space (255 * 27us)
      }

      timings.push_back(540); // EMPTY_TIME pulse
      if (bit_val) {
        timings.push_back(-1485); // ONE_TIME space (55 * 27us)
      } else {
        timings.push_back(-540);  // ZERO_TIME space (20 * 27us)
      }
      bit_count++;
    }
  }

  // Trailing pulse and space sequence
  timings.push_back(540); 
  timings.push_back(-89991); 

  // Fire the hardware transmission command
  ir_send_raw(timings.data(), timings.size());
}

}  // namespace hisense_ac
}  // namespace esphome