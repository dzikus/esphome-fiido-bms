#pragma once

#ifdef USE_ESP32

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "fiido_bms.h"

namespace esphome::fiido_bms {

using FiidoHubBoolSetter = void (FiidoBMSHub::*)(bool);

// write_state only. Hub method handles WRITE dispatch and publish_state
// happens later via STATS parse on the next poll.
template <FiidoHubBoolSetter Setter>
class FiidoBoolSwitch : public switch_::Switch, public Parented<FiidoBMSHub> {
 public:
  void write_state(bool state) override { (this->parent_->*Setter)(state); }
};

// Component variant: setup() applies restore_mode on boot and pushes the
// recovered state to the hub flag and HA UI. write_state mirrors the click.
template <FiidoHubBoolSetter Setter>
class FiidoBoolSwitchWithRestore : public switch_::Switch, public Parented<FiidoBMSHub>, public Component {
 public:
  void setup() override {
    bool state = this->get_initial_state_with_restore_mode().value_or(true);
    this->publish_state(state);
    // BLEClient::setup() runs at AFTER_BLUETOOTH (300) and unconditionally sets
    // enabled=true. This switch runs at DATA (600), so calling the setter here
    // would be overwritten. Defer so the restored intent applies after the
    // client is fully set up.
    this->defer([this, state]() { (this->parent_->*Setter)(state); });
  }
  void write_state(bool state) override {
    (this->parent_->*Setter)(state);
    this->publish_state(state);
  }
};

class FiidoMotorSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_motor_enable> {};
class FiidoLightSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_light_enable> {};
class FiidoSpeakerSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_speaker_enable> {};
class FiidoKeySoundSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_key_sound_enable> {};
class FiidoThrottleSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_throttle_enable> {};
class FiidoSlowModeSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_slow_mode_enable> {};

class FiidoBikeGuardSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_bike_guard_enable> {};

#ifdef USE_FIIDO_BMS_DEV
class FiidoCruiseSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_cruise_enable> {};
class FiidoStartModeSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_start_mode_enable> {};
class FiidoInsensitivitySwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_insensitivity_enable> {};
class FiidoShowTotalKmSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_show_total_km_enable> {};
class FiidoAutoScreenOffSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_auto_screen_off_enable> {};
class FiidoRingSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_ring_enable> {};
class FiidoDoubleSpeedSwitch : public FiidoBoolSwitch<&FiidoBMSHub::set_double_speed_enable> {};
#endif

class FiidoAutoshutdownSwitch : public FiidoBoolSwitchWithRestore<&FiidoBMSHub::set_auto_shutdown_enabled> {};
class FiidoBleSwitch : public FiidoBoolSwitchWithRestore<&FiidoBMSHub::set_ble_user_enabled> {};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
