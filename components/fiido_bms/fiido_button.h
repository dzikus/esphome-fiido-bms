#pragma once

#if defined(USE_ESP32) && defined(USE_FIIDO_BMS_DEV)

#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"
#include "fiido_bms.h"

namespace esphome::fiido_bms {

// press_action forwards to the hub. The hub reads the ESP32 BLE address and
// sends it as the pairing payload.
class FiidoPairWatchButton : public button::Button, public Parented<FiidoBMSHub> {
 public:
  void press_action() override { this->parent_->pair_watch(); }
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32 && USE_FIIDO_BMS_DEV
