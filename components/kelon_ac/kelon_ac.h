#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include <stdint.h>
#include "ir_tx_rtl8720cf.h" // Includes your RTL8720CF PWM transmitter functions

namespace esphome {
namespace kelon_ac {

union KelonProtocol {
  uint64_t raw;

  struct {
    uint8_t preamble[2];
    uint8_t Fan: 2;
    uint8_t PowerToggle: 1;
    uint8_t SleepEnabled: 1;
    uint8_t DehumidifierGrade: 3;
    uint8_t SwingVToggle: 1;
    uint8_t Mode: 3;
    uint8_t TimerEnabled: 1;
    uint8_t Temperature: 4;
    uint8_t TimerHalfHour: 1;
    uint8_t TimerHours: 6;
    uint8_t SmartModeEnabled: 1;
    uint8_t pad1: 4;
    uint8_t SuperCoolEnabled1: 1;
    uint8_t pad2: 2;
    uint8_t SuperCoolEnabled2: 1;
  };
};

// Constants
const uint8_t kKelonModeHeat = 0;
const uint8_t kKelonModeSmart = 1;  // (temp = 26C, but not shown)
const uint8_t kKelonModeCool = 2;
const uint8_t kKelonModeDry = 3;    // (temp = 25C, but not shown)
const uint8_t kKelonModeFan = 4;    // (temp = 25C, but not shown)
const uint8_t kKelonFanAuto = 0;
const uint8_t kKelonFanMin = 3;
const uint8_t kKelonFanMedium = 2;
const uint8_t kKelonFanMax = 1;
const int8_t kKelonDryGradeMin = -2;
const int8_t kKelonDryGradeMax = +2;
const uint8_t kKelonMinTemp = 18;
const uint8_t kKelonMaxTemp = 32;

const int32_t kKelonHdrMark = 9000;
const int32_t kKelonHdrSpace = -4600;
const int32_t kKelonBitMark = 560;
const int32_t kKelonOneSpace = -1680;
const int32_t kKelonZeroSpace = -600;
const uint32_t kKelonGap = 200000;
const uint16_t kKelonFreq = 38000;
const int32_t kKelonInterHeaderSpace = 7850;

class KelonClimate : public climate::Climate, public Component {
 public:
  void setup() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void send_command_();
  void send_command_raw_(KelonProtocol &packet);
  KelonProtocol packet_;
  uint64_t second_part_ = 0;

  bool was_on_ = false;
};

}  // namespace hisense_ac
}  // namespace esphome