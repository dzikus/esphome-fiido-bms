#pragma once

#ifdef USE_ESP32

#include "esphome/core/helpers.h"
#include "esphome/components/number/number.h"

#include "fiido_bms.h"

namespace esphome {
namespace fiido_bms {

using FiidoHubFloatSetter = void (FiidoBMSHub::*)(float);

// control() forwards to the hub setter. The hub clamps and writes the raw byte,
// publishing the new value only once the frame went out; a rejected write
// republishes the previous one. BOOST/DISPLAY polls reconcile later.
template<FiidoHubFloatSetter Setter>
class FiidoNumber : public number::Number, public Parented<FiidoBMSHub> {
 public:
  void control(float value) override {
    (this->parent_->*Setter)(value);
  }
};

class FiidoBrightnessNumber : public FiidoNumber<&FiidoBMSHub::set_brightness> {};
class FiidoBoostNumber      : public FiidoNumber<&FiidoBMSHub::set_boost> {};
class FiidoGuardTimeNumber  : public FiidoNumber<&FiidoBMSHub::set_guard_time> {};

}  // namespace fiido_bms
}  // namespace esphome

#endif  // USE_ESP32
