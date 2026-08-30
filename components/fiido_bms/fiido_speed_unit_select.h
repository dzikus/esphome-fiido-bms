#pragma once

#ifdef USE_ESP32

#include <array>
#include <string>
#include <string_view>

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "fiido_bms.h"

namespace esphome::fiido_bms {

class FiidoSpeedUnitSelect : public select::Select, public Parented<FiidoBMSHub> {
 public:
  void control(const std::string &value) override;
  static constexpr std::array<std::string_view, 2> OPTIONS{"km/h", "mph"};
};

}  // namespace esphome::fiido_bms

#endif  // USE_ESP32
