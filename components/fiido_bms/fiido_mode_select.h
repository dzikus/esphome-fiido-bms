#pragma once

#ifdef USE_ESP32

#include <string>
#include <vector>

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "fiido_bms.h"

namespace esphome::fiido_bms {

class FiidoModeSelect : public select::Select, public Parented<FiidoBMSHub> {
 public:
  void control(const std::string &value) override;
  static const std::vector<std::string> MODE_OPTIONS;
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
