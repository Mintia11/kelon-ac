#pragma once

#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace kelon_ac {

enum KelonProtocolVariant {
  PROTOCOL_KELON = 0,
  PROTOCOL_KELON168 = 1,
};

// --- Kelon (48-bit) native state layout, ported from IRremoteESP8266's
//     ir_Kelon.h `union KelonProtocol`. Bit order/packing must match what
//     GCC produces on the target MCU, which is the same toolchain family
//     IRremoteESP8266 targets, so this is a direct copy rather than a
//     hand-rolled re-encoding. ---
union KelonState {
  uint64_t raw;
  struct {
    uint8_t preamble[2];
    uint8_t Fan : 2;
    uint8_t PowerToggle : 1;
    uint8_t SleepEnabled : 1;
    uint8_t DehumidifierGrade : 3;
    uint8_t SwingVToggle : 1;
    uint8_t Mode : 3;
    uint8_t TimerEnabled : 1;
    uint8_t Temperature : 4;
    uint8_t TimerHalfHour : 1;
    uint8_t TimerHours : 6;
    uint8_t SmartModeEnabled : 1;
    uint8_t pad1 : 4;
    uint8_t SuperCoolEnabled1 : 1;
    uint8_t pad2 : 2;
    uint8_t SuperCoolEnabled2 : 1;
  };
};

// --- Kelon168 (21-byte) native state layout, ported from IRremoteESP8266's
//     ir_Kelon.h `union Kelon168Protocol`. ---
static const uint8_t KELON168_STATE_LENGTH = 21;

union Kelon168State {
  uint8_t raw[KELON168_STATE_LENGTH];
  struct {
    // Byte 0-1
    uint8_t preamble[2];
    // Byte 2
    uint8_t Fan : 2;
    uint8_t Power : 1;
    uint8_t Sleep : 1;
    uint8_t : 3;
    uint8_t Swing1 : 1;
    // Byte 3
    uint8_t Mode : 3;
    uint8_t : 1;
    uint8_t Temp : 4;
    // Byte 4
    uint8_t : 8;
    // Byte 5
    uint8_t : 4;
    uint8_t Super1 : 1;
    uint8_t : 2;
    uint8_t Super2 : 1;
    // Byte 6
    uint8_t ClockHours : 5;
    uint8_t LightOff : 1;
    uint8_t : 2;
    // Byte 7
    uint8_t ClockMins : 6;
    uint8_t : 1;
    uint8_t OffTimerEnabled : 1;
    // Byte 8
    uint8_t OffHours : 5;
    uint8_t : 1;
    uint8_t Swing2 : 1;
    uint8_t : 1;
    // Byte 9
    uint8_t OffMins : 6;
    uint8_t : 1;
    uint8_t OnTimerEnabled : 1;
    // Byte 10
    uint8_t OnHours : 5;
    uint8_t : 3;
    // Byte 11
    uint8_t OnMins : 6;
    uint8_t : 2;
    // Byte 12
    uint8_t : 8;
    // Byte 13
    uint8_t Sum1 : 8;
    // Byte 14
    uint8_t : 8;
    // Byte 15
    uint8_t Cmd : 8;
    // Byte 16
    uint8_t : 1;
    uint8_t Fan2 : 1;
    uint8_t : 6;
    // Byte 17
    uint8_t pad1;
    // Byte 18
    uint8_t Model1 : 4;
    uint8_t On : 1;
    uint8_t Model2 : 3;
    // Byte 19
    uint8_t : 8;
    // Byte 20
    uint8_t Sum2 : 8;
  };
};

class KelonAcClimate : public climate::Climate, public Component {
 public:
  void set_protocol(KelonProtocolVariant protocol) { this->protocol_ = protocol; }

  void setup() override;
  void dump_config() override;
  climate::ClimateTraits traits() override;

 protected:
  void control(const climate::ClimateCall &call) override;

  void transmit_state();
  void transmit_kelon_();
  void transmit_kelon168_();
  // Appends mark/space pairs (mark positive, space negative, per
  // ir_send_raw()'s convention) for `nbytes` LSB-first bytes.
  void append_bytes_(std::vector<int32_t> &out, const uint8_t *bytes, uint8_t nbytes, bool with_header,
                      int32_t footer_space);
  uint8_t xor_bytes_(const uint8_t *bytes, uint8_t len);

  KelonProtocolVariant protocol_{PROTOCOL_KELON168};

  KelonState kelon_state_{};
  Kelon168State kelon168_state_{};

  // Kelon/Kelon168 only support *toggling* certain settings (power, swing) -
  // we need to remember what we last told the AC to know when to toggle.
  bool first_transmit_{true};
  climate::ClimateMode last_mode_{climate::CLIMATE_MODE_OFF};
  float last_target_temperature_{24};
  climate::ClimateFanMode last_fan_mode_{climate::CLIMATE_FAN_AUTO};
  climate::ClimateSwingMode last_swing_mode_{climate::CLIMATE_SWING_OFF};
};

}  // namespace kelon_ac
}  // namespace esphome
