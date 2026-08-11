#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "ir_tx_rtl8720cf.h" // Includes your RTL8720CF PWM transmitter functions

namespace esphome {
namespace hisense_ac {

class HisenseClimate : public climate::Climate, public Component {
 public:
  void setup() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void send_command_();
};

}  // namespace hisense_ac
}  // namespace esphome