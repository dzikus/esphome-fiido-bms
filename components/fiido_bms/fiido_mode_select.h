#pragma once

#ifdef USE_ESP32

#include <vector>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/select/select.h"
#include "fiido_bms.h"

namespace esphome {
namespace fiido_bms {

class FiidoModeSelect : public select::Select, public Parented<FiidoBMSHub> {
 public:
  void control(const std::string &value) override;
  static const std::vector<std::string> MODE_OPTIONS;
};

}  // namespace fiido_bms
}  // namespace esphome

#endif  // USE_ESP32
