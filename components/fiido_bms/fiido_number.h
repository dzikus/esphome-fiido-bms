#pragma once

#if defined(USE_ESP32) && defined(USE_FIIDO_BMS_DEV)

#include "esphome/components/number/number.h"
#include "esphome/core/helpers.h"
#include "fiido_bms.h"

namespace esphome::fiido_bms {

using FiidoHubFloatSetter = void (FiidoBMSHub::*)(float);

// control() forwards to the hub setter. The hub clamps and writes the raw byte,
// publishing the new value only once the frame went out; a rejected write
// republishes the previous one. BOOST/DISPLAY polls reconcile later.
template <FiidoHubFloatSetter Setter>
class FiidoNumber : public number::Number, public Parented<FiidoBMSHub> {
 public:
  void control(float value) override { (this->parent_->*Setter)(value); }
};

class FiidoBrightnessNumber : public FiidoNumber<&FiidoBMSHub::set_brightness> {};
class FiidoBoostNumber : public FiidoNumber<&FiidoBMSHub::set_boost> {};
class FiidoGuardTimeNumber : public FiidoNumber<&FiidoBMSHub::set_guard_time> {};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32 && USE_FIIDO_BMS_DEV
