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
  switch (this->mode) {
    case climate::CLIMATE_MODE_OFF:
      ir_send_raw_pronto("0000 0073 0000 00ac 0138 009e 0015 003c 0015 003c 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 003c 0015 0015 0015 003c 0015 003c 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 003c 0015 003c 0015 003c 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 003c 0015 0015 0015 003c 0015 003c 0015 003c 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0117 0015 003c 0015 0015 0015 0015 0015 003c 0015 0015 0015 0015 0015 0015 0015 003c 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 003c 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 003c 0015 0015 0015 003c 0015 003c 0015 0015 0015 003c 0015 003c 0015 0117 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 003c 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 003c 0015 0015 0015 003c 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 0015 003c 0015 0015 0015 0015 0015 003c 0015 0015 0015 003c 0015 0015 0015 0015 0015 0e4a");
      break;
  }
}

}  // namespace hisense_ac
}  // namespace esphome